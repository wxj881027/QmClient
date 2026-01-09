#include "tclient.h"

#include "data_version.h"

#include <base/log.h>

#include <engine/client.h>
#include <engine/client/enums.h>
#include <engine/external/regex.h>
#include <engine/external/tinyexpr.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/components/chat.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/version.h>

static constexpr const char *TCLIENT_INFO_URL = "https://update.tclient.app/info.json";

CTClient::CTClient()
{
	OnReset();
}

void CTClient::ConRandomTee(IConsole::IResult *pResult, void *pUserData) {}

void CTClient::ConchainRandomColor(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	// Resolve type to randomize
	// Check length of type (0 = all, 1 = body, 2 = feet, 3 = skin, 4 = flag)
	bool RandomizeBody = false;
	bool RandomizeFeet = false;
	bool RandomizeSkin = false;
	bool RandomizeFlag = false;

	if(pResult->NumArguments() == 0)
	{
		RandomizeBody = true;
		RandomizeFeet = true;
		RandomizeSkin = true;
		RandomizeFlag = true;
	}
	else if(pResult->NumArguments() == 1)
	{
		const char *Type = pResult->GetString(0);
		int Length = Type ? str_length(Type) : 0;
		if(Length == 1 && Type[0] == '0')
		{ // Randomize all
			RandomizeBody = true;
			RandomizeFeet = true;
			RandomizeSkin = true;
			RandomizeFlag = true;
		}
		else if(Length == 1)
		{
			// Randomize body
			RandomizeBody = Type[0] == '1';
		}
		else if(Length == 2)
		{
			// Check for body and feet
			RandomizeBody = Type[0] == '1';
			RandomizeFeet = Type[1] == '1';
		}
		else if(Length == 3)
		{
			// Check for body, feet and skin
			RandomizeBody = Type[0] == '1';
			RandomizeFeet = Type[1] == '1';
			RandomizeSkin = Type[2] == '1';
		}
		else if(Length == 4)
		{
			// Check for body, feet, skin and flag
			RandomizeBody = Type[0] == '1';
			RandomizeFeet = Type[1] == '1';
			RandomizeSkin = Type[2] == '1';
			RandomizeFlag = Type[3] == '1';
		}
	}

	if(RandomizeBody)
		RandomBodyColor();
	if(RandomizeFeet)
		RandomFeetColor();
	if(RandomizeSkin)
		RandomSkin(pUserData);
	if(RandomizeFlag)
		RandomFlag(pUserData);
	pThis->GameClient()->SendInfo(false);
}

void CTClient::OnInit()
{
	TextRender()->SetCustomFace(g_Config.m_TcCustomFont);
	m_pGraphics = Kernel()->RequestInterface<IEngineGraphics>();
	FetchTClientInfo();

	char aError[512] = "";
	// 先在 tclient/ 目录找，找不到再返回上一级目录找
	if(!Storage()->FileExists("tclient/gui_logo.png", IStorage::TYPE_ALL) &&
	   !Storage()->FileExists("gui_logo.png", IStorage::TYPE_ALL))
		str_format(aError, sizeof(aError), TCLocalize("%s not found", DATA_VERSION_PATH), "data/tclient/gui_logo.png");
	if(aError[0] == '\0')
		CheckDataVersion(aError, sizeof(aError), Storage()->OpenFile(DATA_VERSION_PATH, IOFLAG_READ, IStorage::TYPE_ALL));
	if(aError[0] != '\0')
	{
		SWarning Warning(aError, TCLocalize("喜报!您可能仅安装了需要DDNet.exe文件，请使用完整的TClient文件夹", "data_version.h"));
		Client()->AddWarning(Warning);
	}
}

static bool LineShouldHighlight(const char *pLine, const char *pName)
{
	const char *pHL = str_utf8_find_nocase(pLine, pName);
	if(pHL)
	{
		int Length = str_length(pName);
		if(Length > 0 && (pLine == pHL || pHL[-1] == ' ') && (pHL[Length] == 0 || pHL[Length] == ' ' || pHL[Length] == '.' || pHL[Length] == '!' || pHL[Length] == ',' || pHL[Length] == '?' || pHL[Length] == ':'))
			return true;
	}
	return false;
}

bool CTClient::SendNonDuplicateMessage(int Team, const char *pLine)
{
	if(str_comp(pLine, m_PreviousOwnMessage) != 0)
	{
		GameClient()->m_Chat.SendChat(Team, pLine);
		return true;
	}
	str_copy(m_PreviousOwnMessage, pLine);
	return false;
}

