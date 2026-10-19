#ifndef GAME_CLIENT_QMUI_QMHOOKCOUNTDOWNRENDER_H
#define GAME_CLIENT_QMUI_QMHOOKCOUNTDOWNRENDER_H

#include "QmPieMenuRender.h"

#include <engine/shared/config.h>

#include <game/client/components/qmclient/hook_countdown.h>

namespace qm_hook_countdown_ui
{
	constexpr float OUTER_RADIUS = 12.5f;
	constexpr float INNER_RADIUS = 8.5f;
	constexpr float INFINITY_OUTER_RADIUS = 18.0f;

	// 可调尺寸：百分比换算成半径乘数，1.0 = 配置默认 100%。
	inline float CountdownSizeScale()
	{
		const int Percent = g_Config.m_QmHookCountdownSize;
		return (Percent > 0 ? (float)Percent : 100.0f) / 100.0f;
	}

	// 无限钩的样式：0 = 彩虹 ∞ 符号，1 = 彩虹圆环。
	inline bool EndlessHookUsesRing()
	{
		return g_Config.m_QmHookCountdownEndlessStyle == 1;
	}

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

	inline void DrawInfinity(IGraphics *pGraphics, const SQmHookCountdownVisual &Visual, float PixelSize)
	{
		const auto &aPath = QmHookCountdownInfinityPath();
		const float Breath = QmHookCountdownBreath(Visual.m_BreathPhase);
		const float Scale = Visual.m_Scale * (0.97f + Breath * 0.05f);
		const float CoreHalfWidth = std::max(0.35f * Scale, PixelSize * 0.35f);
		const float aHalfWidths[] = {CoreHalfWidth, CoreHalfWidth + 0.5f * Scale, CoreHalfWidth + 1.45f * Scale, CoreHalfWidth + 2.85f * Scale, CoreHalfWidth + 5.05f * Scale + PixelSize};
		const float aAlphas[] = {0.76f, 0.46f, 0.14f, 0.02f, 0.0f};
		const float GlowAlpha = Visual.m_Alpha * (0.65f + Breath * 0.3f);
		std::array<ColorRGBA, 97> aColors;
		for(size_t i = 0; i < aPath.size(); ++i)
			aColors[i] = QmHookCountdownFlowColor(Visual.m_FlowPhase, aPath[i].m_PathPosition);

		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		// 两侧用连续透明度带形成外发光，曲线和颜色端点共享，避免旧圆环的分段暗缝。
		for(size_t Layer = 0; Layer + 1 < std::size(aHalfWidths); ++Layer)
		{
			for(size_t i = 0; i + 1 < aPath.size(); ++i)
			{
				const auto &From = aPath[i];
				const auto &To = aPath[i + 1];
				const IGraphics::CColorVertex aVertices[] = {
					{0, aColors[i].WithAlpha(GlowAlpha * aAlphas[Layer])},
					{1, aColors[i + 1].WithAlpha(GlowAlpha * aAlphas[Layer])},
					{2, aColors[i].WithAlpha(GlowAlpha * aAlphas[Layer + 1])},
					{3, aColors[i + 1].WithAlpha(GlowAlpha * aAlphas[Layer + 1])}};
				pGraphics->SetColorVertex(aVertices, std::size(aVertices));
				for(float Side : {-1.0f, 1.0f})
				{
					const IGraphics::CFreeformItem Quad(
						Visual.m_Position + From.m_Position * Scale + From.m_Normal * (aHalfWidths[Layer] * Side),
						Visual.m_Position + To.m_Position * Scale + To.m_Normal * (aHalfWidths[Layer] * Side),
						Visual.m_Position + From.m_Position * Scale + From.m_Normal * (aHalfWidths[Layer + 1] * Side),
						Visual.m_Position + To.m_Position * Scale + To.m_Normal * (aHalfWidths[Layer + 1] * Side));
					pGraphics->QuadsDrawFreeform(&Quad, 1);
				}
			}
		}
		// 彩虹细芯最后绘制：核心也沿路径取色，不能让白色盖住流动的渐变。
		// 交叉处最后绘制，保证连续颜色不会被后画的彩色光晕切断。
		const float CoreAlpha = Visual.m_Alpha * (0.78f + Breath * 0.18f);
		for(size_t i = 0; i + 1 < aPath.size(); ++i)
		{
			const auto &From = aPath[i];
			const auto &To = aPath[i + 1];
			const IGraphics::CColorVertex aVertices[] = {
				{0, aColors[i].WithAlpha(CoreAlpha)},
				{1, aColors[i + 1].WithAlpha(CoreAlpha)},
				{2, aColors[i].WithAlpha(CoreAlpha)},
				{3, aColors[i + 1].WithAlpha(CoreAlpha)}};
			pGraphics->SetColorVertex(aVertices, std::size(aVertices));
			const IGraphics::CFreeformItem Quad(
				Visual.m_Position + From.m_Position * Scale - From.m_Normal * CoreHalfWidth,
				Visual.m_Position + To.m_Position * Scale - To.m_Normal * CoreHalfWidth,
				Visual.m_Position + From.m_Position * Scale + From.m_Normal * CoreHalfWidth,
				Visual.m_Position + To.m_Position * Scale + To.m_Normal * CoreHalfWidth);
			pGraphics->QuadsDrawFreeform(&Quad, 1);
		}
		pGraphics->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
		pGraphics->QuadsEnd();
	}

