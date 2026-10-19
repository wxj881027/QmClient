/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "ui_listbox.h"

#include <base/system.h>
#include <base/vmath.h>

#include <engine/config.h>
#include <engine/shared/config.h>

#include <game/client/QmUi/UiSurface.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

CListBox::CListBox()
{
	Reset();
}

void CListBox::Reset()
{
	m_ListBoxView = m_RowView = CUIRect{0.0f, 0.0f, 0.0f, 0.0f};
	m_ListBoxUpdateScroll = false;
	m_ScrollbarShown = false;
	m_AutoSpacing = 0.0f;
	m_ScrollRegion.Reset();
	m_ScrollProfile = EQmScrollProfile::MENU_LIST;
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsForSize(EQmScrollSize::MEDIUM);
	m_WheelOwnerPriority = EUiWheelOwnerPriority::PAGE;
	m_ScrollbarWidth = ScrollParams.m_ScrollbarThickness;
	m_ScrollbarMargin = ScrollParams.m_ScrollbarMargin;
	m_ScrollbarWidthOverridden = false;
	m_ScrollbarMarginOverridden = false;
	m_HasHeader = false;
	m_Active = true;
	m_HideScrollbar = false;
	m_ScrollbarAlwaysReserved = false;
	m_InitialScrollPending = true;
	m_EntryAnimationElapsedSec = -1.0f;
	m_EntryAnimation.Reset();
	m_EntryAlphaScope.reset();
	m_EntryBaseIndexCaptured = false;
	m_BackgroundCorners = IGraphics::CORNER_ALL;
	m_SelectedItemActiveColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f);
	m_SelectedItemInactiveColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.33f);
	m_HoveredItemColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.33f);
	m_SelectedFeedback.Reset();
	m_HoverFeedback.Reset();
	m_ScrollFeedbackSnap = false;
	m_FeedbackNumItems = -1;
}

void CListBox::DoHeader(const CUIRect *pRect, const char *pTitle, float HeaderHeight, float Spacing, int BackgroundCorners)
{
	CUIRect View = *pRect;
	CUIRect Header;
	m_BackgroundCorners = BackgroundCorners;

	// background
	View.HSplitTop(HeaderHeight + Spacing, &Header, nullptr);
	DrawRoundedSurface(Ui(), Header, Ui()->ScaleBackgroundAlpha(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f)), ColorRGBA(), 5.0f, 0.0f, m_BackgroundCorners & IGraphics::CORNER_T);

	// draw header
	View.HSplitTop(HeaderHeight, &Header, &View);
	Ui()->DoLabel(&Header, pTitle, Header.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_MC);

	View.HSplitTop(Spacing, &Header, &View);

	// setup the variables
	m_ListBoxView = View;
	m_HasHeader = true;
}

void CListBox::DoSpacing(float Spacing)
{
	CUIRect View = m_ListBoxView;
	View.HSplitTop(Spacing, nullptr, &View);
	m_ListBoxView = View;
}

