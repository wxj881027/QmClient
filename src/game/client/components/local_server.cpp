#include "local_server.h"

#include <base/system.h>
#include <base/time.h>

#include <engine/serverbrowser.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

#include <chrono>
#include <cstdlib>

#if defined(CONF_PLATFORM_ANDROID)
#include <android/android_main.h>
#endif

namespace {
// 每个客户端进程独占一个端口文件，避免多开时读到别人的端口。
const char *PortFile(char *pBuf, unsigned BufSize)
{
	str_format(pBuf, BufSize, "local_server_port.%d.txt", pid());
	return pBuf;
}
}

void CLocalServer::Reset()
{
	mem_zero(m_aRconPassword, sizeof(m_aRconPassword));
	m_Port = 0;
	m_WaitingForPort = false;
	m_ConnectRequested = false;

	char aPortFile[IO_MAX_PATH_LENGTH];
	Storage()->RemoveFile(PortFile(aPortFile, sizeof(aPortFile)), IStorage::TYPE_SAVE);
}

void CLocalServer::WaitForPort()
{
	m_WaitingForPort = true;
	m_StartTime = time_get_nanoseconds();
	m_NextPortCheck = m_StartTime;
}

void CLocalServer::Connect()
{
	// 服务端异步上报端口，拿到端口后再连。
	m_ConnectRequested = true;
	if(m_Port == 0 && !m_WaitingForPort)
	{
		WaitForPort();
	}
}

void CLocalServer::CancelConnect()
{
	m_ConnectRequested = false;
}

bool CLocalServer::RunServer(const std::vector<const char *> &vpArguments, bool ConnectWhenReady)
{
	if(IsServerStarting())
		return true;
	if(IsServerRunning())
	{
		if(ConnectWhenReady)
			Connect();
		return true;
	}
	Reset();
	secure_random_password(m_aRconPassword, sizeof(m_aRconPassword), 16);
	char aAuthCommand[64 + sizeof(m_aRconPassword)];
	str_format(aAuthCommand, sizeof(aAuthCommand), "auth_add %s admin %s", DEFAULT_SAVED_RCON_USER, m_aRconPassword);

	std::vector<const char *> vpArgumentsWithAuth = vpArguments;
	vpArgumentsWithAuth.push_back(aAuthCommand);

	char aPortFile[IO_MAX_PATH_LENGTH];
	char aPortFileCommand[IO_MAX_PATH_LENGTH + 16];
	str_format(aPortFileCommand, sizeof(aPortFileCommand), "sv_port_file %s", PortFile(aPortFile, sizeof(aPortFile)));
	vpArgumentsWithAuth.push_back(aPortFileCommand);

#if defined(CONF_PLATFORM_ANDROID)
	m_vPendingArguments.clear();
	for(const char *pArgument : vpArgumentsWithAuth)
		m_vPendingArguments.emplace_back(pArgument);
	m_ConnectWhenReady = ConnectWhenReady;
	m_StartPending = true;
	RequestAndroidNotificationPermission();
	return true;
#else
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
#if defined(CONF_FAMILY_WINDOWS)
		m_Process = shell_execute_owned(aBuf, vpArgumentsWithAuth.data(), vpArgumentsWithAuth.size());
#else
		m_Process = shell_execute(aBuf, EShellExecuteWindowState::BACKGROUND, vpArgumentsWithAuth.data(), vpArgumentsWithAuth.size());
#endif
		if(m_Process != INVALID_PROCESS)
		{
			GameClient()->m_Menus.ForceRefreshLanPage();
			if(ConnectWhenReady)
				Connect();
			return true;
		}
		else
		{
			Client()->AddWarning(SWarning(Localize("Server could not be started")));
			Reset();
			return false;
		}
	}
	else
	{
		Client()->AddWarning(SWarning(Localize("Server executable not found, can't run server")));
		Reset();
		return false;
	}
#endif
}

bool CLocalServer::IsServerStarting() const
{
#if defined(CONF_PLATFORM_ANDROID)
	return m_StartPending;
#else
	return false;
#endif
}

