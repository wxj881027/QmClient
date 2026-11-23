#ifndef ENGINE_CLIENT_QM_GLYPH_GEOMETRY_H
#define ENGINE_CLIENT_QM_GLYPH_GEOMETRY_H

#include "glyph_outline.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

// GPU 字形实验路径的轮廓几何。这里只负责把 FreeType 轮廓展平、按 even-odd
// 嵌套深度拆成独立简单多边形并三角化；真正的覆盖度光栅化由 GPU 离屏目标完成。
// 可见文本仍走同步 CPU 位图路径，GPU 结果只服务不可见预热。
struct SQmGlyphPoint
{
	float m_X = 0.0f;
	float m_Y = 0.0f;
};

struct SQmGlyphTriangle
{
	SQmGlyphPoint m_aPoints[3];
};

struct SQmGlyphContour
{
	int m_Depth = 0;
	// 非零环绕规则下用于 GPU 双通道累加的轮廓方向。
	int m_WindingSign = 0;
	std::vector<SQmGlyphTriangle> m_vTriangles;
};

enum class EQmGlyphFillRule
{
	EVEN_ODD,
	NON_ZERO,
};

struct SQmGlyphGeometry
{
	int m_Width = 0;
	int m_Height = 0;
	int m_CharWidth = 0;
	int m_CharHeight = 0;
	int m_OffsetX = 0;
	int m_OffsetY = 0;
	float m_AdvanceX = 0.0f;
	float m_LayoutWidth = 0.0f;
	float m_LayoutBearingX = 0.0f;
	EQmGlyphFillRule m_FillRule = EQmGlyphFillRule::NON_ZERO;
	std::vector<SQmGlyphContour> m_vContours;
};

inline int QmAdjustOutlineThicknessToFontSize(int OutlineThickness, int FontSize)
{
	if(FontSize > 48)
		OutlineThickness *= 4;
	else if(FontSize >= 18)
		OutlineThickness *= 2;
	return OutlineThickness;
}

namespace qm_glyph_geometry_detail
{
	inline thread_local int g_LastFailureStage = 0;
	inline thread_local size_t g_DebugInputPoints = 0;
	inline thread_local size_t g_DebugCleanedPoints = 0;
	inline thread_local size_t g_DebugEarFailPoints = 0;
	inline thread_local double g_DebugSignedArea = 0.0;
	inline thread_local SQmGlyphPoint g_DebugFirstPoint{};
	inline thread_local SQmGlyphPoint g_aDebugEarFailPoints[4]{};
	inline thread_local float g_aDebugEarFailCross[4]{};
	inline thread_local bool g_aDebugEarFailContains[4]{};
	inline thread_local SQmGlyphPoint g_aDebugRemaining[32]{};
	inline thread_local size_t g_DebugRemainingCount = 0;
	inline constexpr int MAX_CONTOUR_POINTS = 1024;
	inline constexpr int MAX_TRIANGLES_PER_CONTOUR = 2048;
	inline constexpr float FLATTEN_TOLERANCE = 0.15f;
	inline constexpr float POINT_EPSILON = 0.0001f;
	inline constexpr float INTERIOR_SAMPLE_OFFSET = 0.01f;

	struct SFlatContour
	{
		std::vector<SQmGlyphPoint> m_vPoints;
	};

	struct SFlattenContext
	{
		std::vector<SFlatContour> *m_pContours = nullptr;
		SFlatContour m_Current;
		float m_MinXPixel = 0.0f;
		float m_MaxYPixel = 0.0f;
		float m_Padding = 0.0f;
		bool m_Failed = false;
		FT_Vector m_LastPoint{};
		bool m_HasLastPoint = false;
	};

	inline float Cross(const SQmGlyphPoint &A, const SQmGlyphPoint &B, const SQmGlyphPoint &C)
	{
		return (B.m_X - A.m_X) * (C.m_Y - A.m_Y) - (B.m_Y - A.m_Y) * (C.m_X - A.m_X);
	}

