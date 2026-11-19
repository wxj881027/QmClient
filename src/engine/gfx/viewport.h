#ifndef ENGINE_GFX_VIEWPORT_H
#define ENGINE_GFX_VIEWPORT_H

#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace GraphicsViewport
{
	inline bool IsValidScreen(const std::array<float, 4> &aScreen)
	{
		for(float Value : aScreen)
			if(!std::isfinite(Value))
				return false;
		const float Width = aScreen[2] - aScreen[0];
		const float Height = aScreen[3] - aScreen[1];
		return Width > 0.0f && Height > 0.0f && std::isfinite(Width) && std::isfinite(Height);
	}

	// 原 float 投影无效时才调用；完整候选可表示后再交付，不能靠截断端点修复几何。
	inline std::optional<std::array<float, 4>> RecoverWorldScreen(float BaseWidth, float BaseHeight, float CenterX, float CenterY,
		float ParallaxX, float ParallaxY, float ParallaxZoom, float OffsetX, float OffsetY, float Zoom)
	{
		const float aInput[] = {BaseWidth, BaseHeight, CenterX, CenterY, ParallaxX, ParallaxY, ParallaxZoom, OffsetX, OffsetY, Zoom};
		for(float Value : aInput)
			if(!std::isfinite(Value))
				return std::nullopt;
		if(!(BaseWidth > 0.0f) || !(BaseHeight > 0.0f) || !(Zoom > 0.0f))
			return std::nullopt;
		const double Factor = static_cast<double>(ParallaxZoom) / 100.0;
		// 与 zoom * scale 等价，避免先放大溢出，也避免小 zoom 在减一/加一中消失。
		const double EffectiveZoom = Factor * Zoom + (1.0 - Factor);
		if(!(EffectiveZoom > 0.0))
			return std::nullopt;
		const double Width = BaseWidth * EffectiveZoom;
		const double Height = BaseHeight * EffectiveZoom;
		std::array<double, 4> aWide;
		aWide[0] = OffsetX + static_cast<double>(CenterX) * ParallaxX / 100.0 - Width / 2.0;
		aWide[1] = OffsetY + static_cast<double>(CenterY) * ParallaxY / 100.0 - Height / 2.0;
		aWide[2] = aWide[0] + Width;
		aWide[3] = aWide[1] + Height;
		std::array<float, 4> aResult;
		constexpr double Max = std::numeric_limits<float>::max();
		for(int i = 0; i < 4; ++i)
		{
			if(!std::isfinite(aWide[i]) || aWide[i] < -Max || aWide[i] > Max)
				return std::nullopt;
			aResult[i] = static_cast<float>(aWide[i]);
		}
		return IsValidScreen(aResult) ? std::make_optional(aResult) : std::nullopt;
	}
}

#endif // ENGINE_GFX_VIEWPORT_H
