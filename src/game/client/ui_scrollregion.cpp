// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "ui_scrollregion.h"

#include <base/system.h>
#include <base/vmath.h>

#include <engine/client.h>
#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>

#include <algorithm>
#include <cmath>

CScrollRegion::CScrollRegion()
{
	Reset();
}

void CScrollRegion::Reset()
{
	m_ScrollState.Reset();
	m_ContentSize = 0.0f;
	m_ScrollDirection = SCROLLRELATIVE_NONE;
	m_ScrollSpeedMultiplier = 1.0f;
	m_WheelConsumedThisFrame = false;
	m_ClipRect = m_RailRect = m_LastAddedRect = CUIRect{0.0f, 0.0f, 0.0f, 0.0f};
	m_ScrollbarReserveWidth = 0.0f;
	m_ScrollbarReserveInitialized = false;
	m_Params = CScrollRegionParams();
}

SQmScrollMetrics CScrollRegion::ScrollMetrics() const
{
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = ContentAreaSize();
	Metrics.m_ContentSize = m_ContentSize;
	return Metrics;
}

SQmScrollConfig CScrollRegion::ScrollConfig() const
{
	SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, g_Config.m_UiSmoothScrollTime / 1000.0f);
	Config.m_WheelScale = m_Params.m_ScrollUnit;
	return Config;
}

vec2 CScrollRegion::ContentScrollOffset() const
{
	return m_Params.m_ScrollHorizontal ? vec2(-m_ScrollState.Offset(), 0.0f) : vec2(0.0f, -m_ScrollState.Offset());
}

void CScrollRegion::ClampBackgroundScrollOffset()
{
	if(!Ui()->BackgroundRender() || Ui()->MeasurementOnly())
		return;

	// 后台只约束失效的尺寸偏移；合法偏移不重设，保留暂停的动画和待处理定位。
	const float MaxOffset = ContentOverflows() ? MaxScroll() : 0.0f;
	const float Offset = std::clamp(m_ScrollState.Offset(), 0.0f, MaxOffset);
	if(Offset != m_ScrollState.Offset())
		m_ScrollState.SetOffset(Offset, ScrollMetrics(), ScrollConfig());
}

void CScrollRegion::Begin(CUIRect *pClipRect, vec2 *pOutOffset, const CScrollRegionParams *pParams)
{
	m_WheelConsumedThisFrame = false;
	if(pParams)
		m_Params = *pParams;
	m_ClipRect = *pClipRect;

	// m_ContentSize 来自上一帧 End/AddRect 的测量结果。Begin 先用这个
	// 上一帧尺寸预留滚动条空间，随后本帧再重新测量内容尺寸。
	CUIRect ScrollbarBg = SplitContentArea();
	DrawBackground(ScrollbarBg);
	ClampBackgroundScrollOffset();

	if(!ContentOverflows() && !Ui()->RenderOnly())
		m_ScrollState.ResetForNonScrollableContent(Ui()->IsActiveItem(&m_SliderId) && Ui()->MouseButton(0));
	m_ContentSize = 0.0f;

	Ui()->ClipEnable(&m_ClipRect);

	*pClipRect = m_ClipRect;
	*pOutOffset = ContentScrollOffset();
}

void CScrollRegion::End()
{
	Ui()->ClipDisable();

	if(Ui()->RenderOnly())
	{
		// 当前帧测量也可能改变内容尺寸；修正越界位置但不消费输入或推进动画。
		ClampBackgroundScrollOffset();
		if(ContentOverflows())
			DoSlider();
		return;
	}

	if(!m_Params.m_Interactive)
	{
		// 父层停止交互时也要处理内容已不再溢出的情况。
		if(Ui()->ActiveItem() == &m_SliderId)
			Ui()->SetActiveItem(nullptr);
		m_ScrollState.EndThumbDrag();
	}

	if(!ContentOverflows())
	{
		MaintainNoScrollSliderActive();
		return;
	}

	if(m_Params.m_Interactive)
	{
		UpdateHotScrollRegion();
		DoContentDrag();
		DoScrollInput();
	}
	AdvanceAnimation();
	DoSlider();
}

