#ifndef GAME_CLIENT_RACE_H
#define GAME_CLIENT_RACE_H

#include <base/vmath.h>

class CGameClient;

class CRaceHelper
{
	const CGameClient *m_pGameClient;

	int m_aFlagIndex[2] = {-1, -1};

public:
	void Init(const CGameClient *pGameClient);

	// these functions return the time in milliseconds, time -1 is invalid
	static int TimeFromSecondsStr(const char *pStr); // x.xxx
	// 保留完整文件名匹配；pPlayerName 为空时读取不带玩家后缀的录像。
	static int TimeFromDemoFilename(const char *pFilename, const char *pMap, const char *pPlayerName);
	static int TimeFromStr(const char *pStr); // x minute(s) x.xxx second(s) / x 分钟 x.xxx 秒
	static int TimeFromFinishMessage(const char *pStr, char *pNameBuf, int NameBufSize); // xxx finished in: ... / xxx 完成了地图，用时：...

	bool IsStart(vec2 Prev, vec2 Pos) const;
};

#endif // GAME_CLIENT_RACE_H
