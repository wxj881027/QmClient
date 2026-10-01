#ifndef GAME_CLIENT_QMUI_QMPIEMENUGEOMETRY_H
#define GAME_CLIENT_QMUI_QMPIEMENUGEOMETRY_H

#include <base/vmath.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace qm_pie_menu_ui
{
	constexpr int MAX_ARC_SEGMENTS = 256;
	constexpr float ARC_ERROR_PIXELS = 0.2f;
	constexpr float FEATHER_PIXELS = 1.5f;

	struct SBoundaryPoint
	{
		vec2 m_Position;
		vec2 m_Fringe;
	};

	struct SGeometry
	{
		std::array<SBoundaryPoint, 2 * (MAX_ARC_SEGMENTS + 1)> m_aPoints{};
		int m_PointCount = 0;
		int m_ArcSegments = 0;
		float m_Feather = 0.0f;
		bool m_FullRing = false;
		bool m_Disc = false;

		bool HasEdge(int Index) const
		{
			return !m_FullRing || (Index != m_ArcSegments && Index != m_PointCount - 1);
		}

		std::array<vec2, 4> FillQuad(int Index) const
		{
			if(m_Disc)
				return {m_aPoints[Index].m_Position, m_aPoints[(Index + 1) % m_PointCount].m_Position, vec2(0.0f, 0.0f), vec2(0.0f, 0.0f)};
			return {m_aPoints[Index].m_Position, m_aPoints[Index + 1].m_Position,
				m_aPoints[m_PointCount - 1 - Index].m_Position, m_aPoints[m_PointCount - 2 - Index].m_Position};
		}
	};

	inline float UnitsPerPixel(vec2 MappedSize, vec2 FramebufferSize)
	{
		if(FramebufferSize.x <= 0.0f || FramebufferSize.y <= 0.0f)
			return 0.0f;
		return std::max(std::abs(MappedSize.x) / FramebufferSize.x, std::abs(MappedSize.y) / FramebufferSize.y);
	}

	inline int ArcSegments(float Radius, float SweepRadians, float PixelSize)
	{
		if(Radius <= 0.0f || SweepRadians <= 0.0f || PixelSize <= 0.0f)
			return 0;
		// 由弦中点到圆弧的距离约束分段，避免少量选项时出现明显折线。
		const double RelativeError = std::min(static_cast<double>(ARC_ERROR_PIXELS) * PixelSize / Radius, 1.0);
		const double Step = 2.0 * std::acos(1.0 - RelativeError);
		return std::clamp(static_cast<int>(std::ceil(SweepRadians / Step)), 8, MAX_ARC_SEGMENTS);
	}

	inline void BuildFringe(SGeometry &Geometry)
	{
		for(int Index = 0; Index < Geometry.m_PointCount; ++Index)
		{
			auto &Point = Geometry.m_aPoints[Index];
			if(Geometry.m_FullRing)
			{
				const float Direction = Index <= Geometry.m_ArcSegments ? 1.0f : -1.0f;
				Point.m_Fringe = Point.m_Position + normalize(Point.m_Position) * (Geometry.m_Feather * Direction);
				continue;
			}

			const vec2 Previous = Geometry.m_aPoints[(Index + Geometry.m_PointCount - 1) % Geometry.m_PointCount].m_Position;
			const vec2 Next = Geometry.m_aPoints[(Index + 1) % Geometry.m_PointCount].m_Position;
			const vec2 Incoming = normalize(Point.m_Position - Previous);
			const vec2 Outgoing = normalize(Next - Point.m_Position);
			const vec2 NormalBefore(Incoming.y, -Incoming.x);
			const vec2 NormalAfter(Outgoing.y, -Outgoing.x);
			vec2 Offset = (NormalBefore + NormalAfter) * (Geometry.m_Feather / std::max(0.5f, 1.0f + dot(NormalBefore, NormalAfter)));
			const float OffsetLength = length(Offset);
			if(OffsetLength > Geometry.m_Feather * 2.0f)
				Offset *= Geometry.m_Feather * 2.0f / OffsetLength;
			// 相邻边共用同一个外侧角点，羽化带只接边，不在角落重复混合透明度。
			Point.m_Fringe = Point.m_Position + Offset;
		}
	}

	inline SGeometry BuildSector(float InnerRadius, float OuterRadius, float StartAngle, float EndAngle, float GapDegrees, float PixelSize)
	{
		SGeometry Geometry;
		if(InnerRadius <= 0.0f || OuterRadius <= InnerRadius || EndAngle <= StartAngle || PixelSize <= 0.0f)
			return Geometry;

		const float Sweep = std::min(EndAngle - StartAngle, 360.0f) * pi / 180.0f;
		Geometry.m_ArcSegments = ArcSegments(OuterRadius, Sweep, PixelSize);
		Geometry.m_PointCount = 2 * (Geometry.m_ArcSegments + 1);
		Geometry.m_FullRing = EndAngle - StartAngle >= 359.999f;
		Geometry.m_Feather = std::min(FEATHER_PIXELS * PixelSize, std::min(OuterRadius - InnerRadius, InnerRadius) * 0.25f);
		if(!Geometry.m_FullRing)
		{
			// 密集改名扇区仍保留间隙，羽化不能跨到相邻选项。
			const float NarrowAngle = std::min(Sweep, std::max(0.0f, GapDegrees) * pi / 180.0f);
			Geometry.m_Feather = std::min(Geometry.m_Feather, InnerRadius * std::sin(NarrowAngle * 0.5f) * 0.45f);
		}

		for(int Index = 0; Index <= Geometry.m_ArcSegments; ++Index)
		{
			const int InnerIndex = Geometry.m_PointCount - 1 - Index;
			if(Geometry.m_FullRing && Index == Geometry.m_ArcSegments)
			{
				// 完整圆环的首尾复用精确坐标，不为闭合处生成径向羽化带。
				Geometry.m_aPoints[Index].m_Position = Geometry.m_aPoints[0].m_Position;
				Geometry.m_aPoints[InnerIndex].m_Position = Geometry.m_aPoints[Geometry.m_PointCount - 1].m_Position;
				continue;
			}
			const float Angle = StartAngle * pi / 180.0f + Sweep * Index / Geometry.m_ArcSegments;
			const vec2 Direction(std::cos(Angle), std::sin(Angle));
			Geometry.m_aPoints[Index].m_Position = Direction * OuterRadius;
			Geometry.m_aPoints[InnerIndex].m_Position = Direction * InnerRadius;
		}
		BuildFringe(Geometry);
		return Geometry;
	}

	inline SGeometry BuildDisc(float Radius, float PixelSize)
	{
		SGeometry Geometry;
		if(Radius <= 0.0f || PixelSize <= 0.0f)
			return Geometry;
		Geometry.m_ArcSegments = ArcSegments(Radius, 2.0f * pi, PixelSize);
		Geometry.m_PointCount = Geometry.m_ArcSegments;
		Geometry.m_Feather = std::min(FEATHER_PIXELS * PixelSize, Radius * 0.25f);
		Geometry.m_Disc = true;
		for(int Index = 0; Index < Geometry.m_PointCount; ++Index)
		{
			const float Angle = 2.0f * pi * Index / Geometry.m_PointCount;
			Geometry.m_aPoints[Index].m_Position = vec2(std::cos(Angle), std::sin(Angle)) * Radius;
		}
		BuildFringe(Geometry);
		return Geometry;
	}
}

#endif
