/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "qm_auto_team_lock_adapter.h"

#include <algorithm>

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

SQmAutoTeamLockInput BuildQmAutoTeamLockInput(const CGameClient &GameClient, const IClient &Client)
{
	SQmAutoTeamLockInput Input;
	Input.m_Enabled = g_Config.m_QmAutoTeamLock != 0;
	Input.m_Online = Client.State() == IClient::STATE_ONLINE;
	Input.m_TickSpeed = Client.GameTickSpeed();
	Input.m_DelaySeconds = g_Config.m_QmAutoTeamLockDelay;
	if(!Input.m_Online)
		return Input;

	const int Connection = std::clamp(g_Config.m_ClDummy, 0, NUM_DUMMIES - 1);
	Input.m_Dummy = Connection;
	const int ClientId = GameClient.m_aLocalIds[Connection];
	if(ClientId < 0 || ClientId >= MAX_CLIENTS ||
		GameClient.m_Snap.m_LocalClientId != ClientId || !GameClient.m_Snap.m_pLocalInfo)
		return Input;

	Input.m_LocalPlayerValid = true;
	Input.m_Team = GameClient.m_Teams.Team(ClientId);
	Input.m_TeamCanBeLocked = Input.m_Team > TEAM_FLOCK && Input.m_Team < TEAM_SUPER;
	Input.m_CurrentTick = Client.GameTick(Connection);
	return Input;
}

void ApplyQmAutoTeamLockAction(CGameClient &GameClient, const SQmAutoTeamLockAction &Action)
{
	if(Action.m_SendLockCommand)
		GameClient.m_Chat.SendChat(0, "/lock 1");
}