void CListBox::DoStart(float RowHeight, int NumItems, int ItemsPerRow, int RowsPerScroll, int SelectedIndex, const CUIRect *pRect, bool Background, int BackgroundCorners)
{
	CUIRect View;
	if(pRect)
		View = *pRect;
	else
		View = m_ListBoxView;

	// background
	m_BackgroundCorners = BackgroundCorners;
	if(Background)
		DrawRoundedSurface(Ui(), View, Ui()->ScaleBackgroundAlpha(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f)), ColorRGBA(), 5.0f, 0.0f, m_BackgroundCorners & (m_HasHeader ? IGraphics::CORNER_B : IGraphics::CORNER_ALL));

	// setup the variables
	m_ListBoxView = View;
	m_RowView = {};
	m_ListBoxSelectedIndex = SelectedIndex;
	m_ListBoxNewSelected = SelectedIndex;
	m_ListBoxNewSelOffset = 0;
	m_ListBoxItemIndex = 0;
	m_ListBoxRowHeight = RowHeight;
	m_ListBoxNumItems = NumItems;
	m_ListBoxItemsPerRow = ItemsPerRow;
	m_ListBoxItemActivated = false;
	m_ListBoxItemSelected = false;
	const int64_t Now = time_get();
	const int64_t Frequency = maximum<int64_t>(1, time_freq());
	const bool EntryAnimationEnabled = g_Config.m_QmUiListEntryAnimations != 0 && g_Config.m_QmUiMotionLevel > 0;
	// F1 后台仍显示同一列表；只有纯测量不参与可见动画生命周期。
	const EQmListEntryRenderPass AnimationPass = Ui()->MeasurementOnly()  ? EQmListEntryRenderPass::MEASUREMENT :
						     Ui()->BackgroundRender() ? EQmListEntryRenderPass::BACKGROUND :
										EQmListEntryRenderPass::INTERACTIVE;
	const float EntryElapsed = m_EntryAnimation.Resolve(Now, Frequency, EntryAnimationEnabled, g_Config.m_QmUiMotionLevel, AnimationPass);
	m_EntryAnimationElapsedSec = EntryElapsed;
	m_EntryAlphaScope.reset();
	m_EntryBaseIndexCaptured = false;
	if(QmListBoxShouldScrollToInitialSelection(m_InitialScrollPending, SelectedIndex))
	{
		m_ListBoxUpdateScroll = true;
	}
	m_InitialScrollPending = QmListBoxInitialScrollRemainsPending(m_InitialScrollPending, SelectedIndex);

	// handle input
	if(m_Active && !Input()->ModifierIsPressed() && !Input()->ShiftIsPressed() && !Input()->AltIsPressed())
	{
		if(Ui()->ConsumeHotkey(CUi::HOTKEY_DOWN))
			m_ListBoxNewSelOffset += m_ListBoxItemsPerRow;
		else if(Ui()->ConsumeHotkey(CUi::HOTKEY_UP))
			m_ListBoxNewSelOffset -= m_ListBoxItemsPerRow;
		else if(Ui()->ConsumeHotkey(CUi::HOTKEY_RIGHT) && m_ListBoxItemsPerRow > 1)
			m_ListBoxNewSelOffset += 1;
		else if(Ui()->ConsumeHotkey(CUi::HOTKEY_LEFT) && m_ListBoxItemsPerRow > 1)
			m_ListBoxNewSelOffset -= 1;
		else if(Ui()->ConsumeHotkey(CUi::HOTKEY_PAGE_UP))
			m_ListBoxNewSelOffset = -ItemsPerRow * RowsPerScroll * 4;
		else if(Ui()->ConsumeHotkey(CUi::HOTKEY_PAGE_DOWN))
			m_ListBoxNewSelOffset = ItemsPerRow * RowsPerScroll * 4;
		else if(Ui()->ConsumeHotkey(CUi::HOTKEY_HOME))
			m_ListBoxNewSelOffset = 1 - m_ListBoxNumItems;
		else if(Ui()->ConsumeHotkey(CUi::HOTKEY_END))
			m_ListBoxNewSelOffset = m_ListBoxNumItems - 1;
	}

	// setup the scrollbar
	vec2 ScrollOffset = vec2(0.0f, 0.0f);
	SQmScrollRequest ScrollRequest;
	ScrollRequest.m_Profile = m_ScrollProfile;
	ScrollRequest.m_RowExtent = m_ListBoxRowHeight + m_AutoSpacing;
	ScrollRequest.m_RowsPerStep = RowsPerScroll;
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest);
	CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	ScrollParams.m_WheelOwnerPriority = m_WheelOwnerPriority;
	ScrollParams.m_HideScrollbar = m_HideScrollbar;
	ScrollParams.m_ScrollbarAlwaysReserved = m_ScrollbarAlwaysReserved;
	m_ScrollbarWidth = QmListBoxScrollbarMetric(ScrollParams.m_ScrollbarThickness, m_ScrollbarWidth, m_ScrollbarWidthOverridden);
	m_ScrollbarMargin = QmListBoxScrollbarMetric(ScrollParams.m_ScrollbarMargin, m_ScrollbarMargin, m_ScrollbarMarginOverridden);
	ScrollParams.m_ScrollbarThickness = m_ScrollbarWidth;
	ScrollParams.m_ScrollbarMargin = m_ScrollbarMargin;
	const int NumRows = (m_ListBoxNumItems + maximum(1, m_ListBoxItemsPerRow) - 1) / maximum(1, m_ListBoxItemsPerRow);
	m_ScrollRegion.SetContentHeightForNextFrame(NumRows * m_ListBoxRowHeight + maximum(0, NumRows - 1) * m_AutoSpacing);
	m_ScrollRegion.Begin(&m_ListBoxView, &ScrollOffset, &ScrollParams);
	m_ListBoxView.y += ScrollOffset.y;

	// 选中/悬停反馈与滚动同帧：滚动时直接 snap，停止后再多 snap 一帧，
	// 避免上一帧的行索引在滚动位移中错位成另一行。
	const bool ScrollAnimating = m_ScrollRegion.Animating();
	// 条目数变化通常意味着筛选/换页，旧行索引不再指向同一项，直接 snap，
	// 避免旧的选中淡出落在一条无关的新行上。
	const bool NumItemsChanged = m_FeedbackNumItems != NumItems;
	m_FeedbackNumItems = NumItems;
	const bool Snap = g_Config.m_QmUiMotionLevel == 0 || ScrollAnimating || m_ScrollFeedbackSnap || NumItemsChanged;
	m_ScrollFeedbackSnap = ScrollAnimating;
	const bool UpdateFeedback = !Ui()->AnimationsBlocked();
	const uint64_t FeedbackKey = BuildUiAnimNodeKey(str_quickhash("qm-listbox-row-feedback"), reinterpret_cast<uint64_t>(this));
	m_SelectedFeedback.BeginFrame(Ui()->QmAnimationRuntime(), FeedbackKey, Snap, UpdateFeedback);
	m_HoverFeedback.BeginFrame(Ui()->QmAnimationRuntime(), FeedbackKey ^ 0x9E3779B97F4A7C15ULL, Snap, UpdateFeedback);
}

