/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_SETTINGSCARD_H
#define GAME_CLIENT_QMUI_SETTINGSCARD_H

#include "SettingsCardGeometry.h"

#include <array>
#include <cmath>
#include <functional>

struct IUiContext;

struct SSettingsCardDeckVisualOptions
{
	bool m_RainbowTitles = false;
	bool m_AlwaysShowBorders = true;
	ColorRGBA m_BorderColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f);
	ColorRGBA m_SurfaceColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
	bool m_UseSurfaceColor = false;
};

struct SSettingsCardVisualState
{
	bool m_Hovered = false;
	bool m_PointerInside = false;
	bool m_SubtitleVisibleDuringMotion = false;
	bool m_HoverFeedbackEnabled = true;
	bool m_Focused = false;
	bool m_Dragged = false;
	bool m_Collapsed = false;
	bool m_DropFeedback = false;
	bool m_ReflowCompleteFeedback = false;
	bool m_ClipContent = false;
	bool m_ShowDefaultCollapseButton = false;
	float m_DrawOffsetX = 0.0f;
	float m_DrawOffsetY = 0.0f;
	float m_DrawAlpha = 1.0f;
};

inline bool SettingsCardSubtitleVisible(const bool HoverActive, const bool SubtitleVisibleDuringMotion, const bool Focused)
{
	return HoverActive || SubtitleVisibleDuringMotion || Focused;
}

inline bool ResolveSettingsCardSubtitleMotionLatch(const bool PointerInsideCurrentFrame, const bool MotionActive, const bool MotionWasActive, const bool VisibleDuringMotion)
{
	if(!MotionActive)
		return false;
	if(!MotionWasActive)
		return PointerInsideCurrentFrame;
	return VisibleDuringMotion;
}

inline bool SettingsCardDeckGeometryMoved(const bool Initialized, const float LastY, const float LastHeight, const float CurrentY, const float CurrentHeight)
{
	return Initialized && (std::abs(LastY - CurrentY) > 0.001f || std::abs(LastHeight - CurrentHeight) > 0.001f);
}

inline bool SettingsCardInteractionBorderVisible(const SSettingsCardVisualState &State)
{
	return State.m_Focused || State.m_Dragged || State.m_DropFeedback;
}

inline bool SettingsCardShouldDrawChrome(const bool RenderOnly)
{
	return !RenderOnly;
}

template<typename TDrawSurface, typename TDrawBorderedSurface>
inline void ExecuteSettingsCardChromeDraw(const bool DrawChrome, const bool DrawBorder, TDrawSurface &&DrawSurface, TDrawBorderedSurface &&DrawBorderedSurface)
{
	if(!DrawChrome)
		return;
	if(DrawBorder)
		DrawBorderedSurface();
	else
		DrawSurface();
}

inline float AlignSettingsCardValueToPixels(const float Value, const float PixelSize)
{
	return PixelSize > 0.0f ? std::round(Value / PixelSize) * PixelSize : Value;
}

inline float ResolveSettingsCardBorderWidth(const float UiScale, const float PixelSize = 0.0f)
{
	return AlignSettingsCardValueToPixels(std::max(2.0f, 2.0f * std::max(0.0f, UiScale)), PixelSize);
}

inline CUIRect ResolveSettingsCardChromeRect(const CUIRect &Rect, const float PixelSize)
{
	if(PixelSize <= 0.0f)
		return Rect;
	const float Left = AlignSettingsCardValueToPixels(Rect.x, PixelSize);
	const float Top = AlignSettingsCardValueToPixels(Rect.y, PixelSize);
	const float Right = AlignSettingsCardValueToPixels(Rect.x + Rect.w, PixelSize);
	const float Bottom = AlignSettingsCardValueToPixels(Rect.y + Rect.h, PixelSize);
	return {Left, Top, std::max(0.0f, Right - Left), std::max(0.0f, Bottom - Top)};
}

inline CUIRect ResolveSettingsFocusSafeClipRect(const CUIRect &Rect, const float UiScale)
{
	const float Scale = std::max(0.1f, UiScale);
	const float FocusOutset = std::max(1.0f, 2.0f * Scale);
	CUIRect Expanded = Rect;
	Expanded.x -= FocusOutset;
	Expanded.y -= FocusOutset;
	Expanded.w += FocusOutset * 2.0f;
	Expanded.h += FocusOutset * 2.0f;
	return Expanded;
}

inline CUIRect ResolveSettingsCardContentClipRect(const CUIRect &ContentRect, const CUIRect &CardRect, const float UiScale)
{
	const CUIRect Expanded = ResolveSettingsFocusSafeClipRect(ContentRect, UiScale);
	return Expanded.Intersection(CardRect);
}

inline CUIRect ResolveSettingsCardInteractionBorderRect(const CUIRect &SurfaceRect, const float BorderWidth)
{
	const float MaxInset = std::max(0.0f, std::min(SurfaceRect.w, SurfaceRect.h) * 0.5f);
	const float Inset = std::clamp(std::max(0.0f, BorderWidth) * 0.5f, 0.0f, MaxInset);
	return {SurfaceRect.x + Inset, SurfaceRect.y + Inset, std::max(0.0f, SurfaceRect.w - 2.0f * Inset), std::max(0.0f, SurfaceRect.h - 2.0f * Inset)};
}

