/* (c) Redix and Sushi */

#include "race_demo.h"

#include <base/log.h>
#include <base/system.h>

#include <engine/demo.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/media_paths.h>
#include <game/client/components/qmclient/race_timing.h>
#include <game/client/gameclient.h>
#include <game/client/race.h>
#include <game/localization.h>

#include <chrono>

using namespace std::chrono_literals;

const char *CRaceDemo::ms_pRaceDemoDir = "demos/auto/race";

CRaceDemo::CRaceDemo() :
	m_RaceState(RACE_NONE), m_RaceStartTick(-1), m_RecordStopTick(-1), m_Time(0)
{
	m_aTmpFilename[0] = '\0';
}

void CRaceDemo::GetPath(char *pBuf, int Size) const
{
	const char *pMap = Client()->GetCurrentMap();
	const std::string Directory = qmclient::media_paths::Resolve(Storage(), g_Config, ms_pRaceDemoDir);
	char aName[IO_MAX_PATH_LENGTH];
	str_format(aName, sizeof(aName), "%s_tmp_%d.demo", pMap, pid());
	const std::string Path = qmclient::media_paths::Join(Directory, aName);
	str_copy(pBuf, Path.size() < static_cast<size_t>(Size) ? Path.c_str() : "", Size);
}

bool CRaceDemo::StartRecord() noexcept
{
	try
	{
		char aFilename[IO_MAX_PATH_LENGTH];
		GetPath(aFilename, sizeof(aFilename));
		qm_race_demo::CStorageOperations Operations(*Storage());
		if(aFilename[0] == '\0' ||
			!Operations.DestinationAvailable(aFilename, qmclient::media_paths::StorageType(aFilename)) ||
			!qmclient::media_paths::PrepareWrite(Storage(), aFilename))
		{
			ReportSaveFailure(false);
			return false;
		}
		str_copy(m_aTmpFilename, aFilename);
		str_copy(m_aCaptureMap, Client()->GetCurrentMap());
		m_pRecorder = Client()->DemoRecorder(RECORDER_RACE);
		Client()->RaceRecord_Start(m_aTmpFilename);
		if(!m_pRecorder->IsRecording())
		{
			StopRecord(0);
			return false;
		}
		return true;
	}
	catch(...)
	{
		StopRecord(0);
		ReportSaveFailure(false);
		return false;
	}
}

void CRaceDemo::OnStateChange(int NewState, int OldState)
{
	if(OldState == IClient::STATE_ONLINE)
		StopRecord();
}

