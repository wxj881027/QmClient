#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_TILE_VIEWPORT_RANGE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_TILE_VIEWPORT_RANGE_H

#include <base/vmath.h>

#include <cmath>
#include <optional>

struct SQmTileViewportRange
{
	double m_StartX;
	double m_StartY;
	double m_EndX;
	double m_EndY;
};

// 只准备原视口和密度范围；地图裁剪及整数表示限制由消费者决定。
inline std::optional<SQmTileViewportRange> QmCalcTileViewportRange(vec2 ScreenMin, vec2 ScreenMax, ivec2 Pixels)
{
	if(Pixels.x <= 0 || Pixels.y <= 0 ||
		!std::isfinite(ScreenMin.x) || !std::isfinite(ScreenMin.y) || !std::isfinite(ScreenMax.x) || !std::isfinite(ScreenMax.y) ||
		ScreenMin.x > ScreenMax.x || ScreenMin.y > ScreenMax.y)
		return std::nullopt;

	// 先沿原float除法和向零截断，再用double计算边距与密度；限幅后才转int。
	double StartX = std::trunc(static_cast<double>(ScreenMin.x / 32.0f)) - 1;
	double StartY = std::trunc(static_cast<double>(ScreenMin.y / 32.0f)) - 1;
	double EndX = std::trunc(static_cast<double>(ScreenMax.x / 32.0f)) + 1;
	double EndY = std::trunc(static_cast<double>(ScreenMax.y / 32.0f)) + 1;
	const int LimitX = Pixels.x / 12;
	const int LimitY = Pixels.y / 12;
	if(EndX - StartX > LimitX || EndY - StartY > LimitY)
	{
		// 原规则在任一轴超限时同时调整两轴，奇数边差仍向零截断。
		const double EdgeX = std::trunc((EndX - StartX - LimitX) / 2);
		const double EdgeY = std::trunc((EndY - StartY - LimitY) / 2);
		StartX += EdgeX;
		EndX -= EdgeX;
		StartY += EdgeY;
		EndY -= EdgeY;
		// 极大有限坐标的抵消可能失去低位；不能让误差扩大遍历范围。
		if(EndX - StartX > LimitX + 1 || EndY - StartY > LimitY + 1)
			return std::nullopt;
	}
	if(EndX < StartX || EndY < StartY)
		return std::nullopt;
	return SQmTileViewportRange{StartX, StartY, EndX, EndY};
}

#endif
