#ifndef GAME_CLIENT_QMUI_QMHOOKCOUNTDOWNRENDER_H
#define GAME_CLIENT_QMUI_QMHOOKCOUNTDOWNRENDER_H

#include "QmPieMenuRender.h"
#include "UiTokens.h"

#include <engine/shared/config.h>

#include <game/client/components/qmclient/hook_countdown.h>

namespace qm_hook_countdown_ui
{
	constexpr float OUTER_RADIUS = 12.5f;
	constexpr float INNER_RADIUS = 8.5f;
	// 光晕最外层相对实体再往外延伸的世界单位（未乘尺寸）：HUD 裁剪半径必须带上它，
	// 否则调大尺寸后光晕外圈会被提前裁成一条硬边。
	constexpr float GLOW_EXTENT = 7.0f;

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

	// 渲染可能触及的最大半径：普通钩是倒计时环，无限钩是 ∞ 或彩虹环，都要再叠一圈光晕。
	inline float RenderClipRadius(const SQmHookCountdownVisual &Visual)
	{
		const float HalfExtent = Visual.m_EndlessHook ? QM_HOOK_COUNTDOWN_INFINITY_HALF_WIDTH : OUTER_RADIUS;
		return (HalfExtent + GLOW_EXTENT) * Visual.m_Scale * CountdownSizeScale();
	}

	// 任意弧段：StartAngle/EndAngle 为弧度，0 = 环顶部，向顺时针增，EndAngle > StartAngle。
	// 新后端走 RenderProceduralRing（旋转+对称扇区参数化），老后端复用 DrawSector。
	// 只用于单色的普通钩倒计时弧：彩虹环需要逐顶点取色，另有 EmitRingBand。
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

	// 圆头端帽的角度半径：端帽半径（内外径差的一半）在环中线处张开的角度。
	// 内外径等比缩放，所以这个角度与尺寸档位无关，可以直接判断缺口有没有被端帽盖住。
	inline float CountdownCapAngle()
	{
		const float MidRadius = (INNER_RADIUS + OUTER_RADIUS) * 0.5f;
		return (OUTER_RADIUS - INNER_RADIUS) * 0.5f / MidRadius;
	}

	// 视觉缺口（弧度）：整环为 0。刚钩住的一小段弧长会先被两枚圆头端帽吃掉，
	// 缺口从 0 连续张开，不会一出现就切出一道方口；这个函数同时是渲染的判据和测试契约。
	inline float CountdownVisibleGap(float Progress)
	{
		const float Gap = 2.0f * pi * (1.0f - std::clamp(Progress, 0.0f, 1.0f));
		return std::max(0.0f, Gap - 2.0f * CountdownCapAngle());
	}

	// 端帽画成整圆：一半落在弧内被同色盖住，另一半正好补出圆头，
	// 弧端面的羽化与新画的圆重叠，接缝处不会漏出一条暗线。
	inline void DrawCapDisc(IGraphics *pGraphics, vec2 Center, float Radius, ColorRGBA Color, float PixelSize)
	{
		if(Radius <= 0.0f || Color.a <= 0.0f)
			return;
		if(pGraphics->HasProceduralRing())
		{
			// 内径 0、整圈的角度窗口：沿用同一个圆环 SDF，边缘自带抗锯齿。
			const float Extent = Radius + PixelSize;
			IGraphics::SProceduralRingParams Params;
			Params.m_Rect = vec4(Center.x - Extent, Center.y - Extent, Extent * 2.0f, Extent * 2.0f);
			Params.m_Color = Color;
			Params.m_RingInnerRadius = 0.0f;
			Params.m_RingOuterRadius = Radius / (Extent * 2.0f);
			Params.m_RingStartAngle = 0.0f;
			Params.m_RingEndAngle = 2.0f * pi;
			Params.m_Rotation = 0.0f;
			pGraphics->RenderProceduralRing(Params);
		}
		else
		{
			qm_pie_menu_ui::DrawDisc(pGraphics, Center, Radius, Color);
		}
	}