bool CScrollRegion::AddRect(const CUIRect &Rect, bool ShouldScrollHere)
{
	m_LastAddedRect = Rect;
	const vec2 ContentOffset = ContentScrollOffset();
	if(m_Params.m_ScrollHorizontal)
		m_ContentSize = maximum(std::ceil(Rect.x + Rect.w - (m_ClipRect.x + ContentOffset.x)), m_ContentSize);
	else
		m_ContentSize = maximum(Rect.y + Rect.h - (m_ClipRect.y + ContentOffset.y), m_ContentSize);
	if(ShouldScrollHere && !Ui()->RenderOnly())
		ScrollHere();
	return !RectClipped(Rect);
}

void CScrollRegion::ScrollHere(EScrollOption Option)
{
	const float ClipPos = m_Params.m_ScrollHorizontal ? m_ClipRect.x : m_ClipRect.y;
	const float ClipSize = m_Params.m_ScrollHorizontal ? m_ClipRect.w : m_ClipRect.h;
	const float LastAddedPos = m_Params.m_ScrollHorizontal ? m_LastAddedRect.x : m_LastAddedRect.y;
	const float LastAddedSize = m_Params.m_ScrollHorizontal ? m_LastAddedRect.w : m_LastAddedRect.h;
	const vec2 ContentOffset = ContentScrollOffset();
	const float ContentScrollOffset = m_Params.m_ScrollHorizontal ? ContentOffset.x : ContentOffset.y;
	const float MinHeight = minimum(ClipSize, LastAddedSize);
	const float TopScroll = LastAddedPos - (ClipPos + ContentScrollOffset);

	switch(Option)
	{
	case SCROLLHERE_TOP:
		m_ScrollState.RequestScrollTo(TopScroll);
		break;

	case SCROLLHERE_BOTTOM:
		m_ScrollState.RequestScrollTo(TopScroll - (ClipSize - MinHeight));
		break;

	case SCROLLHERE_KEEP_IN_VIEW:
	default:
		const float DeltaY = LastAddedPos - ClipPos;
		if(DeltaY < 0)
			m_ScrollState.RequestScrollTo(TopScroll);
		else if(DeltaY > (ClipSize - MinHeight))
			m_ScrollState.RequestScrollTo(TopScroll - (ClipSize - MinHeight));
		break;
	}
}

void CScrollRegion::ScrollRelative(EScrollRelative Direction, float SpeedMultiplier)
{
	m_ScrollDirection = Direction;
	m_ScrollSpeedMultiplier = SpeedMultiplier;
}

void CScrollRegion::ScrollRelativeDirect(vec2 ScrollAmount)
{
	const float ScrollAmountAxis = m_Params.m_ScrollHorizontal ? ScrollAmount.x : ScrollAmount.y;
	m_ScrollState.RequestScrollTo(m_ScrollState.Offset() + ScrollAmountAxis);
}

void CScrollRegion::ScrollRelativeDirect(float ScrollAmount)
{
	ScrollRelativeDirect(vec2(0.0f, ScrollAmount));
}

void CScrollRegion::SetScrollOffsetY(float OffsetY)
{
	m_ScrollState.SetOffset(maximum(0.0f, -OffsetY), ScrollMetrics(), ScrollConfig());
}

void CScrollRegion::SetContentHeightForNextFrame(float ContentHeight)
{
	m_ContentSize = maximum(0.0f, ContentHeight);
}

