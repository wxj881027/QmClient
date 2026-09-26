/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#include "gamecontext.h"
#include "player.h"
#include "score.h"

#include <base/log.h>
#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/localization.h>
#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamemodes/ddnet.h>
#include <game/server/teams.h>
#include <game/team_state.h>
#include <game/teamscore.h>
#include <game/version.h>

static const char *LocalizeChatHelp(CGameContext *pSelf, const char *pText)
{
	if(pText == nullptr || pText[0] == '\0')
		return pText;

	static CLocalizationDatabase s_SimplifiedChineseLocalization;
	static bool s_LoadedSimplifiedChineseLocalization = false;
	if(!s_LoadedSimplifiedChineseLocalization)
	{
		s_LoadedSimplifiedChineseLocalization = true;
		if(!s_SimplifiedChineseLocalization.Load("languages/simplified_chinese.txt", pSelf->Storage(), pSelf->Console()))
			log_error("chat-help", "failed to load simplified chinese localization");
	}

	const char *pLocalizedText = s_SimplifiedChineseLocalization.FindString(str_quickhash(pText), str_quickhash(""));
	return pLocalizedText != nullptr ? pLocalizedText : pText;
}

void CGameContext::ConCredits(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	static constexpr const char *CREDITS[] = {
		"DDNet is run by the DDNet staff (DDNet.org/staff)",
		"Great maps and many ideas from the great community",
		"Help and code by eeeee, HMH, east, CookieMichal, Learath2,",
		"Savander, laxa, Tobii, BeaR, Wohoo, nuborn, timakro, Shiki,",
		"trml, Soreu, hi_leute_gll, Lady Saavik, Chairn, heinrich5991,",
		"swick, oy, necropotame, Ryozuki, Redix, d3fault, marcelherd,",
		"BannZay, ACTom, SiuFuWong, PathosEthosLogos, TsFreddie,",
		"Jupeyy, noby, ChillerDragon, ZombieToad, weez15, z6zzz,",
		"Piepow, QingGo, RafaelFF, sctt, jao, daverck, fokkonaut,",
		"Bojidar, FallenKN, ardadem, archimede67, sirius1242, Aerll,",
		"trafilaw, Zwelf, Patiga, Konsti, ElXreno, MikiGamer,",
		"Fireball, Banana090, axblk, yangfl, Kaffeine, Zodiac,",
		"c0d3d3v, GiuCcc, Ravie, Robyt3, simpygirl, Tater, Cellegen,",
		"srdante, Nouaa, Voxel, luk51, Vy0x2, Avolicious, louis,",
		"Marmare314, hus3h, ArijanJ, tarunsamanta2k20, Possseidon,",
		"+KZ, Teero, furo, dobrykafe, Moiman, JSaurusRex,",
		"Steinchen, ewancg, gerdoe-jr, melon, KebsCS, bencie,",
		"DynamoFox, MilkeeyCat, iMilchshake, SchrodingerZhu,",
		"catseyenebulous, Rei-Tw, Matodor, Emilcha, art0007i, SollyBunny,",
		"0xfaulty & others",
		"Based on DDRace by the DDRace developers,",
		"which is a mod of Teeworlds by the Teeworlds developers.",
	};
	for(const char *pLine : CREDITS)
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", pLine);
}

void CGameContext::ConInfo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"DDraceNetwork 模组版本: " GAME_VERSION);
	//DDraceNetwork Mod. Version:
	if(GIT_SHORTREV_HASH)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "Git 提交哈希: %s", GIT_SHORTREV_HASH);
		//Git revision hash: %s
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", aBuf);
	}
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"官方网站: DDNet.org");
	//Official site: DDNet.org
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"更多命令请查看: /cmdlist");
	//For more info: /cmdlist
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"或访问 DDNet.org");
	//Or visit DDNet.org
}

void CGameContext::ConList(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->m_ClientId;
	if(!CheckClientId(ClientId))
		return;

	if(pResult->NumArguments() > 0)
		pSelf->List(ClientId, pResult->GetString(0));
	else
		pSelf->List(ClientId, "");
}

void CGameContext::ConHelp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(pResult->NumArguments() == 0)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"/cmdlist 会显示所有聊天命令列表");
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"/help + 任意命令 会显示该命令的帮助");
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"例如 /help settings 会显示 /settings 的帮助");
	}
	else
	{
		const char *pArg = pResult->GetString(0);
		const IConsole::ICommandInfo *pCmdInfo =
			pSelf->Console()->GetCommandInfo(pArg, CFGFLAG_SERVER | CFGFLAG_CHAT, false);
		if(pCmdInfo)
		{
			if(pCmdInfo->Params())
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "用法：%s %s", pCmdInfo->Name(), pCmdInfo->Params());
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", aBuf);
			}

			if(pCmdInfo->Help())
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", LocalizeChatHelp(pSelf, pCmdInfo->Help()));
		}
		else
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "未知命令：%s", pArg);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", aBuf);
		}
	}
}

void CGameContext::ConSettings(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(pResult->NumArguments() == 0)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"输入 /settings 加设置名即可查看服务器设置。可用设置有：");
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"teams, cheats, collision, hooking, endlesshooking,");
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"hitting, oldlaser, timeout, votes, pause and scores");
	}
	else
	{
		const char *pArg = pResult->GetString(0);
		char aBuf[256];
		float ColTemp;
		float HookTemp;
		pSelf->GlobalTuning()->Get("player_collision", &ColTemp);
		pSelf->GlobalTuning()->Get("player_hooking", &HookTemp);
		if(str_comp_nocase(pArg, "teams") == 0)
		{
			str_format(aBuf, sizeof(aBuf), "%s %s",
				g_Config.m_SvTeam == SV_TEAM_ALLOWED ?
					"本服务器允许组队" :
				(g_Config.m_SvTeam == SV_TEAM_FORBIDDEN || g_Config.m_SvTeam == SV_TEAM_FORCED_SOLO) ?
					"本服务器不允许组队" :
					"你必须加入队伍才能在本服务器游玩",
				"；队伍上锁后，队内任意玩家死亡都会导致全队死亡");
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", aBuf);
		}
		else if(str_comp_nocase(pArg, "cheats") == 0)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				g_Config.m_SvTestingCommands ?
					"本服务器已开启作弊功能" :
					"本服务器已关闭作弊功能");
		}
		else if(str_comp_nocase(pArg, "collision") == 0)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				ColTemp ?
					"本服务器允许玩家碰撞" :
					"本服务器不允许玩家碰撞");
		}
		else if(str_comp_nocase(pArg, "hooking") == 0)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				HookTemp ?
					"本服务器允许玩家互钩" :
					"本服务器不允许玩家互钩");
		}
		else if(str_comp_nocase(pArg, "endlesshooking") == 0)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				g_Config.m_SvEndlessDrag ?
					"本服务器的钩子时长不受限制" :
					"本服务器的钩子时长受限制");
		}
		else if(str_comp_nocase(pArg, "hitting") == 0)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				g_Config.m_SvHit ?
					"本服务器允许武器影响其他玩家" :
					"本服务器的武器不会影响其他玩家");
		}
		else if(str_comp_nocase(pArg, "oldlaser") == 0)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				g_Config.m_SvOldLaser ?
					"你自己发射的激光也会命中自己，并把你拉向反弹起点（类似 DDRace Beta）" :
					"你自己发射的激光不会命中自己，激光会把其他玩家拉向发射者");
		}
		else if(str_comp_nocase(pArg, "timeout") == 0)
		{
			str_format(aBuf, sizeof(aBuf), "当前服务器超时时间为 %d 秒", g_Config.m_ConnTimeout);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", aBuf);
		}
		else if(str_comp_nocase(pArg, "votes") == 0)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				g_Config.m_SvVoteKick ?
					"玩家可以通过 Callvote 菜单发起踢人投票" :
					"玩家不能通过 Callvote 菜单发起踢人投票");
			if(g_Config.m_SvVoteKick)
			{
				str_format(aBuf, sizeof(aBuf),
					"被投票踢出的玩家会被封禁 %d 分钟", g_Config.m_SvVoteKickBantime);

				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
					g_Config.m_SvVoteKickBantime ?
						aBuf :
						"被投票踢出的玩家只会被踢出，不会被封禁");
			}
		}
		else if(str_comp_nocase(pArg, "pause") == 0)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				g_Config.m_SvPauseable ?
					"输入 /spec 后你会暂停，tee 也会消失" :
					"输入 /spec 后你会暂停，但 tee 不会消失");
		}
		else if(str_comp_nocase(pArg, "scores") == 0)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				g_Config.m_SvHideScore ?
					"本服务器的成绩是私密的" :
					"本服务器的成绩是公开的");
		}
		else
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"没有找到对应设置。输入 /settings 可以查看可用设置");
		}
	}
}

