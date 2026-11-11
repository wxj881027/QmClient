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
	m_AnimateScrollbarReserve = true;
	m_InitialScrollPending = true;
	m_EntryAnimationElapsedSec = -1.0f;
	m_EntryAnimation.Reset();
	m_EntryAlphaScope.reset();
	m_EntryBaseIndexCaptured = false;
	m_BackgroundCorners = IGraphics::CORNER_ALL;
	// 非胶囊列表的默认选中/悬浮与胶囊行同源：选中 = 主题选中色（与下拉弹层一致），
	// 悬浮 = 列表 token，不再各自写白 0.5/0.33 两档。成员列表在 CUi 就绪前就会
	// 构造，这里先放 token 占位，真正取值在 DoStart 里按当前主题刷新。
	if(Ui() != nullptr)
	{
		m_SelectedItemActiveColor = Ui()->QmControlTheme().m_Selected;
		m_SelectedItemInactiveColor = Ui()->QmControlTheme().m_Selected;
	}
	else
	{
		m_SelectedItemActiveColor = ui_token::color::LIST_ITEM_SELECTED;
		m_SelectedItemInactiveColor = ui_token::color::LIST_ITEM_SELECTED;
	}
	m_HoveredItemColor = ui_token::color::LIST_ITEM_HOVER;
	m_SelectedFeedback.Reset();
	m_HoverFeedback.Reset();
	m_ScrollFeedbackSnap = false;
	m_FeedbackNumItems = -1;
	m_CapsuleRows = false;
	m_ItemColorsOverridden = false;
	m_RowInsetX = ui_token::list::RowInsetX(ui_token::font::BODY);
	m_AutoSpacingOverridden = false;
	m_MinVisibleRows = 0;
	m_MaxVisibleRows = 0;
	m_ScrollbarOverlay = false;
	m_ContainerRadius = ui_token::radius::SMALL_MENU;
	m_HeaderThisFrame = false;
	m_BackgroundDrawn = false;
}

void CListBox::SetCapsuleRows(float FontSize)
{
	m_CapsuleRows = true;
	// 行内左右内边距与弹层条目同源：文字不再贴着胶囊左缘，窄窗口也不跟着挤没。
	m_RowInsetX = ui_token::list::RowInsetX(FontSize > 0.0f ? FontSize : ui_token::font::BODY);
	// 胶囊行默认用覆盖式滚动条：滚动条出现不会挤压行宽，行内容不会横跳。
	m_ScrollbarOverlay = true;
	// 自适应高度也随统一外观生效：默认最多 3 行，调用点要更多行时用
	// SetVisibleRowRange 显式放宽（显示模式 8 行、皮肤队列 6 行）。
	if(m_MaxVisibleRows <= 0)
		SetVisibleRowRange(1, ui_token::list::DEFAULT_MAX_VISIBLE_ROWS);
	// 选中/悬停填充统一走主题：选中色与下拉弹层条目同一条来源
	// （QmSettingsDropdownVisualStyle().m_ActiveEntryColor = Theme.m_Selected），
	// 列表与弹层不再各持一份「选中底色」。调用点显式 SetItemColors 过就以后者为准。
	if(!m_ItemColorsOverridden)
	{
		m_SelectedItemActiveColor = Ui()->QmControlTheme().m_Selected;
		m_SelectedItemInactiveColor = Ui()->QmControlTheme().m_Selected;
		m_HoveredItemColor = ui_token::color::LIST_ITEM_HOVER;
	}
}

void CListBox::SetVisibleRowRange(int MinRows, int MaxRows)
{
	m_MinVisibleRows = maximum(0, MinRows);
	m_MaxVisibleRows = maximum(m_MinVisibleRows, MaxRows);
}

float CListBox::RowReserveWidth() const
{
	// 未开胶囊行或覆盖式轨道被关掉时，行宽本来就不受滚动条影响。
	if(!m_CapsuleRows || !m_ScrollbarOverlay)
		return 0.0f;
	// 调用点显式覆盖过就用覆盖值，否则直接用 token：表头在 DoStart 之前就要算列宽，
	// 不能用上一帧才解析出来的 m_ScrollbarWidth（首帧还是 20 的那一档）。
	const float Thickness = m_ScrollbarWidthOverridden ? m_ScrollbarWidth : ui_token::list::SCROLLBAR_WIDTH;
	const float Margin = m_ScrollbarMarginOverridden ? m_ScrollbarMargin : ui_token::list::SCROLLBAR_MARGIN;
	return Thickness + Margin + ui_token::list::SCROLLBAR_ROW_GAP;
}

