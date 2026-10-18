#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SCRIPTING_STATE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SCRIPTING_STATE_H

#include "impl.h"

#include <engine/shared/protocol.h>

#include <algorithm>
#include <cmath>

// 无本地玩家时沿用脚本的空值语义，不能把 -1 传给玩家或队伍数组。
template<typename TReadTeam>
CScriptingCtx::Any QmScriptLocalTeam(int ClientId, TReadTeam ReadTeam)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return nullptr;
	return ReadTeam(ClientId);
}

inline CScriptingCtx::Any QmScriptLocation(int Width, int Height, float X, float Y)
{
	if(Width <= 0 || Height <= 0 || !std::isfinite(X) || !std::isfinite(Y))
		return nullptr;
	static const char *s_apLocations[] = {
		"NW", "N", "NE",
		"W", "C", "E",
		"SW", "S", "SE"};
	// 保留原来的地图分区比例；先限制浮点范围，再转换为数组下标。
	const int Column = static_cast<int>(std::clamp(X / (Width * 30.0f) * 3.0f, 0.0f, 2.0f));
	const int Row = static_cast<int>(std::clamp(Y / (Height * 30.0f) * 3.0f, 0.0f, 2.0f));
	return s_apLocations[Row * 3 + Column];
}

#endif
