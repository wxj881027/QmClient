// 碰撞体积可视化的几何辅助。
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_COLLISION_HITBOX_LOGIC_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_COLLISION_HITBOX_LOGIC_H

#include <base/dbg.h>
#include <base/vmath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

struct SQmHitboxTileRange
{
	ivec2 m_Begin;
	ivec2 m_End;
};

inline std::optional<SQmHitboxTileRange> QmCalcHitboxTileRange(vec2 ScreenMin, vec2 ScreenMax, ivec2 Pixels, ivec2 MapSize)
{
	if(Pixels.x <= 0 || Pixels.y <= 0 || MapSize.x <= 0 || MapSize.y <= 0 ||
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
	return SQmHitboxTileRange{
		{static_cast<int>(std::clamp(StartX, 0.0, static_cast<double>(MapSize.x))), static_cast<int>(std::clamp(StartY, 0.0, static_cast<double>(MapSize.y)))},
		{static_cast<int>(std::clamp(EndX, 0.0, static_cast<double>(MapSize.x))), static_cast<int>(std::clamp(EndY, 0.0, static_cast<double>(MapSize.y)))}};
}

// 分段数只决定单位圆方向；中心、半径和颜色仍由每次绘制决定。
class CQmHitboxCircleDirections
{
public:
	static constexpr int MIN_SEGMENTS = 8;
	static constexpr int MAX_SEGMENTS = 64;

private:
	std::array<std::array<vec2, MAX_SEGMENTS + 1>, MAX_SEGMENTS - MIN_SEGMENTS + 1> m_aaDirections{};
	std::array<bool, MAX_SEGMENTS - MIN_SEGMENTS + 1> m_aPrepared{};

public:
	// 各档位独立按需准备，不因角色和光束使用不同档位而反复失效。
	const vec2 *Get(int Segments)
	{
		dbg_assert(Segments >= MIN_SEGMENTS && Segments <= MAX_SEGMENTS, "unsupported hitbox circle segment count");
		const int Slot = Segments - MIN_SEGMENTS;
		auto &aDirections = m_aaDirections[Slot];
		if(!m_aPrepared[Slot])
		{
			const float Step = 2.0f * pi / Segments;
			for(int i = 0; i <= Segments; ++i)
			{
				// 保留原角度运算顺序及末端浮点值，不吸附到起点。
				const float Angle = Step * i;
				aDirections[i] = vec2(std::cos(Angle), std::sin(Angle));
			}
			m_aPrepared[Slot] = true;
		}
		return aDirections.data();
	}
};

struct SCollisionHitboxLine
{
	vec2 m_From;
	vec2 m_To;
};

constexpr int COLLISION_HITBOX_CAPSULE_MAX_ARC_SEGMENTS = 64;
constexpr int COLLISION_HITBOX_CAPSULE_MAX_LINES = 2 + COLLISION_HITBOX_CAPSULE_MAX_ARC_SEGMENTS * 2;

// 按原顺序输出胶囊轮廓，调用方可直接填充绘制缓冲，避免中间数组。
// 零长度线段退化为圆；非正半径、非有限输入和长度溢出不输出线段。
template<typename TEmitLine>
inline void BuildHitboxCapsuleOutline(vec2 From, vec2 To, float Radius, int ArcSegments, TEmitLine &&EmitLine)
{
	if(Radius <= 0.0f || !std::isfinite(Radius) || !std::isfinite(From.x) || !std::isfinite(From.y) || !std::isfinite(To.x) || !std::isfinite(To.y))
		return;

	ArcSegments = std::clamp(ArcSegments, 2, COLLISION_HITBOX_CAPSULE_MAX_ARC_SEGMENTS);
	// Subtracting two finite float coordinates can still overflow. Reject the
	// resulting vector before length/normalization rather than drawing garbage.
	const vec2 Delta = To - From;
	if(!std::isfinite(Delta.x) || !std::isfinite(Delta.y))
		return;
	const float SegmentLength = length(Delta);
	if(!std::isfinite(SegmentLength))
		return;
	if(SegmentLength <= 1e-6f)
	{
		const int CircleSegments = ArcSegments * 2;
		vec2 Previous = From + vec2(std::cos(0.0f), std::sin(0.0f)) * Radius;
		for(int Index = 0; Index < CircleSegments; ++Index)
		{
			const float Angle = 2.0f * pi * (float)(Index + 1) / (float)CircleSegments;
			const vec2 Current = From + vec2(std::cos(Angle), std::sin(Angle)) * Radius;
			EmitLine(Previous, Current);
			Previous = Current;
		}
		return;
	}

	const vec2 Direction = Delta / SegmentLength;
	const vec2 Normal(-Direction.y, Direction.x);
	EmitLine(From + Normal * Radius, To + Normal * Radius);
	EmitLine(To - Normal * Radius, From - Normal * Radius);

	const float DirectionAngle = std::atan2(Direction.y, Direction.x);
	const auto AppendArc = [&EmitLine, ArcSegments, Radius](vec2 Center, float StartAngle) {
		vec2 Previous = Center + vec2(std::cos(StartAngle), std::sin(StartAngle)) * Radius;
		for(int Index = 0; Index < ArcSegments; ++Index)
		{
			const float Angle = StartAngle - pi * (float)(Index + 1) / (float)ArcSegments;
			const vec2 Current = Center + vec2(std::cos(Angle), std::sin(Angle)) * Radius;
			EmitLine(Previous, Current);
			Previous = Current;
		}
	};

	// 终点半圆向线段前进方向鼓出，起点半圆向反方向鼓出。
	AppendArc(To, DirectionAngle + pi / 2.0f);
	AppendArc(From, DirectionAngle - pi / 2.0f);
}

// 保留按值获取轮廓的接口，渲染热路径使用上面的直接输出重载。
inline std::vector<SCollisionHitboxLine> BuildHitboxCapsuleOutline(vec2 From, vec2 To, float Radius, int ArcSegments = 16)
{
	std::vector<SCollisionHitboxLine> vLines;
	BuildHitboxCapsuleOutline(From, To, Radius, ArcSegments, [&](vec2 LineFrom, vec2 LineTo) {
		if(vLines.empty())
			vLines.reserve(2 + 2 * std::clamp(ArcSegments, 2, COLLISION_HITBOX_CAPSULE_MAX_ARC_SEGMENTS));
		vLines.push_back({LineFrom, LineTo});
	});
	return vLines;
}

#endif
