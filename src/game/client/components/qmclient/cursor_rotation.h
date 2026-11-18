#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CURSOR_ROTATION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CURSOR_ROTATION_H

#include <base/math.h>

#include <cmath>

// 游戏内光标自转角（弧度）。SpeedDegPerSec 为度/秒，0 表示不转；
// 角度按 360° 取模，负速度反向旋转，长时间运行不会让浮点角度无限增长。
inline float QmCursorRotationRadians(int SpeedDegPerSec, float TimeSeconds)
{
	if(SpeedDegPerSec == 0)
		return 0.0f;
	const float Degrees = std::fmod((float)SpeedDegPerSec * TimeSeconds, 360.0f);
	return Degrees * (pi / 180.0f);
}

#endif