void CTClient::OnMessage(int MsgType, void *pRawMsg)
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;

	if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;
		int ClientId = pMsg->m_ClientId;

		if(ClientId < 0 || ClientId > MAX_CLIENTS)
			return;
		int LocalId = GameClient()->m_Snap.m_LocalClientId;
		if(ClientId == LocalId)
			str_copy(m_PreviousOwnMessage, pMsg->m_pMessage);

		// === 复读功能: 保存最新的公屏消息 ===
		if(ClientId >= 0 && ClientId < MAX_CLIENTS && pMsg->m_Team == 0)
		{
			// 保存最新的公屏消息（不是自己发的）
			if(ClientId != LocalId && pMsg->m_pMessage[0] != '/')
			{
				str_copy(m_aLastChatMessage, pMsg->m_pMessage, sizeof(m_aLastChatMessage));
			}
		}

		// === 恰分功能 ===
		if(g_Config.m_QmQiaFenEnabled && ClientId != LocalId && pMsg->m_Team == 0)
		{
			// 检查是否是公屏消息且不是自己发的
			const char *pMessage = pMsg->m_pMessage;
			if(
				str_find_nocase(pMessage, "有人恰吗")||
				str_find_nocase(pMessage, "有人要吗")||
				str_find_nocase(pMessage,"有恰的吗")||
				str_find_nocase(pMessage,"有恰吗")||
				str_find_nocase(pMessage,"有要吗")||
				str_find_nocase(pMessage,"有人要分吗")||
				str_find_nocase(pMessage,"有人恰分吗")||
				str_find_nocase(pMessage,"有要的吗")||
				str_find_nocase(pMessage,"有恰分的吗")||
				str_find_nocase(pMessage,"有要分的吗")||
				str_find_nocase(pMessage,"有要的吗")||
				str_find_nocase(pMessage,"有要的吗?")||
				str_find_nocase(pMessage, "谁要")||
				str_find_nocase(pMessage,"谁恰")||
				str_find_nocase(pMessage,"恰分有无")||
				str_find_nocase(pMessage,"恰分的有吗")||
				str_find_nocase(pMessage,"要分的有吗")
				)
			{
				// 发送回复
				GameClient()->m_Chat.SendChat(0, "我要恰!!谢谢佬!!!!");
				
				// 在名字后面加"恰"
				char aNewName[MAX_NAME_LENGTH];
				const char *pCurrentName = g_Config.m_PlayerName;
				
				// 检查名字是否已经以"恰"结尾
				int NameLen = str_length(pCurrentName);
				bool AlreadyHasQia = false;
				
				// 检查最后一个字符是否是"恰"（UTF-8：0xE6 0x81 0xB0）
				if(NameLen >= 3 && 
				   (unsigned char)pCurrentName[NameLen-3] == 0xE6 && 
				   (unsigned char)pCurrentName[NameLen-2] == 0x81 && 
				   (unsigned char)pCurrentName[NameLen-1] == 0xB0)
				{
					AlreadyHasQia = true;
				}
				
				if(!AlreadyHasQia && NameLen + 3 < (int)sizeof(aNewName))
				{
					str_copy(aNewName, pCurrentName, sizeof(aNewName));
					str_append(aNewName, "恰", sizeof(aNewName));
					str_copy(g_Config.m_PlayerName, aNewName, sizeof(g_Config.m_PlayerName));
					GameClient()->SendInfo(false);
				}
			}
		}

		bool PingMessage = false;

		bool ValidIds = !(GameClient()->m_aLocalIds[0] < 0 || (GameClient()->Client()->DummyConnected() && GameClient()->m_aLocalIds[1] < 0));

		if(ValidIds && ClientId >= 0 && ClientId != GameClient()->m_aLocalIds[0] && (!GameClient()->Client()->DummyConnected() || ClientId != GameClient()->m_aLocalIds[1]))
		{
			PingMessage |= LineShouldHighlight(pMsg->m_pMessage, GameClient()->m_aClients[GameClient()->m_aLocalIds[0]].m_aName);
			PingMessage |= GameClient()->Client()->DummyConnected() && LineShouldHighlight(pMsg->m_pMessage, GameClient()->m_aClients[GameClient()->m_aLocalIds[1]].m_aName);
		}

		if(pMsg->m_Team == TEAM_WHISPER_RECV)
			PingMessage = true;

		if(!PingMessage)
			return;

		char aPlayerName[MAX_NAME_LENGTH];
		str_copy(aPlayerName, GameClient()->m_aClients[ClientId].m_aName, sizeof(aPlayerName));

		bool PlayerMuted = GameClient()->m_aClients[ClientId].m_Foe || GameClient()->m_aClients[ClientId].m_ChatIgnore;
		if(g_Config.m_TcAutoReplyMuted && PlayerMuted)
		{
			char aBuf[256];
			if(pMsg->m_Team == TEAM_WHISPER_RECV || ServerCommandExists("w"))
				str_format(aBuf, sizeof(aBuf), "/w %s %s", aPlayerName, g_Config.m_TcAutoReplyMutedMessage);
			else
				str_format(aBuf, sizeof(aBuf), "%s: %s", aPlayerName, g_Config.m_TcAutoReplyMutedMessage);
			SendNonDuplicateMessage(0, aBuf);
			return;
		}

		bool WindowActive = m_pGraphics && m_pGraphics->WindowActive();
		if(g_Config.m_TcAutoReplyMinimized && !WindowActive && m_pGraphics)
		{
			char aBuf[256];
			if(pMsg->m_Team == TEAM_WHISPER_RECV || ServerCommandExists("w"))
				str_format(aBuf, sizeof(aBuf), "/w %s %s", aPlayerName, g_Config.m_TcAutoReplyMinimizedMessage);
			else
				str_format(aBuf, sizeof(aBuf), "%s: %s", aPlayerName, g_Config.m_TcAutoReplyMinimizedMessage);
			SendNonDuplicateMessage(0, aBuf);
			return;
		}
	}

	if(MsgType == NETMSGTYPE_SV_VOTESET)
	{
		const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy]; // Do not care about spec behaviour
		const bool Afk = LocalId >= 0 && GameClient()->m_aClients[LocalId].m_Afk; // TODO Depends on server afk time
		CNetMsg_Sv_VoteSet *pMsg = (CNetMsg_Sv_VoteSet *)pRawMsg;
		if(pMsg->m_Timeout && !Afk)
		{
			char aDescription[VOTE_DESC_LENGTH];
			char aReason[VOTE_REASON_LENGTH];
			str_copy(aDescription, pMsg->m_pDescription);
			str_copy(aReason, pMsg->m_pReason);
			bool KickVote = str_startswith(aDescription, "Kick ") != 0 ? true : false;
			bool SpecVote = str_startswith(aDescription, "Pause ") != 0 ? true : false;
			bool SettingVote = !KickVote && !SpecVote;
			bool RandomMapVote = SettingVote && str_find_nocase(aDescription, "random");
			bool MapCoolDown = SettingVote && (str_find_nocase(aDescription, "change map") || str_find_nocase(aDescription, "no not change map"));
			bool CategoryVote = SettingVote && (str_find_nocase(aDescription, "☐") || str_find_nocase(aDescription, "☒"));
			bool FunVote = SettingVote && str_find_nocase(aDescription, "funvote");
			bool MapVote = SettingVote && !RandomMapVote && !MapCoolDown && !CategoryVote && !FunVote && (str_find_nocase(aDescription, "Map:") || str_find_nocase(aDescription, "★") || str_find_nocase(aDescription, "✰"));

			if(g_Config.m_TcAutoVoteWhenFar && (MapVote || RandomMapVote))
			{
				int RaceTime = 0;
				if(GameClient()->m_Snap.m_pGameInfoObj && GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME)
					RaceTime = (Client()->GameTick(g_Config.m_ClDummy) + GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer) / Client()->GameTickSpeed();

				if(RaceTime / 60 >= g_Config.m_TcAutoVoteWhenFarTime)
				{
					CGameClient::CClientData *pVoteCaller = nullptr;
					int CallerId = -1;
					for(int i = 0; i < MAX_CLIENTS; i++)
					{
						if(!GameClient()->m_aStats[i].IsActive())
							continue;

						char aBuf[MAX_NAME_LENGTH + 4];
						str_format(aBuf, sizeof(aBuf), "\'%s\'", GameClient()->m_aClients[i].m_aName);
						if(str_find_nocase(aBuf, pMsg->m_pDescription) == 0)
						{
							pVoteCaller = &GameClient()->m_aClients[i];
							CallerId = i;
						}
					}
					if(pVoteCaller)
					{
						bool Friend = pVoteCaller->m_Friend;
						bool SameTeam = GameClient()->m_Teams.Team(GameClient()->m_Snap.m_LocalClientId) == pVoteCaller->m_Team && pVoteCaller->m_Team != 0;
						bool MySelf = CallerId == GameClient()->m_Snap.m_LocalClientId;

						if(!Friend && !SameTeam && !MySelf)
						{
							GameClient()->m_Voting.Vote(-1);
							if(str_comp(g_Config.m_TcAutoVoteWhenFarMessage, "") != 0)
								SendNonDuplicateMessage(0, g_Config.m_TcAutoVoteWhenFarMessage);
						}
					}
				}
			}
		}
	}

	auto &vServerCommands = GameClient()->m_Chat.m_vServerCommands;
	auto AddSpecId = [&](bool Enable) {
		static const CChat::CCommand SpecId("specid", "v[id]", "Spectate a player");
		vServerCommands.erase(std::remove_if(vServerCommands.begin(), vServerCommands.end(), [](const CChat::CCommand &Command) { return Command == SpecId; }), vServerCommands.end());
		if(Enable)
			vServerCommands.push_back(SpecId);
		GameClient()->m_Chat.m_ServerCommandsNeedSorting = true;
	};
	if(MsgType == NETMSGTYPE_SV_COMMANDINFO)
	{
		CNetMsg_Sv_CommandInfo *pMsg = (CNetMsg_Sv_CommandInfo *)pRawMsg;
		if(str_comp_nocase(pMsg->m_pName, "spec") == 0)
			AddSpecId(!ServerCommandExists("specid"));
		else if(str_comp_nocase(pMsg->m_pName, "specid") == 0)
			AddSpecId(false);
		return;
	}
	if(MsgType == NETMSGTYPE_SV_COMMANDINFOREMOVE)
	{
		CNetMsg_Sv_CommandInfoRemove *pMsg = (CNetMsg_Sv_CommandInfoRemove *)pRawMsg;
		if(str_comp_nocase(pMsg->m_pName, "spec") == 0)
			AddSpecId(false);
		else if(str_comp_nocase(pMsg->m_pName, "specid") == 0)
			AddSpecId(ServerCommandExists("spec"));
		return;
	}
}

void CTClient::ConSpecId(IConsole::IResult *pResult, void *pUserData)
{
	((CTClient *)pUserData)->SpecId(pResult->GetInteger(0));
}