	inline bool SamePoint(const SQmGlyphPoint &A, const SQmGlyphPoint &B)
	{
		return std::abs(A.m_X - B.m_X) <= POINT_EPSILON && std::abs(A.m_Y - B.m_Y) <= POINT_EPSILON;
	}

	inline int FloorPixel(FT_Pos Value)
	{
		return static_cast<int>(Value >> 6);
	}

	inline int CeilPixel(FT_Pos Value)
	{
		return static_cast<int>((Value + 63) >> 6);
	}

	inline SQmGlyphPoint TransformPoint(const SFlattenContext &Context, const FT_Vector &Point)
	{
		return {
			Point.x / 64.0f - Context.m_MinXPixel + Context.m_Padding,
			Context.m_MaxYPixel - Point.y / 64.0f + Context.m_Padding,
		};
	}

	inline void FinalizeContour(SFlattenContext &Context)
	{
		if(Context.m_Current.m_vPoints.size() >= 2 && SamePoint(Context.m_Current.m_vPoints.front(), Context.m_Current.m_vPoints.back()))
			Context.m_Current.m_vPoints.pop_back();
		if(Context.m_Current.m_vPoints.size() >= 3)
		{
			if(Context.m_pContours->size() >= 256 || Context.m_Current.m_vPoints.size() > static_cast<size_t>(MAX_CONTOUR_POINTS))
			{
				Context.m_Failed = true;
				return;
			}
			Context.m_pContours->push_back(std::move(Context.m_Current));
		}
		Context.m_Current = {};
		Context.m_Current.m_vPoints.reserve(32);
	}

	inline int SubdivisionSteps(const FT_Vector &P0, const FT_Vector &P1, const FT_Vector &P2)
	{
		const double Dx = static_cast<double>(P0.x) - 2.0 * static_cast<double>(P1.x) + static_cast<double>(P2.x);
		const double Dy = static_cast<double>(P0.y) - 2.0 * static_cast<double>(P1.y) + static_cast<double>(P2.y);
		const double PixelDistance = std::sqrt(Dx * Dx + Dy * Dy) / 64.0;
		const double Steps = std::ceil(std::sqrt(PixelDistance / FLATTEN_TOLERANCE));
		return std::clamp(static_cast<int>(std::isfinite(Steps) ? Steps : 1.0), 1, 24);
	}

	inline int SubdivisionStepsCubic(const FT_Vector &P0, const FT_Vector &P1, const FT_Vector &P2, const FT_Vector &P3)
	{
		const double D1x = static_cast<double>(P0.x) - 2.0 * static_cast<double>(P1.x) + static_cast<double>(P2.x);
		const double D1y = static_cast<double>(P0.y) - 2.0 * static_cast<double>(P1.y) + static_cast<double>(P2.y);
		const double D2x = static_cast<double>(P1.x) - 2.0 * static_cast<double>(P2.x) + static_cast<double>(P3.x);
		const double D2y = static_cast<double>(P1.y) - 2.0 * static_cast<double>(P2.y) + static_cast<double>(P3.y);
		const double PixelDistance = std::max(std::sqrt(D1x * D1x + D1y * D1y), std::sqrt(D2x * D2x + D2y * D2y)) / 64.0;
		const double Steps = std::ceil(std::sqrt(PixelDistance / FLATTEN_TOLERANCE));
		return std::clamp(static_cast<int>(std::isfinite(Steps) ? Steps : 1.0), 1, 32);
	}

	inline int MoveTo(const FT_Vector *pTo, void *pUser)
	{
		auto *pContext = static_cast<SFlattenContext *>(pUser);
		if(pTo == nullptr || pContext == nullptr)
			return 1;
		FinalizeContour(*pContext);
		if(pContext->m_Failed)
			return 1;
		pContext->m_Current.m_vPoints.push_back(TransformPoint(*pContext, *pTo));
		pContext->m_LastPoint = *pTo;
		pContext->m_HasLastPoint = true;
		return 0;
	}

