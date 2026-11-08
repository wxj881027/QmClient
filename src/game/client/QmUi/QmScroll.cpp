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

namespace
{
	// 弹簧手感的参数直接来自设置界面（qm_ui_scroll_spring_response 毫秒、
	// qm_ui_scroll_spring_damping 百分比），不再从平滑时长推导——后者是固定时长
	// 动画的结束时刻，拿它当弹簧的固有周期会让一档滚轮拖沓到 0.4s，而乘个系数
	// 折算又不可见。响应周期决定"多快走到"，阻尼比只决定过冲与回稳方式。
	SUiSpringConfig ScrollFeelSpringConfig(const SQmScrollConfig &Config)
	{
		return SUiSpringConfig::FromResponse(std::max(Config.m_SpringResponse, 0.02f), std::clamp(Config.m_SpringDamping, 0.05f, 4.0f));
	}

	// 收敛判据与动画后端同一量级：亚像素位移 + 近零速度才吸附，避免可见尾迹。
	constexpr float QM_SCROLL_SPRING_REST_DISTANCE = 0.25f;
	constexpr float QM_SCROLL_SPRING_REST_VELOCITY = 2.0f;
	// 滚轮停在橡皮筋区后保持多久再回收目标。取值要短于"人觉得已经停了"的感知
	// 阈值，又不能短到连续滚动（约 10~20 档/秒）中途被拉回。
	constexpr float QM_SCROLL_BOUNCE_HOLD_SECONDS = 0.12f;
} // namespace

SQmScrollConfig QmNativeWheelScrollConfig(float UiScale, float SmoothScrollTimeSec)
{
	SQmScrollConfig Config;
	(void)UiScale;
	// 步长防零回退：生产由 config loader 保证 ≥2，测试环境的 g_Config 是
	// 零初始化（未跑 loader），0 直接用会把 WheelScale 归零。
	const float StepPixels = g_Config.m_QmUiScrollStep > 0.0f ? static_cast<float>(g_Config.m_QmUiScrollStep) : 10.0f;
	Config.m_WheelScale = StepPixels;
	Config.m_NativeWheelStep = true;
	Config.m_Feel = static_cast<EQmScrollFeel>(std::clamp(g_Config.m_QmUiScrollFeel, 0, 2));
	// 滚动平滑时长统一走引擎动效等级（qm_animation::ApplyMotionLevel，与弹层/
	// 列表/气泡同一缩放口径）：0 档即时定位、1 档 ×0.45、2 档全额。此前滚动
	// 是全站唯一不理会 qm_ui_motion_level 的动画。
	Config.m_NativeWheelAnimationTime = qm_animation::ApplyMotionLevel(
		SUiAnimTransition{.m_DurationSec = std::max(0.0f, SmoothScrollTimeSec), .m_Easing = EEasing::LINEAR},
		g_Config.m_QmUiMotionLevel)
						    .m_DurationSec;
	// 橡皮筋距离：滚轮、拖拽与甩动都可以越界，0 表示关闭（保持历史行为）。
	// 滚轮是离散输入，越界量按档累积到上限；拖拽走 SetOffset 的渐进阻力。
	Config.m_MaxOverscroll = std::clamp(static_cast<float>(g_Config.m_QmUiScrollBounce), 0.0f, 64.0f);
	// 弹簧档参数。防零回退与 m_QmUiScrollStep 同理：测试环境的 g_Config 是
	// 零初始化，0 会让弹簧退化成瞬时吸附，静默改变断言语义。
	const float SpringResponseMs = g_Config.m_QmUiScrollSpringResponse > 0 ? static_cast<float>(g_Config.m_QmUiScrollSpringResponse) : 300.0f;
	const float SpringDampingPct = g_Config.m_QmUiScrollSpringDamping > 0 ? static_cast<float>(g_Config.m_QmUiScrollSpringDamping) : 90.0f;
	Config.m_SpringResponse = SpringResponseMs / 1000.0f;
	Config.m_SpringDamping = SpringDampingPct / 100.0f;
	// 与全站动效等级同一口径：0 档立即吸附，1 档压短周期并抬高阻尼（对应
	// ApplyMotionLevel 对弹簧轨道的处理），2 档全额。
	const int MotionLevel = std::clamp(g_Config.m_QmUiMotionLevel, 0, 2);
	if(MotionLevel == 0)
	{
		Config.m_SpringResponse = 0.0f;
	}
	else if(MotionLevel == 1)
	{
		Config.m_SpringResponse *= 0.45f;
		Config.m_SpringDamping *= 1.35f;
	}
	return Config;
}

