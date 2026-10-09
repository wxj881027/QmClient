/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_UISURFACE_H
#define GAME_CLIENT_QMUI_UISURFACE_H

#include "UiContext.h"

#include <base/color.h>

#include <engine/client/rounded_rect_geometry.h>
#include <engine/graphics.h>

#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>

class CUi;
struct SQmColorGradient;

// 普通 UI 圆角必须走 DrawRoundedSurface / CUIRect::Draw 公共路径。
// 旧直绘路径仅允许用于高频 HUD/聊天/名牌/观战/统计板渲染。
struct SRoundedSurfaceParams
{
	float m_Radius = 0.0f;
	float m_BorderWidth = 0.0f;
	float m_PixelSize = 0.0001f;
	int m_Corners = IGraphics::CORNER_ALL;
};

struct SRoundedSurfacePlan
{
	CUIRect m_Rect{};
	float m_Radius = 0.0f;
	float m_BorderWidth = 0.0f;
	float m_PixelSize = 0.0001f;
	vec4 m_CornerRadii{};
	bool m_UseSdf = false;
};

inline vec4 ResolveRoundedSurfaceCornerRadii(const float Radius, const int Corners)
{
	return vec4(
		Corners & IGraphics::CORNER_TL ? Radius : 0.0f,
		Corners & IGraphics::CORNER_TR ? Radius : 0.0f,
		Corners & IGraphics::CORNER_BR ? Radius : 0.0f,
		Corners & IGraphics::CORNER_BL ? Radius : 0.0f);
}

inline SRoundedSurfacePlan ResolveRoundedSurfacePlan(const CUIRect &Rect, const SRoundedSurfaceParams &Params, const bool HasSdf)
{
	SRoundedSurfacePlan Plan;
	Plan.m_PixelSize = std::max(Params.m_PixelSize, 0.0001f);
	const SRoundedRectGeometry Geometry = ResolveRoundedRectGeometry(Rect.x, Rect.y, Rect.w, Rect.h, Params.m_Radius, Plan.m_PixelSize);
	Plan.m_Rect = {Geometry.m_X, Geometry.m_Y, Geometry.m_W, Geometry.m_H};
	Plan.m_Radius = Geometry.m_Rounding;
	const auto AlignToPixel = [](const float Value, const float Size) {
		return Size > 0.0f ? std::round(Value / Size) * Size : Value;
	};
	const float MaxBorderWidth = std::max(0.0f, std::min(Plan.m_Rect.w, Plan.m_Rect.h) * 0.5f);
	Plan.m_BorderWidth = std::clamp(AlignToPixel(Params.m_BorderWidth, Plan.m_PixelSize), 0.0f, MaxBorderWidth);
	Plan.m_CornerRadii = ResolveRoundedSurfaceCornerRadii(Plan.m_Radius, Params.m_Corners);
	Plan.m_UseSdf = HasSdf && Plan.m_Rect.w > 0.0f && Plan.m_Rect.h > 0.0f;
	return Plan;
}
bool DrawRoundedGradientSurface(IGraphics *pGraphics, const CUIRect &Rect, const SQmColorGradient &Gradient, float Alpha, const ColorRGBA &Border, const SRoundedSurfaceParams &Params);
bool DrawRoundedSurface(IGraphics *pGraphics, const CUIRect &Rect, const ColorRGBA &Fill, const ColorRGBA &Border, const SRoundedSurfaceParams &Params);
bool DrawRoundedSurface(CUi *pUi, const CUIRect &Rect, const ColorRGBA &Fill, const ColorRGBA &Border, float Radius, float BorderWidth = 0.0f, int Corners = IGraphics::CORNER_ALL);
bool DrawRoundedSurface(const IUiContext &Ctx, const CUIRect &Rect, const ColorRGBA &Fill, const ColorRGBA &Border, float Radius, float BorderWidth = 0.0f, int Corners = IGraphics::CORNER_ALL);

#endif
