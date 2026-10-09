/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "UiSurface.h"

#include "QmColorGradient.h"

#include <engine/graphics.h>

#include <game/client/ui.h>

#include <algorithm>

bool DrawRoundedSurface(IGraphics *pGraphics, const CUIRect &Rect, const ColorRGBA &Fill, const ColorRGBA &Border, const SRoundedSurfaceParams &Params)
{
	if(pGraphics == nullptr || Rect.w <= 0.0f || Rect.h <= 0.0f)
		return false;

	const SRoundedSurfacePlan Plan = ResolveRoundedSurfacePlan(Rect, Params, pGraphics->HasRoundedRectSdf());
	if(Plan.m_UseSdf)
	{
		IGraphics::SRoundedRectSdfParams SdfParams;
		SdfParams.m_Rect = vec4(Plan.m_Rect.x, Plan.m_Rect.y, Plan.m_Rect.w, Plan.m_Rect.h);
		SdfParams.m_FillColor = vec4(Fill.r, Fill.g, Fill.b, Fill.a);
		const ColorRGBA ResolvedBorder = Plan.m_BorderWidth > 0.0f ? Border : Fill;
		SdfParams.m_BorderColor = vec4(ResolvedBorder.r, ResolvedBorder.g, ResolvedBorder.b, ResolvedBorder.a);
		SdfParams.m_CornerRadii = Plan.m_CornerRadii;
		SdfParams.m_Params = vec4(Plan.m_BorderWidth, Plan.m_PixelSize, Plan.m_PixelSize * 2.0f, 0.0f);
		pGraphics->RenderRoundedRectSdf(SdfParams);
		return true;
	}

	if(Plan.m_BorderWidth <= 0.0f)
	{
		pGraphics->DrawRect(Plan.m_Rect.x, Plan.m_Rect.y, Plan.m_Rect.w, Plan.m_Rect.h, Fill, Params.m_Corners, Plan.m_Radius);
		return true;
	}

	pGraphics->DrawRect(Plan.m_Rect.x, Plan.m_Rect.y, Plan.m_Rect.w, Plan.m_Rect.h, Border, Params.m_Corners, Plan.m_Radius);
	CUIRect Inner = Plan.m_Rect;
	Inner.Margin(Plan.m_BorderWidth, &Inner);
	if(Inner.w > 0.0f && Inner.h > 0.0f)
		pGraphics->DrawRect(Inner.x, Inner.y, Inner.w, Inner.h, Fill, Params.m_Corners, std::max(0.0f, Plan.m_Radius - Plan.m_BorderWidth));
	return true;
}

bool DrawRoundedSurface(CUi *pUi, const CUIRect &Rect, const ColorRGBA &Fill, const ColorRGBA &Border, const float Radius, const float BorderWidth, const int Corners)
{
	if(pUi == nullptr)
		return false;
	SRoundedSurfaceParams Params;
	Params.m_Radius = Radius;
	Params.m_BorderWidth = BorderWidth;
	Params.m_PixelSize = pUi->PixelSize();
	Params.m_Corners = Corners;
	return DrawRoundedSurface(pUi->Graphics(), Rect, Fill, Border, Params);
}

bool DrawRoundedSurface(const IUiContext &Ctx, const CUIRect &Rect, const ColorRGBA &Fill, const ColorRGBA &Border, const float Radius, const float BorderWidth, const int Corners)
{
	return DrawRoundedSurface(Ctx.m_pUi, Rect, Fill, Border, Radius, BorderWidth, Corners);
}