void CScrollRegion::DoEdgeScrolling()
{
	if(!ContentOverflows())
		return;

	const float ClipPos = m_Params.m_ScrollHorizontal ? m_ClipRect.x : m_ClipRect.y;
	const float ClipSize = m_Params.m_ScrollHorizontal ? m_ClipRect.w : m_ClipRect.h;
	const float ScrollBorderSize = 20.0f;
	const float MaxScrollMultiplier = 2.0f;
	const float ScrollSpeedFactor = MaxScrollMultiplier / ScrollBorderSize;
	const float TopScrollPosition = ClipPos + ScrollBorderSize;
	const float BottomScrollPosition = ClipPos + ClipSize - ScrollBorderSize;
	const float MousePos = m_Params.m_ScrollHorizontal ? Ui()->MouseX() : Ui()->MouseY();
	if(MousePos < TopScrollPosition)
		ScrollRelative(SCROLLRELATIVE_UP, minimum(MaxScrollMultiplier, (TopScrollPosition - MousePos) * ScrollSpeedFactor));
	else if(MousePos > BottomScrollPosition)
		ScrollRelative(SCROLLRELATIVE_DOWN, minimum(MaxScrollMultiplier, (MousePos - BottomScrollPosition) * ScrollSpeedFactor));
}

bool CScrollRegion::RectClipped(const CUIRect &Rect) const
{
	return (m_ClipRect.x > (Rect.x + Rect.w) || (m_ClipRect.x + m_ClipRect.w) < Rect.x || m_ClipRect.y > (Rect.y + Rect.h) || (m_ClipRect.y + m_ClipRect.h) < Rect.y);
}

bool CScrollRegion::ContentOverflows() const
{
	return m_Params.m_ScrollHorizontal ? QmScrollRegionContentOverflows(m_ContentSize, m_ClipRect.w, Ui()->PixelSize()) : QmScrollRegionContentOverflows(m_ContentSize, m_ClipRect.h, Ui()->PixelSize());
}

bool CScrollRegion::ScrollbarShown() const
{
	return !m_Params.m_HideScrollbar && ContentOverflows();
}

bool CScrollRegion::Animating() const
{
	return m_ScrollState.Animating();
}

float CScrollRegion::ContentAreaPos() const
{
	return m_Params.m_ScrollHorizontal ? m_ClipRect.x : m_ClipRect.y;
}

float CScrollRegion::ContentAreaSize() const
{
	return m_Params.m_ScrollHorizontal ? m_ClipRect.w : m_ClipRect.h;
}

float CScrollRegion::MaxScroll() const
{
	return maximum(0.0f, m_ContentSize - ContentAreaSize());
}

bool CScrollRegion::Active() const
{
	return Ui()->ActiveItem() == &m_SliderId;
}

