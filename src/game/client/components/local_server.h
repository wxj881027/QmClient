#ifndef GAME_CLIENT_COMPONENTS_LOCAL_SERVER_H
#define GAME_CLIENT_COMPONENTS_LOCAL_SERVER_H

#include "local_server_launch.h"

#include <base/types.h>

#include <engine/shared/config.h>

#include <game/client/component.h>

#include <chrono>
#include <vector>

class CLocalServer : public CComponentInterfaces
{
public:
	bool RunServer(const std::vector<const char *> &vpArguments, bool ConnectWhenReady = false);
	void Update();
	bool IsServerStarting() const;
	void KillServer();
	bool IsServerRunning();
	void RconAuthIfPossible();
	void Connect();
	void CancelConnect();

private:
	char m_aRconPassword[sizeof(g_Config.m_SvRconPassword)] = "";
	void ReportStartFailure(bool ExecutableMissing = false) noexcept;
	// 服务端启动后把自己实际监听的端口写进这个文件，客户端据此直连本机服务器。
	int m_Port = 0;
	bool m_WaitingForPort = false;
	bool m_ConnectRequested = false;
	std::chrono::nanoseconds m_StartTime{0};
	std::chrono::nanoseconds m_NextPortCheck{0};

	void Reset();
	void WaitForPort();

#if defined(CONF_PLATFORM_ANDROID)
	CLocalServerLaunch m_Launch;
#else
	PROCESS m_Process = INVALID_PROCESS;
#endif
};

#endif