bool CTClient::ChatDoSpecId(const char *pInput)
{
	const char *pNumber = str_startswith_nocase(pInput, "/specid ");
	if(!pNumber)
		return false;

	const int Length = str_length(pInput);
	CChat::CHistoryEntry *pEntry = GameClient()->m_Chat.m_History.Allocate(sizeof(CChat::CHistoryEntry) + Length);
	pEntry->m_Team = 0;
	str_copy(pEntry->m_aText, pInput, Length + 1);

	int ClientId = 0;
	if(!str_toint(pNumber, &ClientId))
		return true;

	SpecId(ClientId);
	return true;
}

void CTClient::SpecId(int ClientId)
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(Client()->State() == IClient::STATE_DEMOPLAYBACK || GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		GameClient()->m_Spectator.Spectate(ClientId);
		return;
	}

	if(ClientId < 0 || ClientId > (int)std::size(GameClient()->m_aClients))
		return;
	const auto &Player = GameClient()->m_aClients[ClientId];
	if(!Player.m_Active)
		return;
	char aBuf[256];
	str_copy(aBuf, "/spec \"");
	char *pDst = aBuf + strlen(aBuf);
	str_escape(&pDst, Player.m_aName, aBuf + sizeof(aBuf));
	str_append(aBuf, "\"");
	GameClient()->m_Chat.SendChat(0, aBuf);
}

void CTClient::ConEmoteCycle(IConsole::IResult *pResult, void *pUserData)
{
	CTClient &This = *(CTClient *)pUserData;
	This.m_EmoteCycle += 1;
	if(This.m_EmoteCycle > 15)
		This.m_EmoteCycle = 0;
	This.GameClient()->m_Emoticon.Emote(This.m_EmoteCycle);
}

void CTClient::AirRescue()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	const int ClientId = GameClient()->m_Snap.m_LocalClientId;
	if(ClientId < 0 || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		return;
	if(GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo && (GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Flags & CHARACTERFLAG_PRACTICE_MODE) == 0)
	{
		GameClient()->Echo("You are not in practice");
		return;
	}

	auto IsIndexAirLike = [&](int Index) {
		const auto Tile = Collision()->GetTileIndex(Index);
		return Tile == TILE_AIR || Tile == TILE_UNFREEZE || Tile == TILE_DUNFREEZE;
	};
	auto IsPosAirLike = [&](vec2 Pos) {
		const int Index = Collision()->GetPureMapIndex(Pos);
		return IsIndexAirLike(Index);
	};
	auto IsRadiusAirLike = [&](vec2 Pos, int Radius) {
		for(int y = -Radius; y <= Radius; ++y)
			for(int x = -Radius; x <= Radius; ++x)
				if(!IsPosAirLike(Pos + vec2(x, y) * 32.0f))
					return false;
		return true;
	};

	auto &AirRescuePositions = m_aAirRescuePositions[g_Config.m_ClDummy];
	while(!AirRescuePositions.empty())
	{
		// Get latest pos from positions
		const vec2 NewPos = AirRescuePositions.front();
		AirRescuePositions.pop_front();
		// Check for safety
		if(!IsRadiusAirLike(NewPos, 2))
			continue;
		// Do it
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "/tpxy %f %f", NewPos.x / 32.0f, NewPos.y / 32.0f);
		GameClient()->m_Chat.SendChat(0, aBuf);
		return;
	}

	GameClient()->Echo("No safe position found");
}

void CTClient::ConAirRescue(IConsole::IResult *pResult, void *pUserData)
{
	((CTClient *)pUserData)->AirRescue();
}

void CTClient::ConCalc(IConsole::IResult *pResult, void *pUserData)
{
	int Error = 0;
	double Out = te_interp(pResult->GetString(0), &Error);
	if(Out == NAN || Error != 0)
		log_info("tclient", "Calc error: %d", Error);
	else
		log_info("tclient", "Calc result: %lf", Out);
}

void CTClient::OnConsoleInit()
{
	Console()->Register("calc", "r[expression]", CFGFLAG_CLIENT, ConCalc, this, "Evaluate an expression");
	Console()->Register("airrescue", "", CFGFLAG_CLIENT, ConAirRescue, this, "Rescue to a nearby air tile");

	Console()->Register("tc_random_player", "s[type]", CFGFLAG_CLIENT, ConRandomTee, this, "Randomize player color (0 = all, 1 = body, 2 = feet, 3 = skin, 4 = flag) example: 0011 = randomize skin and flag [number is position]");
	Console()->Chain("tc_random_player", ConchainRandomColor, this);

	Console()->Register("spec_id", "v[id]", CFGFLAG_CLIENT, ConSpecId, this, "Spectate a player by Id");

	Console()->Register("emote_cycle", "", CFGFLAG_CLIENT, ConEmoteCycle, this, "Cycle through emotes");

	// 复读功能命令
	Console()->Register("+qm_repeat", "", CFGFLAG_CLIENT, ConRepeat, this, "复读");

	// 收藏地图命令
	Console()->Register("add_favorite_map", "s[map_name]", CFGFLAG_CLIENT, ConAddFavoriteMap, this, "Add a map to favorites");
	Console()->Register("remove_favorite_map", "s[map_name]", CFGFLAG_CLIENT, ConRemoveFavoriteMap, this, "Remove a map from favorites");
	Console()->Register("clear_favorite_maps", "", CFGFLAG_CLIENT, ConClearFavoriteMaps, this, "Clear all favorite maps");

	// 本地存档列表命令
	Console()->Register("savelist", "?s[map]", CFGFLAG_CLIENT, ConSaveList, this, "List local saves for current map (or specified map)");

#if defined(CONF_WHISPER)
	// Speech-to-Text command (+stt: press to start, release to stop)
	Console()->Register("+stt", "", CFGFLAG_CLIENT, ConSttToggle, this, "Hold to record voice, release to transcribe");
#endif

	// 注册保存回调
	ConfigManager()->RegisterCallback(ConfigSaveFavoriteMaps, this);

	Console()->Chain(
		"tc_allow_any_resolution", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
			pfnCallback(pResult, pCallbackUserData);
			((CTClient *)pUserData)->SetForcedAspect();
		},
		this);

	Console()->Chain(
		"tc_regex_chat_ignore", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
			if(pResult->NumArguments() == 1)
			{
				auto Re = Regex(pResult->GetString(0));
				if(!Re.error().empty())
				{
					log_error("tclient", "Invalid regex: %s", Re.error().c_str());
					return;
				}
				((CTClient *)pUserData)->m_RegexChatIgnore = std::move(Re);
			}
			pfnCallback(pResult, pCallbackUserData);
		},
		this);
}

