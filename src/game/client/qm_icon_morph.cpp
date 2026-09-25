// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "qm_icon_morph.h"

#include <engine/graphics.h>

#include <generated/qm_icon_morph_generated.inc>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
	constexpr float VIEWBOX_SIZE = 256.0f;

	const SQmIconMorphPlan *PlanForWeight(const int Weight)
	{
		// 只有 Bold 字重有运行时的样例数据（默认 UI 字重，由随包 Phosphor-Bold.ttf
		// 生成）；其余字重返回 nullptr，调用方保留 atlas/MSDF/字形回退。
		if(Weight == 1)
			return &s_EyeMorphPlan_bold;
		return nullptr;
	}

	vec2 ResolvePathPointUnclamped(const SQmIconMorphPathData &Path, const int PointIndex, const float Progress)
	{
		const int ClampedIndex = std::clamp(PointIndex, 0, QM_ICON_MORPH_SAMPLE_COUNT - 1);
		const float SourceX = Path.m_pSource[ClampedIndex * 2];
		const float SourceY = Path.m_pSource[ClampedIndex * 2 + 1];
		const float TargetX = Path.m_pTarget[ClampedIndex * 2];
		const float TargetY = Path.m_pTarget[ClampedIndex * 2 + 1];
		const float ResidualX = SourceX + (TargetX - SourceX) * Progress;
		const float ResidualY = SourceY + (TargetY - SourceY) * Progress;
		const float Scale = std::exp(Path.m_LogScale * Progress);
		const float Angle = Path.m_Theta * Progress;
		const float CosAngle = std::cos(Angle);
		const float SinAngle = std::sin(Angle);
		const float TransformedX = (ResidualX * CosAngle - ResidualY * SinAngle) * Scale;
		const float TransformedY = (ResidualX * SinAngle + ResidualY * CosAngle) * Scale;
		const float CenterX = Path.m_SourceCenterX + (Path.m_TargetCenterX - Path.m_SourceCenterX) * Progress;
		const float CenterY = Path.m_SourceCenterY + (Path.m_TargetCenterY - Path.m_SourceCenterY) * Progress;
		return vec2(CenterX + TransformedX, CenterY + TransformedY);
	}
}

vec2 ResolveQmIconMorphPoint(const SQmIconMorphPathData &Path, const int PointIndex, float Progress)
{
	// 弹簧可能略微越过目标。限制采样范围但保留越界，使重定向切换保持连续。
	Progress = std::clamp(Progress, -0.25f, 1.25f);
	return ResolvePathPointUnclamped(Path, PointIndex, Progress);
}

const SQmIconMorphPlan *QmEyeMorphPlanForWeight(const int Weight)
{
	return PlanForWeight(Weight);
}

bool RenderQmEyeMorph(IGraphics *pGraphics, const int Weight, const CUIRect &Rect, const ColorRGBA &Color, const float Progress)
{
	const SQmIconMorphPlan *pPlan = PlanForWeight(Weight);
	if(pGraphics == nullptr || pPlan == nullptr || pPlan->m_pSurfaces == nullptr || pPlan->m_NumSurfaces <= 0 || Rect.w <= 0.0f || Rect.h <= 0.0f || Color.a <= 0.0f)
		return false;

	std::array<IGraphics::CFreeformItem, QM_ICON_MORPH_SAMPLE_COUNT> aSegments{};
	pGraphics->TextureClear();
	pGraphics->QuadsBegin();
	pGraphics->SetColor(Color);
	for(int SurfaceIndex = 0; SurfaceIndex < pPlan->m_NumSurfaces; ++SurfaceIndex)
	{
		const SQmIconMorphSurfaceData &Surface = pPlan->m_pSurfaces[SurfaceIndex];
		for(int PointIndex = 0; PointIndex < QM_ICON_MORPH_SAMPLE_COUNT; ++PointIndex)
		{
			const int NextIndex = (PointIndex + 1) % QM_ICON_MORPH_SAMPLE_COUNT;
			const vec2 Outer0 = ResolveQmIconMorphPoint(Surface.m_Outer, PointIndex, Progress);
			const vec2 Outer1 = ResolveQmIconMorphPoint(Surface.m_Outer, NextIndex, Progress);
			const vec2 Inner0 = ResolveQmIconMorphPoint(Surface.m_Inner, PointIndex, Progress);
			const vec2 Inner1 = ResolveQmIconMorphPoint(Surface.m_Inner, NextIndex, Progress);
			const auto ToScreen = [&Rect](const vec2 &Point) {
				return vec2(Rect.x + Point.x / VIEWBOX_SIZE * Rect.w, Rect.y + Point.y / VIEWBOX_SIZE * Rect.h);
			};
			aSegments[PointIndex] = IGraphics::CFreeformItem(ToScreen(Outer0), ToScreen(Outer1), ToScreen(Inner1), ToScreen(Inner0));
		}
		pGraphics->QuadsDrawFreeform(aSegments.data(), QM_ICON_MORPH_SAMPLE_COUNT);
	}
	pGraphics->QuadsEnd();
	pGraphics->TextureClear();
	return true;
}
