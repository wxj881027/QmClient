// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "ui_scrollregion.h"

#include <base/system.h>
#include <base/vmath.h>

#include <engine/client.h>
#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/client/QmUi/UiSurface.h>

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

	if(!ContentOverflows())
		m_ScrollState.ResetForNonScrollableContent(Ui()->IsActiveItem(&m_SliderId) && Ui()->MouseButton(0));
	m_ContentSize = 0.0f;

	Ui()->ClipEnable(&m_ClipRect);

	*pClipRect = m_ClipRect;
	*pOutOffset = ContentScrollOffset();
}

void CScrollRegion::End()
{
	Ui()->ClipDisable();

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
	if(ShouldScrollHere)
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
	if(m_Params.m_ScrollHorizontal)
		m_ClipRect.HSplitBottom(m_Params.m_ScrollbarThickness, ReserveScrollbarSpace ? &m_ClipRect : nullptr, &ScrollbarBg);
	else
		m_ClipRect.VSplitRight(m_Params.m_ScrollbarThickness, ReserveScrollbarSpace ? &m_ClipRect : nullptr, &ScrollbarBg);
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
			DrawRoundedSurface(Ui(), m_RailRect, m_Params.m_RailBgColor, ColorRGBA(), Rounding);
		}
	}
	if(m_Params.m_ClipBgColor.a > 0.0f)
	{
		int CornersPartial = m_Params.m_ScrollHorizontal ? IGraphics::CORNER_T : IGraphics::CORNER_L;
		DrawRoundedSurface(Ui(), m_ClipRect, m_Params.m_ClipBgColor, ColorRGBA(), 4.0f, 0.0f, ScrollbarShown() ? CornersPartial : IGraphics::CORNER_ALL);
	}
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
	m_WheelConsumedThisFrame = true;
	m_ScrollState.AddWheelImpulse(WheelDelta, ScrollMetrics(), ScrollConfig());
}

CUIRect CScrollRegion::WheelHotRect() const
{
	CUIRect RegionRect = m_ClipRect;
	if(ScrollbarShown())
	{
		if(m_Params.m_ScrollHorizontal)
			RegionRect.h += m_Params.m_ScrollbarThickness;
		else
			RegionRect.w += m_Params.m_ScrollbarThickness;
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
		MaintainNoScrollSliderActive();
		const bool Active = Ui()->IsActiveItem(pId);
		const float Rounding = m_Params.m_ScrollHorizontal ? Slider.h / 2.0f : Slider.w / 2.0f;
		DrawRoundedSurface(Ui(), Slider, m_Params.SliderColor(Active, Ui()->HotItem() == pId), ColorRGBA(), Rounding);
		return;
	}

	SliderPos += m_ScrollState.Offset() / ScrollMax * MaxSlider;

	const float MousePos = m_Params.m_ScrollHorizontal ? Ui()->MouseX() : Ui()->MouseY();
	const bool WasActive = Ui()->ActiveItem() == pId;
	if(m_Params.m_Interactive)
		Ui()->DoButtonLogic(pId, 0, &m_RailRect, BUTTONFLAG_LEFT); // Result ignored, we only care about the button becoming and being active
	if(m_Params.m_Interactive && Ui()->CheckActiveItem(pId))
	{
		if(!WasActive)
		{
			const float GrabOffset = Ui()->MouseHovered(&Slider) ? (MousePos - SliderPos) : (SliderSize / 2.0f);
			m_ScrollState.BeginThumbDrag(std::clamp(GrabOffset, 0.0f, SliderSize));
		}
		m_ScrollState.SetOffset(m_ScrollState.Offset() + (MousePos - (SliderPos + m_ScrollState.ThumbDragGrabOffset())) / MaxSlider * ScrollMax, Metrics, Config);
	}
	else
	{
		m_ScrollState.EndThumbDrag();
	}

	const float Rounding = m_Params.m_ScrollHorizontal ? Slider.h / 2.0f : Slider.w / 2.0f;
	DrawRoundedSurface(Ui(), Slider, m_Params.SliderColor(Ui()->CheckActiveItem(pId), Ui()->HotItem() == pId), ColorRGBA(), Rounding);
}
