/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_UIDISCRETESLIDER_H
#define GAME_CLIENT_QMUI_UIDISCRETESLIDER_H

#include <base/color.h>

#include <game/client/ui_rect.h>

struct IUiContext;

namespace ui_widget
{
	struct SDiscreteSliderGeometry
	{
		CUIRect m_Track{};
		float m_TravelStart = 0.0f;
		float m_TravelWidth = 0.0f;
		float m_KnobSize = 0.0f;
		float m_DotSize = 0.0f;

		bool IsUsable() const { return m_TravelWidth > 0.0f && m_Track.h > 0.0f; }
		float Position(float Normalized) const;
		CUIRect KnobRect(float Normalized, float Emphasis = 0.0f) const;
	};

	struct SDiscreteSliderState
	{
		float m_GrabOffset = 0.0f;
	};

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

	struct SDiscreteSliderInput
	{
		float m_MouseX = 0.0f;
		bool m_Pressed = false;
		bool m_Down = false;
		bool m_Hovered = false;
		bool m_Active = false;
		bool m_CanActivate = true;
		bool m_Enabled = true;
	};

	struct SDiscreteSliderResult
	{
		int m_Value = 0;
		bool m_Changed = false;
		bool m_Active = false;
	};

	// UiScale 是调用方的布局缩放（Ctx.m_UiScale），不是像素缩放：Rect 已按布局单位给出，
	// 缩放只影响旋钮/圆点的基准尺寸，且始终受 Rect 收边约束。
	SDiscreteSliderGeometry ResolveDiscreteSliderGeometry(const CUIRect &Rect, float UiScale = 1.0f);
	float DiscreteSliderNormalizedValue(int Value, int Min, int Max);
	SDiscreteSliderStyle ResolveDiscreteSliderStyle(int Value, int Min, int Max);
	SDiscreteSliderParticle ResolveDiscreteSliderParticle(const CUIRect &Fill, int Index, float Time);
	SDiscreteSliderResult UpdateDiscreteSlider(SDiscreteSliderState &State, const SDiscreteSliderGeometry &Geometry, const SDiscreteSliderInput &Input, int Value, int Min, int Max);

	// 越界值表示未选中单一档位，只有用户选档才写回；拖动跨档时立即返回 true。
	bool DiscreteSlider(const IUiContext &Ctx, const void *pId, SDiscreteSliderState &State, int *pValue, int Min, int Max, const CUIRect &Rect);
}

#endif // GAME_CLIENT_QMUI_UIDISCRETESLIDER_H
