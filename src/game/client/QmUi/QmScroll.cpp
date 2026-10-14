/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmScroll.h"

#include <engine/shared/config.h>

#include <game/client/QmUi/QmAnimationBackend.h>

#include <algorithm>
#include <cmath>

namespace
{
	bool ScrollAxisHorizontal(const SQmScrollContainerStyle &Style)
	{
		return Style.m_Axis == EQmScrollAxis::HORIZONTAL;
	}

	float ScrollAxisViewportSize(const CUIRect &ViewRect, const SQmScrollContainerStyle &Style)
	{
		return ScrollAxisHorizontal(Style) ? ViewRect.w : ViewRect.h;
	}

	SQmScrollContainerFrame BuildScrollContainerFrame(const CUIRect &ViewRect, float ContentSize, float Offset, const SQmScrollContainerStyle &Style, bool RenderRail)
	{
		const bool Horizontal = ScrollAxisHorizontal(Style);
		CUIRect ClipRect = ViewRect;
		if(Style.m_ReserveScrollbarSpace)
		{
			if(Horizontal)
				ClipRect.h = std::max(0.0f, ViewRect.h - Style.m_ScrollbarWidth);
			else
				ClipRect.w = std::max(0.0f, ViewRect.w - Style.m_ScrollbarWidth);
		}
		SQmScrollMetrics Metrics;
		Metrics.m_ViewportSize = ScrollAxisViewportSize(ClipRect, Style);
		Metrics.m_ContentSize = ContentSize;

		const bool Scrollable = Metrics.MaxOffset() > 0.0f;
		const bool ScrollbarVisible = Scrollable && RenderRail;
		CUIRect ScrollbarRect;
		if(ScrollbarVisible)
		{
			if(!Style.m_ReserveScrollbarSpace && Horizontal)
				ClipRect.h = std::max(0.0f, ViewRect.h - Style.m_ScrollbarWidth);
			else if(!Style.m_ReserveScrollbarSpace)
				ClipRect.w = std::max(0.0f, ViewRect.w - Style.m_ScrollbarWidth);
			if(Horizontal)
			{
				ScrollbarRect.x = ViewRect.x;
				ScrollbarRect.y = ViewRect.y + ClipRect.h;
				ScrollbarRect.w = ViewRect.w;
				ScrollbarRect.h = Style.m_ScrollbarWidth;
			}
			else
			{
				ScrollbarRect.x = ViewRect.x + ClipRect.w;
				ScrollbarRect.y = ViewRect.y;
				ScrollbarRect.w = Style.m_ScrollbarWidth;
				ScrollbarRect.h = ViewRect.h;
			}
		}

		SQmScrollContainerFrame Frame;
		Frame.m_ClipRect = ClipRect;
		Frame.m_Offset = Offset;
		Frame.m_Scrollable = Scrollable;
		Frame.m_ScrollbarVisible = ScrollbarVisible;
		Frame.m_ContentRect = ClipRect;
		if(Horizontal)
		{
			Frame.m_ContentRect.x -= Frame.m_Offset;
			Frame.m_ContentRect.w = ContentSize;
		}
		else
		{
			Frame.m_ContentRect.y -= Frame.m_Offset;
			Frame.m_ContentRect.h = ContentSize;
		}
		if(ScrollbarVisible)
		{
			const float ScrollbarMargin = std::max(0.0f, Style.m_ScrollbarMargin);
			Frame.m_ScrollbarTrackRect.x = ScrollbarRect.x + ScrollbarMargin;
			Frame.m_ScrollbarTrackRect.y = ScrollbarRect.y + ScrollbarMargin;
			Frame.m_ScrollbarTrackRect.w = std::max(0.0f, ScrollbarRect.w - ScrollbarMargin * 2.0f);
			Frame.m_ScrollbarTrackRect.h = std::max(0.0f, ScrollbarRect.h - ScrollbarMargin * 2.0f);
			const float TrackSize = std::max(0.0f, Horizontal ? Frame.m_ScrollbarTrackRect.w : Frame.m_ScrollbarTrackRect.h);
			const float ViewportSize = std::max(0.0f, Horizontal ? ClipRect.w : ClipRect.h);
			const float RawThumbSize = ContentSize > 0.0f ? ViewportSize / ContentSize * TrackSize : TrackSize;
			const float ThumbSize = std::min(TrackSize, std::max(0.0f, std::max(Style.m_MinThumbHeight, RawThumbSize)));
			const float MaxThumbTravel = std::max(0.0f, TrackSize - ThumbSize);
			const float ScrollbarOffset = std::clamp(Frame.m_Offset, 0.0f, Metrics.MaxOffset());
			const float ThumbOffset = Metrics.MaxOffset() > 0.0f ? ScrollbarOffset / Metrics.MaxOffset() * MaxThumbTravel : 0.0f;
			Frame.m_ScrollbarThumbRect = Frame.m_ScrollbarTrackRect;
			if(Horizontal)
			{
				Frame.m_ScrollbarThumbRect.x += ThumbOffset;
				Frame.m_ScrollbarThumbRect.w = ThumbSize;
			}
			else
			{
				Frame.m_ScrollbarThumbRect.y += ThumbOffset;
				Frame.m_ScrollbarThumbRect.h = ThumbSize;
			}
		}
		return Frame;
	}
} // namespace