	// 倒计时弧：两端带圆头端帽。缺口还被端帽盖住时直接画整环——刚钩住的几帧里
	// 环看起来仍是闭合的，端帽不会在环顶叠出一块更亮的凸起。
	inline void DrawProgressArc(IGraphics *pGraphics, vec2 Center, float InnerRadius, float OuterRadius, float Progress, ColorRGBA Color, float PixelSize)
	{
		if(Progress <= 0.0f || Color.a <= 0.0f)
			return;
		if(Progress >= 1.0f || CountdownVisibleGap(Progress) <= 0.0f)
		{
			DrawArcSegment(pGraphics, Center, InnerRadius, OuterRadius, 0.0f, 2.0f * pi, Color, PixelSize);
			return;
		}
		const float EndAngle = 2.0f * pi * Progress;
		DrawArcSegment(pGraphics, Center, InnerRadius, OuterRadius, 0.0f, EndAngle, Color, PixelSize);
		// 0 = 环顶，顺时针到 EndAngle；端帽落在弧两端的中径上。
		const float CapRadius = (OuterRadius - InnerRadius) * 0.5f;
		const float MidRadius = (InnerRadius + OuterRadius) * 0.5f;
		DrawCapDisc(pGraphics, Center + vec2(0.0f, -MidRadius), CapRadius, Color, PixelSize);
		DrawCapDisc(pGraphics, Center + vec2(std::sin(EndAngle), -std::cos(EndAngle)) * MidRadius, CapRadius, Color, PixelSize);
	}

	// 光晕层：越靠外越宽越淡，越靠内越窄越浓；宽度连续衔接，叠起来才是"晕"，
	// 而不是三道能看出分界的硬环。呼吸同时改强度与扩散，光圈才会跟着吸气。
	struct SQmHookCountdownGlowLayer
	{
		float m_Spread = 0.0f;
		float m_Alpha = 0.0f;
	};

	inline std::array<SQmHookCountdownGlowLayer, 3> QmHookCountdownGlowLayers(float Breath)
	{
		const float Strength = 0.72f + Breath * 0.42f;
		return {{
			{4.6f + Breath * 1.2f, 0.10f * Strength},
			{2.8f + Breath * 0.7f, 0.20f * Strength},
			{1.3f + Breath * 0.3f, 0.38f * Strength},
		}};
	}

	// ∞ 的光晕要窄一档：叶肩处的曲率半径只有几个世界单位，光晕铺得和圆环一样宽时，
	// 偏移曲线在那里必然翻折。这里让光晕宽度基本落在曲率半径以内，形状再小也干净。
	inline std::array<SQmHookCountdownGlowLayer, 3> QmHookCountdownInfinityGlowLayers(float Breath)
	{
		const float Strength = 0.72f + Breath * 0.42f;
		return {{
			{2.4f + Breath * 0.5f, 0.13f * Strength},
			{1.5f + Breath * 0.3f, 0.26f * Strength},
			{0.8f + Breath * 0.2f, 0.44f * Strength},
		}};
	}

	// 圆形彩虹环的采样：颜色只由角度和流动相位决定，内沿、外沿与光晕共用同一组颜色，
	// 彩虹才连成一整圈；逐段平涂会在每段之间留下可见接缝。
	struct SQmHookCountdownRingSample
	{
		vec2 m_Outward{0.0f, -1.0f};
		ColorRGBA m_Color = ui_token::color::NEUTRAL_WHITE;
	};

	constexpr int RING_SEGMENTS = 72;
	using TQmHookCountdownRingSamples = std::array<SQmHookCountdownRingSample, RING_SEGMENTS + 1>;