CListboxItem CListBox::DoNextRow()
{
	QmRestoreEntryScope();
	CListboxItem Item = {};

	if(m_ListBoxItemIndex % m_ListBoxItemsPerRow == 0)
		m_ListBoxView.HSplitTop(m_ListBoxRowHeight, &m_RowView, &m_ListBoxView);
	m_ScrollRegion.AddRect(m_RowView);
	if(m_ListBoxUpdateScroll && m_ListBoxSelectedIndex == m_ListBoxItemIndex)
	{
		m_ScrollRegion.ScrollHere(CScrollRegion::SCROLLHERE_KEEP_IN_VIEW);
		m_ListBoxUpdateScroll = false;
	}

	m_RowView.VSplitLeft(m_RowView.w / (m_ListBoxItemsPerRow - m_ListBoxItemIndex % m_ListBoxItemsPerRow), &Item.m_Rect, &m_RowView);

	Item.m_Selected = m_ListBoxSelectedIndex == m_ListBoxItemIndex;
	// 依次自裁剪框内浮现：行内容先画在最终矩形下方位移处，同时把裁剪
	// 收紧到「最终矩形 ∩ 滚动裁剪」，被遮蔽的部分随弹入逐步露出（maskRise）。
	if(!m_EntryBaseIndexCaptured)
	{
		m_EntryBaseItemIndex = m_ListBoxItemIndex;
		m_EntryBaseIndexCaptured = true;
	}
	const SQmListEntryVisual EntryVisual = QmListEntryVisual(m_EntryAnimationElapsedSec, (m_ListBoxItemIndex - m_EntryBaseItemIndex) / maximum(1, m_ListBoxItemsPerRow), g_Config.m_QmUiMotionLevel);
	if(EntryVisual.m_Fade < 0.999f)
		m_EntryAlphaScope.emplace(Ui(), EntryVisual.m_Fade);
	Item.m_Rect = QmListBoxEntryAnimatedRect(Item.m_Rect, m_EntryAnimationFadeOnly ? 0.0f : EntryVisual.m_OffsetY);
	Item.m_Visible = !m_ScrollRegion.RectClipped(Item.m_Rect);

	m_ListBoxItemIndex++;
	return Item;
}

void CListBox::QmRestoreEntryScope()
{
	// 父层显隐系数（CUiScopedControlAlphaScale）随行结束一并恢复。
	// 注意 CUi::ClipEnable 是裁剪栈（Enable=压入交集、Disable=出栈），
	// 若未来行级需要裁剪，恢复必须用 ClipDisable 而非再 ClipEnable。
	m_EntryAlphaScope.reset();
}

