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
#include <array>
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
	// SDF 边框默认取单色 BorderColor；置位后边框改为沿用填充的渐变，
	// 用于彩虹描边这类需要沿边框逐像素取色的效果。几何回退路径忽略此项。
	bool m_BorderUsesGradient = false;
	// 只画边框、丢弃填充：渐变描边（彩虹光环）需要它，否则渐变会把内部整片染色。
	bool m_BorderOnly = false;
	// 边框沿带宽的羽化指数：0 为实心边框，越大越向外淡出。
	// 辉光类描边（彩虹光环）靠它把多层叠成渐变，而不是三道硬边。
	float m_BorderFalloff = 0.0f;
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

// 固定大小环带网格，不分配内存；供几何回退、行为测试和基准共用。
inline std::array<IGraphics::CFreeformItem, 36> ResolveRoundedSurfaceBorderMesh(const SRoundedSurfacePlan &Plan, int Corners)
{
	static const std::array<vec2, 9> s_aDirections = [] {
		std::array<vec2, 9> aDirections;
		for(int i = 0; i <= 8; ++i)
		{
			const float Angle = i * pi / 16.0f;
			aDirections[i] = vec2(std::cos(Angle), std::sin(Angle));
		}
		aDirections.front() = vec2(1, 0);
		aDirections.back() = vec2(0, 1);
		return aDirections;
	}();
	const CUIRect Inner{Plan.m_Rect.x + Plan.m_BorderWidth, Plan.m_Rect.y + Plan.m_BorderWidth,
		Plan.m_Rect.w - 2.0f * Plan.m_BorderWidth, Plan.m_Rect.h - 2.0f * Plan.m_BorderWidth};
	std::array<vec2, 36> aOuter, aInner;
	const int aCornerFlags[] = {IGraphics::CORNER_TL, IGraphics::CORNER_TR, IGraphics::CORNER_BR, IGraphics::CORNER_BL};
	for(int Corner = 0; Corner < 4; ++Corner)
	{
		const bool Right = Corner == 1 || Corner == 2;
		const bool Bottom = Corner >= 2;
		const float Radius = Corners & aCornerFlags[Corner] ? Plan.m_Radius : 0.0f;
		const float InnerRadius = std::max(0.0f, Radius - Plan.m_BorderWidth);
		const vec2 OuterCenter(Plan.m_Rect.x + (Right ? Plan.m_Rect.w - Radius : Radius), Plan.m_Rect.y + (Bottom ? Plan.m_Rect.h - Radius : Radius));
		const vec2 InnerCenter(Inner.x + (Right ? Inner.w - InnerRadius : InnerRadius), Inner.y + (Bottom ? Inner.h - InnerRadius : InnerRadius));
		for(int i = 0; i <= 8; ++i)
		{
			const vec2 D = s_aDirections[i];
			const vec2 Direction = Corner == 0 ? vec2(-D.x, -D.y) : Corner == 1 ? vec2(D.y, -D.x) :
									Corner == 2         ? D :
											      vec2(-D.y, D.x);
			aOuter[Corner * 9 + i] = OuterCenter + Direction * Radius;
			aInner[Corner * 9 + i] = InnerCenter + Direction * InnerRadius;
		}
	}
	std::array<IGraphics::CFreeformItem, 36> aMesh;
	for(size_t i = 0; i < aMesh.size(); ++i)
	{
		const size_t Next = (i + 1) % aMesh.size();
		aMesh[i] = IGraphics::CFreeformItem(aOuter[i], aOuter[Next], aInner[i], aInner[Next]);
	}
	return aMesh;
}
bool DrawRoundedGradientSurface(IGraphics *pGraphics, const CUIRect &Rect, const SQmColorGradient &Gradient, float Alpha, const ColorRGBA &Border, const SRoundedSurfaceParams &Params);
bool DrawRoundedSurface(IGraphics *pGraphics, const CUIRect &Rect, const ColorRGBA &Fill, const ColorRGBA &Border, const SRoundedSurfaceParams &Params);
bool DrawRoundedSurface(CUi *pUi, const CUIRect &Rect, const ColorRGBA &Fill, const ColorRGBA &Border, float Radius, float BorderWidth = 0.0f, int Corners = IGraphics::CORNER_ALL);
bool DrawRoundedSurface(const IUiContext &Ctx, const CUIRect &Rect, const ColorRGBA &Fill, const ColorRGBA &Border, float Radius, float BorderWidth = 0.0f, int Corners = IGraphics::CORNER_ALL);

#endif