	inline TQmHookCountdownRingSamples QmHookCountdownRingSamples(float FlowPhase)
	{
		TQmHookCountdownRingSamples aSamples{};
		for(int i = 0; i <= RING_SEGMENTS; ++i)
		{
			const float Position = static_cast<float>(i) / static_cast<float>(RING_SEGMENTS);
			// 0 = 环顶部，顺时针；首尾位置重合，颜色也闭合。
			const float Angle = Position * 2.0f * pi - pi * 0.5f;
			aSamples[i].m_Outward = vec2(std::cos(Angle), std::sin(Angle));
			aSamples[i].m_Color = QmHookCountdownFlowColor(FlowPhase, Position);
		}
		return aSamples;
	}

	// 一条环形光带：内沿/外沿各自给透明度，颜色逐顶点取彩虹，因此整条带是连续的角度
	// 渐变。Freeform 的 0/1 槽位对应内沿两端，2/3 对应外沿两端。
	inline void EmitRingBand(IGraphics *pGraphics, vec2 Center, float InnerRadius, float OuterRadius, float InnerAlpha, float OuterAlpha, const TQmHookCountdownRingSamples &aSamples)
	{
		if(OuterRadius <= InnerRadius || (InnerAlpha <= 0.0f && OuterAlpha <= 0.0f))
			return;
		for(int i = 0; i < RING_SEGMENTS; ++i)
		{
			const SQmHookCountdownRingSample &From = aSamples[i];
			const SQmHookCountdownRingSample &To = aSamples[i + 1];
			const IGraphics::CColorVertex aVertices[] = {
				{0, From.m_Color.WithAlpha(InnerAlpha)},
				{1, To.m_Color.WithAlpha(InnerAlpha)},
				{2, From.m_Color.WithAlpha(OuterAlpha)},
				{3, To.m_Color.WithAlpha(OuterAlpha)}};
			pGraphics->SetColorVertex(aVertices, std::size(aVertices));
			const IGraphics::CFreeformItem Quad(
				Center + From.m_Outward * InnerRadius,
				Center + To.m_Outward * InnerRadius,
				Center + From.m_Outward * OuterRadius,
				Center + To.m_Outward * OuterRadius);
			pGraphics->QuadsDrawFreeform(&Quad, 1);
		}
	}

	// 纯色环带：给彩虹环铺一圈暗衬，亮场景里才有边界。
	inline void EmitSolidRingBand(IGraphics *pGraphics, vec2 Center, float InnerRadius, float OuterRadius, ColorRGBA Color, float InnerAlpha, float OuterAlpha, const TQmHookCountdownRingSamples &aSamples)
	{
		if(OuterRadius <= InnerRadius || (InnerAlpha <= 0.0f && OuterAlpha <= 0.0f))
			return;
		for(int i = 0; i < RING_SEGMENTS; ++i)
		{
			const SQmHookCountdownRingSample &From = aSamples[i];
			const SQmHookCountdownRingSample &To = aSamples[i + 1];
			const IGraphics::CColorVertex aVertices[] = {
				{0, Color.WithAlpha(InnerAlpha)},
				{1, Color.WithAlpha(InnerAlpha)},
				{2, Color.WithAlpha(OuterAlpha)},
				{3, Color.WithAlpha(OuterAlpha)}};
			pGraphics->SetColorVertex(aVertices, std::size(aVertices));
			const IGraphics::CFreeformItem Quad(
				Center + From.m_Outward * InnerRadius,
				Center + To.m_Outward * InnerRadius,
				Center + From.m_Outward * OuterRadius,
				Center + To.m_Outward * OuterRadius);
			pGraphics->QuadsDrawFreeform(&Quad, 1);
		}
	}

	// ∞ 曲线上的光带：沿曲线两侧对称铺开，HalfWidth 从 Inner 到 Outer 的变化给出
	// 芯线、软边和光晕三种用途；颜色逐点取彩虹，光晕本身也是彩色的。
	// 颜色由调用方一次算好传进来：同一帧要铺六条带，逐带重算色相反复做 HSL 转换没有意义。
	using TQmHookCountdownInfinityColors = std::array<ColorRGBA, QM_HOOK_COUNTDOWN_INFINITY_POINTS>;