CUIRect CScrollRegion::SplitContentArea()
{
	CUIRect ScrollbarBg;
	const bool ReserveScrollbarSpace = QmScrollRegionShouldReserveScrollbarSpace(m_Params.m_ScrollbarAlwaysReserved, ScrollbarShown());
	float ReserveWidth = ReserveScrollbarSpace ? m_Params.m_ScrollbarThickness : 0.0f;
	// 占位宽度和内容裁剪使用同一条动画轨道，滚动条出现/消失时卡片宽度连续变化。
	if(m_Params.m_AnimateScrollbarReserve)
	{
		auto *pAnimRuntime = Ui()->QmAnimationRuntime();
		const uint64_t NodeKey = BuildUiAnimNodeKey(MakeUiScopeHash("scroll_region_reserve"), reinterpret_cast<uint64_t>(this));
		if(pAnimRuntime == nullptr || g_Config.m_QmUiMotionLevel == 0 || Ui()->AnimationsBlocked())
		{
			if(pAnimRuntime != nullptr)
				pAnimRuntime->SetValue(NodeKey, EUiAnimProperty::WIDTH, ReserveWidth);
			m_ScrollbarReserveInitialized = true;
		}
		else if(!m_ScrollbarReserveInitialized)
		{
			// 首次布局直接落在目标宽度，避免页面打开时从无滚动条位置飞入。
			pAnimRuntime->SetValue(NodeKey, EUiAnimProperty::WIDTH, ReserveWidth);
			m_ScrollbarReserveInitialized = true;
		}
		else
		{
			ReserveWidth = ResolveUiAnimValue(*pAnimRuntime, NodeKey, EUiAnimProperty::WIDTH, ReserveWidth, ui_token::motion::HOVER_FADE.m_DurationSec, ui_token::motion::HOVER_FADE.m_Easing);
		}
		ReserveWidth = std::clamp(ReserveWidth, 0.0f, m_Params.m_ScrollbarThickness);
	}
	m_ScrollbarReserveWidth = ReserveWidth;

	if(m_Params.m_ScrollHorizontal)
		m_ClipRect.HSplitBottom(ReserveWidth, &m_ClipRect, &ScrollbarBg);
	else
		m_ClipRect.VSplitRight(ReserveWidth, &m_ClipRect, &ScrollbarBg);
	if(ScrollbarBg.w <= 0.0f || ScrollbarBg.h <= 0.0f)
	{
		m_RailRect = CUIRect{0.0f, 0.0f, 0.0f, 0.0f};
		return ScrollbarBg;
	}
	if(m_Params.m_ScrollbarNoOuterMargin)
	{
		if(m_Params.m_ScrollHorizontal)
		{
			ScrollbarBg.VMargin(m_Params.m_ScrollbarMargin, &m_RailRect);
			m_RailRect.HSplitTop(m_Params.m_ScrollbarMargin, nullptr, &m_RailRect);
		}
		else
		{
			ScrollbarBg.HMargin(m_Params.m_ScrollbarMargin, &m_RailRect);
			m_RailRect.VSplitLeft(m_Params.m_ScrollbarMargin, nullptr, &m_RailRect);
		}
	}
	else
	{
		ScrollbarBg.Margin(m_Params.m_ScrollbarMargin, &m_RailRect);
	}
	m_RailRect.w = std::max(0.0f, m_RailRect.w);
	m_RailRect.h = std::max(0.0f, m_RailRect.h);

	return ScrollbarBg;
}

void CScrollRegion::DrawBackground(const CUIRect &ScrollbarBg)
{
	// only show scrollbar if required
	if(ScrollbarShown())
	{
		if(m_Params.m_ScrollbarBgColor.a > 0.0f)
		{
			int Corners = m_Params.m_ScrollHorizontal ? IGraphics::CORNER_B : IGraphics::CORNER_R;
			DrawRoundedSurface(Ui(), ScrollbarBg, m_Params.m_ScrollbarBgColor, ColorRGBA(), 4.0f, 0.0f, Corners);
		}
		if(m_Params.m_RailBgColor.a > 0.0f)
		{
			float Rounding = m_Params.m_ScrollHorizontal ? m_RailRect.h / 2.0f : m_RailRect.w / 2.0f;
			// 背景倍率只弱化轨道，不再削弱前景滑块的强调色。
			DrawRoundedSurface(Ui(), m_RailRect, Ui()->ScaleBackgroundAlpha(m_Params.m_RailBgColor), ColorRGBA(), Rounding);
		}
	}
	if(m_Params.m_ClipBgColor.a > 0.0f)
	{
		int CornersPartial = m_Params.m_ScrollHorizontal ? IGraphics::CORNER_T : IGraphics::CORNER_L;
		DrawRoundedSurface(Ui(), m_ClipRect, m_Params.m_ClipBgColor, ColorRGBA(), 4.0f, 0.0f, ScrollbarShown() ? CornersPartial : IGraphics::CORNER_ALL);
	}
}

// 同一次按下只允许一个滚动区域接管内容拖动：嵌套时内层先于外层 End，
// 于是内层列表能滑动、外层页面不会被带着一起滚。
static const CScrollRegion *s_pContentDragOwner = nullptr;

