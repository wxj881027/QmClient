#include "water_hammer_indicator.h"

#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/collision.h>
#include <game/gamecore.h>

#include <algorithm>

void CQmWaterHammerIndicator::OnReset()
{
	std::fill(m_aInputs.begin(), m_aInputs.end(), SInputState{});
}

void CQmWaterHammerIndicator::OnNewSnapshot()
{
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(!GameClient()->m_aClients[ClientId].m_Active)
			m_aInputs[ClientId] = SInputState{};
	}
}

void CQmWaterHammerIndicator::OnPreInput(const CNetMsg_Sv_PreInput &Message)
{
	if(Message.m_Owner < 0 || Message.m_Owner >= MAX_CLIENTS)
		return;

	SInputState &Input = m_aInputs[Message.m_Owner];
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

bool CQmWaterHammerIndicator::IsClientInPenaltyArea(const int ClientId) const
{
	const CGameClient::CClientData &Client = GameClient()->m_aClients[ClientId];
	// 以当前渲染位置的地图死亡/冻结判定为准，避免把普通冻结状态误当成水域。
	return IsInPenaltyArea(Client.m_RenderPos);
}

bool CQmWaterHammerIndicator::IsMarked(const int ClientId) const
{
	if(!g_Config.m_QmWaterHammerHighlight || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;

	const int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalClientId < 0 || LocalClientId >= MAX_CLIENTS || ClientId == LocalClientId)
		return false;

	const CGameClient::CClientData &Client = GameClient()->m_aClients[ClientId];
	if(!Client.m_Active || Client.m_Spec || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		return false;
	if(GameClient()->m_Teams.Team(ClientId) != GameClient()->m_Teams.Team(LocalClientId))
		return false;

	const SInputState &Input = m_aInputs[ClientId];
	const bool HammerRequested = Input.m_WantedWeapon == WEAPON_HAMMER + 1 || Client.m_RenderCur.m_Weapon == WEAPON_HAMMER;
	const bool FireHeld = (Input.m_Fire & 1) != 0;
	return Input.m_Received && QmShouldMarkWaterHammer(IsClientInPenaltyArea(ClientId), HammerRequested, FireHeld);
}
