#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BG_DRAW_GEOMETRY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BG_DRAW_GEOMETRY_H

#include <base/color.h>
#include <base/math.h>
#include <base/vmath.h>

#include <engine/graphics.h>

#include <game/client/components/tclient/bg_draw_file.h>

#include <algorithm>
#include <array>
#include <cmath>

inline constexpr int QM_BG_DRAW_ARC_QUADS = 20;

inline vec2 QmBgDrawWorldPosition(const vec2 &Target, const vec2 &Center, float Zoom, bool UseCenter)
{
	return UseCenter ? Center : (Target - Center) * Zoom + Center;
}

inline CBgDrawItemDataPoint QmBgDrawMakePoint(vec2 Position, float Zoom, int Width, ColorRGBA Color)
{
	return {Position, static_cast<float>(Width) * Zoom, Color};
}

inline bool QmBgDrawPointIsFinite(const CBgDrawItemDataPoint &Point)
{
	return std::isfinite(Point.x) && std::isfinite(Point.y) && std::isfinite(Point.w) &&
	       std::isfinite(Point.r) && std::isfinite(Point.g) && std::isfinite(Point.b) && std::isfinite(Point.a);
}

inline bool QmBgDrawColorIsFinite(ColorRGBA Color)
{
	return std::isfinite(Color.r) && std::isfinite(Color.g) && std::isfinite(Color.b) && std::isfinite(Color.a);
}

inline bool QmBgDrawQuadIsFinite(const IGraphics::CFreeformItem &Quad)
{
	return std::isfinite(Quad.m_X0) && std::isfinite(Quad.m_Y0) && std::isfinite(Quad.m_X1) && std::isfinite(Quad.m_Y1) &&
	       std::isfinite(Quad.m_X2) && std::isfinite(Quad.m_Y2) && std::isfinite(Quad.m_X3) && std::isfinite(Quad.m_Y3);
}

// 返回0即整批不可用；输出只在返回正数时消费，不能提交中途生成的前缀。
inline int QmBgDrawBuildArc(vec2 Pos, float Angle1, float Angle2, float Width,
	std::array<IGraphics::CFreeformItem, QM_BG_DRAW_ARC_QUADS> &aQuads)
{
	if(!std::isfinite(Pos.x) || !std::isfinite(Pos.y) || !std::isfinite(Width) ||
		!std::isfinite(Angle1) || !std::isfinite(Angle2))
		return 0;
	const float Delta = Angle2 - Angle1;
	if(!std::isfinite(Delta))
		return 0;
	const float Radius = Width / 2.0f;
	// 先夹到原有[1,20]分段范围，再转整数；有限大角度的比例也可能溢出。
	const int Segments = static_cast<int>(std::clamp(std::fabs(Delta) / (pi / 8.0f), 1.0f, static_cast<float>(QM_BG_DRAW_ARC_QUADS)));
	const float SegmentAngle = Delta / Segments;
	for(int i = 0; i < Segments; ++i)
	{
		const float A1 = Angle1 + static_cast<float>(i) * SegmentAngle;
		const float A2 = A1 + SegmentAngle;
		if(!std::isfinite(A1) || !std::isfinite(A2))
			return 0;
		const vec2 P1 = Pos + direction(A1) * Radius;
		const vec2 P2 = Pos + direction(A2) * Radius;
		aQuads[i] = IGraphics::CFreeformItem(P1.x, P1.y, P2.x, P2.y, Pos.x, Pos.y, Pos.x, Pos.y);
		if(!QmBgDrawQuadIsFinite(aQuads[i]))
			return 0;
	}
	return Segments;
}

#endif