	// 逐点曲率信息：弯曲越急的地方，能朝曲率中心方向偏移的距离越小。
	// ∞ 的叶肩曲率半径只有几个世界单位，光晕若一律按固定宽度铺开，偏移曲线会越过
	// 曲率中心自我翻折，在叶心里画出一片细长的三角形伪影。
	struct SQmHookCountdownInfinityCurvature
	{
		float m_Radius = 1.0e4f;
		// 朝曲率中心偏移的那一侧：+1 表示 +Normal 一侧，-1 表示 -Normal 一侧。
		float m_ConcaveSide = 1.0f;
	};
	using TQmHookCountdownInfinityCurvature = std::array<SQmHookCountdownInfinityCurvature, QM_HOOK_COUNTDOWN_INFINITY_POINTS>;

	inline TQmHookCountdownInfinityCurvature QmHookCountdownInfinityCurvature()
	{
		TQmHookCountdownInfinityCurvature aCurvature{};
		const auto &aPath = QmHookCountdownInfinityPath();
		const size_t Count = aPath.size();
		for(size_t i = 0; i < Count; ++i)
		{
			const vec2 Prev = aPath[i == 0 ? Count - 2 : i - 1].m_Position;
			const vec2 Cur = aPath[i].m_Position;
			const vec2 Next = aPath[i + 1 < Count ? i + 1 : 1].m_Position;
			const vec2 A = Cur - Prev;
			const vec2 B = Next - Cur;
			const float LengthA = length(A);
			const float LengthB = length(B);
			const float Cross = A.x * B.y - A.y * B.x;
			const float Turn = std::atan2(Cross, A.x * B.x + A.y * B.y);
			if(LengthA < 1.0e-6f || LengthB < 1.0e-6f || std::abs(Turn) < 1.0e-4f)
				continue; // 直线段没有曲率限制，保留默认的大半径。
			aCurvature[i].m_Radius = std::abs((LengthA + LengthB) * 0.5f / Turn);
			// 转向一侧就是曲率中心所在的一侧：逆时针转时中心在切线的左手边。
			const vec2 ToCenter = Cross >= 0.0f ? vec2(-A.y, A.x) : vec2(A.y, -A.x);
			aCurvature[i].m_ConcaveSide = ToCenter.x * aPath[i].m_Normal.x + ToCenter.y * aPath[i].m_Normal.y >= 0.0f ? 1.0f : -1.0f;
		}
		return aCurvature;
	}

	// 偏移量上限：曲率半径的 85%，留一点余量，偏移曲线就不会越过曲率中心翻折。
	// 只压窄朝曲率中心的那一侧；背离曲率中心的一侧是凸面，铺多宽都不会翻折，
	// 外圈光晕才能保持完整。
	constexpr float INFINITY_OFFSET_CURVATURE_LIMIT = 0.85f;

	inline void EmitInfinityBand(IGraphics *pGraphics, vec2 Center, float Scale, const TQmHookCountdownInfinityColors &aColors, const TQmHookCountdownInfinityCurvature &aCurvature, float InnerHalfWidth, float OuterHalfWidth, float InnerAlpha, float OuterAlpha)
	{
		if(OuterHalfWidth <= InnerHalfWidth || (InnerAlpha <= 0.0f && OuterAlpha <= 0.0f))
			return;
		const auto &aPath = QmHookCountdownInfinityPath();
		const auto WidthFactor = [&](size_t Index, float Side) {
			if(Side != aCurvature[Index].m_ConcaveSide)
				return 1.0f;
			return std::min(1.0f, INFINITY_OFFSET_CURVATURE_LIMIT * aCurvature[Index].m_Radius / OuterHalfWidth);
		};
		for(size_t i = 0; i + 1 < aPath.size(); ++i)
		{
			const SQmHookCountdownInfinityPoint &From = aPath[i];
			const SQmHookCountdownInfinityPoint &To = aPath[i + 1];
			const ColorRGBA FromColor = aColors[i];
			const ColorRGBA ToColor = aColors[i + 1];
			const IGraphics::CColorVertex aVertices[] = {
				{0, FromColor.WithAlpha(InnerAlpha)},
				{1, ToColor.WithAlpha(InnerAlpha)},
				{2, FromColor.WithAlpha(OuterAlpha)},
				{3, ToColor.WithAlpha(OuterAlpha)}};
			pGraphics->SetColorVertex(aVertices, std::size(aVertices));
			for(float Side : {-1.0f, 1.0f})
			{
				const float FromFactor = WidthFactor(i, Side);
				const float ToFactor = WidthFactor(i + 1, Side);
				const IGraphics::CFreeformItem Quad(
					Center + From.m_Position * Scale + From.m_Normal * (InnerHalfWidth * FromFactor * Side),
					Center + To.m_Position * Scale + To.m_Normal * (InnerHalfWidth * ToFactor * Side),
					Center + From.m_Position * Scale + From.m_Normal * (OuterHalfWidth * FromFactor * Side),
					Center + To.m_Position * Scale + To.m_Normal * (OuterHalfWidth * ToFactor * Side));
				pGraphics->QuadsDrawFreeform(&Quad, 1);
			}
		}
	}

