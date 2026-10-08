#include "steam_client_path.h"
#include "steam_probe.h"

#include <base/fs.h>
#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/steam.h>

#include <game/version.h>

#include <steam/steam_api_flat.h>

namespace
{

	constexpr const char *STEAM_SILENT_ARGUMENT = "-silent";
	const char *SteamSourceName(ESteamClientSource Source)
	{
		switch(Source)
		{
		case ESteamClientSource::NOT_FOUND: return "not found";
		case ESteamClientSource::MANUAL: return "manual path";
		case ESteamClientSource::REGISTRY: return "registry";
		case ESteamClientSource::RUNNING_PROCESS: return "running process";
		case ESteamClientSource::COMMON_DIRECTORY: return "common directory";
		case ESteamClientSource::ENVIRONMENT_PATH: return "PATH";
		case ESteamClientSource::PLATFORM_DIRECTORY: return "platform directory";
		}
		return "unknown";
	}

	class CSteam : public ISteam
	{
		HSteamPipe m_SteamPipe;
		ISteamApps *m_pSteamApps;
		ISteamFriends *m_pSteamFriends;
		char m_aPlayerName[16];
		bool m_GotConnectAddr;
		NETADDR m_ConnectAddr;

		void SetClientPresence()
		{
			SteamAPI_ISteamFriends_SetRichPresence(m_pSteamFriends, "status", CLIENT_NAME);
			SteamAPI_ISteamFriends_SetRichPresence(m_pSteamFriends, "steam_display", "#Status");
		}
		void ResetClientPresence()
		{
			SteamAPI_ISteamFriends_ClearRichPresence(m_pSteamFriends);
			SetClientPresence();
		}

	public:
		CSteam()
		{
			SteamAPI_ManualDispatch_Init();
			m_SteamPipe = SteamAPI_GetHSteamPipe();
			m_pSteamApps = SteamAPI_SteamApps_v008();
			m_pSteamFriends = SteamAPI_SteamFriends_v017();

			ReadLaunchCommandLine();
			str_copy(m_aPlayerName, SteamAPI_ISteamFriends_GetPersonaName(m_pSteamFriends));
			ResetClientPresence();
		}
		~CSteam() override
		{
			SteamAPI_Shutdown();
		}

		void ParseConnectString(const char *pConnect)
		{
			if(pConnect[0])
			{
				NETADDR Connect;
				if(net_host_lookup(pConnect, &Connect, NETTYPE_ALL) == 0)
				{
					m_ConnectAddr = Connect;
					m_GotConnectAddr = true;
				}
				else
				{
					dbg_msg("steam", "got unparsable connect string: '%s'", pConnect);
				}
			}
		}

		void ReadLaunchCommandLine()
		{
			char aConnect[NETADDR_MAXSTRSIZE];
			int ConnectSize = SteamAPI_ISteamApps_GetLaunchCommandLine(m_pSteamApps, aConnect, sizeof(aConnect));
			if(ConnectSize >= NETADDR_MAXSTRSIZE)
			{
				ConnectSize = NETADDR_MAXSTRSIZE - 1;
			}
			aConnect[ConnectSize] = 0;
			m_GotConnectAddr = false;
			ParseConnectString(aConnect);
		}

		void OnGameRichPresenceJoinRequested(GameRichPresenceJoinRequested_t *pEvent)
		{
			ParseConnectString(pEvent->m_aRGCHConnect);
		}

		const char *GetPlayerName() override
		{
			return m_aPlayerName;
		}

		const NETADDR *GetConnectAddress() override
		{
			if(m_GotConnectAddr)
			{
				return &m_ConnectAddr;
			}
			else
			{
				return nullptr;
			}
		}
		void ClearConnectAddress() override
		{
			m_GotConnectAddr = false;
		}

		void Update() override
		{
			SteamAPI_ManualDispatch_RunFrame(m_SteamPipe);
			CallbackMsg_t Callback;
			while(SteamAPI_ManualDispatch_GetNextCallback(m_SteamPipe, &Callback))
			{
				switch(Callback.m_iCallback)
				{
				case NewUrlLaunchParameters_t::k_iCallback:
					ReadLaunchCommandLine();
					break;
				case GameRichPresenceJoinRequested_t::k_iCallback:
					OnGameRichPresenceJoinRequested((GameRichPresenceJoinRequested_t *)Callback.m_pubParam);
					break;
				default:
					if(g_Config.m_Debug)
					{
						dbg_msg("steam/dbg", "unhandled callback id=%d", Callback.m_iCallback);
					}
				}
				SteamAPI_ManualDispatch_FreeLastCallback(m_SteamPipe);
			}
		}
		void ClearGameInfo() override
		{
			ResetClientPresence();
		}
		void SetGameInfo(const NETADDR &ServerAddr, const char *pMapName, bool AnnounceAddr) override
		{
			if(AnnounceAddr)
			{
				char aServerAddr[NETADDR_MAXSTRSIZE];
				net_addr_str(&ServerAddr, aServerAddr, sizeof(aServerAddr), true);
				SteamAPI_ISteamFriends_SetRichPresence(m_pSteamFriends, "connect", aServerAddr);
				SteamAPI_ISteamFriends_SetRichPresence(m_pSteamFriends, "steam_player_group", aServerAddr);
			}

			SteamAPI_ISteamFriends_SetRichPresence(m_pSteamFriends, "map", pMapName);
			SetClientPresence();
		}
	};

	class CSteamStub : public ISteam
	{
	public:
		const char *GetPlayerName() override { return nullptr; }
		const NETADDR *GetConnectAddress() override { return nullptr; }
		void ClearConnectAddress() override {}
		void Update() override {}
		void ClearGameInfo() override {}
		void SetGameInfo(const NETADDR &ServerAddr, const char *pMapName, bool AnnounceAddr) override {}
	};

} // namespace