void CTClient::RandomBodyColor()
{
	g_Config.m_ClPlayerColorBody = ColorHSLA((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, 1.0f).Pack(false);
}

void CTClient::RandomFeetColor()
{
	g_Config.m_ClPlayerColorFeet = ColorHSLA((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, 1.0f).Pack(false);
}

void CTClient::RandomSkin(void *pUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	const auto &Skins = pThis->GameClient()->m_Skins.SkinList().Skins();
	str_copy(g_Config.m_ClPlayerSkin, Skins[std::rand() % (int)Skins.size()].SkinContainer()->Name());
}

void CTClient::RandomFlag(void *pUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	// get the flag count
	int FlagCount = pThis->GameClient()->m_CountryFlags.Num();

	// get a random flag number
	int FlagNumber = std::rand() % FlagCount;

	// get the flag name
	const CCountryFlags::CCountryFlag &Flag = pThis->GameClient()->m_CountryFlags.GetByIndex(FlagNumber);

	// set the flag code as number
	g_Config.m_PlayerCountry = Flag.m_CountryCode;
}

void CTClient::DoFinishCheck()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(g_Config.m_TcChangeNameNearFinish <= 0)
		return;
	m_FinishTextTimeout -= Client()->RenderFrameTime();
	if(m_FinishTextTimeout > 0.0f)
		return;
	m_FinishTextTimeout = 1.0f;
	// Check for finish tile
	const auto &NearTile = [this](vec2 Pos, int RadiusInTiles, int Tile) -> bool {
		const CCollision *pCollision = GameClient()->Collision();
		for(int i = 0; i <= RadiusInTiles * 2; ++i)
		{
			const float h = std::ceil(std::pow(std::sin((float)i * pi / 2.0f / (float)RadiusInTiles), 0.5f) * pi / 2.0f * (float)RadiusInTiles);
			const vec2 Pos1 = vec2(Pos.x + (float)(i - RadiusInTiles) * 32.0f, Pos.y - h);
			const vec2 Pos2 = vec2(Pos.x + (float)(i - RadiusInTiles) * 32.0f, Pos.y + h);
			std::vector<int> vIndices = pCollision->GetMapIndices(Pos1, Pos2);
			if(vIndices.empty())
				vIndices.push_back(pCollision->GetPureMapIndex(Pos1));
			for(int &Index : vIndices)
			{
				if(pCollision->GetTileIndex(Index) == Tile)
					return true;
				if(pCollision->GetFrontTileIndex(Index) == Tile)
					return true;
			}
		}
		return false;
	};
	const auto &SendUrgentRename = [this](int Conn, const char *pNewName) {
		CNetMsg_Cl_ChangeInfo Msg;
		Msg.m_pName = pNewName;
		Msg.m_pClan = Conn == 0 ? g_Config.m_PlayerClan : g_Config.m_ClDummyClan;
		Msg.m_Country = Conn == 0 ? g_Config.m_PlayerCountry : g_Config.m_ClDummyCountry;
		Msg.m_pSkin = Conn == 0 ? g_Config.m_ClPlayerSkin : g_Config.m_ClDummySkin;
		Msg.m_UseCustomColor = Conn == 0 ? g_Config.m_ClPlayerUseCustomColor : g_Config.m_ClDummyUseCustomColor;
		Msg.m_ColorBody = Conn == 0 ? g_Config.m_ClPlayerColorBody : g_Config.m_ClDummyColorBody;
		Msg.m_ColorFeet = Conn == 0 ? g_Config.m_ClPlayerColorFeet : g_Config.m_ClDummyColorFeet;
		CMsgPacker Packer(&Msg);
		Msg.Pack(&Packer);
		Client()->SendMsg(Conn, &Packer, MSGFLAG_VITAL);
		GameClient()->m_aCheckInfo[Conn] = Client()->GameTickSpeed(); // 1 second
	};
	int Dummy = g_Config.m_ClDummy;
	const auto &Player = GameClient()->m_aClients[GameClient()->m_aLocalIds[Dummy]];
	if(!Player.m_Active)
		return;
	const char *NewName = g_Config.m_TcFinishName;
	if(str_comp(Player.m_aName, NewName) == 0)
		return;
	if(!NearTile(Player.m_RenderPos, 10, TILE_FINISH))
		return;
	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), TCLocalize("Changing name to %s near finish"), NewName);
	GameClient()->Echo(aBuf);
	SendUrgentRename(Dummy, NewName);
}

bool CTClient::ServerCommandExists(const char *pCommand)
{
	for(const auto &Command : GameClient()->m_Chat.m_vServerCommands)
		if(str_comp_nocase(pCommand, Command.m_aName) == 0)
			return true;
	return false;
}

void CTClient::OnRender()
{
	if(m_pTClientInfoTask)
	{
		if(m_pTClientInfoTask->State() == EHttpState::DONE)
		{
			FinishTClientInfo();
			ResetTClientInfoTask();
		}
	}

#if defined(CONF_WHISPER)
	// Initialize STT on first render if enabled
	if(!m_SttInitialized && g_Config.m_QmSttEnabled)
	{
		InitStt();
	}
	// Update STT to process transcription results
	if(m_SttInitialized)
	{
		m_Stt.Update();
	}
#endif

	DoFinishCheck();
	CheckFreeze();
	CheckWaterFall();
	CheckAutoUnspecOnUnfreeze(); // 检测解冻自动取消旁观
	CheckAutoSwitchOnUnfreeze(); // HJ大佬辅助 - 检测自动切换
	UpdatePlayerStats(); // 更新玩家统计
}

void CTClient::CheckFreeze()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	if(!g_Config.m_TcFreezeChatEnabled)
		return;

	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		// Only check for active dummy
		if(Dummy == 1 && !Client()->DummyConnected())
			continue;

		const int ClientId = GameClient()->m_aLocalIds[Dummy];
		if(ClientId < 0)
			continue;

		const auto &Client = GameClient()->m_aClients[ClientId];
		if(!Client.m_Active)
			continue;

		// Check if player is currently frozen
		bool IsInFreeze = Client.m_FreezeEnd != 0;

		// Detect entering freeze (transition from not frozen to frozen)
		if(IsInFreeze && !m_aWasInFreeze[Dummy])
		{
			int64_t Now = time_get();
			int64_t FreqMs = time_freq() / 1000;

			// Send emoticon (with 3 second cooldown)
			if(g_Config.m_TcFreezeChatEmoticon && Now - m_aLastFreezeEmoteTime[Dummy] > 3000 * FreqMs)
			{
				GameClient()->m_Emoticon.Emote(g_Config.m_TcFreezeChatEmoticonId);
				m_aLastFreezeEmoteTime[Dummy] = Now;
			}

			// Send chat message (with 5 second cooldown and probability check)
			if(g_Config.m_TcFreezeChatMessage[0] != '\0' && Now - m_aLastFreezeMessageTime[Dummy] > 5000 * FreqMs)
			{
				// Check probability (0-100%)
				int Chance = g_Config.m_TcFreezeChatChance;
				if(Chance > 0 && (Chance >= 100 || (std::rand() % 100) < Chance))
				{
					// Parse comma-separated messages and pick one randomly
					char aMessages[128];
					str_copy(aMessages, g_Config.m_TcFreezeChatMessage);
					
					// Count messages and store pointers
					std::vector<const char *> vMessages;
					char *pToken = strtok(aMessages, ",");
					while(pToken != nullptr)
					{
						// Skip leading spaces
						while(*pToken == ' ')
							pToken++;
						if(*pToken != '\0')
							vMessages.push_back(pToken);
						pToken = strtok(nullptr, ",");
					}
					
					// Pick a random message and send
					if(!vMessages.empty())
					{
						const char *pSelectedMessage = vMessages[std::rand() % vMessages.size()];
						GameClient()->m_Chat.SendChat(0, pSelectedMessage);
						m_aLastFreezeMessageTime[Dummy] = Now;
					}
				}
			}
		}

		m_aWasInFreeze[Dummy] = IsInFreeze;
	}
}

void CTClient::CheckWaterFall()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	if(!g_Config.m_TcWaterFallEnabled)
		return;

	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		// Only check for active dummy
		if(Dummy == 1 && !Client()->DummyConnected())
			continue;

		const int ClientId = GameClient()->m_aLocalIds[Dummy];
		if(ClientId < 0)
			continue;

		const auto &Char = GameClient()->m_Snap.m_aCharacters[ClientId];
		if(!Char.m_Active)
			continue;

		// Check if player is in death tile
		vec2 Pos = vec2(Char.m_Cur.m_X, Char.m_Cur.m_Y);
		int Index = Collision()->GetPureMapIndex(Pos);
		int TileIndex = Collision()->GetTileIndex(Index);
		bool IsInDeath = TileIndex == TILE_DEATH;

		// Detect entering death (transition from not in death to in death)
		if(IsInDeath && !m_aWasInDeath[Dummy])
		{
			int64_t Now = time_get();
			int64_t FreqMs = time_freq() / 1000;

			// Send heart emoticon (with 3 second cooldown)
			if(g_Config.m_TcWaterFallEmoticon && Now - m_aLastWaterHeartTime[Dummy] > 3000 * FreqMs)
			{
				GameClient()->m_Emoticon.Emote(EMOTICON_HEARTS); // Heart emoticon
				m_aLastWaterHeartTime[Dummy] = Now;
			}

			// Send chat message (with 5 second cooldown)
			if(g_Config.m_TcWaterFallMessage[0] != '\0' && Now - m_aLastWaterMessageTime[Dummy] > 5000 * FreqMs)
			{
				GameClient()->m_Chat.SendChat(0, g_Config.m_TcWaterFallMessage);
				m_aLastWaterMessageTime[Dummy] = Now;
			}
		}

		m_aWasInDeath[Dummy] = IsInDeath;
	}
}