	inline int LineTo(const FT_Vector *pTo, void *pUser)
	{
		auto *pContext = static_cast<SFlattenContext *>(pUser);
		if(pTo == nullptr || pContext == nullptr || !pContext->m_HasLastPoint)
			return 1;
		pContext->m_Current.m_vPoints.push_back(TransformPoint(*pContext, *pTo));
		pContext->m_LastPoint = *pTo;
		return pContext->m_Current.m_vPoints.size() <= static_cast<size_t>(MAX_CONTOUR_POINTS) ? 0 : 1;
	}

	inline int ConicTo(const FT_Vector *pControl, const FT_Vector *pTo, void *pUser)
	{
		auto *pContext = static_cast<SFlattenContext *>(pUser);
		if(pControl == nullptr || pTo == nullptr || pContext == nullptr || !pContext->m_HasLastPoint)
			return 1;
		const FT_Vector P0 = pContext->m_LastPoint;
		const int Steps = SubdivisionSteps(P0, *pControl, *pTo);
		for(int i = 1; i <= Steps; ++i)
		{
			const float T = static_cast<float>(i) / static_cast<float>(Steps);
			const float InvT = 1.0f - T;
			FT_Vector Point{};
			Point.x = static_cast<FT_Pos>(std::lround(InvT * InvT * P0.x + 2.0f * InvT * T * pControl->x + T * T * pTo->x));
			Point.y = static_cast<FT_Pos>(std::lround(InvT * InvT * P0.y + 2.0f * InvT * T * pControl->y + T * T * pTo->y));
			pContext->m_Current.m_vPoints.push_back(TransformPoint(*pContext, Point));
		}
		pContext->m_LastPoint = *pTo;
		return pContext->m_Current.m_vPoints.size() <= static_cast<size_t>(MAX_CONTOUR_POINTS) ? 0 : 1;
	}

	inline int CubicTo(const FT_Vector *pControl1, const FT_Vector *pControl2, const FT_Vector *pTo, void *pUser)
	{
		auto *pContext = static_cast<SFlattenContext *>(pUser);
		if(pControl1 == nullptr || pControl2 == nullptr || pTo == nullptr || pContext == nullptr || !pContext->m_HasLastPoint)
			return 1;
		const FT_Vector P0 = pContext->m_LastPoint;
		const int Steps = SubdivisionStepsCubic(P0, *pControl1, *pControl2, *pTo);
		for(int i = 1; i <= Steps; ++i)
		{
			const float T = static_cast<float>(i) / static_cast<float>(Steps);
			const float InvT = 1.0f - T;
			const float B0 = InvT * InvT * InvT;
			const float B1 = 3.0f * InvT * InvT * T;
			const float B2 = 3.0f * InvT * T * T;
			const float B3 = T * T * T;
			FT_Vector Point{};
			Point.x = static_cast<FT_Pos>(std::lround(B0 * P0.x + B1 * pControl1->x + B2 * pControl2->x + B3 * pTo->x));
			Point.y = static_cast<FT_Pos>(std::lround(B0 * P0.y + B1 * pControl1->y + B2 * pControl2->y + B3 * pTo->y));
			pContext->m_Current.m_vPoints.push_back(TransformPoint(*pContext, Point));
		}
		pContext->m_LastPoint = *pTo;
		return pContext->m_Current.m_vPoints.size() <= static_cast<size_t>(MAX_CONTOUR_POINTS) ? 0 : 1;
	}

	inline double SignedArea(const std::vector<SQmGlyphPoint> &vPoints)
	{
		double Area = 0.0;
		for(size_t i = 0; i < vPoints.size(); ++i)
		{
			const SQmGlyphPoint &A = vPoints[i];
			const SQmGlyphPoint &B = vPoints[(i + 1) % vPoints.size()];
			Area += static_cast<double>(A.m_X) * B.m_Y - static_cast<double>(B.m_X) * A.m_Y;
		}
		return Area * 0.5;
	}