#if defined(CONF_FAMILY_UNIX)
namespace
{
	// Unix 智能识别：标准安装脚本位置与常见二进制路径。
	bool SteamFindClientUnix(char *pBuffer, int BufferSize)
	{
#if defined(CONF_PLATFORM_MACOS)
		if(fs_is_dir("/Applications/Steam.app"))
		{
			str_copy(pBuffer, "/Applications/Steam.app", BufferSize);
			return true;
		}
		return false;
#else
		const char *apSystemCandidates[] = {"/usr/bin/steam", "/usr/local/bin/steam"};
		for(const char *pCandidate : apSystemCandidates)
		{
			if(fs_is_file(pCandidate))
			{
				str_copy(pBuffer, pCandidate, BufferSize);
				return true;
			}
		}
		const char *pHome = getenv("HOME");
		if(pHome == nullptr || pHome[0] == '\0')
			return false;
		char aPath[1024];
		const char *apHomeCandidates[] = {
			".steam/steam/steam.sh",
			".steam/root/steam.sh",
			".local/share/Steam/steam.sh",
		};
		for(const char *pCandidate : apHomeCandidates)
		{
			str_format(aPath, (int)sizeof(aPath), "%s/%s", pHome, pCandidate);
			if(fs_is_file(aPath))
			{
				str_copy(pBuffer, aPath, BufferSize);
				return true;
			}
		}
		return false;
#endif
	}
} // namespace
#endif

bool SteamFindClient(char *pBuffer, int BufferSize, ESteamClientSource *pSource, const char *pManualPath)
{
	if(pSource != nullptr)
		*pSource = ESteamClientSource::NOT_FOUND;
	if(pBuffer == nullptr || BufferSize <= 0)
		return false;
	pBuffer[0] = '\0';
#if defined(CONF_PLATFORM_ANDROID)
	return false;
#else
	bool ManualFound = false;
#if defined(CONF_FAMILY_WINDOWS)
	ManualFound = SteamProbeValidateManualPath(pManualPath, pBuffer, BufferSize);
#else
#if defined(CONF_PLATFORM_MACOS)
	const std::string ManualPath = SteamManualPathCandidate(pManualPath, ESteamClientPlatform::MACOS);
	ManualFound = !ManualPath.empty() && fs_is_dir(ManualPath.c_str());
#else
	const std::string ManualPath = SteamManualPathCandidate(pManualPath, ESteamClientPlatform::UNIX);
	ManualFound = !ManualPath.empty() && fs_is_file(ManualPath.c_str());
#endif
	if(ManualFound)
		str_copy(pBuffer, ManualPath.c_str(), BufferSize);
#endif
	if(ManualFound)
	{
		if(pSource != nullptr)
			*pSource = ESteamClientSource::MANUAL;
		return true;
	}
#if defined(CONF_FAMILY_WINDOWS)
	return SteamProbeFindClientWindows(pBuffer, BufferSize, pSource);
#else
	const bool Found = SteamFindClientUnix(pBuffer, BufferSize);
	if(Found && pSource != nullptr)
		*pSource = ESteamClientSource::PLATFORM_DIRECTORY;
	return Found;
#endif
#endif
}

SSteamClientInfo SteamInspectClient(const char *pManualPath)
{
	SSteamClientInfo Info;
	SteamFindClient(Info.m_aPath, sizeof(Info.m_aPath), &Info.m_Source, pManualPath);
	Info.m_ManualPathRejected = pManualPath != nullptr && pManualPath[0] != '\0' && Info.m_Source != ESteamClientSource::MANUAL;
#if defined(CONF_FAMILY_WINDOWS)
	char aRunningPath[1024];
	Info.m_Running = SteamProbePathFromRunningProcess(aRunningPath, sizeof(aRunningPath));
	Info.m_RunningKnown = true;
#endif
	dbg_msg("steam", "detection: source=%s, path='%s', manual_rejected=%d", SteamSourceName(Info.m_Source), Info.m_aPath, Info.m_ManualPathRejected);
	return Info;
}

bool SteamOpenClient()
{
#if defined(CONF_PLATFORM_ANDROID)
	return false;
#else
	// 智能识别：先定位 Steam 客户端再启动；未安装时直接放弃，
	// 绝不把找不到的名字交给 shell，避免触发系统「找不到文件」弹窗。
	char aSteamPath[1024];
	ESteamClientSource Source;
	if(!SteamFindClient(aSteamPath, (int)sizeof(aSteamPath), &Source, g_Config.m_QmSteamClientPath))
	{
		dbg_msg("steam", "steam client not found, skip launch");
		return false;
	}
	if(g_Config.m_Debug)
		dbg_msg("steam", "found steam client: '%s', source=%s", aSteamPath, SteamSourceName(Source));
#if defined(CONF_PLATFORM_MACOS)
	// macOS：用 open 启动检测到的应用包。
	const char *apOpenArguments[] = {aSteamPath};
	return shell_execute("open", EShellExecuteWindowState::BACKGROUND, apOpenArguments, 1) != INVALID_PROCESS;
#else
	const char *apArguments[] = {STEAM_SILENT_ARGUMENT};
	return shell_execute(aSteamPath, EShellExecuteWindowState::BACKGROUND, apArguments, 1) != INVALID_PROCESS;
#endif
#endif
}

ISteam *CreateSteam()
{
	if(!SteamAPI_Init())
	{
		return new CSteamStub();
	}
	return new CSteam();
}
