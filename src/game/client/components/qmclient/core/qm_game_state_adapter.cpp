/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "qm_game_state_adapter.h"

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

bool QmPlayerIndicatorAvailable(const CGameClient &GameClient, const IClient &Client)
{
	return Client.State() != IClient::STATE_DEMOPLAYBACK && GameClient.m_GameInfo.m_Race && GameClient.m_Camera.ZoomAllowed();
}

SQmPlayerIndicatorFrame BuildQmPlayerIndicatorFrame(const CGameClient &GameClient, const IClient &Client, const IGraphics &Graphics)
{
	SQmPlayerIndicatorFrame Frame;
	const bool Available = QmPlayerIndicatorAvailable(GameClient, Client);
	Frame.m_Settings.m_Enabled = g_Config.m_QmPlayerIndicator != 0 && Available;
	Frame.m_Settings.m_TeamOnly = g_Config.m_QmPlayerIndicatorTeamOnly != 0;
	Frame.m_Settings.m_FrozenOnly = g_Config.m_QmPlayerIndicatorFrozenOnly != 0;
	Frame.m_Settings.m_HideVisible = g_Config.m_QmPlayerIndicatorHideVisible != 0;
	Frame.m_Settings.m_VariableDistance = g_Config.m_QmPlayerIndicatorVariableDistance != 0;
	Frame.m_Settings.m_Offset = g_Config.m_QmPlayerIndicatorOffset;
	Frame.m_Settings.m_OffsetMax = g_Config.m_QmPlayerIndicatorOffsetMax;
	Frame.m_Settings.m_MaxDistance = g_Config.m_QmPlayerIndicatorMaxDistance;
	Frame.m_Settings.m_Radius = g_Config.m_QmPlayerIndicatorRadius;
	Frame.m_Settings.m_Opacity = g_Config.m_QmPlayerIndicatorOpacity;
	Frame.m_Settings.m_AliveColor = g_Config.m_QmPlayerIndicatorAliveColor;
	Frame.m_Settings.m_FrozenColor = g_Config.m_QmPlayerIndicatorFrozenColor;
	Frame.m_Settings.m_UseTees = g_Config.m_QmPlayerIndicatorUseTees != 0;
	Frame.m_Screen = Graphics.GetScreen();

	const int LocalClientId = GameClient.m_Snap.m_LocalClientId;
	if(LocalClientId < 0 || LocalClientId >= MAX_CLIENTS || !GameClient.m_Snap.m_aCharacters[LocalClientId].m_Active)
		return Frame;

	const CGameClient::CClientData &LocalClient = GameClient.m_aClients[LocalClientId];
	Frame.m_LocalPosition = LocalClient.m_RenderPos;
	Frame.m_LocalTeam = LocalClient.m_Team;
	Frame.m_LocalRaceTeam = GameClient.m_Teams.Team(LocalClientId);
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(!GameClient.m_Snap.m_apPlayerInfos[ClientId])
			continue;

		const CGameClient::CClientData &OtherClient = GameClient.m_aClients[ClientId];
		SQmPlayerIndicatorPlayer &Player = Frame.m_aPlayers[ClientId];
		Player.m_Position = OtherClient.m_RenderPos;
		Player.m_Team = OtherClient.m_Team;
		Player.m_Active = GameClient.m_Snap.m_aCharacters[ClientId].m_Active;
		Player.m_IsLocal = ClientId == LocalClientId || ClientId == GameClient.m_aLocalIds[0] || ClientId == GameClient.m_aLocalIds[1];
		Player.m_Spectator = OtherClient.m_Spec;
		Player.m_Frozen = OtherClient.m_FreezeEnd != 0 || OtherClient.m_DeepFrozen || OtherClient.m_LiveFrozen;
		Player.m_pRenderInfo = &OtherClient.m_RenderInfo;
		Player.m_Emote = OtherClient.m_RenderCur.m_Emote;
	}
	return Frame;
}