void CGameContext::ConRules(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	bool Printed = false;
	if(g_Config.m_SvDDRaceRules)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"请友善交流。");
		//Be nice.
		Printed = true;
	}
	char *apRuleLines[] = {
		g_Config.m_SvRulesLine1,
		g_Config.m_SvRulesLine2,
		g_Config.m_SvRulesLine3,
		g_Config.m_SvRulesLine4,
		g_Config.m_SvRulesLine5,
		g_Config.m_SvRulesLine6,
		g_Config.m_SvRulesLine7,
		g_Config.m_SvRulesLine8,
		g_Config.m_SvRulesLine9,
		g_Config.m_SvRulesLine10,
	};
	for(auto &pRuleLine : apRuleLines)
	{
		if(pRuleLine[0])
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD,
				"chatresp", pRuleLine);
			Printed = true;
		}
	}
	if(!Printed)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"未设置服务器规则，请联系管理员。");
		//No Rules Defined, Kill em all!!
	}
}

static void ToggleSpecPause(IConsole::IResult *pResult, void *pUserData, int PauseType)
{
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CGameContext *pSelf = (CGameContext *)pUserData;
	IServer *pServ = pSelf->Server();
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	int PauseState = pPlayer->IsPaused();
	if(PauseState > 0)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "你被强制暂停，还需等待 %d 秒。", (PauseState - pServ->Tick()) / pServ->TickSpeed());
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", aBuf);
	}
	else if(pResult->NumArguments() > 0)
	{
		if(-PauseState == PauseType && pPlayer->SpectatorId() != pResult->m_ClientId && pServ->ClientIngame(pPlayer->SpectatorId()) && !str_comp(pServ->ClientName(pPlayer->SpectatorId()), pResult->GetString(0)))
		{
			pPlayer->Pause(CPlayer::PAUSE_NONE, false);
		}
		else
		{
			pPlayer->Pause(PauseType, false);
			pPlayer->SpectatePlayerName(pResult->GetString(0));
		}
	}
	else if(-PauseState != CPlayer::PAUSE_NONE && PauseType != CPlayer::PAUSE_NONE)
	{
		pPlayer->Pause(CPlayer::PAUSE_NONE, false);
	}
	else if(-PauseState != PauseType)
	{
		pPlayer->Pause(PauseType, false);
	}
}

static void ToggleSpecPauseVoted(IConsole::IResult *pResult, void *pUserData, int PauseType)
{
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	int PauseState = pPlayer->IsPaused();
	if(PauseState > 0)
	{
		IServer *pServ = pSelf->Server();
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "你被强制暂停，还需等待 %d 秒。", (PauseState - pServ->Tick()) / pServ->TickSpeed());
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", aBuf);
		return;
	}

	bool IsPlayerBeingVoted = pSelf->m_VoteCloseTime &&
				  (pSelf->IsKickVote() || pSelf->IsSpecVote()) &&
				  pResult->m_ClientId != pSelf->m_VoteVictim;
	if((!IsPlayerBeingVoted && -PauseState == PauseType) ||
		(IsPlayerBeingVoted && PauseState && pPlayer->SpectatorId() == pSelf->m_VoteVictim))
	{
		pPlayer->Pause(CPlayer::PAUSE_NONE, false);
	}
	else
	{
		pPlayer->Pause(PauseType, false);
		if(IsPlayerBeingVoted)
			pPlayer->SetSpectatorId(pSelf->m_VoteVictim);
	}
}

void CGameContext::ConToggleSpec(IConsole::IResult *pResult, void *pUserData)
{
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	int PauseType = g_Config.m_SvPauseable ? CPlayer::PAUSE_SPEC : CPlayer::PAUSE_PAUSED;

	if(pPlayer->GetCharacter())
	{
		CGameTeams &Teams = pSelf->m_pController->Teams();
		if(Teams.IsPractice(Teams.m_Core.Team(pResult->m_ClientId)))
			PauseType = CPlayer::PAUSE_SPEC;
	}

	ToggleSpecPause(pResult, pUserData, PauseType);
}

void CGameContext::ConToggleSpecVoted(IConsole::IResult *pResult, void *pUserData)
{
	ToggleSpecPauseVoted(pResult, pUserData, g_Config.m_SvPauseable ? CPlayer::PAUSE_SPEC : CPlayer::PAUSE_PAUSED);
}

void CGameContext::ConTogglePause(IConsole::IResult *pResult, void *pUserData)
{
	ToggleSpecPause(pResult, pUserData, CPlayer::PAUSE_PAUSED);
}

void CGameContext::ConTogglePauseVoted(IConsole::IResult *pResult, void *pUserData)
{
	ToggleSpecPauseVoted(pResult, pUserData, CPlayer::PAUSE_PAUSED);
}

void CGameContext::ConTeamTop5(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	if(g_Config.m_SvHideScore)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"本服务器不允许查看队伍前 5 名");
		return;
	}

	if(pResult->NumArguments() == 0)
	{
		pSelf->Score()->ShowTeamTop5(pResult->m_ClientId, 1);
	}
	else if(pResult->NumArguments() == 1)
	{
		if(pResult->GetInteger(0) != 0)
		{
			pSelf->Score()->ShowTeamTop5(pResult->m_ClientId, pResult->GetInteger(0));
		}
		else
		{
			const char *pRequestedName = (str_comp_nocase(pResult->GetString(0), "me") == 0) ?
							     pSelf->Server()->ClientName(pResult->m_ClientId) :
							     pResult->GetString(0);
			pSelf->Score()->ShowPlayerTeamTop5(pResult->m_ClientId, pRequestedName, 0);
		}
	}
	else if(pResult->NumArguments() == 2 && pResult->GetInteger(1) != 0)
	{
		const char *pRequestedName = (str_comp_nocase(pResult->GetString(0), "me") == 0) ?
						     pSelf->Server()->ClientName(pResult->m_ClientId) :
						     pResult->GetString(0);
		pSelf->Score()->ShowPlayerTeamTop5(pResult->m_ClientId, pRequestedName, pResult->GetInteger(1));
	}
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "/top5team 需要 0、1 或 2 个参数。第 1 个是名字，第 2 个是起始名次");
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "示例：/top5team、/top5team me、/top5team Hans、/top5team \"Papa Smurf\" 5");
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "错误示例：/top5team Papa Smurf 5；正确示例：/top5team \"Papa Smurf\" 5");
	}
}

void CGameContext::ConTop(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	if(g_Config.m_SvHideScore)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"本服务器不允许查看排行榜");
		return;
	}

	if(pResult->NumArguments() > 0)
		pSelf->Score()->ShowTop(pResult->m_ClientId, pResult->GetInteger(0));
	else
		pSelf->Score()->ShowTop(pResult->m_ClientId);
}

void CGameContext::ConTimes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	int Offset = 1;
	const char *pRequestedName = nullptr;

	// input validation
	if(pResult->NumArguments() == 1)
	{
		if(pResult->GetInteger(0) != 0)
		{
			Offset = pResult->GetInteger(0);
		}
		else
		{
			pRequestedName = pResult->GetString(0);
		}
	}
	else if(pResult->NumArguments() == 2)
	{
		pRequestedName = pResult->GetString(0);
		Offset = pResult->GetInteger(1);
	}
	else if(pResult->NumArguments() > 2)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "/times 需要 0、1 或 2 个参数。第 1 个是名字，第 2 个是起始名次");
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "示例：/times、/times me、/times Hans、/times \"Papa Smurf\" 5");
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "错误示例：/times Papa Smurf 5；正确示例：/times \"Papa Smurf\" 5");
		return;
	}

	// execution
	if(g_Config.m_SvHideScore)
	{
		if(pRequestedName && str_comp_nocase(pRequestedName, "me") != 0 && str_comp_nocase(pRequestedName, pSelf->Server()->ClientName(pResult->m_ClientId)) != 0)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "本服务器不允许查看其他玩家的成绩");
			return;
		}
		pRequestedName = pSelf->Server()->ClientName(pResult->m_ClientId);
		pSelf->Score()->ShowTimes(pResult->m_ClientId, pRequestedName, Offset);
	}
	else if(!pRequestedName)
	{
		pSelf->Score()->ShowTimes(pResult->m_ClientId, Offset);
	}
	else
	{
		if(str_comp_nocase(pRequestedName, "me") == 0)
			pRequestedName = pSelf->Server()->ClientName(pResult->m_ClientId);
		pSelf->Score()->ShowTimes(pResult->m_ClientId, pRequestedName, Offset);
	}
}

void CGameContext::ConDND(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	pPlayer->m_DND = pResult->NumArguments() == 0 ? !pPlayer->m_DND : pResult->GetInteger(0);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", pPlayer->m_DND ? "你将不再接收全局聊天和服务器消息" : "你将继续接收全局聊天和服务器消息");
}

void CGameContext::ConWhispers(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	pPlayer->m_Whispers = pResult->NumArguments() == 0 ? !pPlayer->m_Whispers : pResult->GetInteger(0);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", pPlayer->m_Whispers ? "你现在会收到私聊消息" : "你将不再收到私聊消息");
}

void CGameContext::ConMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	if(g_Config.m_SvMapVote == 0)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"本服务器已禁用 /map");
		return;
	}

	if(pResult->NumArguments() <= 0)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "示例：/map adr3 可以发起 Adrenaline 3 的换图投票。这表示地图名必须以 'a' 开头，并按顺序包含 'd'、'r'、'3'");
		return;
	}

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	if(pSelf->RateLimitPlayerVote(pResult->m_ClientId) || pSelf->RateLimitPlayerMapVote(pResult->m_ClientId))
		return;

	pSelf->Score()->MapVote(pResult->m_ClientId, pResult->GetString(0));
}

