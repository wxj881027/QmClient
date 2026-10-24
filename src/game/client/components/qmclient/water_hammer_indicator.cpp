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
		if(!GameClient()->m_aClients[ClientId].m_Active)
			m_aInputs[ClientId] = SQmWaterHammerInputState{};
	}
}

void CQmWaterHammerIndicator::OnPreInput(const CNetMsg_Sv_PreInput &Message)
{
	if(Message.m_Owner < 0 || Message.m_Owner >= MAX_CLIENTS)
		return;

	SQmWaterHammerInputState &Input = m_aInputs[Message.m_Owner];
	Input.m_Received = true;
	Input.m_WantedWeapon = Message.m_WantedWeapon;
	Input.m_Fire = Message.m_Fire;
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

bool CQmWaterHammerIndicator::IsMarked(const int ClientId) const
{
	if(!g_Config.m_QmWaterHammerHighlight || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;

	const int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalClientId < 0 || LocalClientId >= MAX_CLIENTS || ClientId == LocalClientId)
		return false;

	const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
	if(!ClientData.m_Active || ClientData.m_Spec || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		return false;
	if(GameClient()->m_Teams.Team(ClientId) != GameClient()->m_Teams.Team(LocalClientId))
		return false;

	const CNetObj_PlayerInput *pLocalInput = nullptr;
	if(Client()->DummyConnected() && ClientId == GameClient()->m_aLocalIds[!g_Config.m_ClDummy])
		pLocalInput = g_Config.m_ClDummyHammer ? &GameClient()->m_HammerInput : &GameClient()->m_DummyInput;

	return QmShouldMarkWaterHammer(ClientData.m_FreezeEnd, ClientData.m_LiveFrozen, ClientData.m_RenderCur.m_Weapon, m_aInputs[ClientId], pLocalInput);
}