void CTClient::CheckAutoUnspecOnUnfreeze()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	if(!g_Config.m_QmAutoUnspecOnUnfreeze)
		return;

	// 分别检查主玩家和dummy（每个Tee的旁观状态是独立的）
	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		const int ClientId = GameClient()->m_aLocalIds[Dummy];
		if(ClientId < 0)
			continue;

		const auto &ClientData = GameClient()->m_aClients[ClientId];
		if(!ClientData.m_Active)
			continue;

		// 检查是否处于旁观状态
		bool IsSpectating = GameClient()->m_Snap.m_SpecInfo.m_Active;

		// 检查当前是否被 freeze
		bool IsInFreeze = ClientData.m_FreezeEnd != 0;

		// 检测从 freeze 到 unfreeze 的转换
		if(m_aWasInFreezeForUnspec[Dummy] && !IsInFreeze && IsSpectating)
		{
			// 被解冻了，且当前处于旁观状态，直接发送网络包（最快方式）
			if(Client()->IsSixup())
			{
				// 0.7协议
				protocol7::CNetMsg_Cl_Say Msg7;
				Msg7.m_Mode = protocol7::CHAT_ALL;
				Msg7.m_Target = -1;
				Msg7.m_pMessage = "/spec";
				Client()->SendPackMsgActive(&Msg7, MSGFLAG_VITAL, true);
			}
			else
			{
				// 0.6协议
				CNetMsg_Cl_Say Msg;
				Msg.m_Team = 0; // 全体聊天
				Msg.m_pMessage = "/spec";
				Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL);
			}
		}

		m_aWasInFreezeForUnspec[Dummy] = IsInFreeze;
	}
}

void CTClient::CheckAutoSwitchOnUnfreeze()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	if(!g_Config.m_QmAutoSwitchOnUnfreeze)
		return;

	// 必须有dummy连接
	if(!Client()->DummyConnected())
		return;

	// 获取本体和dummy的ClientId
	const int MainClientId = GameClient()->m_aLocalIds[0];
	const int DummyClientId = GameClient()->m_aLocalIds[1];

	if(MainClientId < 0 || DummyClientId < 0)
		return;

	const auto &MainClient = GameClient()->m_aClients[MainClientId];
	const auto &DummyClient = GameClient()->m_aClients[DummyClientId];

	if(!MainClient.m_Active || !DummyClient.m_Active)
		return;

	// 获取当前freeze状态
	bool MainInFreeze = MainClient.m_FreezeEnd != 0;
	bool DummyInFreeze = DummyClient.m_FreezeEnd != 0;

	// 获取之前的freeze状态
	bool MainWasInFreeze = m_aWasInFreezeForSwitch[0];
	bool DummyWasInFreeze = m_aWasInFreezeForSwitch[1];

	// 当前操控的是哪个 (0=本体, 1=dummy)
	int CurrentDummy = g_Config.m_ClDummy;

	// 核心逻辑：两个都曾被freeze，现在有一个解冻了
	// 如果解冻的不是当前操控的，就切换
	if(MainWasInFreeze && DummyWasInFreeze)
	{
		// 本体刚解冻，而当前操控的是dummy
		if(!MainInFreeze && DummyInFreeze && CurrentDummy == 1)
		{
			// 切换到本体
			Console()->ExecuteLine("cl_dummy 0");
		}
		// dummy刚解冻，而当前操控的是本体
		else if(MainInFreeze && !DummyInFreeze && CurrentDummy == 0)
		{
			// 切换到dummy
			Console()->ExecuteLine("cl_dummy 1");
		}
	}

	// 更新状态
	m_aWasInFreezeForSwitch[0] = MainInFreeze;
	m_aWasInFreezeForSwitch[1] = DummyInFreeze;
}

bool CTClient::NeedUpdate()
{
	return str_comp(m_aVersionStr, "0") != 0;
}

void CTClient::ResetTClientInfoTask()
{
	if(m_pTClientInfoTask)
	{
		m_pTClientInfoTask->Abort();
		m_pTClientInfoTask = NULL;
	}
}

void CTClient::FetchTClientInfo()
{
	if(m_pTClientInfoTask && !m_pTClientInfoTask->Done())
		return;
	char aUrl[256];
	str_copy(aUrl, TCLIENT_INFO_URL);
	m_pTClientInfoTask = HttpGet(aUrl);
	m_pTClientInfoTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pTClientInfoTask->IpResolve(IPRESOLVE::V4);
	Http()->Run(m_pTClientInfoTask);
}

typedef std::tuple<int, int, int> TVersion;
static const TVersion gs_InvalidTCVersion = std::make_tuple(-1, -1, -1);

static TVersion ToTCVersion(char *pStr)
{
	int aVersion[3] = {0, 0, 0};
	const char *p = strtok(pStr, ".");

	for(int i = 0; i < 3 && p; ++i)
	{
		if(!str_isallnum(p))
			return gs_InvalidTCVersion;

		aVersion[i] = str_toint(p);
		p = strtok(NULL, ".");
	}

	if(p)
		return gs_InvalidTCVersion;

	return std::make_tuple(aVersion[0], aVersion[1], aVersion[2]);
}

void CTClient::FinishTClientInfo()
{
	json_value *pJson = m_pTClientInfoTask->ResultJson();
	if(!pJson)
		return;
	const json_value &Json = *pJson;
	const json_value &CurrentVersion = Json["version"];

	if(CurrentVersion.type == json_string)
	{
		char aNewVersionStr[64];
		str_copy(aNewVersionStr, CurrentVersion);
		char aCurVersionStr[64];
		str_copy(aCurVersionStr, TCLIENT_VERSION);
		if(ToTCVersion(aNewVersionStr) > ToTCVersion(aCurVersionStr))
		{
			str_copy(m_aVersionStr, CurrentVersion);
		}
		else
		{
			m_aVersionStr[0] = '0';
			m_aVersionStr[1] = '\0';
		}
		m_FetchedTClientInfo = true;
	}

	json_value_free(pJson);
}

void CTClient::SetForcedAspect()
{
	// TODO: Fix flashing on windows
	int State = Client()->State();
	bool Force = true;
	if(g_Config.m_TcAllowAnyRes == 0)
		;
	else if(State == CClient::EClientState::STATE_DEMOPLAYBACK)
		Force = false;
	else if(State == CClient::EClientState::STATE_ONLINE && GameClient()->m_GameInfo.m_AllowZoom && !GameClient()->m_Menus.IsActive())
		Force = false;
	Graphics()->SetForcedAspect(Force);
}

void CTClient::OnStateChange(int OldState, int NewState)
{
	SetForcedAspect();
	for(auto &AirRescuePositions : m_aAirRescuePositions)
		AirRescuePositions = {};

	// 进入服务器时重置统计数据
	if(NewState == IClient::STATE_ONLINE && g_Config.m_QmPlayerStatsResetOnJoin)
	{
		ResetPlayerStats(-1);
	}
}

