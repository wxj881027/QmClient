/* (c) Redix and Sushi */

#include "race_demo.h"

#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/media_paths.h>
#include <game/client/gameclient.h>
#include <game/client/race.h>
#include <game/localization.h>

#include <cctype>
#include <chrono>

using namespace std::chrono_literals;

const char *CRaceDemo::ms_pRaceDemoDir = "demos/auto/race";

// NOLINTNEXTLINE(misc-use-internal-linkage)
struct CDemoItem
{
	char m_aName[128];
	int m_Time;
};

// NOLINTNEXTLINE(misc-use-internal-linkage)
struct CDemoListParam
{
	const CRaceDemo *m_pThis;
	std::vector<CDemoItem> *m_pvDemos;
	const char *m_pMap;
};

CRaceDemo::CRaceDemo() :
	m_RaceState(RACE_NONE), m_RaceStartTick(-1), m_RecordStopTick(-1), m_Time(0)
{
	m_aTmpFilename[0] = '\0';
}

void CRaceDemo::GetPath(char *pBuf, int Size, int Time) const
{
	const char *pMap = Client()->GetCurrentMap();
	char aDirectory[IO_MAX_PATH_LENGTH];
	if(Time < 0)
	{
		const std::string Directory = qmclient::media_paths::Resolve(Storage(), g_Config, ms_pRaceDemoDir);
		str_copy(aDirectory, Directory.c_str());
	}
	else
	{
		// 完成当前录像时沿用开始录制的目录，不跟随中途修改的设置。
		str_copy(aDirectory, m_aTmpFilename);
		fs_parent_dir(aDirectory);
	}

	char aPlayerName[MAX_NAME_LENGTH];
	str_copy(aPlayerName, Client()->PlayerName());
	str_sanitize_filename(aPlayerName);

	char aName[IO_MAX_PATH_LENGTH];
	if(Time < 0)
		str_format(aName, sizeof(aName), "%s_tmp_%d.demo", pMap, pid());
	else if(g_Config.m_ClDemoName)
		str_format(aName, sizeof(aName), "%s_%d.%03d_%s.demo", pMap, Time / 1000, Time % 1000, aPlayerName);
	else
		str_format(aName, sizeof(aName), "%s_%d.%03d.demo", pMap, Time / 1000, Time % 1000);
	const std::string Path = qmclient::media_paths::Join(aDirectory, aName);
	str_copy(pBuf, Path.size() < static_cast<size_t>(Size) ? Path.c_str() : "", Size);
}

void CRaceDemo::OnStateChange(int NewState, int OldState)
{
	if(OldState == IClient::STATE_ONLINE)
		StopRecord();
}

