#ifndef GAME_CLIENT_QMUI_QMHOOKCOUNTDOWNRENDER_H
#define GAME_CLIENT_QMUI_QMHOOKCOUNTDOWNRENDER_H

#include "QmPieMenuRender.h"

#include <game/client/components/qmclient/hook_countdown.h>

namespace qm_hook_countdown_ui
{
	constexpr float OUTER_RADIUS = 12.5f;
	constexpr float INNER_RADIUS = 8.5f;

	inline void DrawArc(IGraphics *pGraphics, vec2 Center, float InnerRadius, float OuterRadius, float Progress, ColorRGBA Color, float PixelSize)
	{
		if(Progress <= 0.0f || Color.a <= 0.0f)
			return;
		if(pGraphics->HasProceduralRing())
		{
			// 绘制框留出羽化空间，不在圆环外沿截断抗锯齿。
			const float Extent = OuterRadius + PixelSize * 1.5f;
			IGraphics::SProceduralRingParams Params;
			Params.m_Rect = vec4(Center.x - Extent, Center.y - Extent, Extent * 2.0f, Extent * 2.0f);
			Params.m_Color = Color;
			Params.m_RingInnerRadius = InnerRadius / (Extent * 2.0f);
			Params.m_RingOuterRadius = OuterRadius / (Extent * 2.0f);
			// 扇区避开 atan 的正负 pi 接缝，再旋到顶部起笔，避免羽化产生径向暗缝。
			const float HalfSweep = pi * Progress;
			Params.m_RingStartAngle = -HalfSweep;
			Params.m_RingEndAngle = HalfSweep;
			Params.m_Rotation = -pi / 2.0f + HalfSweep;
			pGraphics->RenderProceduralRing(Params);
		}
		else
		{
			// 老后端复用已有扇区几何与羽化；这里没有相邻选项，不限制端面的羽化宽度。
			qm_pie_menu_ui::DrawSector(pGraphics, Center, InnerRadius, OuterRadius, -90.0f, -90.0f + 360.0f * Progress, 360.0f, Color);
		}
	}

	inline void Render(IGraphics *pGraphics, const SQmHookCountdownVisual &Visual, float PixelSize)
	{
		const float InnerRadius = INNER_RADIUS * Visual.m_Scale;
		const float OuterRadius = OUTER_RADIUS * Visual.m_Scale;
		const float Outline = std::min(PixelSize * 0.7f, InnerRadius * 0.25f);
		DrawArc(pGraphics, Visual.m_Position, InnerRadius - Outline, OuterRadius + Outline, 1.0f, ColorRGBA(0.04f, 0.07f, 0.11f, Visual.m_Alpha * 0.45f), PixelSize);
		DrawArc(pGraphics, Visual.m_Position, InnerRadius, OuterRadius, 1.0f, Visual.m_Color.WithAlpha(Visual.m_Alpha * 0.20f), PixelSize);
		DrawArc(pGraphics, Visual.m_Position, InnerRadius, OuterRadius, Visual.m_Progress, Visual.m_Color.WithAlpha(Visual.m_Alpha), PixelSize);
	}
}

#endif