void CTClient::OnNewSnapshot()
{
	SetForcedAspect();
	// Update volleyball
	bool IsVolleyBall = false;
	if(g_Config.m_TcVolleyBallBetterBall > 0 && g_Config.m_TcVolleyBallBetterBallSkin[0] != '\0')
	{
		if(g_Config.m_TcVolleyBallBetterBall > 1)
			IsVolleyBall = true;
		else
			IsVolleyBall = str_startswith_nocase(Client()->GetCurrentMap(), "volleyball");
	};
	for(auto &Client : GameClient()->m_aClients)
	{
		Client.m_IsVolleyBall = IsVolleyBall && Client.m_DeepFrozen;
	}
	// Update air rescue
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
		{
			const int ClientId = GameClient()->m_aLocalIds[Dummy];
			if(ClientId == -1)
				continue;
			const auto &Char = GameClient()->m_Snap.m_aCharacters[ClientId];
			if(!Char.m_Active)
				continue;
			if(Client()->GameTick(Dummy) % 10 != 0) // Works for both 25tps and 50tps
				continue;
			const auto &Client = GameClient()->m_aClients[ClientId];
			if(Client.m_FreezeEnd == -1) // You aren't safe when frozen
				continue;
			const vec2 NewPos = vec2(Char.m_Cur.m_X, Char.m_Cur.m_Y);
			// If new pos is under 2 tiles from old pos, don't record a new position
			if(!m_aAirRescuePositions[Dummy].empty())
			{
				const vec2 OldPos = m_aAirRescuePositions[Dummy].front();
				if(distance(NewPos, OldPos) < 64.0f)
					continue;
			}
			if(m_aAirRescuePositions[Dummy].size() >= 256)
				m_aAirRescuePositions[Dummy].pop_back();
			m_aAirRescuePositions[Dummy].push_front(NewPos);
		}
	}
}

constexpr const char STRIP_CHARS[] = {'-', '=', '+', '_', ' '};
static bool IsStripChar(char c)
{
	return std::any_of(std::begin(STRIP_CHARS), std::end(STRIP_CHARS), [c](char s) {
		return s == c;
	});
}

static void StripStr(const char *pIn, char *pOut, const char *pEnd)
{
	if(!pIn)
	{
		*pOut = '\0';
		return;
	}

	while(*pIn && IsStripChar(*pIn))
		pIn++;

	// Special behaviour for empty checkbox
	if((unsigned char)*pIn == 0xE2 && (unsigned char)(*(pIn + 1)) == 0x98 && (unsigned char)(*(pIn + 2)) == 0x90)
	{
		pIn += 3;
		while(*pIn && IsStripChar(*pIn))
			pIn++;
	}

	char *pLastValid = nullptr;
	while(*pIn && pOut < pEnd - 1)
	{
		*pOut = *pIn;
		if(!IsStripChar(*pIn))
			pLastValid = pOut;
		pIn++;
		pOut++;
	}

	if(pLastValid)
		*(pLastValid + 1) = '\0';
	else
		*pOut = '\0';
}

void CTClient::RenderMiniVoteHud()
{
	CUIRect View = {0.0f, 60.0f, 70.0f, 35.0f};
	View.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_R, 3.0f);
	View.Margin(3.0f, &View);

	SLabelProperties Props;
	Props.m_EllipsisAtEnd = true;
	Props.m_MaxWidth = View.w;

	CUIRect Row, LeftColumn, RightColumn, ProgressSpinner;
	char aBuf[256];

	// Vote description
	View.HSplitTop(6.0f, &Row, &View);
	StripStr(GameClient()->m_Voting.VoteDescription(), aBuf, aBuf + sizeof(aBuf));
	Ui()->DoLabel(&Row, aBuf, 6.0f, TEXTALIGN_ML, Props);

	// Vote reason
	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(4.0f, &Row, &View);
	Ui()->DoLabel(&Row, GameClient()->m_Voting.VoteReason(), 4.0f, TEXTALIGN_ML, Props);

	// Time left
	str_format(aBuf, sizeof(aBuf), Localize("%ds left"), GameClient()->m_Voting.SecondsLeft());
	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(3.0f, &Row, &View);
	Row.VSplitLeft(2.0f, nullptr, &Row);
	Row.VSplitLeft(3.0f, &ProgressSpinner, &Row);
	Row.VSplitLeft(2.0f, nullptr, &Row);

	SProgressSpinnerProperties ProgressProps;
	ProgressProps.m_Progress = std::clamp((time() - GameClient()->m_Voting.m_Opentime) / (float)(GameClient()->m_Voting.m_Closetime - GameClient()->m_Voting.m_Opentime), 0.0f, 1.0f);
	Ui()->RenderProgressSpinner(ProgressSpinner.Center(), ProgressSpinner.h / 2.0f, ProgressProps);

	Ui()->DoLabel(&Row, aBuf, 3.0f, TEXTALIGN_ML);

	// Bars
	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(3.0f, &Row, &View);
	GameClient()->m_Voting.RenderBars(Row);

	// F3 / F4
	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(0.5f, &Row, &View);
	Row.VSplitMid(&LeftColumn, &RightColumn, 4.0f);

	char aKey[64];
	GameClient()->m_Binds.GetKey("vote yes", aKey, sizeof(aKey));
	TextRender()->TextColor(GameClient()->m_Voting.TakenChoice() == 1 ? ColorRGBA(0.2f, 0.9f, 0.2f, 0.85f) : TextRender()->DefaultTextColor());
	Ui()->DoLabel(&LeftColumn, aKey[0] == '\0' ? "yes" : aKey, 0.5f, TEXTALIGN_ML);

	GameClient()->m_Binds.GetKey("vote no", aKey, sizeof(aKey));
	TextRender()->TextColor(GameClient()->m_Voting.TakenChoice() == -1 ? ColorRGBA(0.95f, 0.25f, 0.25f, 0.85f) : TextRender()->DefaultTextColor());
	Ui()->DoLabel(&RightColumn, aKey[0] == '\0' ? "no" : aKey, 0.5f, TEXTALIGN_MR);

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CTClient::RenderCenterLines()
{
	if(g_Config.m_TcShowCenter <= 0)
		return;

	if(GameClient()->m_Scoreboard.IsActive())
		return;

	Graphics()->TextureClear();

	float X0, Y0, X1, Y1;
	Graphics()->GetScreen(&X0, &Y0, &X1, &Y1);
	const float XMid = (X0 + X1) / 2.0f;
	const float YMid = (Y0 + Y1) / 2.0f;

	if(g_Config.m_TcShowCenterWidth == 0)
	{
		Graphics()->LinesBegin();
		IGraphics::CLineItem aLines[2] = {
			{XMid, Y0, XMid, Y1},
			{X0, YMid, X1, YMid}};
		Graphics()->SetColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcShowCenterColor, true)));
		Graphics()->LinesDraw(aLines, std::size(aLines));
		Graphics()->LinesEnd();
	}
	else
	{
		const float W = g_Config.m_TcShowCenterWidth;
		Graphics()->QuadsBegin();
		IGraphics::CQuadItem aQuads[3] = {
			{XMid, mix(Y0, Y1, 0.25f) - W / 4.0f, W, (Y1 - Y0 - W) / 2.0f},
			{XMid, mix(Y0, Y1, 0.75f) + W / 4.0f, W, (Y1 - Y0 - W) / 2.0f},
			{XMid, YMid, X1 - X0, W}};
		Graphics()->SetColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcShowCenterColor, true)));
		Graphics()->QuadsDraw(aQuads, std::size(aQuads));
		Graphics()->QuadsEnd();
	}
}

void CTClient::RenderCtfFlag(vec2 Pos, float Alpha)
{
	// from CItems::RenderFlag
	float Size = 42.0f;
	int QuadOffset;
	if(g_Config.m_TcFakeCtfFlags == 1)
	{
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteFlagRed);
		QuadOffset = GameClient()->m_Items.m_RedFlagOffset;
	}
	else
	{
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteFlagBlue);
		QuadOffset = GameClient()->m_Items.m_BlueFlagOffset;
	}
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	Graphics()->RenderQuadContainerAsSprite(GameClient()->m_Items.m_ItemsQuadContainerIndex, QuadOffset, Pos.x, Pos.y - Size * 0.75f);
}

void CTClient::ResetPlayerStats(int Dummy)
{
	if(Dummy < 0)
	{
		// 重置所有
		for(auto &Stats : m_aPlayerStats)
			Stats.Reset();
	}
	else if(Dummy < NUM_DUMMIES)
	{
		m_aPlayerStats[Dummy].Reset();
	}
}