float SQmScrollMetrics::MaxOffset() const
{
	return std::max(0.0f, m_ContentSize - m_ViewportSize);
}

SQmScrollContainerStyle QmScrollContainerStyleForSize(EQmScrollSize Size, float UiScale)
{
	SQmScrollContainerStyle Style;
	switch(Size)
	{
	case EQmScrollSize::SMALL:
		Style.m_ScrollbarWidth = std::clamp(10.0f * UiScale, 8.0f, 10.0f);
		Style.m_ScrollbarMargin = std::clamp(2.0f * UiScale, 1.0f, 2.0f);
		Style.m_MinThumbHeight = std::clamp(36.0f * UiScale, 24.0f, 36.0f);
		break;
	case EQmScrollSize::MEDIUM:
		Style.m_ScrollbarWidth = std::clamp(20.0f * UiScale, 18.0f, 20.0f);
		Style.m_ScrollbarMargin = std::clamp(5.0f * UiScale, 4.0f, 5.0f);
		Style.m_MinThumbHeight = std::clamp(42.0f * UiScale, 30.0f, 42.0f);
		break;
	case EQmScrollSize::LARGE:
		Style.m_ScrollbarWidth = std::clamp(28.0f * UiScale, 24.0f, 28.0f);
		Style.m_ScrollbarMargin = std::clamp(8.0f * UiScale, 6.0f, 8.0f);
		Style.m_MinThumbHeight = std::clamp(48.0f * UiScale, 36.0f, 48.0f);
		break;
	}
	Style.m_ContentDragThreshold = std::clamp(6.0f * UiScale, 4.0f, 6.0f);
	return Style;
}

SQmScrollConfig QmNativeWheelScrollConfig(float UiScale, float SmoothScrollTimeSec)
{
	SQmScrollConfig Config;
	(void)UiScale;
	// 步长百分比防零回退：生产由 config loader 保证 ≥25，测试环境的
	// g_Config 是零初始化（未跑 loader），0 直接乘会把 WheelScale 归零。
	const float StepPercent = g_Config.m_QmUiScrollStep > 0.0f ? static_cast<float>(g_Config.m_QmUiScrollStep) : 100.0f;
	Config.m_WheelScale = 10.0f * StepPercent / 100.0f;
	Config.m_NativeWheelStep = true;
	Config.m_LegacyWheelFeel = g_Config.m_QmUiScrollLegacyWheel != 0;
	// 滚动平滑时长统一走引擎动效等级（qm_animation::ApplyMotionLevel，与弹层/
	// 列表/气泡同一缩放口径）：0 档即时定位、1 档 ×0.45、2 档全额。此前滚动
	// 是全站唯一不理会 qm_ui_motion_level 的动画。
	Config.m_NativeWheelAnimationTime = qm_animation::ApplyMotionLevel(
		SUiAnimTransition{.m_DurationSec = std::max(0.0f, SmoothScrollTimeSec), .m_Easing = EEasing::LINEAR},
		g_Config.m_QmUiMotionLevel)
						    .m_DurationSec;
	Config.m_MaxOverscroll = 0.0f;
	return Config;
}