void CScrollRegion::DoContentDrag()
{
	SQmScrollContentDragState &Drag = m_ScrollState.ContentDragState();
	if(s_pContentDragOwner != nullptr && s_pContentDragOwner != this)
	{
		Drag = {};
		m_ContentDragVelocity = 0.0f;
		m_ContentDragLastMoveTime = 0;
		return;
	}
	// 手机以外的平台没有内容拖动语义：这里必须清空，避免旧候选状态在新一帧继续生效。
	if(!QmScrollRegionContentDragEnabled())
	{
		Drag = {};
		m_ContentDragVelocity = 0.0f;
		m_ContentDragLastMoveTime = 0;
		return;
	}
	if(!Ui()->MouseButton(0))
	{
		if(s_pContentDragOwner == this)
			s_pContentDragOwner = nullptr;
		// 松手：刚拖过且速度够快就交给滚动状态做惯性衰减。
		const bool MovedRecently = m_ContentDragLastMoveTime != 0 && (time_get() - m_ContentDragLastMoveTime) <= time_freq() / 10;
		if(Drag.m_Active && MovedRecently && std::abs(m_ContentDragVelocity) >= QM_SCROLL_FLING_MIN_VELOCITY)
			m_ScrollState.Fling(m_ContentDragVelocity, ScrollMetrics());
		Drag = {};
		m_ContentDragVelocity = 0.0f;
		m_ContentDragLastMoveTime = 0;
		return;
	}
	// 滚动条滑块有独立语义，同一次按住不能再当作内容拖动。
	if(m_ScrollState.ThumbDragActive())
	{
		Drag = {};
		return;
	}

	const float MousePos = m_Params.m_ScrollHorizontal ? Ui()->MouseX() : Ui()->MouseY();
	if(!Drag.m_Candidate && !Drag.m_Active)
	{
		// 只在内容区内部起手。列表行本身是按钮，按下时会把 ActiveItem 抢走，
		// 所以这里不能要求 ActiveItem 为空，而是等位移超过阈值后再取消那次按下。
		if(!Ui()->MouseButtonClicked(0) || !Ui()->MouseHovered(&m_ClipRect))
			return;
		Drag.m_Candidate = true;
		Drag.m_PressMousePos = MousePos;
		Drag.m_PressOffset = m_ScrollState.Offset();
		Drag.m_LastMousePos = MousePos;
		m_ContentDragVelocity = 0.0f;
		m_ContentDragLastMoveTime = 0;
		return;
	}

	const float DragDistance = MousePos - Drag.m_PressMousePos;
	const float Threshold = QmScrollRegionContentDragThreshold(Ui()->PixelSize());
	if(!QmScrollRegionContentDragBecomesActive(Drag.m_Active, DragDistance, Threshold))
	{
		Drag.m_LastMousePos = MousePos;
		return;
	}
	if(!Drag.m_Active)
	{
		// 这一下已经变成拖动：取消被按下的列表项，松手时不再触发它的点击。
		if(Ui()->ActiveItem() != nullptr)
			Ui()->SetActiveItem(nullptr);
		Drag.m_Active = true;
		s_pContentDragOwner = this;
	}
	// 内容跟手：向上滑动内容即增大滚动偏移，允许越界以便松手后回弹。
	m_ScrollState.SetOffset(Drag.m_PressOffset - DragDistance * QM_SCROLL_TOUCH_DRAG_GAIN, ScrollMetrics(), ScrollConfig(), true);
	// 用相邻两次移动的时间差估计速度，再做一阶低通，避免单帧抖动决定甩动幅度。
	const int64_t Now = time_get();
	if(m_ContentDragLastMoveTime != 0)
	{
		const float Dt = (Now - m_ContentDragLastMoveTime) / (float)time_freq();
		if(Dt > 0.0005f)
		{
			const float FingerVelocity = (MousePos - Drag.m_LastMousePos) / Dt;
			const float Blend = std::clamp(Dt / 0.05f, 0.0f, 1.0f);
			m_ContentDragVelocity += (-FingerVelocity * QM_SCROLL_TOUCH_DRAG_GAIN - m_ContentDragVelocity) * Blend;
		}
	}
	m_ContentDragLastMoveTime = Now;
	Drag.m_LastMousePos = MousePos;
}