void CTClient::UpdatePlayerStats()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		// Only check for active dummy
		if(Dummy == 1 && !Client()->DummyConnected())
			continue;

		const int ClientId = GameClient()->m_aLocalIds[Dummy];
		if(ClientId < 0)
			continue;

		const auto &ClientData = GameClient()->m_aClients[ClientId];
		if(!ClientData.m_Active)
			continue;

		const auto &Char = GameClient()->m_Snap.m_aCharacters[ClientId];
		if(!Char.m_Active)
			continue;

		SPlayerStats &Stats = m_aPlayerStats[Dummy];
		float CurrentX = (float)Char.m_Cur.m_X;
		float CurrentY = (float)Char.m_Cur.m_Y;

		// 检测 freeze 状态变化（用于存活时长统计）
		bool IsInFreeze = ClientData.m_FreezeEnd != 0;

		if(!IsInFreeze && !Stats.m_IsAlive)
		{
			// 刚解冻，开始计时
			Stats.m_IsAlive = true;
			Stats.m_CurrentAliveStart = Client()->GameTick(Dummy);

			// 检查位置是否变化很大（重生了），如果是则不算被救醒
			float Dist = 0.0f;
			if(Stats.m_FreezeX != 0.0f || Stats.m_FreezeY != 0.0f)
			{
				float Dx = CurrentX - Stats.m_FreezeX;
				float Dy = CurrentY - Stats.m_FreezeY;
				Dist = std::sqrt(Dx * Dx + Dy * Dy);
			}

			// 如果位置变化小于200单位，说明是原地解冻，算被救醒
			const float RespawnThreshold = 200.0f;
			if(Dist < RespawnThreshold && (Stats.m_FreezeX != 0.0f || Stats.m_FreezeY != 0.0f))
			{
				Stats.m_RescueCount++;
			}
		}
		else if(IsInFreeze && Stats.m_IsAlive)
		{
			// 刚被冻结，结束计时，落水次数+1，记录冻结位置
			Stats.m_IsAlive = false;
			Stats.m_FreezeCount++;
			Stats.m_FreezeX = CurrentX;
			Stats.m_FreezeY = CurrentY;
			int AliveTime = Client()->GameTick(Dummy) - Stats.m_CurrentAliveStart;
			if(AliveTime > 0)
			{
				Stats.m_TotalAliveTime += AliveTime;
				Stats.m_AliveCount++;
				if(AliveTime > Stats.m_MaxAliveTime)
					Stats.m_MaxAliveTime = AliveTime;
			}
		}

		// 跟踪出钩方向
		TrackHookDirection(Dummy);
	}
}

void CTClient::TrackHookDirection(int Dummy)
{
	const int ClientId = GameClient()->m_aLocalIds[Dummy];
	if(ClientId < 0)
		return;

	const auto &Char = GameClient()->m_Snap.m_aCharacters[ClientId];
	if(!Char.m_Active)
		return;

	SPlayerStats &Stats = m_aPlayerStats[Dummy];

	// 检测 hook 状态
	bool IsHooking = Char.m_Cur.m_HookState > 0 && Char.m_Cur.m_HookState != HOOK_RETRACTED;

	// 检测开始出钩的瞬间
	if(IsHooking && !Stats.m_WasHooking)
	{
		// 使用钩子位置相对于玩家位置来判断方向
		float HookX = (float)(Char.m_Cur.m_HookX - Char.m_Cur.m_X);
		if(HookX < 0)
			Stats.m_HookLeftCount++;
		else if(HookX > 0)
			Stats.m_HookRightCount++;
	}

	Stats.m_WasHooking = IsHooking;
}

// ========== 收藏地图功能实现 ==========

bool CTClient::IsFavoriteMap(const char *pMapName) const
{
	if(!pMapName || pMapName[0] == '\0')
		return false;
	return m_FavoriteMaps.find(std::string(pMapName)) != m_FavoriteMaps.end();
}

void CTClient::AddFavoriteMap(const char *pMapName)
{
	if(!pMapName || pMapName[0] == '\0')
		return;
	m_FavoriteMaps.insert(std::string(pMapName));
	log_info("tclient", "Added favorite map: %s", pMapName);
}

void CTClient::RemoveFavoriteMap(const char *pMapName)
{
	if(!pMapName || pMapName[0] == '\0')
		return;
	m_FavoriteMaps.erase(std::string(pMapName));
	log_info("tclient", "Removed favorite map: %s", pMapName);
}

void CTClient::ClearFavoriteMaps()
{
	m_FavoriteMaps.clear();
	log_info("tclient", "Cleared all favorite maps");
}

void CTClient::ConAddFavoriteMap(IConsole::IResult *pResult, void *pUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	pThis->AddFavoriteMap(pResult->GetString(0));
}

void CTClient::ConRemoveFavoriteMap(IConsole::IResult *pResult, void *pUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	pThis->RemoveFavoriteMap(pResult->GetString(0));
}

void CTClient::ConClearFavoriteMaps(IConsole::IResult *pResult, void *pUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	pThis->ClearFavoriteMaps();
}

void CTClient::ConfigSaveFavoriteMaps(IConfigManager *pConfigManager, void *pUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	char aBuf[256];
	for(const auto &Map : pThis->m_FavoriteMaps)
	{
		str_format(aBuf, sizeof(aBuf), "add_favorite_map \"%s\"", Map.c_str());
		pConfigManager->WriteLine(aBuf);
	}
}

// ==================== Speech-to-Text (STT) ====================
#if defined(CONF_WHISPER)

void CTClient::InitStt()
{
	if(m_SttInitialized)
		return;

	log_info("stt", "Initializing Speech-to-Text...");

	// Set transcription callback
	m_Stt.SetTranscriptionCallback([this](const char *pText, bool IsFinal) {
		OnSttTranscription(pText, IsFinal);
	});

	// Initialize with model path
	if(m_Stt.Init(Console(), Storage(), g_Config.m_QmSttModelPath))
	{
		m_SttInitialized = true;
		log_info("stt", "Speech-to-Text initialized successfully");
	}
	else
	{
		log_error("stt", "Failed to initialize Speech-to-Text: %s", m_Stt.GetError());
	}
}

void CTClient::ConSttToggle(IConsole::IResult *pResult, void *pUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	int KeyPressed = pResult->GetInteger(0); // 1 = key down, 0 = key up

	if(KeyPressed)
	{
		// Key pressed - start recording
		if(!g_Config.m_QmSttEnabled)
		{
			log_info("stt", "STT is disabled (qm_stt_enabled = 0)");
			return;
		}

		if(!pThis->m_SttInitialized)
		{
			pThis->InitStt();
			if(!pThis->m_SttInitialized)
			{
				pThis->GameClient()->Echo("STT: 模型加载失败 (Model load failed)");
				return;
			}
		}

		if(pThis->m_Stt.IsRecording())
			return;

		// Set language before recording
		pThis->m_Stt.SetLanguage(g_Config.m_QmSttLanguage);

		pThis->m_Stt.StartRecording();
		pThis->m_SttRecordingState = 1;

		// Show recording bubble on local player
		int LocalClientId = pThis->GameClient()->m_Snap.m_LocalClientId;
		if(LocalClientId >= 0 && LocalClientId < MAX_CLIENTS)
		{
			CGameClient::CClientData &ClientData = pThis->GameClient()->m_aClients[LocalClientId];
			str_copy(ClientData.m_aChatBubbleText, "🎤 录音中...");
			ClientData.m_ChatBubbleStartTick = time_get();
			ClientData.m_ChatBubbleExpireTick = time_get() + time_freq() * 60;
		}
	}
	else
	{
		// Key released - stop recording and transcribe
		if(!pThis->m_SttInitialized || !pThis->m_Stt.IsRecording())
			return;

		pThis->m_Stt.StopRecording();
		pThis->m_SttRecordingState = 0;

		// Show transcribing status in bubble
		int LocalClientId = pThis->GameClient()->m_Snap.m_LocalClientId;
		if(LocalClientId >= 0 && LocalClientId < MAX_CLIENTS)
		{
			CGameClient::CClientData &ClientData = pThis->GameClient()->m_aClients[LocalClientId];
			str_copy(ClientData.m_aChatBubbleText, "⏳ 识别中...");
			ClientData.m_ChatBubbleStartTick = time_get();
			ClientData.m_ChatBubbleExpireTick = time_get() + time_freq() * 30;
		}
	}
}