SQmScrollConfig QmSettingsScrollConfig(float UiScale, float SmoothScrollTimeSec)
{
	SQmScrollConfig Config = QmNativeWheelScrollConfig(UiScale, SmoothScrollTimeSec);
	// 设置卡片行高远大于普通列表行，每档按面板级倍率保持与原版相当的单屏节奏。
	const float StepPixels = g_Config.m_QmUiScrollStep > 0.0f ? static_cast<float>(g_Config.m_QmUiScrollStep) : 10.0f;
	Config.m_WheelScale = StepPixels * QmPanelWheelStepMultiplier();
	return Config;
}

SQmResolvedScrollPolicy QmResolveScrollPolicy(const SQmScrollRequest &Request, float UiScale, float SmoothScrollTimeSec)
{
	SQmResolvedScrollPolicy Policy;
	Policy.m_Style.m_Axis = Request.m_Axis;
	Policy.m_Config = QmNativeWheelScrollConfig(UiScale, SmoothScrollTimeSec);
	const float RowExtent = std::max(0.0f, Request.m_RowExtent);
	const int RowsPerStep = Request.m_RowsPerStep > 0 ? Request.m_RowsPerStep : 2;
	// 用户自定义步长（qm_ui_scroll_step，默认 10）作为全站滚轮档位的缩放基准：
	// 行数档位（列表/网格）此前完全无视该配置——设置页里调了没有任何可感知
	// 变化。默认值下行为与历史版本逐比特一致，调整后所有滚动面等比缩放。
	const float StepScale = (g_Config.m_QmUiScrollStep > 0.0f ? (float)g_Config.m_QmUiScrollStep : 10.0f) / 10.0f;

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
			Policy.m_Config.m_WheelScale = RowExtent * RowsPerStep * StepScale;
		break;
	case EQmScrollProfile::SETTINGS_INNER:
		// 卡片内部列表拥有独立滚动状态，但沿用统一的紧凑轨道语义。
		Policy.m_Style = QmScrollContainerStyleForSize(EQmScrollSize::SMALL, UiScale);
		if(RowExtent > 0.0f)
			Policy.m_Config.m_WheelScale = RowExtent * RowsPerStep * StepScale;
		break;
	case EQmScrollProfile::POPUP_LIST:
		Policy.m_Style = QmScrollContainerStyleForSize(EQmScrollSize::SMALL, UiScale);
		Policy.m_Config.m_WheelScale = RowExtent > 0.0f ? RowExtent * 3.0f * StepScale : Policy.m_Config.m_WheelScale;
		Policy.m_MaxVisibleItems = QM_POPUP_LIST_MAX_VISIBLE_ITEMS;
		break;
	case EQmScrollProfile::SETTINGS_GRID:
	case EQmScrollProfile::GRID:
		Policy.m_Style = QmScrollContainerStyleForSize(EQmScrollSize::SMALL, UiScale);
		Policy.m_Config.m_WheelScale = RowExtent > 0.0f ? RowExtent * RowsPerStep * StepScale : Policy.m_Config.m_WheelScale;
		Policy.m_ContentDragAllowed = false;
		break;
	case EQmScrollProfile::FILTER_GRID:
		Policy.m_Style = QmScrollContainerStyleForSize(EQmScrollSize::SMALL, UiScale);
		Policy.m_Config.m_WheelScale = RowExtent > 0.0f ? RowExtent * RowsPerStep * StepScale : Policy.m_Config.m_WheelScale;
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
	m_SpringSettling = false;
	m_BounceHoldTimer = 0.0f;
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
	m_AnimStartOffset = m_Offset;
	m_AnimTargetOffset = Target;
	if(Config.m_Feel == EQmScrollFeel::SPRING && Config.m_NativeWheelAnimationTime > 0.0f && std::abs(m_Offset - Target) > 0.5f)
	{
		// 程序化滚动（列表选中项回看等）与滚轮共用同一手感：只换目标，速度保留，
		// 因此正在晃动的滚动面会顺着当前方向转向新目标而不是急停。
		m_SpringSettling = true;
		m_AnimTime = 0.0f;
		m_AnimTimeMax = 0.0f;
		m_LastMaxOffset = MaxOffset;
		return;
	}
	m_SpringSettling = false;
	m_Velocity = 0.0f;
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
	// 越界段用渐进阻力压缩：f(o) = B·o/(o+B)，推得越远越沉且永远到不了 B。
	// 直接把位移线性放到 B 会像"把内容拖出画面"，没有橡皮筋的软硬变化。
	float Target = Offset;
	if(AllowOverscroll && Config.m_MaxOverscroll > 0.0f)
	{
		if(Target < 0.0f)
			Target = -Config.m_MaxOverscroll * -Target / (-Target + Config.m_MaxOverscroll);
		else if(Target > MaxOffset)
		{
			const float Overflow = Target - MaxOffset;
			Target = MaxOffset + Config.m_MaxOverscroll * Overflow / (Overflow + Config.m_MaxOverscroll);
		}
	}
	m_Offset = std::clamp(Target, MinAllowed, MaxAllowed);
	m_Velocity = 0.0f;
	m_LastMaxOffset = MaxOffset;
	m_AnimTime = 0.0f;
	m_AnimTimeMax = 0.0f;
	m_AnimStartOffset = m_Offset;
	m_AnimStartVelocity = 0.0f;
	m_AnimTargetOffset = m_Offset;
	m_SpringSettling = false;
	m_BounceHoldTimer = 0.0f;
}