void CGameContext::ConMapInfo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	// use cached map info for current map
	const bool IsCurrentMap = pResult->NumArguments() == 0 || str_comp_nocase(pResult->GetString(0), pSelf->Server()->GetMapName()) == 0;
	if(IsCurrentMap && pSelf->m_aMapInfoMessage[0] != '\0')
	{
		pSelf->SendChatTarget(pResult->m_ClientId, pSelf->m_aMapInfoMessage);
		return;
	}

	if(pResult->NumArguments() > 0)
		pSelf->Score()->MapInfo(pResult->m_ClientId, pResult->GetString(0));
	else
		pSelf->Score()->MapInfo(pResult->m_ClientId, pSelf->Server()->GetMapName());
}

void CGameContext::ConTimeout(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	const char *pTimeout = pResult->NumArguments() > 0 ? pResult->GetString(0) : pPlayer->m_aTimeoutCode;

	for(int i = 0; i < pSelf->Server()->MaxClients(); i++)
	{
		if(i == pResult->m_ClientId)
			continue;
		if(!pSelf->m_apPlayers[i])
			continue;
		if(str_comp(pSelf->m_apPlayers[i]->m_aTimeoutCode, pTimeout))
			continue;
		if(pSelf->Server()->SetTimedOut(i, pResult->m_ClientId))
		{
			if(pSelf->m_apPlayers[i]->GetCharacter())
				pSelf->SendTuningParams(i, pSelf->m_apPlayers[i]->GetCharacter()->m_TuneZone);
			return;
		}
	}

	pSelf->Server()->SetTimeoutProtected(pResult->m_ClientId);
	str_copy(pPlayer->m_aTimeoutCode, pResult->GetString(0), sizeof(pPlayer->m_aTimeoutCode));
}

void CGameContext::ConPractice(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	if(pSelf->ProcessSpamProtection(pResult->m_ClientId, false))
		return;

	if(!g_Config.m_SvPractice)
	{
		pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"本服务器已禁用练习模式");
		return;
	}

	CGameTeams &Teams = pSelf->m_pController->Teams();

	int Team = Teams.m_Core.Team(pResult->m_ClientId);

	if(!Teams.IsValidTeamNumber(Team) || (Team == TEAM_FLOCK && g_Config.m_SvTeam != SV_TEAM_FORCED_SOLO))
	{
		pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"先加入队伍才能开启练习模式。开启后可以使用 /r，但不会获得排名");
		return;
	}

	if(Teams.TeamFlock(Team))
	{
		pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"0 队模式下不能开启练习模式");
		return;
	}

	if(Teams.GetSaving(Team))
	{
		pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"队伍正在存档或读档时，不能开启练习模式");
		return;
	}

	if(Teams.IsPractice(Team))
	{
		pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"队伍已经处于练习模式");
		return;
	}

	bool VotedForPractice = pResult->NumArguments() == 0 || pResult->GetInteger(0);

	if(VotedForPractice == pPlayer->m_VotedForPractice)
		return;

	pPlayer->m_VotedForPractice = VotedForPractice;

	int NumCurrentVotes = 0;
	int TeamSize = 0;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(Teams.m_Core.Team(i) == Team)
		{
			CPlayer *pPlayer2 = pSelf->m_apPlayers[i];
			if(pPlayer2 && pPlayer2->m_VotedForPractice)
				NumCurrentVotes++;
			TeamSize++;
		}
	}

	int NumRequiredVotes = TeamSize / 2 + 1;

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "'%s' 发起了%s队伍练习模式投票。开启后可以使用练习命令，但不会获得排名。输入 /practice 投票（当前 %d/%d 票）", pSelf->Server()->ClientName(pResult->m_ClientId), VotedForPractice ? "开启" : "关闭", NumCurrentVotes, NumRequiredVotes);
	pSelf->SendChatTeam(Team, aBuf);

	if(NumCurrentVotes >= NumRequiredVotes)
	{
		Teams.SetPractice(Team, true);
		pSelf->SendChatTeam(Team, "你的队伍已开启练习模式，祝你练习愉快！");
		pSelf->SendChatTeam(Team, "输入 /practicecmdlist 可以查看所有可用的练习命令。最常用的是 /telecursor、/lasttp 和 /rescue");
	}
}

void CGameContext::ConUnPractice(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	if(pSelf->ProcessSpamProtection(pResult->m_ClientId, false))
		return;

	CGameTeams &Teams = pSelf->m_pController->Teams();

	int Team = Teams.m_Core.Team(pResult->m_ClientId);

	if(g_Config.m_SvTeam != SV_TEAM_FORCED_SOLO && Team == TEAM_FLOCK)
	{
		log_info("chatresp", "Practice mode can't be disabled for team 0");
		return;
	}

	if(!Teams.IsPractice(Team))
	{
		log_info("chatresp", "Team isn't in practice mode");
		return;
	}

	if(Teams.GetSaving(Team))
	{
		log_info("chatresp", "Practice mode can't be disabled while team save or load is in progress");
		return;
	}

	if(Teams.TeamSize(Team) > g_Config.m_SvMaxTeamSize && pSelf->m_pController->Teams().TeamLocked(Team))
	{
		log_info("chatresp", "Can't disable practice. This team exceeds the maximum allowed size of %d players for regular team", g_Config.m_SvMaxTeamSize);
		return;
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(Teams.m_Core.Team(i) == Team)
		{
			CPlayer *pPlayer2 = pSelf->m_apPlayers[i];
			if(pPlayer2)
			{
				if(pPlayer2->m_VotedForPractice)
					pPlayer2->m_VotedForPractice = false;

				if(!g_Config.m_SvPauseable && pPlayer2->IsPaused() == -1 * CPlayer::PAUSE_SPEC)
					pPlayer2->Pause(CPlayer::PAUSE_PAUSED, true);
			}
		}
	}

	// send before kill, in case team isn't locked
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "'%s' 关闭了你们队伍的练习模式", pSelf->Server()->ClientName(pResult->m_ClientId));
	pSelf->SendChatTeam(Team, aBuf);

	Teams.KillCharacterOrTeam(pResult->m_ClientId, Team);
	Teams.SetPractice(Team, false);
	pPlayer->Respawn(); // set spawn as strong hook
}

void CGameContext::ConPracticeCmdList(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	char aPracticeCommands[256] = "可用练习命令：";
	for(const IConsole::ICommandInfo *pCmd = pSelf->Console()->FirstCommandInfo(pResult->m_ClientId, CMDFLAG_PRACTICE);
		pCmd; pCmd = pSelf->Console()->NextCommandInfo(pCmd, pResult->m_ClientId, CMDFLAG_PRACTICE))
	{
		char aCommand[64];

		str_format(aCommand, sizeof(aCommand), "/%s%s", pCmd->Name(), pSelf->Console()->NextCommandInfo(pCmd, pResult->m_ClientId, CMDFLAG_PRACTICE) ? ", " : "");

		if(str_length(aCommand) + str_length(aPracticeCommands) > 255)
		{
			pSelf->SendChatTarget(pResult->m_ClientId, aPracticeCommands);
			aPracticeCommands[0] = '\0';
		}
		str_append(aPracticeCommands, aCommand);
	}
	pSelf->SendChatTarget(pResult->m_ClientId, aPracticeCommands);
}

void CGameContext::ConSwap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pName = pResult->GetString(0);

	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	if(!g_Config.m_SvSwap)
	{
		pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"本服务器已禁用交换功能");
		return;
	}

	if(g_Config.m_SvTeam == SV_TEAM_FORCED_SOLO)
	{
		pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"强制 solo 服务器上不能使用交换功能");
		return;
	}

	CGameTeams &Teams = pSelf->m_pController->Teams();

	int Team = Teams.m_Core.Team(pResult->m_ClientId);

	if(Team == Teams.m_Core.TeamSuper())
	{
		pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"关闭 super 后才能使用交换功能，也就是和队友互换位置");
		return;
	}

	if(!Teams.IsValidTeamNumber(Team))
	{
		pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"先加入队伍后才能使用交换功能，也就是和队友互换位置");
		return;
	}

	int TargetClientId = -1;
	if(pResult->NumArguments() == 1)
	{
		TargetClientId = pSelf->FindClientIdByName(pName).value_or(-1);
	}
	else
	{
		int TeamSize = 1;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(pSelf->m_apPlayers[i] && Teams.m_Core.Team(i) == Team && i != pResult->m_ClientId)
			{
				TargetClientId = i;
				TeamSize++;
			}
		}
		if(TeamSize != 2)
			TargetClientId = -1;
	}

	if(TargetClientId < 0)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "未找到该玩家");
		return;
	}

	if(TargetClientId == pResult->m_ClientId)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "你不能和自己交换位置");
		return;
	}

	int TargetTeam = Teams.m_Core.Team(TargetClientId);
	if(TargetTeam != Team)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "目标玩家不在你的队伍里");
		return;
	}

	CPlayer *pSwapPlayer = pSelf->m_apPlayers[TargetClientId];
	if(Team == TEAM_FLOCK || Teams.TeamFlock(Team))
	{
		CCharacter *pChr = pPlayer->GetCharacter();
		CCharacter *pSwapChr = pSwapPlayer->GetCharacter();
		if(!pChr || !pSwapChr || pChr->m_DDRaceState != ERaceState::STARTED || pSwapChr->m_DDRaceState != ERaceState::STARTED)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "你和对方都需要先开始地图，才能交换位置");
			return;
		}
	}
	else if(!Teams.IsStarted(Team) && !Teams.TeamFlock(Team))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "你需要先开始地图，才能和其他玩家交换位置");
		return;
	}
	if(pSelf->m_World.m_Core.m_apCharacters[pResult->m_ClientId] == nullptr || pSelf->m_World.m_Core.m_apCharacters[TargetClientId] == nullptr)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "你和对方都不能处于暂停状态，才能交换位置");
		return;
	}

	bool SwapPending = pSwapPlayer->m_SwapTargetsClientId != pResult->m_ClientId;
	if(SwapPending)
	{
		if(pSelf->ProcessSpamProtection(pResult->m_ClientId))
			return;

		Teams.RequestTeamSwap(pPlayer, pSwapPlayer, Team);
		return;
	}

	Teams.SwapTeamCharacters(pPlayer, pSwapPlayer, Team);
}

