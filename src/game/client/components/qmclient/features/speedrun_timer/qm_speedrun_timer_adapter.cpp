/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "qm_speedrun_timer_adapter.h"

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

SQmSpeedrunTimerInput BuildQmSpeedrunTimerInput(const CGameClient &GameClient, const IClient &Client)
{
	SQmSpeedrunTimerInput Input;
	const bool Online = Client.State() == IClient::STATE_ONLINE;
	Input.m_Enabled = g_Config.m_QmSpeedrunTimer != 0;
	Input.m_AutoDisable = g_Config.m_QmSpeedrunTimerAutoDisable != 0;
	Input.m_CanRequestKill = Online;
	Input.m_TickSpeed = Client.GameTickSpeed();
	if(!Online || !GameClient.m_Snap.m_pGameInfoObj || !GameClient.m_Snap.m_pLocalCharacter)
		return Input;

	Input.m_HasLocalCharacter = true;
	Input.m_RaceStarted = (GameClient.m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME) != 0 &&
		GameClient.m_Snap.m_pGameInfoObj->m_WarmupTimer < 0;
	Input.m_CurrentTick = Client.GameTick(g_Config.m_ClDummy);
	Input.m_StartTick = -static_cast<int64_t>(GameClient.m_Snap.m_pGameInfoObj->m_WarmupTimer);
	Input.m_DurationMilliseconds = QmSpeedrunTimerConfiguredDurationMilliseconds(
		g_Config.m_QmSpeedrunTimerHours,
		g_Config.m_QmSpeedrunTimerMinutes,
		g_Config.m_QmSpeedrunTimerSeconds,
		g_Config.m_QmSpeedrunTimerMilliseconds);
	if(Input.m_DurationMilliseconds <= 0)
		Input.m_DurationMilliseconds = QmSpeedrunTimerLegacyDurationMilliseconds(g_Config.m_QmSpeedrunTimerTime);
	return Input;
}

void ApplyQmSpeedrunTimerAction(CGameClient &GameClient, const SQmSpeedrunTimerAction &Action)
{
	if(Action.m_RequestKill)
		GameClient.SendKill();
	if(Action.m_Disable)
		g_Config.m_QmSpeedrunTimer = 0;
}