inline SSettingsCardFrame ResolveSettingsCardDrawFrame(SSettingsCardFrame Frame, const float OffsetX, const float OffsetY)
{
	for(CUIRect *pRect : {&Frame.m_Rect, &Frame.m_HeaderRect, &Frame.m_TitleRect, &Frame.m_SubtitleRect, &Frame.m_HandleRect, &Frame.m_ContentRect})
	{
		pRect->x += OffsetX;
		pRect->y += OffsetY;
	}
	return Frame;
}

inline ColorRGBA ResolveSettingsCardSurfaceColor(ColorRGBA Surface, const SSettingsCardVisualState &State)
{
	// Hover、焦点与拖放反馈只属于边框；卡片内部仅跟随整个 Deck 的绘制透明度。
	Surface.a *= State.m_DrawAlpha;
	return Surface;
}

inline ColorRGBA ResolveSettingsCardInnerSurfaceColor(const ColorRGBA Surface, ColorRGBA Border)
{
	const float SurfaceAlpha = std::clamp(Surface.a, 0.0f, 1.0f);
	Border.a = std::clamp(Border.a, 0.0f, std::max(0.0f, SurfaceAlpha - 0.001f));
	if(Border.a <= 0.0f || SurfaceAlpha >= 0.999f)
		return Surface;
	const float InnerAlpha = (SurfaceAlpha - Border.a) / (1.0f - Border.a);
	if(InnerAlpha <= 0.001f)
		return Surface;
	const float BorderContribution = Border.a * (1.0f - InnerAlpha);
	return ColorRGBA(
		std::clamp((Surface.r * SurfaceAlpha - Border.r * BorderContribution) / InnerAlpha, 0.0f, 1.0f),
		std::clamp((Surface.g * SurfaceAlpha - Border.g * BorderContribution) / InnerAlpha, 0.0f, 1.0f),
		std::clamp((Surface.b * SurfaceAlpha - Border.b * BorderContribution) / InnerAlpha, 0.0f, 1.0f),
		InnerAlpha);
}

inline ColorRGBA ResolveSettingsCardEffectiveBorderColor(ColorRGBA Border, const ColorRGBA Surface)
{
	const float SurfaceAlpha = std::clamp(Surface.a, 0.0f, 1.0f);
	float MaxBorderAlpha = std::max(0.0f, SurfaceAlpha - 0.001f);
	const auto RestrictChannel = [&](const float SurfaceChannel, const float BorderChannel) {
		const float ClampedSurfaceChannel = std::clamp(SurfaceChannel, 0.0f, 1.0f);
		const float ClampedBorderChannel = std::clamp(BorderChannel, 0.0f, 1.0f);
		const float SurfacePremultiplied = ClampedSurfaceChannel * SurfaceAlpha;
		const float LowerDenominator = SurfacePremultiplied + ClampedBorderChannel * (1.0f - SurfaceAlpha);
		if(LowerDenominator > 0.000001f)
			MaxBorderAlpha = std::min(MaxBorderAlpha, SurfacePremultiplied / LowerDenominator);

		const float UpperNumerator = SurfaceAlpha * (1.0f - ClampedSurfaceChannel);
		const float UpperDenominator = 1.0f - SurfacePremultiplied - ClampedBorderChannel * (1.0f - SurfaceAlpha);
		if(UpperDenominator > 0.000001f)
			MaxBorderAlpha = std::min(MaxBorderAlpha, UpperNumerator / UpperDenominator);
	};
	RestrictChannel(Surface.r, Border.r);
	RestrictChannel(Surface.g, Border.g);
	RestrictChannel(Surface.b, Border.b);
	Border.a = std::clamp(Border.a, 0.0f, std::max(0.0f, MaxBorderAlpha));
	return Border;
}

using FSettingsCardMeasure = std::function<float(float ContentWidth)>;
using FSettingsCardRender = std::function<void(CUIRect ContentRect)>;
using FSettingsCardRenderMeasured = std::function<void(CUIRect &ContentRect)>;
using FSettingsCardPreLayoutInput = std::function<bool(CUIRect ContentRect)>;
using FSettingsCardHasPendingPreLayoutInput = std::function<bool()>;
using FSettingsCardPreLayoutHeaderInput = std::function<bool(const SSettingsCardFrame &Frame, bool Collapsed)>;
using FSettingsCardHeaderAction = std::function<void(const SSettingsCardFrame &Frame, bool Collapsed)>;
using FSettingsCardCollapseChanged = std::function<void(bool Collapsed)>;

void RenderSettingsCardCollapseButton(const IUiContext &Ctx, const CUIRect &Rect, bool Collapsed, float DrawAlpha = 1.0f);

SSettingsCardFrame SettingsCard(const IUiContext &Ctx, const CUIRect &Slot, const SSettingsCardSpec &Spec, const SSettingsCardVisualState &State, const SSettingsCardDeckVisualOptions &VisualOptions, const FSettingsCardMeasure &Measure, const FSettingsCardRender &Render, const FSettingsCardHeaderAction &HeaderAction = {}, const FSettingsCardRenderMeasured &RenderMeasured = {}, bool *pPointerInside = nullptr);
SSettingsCardFrame SettingsCard(const IUiContext &Ctx, const SSettingsCardFrame &Frame, const SSettingsCardSpec &Spec, const SSettingsCardVisualState &State, const SSettingsCardDeckVisualOptions &VisualOptions, const FSettingsCardRender &Render, const FSettingsCardHeaderAction &HeaderAction = {}, const FSettingsCardRenderMeasured &RenderMeasured = {}, bool *pPointerInside = nullptr);

#endif