SQmScrollConfig QmSettingsScrollConfig(float UiScale, float SmoothScrollTimeSec)
{
	SQmScrollConfig Config = QmNativeWheelScrollConfig(UiScale, SmoothScrollTimeSec);
	const float StepPercent = g_Config.m_QmUiScrollStep > 0.0f ? static_cast<float>(g_Config.m_QmUiScrollStep) : 100.0f;
	Config.m_WheelScale = 120.0f * StepPercent / 100.0f;
	return Config;
}

SQmResolvedScrollPolicy QmResolveScrollPolicy(const SQmScrollRequest &Request, float UiScale, float SmoothScrollTimeSec)
{
	SQmResolvedScrollPolicy Policy;
	Policy.m_Style.m_Axis = Request.m_Axis;
	Policy.m_Config = QmNativeWheelScrollConfig(UiScale, SmoothScrollTimeSec);
	const float RowExtent = std::max(0.0f, Request.m_RowExtent);
	const int RowsPerStep = Request.m_RowsPerStep > 0 ? Request.m_RowsPerStep : 2;

	switch(Request.m_Profile)
	{
	case EQmScrollProfile::SETTINGS_OUTER:
	case EQmScrollProfile::SETTINGS_PAGE:
		// 设置页外层滚动条沿用 Assets 的 20/5/10 逻辑尺寸，不随页面宽度变化。
		Policy.m_Style = QmScrollContainerStyleForSize(EQmScrollSize::MEDIUM, 1.0f);
		Policy.m_Style.m_ReserveScrollbarSpace = true;
		Policy.m_ScrollbarAlwaysReserved = true;
		Policy.m_Config = QmSettingsScrollConfig(UiScale, SmoothScrollTimeSec);
		break;
	case EQmScrollProfile::MENU_LIST:
		Policy.m_Style = QmScrollContainerStyleForSize(EQmScrollSize::MEDIUM, UiScale);
		if(RowExtent > 0.0f)
			Policy.m_Config.m_WheelScale = RowExtent * RowsPerStep;
		break;
	case EQmScrollProfile::SETTINGS_INNER:
		// 卡片内部列表拥有独立滚动状态，但沿用统一的紧凑轨道语义。
		Policy.m_Style = QmScrollContainerStyleForSize(EQmScrollSize::SMALL, UiScale);
		if(RowExtent > 0.0f)
			Policy.m_Config.m_WheelScale = RowExtent * RowsPerStep;
		break;
	case EQmScrollProfile::POPUP_LIST:
		Policy.m_Style = QmScrollContainerStyleForSize(EQmScrollSize::SMALL, UiScale);
		Policy.m_Config.m_WheelScale = RowExtent > 0.0f ? RowExtent * 3.0f : Policy.m_Config.m_WheelScale;
		Policy.m_MaxVisibleItems = QM_POPUP_LIST_MAX_VISIBLE_ITEMS;
		break;
	case EQmScrollProfile::SETTINGS_GRID:
	case EQmScrollProfile::GRID:
		Policy.m_Style = QmScrollContainerStyleForSize(EQmScrollSize::SMALL, UiScale);
		Policy.m_Config.m_WheelScale = RowExtent > 0.0f ? RowExtent * RowsPerStep : Policy.m_Config.m_WheelScale;
		Policy.m_ContentDragAllowed = false;
		break;
	case EQmScrollProfile::FILTER_GRID:
		Policy.m_Style = QmScrollContainerStyleForSize(EQmScrollSize::SMALL, UiScale);
		Policy.m_Config.m_WheelScale = RowExtent > 0.0f ? RowExtent * RowsPerStep : Policy.m_Config.m_WheelScale;
		Policy.m_RailVisibility = EQmScrollRailVisibility::HIDDEN;
		Policy.m_ContentDragAllowed = false;
		break;
	}
	Policy.m_Style.m_Axis = Request.m_Axis;
	return Policy;
}