void CScrollRegion::DoScrollInput()
{
	if(m_ScrollDirection != SCROLLRELATIVE_NONE)
	{
		SQmScrollConfig Config = ScrollConfig();
		Config.m_WheelScale *= m_ScrollSpeedMultiplier;
		m_ScrollState.ScrollTo(m_ScrollState.Offset() + (int)m_ScrollDirection * Config.m_WheelScale, ScrollMetrics(), Config);
		m_ScrollDirection = SCROLLRELATIVE_NONE;
		m_ScrollSpeedMultiplier = 1.0f;
	}

	const bool HotFromPreviousFrame = Ui()->HotScrollRegion() == this;
	const bool HotThisFrame = Ui()->NextHotScrollRegion() == this;
	const bool WheelEligible = QmScrollRegionCanConsumeWheel(HotFromPreviousFrame, HotThisFrame, Ui()->UnderlyingScrollBlocked(), Ui()->RenderingPopupMenus());
	const void *pWheelOwnerId = m_Params.m_pWheelOwnerId != nullptr ? m_Params.m_pWheelOwnerId : this;
	if(!m_Params.m_WheelOwnerPreRegistered)
		Ui()->RegisterWheelOwner(pWheelOwnerId, m_Params.m_WheelOwnerPriority, WheelHotRect(), ContentOverflows() && WheelEligible);

	float WheelDelta = 0.0f;
	if(!Ui()->TryConsumeWheel(pWheelOwnerId, &WheelDelta))
		return;
	// Alt+滚轮 = 3 倍步长（加速滚动）。
	if(Input() != nullptr && Input()->AltIsPressed())
		WheelDelta *= 3.0f;
	m_WheelConsumedThisFrame = true;
	m_ScrollState.AddWheelImpulse(WheelDelta, ScrollMetrics(), ScrollConfig());
}

CUIRect CScrollRegion::WheelHotRect() const
{
	CUIRect RegionRect = m_ClipRect;
	if(ScrollbarShown())
	{
		if(m_Params.m_ScrollHorizontal)
			RegionRect.h += m_ScrollbarReserveWidth;
		else
			RegionRect.w += m_ScrollbarReserveWidth;
	}
	return RegionRect;
}

void CScrollRegion::UpdateHotScrollRegion()
{
	const CUIRect RegionRect = WheelHotRect();

	if(Ui()->Enabled() && Ui()->MouseHovered(&RegionRect))
	{
		Ui()->SetHotScrollRegion(this, m_Params.m_WheelOwnerPriority);
	}
}

void CScrollRegion::AdvanceAnimation()
{
	m_ScrollState.Advance(Client()->RenderFrameTime(), ScrollMetrics(), ScrollConfig());
}

void CScrollRegion::MaintainNoScrollSliderActive()
{
	const void *pId = &m_SliderId;
	const bool WasActive = Ui()->IsActiveItem(pId);
	const bool Active = ScrollRegionShouldKeepNoScrollSliderActive(WasActive, Ui()->MouseButton(0));
	m_ScrollState.ResetForNonScrollableContent(Active);
	if(Active)
		Ui()->SetActiveItem(pId);
	else if(WasActive)
	{
		Ui()->SetActiveItem(nullptr);
	}
}

