#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SNAP_GUIDES_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SNAP_GUIDES_H

#include <engine/graphics.h>

#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

// 拖动磁吸与辅助线的公共实现：设置页名牌预览和 HUD 编辑器共用同一套
// 阈值语义、虚线节奏与配色，避免两处手感和观感分叉。

// 邻近吸附半径：进入该距离内就把锚点对齐到目标。
inline constexpr float QM_DRAG_SNAP_DISTANCE = 6.0f;
inline constexpr float QM_DRAG_GUIDE_DASH = 6.0f;
inline constexpr float QM_DRAG_GUIDE_GAP = 4.0f;

inline ColorRGBA QmDragGuideColor()
{
	// 淡蓝白：叠在深色预览和游戏画面上都能读清，又不盖住被拖的内容。
	return ColorRGBA(0.62f, 0.82f, 1.0f, 0.45f);
}

struct SQmSnapTarget
{
	// 命中后元素锚点应落到的值。
	float m_Value = 0.0f;
	// 辅助线画在哪个坐标（屏幕/画布空间）。
	float m_Guide = 0.0f;
};

struct SQmSnapAxisResult
{
	float m_Value = 0.0f;
	bool m_HasGuide = false;
	float m_Guide = 0.0f;
};

// 在阈值内取最近目标；候选值先按 [MinValue, MaxValue] 约束，与拖动允许范围一致。
inline SQmSnapAxisResult QmSnapAxisToTargets(float Value, const SQmSnapTarget *pTargets, int TargetCount, float SnapDistance, float MinValue, float MaxValue)
{
	SQmSnapAxisResult Result;
	Result.m_Value = std::clamp(Value, MinValue, MaxValue);
	float BestDistance = SnapDistance + 1.0f;
	for(int i = 0; i < TargetCount; ++i)
	{
		const float Candidate = std::clamp(pTargets[i].m_Value, MinValue, MaxValue);
		const float Distance = std::fabs(Candidate - Value);
		if(Distance > SnapDistance || Distance >= BestDistance)
			continue;
		Result.m_Value = Candidate;
		Result.m_HasGuide = true;
		Result.m_Guide = pTargets[i].m_Guide;
		BestDistance = Distance;
	}
	return Result;
}

// 把一个参考矩形在某一轴上的两条边、中线，以及「本元素边框贴合到它」的落点
// 换算成目标值追加进缓冲。PivotCenter/PivotSize 是被拖元素在该轴上的无偏移位置与尺寸。
// 返回值是追加后的目标数量；缓冲满时静默截断。
inline int QmAppendRectSnapTargets(SQmSnapTarget *pTargets, int Count, int Capacity, float PivotCenter, float PivotSize, float RectMin, float RectMax)
{
	if(pTargets == nullptr || Count < 0)
		return 0;
	const float PivotMin = PivotCenter - PivotSize / 2.0f;
	const float PivotMax = PivotCenter + PivotSize / 2.0f;
	const float RectCenter = (RectMin + RectMax) * 0.5f;
	const SQmSnapTarget aCandidates[] = {
		{RectMin - PivotMin, RectMin},
		{RectMax - PivotMax, RectMax},
		{RectCenter - PivotCenter, RectCenter},
		// 边框贴合：本元素左/上边贴住参考边的右/下边，或反过来。
		{RectMax - PivotMin, RectMax},
		{RectMin - PivotMax, RectMin},
	};
	for(const SQmSnapTarget &Candidate : aCandidates)
	{
		if(Count >= Capacity)
			break;
		pTargets[Count] = Candidate;
		++Count;
	}
	return Count;
}

// 框类参考（预览边界等）只提供三条对齐线：两条边 + 中线。
// 「边框贴合」对框没有意义——贴着框外沿的落点根本不可达，只会被夹到边界上变成死角。
inline int QmAppendAlignSnapTargets(SQmSnapTarget *pTargets, int Count, int Capacity, float PivotMin, float PivotMax, float RectMin, float RectMax)
{
	if(pTargets == nullptr || Count < 0)
		return 0;
	const float PivotCenter = (PivotMin + PivotMax) * 0.5f;
	const float RectCenter = (RectMin + RectMax) * 0.5f;
	const SQmSnapTarget aCandidates[] = {
		{RectMin - PivotMin, RectMin},
		{RectMax - PivotMax, RectMax},
		{RectCenter - PivotCenter, RectCenter},
	};
	for(const SQmSnapTarget &Candidate : aCandidates)
	{
		if(Count >= Capacity)
			break;
		pTargets[Count] = Candidate;
		++Count;
	}
	return Count;
}