void CGameContext::ConCancelSwap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	if(!g_Config.m_SvSwap)
	{
		pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"本服务器已禁用交换功能");
		return;
	}

	if(g_Config.m_SvTeam == SV_TEAM_FORCED_SOLO)
	{
		pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"强制 solo 服务器上不能使用交换功能");
		return;
	}

	CGameTeams &Teams = pSelf->m_pController->Teams();

	int Team = Teams.m_Core.Team(pResult->m_ClientId);

	bool SwapPending = pPlayer->m_SwapTargetsClientId != -1 && !pSelf->Server()->ClientSlotEmpty(pPlayer->m_SwapTargetsClientId);

	if(!SwapPending)
	{
		pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"你当前没有待处理的交换请求");
		return;
	}

	Teams.CancelTeamSwap(pPlayer, Team);
}

void CGameContext::ConSave(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	if(!g_Config.m_SvSaveGames)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "本服务器已禁用存档功能");
		return;
	}

	const char *pCode = "";
	if(pResult->NumArguments() > 0)
		pCode = pResult->GetString(0);

	pSelf->Score()->SaveTeam(pResult->m_ClientId, pCode, g_Config.m_SvSqlServerName);
}

void CGameContext::ConLoad(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	if(!g_Config.m_SvSaveGames)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "本服务器已禁用存档功能");
		return;
	}

	if(pResult->NumArguments() > 0)
		pSelf->Score()->LoadTeam(pResult->GetString(0), pResult->m_ClientId);
	else
		pSelf->Score()->GetSaves(pResult->m_ClientId);
}

void CGameContext::ConSaveList(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	if(!g_Config.m_SvSaveGames)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "本服务器已禁用存档功能");
		return;
	}

	pSelf->Score()->ListSaves(pResult->m_ClientId);
}

void CGameContext::ConTeamRank(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	if(pResult->NumArguments() > 0)
	{
		if(!g_Config.m_SvHideScore)
			pSelf->Score()->ShowTeamRank(pResult->m_ClientId, pResult->GetString(0));
		else
			pSelf->Console()->Print(
				IConsole::OUTPUT_LEVEL_STANDARD,
				"chatresp",
				"本服务器不允许查看其他玩家的队伍排名");
	}
	else
		pSelf->Score()->ShowTeamRank(pResult->m_ClientId,
			pSelf->Server()->ClientName(pResult->m_ClientId));
}

void CGameContext::ConRank(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	if(pResult->NumArguments() > 0)
	{
		if(!g_Config.m_SvHideScore)
			pSelf->Score()->ShowRank(pResult->m_ClientId, pResult->GetString(0));
		else
			pSelf->Console()->Print(
				IConsole::OUTPUT_LEVEL_STANDARD,
				"chatresp",
				"本服务器不允许查看其他玩家的排名");
	}
	else
		pSelf->Score()->ShowRank(pResult->m_ClientId,
			pSelf->Server()->ClientName(pResult->m_ClientId));
}

void CGameContext::ConLock(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	if(g_Config.m_SvTeam == SV_TEAM_FORBIDDEN || g_Config.m_SvTeam == SV_TEAM_FORCED_SOLO)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"队伍功能已禁用");
		return;
	}

	int Team = pSelf->GetDDRaceTeam(pResult->m_ClientId);

	bool Lock = pSelf->m_pController->Teams().TeamLocked(Team);

	if(pResult->NumArguments() > 0)
		Lock = !pResult->GetInteger(0);

	if(Team == TEAM_FLOCK || !pSelf->m_pController->Teams().IsValidTeamNumber(Team))
	{
		pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"这个队伍不能被锁定");
		return;
	}

	if(pSelf->ProcessSpamProtection(pResult->m_ClientId, false))
		return;

	char aBuf[512];
	if(Lock)
	{
		pSelf->UnlockTeam(pResult->m_ClientId, Team);
	}
	else
	{
		pSelf->m_pController->Teams().SetTeamLock(Team, true);

		if(pSelf->m_pController->Teams().TeamFlock(Team))
			str_format(aBuf, sizeof(aBuf), "'%s' 锁定了你们的队伍", pSelf->Server()->ClientName(pResult->m_ClientId));
		else
			str_format(aBuf, sizeof(aBuf), "'%s' 锁定了你们的队伍。比赛开始后，任何人 kill 都会导致整队死亡", pSelf->Server()->ClientName(pResult->m_ClientId));
		pSelf->SendChatTeam(Team, aBuf);
	}
}

void CGameContext::ConUnlock(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	if(g_Config.m_SvTeam == SV_TEAM_FORBIDDEN || g_Config.m_SvTeam == SV_TEAM_FORCED_SOLO)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"队伍功能已禁用");
		return;
	}

	int Team = pSelf->GetDDRaceTeam(pResult->m_ClientId);

	if(Team == TEAM_FLOCK || !pSelf->m_pController->Teams().IsValidTeamNumber(Team))
		return;

	if(pSelf->ProcessSpamProtection(pResult->m_ClientId, false))
		return;

	pSelf->UnlockTeam(pResult->m_ClientId, Team);
}

void CGameContext::UnlockTeam(int ClientId, int Team) const
{
	m_pController->Teams().SetTeamLock(Team, false);

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "'%s' 解锁了你们的队伍", Server()->ClientName(ClientId));
	SendChatTeam(Team, aBuf);
}

void CGameContext::AttemptJoinTeam(int ClientId, int Team)
{
	CPlayer *pPlayer = m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(IsRunningKickOrSpecVote(ClientId))
	{
		Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"你正在发起投票，请等当前投票结束后再试");
		return;
	}
	else if(g_Config.m_SvTeam == SV_TEAM_FORBIDDEN || g_Config.m_SvTeam == SV_TEAM_FORCED_SOLO)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"队伍功能已禁用");
		return;
	}
	else if(g_Config.m_SvTeam == SV_TEAM_MANDATORY && Team == 0 && pPlayer->GetCharacter() && pPlayer->GetCharacter()->m_LastStartWarning < Server()->Tick() - 3 * Server()->TickSpeed())
	{
		Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"你必须加入一个队伍并和其他人一起玩，否则无法开始");
		pPlayer->GetCharacter()->m_LastStartWarning = Server()->Tick();
	}

	if(!m_pController->Teams().IsValidTeamNumber(Team))
	{
		auto EmptyTeam = m_pController->Teams().GetFirstEmptyTeam();
		if(!EmptyTeam.has_value())
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"已经没有空队伍了");
			return;
		}
		Team = EmptyTeam.value();
	}

	char aError[512];
	if(pPlayer->m_LastDDRaceTeamChange.has_value() && pPlayer->m_LastDDRaceTeamChange.value() + (int64_t)Server()->TickSpeed() * g_Config.m_SvTeamChangeDelay > Server()->Tick())
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"你切换队伍太快了");
	}
	else if(Team != TEAM_FLOCK && m_pController->Teams().TeamLocked(Team) && !m_pController->Teams().IsInvited(Team, ClientId))
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			g_Config.m_SvInvite ?
				"这个队伍已用 /lock 锁定，只有队伍成员才能用 /lock 解锁" :
				"这个队伍已用 /lock 锁定，只有队伍成员才能邀请你或用 /lock 解锁");
	}
	else if(Team != TEAM_FLOCK && m_pController->Teams().TeamSize(Team) >= g_Config.m_SvMaxTeamSize && !m_pController->Teams().TeamFlock(Team) && !m_pController->Teams().IsPractice(Team))
	{
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "这个队伍已经达到最大人数上限 %d", g_Config.m_SvMaxTeamSize);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", aBuf);
	}
	else if(!m_pController->Teams().SetCharacterTeam(pPlayer->GetCid(), Team, aError, sizeof(aError)))
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", aError);
	}
	else
	{
		if(PracticeByDefault())
		{
			// joined an empty team
			if(m_pController->Teams().TeamSize(Team) == 1)
				m_pController->Teams().SetPractice(Team, true);
		}

		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "'%s' 加入了 %d 队",
			Server()->ClientName(pPlayer->GetCid()),
			Team);
		SendChat(-1, TEAM_ALL, aBuf);
		pPlayer->m_LastDDRaceTeamChange = Server()->Tick();

		if(m_pController->Teams().IsPractice(Team))
			SendChatTarget(pPlayer->GetCid(), "你的队伍已开启练习模式，祝你练习愉快！");

		if(m_pController->Teams().TeamFlock(Team))
			SendChatTarget(pPlayer->GetCid(), "你的队伍已开启 team 0 模式。现在会按 team 0 的规则运作。");
	}
}