float CListBox::ResolveRowCornerRadius() const
{
	// 只有显式开启胶囊行（SetCapsuleRows）的列表才换成胶囊横条：它会同时带来行距、
	// 行内边距和覆盖式滚动条，四件事是一整套。没开启的列表（服务器浏览器、demo、
	// 投票等既有页面）保持原来的小圆角，不被顺带改观感。
	if(m_CapsuleRows && m_ListBoxItemsPerRow <= 1)
		return ui_token::list::RowRadius(m_ListBoxRowHeight);
	// 多列网格里的格子保留旧小圆角（格子本身有卡片外形）。
	return ui_token::radius::SMALL_MENU;
}

void CListBox::DoHeader(const CUIRect *pRect, const char *pTitle, float HeaderHeight, float Spacing, int BackgroundCorners)
{
	CUIRect View = *pRect;
	CUIRect Header;
	m_BackgroundCorners = BackgroundCorners;
	// 标题和列表体共用同一个容器圆角：这里拿到的就是整容器，先按它算出来记住，
	// 紧随其后的 DoStart 只拿到列表体，沿用这一档才不会上下两段圆角不一致。
	m_ContainerRadius = ui_token::list::ContainerRadius(pRect->w, pRect->h);
	m_HeaderThisFrame = true;

	// background
	View.HSplitTop(HeaderHeight + Spacing, &Header, nullptr);
	DrawRoundedSurface(Ui(), Header, Ui()->ScaleBackgroundAlpha(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f)), ColorRGBA(), m_ContainerRadius, 0.0f, m_BackgroundCorners & IGraphics::CORNER_T);

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

	// setup the variables
	m_BackgroundCorners = BackgroundCorners;
	m_RowView = {};
	m_ListBoxSelectedIndex = SelectedIndex;
	m_ListBoxNewSelected = SelectedIndex;
	m_ListBoxNewSelOffset = 0;
	m_ListBoxItemIndex = 0;
	m_ListBoxRowHeight = RowHeight > 0.0f ? RowHeight : ui_token::list::ROW_HEIGHT;
	m_ListBoxNumItems = NumItems;
	m_ListBoxItemsPerRow = ItemsPerRow;
	m_ListBoxItemActivated = false;
	m_ListBoxItemSelected = false;
	// 选中/悬浮默认色每帧按当前主题刷新：成员列表构造期 CUi 还没就绪，主题也可以
	// 热切换；调用点显式 SetItemColors 过就保留调用点那一组。
	if(!m_ItemColorsOverridden)
	{
		m_SelectedItemActiveColor = Ui()->QmControlTheme().m_Selected;
		m_SelectedItemInactiveColor = Ui()->QmControlTheme().m_Selected;
		m_HoveredItemColor = ui_token::color::LIST_ITEM_HOVER;
	}
	// 胶囊行：行距由组件统一给（调用点显式 DoAutoSpacing 过就不覆盖）。
	if(m_CapsuleRows && !m_AutoSpacingOverridden)
		m_AutoSpacing = ui_token::list::ROW_SPACING;
	// 包裹式胶囊行不额外上下内缩：滚动区就等于调用方给的矩形，高度是否「严格整数行」
	// 由调用方用 SetVisibleRowRange + RowsHeight 决定（所有胶囊列表都这样算，见 UiTokens 的
	// VisibleRows/RowsHeight）。组件在这里再收 8px 只会让别人的整数行变成半行。
	m_RowReserveRight = 0.0f;
	// 自适应高度：按条目数和行高算出内容高度，只在调用方给的上限内取，调用点不必
	// 再自己 std::min；超出的部分交给滚动条。空列表不占高度。
	if(m_MaxVisibleRows > 0)
	{
		const int VisibleRows = ui_token::list::VisibleRows(m_ListBoxNumItems, m_ListBoxItemsPerRow, m_MinVisibleRows, m_MaxVisibleRows);
		View.h = minimum(View.h, ui_token::list::RowsHeight(VisibleRows, m_ListBoxRowHeight, m_AutoSpacing));
	}
	// 容器圆角按容器长宽推导；带标题的列表沿用 DoHeader 用整容器算出的那一档。
	if(!m_HeaderThisFrame)
		m_ContainerRadius = ui_token::list::ContainerRadius(View.w, View.h);
	m_HeaderThisFrame = false;
	m_ListBoxView = View;

	// background
	m_BackgroundDrawn = Background;
	if(Background)
		DrawRoundedSurface(Ui(), View, Ui()->ScaleBackgroundAlpha(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f)), ColorRGBA(), m_ContainerRadius, 0.0f, m_BackgroundCorners & (m_HasHeader ? IGraphics::CORNER_B : IGraphics::CORNER_ALL));
	const int64_t Now = time_get();
	const int64_t Frequency = maximum<int64_t>(1, time_freq());
	// 只有在「上一帧没画过」（切页、关闭后重开）时才算重新出现：连续渲染不重复滚动，
	// 避免用户刚滚走就被拉回去。此前只在首次渲染滚一次，重开面板常停在错误位置。
	// 250ms 阈值：单帧卡顿也会超过 50ms，50ms 会把连续渲染误判成重新出现。
	if(m_LastDrawTime != 0 && Now - m_LastDrawTime > Frequency / 4 && SelectedIndex >= 0)
		m_ListBoxUpdateScroll = true;
	m_LastDrawTime = Now;
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
	ScrollParams.m_AnimateScrollbarReserve = m_AnimateScrollbarReserve;
	// 覆盖式滚动条：不预留内容宽度，出现/消失只走显示系数与淡入滑出，
	// 行宽在有没有滚动条时完全一致。细轨比面板级滚动条窄，否则会压住行文字。
	ScrollParams.m_ScrollbarOverlay = m_ScrollbarOverlay;
	ScrollParams.m_ScrollbarRevealSec = m_ScrollbarOverlay ? ui_token::list::SCROLLBAR_FADE.m_DurationSec : 0.0f;
	ScrollParams.m_ScrollbarSlide = m_ScrollbarOverlay ? ui_token::list::SCROLLBAR_SLIDE : 0.0f;
	if(m_ScrollbarOverlay)
	{
		if(!m_ScrollbarWidthOverridden)
			ScrollParams.m_ScrollbarThickness = ui_token::list::SCROLLBAR_WIDTH;
		if(!m_ScrollbarMarginOverridden)
			ScrollParams.m_ScrollbarMargin = ui_token::list::SCROLLBAR_MARGIN;
	}
	m_ScrollbarWidth = QmListBoxScrollbarMetric(ScrollParams.m_ScrollbarThickness, m_ScrollbarWidth, m_ScrollbarWidthOverridden);
	m_ScrollbarMargin = QmListBoxScrollbarMetric(ScrollParams.m_ScrollbarMargin, m_ScrollbarMargin, m_ScrollbarMarginOverridden);
	ScrollParams.m_ScrollbarThickness = m_ScrollbarWidth;
	ScrollParams.m_ScrollbarMargin = m_ScrollbarMargin;
	// 轨道沿长度方向按容器圆角内缩：滑块端头不会贴到（甚至探出）容器圆角。
	ScrollParams.m_ScrollbarTrackInset = std::max(0.0f, m_ContainerRadius);
	const int NumRows = (m_ListBoxNumItems + maximum(1, m_ListBoxItemsPerRow) - 1) / maximum(1, m_ListBoxItemsPerRow);
	const float ContentHeight = ui_token::list::RowsHeight(NumRows, m_ListBoxRowHeight, m_AutoSpacing);
	// 胶囊行：覆盖式滚动条不占行宽，但行右侧恒定让出滚动条那一列，行胶囊右缘
	// 不贴轨道、滚动条出现/消失也不横跳。这里不能按「内容是否溢出」判断：
	// 服务器列表用 SkipItems 做可见范围裁剪，本帧根本拿不到真实内容高度。
	if(m_CapsuleRows && m_ListBoxItemsPerRow <= 1)
		m_RowReserveRight = RowReserveWidth();
	m_ScrollRegion.SetContentHeightForNextFrame(ContentHeight);
	m_ScrollRegion.Begin(&m_ListBoxView, &ScrollOffset, &ScrollParams);
	m_ListBoxView.y += ScrollOffset.y;
	// 多列胶囊网格：轨道压在内容上，整块列表体右侧让出同一列（不是每格各收，
	// 否则格子宽窄不一）；只在实际溢出、轨道可见时收，没有滚动条时不多缩一列。
	if(m_CapsuleRows && m_ListBoxItemsPerRow > 1 && ContentHeight > maximum(0.0f, m_ListBoxView.h) + 0.5f)
		m_ListBoxView.w = maximum(0.0f, m_ListBoxView.w - RowReserveWidth());

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
	// 胶囊行沿左右各收一段：容器圆角比行胶囊圆角大出来的那几像素，若贴着容器边，
	// 顶行/底行的胶囊直角边会从容器圆角外侧探出来。两档都按尺寸推导，所以标准行
	// （行高 25 对容器 12~14）几乎不用让位，矮行才多让一点；多列网格没有这一层。
	if(m_CapsuleRows && m_ListBoxItemsPerRow <= 1 && m_BackgroundDrawn)
	{
		const float TrackInset = maximum(0.0f, m_ContainerRadius - ui_token::list::RowRadius(m_ListBoxRowHeight));
		Item.m_Rect.x += TrackInset;
		Item.m_Rect.w = maximum(0.0f, Item.m_Rect.w - 2.0f * TrackInset);
	}
	// 覆盖式滚动条压在内容上：行胶囊与行文字都要给滚动条那一列让位，否则选中/悬浮的
	// 胶囊会被滚动条压住一角（用户实机反馈的现象）。
	Item.m_Rect.w = maximum(0.0f, Item.m_Rect.w - m_RowReserveRight);

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
	// 行内容区：把 token 派生的左右内边距让出来，文字/图标排在这里；行背景与命中区域
	// 仍是 m_Rect。内退对**所有行**成立（不再只在胶囊行生效），这样非胶囊列表的弹层
	// 条目也能拿到同一条内退；只用 m_Rect 排版的旧调用点不受影响。
	Item.m_ContentRect = Item.m_Rect;
	if(m_RowInsetX > 0.0f)
	{
		Item.m_ContentRect.x += m_RowInsetX;
		Item.m_ContentRect.w = maximum(0.0f, Item.m_ContentRect.w - 2.0f * m_RowInsetX);
	}
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
	// 反馈矩形贴合卡片本身。选中态与下拉弹层条目逐位一致：只有主题选中色填充、
	// 不加描边（白色描边只属于卡片内的小控件，列表行不用）。
	// 旧选中行即使已经不再被选中，也要继续画到淡出结束，才能和新行走成
	// 真正的交叉淡变，而不是先消失再淡入。
	const float RowCornerRadius = CornerRadius >= 0.0f ? CornerRadius : ResolveRowCornerRadius();
	const float SelectionAlpha = m_SelectedFeedback.Alpha(ThisItemIndex);
	if(SelectionAlpha > 0.001f)
	{
		CUIRect SelectionRect = Item.m_Rect;
		if(m_ItemSelectionInset > 0.0f)
			SelectionRect.Margin(m_ItemSelectionInset, &SelectionRect);
		const ColorRGBA SelectionFill = Ui()->ScaleBackgroundAlpha(m_Active ? m_SelectedItemActiveColor : m_SelectedItemInactiveColor).WithMultipliedAlpha(SelectionAlpha);
		DrawRoundedSurface(Ui(), SelectionRect, SelectionFill, ColorRGBA(), RowCornerRadius);
	}
	if(Ui()->HotItem() == pId)
		m_HoverFeedback.Observe(ThisItemIndex);
	const float HoverAlpha = m_HoverFeedback.Alpha(ThisItemIndex);
	if(HoverAlpha > 0.001f && !m_ScrollRegion.Animating())
	{
		CUIRect HoverRect = Item.m_Rect;
		if(m_ItemSelectionInset > 0.0f)
			HoverRect.Margin(m_ItemSelectionInset, &HoverRect);
		DrawRoundedSurface(Ui(), HoverRect, Ui()->ScaleBackgroundAlpha(m_HoveredItemColor).WithMultipliedAlpha(HoverAlpha), ColorRGBA(), RowCornerRadius);
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
	// 自定义行也要有内容矩形：以前留全 0，胶囊列表里插一行自定义内容就会拿到 0 宽，
	// 只能退回用整行矩形（于是又贴边）。这里与普通行走同一套内退规则。
	Item.m_ContentRect = Item.m_Rect;
	if(m_CapsuleRows && m_RowInsetX > 0.0f)
	{
		Item.m_ContentRect.x += m_RowInsetX;
		Item.m_ContentRect.w = maximum(0.0f, Item.m_ContentRect.w - 2.0f * m_RowInsetX);
	}
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