void CRaceDemo::OnNewSnapshot()
{
	if(!m_Finalizer.CanStartCapture())
		return;
	if(!GameClient()->m_GameInfo.m_Race || !g_Config.m_ClAutoRaceRecord || Client()->State() != IClient::STATE_ONLINE)
		return;

	if(!GameClient()->m_Snap.m_pGameInfoObj || GameClient()->m_Snap.m_SpecInfo.m_Active || !GameClient()->m_Snap.m_pLocalCharacter || !GameClient()->m_Snap.m_pLocalPrevCharacter)
		return;

	static int s_LastRaceTick = -1;

	bool RaceFlag = GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME;
	bool ServerControl = RaceFlag && g_Config.m_ClRaceRecordServerControl;
	const auto RaceTick = qm_race_timing::DecodeStartTick(GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer);

	// start the demo
	bool ForceStart = ServerControl && qm_race_timing::IsNewServerStart(s_LastRaceTick, RaceTick, Client()->GameTick(g_Config.m_ClDummy), Client()->GameTickSpeed());
	bool AllowRestart = (m_AllowRestart || ForceStart) && qm_race_timing::CanRestartRecording(Client()->GameTick(g_Config.m_ClDummy), m_RaceStartTick, Client()->GameTickSpeed());
	if(m_RaceState == RACE_IDLE || m_RaceState == RACE_PREPARE || (m_RaceState == RACE_STARTED && AllowRestart))
	{
		vec2 PrevPos = vec2(GameClient()->m_Snap.m_pLocalPrevCharacter->m_X, GameClient()->m_Snap.m_pLocalPrevCharacter->m_Y);
		vec2 Pos = vec2(GameClient()->m_Snap.m_pLocalCharacter->m_X, GameClient()->m_Snap.m_pLocalCharacter->m_Y);

		if(ForceStart || (!ServerControl && GameClient()->RaceHelper()->IsStart(PrevPos, Pos)))
		{
			if(m_RaceState == RACE_STARTED)
			{
				StopRecord(0);
				if(!m_Finalizer.CanStartCapture())
					return;
			}
			if(m_RaceState != RACE_PREPARE) // start recording again
			{
				if(!StartRecord())
				{
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
		if(!StartRecord())
		{
			m_RaceState = RACE_IDLE;
			return;
		}
		m_RaceStartTick = Client()->GameTick(g_Config.m_ClDummy);
		m_RaceState = RACE_PREPARE;
	}

	// stop recording if the player did not pass the start line after 20 seconds
	if(m_RaceState == RACE_PREPARE && qm_race_timing::PreparationExpired(Client()->GameTick(g_Config.m_ClDummy), m_RaceStartTick, Client()->GameTickSpeed()))
	{
		StopRecord();
		m_RaceState = RACE_IDLE;
	}

	// stop the demo
	if(m_RaceState == RACE_FINISHED && m_RecordStopTick <= Client()->GameTick(g_Config.m_ClDummy))
		StopRecord(m_Time);

	s_LastRaceTick = qm_race_timing::UpdateLastTick(s_LastRaceTick, RaceFlag, GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer);
}

void CRaceDemo::OnReset()
{
	StopRecord();
}

void CRaceDemo::OnShutdown()
{
	StopRecord(-1, true);
}

void CRaceDemo::OnUpdate()
{
	FinishPending();
}

void CRaceDemo::OnMessage(int MsgType, void *pRawMsg)
{
	// check for messages from server
	if(MsgType == NETMSGTYPE_SV_KILLMSG)
	{
		CNetMsg_Sv_KillMsg *pMsg = (CNetMsg_Sv_KillMsg *)pRawMsg;
		if(pMsg->m_Victim == GameClient()->m_Snap.m_LocalClientId && m_pRecorder && m_pRecorder->IsRecording())
			StopRecord(m_Time);
	}
	else if(MsgType == NETMSGTYPE_SV_KILLMSGTEAM)
	{
		CNetMsg_Sv_KillMsgTeam *pMsg = (CNetMsg_Sv_KillMsgTeam *)pRawMsg;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameClient()->m_Teams.Team(i) == pMsg->m_Team && i == GameClient()->m_Snap.m_LocalClientId && m_pRecorder && m_pRecorder->IsRecording())
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
				m_RecordStopTick = qm_race_timing::RecordingStopTick(Client()->GameTick(g_Config.m_ClDummy), Client()->GameTickSpeed());
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
			m_RecordStopTick = qm_race_timing::RecordingStopTick(Client()->GameTick(g_Config.m_ClDummy), Client()->GameTickSpeed());
			m_Time = pMsg->m_Time;
		}
	}
}

void CRaceDemo::OnMapLoad()
{
	m_AllowRestart = false;
}

void CRaceDemo::ResetRaceState() noexcept
{
	m_Time = 0;
	m_RaceState = RACE_NONE;
	m_RaceStartTick = -1;
	m_RecordStopTick = -1;
}

void CRaceDemo::ReportSaveFailure(bool AfterCommit) noexcept
{
	try
	{
		if(AfterCommit)
			log_warn("race_demo", "Could not remove an obsolete race demo");
		else
			GameClient()->Echo(Localize("Failed to save demo"));
	}
	catch(...)
	{
	}
}

void CRaceDemo::FinishPending(bool Force) noexcept
{
	if(!m_Finalizer.Pending())
		return;
	const auto StartedAt = time_get_nanoseconds();
	qm_race_demo::CStorageOperations Operations(*Storage());
	const auto Stop = [this]() {
		if(!m_pRecorder)
			return qm_race_demo::CFinalizer::CStopResult{true, false};
		if(m_pRecorder->IsRecording())
			m_pRecorder->Stop(IDemoRecorder::EStopMode::KEEP_FILE);
		return qm_race_demo::CFinalizer::CStopResult{!m_pRecorder->IsRecording(), !m_pRecorder->HasWriteError()};
	};
	const auto Progress = [this, StartedAt]() {
		if(time_get_nanoseconds() - StartedAt > 500ms)
			GameClient()->m_Menus.RenderLoading(Localize("Loading race demo files"), "", 0);
	};
	const auto Notify = [this](bool AfterCommit) { ReportSaveFailure(AfterCommit); };
	m_Finalizer.Try(StartedAt, Operations, Stop, Progress, Notify, Force);
	if(!m_Finalizer.Pending())
		m_pRecorder = nullptr;
}

void CRaceDemo::StopRecord(int Time, bool Force) noexcept
{
	try
	{
		if(m_aTmpFilename[0] != '\0')
		{
			char aPlayerName[MAX_NAME_LENGTH] = "";
			const char *pMap = "";
			if(Time > 0)
			{
				pMap = m_aCaptureMap;
				str_copy(aPlayerName, Client()->PlayerName());
				str_sanitize_filename(aPlayerName);
			}
			if(!m_Finalizer.Queue(m_aTmpFilename, pMap, aPlayerName, g_Config.m_ClDemoName != 0, Time))
			{
				ReportSaveFailure(false);
				return;
			}
			m_aTmpFilename[0] = '\0';
		}
		ResetRaceState();
		if(m_Finalizer.Pending())
			FinishPending(Force);
		else
			GameClient()->StopRaceRecordIfRecording();
	}
	catch(...)
	{
		ReportSaveFailure(false);
	}
}