void CGameContext::ConInvite(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	auto *pController = pSelf->m_pController;
	const char *pName = pResult->GetString(0);

	if(g_Config.m_SvTeam == SV_TEAM_FORBIDDEN || g_Config.m_SvTeam == SV_TEAM_FORCED_SOLO)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"队伍功能已禁用");
		return;
	}

	if(!g_Config.m_SvInvite)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "邀请功能已禁用");
		return;
	}

	int Team = pController->Teams().m_Core.Team(pResult->m_ClientId);
	if(Team != TEAM_FLOCK && pController->Teams().IsValidTeamNumber(Team))
	{
		int Target = pSelf->FindClientIdByName(pName).value_or(-1);
		if(Target == -1)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "未找到该玩家");
			return;
		}

		if(pController->Teams().IsInvited(Team, Target))
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "该玩家已经被邀请过了");
			return;
		}

		if(pSelf->m_apPlayers[pResult->m_ClientId] && pSelf->m_apPlayers[pResult->m_ClientId]->m_LastInvited + g_Config.m_SvInviteFrequency * pSelf->Server()->TickSpeed() > pSelf->Server()->Tick())
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "邀请过于频繁，请稍后再试");
			return;
		}

		pController->Teams().SetClientInvited(Team, Target, true);
		pSelf->m_apPlayers[pResult->m_ClientId]->m_LastInvited = pSelf->Server()->Tick();

		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "'%s' 邀请你加入 %d 队。输入 /team %d 即可加入。", pSelf->Server()->ClientName(pResult->m_ClientId), Team, Team);
		pSelf->SendChatTarget(Target, aBuf);

		str_format(aBuf, sizeof(aBuf), "'%s' 邀请了 '%s' 加入你们的队伍。", pSelf->Server()->ClientName(pResult->m_ClientId), pSelf->Server()->ClientName(Target));
		pSelf->SendChatTeam(Team, aBuf);
	}
	else
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "这个队伍不能邀请玩家");
}

void CGameContext::ConTeam0Mode(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	auto *pController = pSelf->m_pController;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	if(g_Config.m_SvTeam == SV_TEAM_FORBIDDEN || g_Config.m_SvTeam == SV_TEAM_FORCED_SOLO || g_Config.m_SvTeam == SV_TEAM_MANDATORY)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"队伍模式切换已禁用");
		return;
	}

	if(!g_Config.m_SvTeam0Mode)
	{
		pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"本服务器已禁用队伍模式切换。");
		return;
	}

	int Team = pController->Teams().m_Core.Team(pResult->m_ClientId);
	bool Mode = pController->Teams().TeamFlock(Team);

	if(Team == TEAM_FLOCK || !pController->Teams().IsValidTeamNumber(Team))
	{
		pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"这个队伍不能切换模式");
		return;
	}

	if(pController->Teams().GetTeamState(Team) != ETeamState::OPEN)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "比赛进行中不能切换队伍模式");
		return;
	}

	if(pResult->NumArguments() > 0)
		Mode = !pResult->GetInteger(0);

	if(pSelf->ProcessSpamProtection(pResult->m_ClientId, false))
		return;

	char aBuf[512];
	if(Mode)
	{
		if(pController->Teams().TeamSize(Team) > g_Config.m_SvMaxTeamSize)
		{
			str_format(aBuf, sizeof(aBuf), "无法关闭 team 0 模式。该队伍人数已超过普通队伍允许上限 %d", g_Config.m_SvMaxTeamSize);
			pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
		}
		else
		{
			pController->Teams().SetTeamFlock(Team, false);

			str_format(aBuf, sizeof(aBuf), "'%s' 关闭了 team 0 模式。", pSelf->Server()->ClientName(pResult->m_ClientId));
			pSelf->SendChatTeam(Team, aBuf);
		}
	}
	else
	{
		if(pController->Teams().IsPractice(Team))
		{
			pSelf->SendChatTarget(pResult->m_ClientId, "练习模式开启时不能启用 team 0 模式。");
		}
		else
		{
			pController->Teams().SetTeamFlock(Team, true);

			str_format(aBuf, sizeof(aBuf), "'%s' 开启了 team 0 模式。你们的队伍现在会按 team 0 规则运作。", pSelf->Server()->ClientName(pResult->m_ClientId));
			pSelf->SendChatTeam(Team, aBuf);
		}
	}
}

void CGameContext::ConTeam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	if(pResult->NumArguments() > 0)
	{
		pSelf->AttemptJoinTeam(pResult->m_ClientId, pResult->GetInteger(0));
	}
	else
	{
		char aBuf[512];
		if(!pPlayer->IsPlaying())
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "你死亡或处于旁观状态时，不能查看自己的队伍。");
		}
		else
		{
			int TeamSize = 0;
			const int PlayerTeam = pSelf->GetDDRaceTeam(pResult->m_ClientId);

			// Count players in team
			for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
			{
				const CPlayer *pOtherPlayer = pSelf->m_apPlayers[ClientId];
				if(!pOtherPlayer || !pOtherPlayer->IsPlaying())
					continue;

				if(pSelf->GetDDRaceTeam(ClientId) == PlayerTeam)
					TeamSize++;
			}

			str_format(aBuf, sizeof(aBuf), "你当前在 %d 队，队伍里有 %d 人", PlayerTeam, TeamSize);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", aBuf);
		}
	}
}

void CGameContext::ConJoin(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	const char *pName = pResult->GetString(0);
	int Target = pSelf->FindClientIdByName(pName).value_or(-1);
	if(Target == -1)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "未找到该玩家");
		return;
	}

	int Team = pSelf->GetDDRaceTeam(Target);
	if(pSelf->ProcessSpamProtection(pResult->m_ClientId, false))
		return;

	pSelf->AttemptJoinTeam(pResult->m_ClientId, Team);
}

void CGameContext::ConConverse(IConsole::IResult *pResult, void *pUserData)
{
	// This will never be called
}

void CGameContext::ConWhisper(IConsole::IResult *pResult, void *pUserData)
{
	// This will never be called
}

void CGameContext::ConSetEyeEmote(IConsole::IResult *pResult,
	void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;
	if(pResult->NumArguments() == 0)
	{
		pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			(pPlayer->m_EyeEmoteEnabled) ?
				"你现在可以使用预设眼睛表情了。" :
				"你还没有绑定任何眼睛表情，记得先绑定。");
		return;
	}
	else if(str_comp_nocase(pResult->GetString(0), "on") == 0)
		pPlayer->m_EyeEmoteEnabled = true;
	else if(str_comp_nocase(pResult->GetString(0), "off") == 0)
		pPlayer->m_EyeEmoteEnabled = false;
	else if(str_comp_nocase(pResult->GetString(0), "toggle") == 0)
		pPlayer->m_EyeEmoteEnabled = !pPlayer->m_EyeEmoteEnabled;
	pSelf->Console()->Print(
		IConsole::OUTPUT_LEVEL_STANDARD,
		"chatresp",
		(pPlayer->m_EyeEmoteEnabled) ?
			"你现在可以使用预设眼睛表情了。" :
			"你还没有绑定任何眼睛表情，记得先绑定。");
}

void CGameContext::ConEyeEmote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(g_Config.m_SvEmotionalTees == -1)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"表情功能已禁用。");
		return;
	}

	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	if(pResult->NumArguments() == 0)
	{
		pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"可用表情命令：/emote surprise /emote blink /emote close /emote angry /emote happy /emote pain /emote normal");
		pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"示例：/emote surprise 10 表示持续 10 秒，或直接 /emote surprise（默认 1 秒）");
	}
	else
	{
		if(!pPlayer->CanOverrideDefaultEmote())
			return;

		int EmoteType = 0;
		if(!str_comp_nocase(pResult->GetString(0), "angry"))
			EmoteType = EMOTE_ANGRY;
		else if(!str_comp_nocase(pResult->GetString(0), "blink"))
			EmoteType = EMOTE_BLINK;
		else if(!str_comp_nocase(pResult->GetString(0), "close"))
			EmoteType = EMOTE_BLINK;
		else if(!str_comp_nocase(pResult->GetString(0), "happy"))
			EmoteType = EMOTE_HAPPY;
		else if(!str_comp_nocase(pResult->GetString(0), "pain"))
			EmoteType = EMOTE_PAIN;
		else if(!str_comp_nocase(pResult->GetString(0), "surprise"))
			EmoteType = EMOTE_SURPRISE;
		else if(!str_comp_nocase(pResult->GetString(0), "normal"))
			EmoteType = EMOTE_NORMAL;
		else
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD,
				"chatresp", "未知表情。输入 /emote 查看帮助");
			return;
		}

		int Duration = 1;
		if(pResult->NumArguments() > 1)
			Duration = std::clamp(pResult->GetInteger(1), 1, 86400);

		pPlayer->OverrideDefaultEmote(EmoteType, pSelf->Server()->Tick() + Duration * pSelf->Server()->TickSpeed());
	}
}

