#ifndef GAME_EDITOR_QUAD_SLICE_H
#define GAME_EDITOR_QUAD_SLICE_H

#include <game/mapitems.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace quad_slice
{
	inline double TriangleArea(vec2 A, vec2 B, vec2 C)
	{
		return std::abs((double(B.x) - A.x) * (double(C.y) - A.y) - (double(C.x) - A.x) * (double(B.y) - A.y)) * 0.5;
	}

	inline bool InTriangle(vec2 Point, vec2 A, vec2 B, vec2 C)
	{
		const double Area = TriangleArea(A, B, C);
		return Area > 0.0 && std::abs(TriangleArea(Point, B, C) + TriangleArea(Point, C, A) + TriangleArea(Point, A, B) - Area) <= Area * 0.000001;
	}

	inline std::array<vec2, 4> Rectangle(vec2 Start, vec2 End)
	{
		const float Left = std::min(Start.x, End.x), Right = std::max(Start.x, End.x);
		const float Top = std::min(Start.y, End.y), Bottom = std::max(Start.y, End.y);
		return {vec2(Left, Top), vec2(Right, Top), vec2(Left, Bottom), vec2(Right, Bottom)};
	}

	inline bool TriangleInterval(vec2 Start, vec2 End, vec2 A, vec2 B, vec2 C, double &Low, double &High)
	{
		const auto Cross = [](vec2 First, vec2 Second, vec2 Point) {
			return (double(Second.x) - First.x) * (double(Point.y) - First.y) - (double(Second.y) - First.y) * (double(Point.x) - First.x);
		};
		const double Area = Cross(A, B, C);
		if(Area == 0.0)
			return false;
		Low = 0.0;
		High = 1.0;
		const vec2 aVertices[] = {A, B, C};
		for(int i = 0; i < 3; ++i)
		{
			const double First = Cross(aVertices[i], aVertices[(i + 1) % 3], Start) / Area;
			const double Last = Cross(aVertices[i], aVertices[(i + 1) % 3], End) / Area;
			if(First < -0.000001 && Last < -0.000001)
				return false;
			if(Last > First)
				Low = std::max(Low, (-0.000001 - First) / (Last - First));
			else if(Last < First)
				High = std::min(High, (-0.000001 - First) / (Last - First));
		}
		return Low <= High;
	}

	inline bool EdgeInside(vec2 Start, vec2 End, const std::array<vec2, 4> &Vertices)
	{
		double LowA = 0, HighA = 0, LowB = 0, HighB = 0;
		const bool First = TriangleInterval(Start, End, Vertices[0], Vertices[3], Vertices[1], LowA, HighA);
		const bool Second = TriangleInterval(Start, End, Vertices[0], Vertices[3], Vertices[2], LowB, HighB);
		if(!First)
			return Second && LowB <= 0 && HighB >= 1;
		if(!Second)
			return LowA <= 0 && HighA >= 1;
		if(LowA > LowB)
		{
			std::swap(LowA, LowB);
			std::swap(HighA, HighB);
		}
		return LowA <= 0 && HighA >= LowB && std::max(HighA, HighB) >= 1;
	}

	inline bool SliceRectangle(const CQuad &Source, vec2 Start, vec2 End, CQuad &Result)
	{
		const auto Points = Rectangle(Start, End);
		if(f2fx(Points[0].x) == f2fx(Points[3].x) || f2fx(Points[0].y) == f2fx(Points[3].y))
			return false;
		std::array<vec2, 4> Vertices;
		for(int i = 0; i < 4; ++i)
			Vertices[i] = vec2(fx2f(Source.m_aPoints[i].x), fx2f(Source.m_aPoints[i].y));
		// 凹形 Quad 的四个角都在内部，也可能有选框边线穿过缺口。
		const int aOrder[] = {0, 1, 3, 2};
		for(int i = 0; i < 4; ++i)
			if(!EdgeInside(Points[aOrder[i]], Points[aOrder[(i + 1) % 4]], Vertices))
				return false;
		CQuad Cropped = Source;
		for(int i = 0; i < 4; ++i)
		{
			const int Third = InTriangle(Points[i], Vertices[0], Vertices[3], Vertices[2]) ? 2 : 1;
			if(!InTriangle(Points[i], Vertices[0], Vertices[3], Vertices[Third]))
				return false;
			const double Area = TriangleArea(Vertices[0], Vertices[3], Vertices[Third]);
			const double A = TriangleArea(Points[i], Vertices[3], Vertices[Third]) / Area;
			const double B = TriangleArea(Points[i], Vertices[Third], Vertices[0]) / Area;
			const double C = TriangleArea(Points[i], Vertices[0], Vertices[3]) / Area;
			const auto Interpolate = [&](int First, int Second, int Last) { return static_cast<int>(std::round(First * A + Second * B + Last * C)); };
			Cropped.m_aColors[i] = {
				Interpolate(Source.m_aColors[0].r, Source.m_aColors[3].r, Source.m_aColors[Third].r),
				Interpolate(Source.m_aColors[0].g, Source.m_aColors[3].g, Source.m_aColors[Third].g),
				Interpolate(Source.m_aColors[0].b, Source.m_aColors[3].b, Source.m_aColors[Third].b),
				Interpolate(Source.m_aColors[0].a, Source.m_aColors[3].a, Source.m_aColors[Third].a)};
			Cropped.m_aTexcoords[i] = {
				Interpolate(Source.m_aTexcoords[0].x, Source.m_aTexcoords[3].x, Source.m_aTexcoords[Third].x),
				Interpolate(Source.m_aTexcoords[0].y, Source.m_aTexcoords[3].y, Source.m_aTexcoords[Third].y)};
			Cropped.m_aPoints[i] = {f2fx(Points[i].x), f2fx(Points[i].y)};
		}
		// 保留动画绑定和原旋转中心，裁剪后的动画继续沿原 Quad 运动。
		Result = Cropped;
		return true;
	}
}

#endif