CListboxItem CListBox::DoNextItem(const void *pId, bool Selected, float CornerRadius)
{
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
	if(m_AutoSpacing > 0.0f && m_ListBoxItemIndex > 0)
		DoSpacing(m_AutoSpacing);

	const int ThisItemIndex = m_ListBoxItemIndex;
	if(Selected)
	{
		if(QmListBoxShouldScrollToInitialSelection(m_InitialScrollPending, ThisItemIndex))
			m_ListBoxUpdateScroll = true;
		m_InitialScrollPending = QmListBoxInitialScrollRemainsPending(m_InitialScrollPending, ThisItemIndex);
		if(m_ListBoxSelectedIndex == m_ListBoxNewSelected)
			m_ListBoxNewSelected = ThisItemIndex;
		m_ListBoxSelectedIndex = ThisItemIndex;
	}

	CListboxItem Item = DoNextRow();
	Item.m_pUi = Ui();
	Item.m_GaussianBlurSuppressed = true;
	const int ItemClicked = Item.m_Visible ? Ui()->DoButtonLogic(pId, 0, &Item.m_Rect, BUTTONFLAG_LEFT) : 0;
	if(ItemClicked)
	{
		m_ListBoxNewSelected = ThisItemIndex;
		m_ListBoxItemSelected = true;
		m_Active = true;
	}

	// process input, regard selected index
	if(m_ListBoxNewSelected == ThisItemIndex)
	{
		if(m_Active)
		{
			if(Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || (ItemClicked == 1 && Ui()->DoDoubleClickLogic(pId)))
			{
				m_ListBoxItemActivated = true;
				Ui()->SetActiveItem(nullptr);
			}
		}

		m_SelectedFeedback.Observe(ThisItemIndex);
	}
	// 选中/悬浮反馈默认铺满条目矩形（行列表）。卡片网格设置了内容内缩，
	// 反馈矩形贴合卡片本身。选中描边固定白色（用户指定）——不走亮度自动
	// 前景：选中填充为白色低透明，与底合成后亮度会跨过阈值被解析成黑。
	// 旧选中行即使已经不再被选中，也要继续画到淡出结束，才能和新行走成
	// 真正的交叉淡变，而不是先消失再淡入。
	const float SelectionAlpha = m_SelectedFeedback.Alpha(ThisItemIndex);
	if(SelectionAlpha > 0.001f)
	{
		CUIRect SelectionRect = Item.m_Rect;
		if(m_ItemSelectionInset > 0.0f)
			SelectionRect.Margin(m_ItemSelectionInset, &SelectionRect);
		const ColorRGBA SelectionFill = Ui()->ScaleBackgroundAlpha(m_Active ? m_SelectedItemActiveColor : m_SelectedItemInactiveColor).WithMultipliedAlpha(SelectionAlpha);
		ColorRGBA SelectionBorder = ColorRGBA();
		if(m_ItemSelectionInset > 0.0f)
			SelectionBorder = ui_token::color::NEUTRAL_WHITE.WithAlpha(ui_token::feedback::ICON_BORDER_ALPHA * SelectionAlpha);
		DrawRoundedSurface(Ui(), SelectionRect, SelectionFill, SelectionBorder, CornerRadius, 1.0f);
	}
	if(Ui()->HotItem() == pId)
		m_HoverFeedback.Observe(ThisItemIndex);
	const float HoverAlpha = m_HoverFeedback.Alpha(ThisItemIndex);
	if(HoverAlpha > 0.001f && !m_ScrollRegion.Animating())
	{
		CUIRect HoverRect = Item.m_Rect;
		if(m_ItemSelectionInset > 0.0f)
			HoverRect.Margin(m_ItemSelectionInset, &HoverRect);
		DrawRoundedSurface(Ui(), HoverRect, Ui()->ScaleBackgroundAlpha(m_HoveredItemColor).WithMultipliedAlpha(HoverAlpha), ColorRGBA(), CornerRadius);
	}

	return Item;
}