void CQmScrollState::Reset()
{
	m_Offset = 0.0f;
	m_Velocity = 0.0f;
	m_LastMaxOffset = 0.0f;
	m_AnimTime = 0.0f;
	m_AnimTimeMax = 0.0f;
	m_AnimStartOffset = 0.0f;
	m_AnimStartVelocity = 0.0f;
	m_AnimTargetOffset = 0.0f;
	m_HasPendingScrollTarget = false;
	m_PendingScrollTarget = 0.0f;
	m_ThumbDragActive = false;
	m_ThumbDragGrabOffset = 0.0f;
	m_ContentDragState = {};
}

void CQmScrollState::ResetForNonScrollableContent(bool PreserveThumbDrag)
{
	const bool ThumbDragActive = PreserveThumbDrag && m_ThumbDragActive;
	const float ThumbDragGrabOffset = m_ThumbDragGrabOffset;
	Reset();
	if(ThumbDragActive)
	{
		m_ThumbDragActive = true;
		m_ThumbDragGrabOffset = ThumbDragGrabOffset;
	}
}

void CQmScrollState::ScrollTo(float TargetOffset, const SQmScrollMetrics &Metrics, const SQmScrollConfig &Config)
{
	m_HasPendingScrollTarget = false;
	const float MaxOffset = Metrics.MaxOffset();
	if(MaxOffset <= 0.0f)
	{
		Reset();
		return;
	}

	const float Target = std::clamp(TargetOffset, 0.0f, MaxOffset);
	m_Offset = std::clamp(m_Offset, 0.0f, MaxOffset);
	m_Velocity = 0.0f;
	m_AnimStartOffset = m_Offset;
	m_AnimTargetOffset = Target;
	m_AnimTimeMax = std::max(0.0f, Config.m_NativeWheelAnimationTime);
	m_AnimTime = m_AnimTimeMax;
	if(m_AnimTimeMax <= 0.0f || std::abs(m_AnimStartOffset - m_AnimTargetOffset) < 0.5f)
	{
		m_Offset = m_AnimTargetOffset;
		m_AnimTimeMax = 0.0f;
		m_AnimTime = 0.0f;
	}
	m_AnimStartVelocity = m_AnimTimeMax > 0.0f ? 3.0f * (m_AnimTargetOffset - m_AnimStartOffset) / m_AnimTimeMax : 0.0f;
	m_Velocity = m_AnimStartVelocity;
	m_LastMaxOffset = MaxOffset;
}

void CQmScrollState::RequestScrollTo(float TargetOffset)
{
	m_HasPendingScrollTarget = true;
	m_PendingScrollTarget = TargetOffset;
}

void CQmScrollState::SetOffset(float Offset, const SQmScrollMetrics &Metrics, const SQmScrollConfig &Config, bool AllowOverscroll)
{
	m_HasPendingScrollTarget = false;
	const float MaxOffset = Metrics.MaxOffset();
	if(MaxOffset <= 0.0f)
	{
		Reset();
		return;
	}

	const float MinAllowed = AllowOverscroll ? -Config.m_MaxOverscroll : 0.0f;
	const float MaxAllowed = AllowOverscroll ? MaxOffset + Config.m_MaxOverscroll : MaxOffset;
	m_Offset = std::clamp(Offset, MinAllowed, MaxAllowed);
	m_Velocity = 0.0f;
	m_LastMaxOffset = MaxOffset;
	m_AnimTime = 0.0f;
	m_AnimTimeMax = 0.0f;
	m_AnimStartOffset = m_Offset;
	m_AnimStartVelocity = 0.0f;
	m_AnimTargetOffset = m_Offset;
}

