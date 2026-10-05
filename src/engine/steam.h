#ifndef ENGINE_STEAM_H
#define ENGINE_STEAM_H

#include "kernel.h"

#include <base/types.h>

class ISteam : public IInterface
{
	MACRO_INTERFACE("steam")
public:
	// Returns NULL if the name cannot be determined.
	virtual const char *GetPlayerName() = 0;

	// Returns NULL if the no server needs to be joined.
	// Can change while the game is running.
	virtual const NETADDR *GetConnectAddress() = 0;
	virtual void ClearConnectAddress() = 0;

	virtual void Update() = 0;

	virtual void ClearGameInfo() = 0;
	virtual void SetGameInfo(const NETADDR &ServerAddr, const char *pMapName, bool AnnounceAddr) = 0;
};

// 智能识别本机 Steam 客户端：依次检查注册表（用户级/机器级/App Paths）、
// 运行中的 steam.exe 进程、常见安装目录与 PATH。找到时把可执行文件完整路径
// 写入 pBuffer 并返回 true；未安装或无法确定时返回 false。
bool SteamFindClient(char *pBuffer, int BufferSize);

// 打开 Steam 主窗口，不启动任何游戏。
// 先经 SteamFindClient 定位客户端再启动；未安装时直接返回 false，
// 不调用 shell，因此不会触发系统「找不到文件」弹窗。
bool SteamOpenClient();

ISteam *CreateSteam();

#endif // ENGINE_STEAM_H
