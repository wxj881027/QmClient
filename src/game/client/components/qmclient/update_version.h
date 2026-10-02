// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_UPDATE_VERSION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_UPDATE_VERSION_H

#include <array>

struct SQmClientVersion
{
	std::array<int, 4> m_aParts{};
	int m_Preview = 0;
};

bool ParseQmClientVersion(const char *pVersion, SQmClientVersion &Version);
bool IsQmClientRemoteVersionNewer(const char *pRemoteVersion, const char *pLocalVersion, bool LocalIsDevelopmentBuild = false);

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_UPDATE_VERSION_H
