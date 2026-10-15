#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_STATISTICS_RING_GEOMETRY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_STATISTICS_RING_GEOMETRY_H

#include <engine/graphics.h>

#include <cmath>

inline IGraphics::CFreeformItem QmStatisticsRingSegment(vec2 Center, float OuterRadius, float InnerRadius, float StartAngle, float EndAngle)
{
	const vec2 Start(std::cos(StartAngle), std::sin(StartAngle));
	const vec2 End(std::cos(EndAngle), std::sin(EndAngle));
	// Freeform 按两排顶点编码，渲染器三角形为 0/1/3 与 0/3/2。
	// 内圈必须同样按起点/终点排列；按周长顺序传入会交叉并漏出底环。
	return {Center + Start * OuterRadius, Center + End * OuterRadius,
		Center + Start * InnerRadius, Center + End * InnerRadius};
}

#endif