void CQmScrollState::AddWheelImpulse(float WheelDelta, const SQmScrollMetrics &Metrics, const SQmScrollConfig &Config)
{
	m_HasPendingScrollTarget = false;
	const float MaxOffset = Metrics.MaxOffset();
	if(MaxOffset <= 0.0f)
	{
		Reset();
		return;
	}

	if(Config.m_NativeWheelStep)
	{
		const float StepCount = std::max(1.0f, std::abs(WheelDelta) / 120.0f);
		const float WheelSteps = WheelDelta < 0.0f ? StepCount : -StepCount;
		const float AnimationTime = std::max(0.0f, Config.m_NativeWheelAnimationTime);
		const bool Continuing = m_AnimTime > 0.0f;
		const float BaseOffset = Continuing ? m_AnimTargetOffset : m_Offset;
		const float TargetOffset = std::clamp(BaseOffset + WheelSteps * Config.m_WheelScale, 0.0f, MaxOffset);
		m_AnimStartOffset = m_Offset;
		m_AnimTargetOffset = TargetOffset;
		m_AnimTimeMax = AnimationTime;
		m_AnimTime = AnimationTime;
		// 首次滚动保持原三次 ease-out，后续滚轮事件继承当前速度，包括反向操作。
		// 原版手感（LegacyWheelFeel）：每档重置初速、不继承速度——连续滚动
		// 每档重新起步（上游 DDNet 的可感知差异就在速度处理，距离两版相同）。
		m_AnimStartVelocity = (Continuing && !Config.m_LegacyWheelFeel) ? m_Velocity : (AnimationTime > 0.0f ? 3.0f * (TargetOffset - m_Offset) / AnimationTime : 0.0f);
		m_Velocity = m_AnimStartVelocity;
		if(AnimationTime <= 0.0f || (!Continuing && std::abs(m_AnimStartOffset - m_AnimTargetOffset) < 0.5f))
		{
			m_Offset = TargetOffset;
			m_Velocity = 0.0f;
			m_AnimStartVelocity = 0.0f;
			m_AnimTimeMax = 0.0f;
			m_AnimTime = 0.0f;
		}
		m_LastMaxOffset = MaxOffset;
		return;
	}

	m_Velocity += -WheelDelta * Config.m_WheelScale;
	m_Offset = std::clamp(m_Offset + m_Velocity * (1.0f / 60.0f), -Config.m_MaxOverscroll, MaxOffset + Config.m_MaxOverscroll);
	m_LastMaxOffset = MaxOffset;
}

