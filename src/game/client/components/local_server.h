#ifndef GAME_CLIENT_COMPONENTS_LOCAL_SERVER_H
#define GAME_CLIENT_COMPONENTS_LOCAL_SERVER_H

#include "local_server_launch.h"

#include <base/types.h>

#include <engine/shared/config.h>

#include <game/client/component.h>

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

private:
	char m_aRconPassword[sizeof(g_Config.m_SvRconPassword)] = "";
	void ReportStartFailure(bool ExecutableMissing = false) noexcept;

#if defined(CONF_PLATFORM_ANDROID)
	CLocalServerLaunch m_Launch;
#else
	PROCESS m_Process = INVALID_PROCESS;
#endif
};

#endif
