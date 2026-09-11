// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMISLANDSURFACE_H
#define GAME_CLIENT_QMUI_QMISLANDSURFACE_H

#include <base/vmath.h>

#include <engine/graphics.h>

#include <game/client/components/hud_media_island_logic.h>

#include <algorithm>
#include <cmath>

// 灵动岛表面：把「一份 SDF 渲染状态 → 屏幕上的胶囊岛」这条路径收成一个公共组件。
//
// 与 HUD 的关系：HUD 动态岛当前仍使用自己的私有绘制路径（hud.cpp 内），
// 本组件只做新增调用方，不反向替换 HUD，避免动到守护 HUD 渲染结构的既有测试。
// 两边共用同一份状态结构（SHudMediaIslandSdfRenderState）与 GPU 参数构建
// （QmHudMediaIslandBuildGpuSdfParams），因此观感同源。
namespace qm_island
{
	// ==== 圆角矩形轮廓参数化 ====
	// 倒计时环贴的是「主体外轮廓」，胶囊只是 Radius = 半高的特例。
	// 参数从正上方中点起、顺时针计一圈，与着色器里的 RoundedRectPerimeterFraction 同语义。
	inline float ClampRoundedRectRadius(const CUIRect &Rect, float Radius)
	{
		return std::clamp(Radius, 0.0f, std::min(std::max(0.0f, Rect.w), std::max(0.0f, Rect.h)) * 0.5f);
	}

	inline float RoundedRectPerimeterLength(const CUIRect &Rect, float Radius)
	{
		constexpr float Pi = 3.14159265359f;
		const float HalfWidth = std::max(0.0f, Rect.w * 0.5f);
		const float CornerRadius = ClampRoundedRectRadius(Rect, Radius);
		return 4.0f * std::max(HalfWidth - CornerRadius, 0.0f) + 2.0f * Pi * CornerRadius;
	}

	// 轮廓上的点：Fraction ∈ [0,1) 从正上方中点起顺时针；超出范围自动环绕。
	inline vec2 RoundedRectPerimeterPoint(const CUIRect &Rect, float Radius, float Fraction)
	{
		constexpr float Pi = 3.14159265359f;
		const float HalfWidth = std::max(0.0f, Rect.w * 0.5f);
		const float HalfHeight = std::max(0.0f, Rect.h * 0.5f);
		const float CornerRadius = ClampRoundedRectRadius(Rect, Radius);
		const float StraightHalf = std::max(HalfWidth - CornerRadius, 0.0f);
		const float CapLength = Pi * CornerRadius;
		const float Perimeter = 4.0f * StraightHalf + 2.0f * CapLength;
		const float CenterX = Rect.x + HalfWidth;
		const float CenterY = Rect.y + HalfHeight;
		if(Perimeter <= 0.0001f)
			return vec2(CenterX, CenterY);

		float Along = (Fraction - std::floor(Fraction)) * Perimeter;
		// 上边右半段：直接量 x。
		if(Along <= StraightHalf)
			return vec2(CenterX + Along, CenterY - HalfHeight);
		Along -= StraightHalf;
		// 右端圆弧：从上（-90°）顺时针转到下（+90°）。
		if(Along <= CapLength)
		{
			const float Angle = Along / CornerRadius;
			return vec2(CenterX + StraightHalf + std::sin(Angle) * CornerRadius, CenterY - std::cos(Angle) * CornerRadius);
		}
		Along -= CapLength;
		// 下边：从右往左。
		if(Along <= 2.0f * StraightHalf)
			return vec2(CenterX + StraightHalf - Along, CenterY + HalfHeight);
		Along -= 2.0f * StraightHalf;
		// 左端圆弧：从下顺时针转到上。
		if(Along <= CapLength)
		{
			const float Angle = Along / CornerRadius;
			return vec2(CenterX - StraightHalf - std::sin(Angle) * CornerRadius, CenterY + std::cos(Angle) * CornerRadius);
		}
		Along -= CapLength;
		// 上边左半段：从左往中间收。
		return vec2(CenterX - StraightHalf + Along, CenterY - HalfHeight);
	}

	// 绘制一块灵动岛表面（调用方负责填好 SdfState，含 m_Rect）。
	// 优先走实时 SDF（含液体融合、轮廓环/卫星环、可选外阴影）；
	// 不支持时退回圆角矩形 + 环几何。
	// Backdrop 为 HUD 侧的背景模糊目标，菜单调用方传默认值即可（不做背景模糊）。
	bool Render(IGraphics *pGraphics, const SHudMediaIslandSdfRenderState &SdfState, IGraphics::CRenderTargetHandle Backdrop = IGraphics::CRenderTargetHandle());
} // namespace qm_island

#endif