void CGameContext::ConNinjaJetpack(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;
	if(pResult->NumArguments())
		pPlayer->m_NinjaJetpack = pResult->GetInteger(0);
	else
		pPlayer->m_NinjaJetpack = !pPlayer->m_NinjaJetpack;
}

void CGameContext::ConShowOthers(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;
	if(g_Config.m_SvShowOthers)
	{
		if(pResult->NumArguments())
			pPlayer->m_ShowOthers = pResult->GetInteger(0);
		else
			pPlayer->m_ShowOthers = !pPlayer->m_ShowOthers;
	}
	else
		pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"本服务器已禁用显示其他队伍玩家");
}

void CGameContext::ConShowAll(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	if(pResult->NumArguments())
	{
		if(pPlayer->m_ShowAll == (bool)pResult->GetInteger(0))
			return;

		pPlayer->m_ShowAll = pResult->GetInteger(0);
	}
	else
	{
		pPlayer->m_ShowAll = !pPlayer->m_ShowAll;
	}

	if(pPlayer->m_ShowAll)
		pSelf->SendChatTarget(pResult->m_ClientId, "你现在可以看到本服所有 tee，不受距离限制");
	else
		pSelf->SendChatTarget(pResult->m_ClientId, "你将不再看到本服所有 tee");
}

void CGameContext::ConSpecTeam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	if(pResult->NumArguments())
		pPlayer->m_SpecTeam = pResult->GetInteger(0);
	else
		pPlayer->m_SpecTeam = !pPlayer->m_SpecTeam;
}

void CGameContext::ConSayTime(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId;
	char aBufName[MAX_NAME_LENGTH];

	if(pResult->NumArguments() > 0)
	{
		ClientId = pSelf->FindClientIdByName(pResult->GetString(0)).value_or(-1);
		if(ClientId == -1)
			return;

		str_format(aBufName, sizeof(aBufName), "%s 的", pSelf->Server()->ClientName(ClientId));
	}
	else
	{
		str_copy(aBufName, "你的", sizeof(aBufName));
		ClientId = pResult->m_ClientId;
	}

	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr)
		return;
	if(pChr->m_DDRaceState != ERaceState::STARTED)
		return;

	char aBufTime[32];
	char aBuf[64];
	int64_t Time = (int64_t)100 * (float)(pSelf->Server()->Tick() - pChr->m_StartTime) / ((float)pSelf->Server()->TickSpeed());
	str_time(Time, TIME_HOURS, aBufTime, sizeof(aBufTime));
	str_format(aBuf, sizeof(aBuf), "%s当前用时是 %s", aBufName, aBufTime);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", aBuf);
}

void CGameContext::ConSayTimeAll(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr)
		return;
	if(pChr->m_DDRaceState != ERaceState::STARTED)
		return;

	char aBufTime[32];
	char aBuf[64];
	int64_t Time = (int64_t)100 * (float)(pSelf->Server()->Tick() - pChr->m_StartTime) / ((float)pSelf->Server()->TickSpeed());
	const char *pName = pSelf->Server()->ClientName(pResult->m_ClientId);
	str_time(Time, TIME_HOURS, aBufTime, sizeof(aBufTime));
	str_format(aBuf, sizeof(aBuf), "%s 当前用时是 %s", pName, aBufTime);
	pSelf->SendChat(-1, TEAM_ALL, aBuf, pResult->m_ClientId);
}

void CGameContext::ConTime(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr)
		return;

	char aBufTime[32];
	char aBuf[64];
	int64_t Time = (int64_t)100 * (float)(pSelf->Server()->Tick() - pChr->m_StartTime) / ((float)pSelf->Server()->TickSpeed());
	str_time(Time, TIME_HOURS, aBufTime, sizeof(aBufTime));
	str_format(aBuf, sizeof(aBuf), "你的用时是 %s", aBufTime);
	pSelf->SendBroadcast(aBuf, pResult->m_ClientId);
}

static const char s_aaMsg[4][128] = {"游戏/回合计时器。", "广播。", "游戏/回合计时器和广播。", "计时器。"};

void CGameContext::ConSetTimerType(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	char aBuf[128];

	if(pResult->NumArguments() > 0)
	{
		int OldType = pPlayer->m_TimerType;
		bool Result = false;

		if(str_comp_nocase(pResult->GetString(0), "default") == 0)
			Result = pPlayer->SetTimerType(CPlayer::TIMERTYPE_DEFAULT);
		else if(str_comp_nocase(pResult->GetString(0), "gametimer") == 0)
			Result = pPlayer->SetTimerType(CPlayer::TIMERTYPE_GAMETIMER);
		else if(str_comp_nocase(pResult->GetString(0), "broadcast") == 0)
			Result = pPlayer->SetTimerType(CPlayer::TIMERTYPE_BROADCAST);
		else if(str_comp_nocase(pResult->GetString(0), "both") == 0)
			Result = pPlayer->SetTimerType(CPlayer::TIMERTYPE_GAMETIMER_AND_BROADCAST);
		else if(str_comp_nocase(pResult->GetString(0), "none") == 0)
			Result = pPlayer->SetTimerType(CPlayer::TIMERTYPE_NONE);
		else
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "未知参数。可用值：default、gametimer、broadcast、both、none");
			return;
		}

		if(!Result)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "你当前客户端不支持所选计时器类型");
			return;
		}

		if((OldType == CPlayer::TIMERTYPE_BROADCAST || OldType == CPlayer::TIMERTYPE_GAMETIMER_AND_BROADCAST) && (pPlayer->m_TimerType == CPlayer::TIMERTYPE_GAMETIMER || pPlayer->m_TimerType == CPlayer::TIMERTYPE_NONE))
			pSelf->SendBroadcast("", pResult->m_ClientId);
	}

	if(pPlayer->m_TimerType <= CPlayer::TIMERTYPE_SIXUP && pPlayer->m_TimerType >= CPlayer::TIMERTYPE_GAMETIMER)
		str_format(aBuf, sizeof(aBuf), "计时器显示在 %s", s_aaMsg[pPlayer->m_TimerType]);
	else if(pPlayer->m_TimerType == CPlayer::TIMERTYPE_NONE)
		str_copy(aBuf, "计时器不会显示。");

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", aBuf);
}

void CGameContext::ConRescue(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr)
		return;

	CGameTeams &Teams = pSelf->m_pController->Teams();
	int Team = pSelf->GetDDRaceTeam(pResult->m_ClientId);
	if(!g_Config.m_SvRescue && !Teams.IsPractice(Team))
	{
		pSelf->SendChatTarget(pPlayer->GetCid(), "本服务器未开启救援功能，而你所在的队伍也没有开启 /practice。注意：练习模式下无法获得排名。");
		return;
	}

	bool GoRescue = true;

	if(pPlayer->m_RescueMode == RESCUEMODE_MANUAL)
	{
		// if character can't set their rescue state then we should rescue them instead
		GoRescue = !pChr->TrySetRescue(RESCUEMODE_MANUAL);
	}

	if(GoRescue)
	{
		if(pChr->Rescue())
			pChr->Unfreeze();
	}
}

void CGameContext::ConRescueMode(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	CGameTeams &Teams = pSelf->m_pController->Teams();
	int Team = pSelf->GetDDRaceTeam(pResult->m_ClientId);
	if(!g_Config.m_SvRescue && !Teams.IsPractice(Team))
	{
		pSelf->SendChatTarget(pPlayer->GetCid(), "本服务器未开启救援功能，而你所在的队伍也没有开启 /practice。注意：练习模式下无法获得排名。");
		return;
	}

	if(str_comp_nocase(pResult->GetString(0), "auto") == 0)
	{
		if(pPlayer->m_RescueMode != RESCUEMODE_AUTO)
		{
			pPlayer->m_RescueMode = RESCUEMODE_AUTO;

			pSelf->SendChatTarget(pPlayer->GetCid(), "救援模式已切换为 auto。");
		}

		return;
	}

	if(str_comp_nocase(pResult->GetString(0), "manual") == 0)
	{
		if(pPlayer->m_RescueMode != RESCUEMODE_MANUAL)
		{
			pPlayer->m_RescueMode = RESCUEMODE_MANUAL;

			pSelf->SendChatTarget(pPlayer->GetCid(), "救援模式已切换为 manual。");
		}

		return;
	}

	if(str_comp_nocase(pResult->GetString(0), "list") == 0)
	{
		pSelf->SendChatTarget(pPlayer->GetCid(), "可用救援模式：auto、manual");
	}
	else if(str_comp_nocase(pResult->GetString(0), "") == 0)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "当前救援模式：%s。", pPlayer->m_RescueMode == RESCUEMODE_MANUAL ? "manual" : "auto");
		pSelf->SendChatTarget(pPlayer->GetCid(), aBuf);
	}
	else
	{
		pSelf->SendChatTarget(pPlayer->GetCid(), "未知救援模式参数");
	}
}

void CGameContext::ConBack(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CGameContext *>(pUserData);
	if(auto *pChr = pSelf->GetPracticeCharacter(pResult))
	{
		auto *pPlayer = pChr->GetPlayer();
		if(!pPlayer->m_LastDeath.has_value())
		{
			pSelf->SendChatTarget(pPlayer->GetCid(), "没有可返回的位置。");
			return;
		}
		pChr->GetLastRescueTeeRef(pPlayer->m_RescueMode) = pPlayer->m_LastDeath.value();
		if(pChr->Rescue())
			pChr->Unfreeze();
	}
}