void CQmScrollState::Advance(float Dt, const SQmScrollMetrics &Metrics, const SQmScrollConfig &Config, bool PauseNativeWheelAnimation)
{
	const float MaxOffset = Metrics.MaxOffset();
	if(MaxOffset <= 0.0f)
	{
		Reset();
		return;
	}
	if(m_HasPendingScrollTarget)
	{
		const float PendingScrollTarget = m_PendingScrollTarget;
		m_HasPendingScrollTarget = false;
		ScrollTo(PendingScrollTarget, Metrics, Config);
	}
	if(m_LastMaxOffset > MaxOffset && m_Offset > MaxOffset)
	{
		m_Offset = MaxOffset;
		if(m_Velocity > 0.0f)
			m_Velocity = 0.0f;
		if(m_AnimTargetOffset > MaxOffset)
		{
			m_AnimStartOffset = MaxOffset;
			m_AnimTargetOffset = MaxOffset;
			m_AnimTime = 0.0f;
			m_AnimTimeMax = 0.0f;
		}
	}
	else if(m_LastMaxOffset > MaxOffset && m_Offset < 0.0f)
	{
		m_Offset = 0.0f;
		if(m_Velocity < 0.0f)
			m_Velocity = 0.0f;
	}
	m_LastMaxOffset = MaxOffset;
	if(!std::isfinite(Dt) || Dt <= 0.0f)
		return;

	const float ClampedDt = std::min(Dt, 1.0f / 15.0f);

	if(Config.m_NativeWheelStep && m_AnimTime > 0.0f)
	{
		m_AnimTargetOffset = std::clamp(m_AnimTargetOffset, 0.0f, MaxOffset);
		m_AnimStartOffset = std::clamp(m_AnimStartOffset, 0.0f, MaxOffset);
		if(PauseNativeWheelAnimation)
			return;
		m_AnimTime -= Dt;
		if(m_AnimTime < 0.0f)
			m_AnimTime = 0.0f;
		const float Progress = m_AnimTimeMax > 0.0f ? 1.0f - m_AnimTime / m_AnimTimeMax : 1.0f;
		const float Remaining = 1.0f - Progress;
		const float Distance = m_AnimTargetOffset - m_AnimStartOffset;
		// Hermite 三次曲线同时满足当前位移、当前速度、目标位移和结束速度为零。
		m_Offset = m_AnimStartOffset + Distance * Progress * Progress * (3.0f - 2.0f * Progress) + m_AnimStartVelocity * m_AnimTimeMax * Progress * Remaining * Remaining;
		m_Velocity = Distance * 6.0f * Progress * Remaining / m_AnimTimeMax + m_AnimStartVelocity * (1.0f - 4.0f * Progress + 3.0f * Progress * Progress);
		if(m_Offset < 0.0f || m_Offset > MaxOffset || (m_Offset == 0.0f && m_Velocity < 0.0f) || (m_Offset == MaxOffset && m_Velocity > 0.0f))
			m_Velocity = 0.0f;
		m_Offset = std::clamp(m_Offset, 0.0f, MaxOffset);
		if(m_AnimTime <= 0.0f)
		{
			m_Offset = m_AnimTargetOffset;
			m_Velocity = 0.0f;
		}
		return;
	}

	const float LowerOverscroll = std::min(0.0f, m_Offset);
	const float UpperOverscroll = std::max(0.0f, m_Offset - MaxOffset);
	const float Overscroll = LowerOverscroll != 0.0f ? LowerOverscroll : UpperOverscroll;

	if(Overscroll != 0.0f)
	{
		const float Accel = -Overscroll * Config.m_OverscrollStiffness - m_Velocity * Config.m_OverscrollDamping;
		m_Velocity += Accel * ClampedDt;
	}
	else
	{
		const float FrictionFactor = std::exp(-std::max(0.0f, Config.m_Friction) * ClampedDt);
		m_Velocity *= FrictionFactor;
	}

	m_Offset += m_Velocity * ClampedDt;
	m_Offset = std::clamp(m_Offset, -Config.m_MaxOverscroll, MaxOffset + Config.m_MaxOverscroll);

	const bool InRange = m_Offset >= 0.0f && m_Offset <= MaxOffset;
	if(InRange && std::abs(m_Velocity) < Config.m_RestVelocity)
		m_Velocity = 0.0f;

	if(m_Offset < 0.0f && std::abs(m_Offset) < Config.m_RestDistance && std::abs(m_Velocity) < Config.m_RestVelocity)
	{
		m_Offset = 0.0f;
		m_Velocity = 0.0f;
	}
	else if(m_Offset > MaxOffset && std::abs(m_Offset - MaxOffset) < Config.m_RestDistance && std::abs(m_Velocity) < Config.m_RestVelocity)
	{
		m_Offset = MaxOffset;
		m_Velocity = 0.0f;
	}
}

void CQmScrollState::BeginThumbDrag(float GrabOffset)
{
	m_ThumbDragActive = true;
	m_ThumbDragGrabOffset = std::max(0.0f, GrabOffset);
}

void CQmScrollState::EndThumbDrag()
{
	m_ThumbDragActive = false;
	m_ThumbDragGrabOffset = 0.0f;
}

void CQmScrollController::ScrollByWheel(CQmScrollState &State, float WheelDelta, float ViewportHeight, float ContentHeight, const SQmScrollConfig &Config) const
{
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = ViewportHeight;
	Metrics.m_ContentSize = ContentHeight;
	State.AddWheelImpulse(WheelDelta, Metrics, Config);
}