	inline void RemoveRedundantPoints(std::vector<SQmGlyphPoint> &vPoints)
	{
		if(vPoints.empty())
			return;
		std::vector<SQmGlyphPoint> vFiltered;
		vFiltered.reserve(vPoints.size());
		for(const SQmGlyphPoint &Point : vPoints)
		{
			if(vFiltered.empty() || !SamePoint(vFiltered.back(), Point))
				vFiltered.push_back(Point);
		}
		if(vFiltered.size() >= 2 && SamePoint(vFiltered.front(), vFiltered.back()))
			vFiltered.pop_back();
		bool Changed = true;
		while(Changed && vFiltered.size() >= 3)
		{
			Changed = false;
			for(size_t i = 0; i < vFiltered.size() && vFiltered.size() >= 3; ++i)
			{
				const size_t Previous = (i + vFiltered.size() - 1) % vFiltered.size();
				const size_t Next = (i + 1) % vFiltered.size();
				if(std::abs(Cross(vFiltered[Previous], vFiltered[i], vFiltered[Next])) <= POINT_EPSILON)
				{
					vFiltered.erase(vFiltered.begin() + i);
					Changed = true;
					break;
				}
			}
		}
		vPoints = std::move(vFiltered);
	}

	inline void SplitRepeatedContour(const std::vector<SQmGlyphPoint> &vInput, std::vector<std::vector<SQmGlyphPoint>> &vOutput, int Recursion)
	{
		if(vInput.size() < 3 || Recursion >= 16)
		{
			if(vInput.size() >= 3)
				vOutput.push_back(vInput);
			return;
		}
		for(size_t i = 0; i < vInput.size(); ++i)
		{
			for(size_t j = i + 2; j < vInput.size(); ++j)
			{
				if(i == 0 && j + 1 == vInput.size())
					continue;
				if(!SamePoint(vInput[i], vInput[j]))
					continue;
				std::vector<SQmGlyphPoint> vFirst(vInput.begin() + i, vInput.begin() + j + 1);
				std::vector<SQmGlyphPoint> vSecond;
				vSecond.reserve(i + 1 + vInput.size() - j);
				vSecond.insert(vSecond.end(), vInput.begin(), vInput.begin() + i + 1);
				vSecond.insert(vSecond.end(), vInput.begin() + j + 1, vInput.end());
				SplitRepeatedContour(vFirst, vOutput, Recursion + 1);
				SplitRepeatedContour(vSecond, vOutput, Recursion + 1);
				return;
			}
		}
		vOutput.push_back(vInput);
	}

	inline bool PointInTriangle(const SQmGlyphPoint &Point, const SQmGlyphPoint &A, const SQmGlyphPoint &B, const SQmGlyphPoint &C)
	{
		const float D1 = Cross(A, B, Point);
		const float D2 = Cross(B, C, Point);
		const float D3 = Cross(C, A, Point);
		const bool HasNegative = D1 < -POINT_EPSILON || D2 < -POINT_EPSILON || D3 < -POINT_EPSILON;
		const bool HasPositive = D1 > POINT_EPSILON || D2 > POINT_EPSILON || D3 > POINT_EPSILON;
		return HasNegative != HasPositive;
	}

