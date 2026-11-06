#include "water_hammer_indicator.h"

#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/collision.h>
#include <game/gamecore.h>

#include <algorithm>

void CQmWaterHammerIndicator::OnReset()
{
	std::fill(m_aInputs.begin(), m_aInputs.end(), SQmWaterHammerInputState{});
}

void CQmWaterHammerIndicator::OnNewSnapshot()
{
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(!CanUseRemoteInput(ClientId))
			m_aInputs[ClientId] = SQmWaterHammerInputState{};
	}
}

void CQmWaterHammerIndicator::OnPreInput(const CNetMsg_Sv_PreInput &Message)
{
	if(Message.m_Owner < 0 || Message.m_Owner >= MAX_CLIENTS)
		return;

	m_aInputs[Message.m_Owner].Update(Message);
}

bool CQmWaterHammerIndicator::CanUseRemoteInput(const int ClientId) const
{
	if(Client()->State() != IClient::STATE_ONLINE || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;

	const int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalClientId < 0 || LocalClientId >= MAX_CLIENTS || ClientId == LocalClientId)
		return false;

	const auto &ClientData = GameClient()->m_aClients[ClientId];
	return ClientData.m_Active && !ClientData.m_Spec &&
		!GameClient()->m_aClients[LocalClientId].m_Spec &&
		GameClient()->m_Snap.m_aCharacters[LocalClientId].m_Active &&
		GameClient()->m_Snap.m_aCharacters[ClientId].m_Active &&
		GameClient()->m_Teams.Team(ClientId) == GameClient()->m_Teams.Team(LocalClientId);
}

SQmWaterDirectionState CQmWaterHammerIndicator::DirectionInputs(const int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return {};

	const auto &Character = GameClient()->m_Snap.m_aCharacters[ClientId];
	const auto &ClientData = GameClient()->m_aClients[ClientId];
	// 方向箭头沿用“显示按键”设置，不受卡锤、卡钩高亮开关影响。
	return QmResolveWaterDirection(ClientData.m_FreezeEnd, ClientData.m_LiveFrozen,
		Character.m_Cur.m_Direction, Character.m_Cur.m_Jumped,
		CanUseRemoteInput(ClientId) ? m_aInputs[ClientId] : SQmWaterHammerInputState{});
}

bool CQmWaterHammerIndicator::IsInPenaltyArea(const vec2 Position) const
{
	const CCollision *pCollision = Collision();
	const float SampleRadius = CCharacterCore::PhysicalSize() / 3.0f;
	const vec2 aSamples[] = {
		vec2(0.0f, 0.0f),
		vec2(-SampleRadius, -SampleRadius),
		vec2(SampleRadius, -SampleRadius),
		vec2(-SampleRadius, SampleRadius),
		vec2(SampleRadius, SampleRadius),
	};

	for(const vec2 Sample : aSamples)
	{
		const int MapIndex = pCollision->GetPureMapIndex(Position + Sample);
		if(QmIsWaterHammerPenaltyTile(pCollision->GetTileIndex(MapIndex)) ||
			QmIsWaterHammerPenaltyTile(pCollision->GetFrontTileIndex(MapIndex)) ||
			QmIsWaterHammerPenaltyTile(pCollision->GetSwitchType(MapIndex)))
		{
			return true;
		}
	}

	return false;
}

SQmWaterHeldState CQmWaterHammerIndicator::MarkedInputs(const int ClientId) const
{
	if(!g_Config.m_QmWaterHammerHighlight || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return {};

	const int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalClientId < 0 || LocalClientId >= MAX_CLIENTS || ClientId == LocalClientId)
		return {};

	const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
	if(!ClientData.m_Active || ClientData.m_Spec || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		return {};
	if(GameClient()->m_Teams.Team(ClientId) != GameClient()->m_Teams.Team(LocalClientId))
		return {};

	const CNetObj_PlayerInput *pLocalInput = nullptr;
	if(Client()->DummyConnected() && ClientId == GameClient()->m_aLocalIds[!g_Config.m_ClDummy])
		pLocalInput = g_Config.m_ClDummyHammer ? &GameClient()->m_HammerInput : &GameClient()->m_DummyInput;

	return {
		QmShouldMarkWaterHammer(ClientData.m_FreezeEnd, ClientData.m_LiveFrozen, ClientData.m_RenderCur.m_Weapon, m_aInputs[ClientId], pLocalInput),
		QmShouldMarkWaterHook(ClientData.m_FreezeEnd, ClientData.m_LiveFrozen, m_aInputs[ClientId], pLocalInput),
	};
}

bool CQmWaterHammerIndicator::IsMarked(const int ClientId) const
{
	return MarkedInputs(ClientId).m_Hammer;
}