SQmScrollContainerFrame CQmScrollController::PreviewFrame(const CQmScrollState &State, const CUIRect &ViewRect, float ContentHeight, const SQmScrollContainerStyle &Style) const
{
	return BuildScrollContainerFrame(ViewRect, ContentHeight, State.Offset(), Style, true);
}

SQmScrollContainerFrame CQmScrollController::Update(CQmScrollState &State, const CUIRect &ViewRect, float ContentHeight, float Dt, const SQmScrollConfig &Config) const
{
	SQmScrollContainerStyle Style;
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = ScrollAxisViewportSize(ViewRect, Style);
	Metrics.m_ContentSize = ContentHeight;
	State.Advance(Dt, Metrics, Config);

	return BuildScrollContainerFrame(ViewRect, ContentHeight, State.Offset(), Style, true);
}

SQmScrollContainerFrame CQmScrollController::Update(CQmScrollState &State, const CUIRect &ViewRect, float ContentHeight, float Dt, const SQmScrollContainerInput &Input, const SQmScrollContainerStyle &Style, const SQmScrollConfig &Config) const
{
	return UpdateInternal(State, ViewRect, ContentHeight, Dt, Input, Style, Config, true);
}

SQmScrollContainerFrame CQmScrollController::Update(CQmScrollState &State, const CUIRect &ViewRect, float ContentHeight, float Dt, const SQmScrollContainerInput &Input, const SQmScrollRequest &Request, float UiScale, float SmoothScrollTimeSec) const
{
	const SQmResolvedScrollPolicy Policy = QmResolveScrollPolicy(Request, UiScale, SmoothScrollTimeSec);
	SQmScrollContainerInput PolicyInput = Input;
	PolicyInput.m_ContentDragAllowed = PolicyInput.m_ContentDragAllowed && Policy.m_ContentDragAllowed;
	return UpdateInternal(State, ViewRect, ContentHeight, Dt, PolicyInput, Policy.m_Style, Policy.m_Config, Policy.m_RailVisibility == EQmScrollRailVisibility::AUTO);
}