	inline bool TriangulateContour(const std::vector<SQmGlyphPoint> &vInput, std::vector<SQmGlyphTriangle> &vTriangles)
	{
		vTriangles.clear();
		std::vector<SQmGlyphPoint> vPoints = vInput;
		g_DebugInputPoints = vInput.size();
		if(vPoints.size() < 3)
			return false;
		g_DebugSignedArea = SignedArea(vPoints);
		if(g_DebugSignedArea < 0.0)
			std::reverse(vPoints.begin(), vPoints.end());
		g_DebugCleanedPoints = vPoints.size();
		g_DebugFirstPoint = vPoints.front();
		std::vector<size_t> vIndices(vPoints.size());
		for(size_t i = 0; i < vIndices.size(); ++i)
			vIndices[i] = i;
		size_t Guard = 0;
		const size_t MaxGuard = vIndices.size() * vIndices.size() + 8;
		while(vIndices.size() > 3)
		{
			if(++Guard > MaxGuard)
				return false;
			// 耳切会不断产生新的共线三点；先清掉它们，否则边界点会被误判
			// 成“三角形内部点”，把后续所有耳朵都堵死。
			bool RemovedCollinear = true;
			while(RemovedCollinear && vIndices.size() > 3)
			{
				RemovedCollinear = false;
				for(size_t i = 0; i < vIndices.size(); ++i)
				{
					const SQmGlyphPoint &A = vPoints[vIndices[(i + vIndices.size() - 1) % vIndices.size()]];
					const SQmGlyphPoint &B = vPoints[vIndices[i]];
					const SQmGlyphPoint &C = vPoints[vIndices[(i + 1) % vIndices.size()]];
					if(std::abs(Cross(A, B, C)) <= POINT_EPSILON)
					{
						vIndices.erase(vIndices.begin() + i);
						RemovedCollinear = true;
						break;
					}
				}
			}
			if(vIndices.size() <= 3)
				break;
			bool FoundEar = false;
			for(size_t i = 0; i < vIndices.size(); ++i)
			{
				const size_t Previous = vIndices[(i + vIndices.size() - 1) % vIndices.size()];
				const size_t Current = vIndices[i];
				const size_t Next = vIndices[(i + 1) % vIndices.size()];
				const SQmGlyphPoint &A = vPoints[Previous];
				const SQmGlyphPoint &B = vPoints[Current];
				const SQmGlyphPoint &C = vPoints[Next];
				if(Cross(A, B, C) <= POINT_EPSILON)
					continue;
				bool ContainsPoint = false;
				for(size_t Index : vIndices)
				{
					if(Index == Previous || Index == Current || Index == Next)
						continue;
					if(PointInTriangle(vPoints[Index], A, B, C))
					{
						ContainsPoint = true;
						break;
					}
				}
				if(ContainsPoint)
					continue;
				vTriangles.push_back({{A, B, C}});
				if(vTriangles.size() > static_cast<size_t>(MAX_TRIANGLES_PER_CONTOUR))
					return false;
				vIndices.erase(vIndices.begin() + i);
				FoundEar = true;
				break;
			}
			if(!FoundEar)
			{
				double RemainingArea = 0.0;
				for(size_t i = 0; i < vIndices.size(); ++i)
				{
					const SQmGlyphPoint &A = vPoints[vIndices[i]];
					const SQmGlyphPoint &B = vPoints[vIndices[(i + 1) % vIndices.size()]];
					RemainingArea += static_cast<double>(A.m_X) * B.m_Y - static_cast<double>(B.m_X) * A.m_Y;
				}
				// 耳切到末尾时可能只剩一条由共线点组成的零面积边；这不代表
				// 轮廓有洞或失败，已经生成的三角形已覆盖全部可见面积。
				if(std::abs(RemainingArea) <= POINT_EPSILON)
				{
					vIndices.clear();
					break;
				}
				g_DebugEarFailPoints = vIndices.size();
				g_DebugRemainingCount = std::min(vIndices.size(), static_cast<size_t>(32));
				for(size_t i = 0; i < g_DebugRemainingCount; ++i)
					g_aDebugRemaining[i] = vPoints[vIndices[i]];
				if(vIndices.size() == 4)
				{
					for(size_t i = 0; i < vIndices.size(); ++i)
					{
						const size_t Previous = vIndices[(i + vIndices.size() - 1) % vIndices.size()];
						const size_t Current = vIndices[i];
						const size_t Next = vIndices[(i + 1) % vIndices.size()];
						const SQmGlyphPoint &A = vPoints[Previous];
						const SQmGlyphPoint &B = vPoints[Current];
						const SQmGlyphPoint &C = vPoints[Next];
						g_aDebugEarFailPoints[i] = B;
						g_aDebugEarFailCross[i] = Cross(A, B, C);
						g_aDebugEarFailContains[i] = false;
						for(size_t Index : vIndices)
						{
							if(Index == Previous || Index == Current || Index == Next)
								continue;
							g_aDebugEarFailContains[i] |= PointInTriangle(vPoints[Index], A, B, C);
						}
					}
				}
				return false;
			}
		}
		if(vIndices.size() == 3)
			vTriangles.push_back({{vPoints[vIndices[0]], vPoints[vIndices[1]], vPoints[vIndices[2]]}});
		return !vTriangles.empty();
	}

