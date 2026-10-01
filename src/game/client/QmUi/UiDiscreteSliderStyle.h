// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_UIDISCRETESLIDERSTYLE_H
#define GAME_CLIENT_QMUI_UIDISCRETESLIDERSTYLE_H

#include <base/color.h>

#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>

namespace ui_widget
{
	struct SDiscreteSliderStyle
	{
		ColorRGBA m_Color{};
		ColorRGBA m_GradientStart{};
		ColorRGBA m_GradientMiddle{};
		ColorRGBA m_GradientEnd{};
		bool m_Gradient = false;
		int m_ParticleCount = 0;
	};

	struct SDiscreteSliderParticle
	{
		CUIRect m_Rect{};
		float m_Alpha = 0.0f;
	};

	inline SDiscreteSliderStyle ResolveDiscreteSliderStyle(const float Normalized)
	{
		SDiscreteSliderStyle Style;
		const float Clamped = std::clamp(Normalized, 0.0f, 1.0f);

		// 低档从淡蓝色开始，档位升高时经过蓝紫、粉紫，最高档使用亮紫色高光。
		const ColorRGBA aColors[] = {
			ColorRGBA(0.48f, 0.67f, 0.91f, 1.0f),
			ColorRGBA(0.65f, 0.54f, 0.91f, 1.0f),
			ColorRGBA(0.84f, 0.48f, 0.78f, 1.0f),
			ColorRGBA(0.92f, 0.45f, 0.69f, 1.0f)};
		const float Position = std::min(Clamped * 3.6f, 3.0f);
		const int Index = std::min(static_cast<int>(Position), 2);
		const float Mix = Position - Index;
		Style.m_Color = ColorRGBA(
			aColors[Index].r * (1.0f - Mix) + aColors[Index + 1].r * Mix,
			aColors[Index].g * (1.0f - Mix) + aColors[Index + 1].g * Mix,
			aColors[Index].b * (1.0f - Mix) + aColors[Index + 1].b * Mix,
			1.0f);

		Style.m_Gradient = Clamped >= 0.999f;
		Style.m_GradientStart = ColorRGBA(0.51f, 0.16f, 0.52f, 1.0f);
		Style.m_GradientMiddle = ColorRGBA(0.77f, 0.53f, 0.97f, 1.0f);
		Style.m_GradientEnd = ColorRGBA(0.57f, 0.32f, 0.79f, 1.0f);
		if(Style.m_Gradient)
			Style.m_Color = Style.m_GradientMiddle;

		if(Clamped >= 0.8f)
			Style.m_ParticleCount = 14;
		return Style;
	}

	inline SDiscreteSliderParticle ResolveDiscreteSliderParticle(const CUIRect &Fill, const int Index, const float Time)
	{
		SDiscreteSliderParticle Particle;
		if(Index < 0 || Fill.h <= 0.0f || Fill.w <= Fill.h)
			return Particle;

		const float Seed = std::fmod((Index + 1) * 0.618034f, 1.0f);
		const float Size = Fill.h * (0.045f + 0.035f * Seed);
		const float Padding = Fill.h * 0.5f + Size;
		if(Fill.w <= Padding * 2.0f)
			return Particle;

		const float Phase = std::max(0.0f, Time);
		const float X = std::fmod(Seed + Phase * 0.035f, 1.0f);
		const float Y = std::fmod((Index + 1) * 0.414214f + Phase * 0.018f, 1.0f);
		Particle.m_Rect = {Fill.x + Padding + X * (Fill.w - Padding * 2.0f) - Size * 0.5f, Fill.y + Size + Y * (Fill.h - Size * 3.0f), Size, Size};
		Particle.m_Alpha = 0.25f + 0.60f * (0.5f + 0.5f * std::sin(Phase * 2.4f + Seed * 18.0f));
		return Particle;
	}
} // namespace ui_widget

#endif // GAME_CLIENT_QMUI_UIDISCRETESLIDERSTYLE_H