void CLocalServer::Update()
{
	if(m_WaitingForPort && time_get_nanoseconds() >= m_NextPortCheck)
	{
		m_NextPortCheck = time_get_nanoseconds() + std::chrono::milliseconds(100);
		char aPortFile[IO_MAX_PATH_LENGTH];
		char *pPort = Storage()->ReadFileStr(PortFile(aPortFile, sizeof(aPortFile)), IStorage::TYPE_SAVE);
		if(pPort != nullptr)
		{
			const int Port = str_toint(pPort);
			free(pPort);
			Storage()->RemoveFile(aPortFile, IStorage::TYPE_SAVE);
			// 文件内容非法时保持等待，交给下面的超时分支决定。
			if(Port > 0 && Port <= 65535)
			{
				m_Port = Port;
				m_WaitingForPort = false;
			}
		}

		if(m_WaitingForPort)
		{
			const std::chrono::nanoseconds Waiting = time_get_nanoseconds() - m_StartTime;
			if(Waiting > std::chrono::seconds(5) && !IsServerRunning())
			{
				// Android 的服务端是异步启动的，多给它一点时间再判定失败。
				m_WaitingForPort = false;
				if(m_ConnectRequested)
				{
					m_ConnectRequested = false;
					Client()->AddWarning(SWarning(Localize("Server could not be started")));
				}
			}
			else if(Waiting > std::chrono::seconds(5))
			{
				// 服务端在跑却没写端口文件，多半是没有写权限；退回默认 LAN 端口。
				m_Port = IServerBrowser::LAN_PORT_BEGIN;
				m_WaitingForPort = false;
				mem_zero(m_aRconPassword, sizeof(m_aRconPassword));
			}
		}
	}

	if(m_ConnectRequested)
	{
		const IClient::EClientState State = Client()->State();
		if(State != IClient::STATE_OFFLINE && State != IClient::STATE_ONLINE)
		{
			// 期间用户自己发起了连接，优先用户的请求。
			m_ConnectRequested = false;
		}
		else if(m_Port != 0)
		{
			m_ConnectRequested = false;
			char aAddress[32];
			str_format(aAddress, sizeof(aAddress), "localhost:%d", m_Port);
			Client()->Connect(aAddress);
		}
	}

#if defined(CONF_PLATFORM_ANDROID)
	if(!m_StartPending)
		return;
	const auto Permission = AndroidNotificationPermission();
	if(Permission == EAndroidPermissionState::PENDING || Permission == EAndroidPermissionState::UNKNOWN)
		return;
	m_StartPending = false;
	std::vector<const char *> vpArguments;
	for(const auto &Argument : m_vPendingArguments)
		vpArguments.push_back(Argument.c_str());
	if(Permission == EAndroidPermissionState::GRANTED && StartAndroidServer(vpArguments.data(), vpArguments.size()))
	{
		GameClient()->m_Menus.ForceRefreshLanPage();
		if(m_ConnectWhenReady)
			Client()->Connect("localhost");
	}
	else
	{
		Client()->AddWarning(SWarning(Localize("Server could not be started. Make sure to grant the notification permission in the app settings so the server can run in the background.")));
		mem_zero(m_aRconPassword, sizeof(m_aRconPassword));
	}
	m_vPendingArguments.clear();
	m_ConnectWhenReady = false;
#endif
}

void CLocalServer::KillServer()
{
#if defined(CONF_PLATFORM_ANDROID)
	m_StartPending = false;
	m_ConnectWhenReady = false;
	m_vPendingArguments.clear();
	ExecuteAndroidServerCommand("shutdown");
	GameClient()->m_Menus.ForceRefreshLanPage();
#else
	if(m_Process != INVALID_PROCESS && kill_process(m_Process))
	{
		m_Process = INVALID_PROCESS;
		GameClient()->m_Menus.ForceRefreshLanPage();
	}
#endif
	Reset();
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