SQmScrollContainerFrame CQmScrollController::UpdateInternal(CQmScrollState &State, const CUIRect &ViewRect, float ContentHeight, float Dt, const SQmScrollContainerInput &Input, const SQmScrollContainerStyle &Style, const SQmScrollConfig &Config, bool RenderRail) const
{
	const bool Horizontal = ScrollAxisHorizontal(Style);
	CUIRect MetricsView = ViewRect;
	if(Style.m_ReserveScrollbarSpace)
	{
		if(Horizontal)
			MetricsView.h = std::max(0.0f, MetricsView.h - Style.m_ScrollbarWidth);
		else
			MetricsView.w = std::max(0.0f, MetricsView.w - Style.m_ScrollbarWidth);
	}
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = ScrollAxisViewportSize(MetricsView, Style);
	Metrics.m_ContentSize = ContentHeight;
	SQmScrollConfig EffectiveConfig = Config;
	if(Input.m_AltPressed)
		EffectiveConfig.m_WheelScale *= QmScrollAltMultiplier();
	if(Input.m_Hovered && Input.m_WheelDelta != 0.0f)
		State.AddWheelImpulse(Input.m_WheelDelta, Metrics, EffectiveConfig);
	State.Advance(Dt, Metrics, EffectiveConfig);

	SQmScrollContainerFrame Frame = BuildScrollContainerFrame(ViewRect, ContentHeight, State.Offset(), Style, RenderRail);
	SQmScrollContentDragState &ContentDrag = State.ContentDragState();
	if(Frame.m_ScrollbarVisible)
	{
		const float TrackSize = std::max(0.0f, Horizontal ? Frame.m_ScrollbarTrackRect.w : Frame.m_ScrollbarTrackRect.h);
		const float ThumbSize = std::max(0.0f, Horizontal ? Frame.m_ScrollbarThumbRect.w : Frame.m_ScrollbarThumbRect.h);
		const float MaxThumbTravel = std::max(0.0f, TrackSize - ThumbSize);
		const CUIRect ClipRect = Frame.m_ClipRect;
		bool JustStartedTrackDrag = false;
		auto SetOffsetFromThumbPosition = [&](float ThumbPosition) {
			const float TrackStart = Horizontal ? Frame.m_ScrollbarTrackRect.x : Frame.m_ScrollbarTrackRect.y;
			const float ThumbStart = std::clamp(ThumbPosition, TrackStart, TrackStart + MaxThumbTravel);
			const float Ratio = MaxThumbTravel > 0.0f ? (ThumbStart - TrackStart) / MaxThumbTravel : 0.0f;
			State.SetOffset(Ratio * Metrics.MaxOffset(), Metrics, EffectiveConfig);
		};

		if(!Input.m_MouseDown)
		{
			State.EndThumbDrag();
		}
		else if(Input.m_MousePressed && Input.m_ThumbHovered)
		{
			const float MousePosition = Horizontal ? Input.m_MouseX : Input.m_MouseY;
			const float ThumbPosition = Horizontal ? Frame.m_ScrollbarThumbRect.x : Frame.m_ScrollbarThumbRect.y;
			State.BeginThumbDrag(std::clamp(MousePosition - ThumbPosition, 0.0f, ThumbSize));
		}
		else if(Input.m_MousePressed && Input.m_TrackHovered)
		{
			const float MousePosition = Horizontal ? Input.m_MouseX : Input.m_MouseY;
			const float ThumbPosition = Horizontal ? Frame.m_ScrollbarThumbRect.x : Frame.m_ScrollbarThumbRect.y;
			const float PageDirection = MousePosition < ThumbPosition ? -1.0f : 1.0f;
			State.SetOffset(State.Offset() + PageDirection * (Horizontal ? ClipRect.w : ClipRect.h), Metrics, EffectiveConfig);
			Frame = BuildScrollContainerFrame(ViewRect, ContentHeight, State.Offset(), Style, RenderRail);
			State.BeginThumbDrag((Horizontal ? Frame.m_ScrollbarThumbRect.w : Frame.m_ScrollbarThumbRect.h) * 0.5f);
			JustStartedTrackDrag = true;
		}

		if(State.ThumbDragActive() && Input.m_MouseDown && !JustStartedTrackDrag)
		{
			SetOffsetFromThumbPosition((Horizontal ? Input.m_MouseX : Input.m_MouseY) - State.ThumbDragGrabOffset());
			Frame = BuildScrollContainerFrame(ViewRect, ContentHeight, State.Offset(), Style, RenderRail);
		}
	}
	else
	{
		State.EndThumbDrag();
	}
	if(!Input.m_MouseDown)
	{
		ContentDrag = {};
	}
	else if(Input.m_ContentDragBlocked)
	{
		ContentDrag = {};
	}
	else if(Input.m_ContentDragAllowed && !Input.m_ContentDragBlocked && Input.m_Hovered && Input.m_MousePressed && !Input.m_ThumbHovered && !Input.m_TrackHovered)
	{
		const float MousePosition = Horizontal ? Input.m_MouseX : Input.m_MouseY;
		ContentDrag.m_Candidate = true;
		ContentDrag.m_PressMousePos = MousePosition;
		ContentDrag.m_PressOffset = State.Offset();
		ContentDrag.m_LastMousePos = MousePosition;
	}
	else if((ContentDrag.m_Candidate || ContentDrag.m_Active) && Input.m_MouseDown)
	{
		const float MousePosition = Horizontal ? Input.m_MouseX : Input.m_MouseY;
		const float DragDistance = MousePosition - ContentDrag.m_PressMousePos;
		if(!ContentDrag.m_Active && std::abs(DragDistance) >= std::max(0.0f, Style.m_ContentDragThreshold))
			ContentDrag.m_Active = true;
		if(ContentDrag.m_Active)
		{
			State.SetOffset(ContentDrag.m_PressOffset - DragDistance, Metrics, EffectiveConfig, true);
			Frame = BuildScrollContainerFrame(ViewRect, ContentHeight, State.Offset(), Style, RenderRail);
		}
		ContentDrag.m_LastMousePos = MousePosition;
	}
	return Frame;
}