void CListBox::SkipItems(int Count)
{
	Count = std::max(0, Count);
	while(Count > 0 && m_ListBoxItemIndex < m_ListBoxNumItems)
	{
		if(m_ListBoxItemIndex % m_ListBoxItemsPerRow == 0)
		{
			m_ListBoxView.HSplitTop(m_ListBoxRowHeight, &m_RowView, &m_ListBoxView);
			m_ScrollRegion.AddRect(m_RowView);
		}

		const int ItemIndexInRow = m_ListBoxItemIndex % m_ListBoxItemsPerRow;
		const int ItemsLeftInRow = m_ListBoxItemsPerRow - ItemIndexInRow;
		if(m_ListBoxUpdateScroll &&
			m_ListBoxSelectedIndex >= m_ListBoxItemIndex &&
			m_ListBoxSelectedIndex < m_ListBoxItemIndex + ItemsLeftInRow)
		{
			m_ScrollRegion.ScrollHere(CScrollRegion::SCROLLHERE_KEEP_IN_VIEW);
			m_ListBoxUpdateScroll = false;
		}

		const int ItemsToSkip = std::min(Count, ItemsLeftInRow);
		for(int i = 0; i < ItemsToSkip; ++i)
		{
			CUIRect Skipped;
			m_RowView.VSplitLeft(m_RowView.w / (m_ListBoxItemsPerRow - m_ListBoxItemIndex % m_ListBoxItemsPerRow), &Skipped, &m_RowView);
			++m_ListBoxItemIndex;
		}
		Count -= ItemsToSkip;
	}
}

CListboxItem CListBox::DoCustomRow(float Height, bool ScrollHere)
{
	QmRestoreEntryScope();
	CListboxItem Item = {};
	m_ListBoxView.HSplitTop(Height, &Item.m_Rect, &m_ListBoxView);
	m_ScrollRegion.AddRect(Item.m_Rect, ScrollHere);
	if(!m_EntryBaseIndexCaptured)
	{
		m_EntryBaseItemIndex = m_ListBoxItemIndex;
		m_EntryBaseIndexCaptured = true;
	}
	const SQmListEntryVisual EntryVisual = QmListEntryVisual(m_EntryAnimationElapsedSec, (m_ListBoxItemIndex - m_EntryBaseItemIndex) / maximum(1, m_ListBoxItemsPerRow), g_Config.m_QmUiMotionLevel);
	if(EntryVisual.m_Fade < 0.999f)
		m_EntryAlphaScope.emplace(Ui(), EntryVisual.m_Fade);
	Item.m_Rect = QmListBoxEntryAnimatedRect(Item.m_Rect, m_EntryAnimationFadeOnly ? 0.0f : EntryVisual.m_OffsetY);
	Item.m_Visible = !m_ScrollRegion.RectClipped(Item.m_Rect);
	return Item;
}

CListboxItem CListBox::DoSubheader()
{
	CListboxItem Item = DoNextRow();
	DrawRoundedSurface(Ui(), Item.m_Rect, Ui()->ScaleBackgroundAlpha(ColorRGBA(1.0f, 1.0f, 1.0f, 0.2f)), ColorRGBA(), 0.0f, 0.0f, IGraphics::CORNER_NONE);
	return Item;
}

int CListBox::DoEnd()
{
	QmRestoreEntryScope();
	m_SelectedFeedback.EndFrame();
	m_HoverFeedback.EndFrame();
	m_ScrollRegion.End();
	m_Active |= m_ScrollRegion.Active();

	m_ScrollbarShown = m_ScrollRegion.ScrollbarShown();
	if(m_ListBoxItemSelected || m_ScrollRegion.WheelConsumedThisFrame() || m_ScrollRegion.Active())
		m_InitialScrollPending = false;
	if(m_ListBoxNewSelOffset != 0 && m_ListBoxNumItems > 0 && m_ListBoxSelectedIndex == m_ListBoxNewSelected)
	{
		if(m_ListBoxNewSelected == -1)
			m_ListBoxNewSelected = 0;
		else
			m_ListBoxNewSelected = std::clamp(m_ListBoxNewSelected + m_ListBoxNewSelOffset, 0, m_ListBoxNumItems - 1);
		ScrollToSelected();
	}
	return m_ListBoxNewSelected;
}
