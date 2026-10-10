#ifndef GAME_CLIENT_QMUI_QMHOOKCOUNTDOWNRENDER_H
#define GAME_CLIENT_QMUI_QMHOOKCOUNTDOWNRENDER_H

#include "QmPieMenuRender.h"

#include <game/client/components/qmclient/hook_countdown.h>

namespace qm_hook_countdown_ui
{
	constexpr float OUTER_RADIUS = 12.5f;
	constexpr float INNER_RADIUS = 8.5f;

	// 任意弧段：StartAngle/EndAngle 为弧度，0 = 环顶部，向顺时针增，EndAngle > StartAngle。
	// 新后端走 RenderProceduralRing（旋转+对称扇区参数化），老后端复用 DrawSector。
	inline void DrawArcSegment(IGraphics *pGraphics, vec2 Center, float InnerRadius, float OuterRadius, float StartAngle, float EndAngle, ColorRGBA Color, float PixelSize)
	{
		if(EndAngle <= StartAngle || Color.a <= 0.0f)
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
			// 扇区避开 atan 的正负 pi 接缝，再旋到段中心，避免羽化产生径向暗缝。
			const float SegmentCenter = (StartAngle + EndAngle) * 0.5f;
			const float HalfSweep = (EndAngle - StartAngle) * 0.5f;
			Params.m_RingStartAngle = -HalfSweep;
			Params.m_RingEndAngle = HalfSweep;
			Params.m_Rotation = -pi / 2.0f + SegmentCenter;
			pGraphics->RenderProceduralRing(Params);
		}
		else
		{
			// 老后端复用已有扇区几何与羽化；这里没有相邻选项，不限制端面的羽化宽度。
			constexpr float RadToDeg = 180.0f / pi;
			qm_pie_menu_ui::DrawSector(pGraphics, Center, InnerRadius, OuterRadius, -90.0f + StartAngle * RadToDeg, -90.0f + EndAngle * RadToDeg, 360.0f, Color);
		}
	}

	inline void DrawArc(IGraphics *pGraphics, vec2 Center, float InnerRadius, float OuterRadius, float Progress, ColorRGBA Color, float PixelSize)
	{
		if(Progress <= 0.0f || Color.a <= 0.0f)
			return;
		DrawArcSegment(pGraphics, Center, InnerRadius, OuterRadius, 0.0f, 2.0f * pi * Progress, Color, PixelSize);
	}

	inline void Render(IGraphics *pGraphics, const SQmHookCountdownVisual &Visual, float PixelSize)
	{
		const float InnerRadius = INNER_RADIUS * Visual.m_Scale;
		const float OuterRadius = OUTER_RADIUS * Visual.m_Scale;
		const float Outline = std::min(PixelSize * 0.7f, InnerRadius * 0.25f);
		DrawArc(pGraphics, Visual.m_Position, InnerRadius - Outline, OuterRadius + Outline, 1.0f, ColorRGBA(0.04f, 0.07f, 0.11f, Visual.m_Alpha * 0.45f), PixelSize);
		DrawArc(pGraphics, Visual.m_Position, InnerRadius, OuterRadius, 1.0f, Visual.m_Color.WithAlpha(Visual.m_Alpha * 0.20f), PixelSize);
		if(Visual.m_EndlessHook && Visual.m_FlowStyle != static_cast<int>(EQmHookCountdownFlowStyle::STATIC))
		{
			// 无限钩：静止满环换成沿外环流动的光弧——细分多段让亮度与颜色沿弧平滑渐变，
			// 随 m_FlowPhase 旋转；松钩收拢时随 Visual.m_Alpha 一起淡出。
			constexpr float HeadSweep = pi / 3.0f;
			constexpr float TailSweep = 2.0f * pi / 3.0f;
			constexpr int FlowSegments = 12;
			const float TotalSweep = HeadSweep + TailSweep;
			const float Head = Visual.m_FlowPhase * 2.0f * pi;
			for(int i = 0; i < FlowSegments; ++i)
			{
				const float From = TotalSweep * i / FlowSegments;
				const float To = TotalSweep * (i + 1) / FlowSegments;
				// 亮度窗：光头最亮，向尾部余弦渐暗（段中取样，细分足够时观感连续）。
				const float T = (From + To) * 0.5f / TotalSweep;
				const float Window = 0.10f + 0.90f * (0.5f + 0.5f * std::cos(pi * T));
				// 彩虹模式：色相随相位流动、沿弧向后偏移，形成移动的彩虹渐变段。
				ColorRGBA SegmentColor = Visual.m_Color;
				if(Visual.m_FlowStyle == static_cast<int>(EQmHookCountdownFlowStyle::RAINBOW))
					SegmentColor = QmHookCountdownFlowColor(Visual.m_FlowPhase, T);
				DrawArcSegment(pGraphics, Visual.m_Position, InnerRadius, OuterRadius, Head - To, Head - From, SegmentColor.WithAlpha(Visual.m_Alpha * Window), PixelSize);
			}
		}
		else
			DrawArc(pGraphics, Visual.m_Position, InnerRadius, OuterRadius, Visual.m_Progress, Visual.m_Color.WithAlpha(Visual.m_Alpha), PixelSize);
	}
}

#endif