void CRaceDemo::OnNewSnapshot()
{
	if(!GameClient()->m_GameInfo.m_Race || !g_Config.m_ClAutoRaceRecord || Client()->State() != IClient::STATE_ONLINE)
		return;

	if(!GameClient()->m_Snap.m_pGameInfoObj || GameClient()->m_Snap.m_SpecInfo.m_Active || !GameClient()->m_Snap.m_pLocalCharacter || !GameClient()->m_Snap.m_pLocalPrevCharacter)
		return;

	static int s_LastRaceTick = -1;

	bool RaceFlag = GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME;
	bool ServerControl = RaceFlag && g_Config.m_ClRaceRecordServerControl;
	int RaceTick = -GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer;

	// start the demo
	bool ForceStart = ServerControl && s_LastRaceTick != RaceTick && Client()->GameTick(g_Config.m_ClDummy) - RaceTick < Client()->GameTickSpeed();
	bool AllowRestart = (m_AllowRestart || ForceStart) && m_RaceStartTick + 10 * Client()->GameTickSpeed() < Client()->GameTick(g_Config.m_ClDummy);
	if(m_RaceState == RACE_IDLE || m_RaceState == RACE_PREPARE || (m_RaceState == RACE_STARTED && AllowRestart))
	{
		vec2 PrevPos = vec2(GameClient()->m_Snap.m_pLocalPrevCharacter->m_X, GameClient()->m_Snap.m_pLocalPrevCharacter->m_Y);
		vec2 Pos = vec2(GameClient()->m_Snap.m_pLocalCharacter->m_X, GameClient()->m_Snap.m_pLocalCharacter->m_Y);

		if(ForceStart || (!ServerControl && GameClient()->RaceHelper()->IsStart(PrevPos, Pos)))
		{
			if(m_RaceState == RACE_STARTED)
			{
				Client()->RaceRecord_Stop();
				Storage()->RemoveFile(m_aTmpFilename, qmclient::media_paths::StorageType(m_aTmpFilename));
			}
			if(m_RaceState != RACE_PREPARE) // start recording again
			{
				GetPath(m_aTmpFilename, sizeof(m_aTmpFilename));
				if(!qmclient::media_paths::PrepareWrite(Storage(), m_aTmpFilename))
				{
					GameClient()->Echo(Localize("Failed to save demo"));
					m_aTmpFilename[0] = '\0';
					m_RaceState = RACE_IDLE;
					return;
				}
				Client()->RaceRecord_Start(m_aTmpFilename);
				if(!Client()->RaceRecord_IsRecording())
				{
					m_aTmpFilename[0] = '\0';
					m_RaceState = RACE_IDLE;
					return;
				}
			}
			m_RaceStartTick = Client()->GameTick(g_Config.m_ClDummy);
			m_RaceState = RACE_STARTED;
		}
	}

	// start recording before the player passes the start line, so we can see some preparation steps
	if(m_RaceState == RACE_NONE)
	{
		GetPath(m_aTmpFilename, sizeof(m_aTmpFilename));
		if(!qmclient::media_paths::PrepareWrite(Storage(), m_aTmpFilename))
		{
			GameClient()->Echo(Localize("Failed to save demo"));
			m_aTmpFilename[0] = '\0';
			m_RaceState = RACE_IDLE;
			return;
		}
		Client()->RaceRecord_Start(m_aTmpFilename);
		if(!Client()->RaceRecord_IsRecording())
		{
			m_aTmpFilename[0] = '\0';
			m_RaceState = RACE_IDLE;
			return;
		}
		m_RaceStartTick = Client()->GameTick(g_Config.m_ClDummy);
		m_RaceState = RACE_PREPARE;
	}

	// stop recording if the player did not pass the start line after 20 seconds
	if(m_RaceState == RACE_PREPARE && Client()->GameTick(g_Config.m_ClDummy) - m_RaceStartTick >= Client()->GameTickSpeed() * 20)
	{
		StopRecord();
		m_RaceState = RACE_IDLE;
	}

	// stop the demo
	if(m_RaceState == RACE_FINISHED && m_RecordStopTick <= Client()->GameTick(g_Config.m_ClDummy))
		StopRecord(m_Time);

	s_LastRaceTick = RaceFlag ? RaceTick : -1;
}

void CRaceDemo::OnReset()
{
	StopRecord();
}

void CRaceDemo::OnShutdown()
{
	StopRecord();
}

void CRaceDemo::OnMessage(int MsgType, void *pRawMsg)
{
	// check for messages from server
	if(MsgType == NETMSGTYPE_SV_KILLMSG)
	{
		CNetMsg_Sv_KillMsg *pMsg = (CNetMsg_Sv_KillMsg *)pRawMsg;
		if(pMsg->m_Victim == GameClient()->m_Snap.m_LocalClientId && Client()->RaceRecord_IsRecording())
			StopRecord(m_Time);
	}
	else if(MsgType == NETMSGTYPE_SV_KILLMSGTEAM)
	{
		CNetMsg_Sv_KillMsgTeam *pMsg = (CNetMsg_Sv_KillMsgTeam *)pRawMsg;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameClient()->m_Teams.Team(i) == pMsg->m_Team && i == GameClient()->m_Snap.m_LocalClientId && Client()->RaceRecord_IsRecording())
				StopRecord(m_Time);
		}
	}
	else if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;
		if(pMsg->m_ClientId == -1 && m_RaceState == RACE_STARTED)
		{
			char aName[MAX_NAME_LENGTH];
			int Time = CRaceHelper::TimeFromFinishMessage(pMsg->m_pMessage, aName, sizeof(aName));
			if(Time > 0 && GameClient()->m_Snap.m_LocalClientId >= 0 && str_comp(aName, GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_aName) == 0)
			{
				m_RaceState = RACE_FINISHED;
				m_RecordStopTick = Client()->GameTick(g_Config.m_ClDummy) + Client()->GameTickSpeed();
				m_Time = Time;
			}
		}
	}
	else if(MsgType == NETMSGTYPE_SV_RACEFINISH)
	{
		CNetMsg_Sv_RaceFinish *pMsg = (CNetMsg_Sv_RaceFinish *)pRawMsg;
		if(m_RaceState == RACE_STARTED && pMsg->m_ClientId == GameClient()->m_Snap.m_LocalClientId)
		{
			m_RaceState = RACE_FINISHED;
			m_RecordStopTick = Client()->GameTick(g_Config.m_ClDummy) + Client()->GameTickSpeed();
			m_Time = pMsg->m_Time;
		}
	}
}