// 按「被拖元素（或整组）的包围盒」生成贴合候选：左边贴对方右边、右边贴对方左边，
// 以及三条对齐线。多选整组移动时贴合的是整组外轮廓，而不是主拖行自己的框。
inline int QmAppendBoundsSnapTargets(SQmSnapTarget *pTargets, int Count, int Capacity, float PivotMin, float PivotMax, float RectMin, float RectMax)
{
	if(pTargets == nullptr || Count < 0)
		return 0;
	const float PivotCenter = (PivotMin + PivotMax) * 0.5f;
	const float RectCenter = (RectMin + RectMax) * 0.5f;
	const SQmSnapTarget aCandidates[] = {
		{RectMin - PivotMin, RectMin},
		{RectMax - PivotMax, RectMax},
		{RectCenter - PivotCenter, RectCenter},
		{RectMax - PivotMin, RectMax},
		{RectMin - PivotMax, RectMin},
	};
	for(const SQmSnapTarget &Candidate : aCandidates)
	{
		if(Count >= Capacity)
			break;
		pTargets[Count] = Candidate;
		++Count;
	}
	return Count;
}

// 去掉落在拖动范围外（夹紧后只会堆在边界上）与彼此重复的候选，
// 否则候选越叠越多，拖动时几乎每一像素都在吸附。
inline int QmFilterSnapTargets(SQmSnapTarget *pTargets, int Count, float MinValue, float MaxValue, float Epsilon = 0.5f)
{
	if(pTargets == nullptr || Count <= 0)
		return 0;
	int Kept = 0;
	for(int Index = 0; Index < Count; ++Index)
	{
		const SQmSnapTarget &Candidate = pTargets[Index];
		if(Candidate.m_Value < MinValue - Epsilon || Candidate.m_Value > MaxValue + Epsilon)
			continue;
		bool Duplicate = false;
		for(int Existing = 0; Existing < Kept; ++Existing)
		{
			if(std::fabs(pTargets[Existing].m_Value - Candidate.m_Value) <= Epsilon)
			{
				Duplicate = true;
				break;
			}
		}
		if(Duplicate)
			continue;
		pTargets[Kept++] = Candidate;
	}
	return Kept;
}

// 辅助线只画横竖两条；斜线不参与磁吸，调用点传进来时按一条实线兜底。
inline int QmBuildDashedLine(float X0, float Y0, float X1, float Y1, float DashLength, float GapLength, IGraphics::CLineItem *pSegments, int MaxSegments)
{
	if(pSegments == nullptr || MaxSegments <= 0)
		return 0;
	const float Dash = std::max(1.0f, DashLength);
	const float Gap = std::max(1.0f, GapLength);
	const bool Vertical = X0 == X1;
	const bool Horizontal = Y0 == Y1;
	if(!Vertical && !Horizontal)
	{
		pSegments[0] = IGraphics::CLineItem(X0, Y0, X1, Y1);
		return 1;
	}
	const float Total = Vertical ? std::fabs(Y1 - Y0) : std::fabs(X1 - X0);
	const float Sign = Vertical ? (Y1 >= Y0 ? 1.0f : -1.0f) : (X1 >= X0 ? 1.0f : -1.0f);
	const float Start = Vertical ? Y0 : X0;
	int Count = 0;
	for(float Offset = 0.0f; Offset < Total && Count < MaxSegments; Offset += Dash + Gap)
	{
		const float SegmentEnd = std::min(Offset + Dash, Total);
		if(Vertical)
			pSegments[Count] = IGraphics::CLineItem(X0, Start + Sign * Offset, X0, Start + Sign * SegmentEnd);
		else
			pSegments[Count] = IGraphics::CLineItem(Start + Sign * Offset, Y0, Start + Sign * SegmentEnd, Y0);
		++Count;
	}
	return Count;
}

// 画一条虚线辅助线；线段数受固定缓冲限制，超长/超密的线按缓冲容量截断。
inline void QmDrawDragGuideLine(IGraphics *pGraphics, float X0, float Y0, float X1, float Y1, const ColorRGBA &Color)
{
	if(pGraphics == nullptr)
		return;
	IGraphics::CLineItem aSegments[128];
	const int Count = QmBuildDashedLine(X0, Y0, X1, Y1, QM_DRAG_GUIDE_DASH, QM_DRAG_GUIDE_GAP, aSegments, (int)std::size(aSegments));
	if(Count <= 0)
		return;
	pGraphics->TextureClear();
	pGraphics->SetColor(Color);
	pGraphics->LinesBegin();
	pGraphics->LinesDraw(aSegments, Count);
	pGraphics->LinesEnd();
	pGraphics->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
}

#endif