	// 1 内部，0 外部，-1 落在边界。边界情况不参与嵌套深度计数，避免两个
	// 共边轮廓互相把对方算成孔洞。
	inline int PointInPolygon(const SQmGlyphPoint &Point, const std::vector<SQmGlyphPoint> &vPolygon)
	{
		bool Inside = false;
		for(size_t i = 0, j = vPolygon.size() - 1; i < vPolygon.size(); j = i++)
		{
			const SQmGlyphPoint &A = vPolygon[j];
			const SQmGlyphPoint &B = vPolygon[i];
			const float EdgeCross = Cross(A, B, Point);
			const float MinX = std::min(A.m_X, B.m_X) - POINT_EPSILON;
			const float MaxX = std::max(A.m_X, B.m_X) + POINT_EPSILON;
			const float MinY = std::min(A.m_Y, B.m_Y) - POINT_EPSILON;
			const float MaxY = std::max(A.m_Y, B.m_Y) + POINT_EPSILON;
			if(std::abs(EdgeCross) <= POINT_EPSILON && Point.m_X >= MinX && Point.m_X <= MaxX && Point.m_Y >= MinY && Point.m_Y <= MaxY)
				return -1;
			if(((A.m_Y > Point.m_Y) != (B.m_Y > Point.m_Y)) &&
				(Point.m_X < (B.m_X - A.m_X) * (Point.m_Y - A.m_Y) / (B.m_Y - A.m_Y) + A.m_X))
				Inside = !Inside;
		}
		return Inside ? 1 : 0;
	}

	// 轮廓的“代表点”必须靠近自身边界。直接取三角化首个三角形的重心时，
	// 外轮廓可能恰好选中覆盖孔洞的三角形，重心落进孔洞，从而把外轮廓误判成
	// 嵌套轮廓。沿第一条可用边的内法线取一个极小偏移点，能稳定落在当前轮廓
	// 的内部且不跨入其他轮廓。
	inline bool InteriorPointNearBoundary(const std::vector<SQmGlyphPoint> &vPolygon, SQmGlyphPoint &Point)
	{
		if(vPolygon.size() < 3)
			return false;
		const double Area = SignedArea(vPolygon);
		if(std::abs(Area) <= POINT_EPSILON)
			return false;
		for(size_t i = 0; i < vPolygon.size(); ++i)
		{
			const SQmGlyphPoint &A = vPolygon[i];
			const SQmGlyphPoint &B = vPolygon[(i + 1) % vPolygon.size()];
			const float Dx = B.m_X - A.m_X;
			const float Dy = B.m_Y - A.m_Y;
			const float LengthSquared = Dx * Dx + Dy * Dy;
			if(LengthSquared <= POINT_EPSILON * POINT_EPSILON)
				continue;
			const float Length = std::sqrt(LengthSquared);
			const float Direction = Area > 0.0 ? 1.0f : -1.0f;
			const SQmGlyphPoint Candidate = {
				(A.m_X + B.m_X) * 0.5f - Dy * Direction * (INTERIOR_SAMPLE_OFFSET / Length),
				(A.m_Y + B.m_Y) * 0.5f + Dx * Direction * (INTERIOR_SAMPLE_OFFSET / Length),
			};
			if(PointInPolygon(Candidate, vPolygon) > 0)
			{
				Point = Candidate;
				return true;
			}
		}
		return false;
	}