	// 无限钩的圆环样式：彩虹沿环流动，外圈是连续衰减的彩色光晕。
	inline void RenderEndlessRing(IGraphics *pGraphics, const SQmHookCountdownVisual &Visual, float PixelSize)
	{
		const float Scale = Visual.m_Scale * CountdownSizeScale();
		const float Inner = INNER_RADIUS * Scale;
		const float Outer = OUTER_RADIUS * Scale;
		const float Breath = QmHookCountdownBreath(Visual.m_BreathPhase);
		const float Alpha = Visual.m_Alpha;
		// 软边宽度：屏幕上半像素起步，缩小后环沿才不会硬成锯齿。
		const float Soft = std::max(PixelSize * 0.75f, Scale * 0.35f);
		const float CoreAlpha = Alpha * (0.90f + Breath * 0.10f);
		const auto aSamples = QmHookCountdownRingSamples(Visual.m_FlowPhase);

		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		// 暗衬比环略宽一点：彩虹再亮也需要一圈对比，否则贴在亮地面上会糊掉。
		EmitSolidRingBand(pGraphics, Visual.m_Position, Inner - Soft, Outer + Soft, ui_token::color::SURFACE_BACKDROP, Alpha * 0.38f, Alpha * 0.38f, aSamples);
		// 外光晕由宽到窄、由淡到浓，每层都从环沿连续衰减到 0；颜色跟着环走，光晕才是彩色的。
		for(const auto &Layer : QmHookCountdownGlowLayers(Breath))
			EmitRingBand(pGraphics, Visual.m_Position, Outer, Outer + Layer.m_Spread * Scale, Alpha * Layer.m_Alpha, 0.0f, aSamples);
		// 内侧补一圈，光环才是围绕圆环的光，而不是单向投影。
		EmitRingBand(pGraphics, Visual.m_Position, Inner - (1.6f + Breath * 0.4f) * Scale, Inner, 0.0f, Alpha * 0.24f * (0.72f + Breath * 0.42f), aSamples);
		// 主环：内外各一条软边，中间是实心彩带。
		EmitRingBand(pGraphics, Visual.m_Position, Inner - Soft, Inner, 0.0f, CoreAlpha, aSamples);
		EmitRingBand(pGraphics, Visual.m_Position, Outer, Outer + Soft, CoreAlpha, 0.0f, aSamples);
		EmitRingBand(pGraphics, Visual.m_Position, Inner, Outer, CoreAlpha, CoreAlpha, aSamples);
		pGraphics->SetColor(ui_token::color::NEUTRAL_WHITE);
		pGraphics->QuadsEnd();
	}

