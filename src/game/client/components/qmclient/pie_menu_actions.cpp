#include <game/client/components/pie_menu.h>

#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/friends.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

void CPieMenu::ExecuteTeamAction(EMenuOption Option)
{
	const bool UseDummy = g_Config.m_ClDummy && Client()->DummyConnected();
	const int LocalClientId = GameClient()->m_aLocalIds[UseDummy ? 1 : 0];
	if(LocalClientId < 0 || LocalClientId >= MAX_CLIENTS || !HasTargetPlayer())
		return;
	const int TargetTeam = GameClient()->m_Teams.Team(m_TargetClientId);
	const auto Status = qm_pie_menu::TeamActionStatus(Option, GameClient()->m_GameInfo.m_DDRaceTeam,
		GameClient()->m_Teams.Team(LocalClientId), TargetTeam, GameClient()->m_Teams.TeamSuper());
	switch(Status)
	{
	case qm_pie_menu::ETeamActionStatus::UNSUPPORTED:
		GameClient()->Echo(Localize("This server does not support race teams"), true);
		return;
	case qm_pie_menu::ETeamActionStatus::LOCAL_NEEDS_TEAM:
		GameClient()->Echo(Localize("Join a team before inviting another player"), true);
		return;
	case qm_pie_menu::ETeamActionStatus::TARGET_NEEDS_TEAM:
		GameClient()->Echo(Localize("Cannot join the target player's team"), true);
		return;
	case qm_pie_menu::ETeamActionStatus::ALREADY_TOGETHER:
		GameClient()->Echo(Localize("You are already in the same team"), true);
		return;
	case qm_pie_menu::ETeamActionStatus::READY:
		break;
	}
	const std::string Command = Option == EMenuOption::INVITE_TEAM ?
		qm_pie_menu::QuotedPlayerCommand("/invite", m_TargetName.c_str()) : "/team " + std::to_string(TargetTeam);
	GameClient()->m_Chat.SendChat(0, Command.c_str());
}

void CPieMenu::RequestTargetPoints()
{
	if(!HasTargetPlayer())
		return;
	// 保存名字而非客户端 ID，目标离开、换服或 ID 被复用后结果仍属于原玩家。
	m_PointsRequest.Start(m_TargetName.c_str(), static_cast<double>(time_get()) / time_freq());
	UpdatePointsRequest();
	if(!m_PointsRequest.Name().empty())
	{
		char aMessage[192];
		str_format(aMessage, sizeof(aMessage), Localize("Querying points for %s..."), m_PointsRequest.Name().c_str());
		GameClient()->Echo(aMessage, true);
	}
}

void CPieMenu::UpdatePointsRequest()
{
	if(m_PointsRequest.Name().empty())
		return;
	CPlayerPoints &Points = GameClient()->m_PlayerPoints;
	const char *pName = m_PointsRequest.Name().c_str();
	Points.EnsureQueried(pName);
	const auto Notification = m_PointsRequest.Poll(pName, Points.GetPoints(pName), static_cast<double>(time_get()) / time_freq());
	if(!Notification.has_value())
		return;
	char aMessage[192];
	if(Notification->m_Result.m_Status == EPointsStatus::READY)
		str_format(aMessage, sizeof(aMessage), Localize("%s: %d points"), Notification->m_Name.c_str(), Notification->m_Result.m_Points);
	else
		str_format(aMessage, sizeof(aMessage), Localize("Could not query points for %s"), Notification->m_Name.c_str());
	GameClient()->Echo(aMessage, true);
}

bool CPieMenu::FormatTargetScore(char *pBuffer, size_t BufferSize) const
{
	if(!HasTargetPlayer() || pBuffer == nullptr || BufferSize == 0)
		return false;
	const SPlayerPointsResult Result = GameClient()->m_PlayerPoints.GetPoints(m_TargetName.c_str());
	switch(Result.m_Status)
	{
	case EPointsStatus::READY:
		str_format(pBuffer, BufferSize, Localize("%d points"), Result.m_Points);
		return true;
	case EPointsStatus::FAILED:
		str_copy(pBuffer, Localize("Points unavailable"), BufferSize);
		return true;
	case EPointsStatus::FETCHING:
		str_copy(pBuffer, Localize("Querying points..."), BufferSize);
		return true;
	case EPointsStatus::NOT_REQUESTED:
		if(m_PointsRequest.Name() == m_TargetName)
		{
			str_copy(pBuffer, Localize("Querying points..."), BufferSize);
			return true;
		}
		return false;
	}
	return false;
}

void CPieMenu::ExecuteRenameOption(int RenameIndex)
{
	if(RenameIndex < 0 || RenameIndex >= (int)m_vRenameQueue.size())
		return;

	const char *pNewName = m_vRenameQueue[RenameIndex].c_str();
	if(!pNewName || pNewName[0] == '\0')
		return;

	const bool UseDummy = g_Config.m_ClDummy && Client()->DummyConnected();
	char *pConfigName = UseDummy ? g_Config.m_ClDummyName : g_Config.m_PlayerName;
	const int ConfigNameSize = UseDummy ? (int)sizeof(g_Config.m_ClDummyName) : (int)sizeof(g_Config.m_PlayerName);

	str_copy(pConfigName, pNewName, ConfigNameSize);
	if(UseDummy)
		GameClient()->SendDummyInfo(false);
	else
		GameClient()->SendInfo(false);

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "已切换名字: %s%s", pConfigName, UseDummy ? " (分身)" : "");
	GameClient()->m_Chat.AddLine(-2, 0, aBuf);
}

