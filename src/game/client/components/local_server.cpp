#include "local_server.h"

#include <game/client/gameclient.h>
#include <game/localization.h>

#if defined(CONF_PLATFORM_ANDROID)
#include <android/android_main.h>
#endif

bool CLocalServer::RunServer(const std::vector<const char *> &vpArguments, bool ConnectWhenReady)
{
	if(IsServerStarting())
		return true;
	if(IsServerRunning())
	{
		if(ConnectWhenReady)
			Client()->Connect("localhost");
		return true;
	}
	char aPassword[sizeof(m_aRconPassword)] = "";
	secure_random_password(aPassword, sizeof(aPassword), 16);
	char aAuthCommand[64 + sizeof(m_aRconPassword)];
	str_format(aAuthCommand, sizeof(aAuthCommand), "auth_add %s admin %s", DEFAULT_SAVED_RCON_USER, aPassword);

#if defined(CONF_PLATFORM_ANDROID)
	if(!m_Launch.TryPrepare(vpArguments.data(), vpArguments.size(), aAuthCommand, ConnectWhenReady))
	{
		ReportStartFailure();
		return false;
	}
	mem_copy(m_aRconPassword, aPassword, sizeof(m_aRconPassword));
	RequestAndroidNotificationPermission();
	return true;
#else
	CLocalServerLaunch Launch;
	if(!Launch.TryPrepare(vpArguments.data(), vpArguments.size(), aAuthCommand, ConnectWhenReady))
	{
		ReportStartFailure();
		return false;
	}
	mem_copy(m_aRconPassword, aPassword, sizeof(m_aRconPassword));
	char aBuf[IO_MAX_PATH_LENGTH];
	Storage()->GetBinaryPath(PLAT_SERVER_EXEC, aBuf, sizeof(aBuf));
#if defined(CONF_PLATFORM_MACOS)
	if(!fs_is_file(aBuf) && fs_parent_dir(aBuf) == 0)
	{
		str_append(aBuf, "/../../../DDNet-Server.app/Contents/MacOS/");
		str_append(aBuf, PLAT_SERVER_EXEC);
	}
#endif
	// No / in binary path means to search in $PATH, so it is expected that the file can't be opened. Just try executing anyway.
	if(str_find(aBuf, "/") == nullptr || fs_is_file(aBuf))
	{
		try
		{
#if defined(CONF_FAMILY_WINDOWS)
			m_Process = shell_execute_owned(aBuf, Launch.Data(), Launch.Count());
#else
			m_Process = shell_execute(aBuf, EShellExecuteWindowState::BACKGROUND, Launch.Data(), Launch.Count());
#endif
		}
		catch(...)
		{
			m_Process = INVALID_PROCESS;
		}
		if(m_Process != INVALID_PROCESS)
		{
			GameClient()->m_Menus.ForceRefreshLanPage();
			if(ConnectWhenReady)
				Client()->Connect("localhost");
			return true;
		}
		else
		{
			ReportStartFailure();
			return false;
		}
	}
	else
	{
		ReportStartFailure(true);
		return false;
	}
#endif
}

void CLocalServer::ReportStartFailure(bool ExecutableMissing) noexcept
{
#if defined(CONF_PLATFORM_ANDROID)
	m_Launch.Reset();
#endif
	mem_zero(m_aRconPassword, sizeof(m_aRconPassword));
	try
	{
		const char *pMessage;
		if(ExecutableMissing)
			pMessage = Localize("Server executable not found, can't run server");
		else
		{
#if defined(CONF_PLATFORM_ANDROID)
			pMessage = Localize("Server could not be started. Make sure to grant the notification permission in the app settings so the server can run in the background.");
#else
			pMessage = Localize("Server could not be started");
#endif
		}
		Client()->AddWarning(SWarning(pMessage));
	}
	catch(...)
	{
		// 内存不足时提示本身也可能分配失败，仍须保留已清理的失败状态。
	}
}

bool CLocalServer::IsServerStarting() const
{
#if defined(CONF_PLATFORM_ANDROID)
	return m_Launch.IsStarting();
#else
	return false;
#endif
}

void CLocalServer::Update()
{
#if defined(CONF_PLATFORM_ANDROID)
	if(!m_Launch.IsStarting())
		return;
	if(m_Launch.IsReady())
	{
		const auto Permission = AndroidNotificationPermission();
		if(Permission == EAndroidPermissionState::PENDING || Permission == EAndroidPermissionState::UNKNOWN)
			return;
		if(Permission == EAndroidPermissionState::GRANTED && StartAndroidServer(m_Launch.Data(), m_Launch.Count()))
		{
			m_Launch.MarkBinding(time_get_nanoseconds());
			GameClient()->m_Menus.ForceRefreshLanPage();
		}
		else
		{
			if(Permission == EAndroidPermissionState::GRANTED)
				StopAndroidServer();
			ReportStartFailure();
		}
		return;
	}
	switch(m_Launch.PollBinding(IsAndroidServerRunning(), time_get_nanoseconds()))
	{
	case CLocalServerLaunch::EBindingResult::CONNECT:
		Client()->Connect("localhost");
		break;
	case CLocalServerLaunch::EBindingResult::FAILED:
		StopAndroidServer();
		ReportStartFailure();
		break;
	default:
		break;
	}
#endif
}

void CLocalServer::KillServer()
{
	mem_zero(m_aRconPassword, sizeof(m_aRconPassword));
#if defined(CONF_PLATFORM_ANDROID)
	m_Launch.Reset();
	StopAndroidServer();
	GameClient()->m_Menus.ForceRefreshLanPage();
#else
	if(m_Process != INVALID_PROCESS && kill_process(m_Process))
	{
		m_Process = INVALID_PROCESS;
		GameClient()->m_Menus.ForceRefreshLanPage();
	}
#endif
}

bool CLocalServer::IsServerRunning()
{
#if defined(CONF_PLATFORM_ANDROID)
	return IsAndroidServerRunning();
#else
	if(m_Process != INVALID_PROCESS && !is_process_alive(m_Process))
	{
		KillServer();
	}
	return m_Process != INVALID_PROCESS;
#endif
}

void CLocalServer::RconAuthIfPossible()
{
	if(!IsServerRunning() ||
		m_aRconPassword[0] == '\0' ||
		Client()->ServerAddress() == nullptr ||
		!net_addr_is_local(Client()->ServerAddress()))
	{
		return;
	}
	Client()->RconAuth(DEFAULT_SAVED_RCON_USER, m_aRconPassword, g_Config.m_ClDummy);
}