void CQmScrollState::Fling(float Velocity, const SQmScrollMetrics &Metrics)
{
	const float MaxOffset = Metrics.MaxOffset();
	if(MaxOffset <= 0.0f)
	{
		Reset();
		return;
	}
	// 甩动接管后续几帧：清掉滚轮平滑动画与弹簧收敛，交给 Advance 的摩擦积分。
	m_HasPendingScrollTarget = false;
	m_SpringSettling = false;
	m_AnimTime = 0.0f;
	m_AnimTimeMax = 0.0f;
	m_AnimStartOffset = std::clamp(m_Offset, 0.0f, MaxOffset);
	m_AnimStartVelocity = 0.0f;
	m_AnimTargetOffset = m_AnimStartOffset;
	m_Velocity = std::clamp(Velocity, -QM_SCROLL_MAX_FLING_VELOCITY, QM_SCROLL_MAX_FLING_VELOCITY);
	m_LastMaxOffset = MaxOffset;
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
		m_AnimStartOffset = m_Offset;

		if(Config.m_Feel == EQmScrollFeel::SPRING)
		{
			// 目标在上一目标上继续叠加（与原版同口径，连续滚动不会丢距离）；速度
			// 不清零，换向时弹簧先减速再反向，而不是每档从静止重新起步。
			// 反向时从边界起算：否则在橡皮筋区往回滚的前几档只是在消化越界量，
			// 看起来像"滚了没反应"。
			const float PrevTarget = m_SpringSettling ? m_AnimTargetOffset : m_Offset;
			const bool ReversingIntoRange = (WheelSteps > 0.0f && PrevTarget < 0.0f) || (WheelSteps < 0.0f && PrevTarget > MaxOffset);
			const float BaseOffset = ReversingIntoRange ? std::clamp(PrevTarget, 0.0f, MaxOffset) : PrevTarget;
			float TargetOffset = BaseOffset + WheelSteps * Config.m_WheelScale;
			// 触底反馈：滚轮可以让目标进入橡皮筋区。滚轮是离散输入，越界量按档
			// 累积到上限即可——这里不能沿用拖拽的渐进阻力 f(o)=B·o/(o+B)：它在
			// 饱和点附近会把每档位移全部吃掉（实测连续 6 档位移恒为同一个值），
			// 表现成"滚到底再滚完全不动"，看起来就是硬停。
			// 桌面端没有"松手"事件，靠停止滚动后的保持计时回收目标（见 Advance），
			// 弹簧再把内容拉回边界。动效等级 0（响应周期为 0）表示关闭动画，按硬边界。
			if(Config.m_MaxOverscroll > 0.0f && Config.m_SpringResponse > 0.0f)
			{
				TargetOffset = std::clamp(TargetOffset, -Config.m_MaxOverscroll, MaxOffset + Config.m_MaxOverscroll);
				if(TargetOffset < 0.0f || TargetOffset > MaxOffset)
					m_BounceHoldTimer = QM_SCROLL_BOUNCE_HOLD_SECONDS;
			}
			else
			{
				TargetOffset = std::clamp(TargetOffset, 0.0f, MaxOffset);
			}
			m_AnimTargetOffset = TargetOffset;
			// 是否瞬移由弹簧自己的响应周期决定，而不是平滑时长：否则把
			// ui_smooth_scroll_time 设成 0 会让弹簧档静默退化成直接落位。
			if(Config.m_SpringResponse <= 0.0f)
			{
				// 动效等级 0：与 Hermite 路径同一语义，直接落位。
				m_Offset = TargetOffset;
				m_Velocity = 0.0f;
				m_SpringSettling = false;
				m_AnimTime = 0.0f;
				m_AnimTimeMax = 0.0f;
			}
			else
			{
				m_SpringSettling = true;
				m_AnimTime = 0.0f;
				m_AnimTimeMax = 0.0f;
			}
			m_LastMaxOffset = MaxOffset;
			return;
		}

		m_SpringSettling = false;
		const bool Continuing = m_AnimTime > 0.0f;
		const float BaseOffset = Continuing ? m_AnimTargetOffset : m_Offset;
		const float TargetOffset = std::clamp(BaseOffset + WheelSteps * Config.m_WheelScale, 0.0f, MaxOffset);
		m_AnimTargetOffset = TargetOffset;
		m_AnimTimeMax = AnimationTime;
		m_AnimTime = AnimationTime;
		// 首次滚动保持原三次 ease-out，后续滚轮事件继承当前速度，包括反向操作。
		// 原版手感（EQmScrollFeel::ORIGINAL）：每档重置初速、不继承速度——连续
		// 滚动每档重新起步（上游 DDNet 的可感知差异就在速度处理，距离两版相同）。
		const bool InheritVelocity = Continuing && Config.m_Feel == EQmScrollFeel::SMOOTH;
		m_AnimStartVelocity = InheritVelocity ? m_Velocity : (AnimationTime > 0.0f ? 3.0f * (TargetOffset - m_Offset) / AnimationTime : 0.0f);
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
	if(m_LastMaxOffset > MaxOffset && m_Offset > MaxOffset + Config.m_MaxOverscroll)
	{
		// 与顶部分支对称：橡皮筋区内属于合法状态，只有真正超出上限才回收，
		// 否则内容高度的一点点波动都会把回弹打断。
		m_Offset = MaxOffset + Config.m_MaxOverscroll;
		if(m_Velocity > 0.0f)
			m_Velocity = 0.0f;
		if(m_AnimTargetOffset > MaxOffset + Config.m_MaxOverscroll)
		{
			m_AnimStartOffset = m_Offset;
			m_AnimTargetOffset = m_Offset;
			m_AnimTime = 0.0f;
			m_AnimTimeMax = 0.0f;
		}
	}
	else if(m_LastMaxOffset > MaxOffset && m_Offset < -Config.m_MaxOverscroll)
	{
		// 内容变矮时把越界量收到橡皮筋上限以内，而不是直接归零：橡皮筋区本身是
		// 合法状态，归零会让弹簧在"0 与负 target"之间反复起步，表现为顶部越界抖动。
		m_Offset = -Config.m_MaxOverscroll;
		if(m_Velocity < 0.0f)
			m_Velocity = 0.0f;
		m_AnimTargetOffset = std::clamp(m_AnimTargetOffset, -Config.m_MaxOverscroll, MaxOffset);
	}
	m_LastMaxOffset = MaxOffset;
	if(!std::isfinite(Dt) || Dt <= 0.0f)
		return;

	// 时间基准与动画后端保持同一口径：直接用帧时间，不再截断到 1/15。下方两条
	// 物理路径都是闭式解，大 dt 只代表这一步跨度更大、不会数值发散；保留截断反而
	// 会让长帧（地图加载、失焦回切）里的滚动落后于同一帧中已经跳到终点的 UI 动画。

	// 运行中切走弹簧档位时不能把 settling 留在 true：否则 Animating() 永远为真，
	// 滚动面的按需重绘预算再也回不到空闲。橡皮筋保持计时同理必须清掉；越界位移
	// 本身不在这里夹，交给下方越界分支用回弹弹簧收回——拖拽路径与非弹簧档都依赖它。
	if(Config.m_Feel != EQmScrollFeel::SPRING)
	{
		m_SpringSettling = false;
		m_BounceHoldTimer = 0.0f;
	}

	if(Config.m_Feel == EQmScrollFeel::SPRING && m_SpringSettling)
	{
		// 动效等级 0 把响应周期压成 0：直接吸附，不再让弹簧走一小段。
		if(Config.m_SpringResponse <= 0.0f)
		{
			m_BounceHoldTimer = 0.0f;
			m_Offset = std::clamp(m_AnimTargetOffset, 0.0f, MaxOffset);
			m_AnimTargetOffset = m_Offset;
			m_Velocity = 0.0f;
			m_SpringSettling = false;
			return;
		}
		// 滚轮把目标顶在橡皮筋区后，停止滚动一段时间就回收目标：弹簧带着当前
		// 速度把内容拉回边界，形成"推出去再弹回来"。正常滚动不会启动计时。
		if(m_BounceHoldTimer > 0.0f)
		{
			m_BounceHoldTimer -= Dt;
			if(m_BounceHoldTimer <= 0.0f)
			{
				m_BounceHoldTimer = 0.0f;
				m_AnimTargetOffset = std::clamp(m_AnimTargetOffset, 0.0f, MaxOffset);
			}
		}
		const SUiSpringConfig Spring = ScrollFeelSpringConfig(Config);
		QmSpringAdvance(m_Offset, m_Velocity, m_AnimTargetOffset, Spring, Dt);
		if(std::abs(m_Offset - m_AnimTargetOffset) < QM_SCROLL_SPRING_REST_DISTANCE && std::abs(m_Velocity) < QM_SCROLL_SPRING_REST_VELOCITY)
		{
			m_Offset = m_AnimTargetOffset;
			m_Velocity = 0.0f;
			m_SpringSettling = false;
		}
		else
		{
			// 欠阻尼过冲与滚轮橡皮筋都只能停在可视范围外一点点，不允许无限越界。
			m_Offset = std::clamp(m_Offset, -Config.m_MaxOverscroll, MaxOffset + Config.m_MaxOverscroll);
		}
		return;
	}

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
		// 越界回弹就是阻尼振子：目标取最近的那条边界，速度作为初值带入，因此手指
		// 松手瞬间的甩动速度和回弹是同一条轨迹。用与 UI 弹簧同一份闭式解实现，
		// 不再单独写半隐式 Euler（后者会随帧率改变回弹形状）。
		SUiSpringConfig Spring;
		Spring.m_Mass = 1.0f;
		Spring.m_Stiffness = Config.m_OverscrollStiffness;
		Spring.m_Damping = Config.m_OverscrollDamping;
		QmSpringAdvance(m_Offset, m_Velocity, Overscroll < 0.0f ? 0.0f : MaxOffset, Spring, Dt);
	}
	else
	{
		// 指数摩擦的闭式积分：一步内的位移恰为 (v_old - v_new)/friction。旧实现用
		// 衰减后的速度乘 dt，把位移系统性低估——60fps 下滑行距离比解析解短 7.3%，
		// 同一手势在不同刷新率上滑出的距离不一致。
		const float Friction = std::max(0.0f, Config.m_Friction);
		const float PreviousVelocity = m_Velocity;
		m_Velocity *= std::exp(-Friction * Dt);
		m_Offset += Friction > 1e-6f ? (PreviousVelocity - m_Velocity) / Friction : PreviousVelocity * Dt;
	}

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