void CPieMenu::ExecuteOption(EMenuOption Option)
{
	if(!HasTargetPlayer())
		return;

	const char *pPlayerName = GameClient()->m_aClients[m_TargetClientId].m_aName;
	const char *pPlayerClan = GameClient()->m_aClients[m_TargetClientId].m_aClan;

	switch(Option)
	{
	case EMenuOption::FRIEND:
	{
		// Toggle friend status using console command (more reliable)
		char aBuf[256];
		if(GameClient()->m_aClients[m_TargetClientId].m_Friend)
		{
			str_format(aBuf, sizeof(aBuf), "remove_friend \"%s\" \"%s\"", pPlayerName, pPlayerClan);
			Console()->ExecuteLine(aBuf);

			char aMsg[128];
			str_format(aMsg, sizeof(aMsg), Localize("Removed %s from friends"), pPlayerName);
			GameClient()->m_Chat.AddLine(-2, 0, aMsg);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "add_friend \"%s\" \"%s\"", pPlayerName, pPlayerClan);
			Console()->ExecuteLine(aBuf);

			char aMsg[128];
			str_format(aMsg, sizeof(aMsg), Localize("Added %s as friend"), pPlayerName);
			GameClient()->m_Chat.AddLine(-2, 0, aMsg);
		}
		break;
	}
	case EMenuOption::WHISPER:
	{
		// Open chat with whisper command
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "/w \"%s\" ", pPlayerName);
		GameClient()->m_Chat.EnableMode(0);
		GameClient()->m_Chat.m_Input.Set(aBuf);
		break;
	}
	case EMenuOption::MENTION:
	{
		// Insert player name in chat (for @mention)
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s: ", pPlayerName);
		GameClient()->m_Chat.EnableMode(0);
		GameClient()->m_Chat.m_Input.Set(aBuf);
		break;
	}
	case EMenuOption::COPY_SKIN:
	{
		// Copy player skin to local config (supports both main player and dummy)
		const auto &TargetClient = GameClient()->m_aClients[m_TargetClientId];
		const bool IsDummy = g_Config.m_ClDummy != 0;

		// Copy skin name to appropriate config
		if(IsDummy)
		{
			str_copy(g_Config.m_ClDummySkin, TargetClient.m_aSkinName, sizeof(g_Config.m_ClDummySkin));

			// Copy custom colors if used
			if(TargetClient.m_UseCustomColor)
			{
				g_Config.m_ClDummyUseCustomColor = 1;
				g_Config.m_ClDummyColorBody = TargetClient.m_ColorBody;
				g_Config.m_ClDummyColorFeet = TargetClient.m_ColorFeet;
			}
			else
			{
				g_Config.m_ClDummyUseCustomColor = 0;
			}
		}
		else
		{
			str_copy(g_Config.m_ClPlayerSkin, TargetClient.m_aSkinName, sizeof(g_Config.m_ClPlayerSkin));

			// Copy custom colors if used
			if(TargetClient.m_UseCustomColor)
			{
				g_Config.m_ClPlayerUseCustomColor = 1;
				g_Config.m_ClPlayerColorBody = TargetClient.m_ColorBody;
				g_Config.m_ClPlayerColorFeet = TargetClient.m_ColorFeet;
			}
			else
			{
				g_Config.m_ClPlayerUseCustomColor = 0;
			}
		}

		// Send skin change to server
		if(IsDummy)
			GameClient()->SendDummyInfo(false);
		else
			GameClient()->SendInfo(false);

		// Show notification
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "已复制 %s 的皮肤%s", pPlayerName, IsDummy ? " (分身)" : "");
		GameClient()->m_Chat.AddLine(-2, 0, aBuf);
		break;
	}
	case EMenuOption::SWAP:
	{
		// Check if target is in the same team
		const int LocalClientId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
		int LocalTeam = GameClient()->m_Teams.Team(LocalClientId);
		int TargetTeam = GameClient()->m_Teams.Team(m_TargetClientId);

		if(LocalTeam != TargetTeam)
		{
			GameClient()->m_Chat.AddLine(-2, 0, Localize("Cannot swap: the other player is not in your team"));
			break;
		}

		// Execute swap command with player name
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "/swap \"%s\"", pPlayerName);
		GameClient()->m_Chat.SendChat(0, aBuf);
		break;
	}
	case EMenuOption::SPECTATE:
	{
		// Spectate the player
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "/spec \"%s\"", pPlayerName);
		GameClient()->m_Chat.SendChat(0, aBuf);
		break;
	}
	case EMenuOption::INVITE_TEAM:
	case EMenuOption::JOIN_TEAM:
		ExecuteTeamAction(Option);
		break;
	case EMenuOption::FOLLOW:
		ToggleTargetFollow();
		break;
	case EMenuOption::SCORE:
		RequestTargetPoints();
		break;
	default:
		break;
	}
}