void CScrollRegion::DoSlider()
{
	const SQmScrollMetrics Metrics = ScrollMetrics();
	const SQmScrollConfig Config = ScrollConfig();
	const float ScrollMax = Metrics.MaxOffset();
	if(m_Params.m_HideScrollbar)
	{
		m_ScrollState.Advance(0.0f, Metrics, Config);
		m_ScrollState.EndThumbDrag();
		return;
	}

	// 滑块与占位宽度走同一拍：轨道还在展开时滑块淡入，不会「硬切」冒出来。
	const float RevealAlpha = m_Params.m_ScrollbarThickness > 0.0f ?
					  std::clamp(m_ScrollbarReserveWidth / m_Params.m_ScrollbarThickness, 0.0f, 1.0f) :
					  1.0f;
	const float ClipSize = m_Params.m_ScrollHorizontal ? m_ClipRect.w : m_ClipRect.h;
	const float RailSize = m_Params.m_ScrollHorizontal ? m_RailRect.w : m_RailRect.h;
	const bool CanScroll = ContentOverflows() && ScrollMax > 0.0f && RailSize > 0.0f;
	const float SliderMaxSize = maximum(0.0f, RailSize);
	const float SliderMinSize = minimum(m_Params.m_SliderMinSize, SliderMaxSize);
	const float SliderSize = CanScroll ? std::clamp(ClipSize / m_ContentSize * RailSize, SliderMinSize, SliderMaxSize) : SliderMaxSize;

	CUIRect Slider = m_RailRect;
	float &SliderPos = m_Params.m_ScrollHorizontal ? Slider.x : Slider.y;
	if(m_Params.m_ScrollHorizontal)
		Slider.w = SliderSize;
	else
		Slider.h = SliderSize;

	const float MaxSlider = RailSize - SliderSize;
	const void *pId = &m_SliderId;

	if(!CanScroll || MaxSlider <= 0.0f)
	{
		if(!Ui()->RenderOnly())
			MaintainNoScrollSliderActive();
		const bool Active = Ui()->IsActiveItem(pId);
		const float Rounding = m_Params.m_ScrollHorizontal ? Slider.h / 2.0f : Slider.w / 2.0f;
		if(Ui()->QmAnimationRuntime() != nullptr)
		{
			CUiScopedControlAlphaScale SliderAlpha(Ui(), m_Params.m_SliderAlpha * RevealAlpha);
			ui_widget::DrawScrollbarHandle(ui_widget::ControlContext(Ui()), pId, Slider, m_Params.m_Interactive, nullptr, m_Params.m_pSliderThemeColor);
		}
		else
			DrawRoundedSurface(Ui(), Slider, m_Params.SliderColor(Active, Ui()->HotItem() == pId), ColorRGBA(), Rounding);
		return;
	}

	SliderPos += m_ScrollState.Offset() / ScrollMax * MaxSlider;

	const float MousePos = m_Params.m_ScrollHorizontal ? Ui()->MouseX() : Ui()->MouseY();
	const bool WasActive = Ui()->ActiveItem() == pId;
	if(m_Params.m_Interactive && !Ui()->RenderOnly())
		Ui()->DoButtonLogic(pId, 0, &m_RailRect, BUTTONFLAG_LEFT); // Result ignored, we only care about the button becoming and being active
	if(m_Params.m_Interactive && !Ui()->RenderOnly() && Ui()->CheckActiveItem(pId))
	{
		if(!WasActive)
		{
			const float GrabOffset = Ui()->MouseHovered(&Slider) ? (MousePos - SliderPos) : (SliderSize / 2.0f);
			m_ScrollState.BeginThumbDrag(std::clamp(GrabOffset, 0.0f, SliderSize));
		}
		m_ScrollState.SetOffset(m_ScrollState.Offset() + (MousePos - (SliderPos + m_ScrollState.ThumbDragGrabOffset())) / MaxSlider * ScrollMax, Metrics, Config);
	}
	else if(!Ui()->RenderOnly())
	{
		m_ScrollState.EndThumbDrag();
	}

	const float Rounding = m_Params.m_ScrollHorizontal ? Slider.h / 2.0f : Slider.w / 2.0f;
	if(Ui()->QmAnimationRuntime() != nullptr)
	{
		CUiScopedControlAlphaScale SliderAlpha(Ui(), m_Params.m_SliderAlpha * RevealAlpha);
		ui_widget::DrawScrollbarHandle(ui_widget::ControlContext(Ui()), pId, Slider, m_Params.m_Interactive, nullptr, m_Params.m_pSliderThemeColor);
	}
	else
		DrawRoundedSurface(Ui(), Slider, m_Params.SliderColor(Ui()->CheckActiveItem(pId), Ui()->HotItem() == pId), ColorRGBA(), Rounding);
}