	inline bool BuildFromOutline(FT_Outline &Outline, const FT_Glyph_Metrics &Metrics, int FontSize, bool Nameplate,
		bool EvenOddFill, SQmGlyphGeometry &Geometry)
	{
		g_LastFailureStage = 0;
		Geometry = {};
		FT_BBox CBox;
		FT_Outline_Get_CBox(&Outline, &CBox);
		const int MinX = FloorPixel(CBox.xMin);
		const int MinY = FloorPixel(CBox.yMin);
		const int MaxX = CeilPixel(CBox.xMax);
		const int MaxY = CeilPixel(CBox.yMax);
		const int RealWidth = std::max(0, MaxX - MinX);
		const int RealHeight = std::max(0, MaxY - MinY);
		float OutlineRadius = 0.0f;
		int Padding = 0;
		if(RealWidth > 0)
		{
			OutlineRadius = Nameplate ? QmNameplateGlyphOutlineRadius(FontSize) : static_cast<float>(QmAdjustOutlineThicknessToFontSize(1, FontSize));
			Padding = static_cast<int>(std::ceil(OutlineRadius)) + 1;
		}
		if(RealWidth > std::numeric_limits<int>::max() - Padding * 2 ||
			RealHeight > std::numeric_limits<int>::max() - Padding * 2)
			return false;

		Geometry.m_Width = RealWidth + Padding * 2;
		Geometry.m_Height = RealHeight + Padding * 2;
		Geometry.m_CharWidth = RealWidth;
		Geometry.m_CharHeight = RealHeight;
		Geometry.m_OffsetX = Nameplate ? 0 : static_cast<int>(Metrics.horiBearingX >> 6);
		Geometry.m_OffsetY = Nameplate ? 0 : -static_cast<int>((Metrics.height >> 6) - (Metrics.horiBearingY >> 6));
		Geometry.m_AdvanceX = static_cast<float>(Metrics.horiAdvance) / 64.0f;
		Geometry.m_LayoutWidth = Metrics.width / 64.0f;
		Geometry.m_LayoutBearingX = Metrics.horiBearingX / 64.0f;
		Geometry.m_FillRule = EvenOddFill ? EQmGlyphFillRule::EVEN_ODD : EQmGlyphFillRule::NON_ZERO;
		if(RealWidth <= 0 || RealHeight <= 0)
			return true;

		SFlattenContext Context;
		std::vector<SFlatContour> vContours;
		vContours.reserve(16);
		Context.m_pContours = &vContours;
		Context.m_MinXPixel = static_cast<float>(MinX);
		Context.m_MaxYPixel = static_cast<float>(MaxY);
		Context.m_Padding = static_cast<float>(Padding);
		Context.m_Current.m_vPoints.reserve(32);

		FT_Outline_Funcs Functions{};
		Functions.move_to = MoveTo;
		Functions.line_to = LineTo;
		Functions.conic_to = ConicTo;
		Functions.cubic_to = CubicTo;
		Functions.shift = 0;
		Functions.delta = 0;
		if(FT_Outline_Decompose(&Outline, &Functions, &Context) != 0)
		{
			g_LastFailureStage = 1;
			return false;
		}
		FinalizeContour(Context);
		if(Context.m_Failed || vContours.empty())
		{
			g_LastFailureStage = 2;
			return false;
		}

		std::vector<SQmGlyphContour> vBuilt;
		std::vector<std::vector<SQmGlyphPoint>> vPolygons;
		vBuilt.reserve(vContours.size());
		vPolygons.reserve(vContours.size());
		for(SFlatContour &Flat : vContours)
		{
			std::vector<std::vector<SQmGlyphPoint>> vLoops;
			SplitRepeatedContour(Flat.m_vPoints, vLoops, 0);
			for(std::vector<SQmGlyphPoint> &vLoop : vLoops)
			{
				RemoveRedundantPoints(vLoop);
				SQmGlyphContour Contour;
				if(vLoop.size() < 3 || !TriangulateContour(vLoop, Contour.m_vTriangles))
				{
					g_LastFailureStage = 3;
					return false;
				}
				Contour.m_WindingSign = SignedArea(vLoop) > 0.0 ? 1 : -1;
				vPolygons.push_back(std::move(vLoop));
				vBuilt.push_back(std::move(Contour));
			}
		}

		// 每个轮廓取靠近自身边界的内部点作为嵌套判定点；嵌套深度决定偶奇
		// 填充规则下这个轮廓是填充还是孔洞。
		for(size_t i = 0; i < vBuilt.size(); ++i)
		{
			SQmGlyphPoint Center;
			if(!InteriorPointNearBoundary(vPolygons[i], Center))
				return false;
			int Depth = 0;
			for(size_t j = 0; j < vPolygons.size(); ++j)
			{
				if(i == j)
					continue;
				if(PointInPolygon(Center, vPolygons[j]) > 0)
					++Depth;
			}
			vBuilt[i].m_Depth = Depth;
		}
		std::sort(vBuilt.begin(), vBuilt.end(), [](const SQmGlyphContour &A, const SQmGlyphContour &B) {
			return A.m_Depth < B.m_Depth;
		});
		Geometry.m_vContours = std::move(vBuilt);
		return true;
	}
} // namespace qm_glyph_geometry_detail