	// 无限钩的 ∞ 符号样式：与圆环共用同一套流光色与光晕层，两者只是形状不同。
	inline void RenderEndlessInfinity(IGraphics *pGraphics, const SQmHookCountdownVisual &Visual, float PixelSize)
	{
		const float Breath = QmHookCountdownBreath(Visual.m_BreathPhase);
		// 呼吸顺带轻微缩放，∞ 符号才跟着光晕一起起伏。
		const float Scale = Visual.m_Scale * CountdownSizeScale() * (0.97f + Breath * 0.05f);
		const float Alpha = Visual.m_Alpha;
		// 芯线宽度与软边都要撑得住放大后的尺寸，也要在高分辨率下保持可见。
		const float Core = std::max(PixelSize * 0.6f, Scale * 0.45f);
		const float Soft = std::max(PixelSize * 0.7f, Scale * 0.30f);
		const float CoreAlpha = Alpha * (0.90f + Breath * 0.10f);

		// 沿曲线取一次彩虹：芯线、软边与三层光晕共用同一组顶点色，才是一条连续的流光。
		TQmHookCountdownInfinityColors aColors{};
		const auto &aPath = QmHookCountdownInfinityPath();
		for(size_t i = 0; i < aPath.size(); ++i)
			aColors[i] = QmHookCountdownFlowColor(Visual.m_FlowPhase, aPath[i].m_PathPosition);
		const auto aCurvature = QmHookCountdownInfinityCurvature();

		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		// 外光晕：由宽到窄、由淡到浓，颜色沿曲线取彩虹，光晕本身也在流动。
		for(const auto &Layer : QmHookCountdownInfinityGlowLayers(Breath))
			EmitInfinityBand(pGraphics, Visual.m_Position, Scale, aColors, aCurvature, Core, Core + Layer.m_Spread * Scale, Alpha * Layer.m_Alpha, 0.0f);
		// 芯线的硬边用两条软边抹掉，细线上也不会出现台阶。
		EmitInfinityBand(pGraphics, Visual.m_Position, Scale, aColors, aCurvature, Core - Soft, Core, 0.0f, CoreAlpha);
		EmitInfinityBand(pGraphics, Visual.m_Position, Scale, aColors, aCurvature, Core, Core + Soft, CoreAlpha, 0.0f);
		EmitInfinityBand(pGraphics, Visual.m_Position, Scale, aColors, aCurvature, 0.0f, Core, CoreAlpha, CoreAlpha);
		pGraphics->SetColor(ui_token::color::NEUTRAL_WHITE);
		pGraphics->QuadsEnd();
	}

	inline void Render(IGraphics *pGraphics, const SQmHookCountdownVisual &Visual, float PixelSize)
	{
		if(Visual.m_Alpha <= 0.0f)
			return;
		if(Visual.m_EndlessHook)
		{
			// 两种无限钩样式共用彩虹流动色、呼吸相位与光晕层，只有形状不同。
			if(EndlessHookUsesRing())
				RenderEndlessRing(pGraphics, Visual, PixelSize);
			else
				RenderEndlessInfinity(pGraphics, Visual, PixelSize);
			return;
		}
		const float SizeScale = CountdownSizeScale();
		const float InnerRadius = INNER_RADIUS * Visual.m_Scale * SizeScale;
		const float OuterRadius = OUTER_RADIUS * Visual.m_Scale * SizeScale;
		const float Outline = std::min(PixelSize * 0.7f, InnerRadius * 0.25f);
		DrawArc(pGraphics, Visual.m_Position, InnerRadius - Outline, OuterRadius + Outline, 1.0f, ColorRGBA(0.04f, 0.07f, 0.11f, Visual.m_Alpha * 0.45f), PixelSize);
		DrawArc(pGraphics, Visual.m_Position, InnerRadius, OuterRadius, 1.0f, Visual.m_Color.WithAlpha(Visual.m_Alpha * 0.20f), PixelSize);
		DrawProgressArc(pGraphics, Visual.m_Position, InnerRadius, OuterRadius, Visual.m_Progress, Visual.m_Color.WithAlpha(Visual.m_Alpha), PixelSize);
	}
}

#endif
