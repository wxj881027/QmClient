#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_TRAIL_BAND_GEOMETRY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_TRAIL_BAND_GEOMETRY_H

#include <base/vmath.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

// 生成端从透明窄口过渡到正常截面；只修改头部，保留尾端包络和独立装饰。
template<typename TBandPoint>
void QmPrepareTrailBandHead(TBandPoint *pBand, size_t Count)
{
	if(Count < 2)
		return;
	const float HeadLength = std::clamp(std::max(pBand[0].m_Left, pBand[0].m_Right) * 1.5f, 8.0f, 48.0f);
	float Distance = 0.0f;
	for(size_t i = 0; i < Count; ++i)
	{
		if(i > 0)
			Distance += distance(pBand[i - 1].m_Pos, pBand[i].m_Pos);
		if(Distance >= HeadLength)
			break;
		const float T = Distance / HeadLength;
		const float Gain = T * T * (3.0f - 2.0f * T);
		pBand[i].m_Left *= Gain;
		pBand[i].m_Right *= Gain;
		pBand[i].m_Color = pBand[i].m_Color.WithMultipliedAlpha(Gain);
	}
}

// 相邻截面共用内部边的长度与方向；退化边仍按各截面原来的规则选择回退方向。
template<typename TBandPoint>
void QmPrepareTrailBandJoins(TBandPoint *pBand, size_t Count, vec2 *pNormals)
{
	if(Count < 2)
		return;
	struct SEdge
	{
		vec2 m_Direction;
		float m_Length;
	};
	const auto PrepareEdge = [](vec2 Delta) {
		const float Length = length(Delta);
		return SEdge{Length > 0.0001f ? Delta / Length : vec2(1, 0), Length};
	};
	const vec2 FirstPrev = pBand[0].m_Pos * 2 - pBand[1].m_Pos;
	SEdge BeforeEdge = PrepareEdge(pBand[0].m_Pos - FirstPrev);
	for(size_t i = 0; i < Count; ++i)
	{
		const vec2 Prev = i > 0 ? pBand[i - 1].m_Pos : FirstPrev;
		const vec2 Next = i + 1 < Count ? pBand[i + 1].m_Pos : pBand[i].m_Pos * 2 - Prev;
		const SEdge AfterEdge = PrepareEdge(Next - pBand[i].m_Pos);
		const vec2 Before = BeforeEdge.m_Direction;
		const vec2 After = AfterEdge.m_Length > 0.0001f ? AfterEdge.m_Direction : Before;
		const vec2 Sum = Before + After;
		const float SumLength = length(Sum);
		const vec2 Tangent = SumLength > 0.0001f ? Sum / SumLength : After;
		pNormals[i] = vec2(-Tangent.y, Tangent.x);
		// 限宽公式和运算顺序不变，避免弯角内侧出现自交。
		const float Turn = std::sqrt(std::max(0.000001f, 2 - 2 * dot(Before, After)));
		const float Radius = std::min(BeforeEdge.m_Length, AfterEdge.m_Length) / Turn;
		const float Miter = 1 / std::max(0.7f, dot(Before, Tangent));
		pBand[i].m_Left = std::min(pBand[i].m_Left * Miter, Radius * 0.8f);
		pBand[i].m_Right = std::min(pBand[i].m_Right * Miter, Radius * 0.8f);
		BeforeEdge = AfterEdge;
	}
}

#endif