void CTClient::OnSttTranscription(const char *pText, bool IsFinal)
{
	int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalClientId < 0 || LocalClientId >= MAX_CLIENTS)
		return;

	CGameClient::CClientData &ClientData = GameClient()->m_aClients[LocalClientId];

	if(!pText || pText[0] == '\0')
	{
		// Show no speech detected in bubble
		str_copy(ClientData.m_aChatBubbleText, "❌ 未检测到语音");
		ClientData.m_ChatBubbleStartTick = time_get();
		ClientData.m_ChatBubbleExpireTick = time_get() + time_freq() * 2;
		return;
	}

	// Trim leading/trailing whitespace
	char aText[2048];
	str_copy(aText, pText);
	str_utf8_trim_right(aText);

	// Skip if empty after trim
	const char *pTrimmed = aText;
	while(*pTrimmed == ' ')
		pTrimmed++;
	if(*pTrimmed == '\0')
	{
		str_copy(ClientData.m_aChatBubbleText, "❌ 未检测到语音");
		ClientData.m_ChatBubbleStartTick = time_get();
		ClientData.m_ChatBubbleExpireTick = time_get() + time_freq() * 2;
		return;
	}

	log_info("stt", "Transcription: \"%s\"", pTrimmed);

	// Store transcription for sending
	str_copy(m_aSttTranscription, pTrimmed);
	m_SttHasTranscription = true;

	// Show transcription in bubble for preview
	str_copy(ClientData.m_aChatBubbleText, pTrimmed);
	ClientData.m_ChatBubbleStartTick = time_get();
	ClientData.m_ChatBubbleExpireTick = time_get() + time_freq() * g_Config.m_TcChatBubbleDuration;

	// Auto send if enabled
	if(g_Config.m_QmSttAutoSend)
	{
		int Team = g_Config.m_QmSttTeamChat ? 1 : 0;
		GameClient()->m_Chat.SendChat(Team, pTrimmed);
	}
}

#endif // CONF_WHISPER

void CTClient::ConSaveList(IConsole::IResult *pResult, void *pUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	// 如果用户指定了地图名，使用指定的；否则使用当前地图
	const char *pFilterMap = pResult->NumArguments() > 0 ? pResult->GetString(0) : pThis->Client()->GetCurrentMap();

	// 打开本地存档文件
	IOHANDLE File = pThis->Storage()->OpenFile(SAVES_FILE, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
	{
		pThis->GameClient()->Echo("No local saves file found (ddnet-saves.txt)");
		return;
	}

	// 读取整个文件内容
	char *pFileContent = io_read_all_str(File);
	io_close(File);

	if(!pFileContent)
	{
		pThis->GameClient()->Echo("Failed to read saves file");
		return;
	}

	int Count = 0;
	bool IsFirstLine = true;

	// 显示标题
	char aTitle[256];
	if(pFilterMap && pFilterMap[0] != '\0')
		str_format(aTitle, sizeof(aTitle), "=== Saves for '%s' ===", pFilterMap);
	else
		str_copy(aTitle, "=== All Local Saves ===");
	pThis->GameClient()->Echo(aTitle);

	// 逐行处理
	char *pLine = pFileContent;
	while(*pLine)
	{
		// 找到行尾
		char *pLineEnd = pLine;
		while(*pLineEnd && *pLineEnd != '\n' && *pLineEnd != '\r')
			pLineEnd++;

		// 保存行尾字符并临时设为结束符
		char LineEndChar = *pLineEnd;
		if(*pLineEnd)
			*pLineEnd = '\0';

		// 处理这一行
		if(pLine[0] != '\0')
		{
			// 跳过表头
			if(IsFirstLine)
			{
				IsFirstLine = false;
				if(!str_startswith(pLine, "Time"))
				{
					// 如果第一行不是表头，也要处理
					IsFirstLine = false;
				}
				else
				{
					// 跳过表头行
					goto next_line;
				}
			}

			// 解析 CSV 行: Time,Players,Map,Code
			char aTime[64] = {0};
			char aPlayers[256] = {0};
			char aMap[128] = {0};
			char aCode[128] = {0};

			// 简单的 CSV 解析
			const char *pCurrent = pLine;
			char *apFields[4] = {aTime, aPlayers, aMap, aCode};
			int FieldSizes[4] = {sizeof(aTime), sizeof(aPlayers), sizeof(aMap), sizeof(aCode)};
			int FieldIndex = 0;
			bool InQuotes = false;

			for(int i = 0; pCurrent[i] && FieldIndex < 4; i++)
			{
				if(pCurrent[i] == '"')
				{
					InQuotes = !InQuotes;
				}
				else if(pCurrent[i] == ',' && !InQuotes)
				{
					FieldIndex++;
				}
				else if(FieldIndex < 4)
				{
					int Len = str_length(apFields[FieldIndex]);
					if(Len < FieldSizes[FieldIndex] - 1)
					{
						apFields[FieldIndex][Len] = pCurrent[i];
						apFields[FieldIndex][Len + 1] = '\0';
					}
				}
			}

			// 过滤地图
			if(pFilterMap && pFilterMap[0] != '\0')
			{
				// 精确匹配地图名（不区分大小写）
				if(str_comp_nocase(aMap, pFilterMap) != 0)
					goto next_line;
			}

			// 输出格式: [玩家名] 密码 (地图: xxx, 保存时间: xxx)
			char aOutput[512];
			str_format(aOutput, sizeof(aOutput), "[%s] %s (Map: %s, Time: %s)",
				aPlayers[0] ? aPlayers : "Unknown",
				aCode[0] ? aCode : "no-code",
				aMap[0] ? aMap : "Unknown",
				aTime[0] ? aTime : "Unknown");
			pThis->GameClient()->Echo(aOutput);
			Count++;
		}

next_line:
		// 恢复行尾字符并移动到下一行
		if(LineEndChar)
		{
			*pLineEnd = LineEndChar;
			pLine = pLineEnd + 1;
			// 跳过 \r\n 的第二个字符
			if(LineEndChar == '\r' && *pLine == '\n')
				pLine++;
		}
		else
		{
			break;
		}
	}

	free(pFileContent);

	char aCountMsg[128];
	str_format(aCountMsg, sizeof(aCountMsg), "Total: %d save(s)", Count);
	pThis->GameClient()->Echo(aCountMsg);
}

// ========== 复读功能 ==========

void CTClient::ConRepeat(IConsole::IResult *pResult, void *pUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	if(!pThis->GameClient())
		return;
	
	// +命令：按键按下时触发
	pThis->RepeatLastMessage();
}

bool CTClient::OnInput(const IInput::CEvent &Event)
{
	// 不再需要在这里处理复读功能，已通过 +qm_repeat 命令绑定
	return false;
}

void CTClient::RepeatLastMessage()
{
	// 检查是否有消息可以复读
	if(m_aLastChatMessage[0] == '\0')
	{
		GameClient()->m_Chat.AddLine(-2, 0, "无消息可复读");
		return;
	}

	// 检查冷却时间（1秒）
	int64_t Now = time_get();
	if(Now - m_LastRepeatTime < time_freq())
	{
		return;
	}
	m_LastRepeatTime = Now;

	// 发送复读消息
	GameClient()->m_Chat.SendChat(0, m_aLastChatMessage);
}
