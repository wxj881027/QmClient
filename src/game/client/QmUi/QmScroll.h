/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMSCROLL_H
#define GAME_CLIENT_QMUI_QMSCROLL_H

#include <game/client/ui_rect.h>

constexpr float QmScrollAltMultiplier()
{
	return 3.0f;
}

// 面板级滚动面（设置主区、wiki 弹窗、统计页等整页容器）的滚轮档位倍率：
// 这类容器行高远大于列表行，每档 = qm_ui_scroll_step × 该倍率，保持单屏节奏。
// 全站滚轮档位都由 qm_ui_scroll_step 等比驱动，此处只定义语义倍率，不含第二处 token。
constexpr float QmPanelWheelStepMultiplier()
{
	return 12.0f;
}

constexpr int QM_POPUP_LIST_MAX_VISIBLE_ITEMS = 8;

struct SQmScrollMetrics
{
	float m_ViewportSize = 0.0f;
	float m_ContentSize = 0.0f;

	float MaxOffset() const;
};

struct SQmScrollConfig
{
	float m_WheelScale = 10.0f;
	bool m_NativeWheelStep = true;
	float m_NativeWheelAnimationTime = 0.0f;
	// 原版滚轮手感：每档滚轮重置平滑动画的初速（不继承当前速度），连续
	// 滚动每档重新起步（上游 DDNet 语义）；关闭时维持速度继承的顺滑手感。
	bool m_LegacyWheelFeel = false;
	float m_Friction = 9.0f;
	float m_OverscrollStiffness = 120.0f;
	float m_OverscrollDamping = 22.0f;
	float m_MaxOverscroll = 0.0f;
	float m_RestVelocity = 0.25f;
	float m_RestDistance = 0.25f;
};

struct SQmScrollContainerInput
{
	float m_WheelDelta = 0.0f;
	float m_MouseX = 0.0f;
	float m_MouseY = 0.0f;
	bool m_Hovered = false;
	bool m_MouseValid = false;
	bool m_MouseDown = false;
	bool m_MousePressed = false;
	bool m_ThumbHovered = false;
	bool m_TrackHovered = false;
	bool m_ModifierPressed = false;
	bool m_AltPressed = false;
	bool m_ContentDragAllowed = false;
	bool m_ContentDragBlocked = false;
};

enum class EQmScrollAxis
{
	VERTICAL,
	HORIZONTAL,
};

struct SQmScrollContainerStyle
{
	EQmScrollAxis m_Axis = EQmScrollAxis::VERTICAL;
	float m_ScrollbarWidth = 10.0f;
	float m_ScrollbarMargin = 2.0f;
	float m_MinThumbHeight = 24.0f;
	float m_ContentDragThreshold = 6.0f;
	bool m_ReserveScrollbarSpace = false;
};

enum class EQmScrollSize
{
	SMALL,
	MEDIUM,
	LARGE,
};

enum class EQmScrollProfile
{
	SETTINGS_OUTER,
	SETTINGS_PAGE,
	SETTINGS_INNER,
	SETTINGS_GRID,
	MENU_LIST,
	POPUP_LIST,
	GRID,
	FILTER_GRID,
};

enum class EQmScrollRailVisibility
{
	AUTO,
	HIDDEN,
};

struct SQmScrollRequest
{
	EQmScrollProfile m_Profile = EQmScrollProfile::MENU_LIST;
	EQmScrollAxis m_Axis = EQmScrollAxis::VERTICAL;
	float m_RowExtent = 0.0f;
	int m_RowsPerStep = 0;
};

struct SQmResolvedScrollPolicy
{
	SQmScrollContainerStyle m_Style;
	SQmScrollConfig m_Config;
	EQmScrollRailVisibility m_RailVisibility = EQmScrollRailVisibility::AUTO;
	float m_AltMultiplier = QmScrollAltMultiplier();
	int m_MaxVisibleItems = 0;
	bool m_ContentDragAllowed = true;
	bool m_ScrollbarAlwaysReserved = false;
};

SQmScrollContainerStyle QmScrollContainerStyleForSize(EQmScrollSize Size, float UiScale = 1.0f);
SQmScrollConfig QmNativeWheelScrollConfig(float UiScale, float SmoothScrollTimeSec);
SQmScrollConfig QmSettingsScrollConfig(float UiScale, float SmoothScrollTimeSec);
SQmResolvedScrollPolicy QmResolveScrollPolicy(const SQmScrollRequest &Request, float UiScale = 1.0f, float SmoothScrollTimeSec = 0.0f);

