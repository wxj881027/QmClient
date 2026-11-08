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
// 松手甩动的速度上限（滚动偏移量/秒）：手指快速甩动时的瞬时速度可能极大，
// 不设上限会让内容直接跳过整页。
constexpr float QM_SCROLL_MAX_FLING_VELOCITY = 6000.0f;

struct SQmScrollMetrics
{
	float m_ViewportSize = 0.0f;
	float m_ContentSize = 0.0f;

	float MaxOffset() const;
};

// 滚轮手感档位。SMOOTH 必须占 0：g_Config 在未跑配置加载时（测试环境）是零初始化，
// 0 必须是生产默认档，否则测试与线上手感会静默分叉。档位在设置界面的展示顺序
// （原版 → 顺滑 → 弹簧）由卡片侧单独映射，不依赖枚举值大小。
enum class EQmScrollFeel
{
	SMOOTH = 0,
	ORIGINAL = 1,
	SPRING = 2,
};

struct SQmScrollConfig
{
	float m_WheelScale = 10.0f;
	bool m_NativeWheelStep = true;
	float m_NativeWheelAnimationTime = 0.0f;
	EQmScrollFeel m_Feel = EQmScrollFeel::SMOOTH;
	// 弹簧档的两个参数，直接来自设置界面（qm_ui_scroll_spring_response /
	// qm_ui_scroll_spring_damping）。响应周期是弹簧的固有周期，与"固定时长动画
	// 的结束时刻"不是同一个量，所以不再从 m_NativeWheelAnimationTime 推导。
	// m_SpringResponse <= 0 表示立即吸附（动效等级 0 的瞬移语义）。
	float m_SpringResponse = 0.30f;
	float m_SpringDamping = 0.90f;
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
	// 触控甩动：把松手瞬间的速度交给同一套摩擦/回弹积分，产生惯性滑动。
	void Fling(float Velocity, const SQmScrollMetrics &Metrics);
	void Advance(float Dt, const SQmScrollMetrics &Metrics, const SQmScrollConfig &Config = SQmScrollConfig(), bool PauseNativeWheelAnimation = false);
	void BeginThumbDrag(float GrabOffset);
	void EndThumbDrag();

	float Offset() const { return m_Offset; }
	float Velocity() const { return m_Velocity; }
	bool Animating() const { return m_AnimTime > 0.0f || m_SpringSettling; }
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
	// 弹簧手感专用：m_AnimTargetOffset 在 SPRING 档是弹簧目标，settling 表示仍在
	// 收敛。与 m_AnimTime 分开，因为弹簧没有固定结束时刻，靠位移/速度阈值吸附。
	bool m_SpringSettling = false;
	// 滚轮把内容顶在橡皮筋区时的保持计时。桌面端没有"松手"事件，滚轮是离散输入，
	// 只能在停止滚动一小段时间后回收目标、让弹簧把内容拉回边界。
	float m_BounceHoldTimer = 0.0f;
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