void CGameContext::ConTeleTo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pCallingPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pCallingPlayer)
		return;
	CCharacter *pCallingCharacter = pCallingPlayer->GetCharacter();
	if(!pCallingCharacter)
		return;

	CGameTeams &Teams = pSelf->m_pController->Teams();
	int Team = pSelf->GetDDRaceTeam(pResult->m_ClientId);
	if(!Teams.IsPractice(Team))
	{
		pSelf->SendChatTarget(pCallingPlayer->GetCid(), "你不在开启了 /practice 的队伍里。注意：开启练习模式后无法获得排名。");
		return;
	}

	vec2 Pos = {};

	if(pResult->NumArguments() == 0)
	{
		// Set calling tee's position to the origin of its spectating viewport
		Pos = pCallingPlayer->m_ViewPos;
	}
	else
	{
		const CPlayer *pDestPlayer = pSelf->FindPlayerByName(pResult->GetString(0));
		if(!pDestPlayer)
		{
			pSelf->SendChatTarget(pCallingPlayer->GetCid(), "未找到这个名字的玩家。");
			return;
		}
		const CCharacter *pDestCharacter = pDestPlayer->GetCharacter();
		if(!pDestCharacter)
			return;

		// Set calling tee's position to that of the destination tee
		Pos = pDestCharacter->m_Pos;
	}

	// Teleport tee
	pSelf->Teleport(pCallingCharacter, Pos);
	pCallingCharacter->ResetJumps();
	pCallingCharacter->Unfreeze();
	pCallingCharacter->ResetVelocity();
	pCallingPlayer->m_LastTeleTee.Save(pCallingCharacter);
}

void CGameContext::ConTeleXY(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pCallingPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pCallingPlayer)
		return;
	CCharacter *pCallingCharacter = pCallingPlayer->GetCharacter();
	if(!pCallingCharacter)
		return;

	CGameTeams &Teams = pSelf->m_pController->Teams();
	int Team = pSelf->GetDDRaceTeam(pResult->m_ClientId);
	if(!Teams.IsPractice(Team))
	{
		pSelf->SendChatTarget(pCallingPlayer->GetCid(), "你不在开启了 /practice 的队伍里。注意：开启练习模式后无法获得排名。");
		return;
	}

	vec2 Pos = {};

	if(pResult->NumArguments() != 2)
	{
		pSelf->SendChatTarget(pCallingPlayer->GetCid(), "无法识别指定参数。用法：/tpxy x y，例如 /tpxy 9 3。");
		return;
	}
	else
	{
		float BaseX = 0.f, BaseY = 0.f;

		CMapItemLayerTilemap *pGameLayer = pSelf->m_Layers.GameLayer();
		constexpr float OuterKillTileBoundaryDistance = 201 * 32.f;
		float MapWidth = (pGameLayer->m_Width * 32) + (OuterKillTileBoundaryDistance * 2.f), MapHeight = (pGameLayer->m_Height * 32) + (OuterKillTileBoundaryDistance * 2.f);

		const auto DetermineCoordinateRelativity = [](const char *pInString, const float AbsoluteDefaultValue, float &OutFloat) -> bool {
			// mode 0 = abs, 1 = sub, 2 = add

			// Relative?
			const char *pStrDelta = str_startswith(pInString, "~");

			float d;
			if(!str_tofloat(pStrDelta ? pStrDelta : pInString, &d))
				return false;

			// Is the number valid?
			if(std::isnan(d) || std::isinf(d))
				return false;

			// Convert our gleaned 'display' coordinate to an actual map coordinate
			d *= 32.f;

			OutFloat = (pStrDelta ? AbsoluteDefaultValue : 0) + d;
			return true;
		};

		if(!DetermineCoordinateRelativity(pResult->GetString(0), pCallingPlayer->m_ViewPos.x, BaseX))
		{
			pSelf->SendChatTarget(pCallingPlayer->GetCid(), "无效的 X 坐标。");
			return;
		}
		if(!DetermineCoordinateRelativity(pResult->GetString(1), pCallingPlayer->m_ViewPos.y, BaseY))
		{
			pSelf->SendChatTarget(pCallingPlayer->GetCid(), "无效的 Y 坐标。");
			return;
		}

		Pos = {std::clamp(BaseX, (-OuterKillTileBoundaryDistance) + 1.f, (-OuterKillTileBoundaryDistance) + MapWidth - 1.f), std::clamp(BaseY, (-OuterKillTileBoundaryDistance) + 1.f, (-OuterKillTileBoundaryDistance) + MapHeight - 1.f)};
	}

	// Teleport tee
	pSelf->Teleport(pCallingCharacter, Pos);
	pCallingCharacter->ResetJumps();
	pCallingCharacter->Unfreeze();
	pCallingCharacter->ResetVelocity();
	pCallingPlayer->m_LastTeleTee.Save(pCallingCharacter);
}

void CGameContext::ConTeleCursor(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr)
		return;

	CGameTeams &Teams = pSelf->m_pController->Teams();
	int Team = pSelf->GetDDRaceTeam(pResult->m_ClientId);
	if(!Teams.IsPractice(Team))
	{
		pSelf->SendChatTarget(pPlayer->GetCid(), "你不在开启了 /practice 的队伍里。注意：开启练习模式后无法获得排名。");
		return;
	}

	// default to view pos when character is not available
	vec2 Pos = pPlayer->m_ViewPos;
	if(pResult->NumArguments() == 0 && !pPlayer->IsPaused() && pPlayer->GetCharacter() && pPlayer->GetCharacter()->IsAlive())
	{
		vec2 Target = vec2(pChr->Core()->m_Input.m_TargetX, pChr->Core()->m_Input.m_TargetY);
		Pos = pPlayer->m_CameraInfo.ConvertTargetToWorld(pPlayer->GetCharacter()->GetPos(), Target);
	}
	else if(pResult->NumArguments() > 0)
	{
		const CPlayer *pPlayerTo = pSelf->FindPlayerByName(pResult->GetString(0));
		if(!pPlayerTo)
		{
			pSelf->SendChatTarget(pPlayer->GetCid(), "未找到这个名字的玩家。");
			return;
		}
		const CCharacter *pChrTo = pPlayerTo->GetCharacter();
		if(!pChrTo)
			return;
		Pos = pChrTo->m_Pos;
	}
	pSelf->Teleport(pChr, Pos);
	pChr->ResetJumps();
	pChr->Unfreeze();
	pChr->ResetVelocity();
	pPlayer->m_LastTeleTee.Save(pChr);
}

void CGameContext::ConLastTele(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr)
		return;

	CGameTeams &Teams = pSelf->m_pController->Teams();
	int Team = pSelf->GetDDRaceTeam(pResult->m_ClientId);
	if(!Teams.IsPractice(Team))
	{
		pSelf->SendChatTarget(pPlayer->GetCid(), "你不在开启了 /practice 的队伍里。注意：开启练习模式后无法获得排名。");
		return;
	}
	if(!pPlayer->m_LastTeleTee.GetPos().x)
	{
		pSelf->SendChatTarget(pPlayer->GetCid(), "你之前没有传送过。请先使用 /tp 再执行这个命令。");
		return;
	}
	pPlayer->m_LastTeleTee.Load(pChr);
	pPlayer->Pause(CPlayer::PAUSE_NONE, true);
}

CCharacter *CGameContext::GetPracticeCharacter(IConsole::IResult *pResult)
{
	if(!CheckClientId(pResult->m_ClientId))
		return nullptr;
	CPlayer *pPlayer = m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return nullptr;
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr)
		return nullptr;

	CGameTeams &Teams = m_pController->Teams();
	int Team = GetDDRaceTeam(pResult->m_ClientId);
	if(!Teams.IsPractice(Team))
	{
		SendChatTarget(pPlayer->GetCid(), "你不在开启了 /practice 的队伍里。注意：开启练习模式后无法获得排名。");
		return nullptr;
	}
	return pChr;
}

void CGameContext::ConPracticeToTeleporter(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CCharacter *pChr = pSelf->GetPracticeCharacter(pResult);
	if(pChr)
	{
		if(pSelf->Collision()->TeleOuts(pResult->GetInteger(0) - 1).empty())
		{
			pSelf->SendChatTarget(pChr->GetPlayer()->GetCid(), "地图上不存在这个编号的传送器。");
			return;
		}

		ConToTeleporter(pResult, pUserData);
		pChr->ResetJumps();
		pChr->Unfreeze();
		pChr->ResetVelocity();
		pChr->GetPlayer()->m_LastTeleTee.Save(pChr);
	}
}

void CGameContext::ConPracticeToCheckTeleporter(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CCharacter *pChr = pSelf->GetPracticeCharacter(pResult);
	if(pChr)
	{
		if(pSelf->Collision()->TeleCheckOuts(pResult->GetInteger(0) - 1).empty())
		{
			pSelf->SendChatTarget(pChr->GetPlayer()->GetCid(), "地图上不存在这个编号的检查点传送器。");
			return;
		}

		ConToCheckTeleporter(pResult, pUserData);
		pChr->ResetJumps();
		pChr->Unfreeze();
		pChr->ResetVelocity();
		pChr->GetPlayer()->m_LastTeleTee.Save(pChr);
	}
}