void CRaceDemo::OnMapLoad()
{
	m_AllowRestart = false;
}

void CRaceDemo::StopRecord(int Time)
{
	GameClient()->StopRaceRecordIfRecording();

	if(m_aTmpFilename[0] != '\0')
	{
		char aNewFilename[IO_MAX_PATH_LENGTH];
		GetPath(aNewFilename, sizeof(aNewFilename), Time);
		if(Time > 0 && aNewFilename[0] == '\0')
			GameClient()->Echo(Localize("Failed to save demo"));
		else if(Time > 0 && CheckDemo(Time))
		{
			if(!Storage()->RenameFile(m_aTmpFilename, aNewFilename, qmclient::media_paths::StorageType(m_aTmpFilename)))
				GameClient()->Echo(Localize("Failed to save demo"));
		}
		else
		{ // no new record
			Storage()->RemoveFile(m_aTmpFilename, qmclient::media_paths::StorageType(m_aTmpFilename));
		}

		m_aTmpFilename[0] = '\0';
	}

	m_Time = 0;
	m_RaceState = RACE_NONE;
	m_RaceStartTick = -1;
	m_RecordStopTick = -1;
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
struct SRaceDemoFetchUser
{
	CRaceDemo *m_pThis;
	CDemoListParam *m_pParam;
};

int CRaceDemo::RaceDemolistFetchCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser)
{
	auto *pRealUser = (SRaceDemoFetchUser *)pUser;
	auto *pParam = pRealUser->m_pParam;
	int MapLen = str_length(pParam->m_pMap);
	if(IsDir || !str_endswith(pInfo->m_pName, ".demo") || !str_startswith(pInfo->m_pName, pParam->m_pMap) || pInfo->m_pName[MapLen] != '_')
		return 0;

	CDemoItem Item;
	str_truncate(Item.m_aName, sizeof(Item.m_aName), pInfo->m_pName, str_length(pInfo->m_pName) - 5);

	const char *pTime = Item.m_aName + MapLen + 1;
	const char *pTEnd = pTime;
	while(isdigit(*pTEnd) || *pTEnd == ' ' || *pTEnd == '.' || *pTEnd == ',')
		pTEnd++;

	if(g_Config.m_ClDemoName)
	{
		char aPlayerName[MAX_NAME_LENGTH];
		str_copy(aPlayerName, pParam->m_pThis->Client()->PlayerName());
		str_sanitize_filename(aPlayerName);

		if(pTEnd[0] != '_' || str_comp(pTEnd + 1, aPlayerName) != 0)
			return 0;
	}
	else if(pTEnd[0])
	{
		return 0;
	}

	Item.m_Time = CRaceHelper::TimeFromSecondsStr(pTime);
	if(Item.m_Time > 0)
		pParam->m_pvDemos->push_back(Item);

	if(time_get_nanoseconds() - pRealUser->m_pThis->m_RaceDemosLoadStartTime > 500ms)
	{
		pRealUser->m_pThis->GameClient()->m_Menus.RenderLoading(Localize("Loading race demo files"), "", 0);
	}

	return 0;
}

bool CRaceDemo::CheckDemo(int Time)
{
	std::vector<CDemoItem> vDemos;
	CDemoListParam Param = {this, &vDemos, Client()->GetCurrentMap()};
	m_RaceDemosLoadStartTime = time_get_nanoseconds();
	SRaceDemoFetchUser User;
	User.m_pParam = &Param;
	User.m_pThis = this;
	char aDirectory[IO_MAX_PATH_LENGTH];
	str_copy(aDirectory, m_aTmpFilename);
	fs_parent_dir(aDirectory);
	Storage()->ListDirectoryInfo(qmclient::media_paths::StorageType(aDirectory), aDirectory, RaceDemolistFetchCallback, &User);

	// loop through demo files
	for(auto &Demo : vDemos)
	{
		if(Time >= Demo.m_Time) // found a better demo
			return false;

		// delete old demo
		const std::string Path = qmclient::media_paths::Join(aDirectory, std::string(Demo.m_aName) + ".demo");
		if(!Path.empty())
			Storage()->RemoveFile(Path.c_str(), qmclient::media_paths::StorageType(aDirectory));
	}

	return true;
}