struct SQmScrollContentDragState
{
	bool m_Active = false;
	bool m_Candidate = false;
	float m_PressMousePos = 0.0f;
	float m_PressOffset = 0.0f;
	float m_LastMousePos = 0.0f;
};

class CQmScrollState
{
public:
	void Reset();
	void ResetForNonScrollableContent(bool PreserveThumbDrag);
	void SetOffset(float Offset, const SQmScrollMetrics &Metrics, const SQmScrollConfig &Config = SQmScrollConfig(), bool AllowOverscroll = false);
	void ScrollTo(float TargetOffset, const SQmScrollMetrics &Metrics, const SQmScrollConfig &Config = SQmScrollConfig());
	void RequestScrollTo(float TargetOffset);
	void AddWheelImpulse(float WheelDelta, const SQmScrollMetrics &Metrics, const SQmScrollConfig &Config = SQmScrollConfig());
	void Advance(float Dt, const SQmScrollMetrics &Metrics, const SQmScrollConfig &Config = SQmScrollConfig(), bool PauseNativeWheelAnimation = false);
	void BeginThumbDrag(float GrabOffset);
	void EndThumbDrag();

	float Offset() const { return m_Offset; }
	float Velocity() const { return m_Velocity; }
	bool Animating() const { return m_AnimTime > 0.0f; }
	bool ThumbDragActive() const { return m_ThumbDragActive; }
	float ThumbDragGrabOffset() const { return m_ThumbDragGrabOffset; }
	SQmScrollContentDragState &ContentDragState() { return m_ContentDragState; }
	const SQmScrollContentDragState &ContentDragState() const { return m_ContentDragState; }

private:
	float m_Offset = 0.0f;
	float m_Velocity = 0.0f;
	float m_LastMaxOffset = 0.0f;
	float m_AnimTime = 0.0f;
	float m_AnimTimeMax = 0.0f;
	float m_AnimStartOffset = 0.0f;
	float m_AnimStartVelocity = 0.0f;
	float m_AnimTargetOffset = 0.0f;
	bool m_HasPendingScrollTarget = false;
	float m_PendingScrollTarget = 0.0f;
	bool m_ThumbDragActive = false;
	float m_ThumbDragGrabOffset = 0.0f;
	SQmScrollContentDragState m_ContentDragState;
};

struct SQmScrollContainerFrame
{
	CUIRect m_ClipRect{};
	CUIRect m_ContentRect{};
	CUIRect m_ScrollbarTrackRect{};
	CUIRect m_ScrollbarThumbRect{};
	float m_Offset = 0.0f;
	bool m_Scrollable = false;
	bool m_ScrollbarVisible = false;
};

class CQmScrollController
{
public:
	void ScrollByWheel(CQmScrollState &State, float WheelDelta, float ViewportHeight, float ContentHeight, const SQmScrollConfig &Config = SQmScrollConfig()) const;
	SQmScrollContainerFrame PreviewFrame(const CQmScrollState &State, const CUIRect &ViewRect, float ContentHeight, const SQmScrollContainerStyle &Style = SQmScrollContainerStyle()) const;
	SQmScrollContainerFrame Update(CQmScrollState &State, const CUIRect &ViewRect, float ContentHeight, float Dt, const SQmScrollConfig &Config = SQmScrollConfig()) const;
	SQmScrollContainerFrame Update(CQmScrollState &State, const CUIRect &ViewRect, float ContentHeight, float Dt, const SQmScrollContainerInput &Input, const SQmScrollContainerStyle &Style = SQmScrollContainerStyle(), const SQmScrollConfig &Config = SQmScrollConfig()) const;
	SQmScrollContainerFrame Update(CQmScrollState &State, const CUIRect &ViewRect, float ContentHeight, float Dt, const SQmScrollContainerInput &Input, const SQmScrollRequest &Request, float UiScale, float SmoothScrollTimeSec) const;

	bool ScrollbarDragActive(const CQmScrollState &State) const { return State.ThumbDragActive(); }
	bool ContentDragActive(const CQmScrollState &State) const { return State.ContentDragState().m_Active; }

private:
	SQmScrollContainerFrame UpdateInternal(CQmScrollState &State, const CUIRect &ViewRect, float ContentHeight, float Dt, const SQmScrollContainerInput &Input, const SQmScrollContainerStyle &Style, const SQmScrollConfig &Config, bool RenderRail) const;
};

using CQmScrollContainer = CQmScrollController;

#endif