// 固定网格一次提交顶点；预览和候选栏共享五种渐变，不生成每帧纹理。
bool DrawRoundedGradientSurface(IGraphics *pGraphics, const CUIRect &Rect, const SQmColorGradient &Gradient,
	float Alpha, const ColorRGBA &Border, const SRoundedSurfaceParams &Params)
{
	if(pGraphics == nullptr || Rect.w <= 0.0f || Rect.h <= 0.0f || Alpha <= 0.0f)
		return false;
	if(Gradient.m_NumColors <= 1)
	{
		ColorRGBA Fill = Gradient.m_aColors[0];
		Fill.a *= Alpha;
		return DrawRoundedSurface(pGraphics, Rect, Fill, Border, Params);
	}
	const SRoundedSurfacePlan Plan = ResolveRoundedSurfacePlan(Rect, Params, pGraphics->HasRoundedRectSdf());
	CUIRect Inner = Plan.m_Rect;
	if(Plan.m_BorderWidth > 0.0f)
	{
		SRoundedSurfaceParams BorderParams = Params;
		BorderParams.m_BorderWidth = 0.0f;
		DrawRoundedSurface(pGraphics, Inner, Border, ColorRGBA(), BorderParams);
		Inner.Margin(Plan.m_BorderWidth, &Inner);
	}
	if(Inner.w <= 0.0f || Inner.h <= 0.0f)
		return true;
	const float Radius = std::clamp(Plan.m_Radius - Plan.m_BorderWidth, 0.0f, std::min(Inner.w, Inner.h) * 0.5f);
	constexpr int COLUMNS = 32;
	constexpr int ROWS = 16;
	const auto RoundedCorner = [&](float Y, bool Right) {
		return Radius > 0.0f && ((Y < Radius && (Params.m_Corners & (Right ? IGraphics::CORNER_TR : IGraphics::CORNER_TL))) ||
			(Y > Inner.h - Radius && (Params.m_Corners & (Right ? IGraphics::CORNER_BR : IGraphics::CORNER_BL))));
	};
	const auto Edge = [&](float Y, bool Right) {
		float Inset = 0.0f;
		if(RoundedCorner(Y, Right))
		{
			const float Distance = Y < Radius ? Radius - Y : std::max(0.0f, Y - (Inner.h - Radius));
			Inset = Radius - std::sqrt(std::max(0.0f, Radius * Radius - Distance * Distance));
		}
		return vec2(Inner.x + (Right ? Inner.w - Inset : Inset), Inner.y + Y);
	};
	const auto Normal = [&](vec2 Point, bool Right) {
		const float LocalY = Point.y - Inner.y;
		if(!RoundedCorner(LocalY, Right))
			return vec2(Right ? 1.0f : -1.0f, 0.0f);
		const vec2 Center(Inner.x + (Right ? Inner.w - Radius : Radius),
			Inner.y + (LocalY < Radius ? Radius : Inner.h - Radius));
		return (Point - Center) / Radius;
	};
	const auto DrawQuad = [&](vec2 TopLeft, vec2 TopRight, vec2 BottomLeft, vec2 BottomRight, bool FadeTop = false, bool FadeBottom = false) {
		const std::array<vec2, 4> aPoints = {TopLeft, TopRight, BottomLeft, BottomRight};
		std::array<IGraphics::CColorVertex, 4> aColors;
		for(int i = 0; i < 4; ++i)
		{
			ColorRGBA Color = Gradient.Sample((aPoints[i] - Inner.TopLeft()) / Inner.Size());
			Color.a *= Alpha;
			if((FadeTop && i < 2) || (FadeBottom && i >= 2))
				Color.a = 0.0f;
			aColors[i] = IGraphics::CColorVertex(i, Color);
		}
		pGraphics->SetColorVertex(aColors.data(), aColors.size());
		const IGraphics::CFreeformItem Quad(TopLeft, TopRight, BottomLeft, BottomRight);
		pGraphics->QuadsDrawFreeform(&Quad, 1);
	};
	pGraphics->TextureClear();
	pGraphics->QuadsBegin();
	for(int Row = 0; Row < ROWS; ++Row)
	{
		const vec2 LeftTop = Edge(Inner.h * Row / ROWS, false);
		const vec2 RightTop = Edge(Inner.h * Row / ROWS, true);
		const vec2 LeftBottom = Edge(Inner.h * (Row + 1) / ROWS, false);
		const vec2 RightBottom = Edge(Inner.h * (Row + 1) / ROWS, true);
		for(int Column = 0; Column < COLUMNS; ++Column)
		{
			const float T0 = static_cast<float>(Column) / COLUMNS;
			const float T1 = static_cast<float>(Column + 1) / COLUMNS;
			DrawQuad(mix(LeftTop, RightTop, T0), mix(LeftTop, RightTop, T1),
				mix(LeftBottom, RightBottom, T0), mix(LeftBottom, RightBottom, T1));
		}
		// 与填充使用同一边界节点，透明外缘不会在圆角处留下裂缝。
		DrawQuad(LeftTop, LeftBottom,
			LeftTop + Normal(LeftTop, false) * Plan.m_PixelSize, LeftBottom + Normal(LeftBottom, false) * Plan.m_PixelSize, false, true);
		DrawQuad(RightTop, RightBottom, RightTop + Normal(RightTop, true) * Plan.m_PixelSize,
			RightBottom + Normal(RightBottom, true) * Plan.m_PixelSize, false, true);
	}
	const vec2 TopLeft = Edge(0.0f, false), TopRight = Edge(0.0f, true);
	const vec2 BottomLeft = Edge(Inner.h, false), BottomRight = Edge(Inner.h, true);
	DrawQuad(TopLeft - vec2(0.0f, Plan.m_PixelSize), TopRight - vec2(0.0f, Plan.m_PixelSize), TopLeft, TopRight, true);
	DrawQuad(BottomLeft, BottomRight, BottomLeft + vec2(0.0f, Plan.m_PixelSize), BottomRight + vec2(0.0f, Plan.m_PixelSize), false, true);
	pGraphics->QuadsEnd();
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	return true;
}
