#include "UiDiscreteSlider.h"

#include <algorithm>
#include <cmath>

namespace ui_widget
{
	namespace
	{
		int ValueAt(const SDiscreteSliderGeometry &Geometry, float MouseX, int Min, int Max)
		{
			const float Normalized = std::clamp((MouseX - Geometry.m_TravelStart) / Geometry.m_TravelWidth, 0.0f, 1.0f);
			const double Offset = std::round(Normalized * (static_cast<double>(Max) - Min));
			return static_cast<int>(std::clamp(Min + Offset, static_cast<double>(Min), static_cast<double>(Max)));
		}
	}

	float SDiscreteSliderGeometry::Position(float Normalized) const
	{
		return m_TravelStart + m_TravelWidth * std::clamp(Normalized, 0.0f, 1.0f);
	}

	CUIRect SDiscreteSliderGeometry::KnobRect(float Normalized, float Emphasis) const
	{
		const float Size = m_KnobSize * (1.0f + 0.12f * std::clamp(Emphasis, 0.0f, 1.0f));
		return {Position(Normalized) - Size * 0.5f, m_Track.y + (m_Track.h - Size) * 0.5f, Size, Size};
	}

	SDiscreteSliderGeometry ResolveDiscreteSliderGeometry(const CUIRect &Rect, float UiScale)
	{
		SDiscreteSliderGeometry Geometry;
		if(Rect.w <= 0.0f || Rect.h <= 0.0f)
			return Geometry;

		const float Scale = std::max(UiScale, 0.1f);
		Geometry.m_KnobSize = std::min(18.0f * Scale, Rect.h / 1.12f);
		// 两端预留悬停放大后的半径，绘制与鼠标换算共用圆心行程。
		const float Padding = Geometry.m_KnobSize * 0.56f;
		const float TrackHeight = Geometry.m_KnobSize * (6.0f / 7.0f);
		Geometry.m_TravelStart = Rect.x + Padding;
		Geometry.m_TravelWidth = std::max(0.0f, Rect.w - Padding * 2.0f);
		// 胶囊延伸到端点旋钮外缘，圆点与鼠标换算仍使用圆心行程。
		const float TrackInset = Padding - Geometry.m_KnobSize * 0.5f;
		Geometry.m_Track = {Rect.x + TrackInset, Rect.y + (Rect.h - TrackHeight) * 0.5f, std::max(0.0f, Rect.w - TrackInset * 2.0f), TrackHeight};
		Geometry.m_DotSize = std::min(2.5f * Scale, Geometry.m_KnobSize / 7.0f);
		return Geometry;
	}

	float DiscreteSliderNormalizedValue(int Value, int Min, int Max)
	{
		if(Max <= Min)
			return 0.0f;
		return std::clamp((static_cast<float>(Value) - Min) / (static_cast<float>(Max) - Min), 0.0f, 1.0f);
	}

	SDiscreteSliderStyle ResolveDiscreteSliderStyle(int Value, int Min, int Max)
	{
		SDiscreteSliderStyle Style;
		// 低档冷色逐步过渡到高档粉色，最高档采用独立紫色高光。
		const ColorRGBA aColors[] = {
			ColorRGBA(0.48f, 0.67f, 0.91f, 1.0f),
			ColorRGBA(0.65f, 0.54f, 0.91f, 1.0f),
			ColorRGBA(0.84f, 0.48f, 0.78f, 1.0f),
			ColorRGBA(0.92f, 0.45f, 0.69f, 1.0f)};
		const float Position = std::min(DiscreteSliderNormalizedValue(Value, Min, Max) * 3.6f, 3.0f);
		const int Index = std::min(static_cast<int>(Position), 2);
		const float Mix = Position - Index;
		Style.m_Color = ColorRGBA(
			aColors[Index].r * (1.0f - Mix) + aColors[Index + 1].r * Mix,
			aColors[Index].g * (1.0f - Mix) + aColors[Index + 1].g * Mix,
			aColors[Index].b * (1.0f - Mix) + aColors[Index + 1].b * Mix, 1.0f);
		Style.m_Gradient = Max > Min && Value == Max;
		Style.m_GradientStart = ColorRGBA(0.51f, 0.16f, 0.52f, 1.0f);
		Style.m_GradientMiddle = ColorRGBA(0.77f, 0.53f, 0.97f, 1.0f);
		Style.m_GradientEnd = ColorRGBA(0.57f, 0.32f, 0.79f, 1.0f);
		if(Style.m_Gradient)
			Style.m_Color = Style.m_GradientMiddle;
		if(Max > Min && Value > Min && Value <= Max && DiscreteSliderNormalizedValue(Value, Min, Max) >= 0.8f)
			Style.m_ParticleCount = 14;
		return Style;
	}

	SDiscreteSliderParticle ResolveDiscreteSliderParticle(const CUIRect &Fill, int Index, float Time)
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

	SDiscreteSliderResult UpdateDiscreteSlider(SDiscreteSliderState &State, const SDiscreteSliderGeometry &Geometry, const SDiscreteSliderInput &Input, int Value, int Min, int Max)
	{
		SDiscreteSliderResult Result;
		Result.m_Value = Value;
		if(!Input.m_Enabled || !Geometry.IsUsable() || Max <= Min)
		{
			State.m_GrabOffset = 0.0f;
			return Result;
		}

		const bool Pressed = Input.m_Pressed && Input.m_Down && Input.m_Hovered && Input.m_CanActivate;
		if(!Input.m_Active && !Pressed)
		{
			State.m_GrabOffset = 0.0f;
			return Result;
		}

		if(!Input.m_Active)
		{
			const float Normalized = DiscreteSliderNormalizedValue(Value, Min, Max);
			const float KnobCenter = Geometry.Position(Normalized);
			// 窄轨道上旋钮可能盖住相邻刻度，点击更近的另一档时仍按轨道选档。
			const bool OnKnob = Value >= Min && Value <= Max && ValueAt(Geometry, Input.m_MouseX, Min, Max) == Value && std::abs(Input.m_MouseX - KnobCenter) <= Geometry.KnobRect(Normalized, 1.0f).w * 0.5f;
			State.m_GrabOffset = OnKnob ? Input.m_MouseX - KnobCenter : 0.0f;
		}

		// 保留按住旋钮时的抓取偏移；点击轨道则直接选最近档位，无须先悬停一帧。
		Result.m_Value = ValueAt(Geometry, Input.m_MouseX - State.m_GrabOffset, Min, Max);
		Result.m_Changed = Result.m_Value != Value;
		Result.m_Active = Input.m_Down;
		if(!Result.m_Active)
			State.m_GrabOffset = 0.0f;
		return Result;
	}
}
