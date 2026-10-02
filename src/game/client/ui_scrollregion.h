// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_UI_SCROLLREGION_H
#define GAME_CLIENT_UI_SCROLLREGION_H

#include "ui.h"

#include <game/client/QmUi/QmScroll.h>

constexpr bool QmScrollRegionCanConsumeWheel(bool HotFromPreviousFrame, bool HotThisFrame, bool UnderlyingScrollBlocked, bool RenderingPopup)
{
	return (HotFromPreviousFrame || HotThisFrame) && (!UnderlyingScrollBlocked || RenderingPopup);
}

constexpr bool QmScrollRegionShouldReserveScrollbarSpace(bool AlwaysReserved, bool ScrollbarShown)
{
	return AlwaysReserved || ScrollbarShown;
}

// 相邻布局分割会留下微小的浮点误差。少于四分之一物理像素的尾部不应让滚动条在显示和隐藏间跳变。
constexpr bool QmScrollRegionContentOverflows(float ContentSize, float ViewportSize, float PixelSize)
{
	const float Tolerance = PixelSize > 0.0f ? PixelSize * 0.25f : 0.0f;
	return ContentSize > ViewportSize + Tolerance;
}

struct CScrollRegionParams
{
	float m_ScrollbarThickness;
	float m_ScrollbarMargin;
	bool m_ScrollbarAlwaysReserved;
	bool m_ScrollbarNoOuterMargin;
	float m_SliderMinSize;
	float m_ScrollUnit;
	ColorRGBA m_ClipBgColor;
	ColorRGBA m_ScrollbarBgColor;
	ColorRGBA m_RailBgColor;
	ColorRGBA m_SliderColor;
	ColorRGBA m_SliderColorHover;
	ColorRGBA m_SliderColorGrabbed;
	bool m_HideScrollbar;
	bool m_ScrollHorizontal;
	const void *m_pWheelOwnerId;
	bool m_WheelOwnerPreRegistered;
	bool m_Interactive;
	EUiWheelOwnerPriority m_WheelOwnerPriority;

	CScrollRegionParams();

	ColorRGBA SliderColor(bool Active, bool Hovered) const
	{
		if(Active)
			return m_SliderColorGrabbed;
		else if(Hovered)
			return m_SliderColorHover;
		return m_SliderColor;
	}
};

inline CScrollRegionParams QmScrollRegionParamsForSize(EQmScrollSize Size, float UiScale = 1.0f, EQmScrollAxis Axis = EQmScrollAxis::VERTICAL)
{
	const SQmScrollContainerStyle Style = QmScrollContainerStyleForSize(Size, UiScale);
	const SQmScrollConfig Config = QmNativeWheelScrollConfig(UiScale, 0.0f);
	CScrollRegionParams Params;
	Params.m_ScrollbarThickness = Style.m_ScrollbarWidth;
	Params.m_ScrollbarMargin = Style.m_ScrollbarMargin;
	Params.m_SliderMinSize = Style.m_MinThumbHeight;
	Params.m_ScrollUnit = Config.m_WheelScale;
	Params.m_ScrollHorizontal = Axis == EQmScrollAxis::HORIZONTAL;
	return Params;
}

inline CScrollRegionParams QmScrollRegionParamsFromPolicy(const SQmResolvedScrollPolicy &Policy)
{
	CScrollRegionParams Params;
	Params.m_ScrollbarThickness = Policy.m_Style.m_ScrollbarWidth;
	Params.m_ScrollbarMargin = Policy.m_Style.m_ScrollbarMargin;
	Params.m_ScrollbarAlwaysReserved = Policy.m_ScrollbarAlwaysReserved;
	Params.m_SliderMinSize = Policy.m_Style.m_MinThumbHeight;
	Params.m_ScrollUnit = Policy.m_Config.m_WheelScale;
	Params.m_HideScrollbar = Policy.m_RailVisibility == EQmScrollRailVisibility::HIDDEN;
	Params.m_ScrollHorizontal = Policy.m_Style.m_Axis == EQmScrollAxis::HORIZONTAL;
	return Params;
}

inline CScrollRegionParams::CScrollRegionParams()
{
	const SQmScrollContainerStyle Style = QmScrollContainerStyleForSize(EQmScrollSize::MEDIUM);
	const SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, 0.0f);
	m_ScrollbarThickness = Style.m_ScrollbarWidth;
	m_ScrollbarMargin = Style.m_ScrollbarMargin;
	m_ScrollbarAlwaysReserved = false;
	m_ScrollbarNoOuterMargin = false;
	m_SliderMinSize = 25.0f;
	m_ScrollUnit = Config.m_WheelScale;
	m_ClipBgColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
	m_ScrollbarBgColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
	m_RailBgColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f);
	m_SliderColor = ColorRGBA(0.8f, 0.8f, 0.8f, 1.0f);
	m_SliderColorHover = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	m_SliderColorGrabbed = ColorRGBA(0.9f, 0.9f, 0.9f, 1.0f);
	m_HideScrollbar = false;
	m_ScrollHorizontal = false;
	m_pWheelOwnerId = nullptr;
	m_WheelOwnerPreRegistered = false;
	m_Interactive = true;
	m_WheelOwnerPriority = EUiWheelOwnerPriority::PAGE;
}