	inline void Render(IGraphics *pGraphics, const SQmHookCountdownVisual &Visual, float PixelSize)
	{
		const float SizeScale = CountdownSizeScale();
		if(Visual.m_EndlessHook)
		{
			// 圆环样式：沿用无限钩的彩虹流动色，但用环而不是 ∞ 符号表达"无倒计时"。
			if(EndlessHookUsesRing())
			{
				SQmHookCountdownVisual RingVisual = Visual;
				RingVisual.m_Scale *= SizeScale;
				const float RingInner = INNER_RADIUS * RingVisual.m_Scale;
				const float RingOuter = OUTER_RADIUS * RingVisual.m_Scale;
				const float RingOutline = std::min(PixelSize * 0.7f, RingInner * 0.25f);
				const float Breath = QmHookCountdownBreath(RingVisual.m_BreathPhase);
				DrawArc(pGraphics, RingVisual.m_Position, RingInner - RingOutline, RingOuter + RingOutline, 1.0f, ColorRGBA(0.04f, 0.07f, 0.11f, RingVisual.m_Alpha * 0.45f), PixelSize);
				// 外发光：三圈递减 alpha 的整环叠出光晕。
				const float aGlowAlphas[] = {0.10f, 0.20f, 0.38f};
				const float aGlowSpreads[] = {RingOutline + 7.0f, RingOutline + 3.6f, RingOutline + 1.4f};
				for(int Layer = 0; Layer < 3; ++Layer)
				{
					const float Alpha = aGlowAlphas[Layer] * (0.75f + Breath * 0.35f) * RingVisual.m_Alpha;
					DrawArc(pGraphics, RingVisual.m_Position, RingInner - aGlowSpreads[Layer], RingOuter + aGlowSpreads[Layer], 1.0f, QmHookCountdownFlowColor(RingVisual.m_FlowPhase, 0.0f).WithAlpha(Alpha), PixelSize);
				}
				// 主环按路径位置分色，才看得出彩虹在流动。
				constexpr int ARCS = 48;
				for(int i = 0; i < ARCS; ++i)
				{
					const float From = (float)i / ARCS;
					const float To = (float)(i + 1) / ARCS;
					const ColorRGBA Color = QmHookCountdownFlowColor(RingVisual.m_FlowPhase, From).WithAlpha(RingVisual.m_Alpha * (0.90f + Breath * 0.10f));
					DrawArcSegment(pGraphics, RingVisual.m_Position, RingInner, RingOuter, From * 2.0f * pi, To * 2.0f * pi, Color, PixelSize);
				}
				return;
			}
			SQmHookCountdownVisual ScaledVisual = Visual;
			ScaledVisual.m_Scale *= SizeScale;
			DrawInfinity(pGraphics, ScaledVisual, PixelSize);
			return;
		}
		const float InnerRadius = INNER_RADIUS * Visual.m_Scale * SizeScale;
		const float OuterRadius = OUTER_RADIUS * Visual.m_Scale * SizeScale;
		const float Outline = std::min(PixelSize * 0.7f, InnerRadius * 0.25f);
		DrawArc(pGraphics, Visual.m_Position, InnerRadius - Outline, OuterRadius + Outline, 1.0f, ColorRGBA(0.04f, 0.07f, 0.11f, Visual.m_Alpha * 0.45f), PixelSize);
		DrawArc(pGraphics, Visual.m_Position, InnerRadius, OuterRadius, 1.0f, Visual.m_Color.WithAlpha(Visual.m_Alpha * 0.20f), PixelSize);
		DrawArc(pGraphics, Visual.m_Position, InnerRadius, OuterRadius, Visual.m_Progress, Visual.m_Color.WithAlpha(Visual.m_Alpha), PixelSize);
	}
}

#endif