// 调用前 face 已设置到目标字号，并已用与 CPU 路径一致的 flags 载入 glyph。
inline bool QmBuildGlyphGeometry(FT_Outline &Outline, const FT_Glyph_Metrics &Metrics, int FontSize, bool Nameplate,
	bool EvenOddFill, SQmGlyphGeometry &Geometry)
{
	return qm_glyph_geometry_detail::BuildFromOutline(Outline, Metrics, FontSize, Nameplate, EvenOddFill, Geometry);
}

// 偶奇规则使用白底黑填充，红色通道为 0 表示覆盖；非零环绕规则把正/负
// 环绕分别累加进红/绿通道，采样在 R != G 时算覆盖。这里把超采样 cell
// 降采样成 8 位 alpha 填充掩码；越界或尺寸异常返回 false。
inline bool QmDownsampleGpuGlyphCoverage(const uint8_t *pRgba, int ImageWidth, int ImageHeight,
	int X, int Y, int CellWidth, int CellHeight, int Scale, EQmGlyphFillRule FillRule, uint8_t *pFill)
{
	if(pRgba == nullptr || pFill == nullptr || ImageWidth <= 0 || ImageHeight <= 0 || CellWidth <= 0 || CellHeight <= 0 || Scale <= 0)
		return false;
	if(X < 0 || Y < 0 || CellWidth > std::numeric_limits<int>::max() / Scale || CellHeight > std::numeric_limits<int>::max() / Scale)
		return false;
	const int SourceWidth = CellWidth * Scale;
	const int SourceHeight = CellHeight * Scale;
	if(X > ImageWidth - SourceWidth || Y > ImageHeight - SourceHeight)
		return false;
	const int SampleCount = Scale * Scale;
	for(int y = 0; y < CellHeight; ++y)
	{
		for(int x = 0; x < CellWidth; ++x)
		{
			int CoveredSamples = 0;
			for(int sy = 0; sy < Scale; ++sy)
			{
				const size_t RowOffset = static_cast<size_t>(Y + y * Scale + sy) * static_cast<size_t>(ImageWidth);
				for(int sx = 0; sx < Scale; ++sx)
				{
					const size_t Offset = (RowOffset + static_cast<size_t>(X + x * Scale + sx)) * 4;
					if(FillRule == EQmGlyphFillRule::NON_ZERO)
						CoveredSamples += pRgba[Offset] != pRgba[Offset + 1] ? 1 : 0;
					else
						CoveredSamples += pRgba[Offset] < 128 ? 1 : 0;
				}
			}
			pFill[static_cast<size_t>(y) * CellWidth + x] = static_cast<uint8_t>((CoveredSamples * 255 + SampleCount / 2) / SampleCount);
		}
	}
	return true;
}

#endif