inline bool ScrollRegionShouldKeepNoScrollSliderActive(bool Active, bool MouseDown)
{
	return Active && MouseDown;
}

/*
Usage example:

	// -- Layout --
	CUIRect View = ...; // parent UI rect initialized elsewhere
	CUIRect Content; // rect for scrollable content
	View.HSplitTop(500.0f, &Content, &View); // split maximum size of scrollable content

	// -- Initialization --
	static CScrollRegion s_ScrollRegion;
	s_ScrollRegion.Begin(&Content);
	// Content rect is now offset by the scroll offset

	// -- [Optional] Initialization with parameters --
	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 3 * LineHeight;
	s_ScrollRegion.Begin(&Content, &ScrollParams);
	// Content rect is now offset by the scroll offset

	// -- "Register" your content rects --
	CUIRect Rect;
	Content.HSplitTop(SomeValue, &Rect, &Content);
	s_ScrollRegion.AddRect(Rect);

	// -- [Optional] Knowing if a rect is clipped --
	s_ScrollRegion.RectClipped(Rect);

	// -- [Optional] Scroll to the last added rect --
	s_ScrollRegion.AddRect(Rect);
	s_ScrollRegion.ScrollHere(Option);

	// -- [Convenience] Add rect and check for visibility at the same time --
	if(s_ScrollRegion.AddRect(Rect))
	{
		// The rect is visible (not clipped)
	}

	// -- [Convenience] Add rect and scroll to it if it's selected --
	if(s_ScrollRegion.AddRect(Rect, ScrollToSelection && IsSelected))
	{
		// The rect is visible (not clipped)
	}

	// -- End --
	s_ScrollRegion.End();
*/

// Instances of CScrollRegion must be static, as member addresses are used as UI item IDs
class CScrollRegion : private CUIElementBase
{
public:
	enum EScrollRelative
	{
		SCROLLRELATIVE_UP = -1,
		SCROLLRELATIVE_NONE = 0,
		SCROLLRELATIVE_DOWN = 1,
	};

public:
	CQmScrollState &State() { return m_ScrollState; }
	const CQmScrollState &State() const { return m_ScrollState; }

private:
	CQmScrollState m_ScrollState;
	float m_ContentSize;
	EScrollRelative m_ScrollDirection;
	float m_ScrollSpeedMultiplier;
	bool m_WheelConsumedThisFrame = false;

	CUIRect m_ClipRect;
	CUIRect m_RailRect;
	CUIRect m_LastAddedRect; // saved for ScrollHere()
	char m_SliderId = 0;
	CScrollRegionParams m_Params;

public:
	enum EScrollOption
	{
		SCROLLHERE_KEEP_IN_VIEW = 0,
		SCROLLHERE_TOP,
		SCROLLHERE_BOTTOM,
	};

	CScrollRegion();
	void Reset();

	void Begin(CUIRect *pClipRect, vec2 *pOutOffset, const CScrollRegionParams *pParams = nullptr);
	void End();
	bool AddRect(const CUIRect &Rect, bool ShouldScrollHere = false); // returns true if the added rect is visible (not clipped)
	void ScrollHere(EScrollOption Option = SCROLLHERE_KEEP_IN_VIEW);
	void ScrollRelative(EScrollRelative Direction, float SpeedMultiplier = 1.0f);
	void ScrollRelativeDirect(vec2 ScrollAmount);
	void ScrollRelativeDirect(float ScrollAmount);
	void SetScrollOffsetY(float OffsetY);
	void SetContentHeightForNextFrame(float ContentHeight);
	const CUIRect *ClipRect() const { return &m_ClipRect; }
	float ContentScrollOffsetY() const { return m_Params.m_ScrollHorizontal ? 0.0f : -m_ScrollState.Offset(); }
	void DoEdgeScrolling();
	bool RectClipped(const CUIRect &Rect) const;
	bool ContentOverflows() const;
	bool ScrollbarShown() const;
	bool ScrollbarVisible() const { return ScrollbarShown(); }
	bool Animating() const;
	bool Active() const;
	float ContentAreaPos() const;
	float ContentAreaSize() const;
	float MaxScroll() const;
	const CScrollRegionParams &Params() const { return m_Params; }
	bool WheelConsumedThisFrame() const { return m_WheelConsumedThisFrame; }

private:
	CUIRect SplitContentArea();
	void DrawBackground(const CUIRect &ScrollbarBg);
	SQmScrollMetrics ScrollMetrics() const;
	SQmScrollConfig ScrollConfig() const;
	vec2 ContentScrollOffset() const;
	void DoScrollInput();
	CUIRect WheelHotRect() const;
	void UpdateHotScrollRegion();
	void AdvanceAnimation();
	void MaintainNoScrollSliderActive();
	void DoSlider();
};

#endif