void CGameContext::ConPracticeUnSolo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr)
		return;

	if(g_Config.m_SvTeam == SV_TEAM_FORBIDDEN || g_Config.m_SvTeam == SV_TEAM_FORCED_SOLO)
	{
		pSelf->SendChatTarget(pPlayer->GetCid(), "该命令在 solo 服务器上不可用");
		return;
	}

	CGameTeams &Teams = pSelf->m_pController->Teams();
	int Team = pSelf->GetDDRaceTeam(pResult->m_ClientId);
	if(!Teams.IsPractice(Team))
	{
		pSelf->SendChatTarget(pPlayer->GetCid(), "你不在开启了 /practice 的队伍里。注意：开启练习模式后无法获得排名。");
		return;
	}
	pChr->SetSolo(false);
}

void CGameContext::ConPracticeSolo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr)
		return;

	if(g_Config.m_SvTeam == SV_TEAM_FORBIDDEN || g_Config.m_SvTeam == SV_TEAM_FORCED_SOLO)
	{
		pSelf->SendChatTarget(pPlayer->GetCid(), "该命令在 solo 服务器上不可用");
		return;
	}

	CGameTeams &Teams = pSelf->m_pController->Teams();
	int Team = pSelf->GetDDRaceTeam(pResult->m_ClientId);
	if(!Teams.IsPractice(Team))
	{
		pSelf->SendChatTarget(pPlayer->GetCid(), "你不在开启了 /practice 的队伍里。注意：开启练习模式后无法获得排名。");
		return;
	}
	pChr->SetSolo(true);
}

void CGameContext::ConPracticeUnDeep(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	auto *pChr = pSelf->GetPracticeCharacter(pResult);
	if(!pChr)
		return;

	pChr->SetDeepFrozen(false);
	pChr->Unfreeze();
}

void CGameContext::ConPracticeDeep(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	auto *pChr = pSelf->GetPracticeCharacter(pResult);
	if(!pChr)
		return;

	pChr->SetDeepFrozen(true);
}

void CGameContext::ConPracticeUnLiveFreeze(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	auto *pChr = pSelf->GetPracticeCharacter(pResult);
	if(!pChr)
		return;

	pChr->SetLiveFrozen(false);
}

void CGameContext::ConPracticeLiveFreeze(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	auto *pChr = pSelf->GetPracticeCharacter(pResult);
	if(!pChr)
		return;

	pChr->SetLiveFrozen(true);
}

void CGameContext::ConPracticeShotgun(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->GetPracticeCharacter(pResult))
		ConShotgun(pResult, pUserData);
}

void CGameContext::ConPracticeGrenade(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->GetPracticeCharacter(pResult))
		ConGrenade(pResult, pUserData);
}

void CGameContext::ConPracticeLaser(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->GetPracticeCharacter(pResult))
		ConLaser(pResult, pUserData);
}

void CGameContext::ConPracticeJetpack(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->GetPracticeCharacter(pResult))
		ConJetpack(pResult, pUserData);
}

void CGameContext::ConPracticeEndlessJump(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->GetPracticeCharacter(pResult))
		ConEndlessJump(pResult, pUserData);
}

void CGameContext::ConPracticeSetJumps(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->GetPracticeCharacter(pResult))
		ConSetJumps(pResult, pUserData);
}

void CGameContext::ConPracticeWeapons(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->GetPracticeCharacter(pResult))
		ConWeapons(pResult, pUserData);
}

void CGameContext::ConPracticeUnShotgun(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->GetPracticeCharacter(pResult))
		ConUnShotgun(pResult, pUserData);
}

void CGameContext::ConPracticeUnGrenade(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->GetPracticeCharacter(pResult))
		ConUnGrenade(pResult, pUserData);
}

void CGameContext::ConPracticeUnLaser(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->GetPracticeCharacter(pResult))
		ConUnLaser(pResult, pUserData);
}

void CGameContext::ConPracticeUnJetpack(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->GetPracticeCharacter(pResult))
		ConUnJetpack(pResult, pUserData);
}

void CGameContext::ConPracticeUnEndlessJump(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->GetPracticeCharacter(pResult))
		ConUnEndlessJump(pResult, pUserData);
}

void CGameContext::ConPracticeUnWeapons(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->GetPracticeCharacter(pResult))
		ConUnWeapons(pResult, pUserData);
}

void CGameContext::ConPracticeNinja(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->GetPracticeCharacter(pResult))
		ConNinja(pResult, pUserData);
}

void CGameContext::ConPracticeUnNinja(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->GetPracticeCharacter(pResult))
		ConUnNinja(pResult, pUserData);
}

void CGameContext::ConPracticeEndlessHook(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->GetPracticeCharacter(pResult))
		ConEndlessHook(pResult, pUserData);
}

void CGameContext::ConPracticeUnEndlessHook(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->GetPracticeCharacter(pResult))
		ConUnEndlessHook(pResult, pUserData);
}

void CGameContext::ConPracticeSetSwitch(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->GetPracticeCharacter(pResult))
		ConSetSwitch(pResult, pUserData);
}

void CGameContext::ConPracticeToggleInvincible(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->GetPracticeCharacter(pResult))
		ConToggleInvincible(pResult, pUserData);
}

void CGameContext::ConPracticeToggleCollision(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	auto *pChr = pSelf->GetPracticeCharacter(pResult);
	if(!pChr)
		return;

	pChr->SetCollisionDisabled(!pChr->Core()->m_CollisionDisabled);
}

void CGameContext::ConPracticeToggleHookCollision(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	auto *pChr = pSelf->GetPracticeCharacter(pResult);
	if(!pChr)
		return;

	pChr->SetHookHitDisabled(!pChr->Core()->m_HookHitDisabled);
}

void CGameContext::ConPracticeToggleHitOthers(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	auto *pChr = pSelf->GetPracticeCharacter(pResult);
	if(!pChr)
		return;

	if(pResult->NumArguments() == 0 || str_comp(pResult->GetString(0), "all") == 0)
	{
		bool IsEnabled = (pChr->HammerHitDisabled() && pChr->ShotgunHitDisabled() &&
				  pChr->GrenadeHitDisabled() && pChr->LaserHitDisabled());
		pChr->SetHammerHitDisabled(!IsEnabled);
		pChr->SetShotgunHitDisabled(!IsEnabled);
		pChr->SetGrenadeHitDisabled(!IsEnabled);
		pChr->SetLaserHitDisabled(!IsEnabled);
		return;
	}

	if(str_comp(pResult->GetString(0), "hammer") == 0)
		pChr->SetHammerHitDisabled(!pChr->HammerHitDisabled());
	else if(str_comp(pResult->GetString(0), "shotgun") == 0)
		pChr->SetShotgunHitDisabled(!pChr->ShotgunHitDisabled());
	else if(str_comp(pResult->GetString(0), "grenade") == 0)
		pChr->SetGrenadeHitDisabled(!pChr->GrenadeHitDisabled());
	else if(str_comp(pResult->GetString(0), "laser") == 0)
		pChr->SetLaserHitDisabled(!pChr->LaserHitDisabled());
}

void CGameContext::ConPracticeAddWeapon(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->GetPracticeCharacter(pResult))
		ConAddWeapon(pResult, pUserData);
}

void CGameContext::ConPracticeRemoveWeapon(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->GetPracticeCharacter(pResult))
		ConRemoveWeapon(pResult, pUserData);
}

void CGameContext::ConProtectedKill(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr)
		return;

	int CurrTime = (pSelf->Server()->Tick() - pChr->m_StartTime) / pSelf->Server()->TickSpeed();
	if(g_Config.m_SvKillProtection != 0 && CurrTime >= (60 * g_Config.m_SvKillProtection) && pChr->m_DDRaceState == ERaceState::STARTED)
	{
		pPlayer->KillCharacter(WEAPON_SELF);
		pPlayer->Respawn();
	}
}

void CGameContext::ConPoints(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	if(pResult->NumArguments() > 0)
	{
		if(!g_Config.m_SvHideScore)
			pSelf->Score()->ShowPoints(pResult->m_ClientId, pResult->GetString(0));
		else
			pSelf->Console()->Print(
				IConsole::OUTPUT_LEVEL_STANDARD,
				"chatresp",
				"本服务器不允许查看其他玩家的全局积分。");
	}
	else
		pSelf->Score()->ShowPoints(pResult->m_ClientId,
			pSelf->Server()->ClientName(pResult->m_ClientId));
}

void CGameContext::ConTopPoints(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	if(g_Config.m_SvHideScore)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"本服务器不允许查看全局积分排行榜");
		return;
	}

	if(pResult->NumArguments() > 0)
		pSelf->Score()->ShowTopPoints(pResult->m_ClientId, pResult->GetInteger(0));
	else
		pSelf->Score()->ShowTopPoints(pResult->m_ClientId);
}

void CGameContext::ConTimeCP(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	if(g_Config.m_SvHideScore)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"本服务器不允许查看 checkpoint 时间");
		return;
	}

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	const char *pName = pResult->GetString(0);
	pSelf->Score()->LoadPlayerTimeCp(pResult->m_ClientId, pName);
}
