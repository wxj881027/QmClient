/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmUi/QmDropdown.h"
#include "QmUi/QmPopupPointer.h"
#include "QmUi/QmUiPerf.h"
#include "QmUi/UiSurface.h"
#include "components/qmclient/perf_logging.h"
#include "ui.h"
#include "ui_scrollregion.h"

#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/localization.h>

#include <algorithm>
#include <cmath>

const CUIRect *CUi::GetPopupMenuRect(const SPopupMenuId *pId) const
{
	const auto PopupMenuIt = std::find_if(m_vPopupMenus.begin(), m_vPopupMenus.end(), [pId](const SPopupMenu &PopupMenu) { return PopupMenu.m_pId == pId; });
	return PopupMenuIt == m_vPopupMenus.end() ? nullptr : &PopupMenuIt->m_Rect;
}

void CUi::DoPopupMenu(const SPopupMenuId *pId, float X, float Y, float Width, float Height, void *pContext, FPopupMenuFunction pfnFunc, const SPopupMenuProperties &Props)
{
	if(RenderOnly())
		return;
	if(Props.m_CenterInViewport)
	{
		// 二级界面弹窗：在当前视口内居中，忽略调用点传入的锚定坐标；
		// 之后的 AutoReposition 仍兜底弹窗超出视口的情形。
		X = Screen()->x + (Screen()->w - Width) / 2.0f;
		Y = Screen()->y + (Screen()->h - Height) / 2.0f;
	}
	if(Props.m_AutoReposition)
	{
		constexpr float Margin = SPopupMenu::POPUP_BORDER + SPopupMenu::POPUP_MARGIN;
		if(X + Width > Screen()->w - Margin)
			X = maximum<float>(X - Width, Margin);
		if(Y + Height > Screen()->h - Margin)
			Y = maximum<float>(Y - Height, Margin);
	}

	auto ExistingPopupMenu = std::find_if(m_vPopupMenus.begin(), m_vPopupMenus.end(), [pId](const SPopupMenu &PopupMenu) { return PopupMenu.m_pId == pId; });
	if(ExistingPopupMenu != m_vPopupMenus.end())
	{
		ExistingPopupMenu->m_Props = Props;
		ExistingPopupMenu->m_Rect.x = X;
		ExistingPopupMenu->m_Rect.y = Y;
		ExistingPopupMenu->m_Rect.w = Width;
		ExistingPopupMenu->m_Rect.h = Height;
		ExistingPopupMenu->m_pContext = pContext;
		ExistingPopupMenu->m_pfnFunc = pfnFunc;
		// 出场动画期间被重新打开：取消关闭状态并重放入场动画。
		if(ExistingPopupMenu->m_Closing)
		{
			ExistingPopupMenu->m_Closing = false;
			ExistingPopupMenu->m_OpenStart = Props.m_Animate ? Client()->LocalTime() : -1.0f;
		}
		return;
	}

	m_vPopupMenus.emplace_back();
	SPopupMenu *pNewMenu = &m_vPopupMenus.back();
	pNewMenu->m_pId = pId;
	pNewMenu->m_Props = Props;
	pNewMenu->m_Rect.x = X;
	pNewMenu->m_Rect.y = Y;
	pNewMenu->m_Rect.w = Width;
	pNewMenu->m_Rect.h = Height;
	pNewMenu->m_pContext = pContext;
	pNewMenu->m_pfnFunc = pfnFunc;
	pNewMenu->m_OpenStart = Props.m_Animate ? Client()->LocalTime() : -1.0f;
	if(Props.m_BlockUnderlyingPointerInput)
	{
		if(CLineInput *pActiveInput = CLineInput::GetActiveInput())
			pActiveInput->Deactivate();
		m_pLastActiveItem = nullptr;
		SetActiveItem(nullptr);
		m_ActiveButtonLogicButton = -1;
		SetHotItem(pId);
	}
}

void CUi::RenderPopupMenus()
{
	m_RenderingPopupMenus = true;
	const float Now = Client()->LocalTime();
	for(size_t i = 0; i < m_vPopupMenus.size(); ++i)
	{
		const SPopupMenu &PopupMenu = m_vPopupMenus[i];
		const SPopupMenuId *pId = PopupMenu.m_pId;

		// 出场动画中：逻辑已关闭，不参与任何输入判定与回调，只画收缩渐隐，
		// 动画结束后由本循环移除（不触发关闭回调，逻辑关闭时已触发过）。
		if(PopupMenu.m_Closing)
		{
			const float Progress = std::clamp((Now - PopupMenu.m_CloseStart) / SPopupMenu::POPUP_CLOSE_DURATION, 0.0f, 1.0f);
			CUIRect ShrinkRect = PopupMenu.m_Rect;
			const float Scale = 1.0f - 0.08f * Progress;
			ShrinkRect.x += ShrinkRect.w * (1.0f - Scale) * 0.5f;
			ShrinkRect.y += ShrinkRect.h * (1.0f - Scale) * 0.5f;
			ShrinkRect.w *= Scale;
			ShrinkRect.h *= Scale;
			const float Alpha = 1.0f - Progress;
			DrawRoundedSurface(this, ShrinkRect,
				PopupMenu.m_Props.m_BackgroundColor.WithAlpha(PopupMenu.m_Props.m_BackgroundColor.a * Alpha),
				PopupMenu.m_Props.m_BorderColor.WithAlpha(PopupMenu.m_Props.m_BorderColor.a * Alpha),
				ui_token::radius::BASE, SPopupMenu::POPUP_BORDER, PopupMenu.m_Props.m_Corners);
			if(Progress >= 1.0f)
			{
				m_vPopupMenus.erase(m_vPopupMenus.begin() + i);
				--i;
			}
			continue;
		}

		// 来源弹窗通常在同一渲染循环中刷新下拉层，但嵌套弹窗的父层回调
		// 可能在下一次 PerfFrame 才运行。允许一个帧差，避免下拉刚打开就
		// 被来源新鲜度检查收掉；CUi::Update 的兜底清扫仍会关闭真正失联的弹窗。
		if(PopupMenu.m_Props.m_RequireSourceRefresh && !QmDropdownSourceAlive(Client()->PerfFrame(), PopupMenu.m_Props.m_SourceFrame, true))
		{
			ClosePopupMenu(pId);
			--i;
			continue;
		}
		const bool Inside = MouseInside(&PopupMenu.m_Rect) && (!PopupMenu.m_Props.m_ClipToViewport || MouseInside(&PopupMenu.m_Props.m_Viewport));
		// 活跃弹窗 = 栈顶未处于出场动画的弹窗（closing 弹窗不接管输入，
		// 下层的活跃弹窗/页面交互在短动画期间照常）。
		bool Active = true;
		for(size_t j = i + 1; j < m_vPopupMenus.size(); ++j)
		{
			if(!m_vPopupMenus[j].m_Closing)
			{
				Active = false;
				break;
			}
		}
		const bool ClipToViewport = PopupMenu.m_Props.m_ClipToViewport;
		const bool AllowPopupPointerInput = Active && PopupMenu.m_Props.m_BlockUnderlyingPointerInput;
		CQmPopupInputScope PopupInputScope(m_PopupInputDepth, AllowPopupPointerInput);

		// 非阻断弹窗沿用上游机制：栈顶弹窗每帧接管热项，既阻止弹窗打开期间
		// 底层 UI 被悬停/激活，也保证「弹窗外按下 → 成为活动项 → 弹窗外
		// 松开即关闭」的链路可用（编辑器菜单栏等非阻断弹窗依赖它）。
		if(Active && !PopupMenu.m_Props.m_BlockUnderlyingPointerInput)
			SetHotItem(pId);

		// 先保存关闭意图，绘制完成后释放输入深度，再运行关闭回调。
		// 作用域兜底保证提前退出也不会泄漏深度，防止底层页面抢占弹窗拖拽。
		const SQmPopupPointerInput PointerInput{Active, PopupMenu.m_Props.m_BlockUnderlyingPointerInput, Inside, MouseButtonClicked(0), MouseButton(0), CheckActiveItem(pId), HotItem() == pId};
		const EQmPopupPointerAction PointerAction = QmResolvePopupPointerAction(PointerInput);
		const bool CloseBeforeRender = PointerAction == EQmPopupPointerAction::CLOSE;
		if(PointerAction == EQmPopupPointerAction::CAPTURE)
			SetActiveItem(pId);
		else if(PointerAction == EQmPopupPointerAction::RELEASE)
			SetActiveItem(nullptr);

		EPopupMenuFunctionResult Result = POPUP_KEEP_OPEN;
		if(!CloseBeforeRender)
		{
			if(Inside && PopupMenu.m_Props.m_BlockUnderlyingScroll)
			{
				// Prevent scroll regions directly behind popup menus from using the mouse scroll events.
				SetHotScrollRegion(nullptr);
			}
			if(ClipToViewport)
				ClipEnable(&PopupMenu.m_Props.m_Viewport);

			CUIRect PopupRect = PopupMenu.m_Rect;
			const SPopupMenuProperties &PopupProps = PopupMenu.m_Props;
			// 入场缩放动画：矩形从 92% 平滑放大到 100%（内容随矩形重排），
			// 背景/边框同步渐显；非动画弹窗乘子恒 1，路径零变化。
			float AnimAlphaMul = 1.0f;
			if(PopupProps.m_Animate && PopupMenu.m_OpenStart >= 0.0f)
			{
				const float Progress = std::clamp((Now - PopupMenu.m_OpenStart) / SPopupMenu::POPUP_OPEN_DURATION, 0.0f, 1.0f);
				const float Eased = 1.0f - (1.0f - Progress) * (1.0f - Progress);
				const float Scale = 0.92f + 0.08f * Eased;
				PopupRect.x += PopupRect.w * (1.0f - Scale) * 0.5f;
				PopupRect.y += PopupRect.h * (1.0f - Scale) * 0.5f;
				PopupRect.w *= Scale;
				PopupRect.h *= Scale;
				AnimAlphaMul = Eased;
			}
			// 对齐判定留浮点余量：几何端 AlignToAnchor 已保证严格相等，
			// 余量只吸收布局趟/渲染趟的舍入误差。
			const bool HasAlignedAnchor = PopupProps.m_HasAnchorSurface &&
						      PopupProps.m_AnchorSurface.w > 0.0f && PopupProps.m_AnchorSurface.h > 0.0f &&
						      std::fabs(PopupProps.m_AnchorSurface.x - PopupRect.x) < 1.5f &&
						      std::fabs(PopupProps.m_AnchorSurface.w - PopupRect.w) < 1.5f;
			if(HasAlignedAnchor)
			{
				// 边框把触发按钮与列表包成一个整体：外框只描边，填充由按钮
				//（页面阶段已绘制）与列表各自负责，接缝一侧用直角贴合。
				const CUIRect &Anchor = PopupProps.m_AnchorSurface;
				const bool Below = PopupRect.y > Anchor.y;
				CUIRect UnionRect;
				if(Below)
					UnionRect = CUIRect{Anchor.x, Anchor.y, Anchor.w, PopupRect.y + PopupRect.h - Anchor.y};
				else
					UnionRect = CUIRect{Anchor.x, PopupRect.y, Anchor.w, Anchor.y + Anchor.h - PopupRect.y};
				DrawRoundedSurface(this, UnionRect, ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), PopupProps.m_BorderColor, ui_token::radius::BASE, SPopupMenu::POPUP_BORDER, IGraphics::CORNER_ALL);
				CUIRect FillRect = PopupRect;
				FillRect.x += SPopupMenu::POPUP_BORDER;
				FillRect.w -= SPopupMenu::POPUP_BORDER * 2.0f;
				if(Below)
					FillRect.h -= SPopupMenu::POPUP_BORDER;
				else
				{
					FillRect.y += SPopupMenu::POPUP_BORDER;
					FillRect.h -= SPopupMenu::POPUP_BORDER;
				}
				DrawRoundedSurface(this, FillRect, PopupProps.m_BackgroundColor, ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), ui_token::radius::BASE, 0.0f, Below ? IGraphics::CORNER_B : IGraphics::CORNER_T);
			}
			else
			{
				DrawRoundedSurface(this, PopupRect,
					PopupProps.m_BackgroundColor.WithAlpha(PopupProps.m_BackgroundColor.a * AnimAlphaMul),
					PopupProps.m_BorderColor.WithAlpha(PopupProps.m_BorderColor.a * AnimAlphaMul),
					ui_token::radius::BASE, SPopupMenu::POPUP_BORDER, PopupProps.m_Corners);
			}
			PopupRect.Margin(SPopupMenu::POPUP_BORDER, &PopupRect);
			PopupRect.Margin(SPopupMenu::POPUP_MARGIN, &PopupRect);

			// The popup render function can open/close popups, which may resize the vector and thus
			// invalidate the variable PopupMenu. We therefore store pId in a separate variable.
			Result = PopupMenu.m_pfnFunc(PopupMenu.m_pContext, PopupRect, Active);
			if(ClipToViewport)
				ClipDisable();
		}
		PopupInputScope.Release();
		if(CloseBeforeRender)
		{
			ClosePopupMenu(pId);
			--i;
		}
		else if(Result != POPUP_KEEP_OPEN || (Active && ConsumeHotkey(HOTKEY_ESCAPE)))
		{
			ClosePopupMenu(pId, Result == POPUP_CLOSE_CURRENT_AND_DESCENDANTS);
		}
	}
	m_RenderingPopupMenus = false;
}

void CUi::ClosePopupMenu(const SPopupMenuId *pId, bool IncludeDescendants)
{
	auto PopupMenuToClose = std::find_if(m_vPopupMenus.begin(), m_vPopupMenus.end(), [pId](const SPopupMenu &PopupMenu) { return PopupMenu.m_pId == pId; });
	if(PopupMenuToClose != m_vPopupMenus.end())
	{
		// 出场动画已在播放：等待渲染循环自然移除，避免重置动画起点或重复回调。
		if(!IncludeDescendants && PopupMenuToClose->m_Closing)
			return;
		// 带出场动画的弹窗（仅二级界面大弹窗启用）：后代立即移除，自身标记
		// closing 播放收缩渐隐后由渲染循环移除；逻辑关闭（输入解除、关闭回调）
		// 在标记瞬间完成，与立即关闭语义一致。
		if(!IncludeDescendants && PopupMenuToClose->m_Props.m_Animate)
		{
			m_vPopupMenus.erase(PopupMenuToClose + 1, m_vPopupMenus.end());
			PopupMenuToClose->m_Closing = true;
			PopupMenuToClose->m_CloseStart = Client()->LocalTime();
			SetActiveItem(nullptr);
			if(m_pfnPopupMenuClosedCallback)
				m_pfnPopupMenuClosedCallback();
			return;
		}
		if(IncludeDescendants)
			m_vPopupMenus.erase(PopupMenuToClose, m_vPopupMenus.end());
		else
		{
			// 关闭弹窗时，栈存其上方的子弹窗必须一并结束：子弹窗的来源渲染
			// 多半就是被关弹窗，遗留成孤儿阻断弹窗会永久锁死底层指针输入。
			// 带出场动画的子弹窗转为收缩渐隐，由渲染循环自然移除。
			const float Now = Client()->LocalTime();
			for(auto It = PopupMenuToClose + 1; It != m_vPopupMenus.end(); ++It)
			{
				if(It->m_Closing || !It->m_Props.m_Animate)
					continue;
				It->m_Closing = true;
				It->m_CloseStart = Now;
			}
			m_vPopupMenus.erase(
				std::remove_if(PopupMenuToClose, m_vPopupMenus.end(), [](const SPopupMenu &PopupMenu) { return !PopupMenu.m_Closing; }),
				m_vPopupMenus.end());
		}
		SetActiveItem(nullptr);
		if(m_pfnPopupMenuClosedCallback)
			m_pfnPopupMenuClosedCallback();
	}
}

void CUi::ClosePopupMenus()
{
	if(m_vPopupMenus.empty())
		return;

	m_vPopupMenus.clear();
	SetActiveItem(nullptr);
	if(m_pfnPopupMenuClosedCallback)
		m_pfnPopupMenuClosedCallback();
}

bool CUi::IsPopupOpen() const
{
	return !m_vPopupMenus.empty();
}

bool CUi::IsPopupOpen(const SPopupMenuId *pId) const
{
	return std::any_of(m_vPopupMenus.begin(), m_vPopupMenus.end(), [pId](const SPopupMenu PopupMenu) { return PopupMenu.m_pId == pId; });
}

bool CUi::IsPopupHovered() const
{
	return std::any_of(m_vPopupMenus.begin(), m_vPopupMenus.end(), [this](const SPopupMenu PopupMenu) { return !PopupMenu.m_Closing && MouseHovered(&PopupMenu.m_Rect); });
}

void CUi::SetPopupMenuClosedCallback(FPopupMenuClosedCallback pfnCallback)
{
	m_pfnPopupMenuClosedCallback = std::move(pfnCallback);
}

void CUi::SMessagePopupContext::DefaultColor(ITextRender *pTextRender)
{
	m_TextColor = pTextRender->DefaultTextColor();
}

void CUi::SMessagePopupContext::ErrorColor()
{
	m_TextColor = ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f);
}

CUi::EPopupMenuFunctionResult CUi::PopupMessage(void *pContext, CUIRect View, bool Active)
{
	SMessagePopupContext *pMessagePopup = static_cast<SMessagePopupContext *>(pContext);
	CUi *pUI = pMessagePopup->m_pUI;

	pUI->TextRender()->TextColor(pMessagePopup->m_TextColor);
	pUI->TextRender()->Text(View.x, View.y, SMessagePopupContext::POPUP_FONT_SIZE, pMessagePopup->m_aMessage, View.w);
	pUI->TextRender()->TextColor(pUI->TextRender()->DefaultTextColor());

	return (Active && pUI->ConsumeHotkey(HOTKEY_ENTER)) ? CUi::POPUP_CLOSE_CURRENT : CUi::POPUP_KEEP_OPEN;
}

void CUi::ShowPopupMessage(float X, float Y, SMessagePopupContext *pContext)
{
	const float TextWidth = minimum(std::ceil(TextRender()->TextWidth(SMessagePopupContext::POPUP_FONT_SIZE, pContext->m_aMessage, -1, -1.0f) + 0.5f), SMessagePopupContext::POPUP_MAX_WIDTH);
	float TextHeight = 0.0f;
	STextSizeProperties TextSizeProps{};
	TextSizeProps.m_pHeight = &TextHeight;
	TextRender()->TextWidth(SMessagePopupContext::POPUP_FONT_SIZE, pContext->m_aMessage, -1, TextWidth, 0, TextSizeProps);
	pContext->m_pUI = this;
	DoPopupMenu(pContext, X, Y, TextWidth + 10.0f, TextHeight + 10.0f, pContext, PopupMessage);
}

CUi::SConfirmPopupContext::SConfirmPopupContext()
{
	Reset();
}

void CUi::SConfirmPopupContext::Reset()
{
	m_Result = SConfirmPopupContext::UNSET;
}

void CUi::SConfirmPopupContext::YesNoButtons()
{
	str_copy(m_aPositiveButtonLabel, Localize("Yes"));
	str_copy(m_aNegativeButtonLabel, Localize("No"));
}

void CUi::ShowPopupConfirm(float X, float Y, SConfirmPopupContext *pContext)
{
	const float TextWidth = minimum(std::ceil(TextRender()->TextWidth(SConfirmPopupContext::POPUP_FONT_SIZE, pContext->m_aMessage, -1, -1.0f) + 0.5f), SConfirmPopupContext::POPUP_MAX_WIDTH);
	float TextHeight = 0.0f;
	STextSizeProperties TextSizeProps{};
	TextSizeProps.m_pHeight = &TextHeight;
	TextRender()->TextWidth(SConfirmPopupContext::POPUP_FONT_SIZE, pContext->m_aMessage, -1, TextWidth, 0, TextSizeProps);
	const float PopupHeight = TextHeight + SConfirmPopupContext::POPUP_BUTTON_HEIGHT + SConfirmPopupContext::POPUP_BUTTON_SPACING + 10.0f;
	pContext->m_pUI = this;
	pContext->m_Result = SConfirmPopupContext::UNSET;
	DoPopupMenu(pContext, X, Y, TextWidth + 10.0f, PopupHeight, pContext, PopupConfirm);
}

CUi::EPopupMenuFunctionResult CUi::PopupConfirm(void *pContext, CUIRect View, bool Active)
{
	SConfirmPopupContext *pConfirmPopup = static_cast<SConfirmPopupContext *>(pContext);
	CUi *pUI = pConfirmPopup->m_pUI;

	CUIRect Label, ButtonBar, CancelButton, ConfirmButton;
	View.HSplitBottom(SConfirmPopupContext::POPUP_BUTTON_HEIGHT, &Label, &ButtonBar);
	ButtonBar.VSplitMid(&CancelButton, &ConfirmButton, SConfirmPopupContext::POPUP_BUTTON_SPACING);

	pUI->TextRender()->Text(Label.x, Label.y, SConfirmPopupContext::POPUP_FONT_SIZE, pConfirmPopup->m_aMessage, Label.w);

	if(pUI->DoButton_PopupMenu(&pConfirmPopup->m_CancelButton, pConfirmPopup->m_aNegativeButtonLabel, &CancelButton, SConfirmPopupContext::POPUP_FONT_SIZE, TEXTALIGN_MC))
	{
		pConfirmPopup->m_Result = SConfirmPopupContext::CANCELED;
		return CUi::POPUP_CLOSE_CURRENT;
	}

	if(pUI->DoButton_PopupMenu(&pConfirmPopup->m_ConfirmButton, pConfirmPopup->m_aPositiveButtonLabel, &ConfirmButton, SConfirmPopupContext::POPUP_FONT_SIZE, TEXTALIGN_MC) || (Active && pUI->ConsumeHotkey(HOTKEY_ENTER)))
	{
		pConfirmPopup->m_Result = SConfirmPopupContext::CONFIRMED;
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::SSelectionPopupContext::SSelectionPopupContext()
{
	Reset();
}

void CUi::SSelectionPopupContext::Reset()
{
	m_pUI = nullptr;
	m_pScrollRegion = nullptr;
	m_Props = SPopupMenuProperties();
	m_aMessage[0] = '\0';
	m_pSelection = nullptr;
	m_SelectionIndex = -1;
	m_ActiveIndex = -1;
	m_vEntries.clear();
	m_vButtonContainers.clear();
	m_EntryHeight = 12.0f;
	m_EntryPadding = 0.0f;
	m_EntrySpacing = 5.0f;
	m_FontSize = 10.0f;
	m_MinimumFontSize = -1.0f;
	m_Width = 300.0f + (SPopupMenu::POPUP_BORDER + SPopupMenu::POPUP_MARGIN) * 2;
	m_AlignmentHeight = -1.0f;
	m_ActiveEntryColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.22f);
	m_TransparentButtons = false;
	m_AnchorVisible = true;
	m_PopupVisible = true;
	m_BlockUnderlyingScroll = false;
	m_Scrollable = false;
	m_ScrollToActiveItem = false;
	m_MenuUiFirstWheelLogged = false;
	m_Viewport = {};
	m_PopupPolicy = {};
	m_SpecialFontRenderMode = false;
	m_FontFaceAvailabilityCheck = false;
}

CUi::EPopupMenuFunctionResult CUi::PopupSelection(void *pContext, CUIRect View, bool Active)
{
	const bool MenuUiPerfEnabled = QmPerfEnabled();
	const auto MenuUiStartTime = MenuUiPerfEnabled ? time_get_nanoseconds() : std::chrono::nanoseconds::zero();
	SSelectionPopupContext *pSelectionPopup = static_cast<SSelectionPopupContext *>(pContext);
	CUi *pUI = pSelectionPopup->m_pUI;
	CScrollRegion *pScrollRegion = pSelectionPopup->m_pScrollRegion;
	if(pScrollRegion == nullptr)
	{
		log_error("ui", "Selection popup opened without a scroll region");
		return CUi::POPUP_CLOSE_CURRENT;
	}

	// 键盘输入属于栈顶子层，父菜单失活时不能停止导航或让父层抢走按键。
	SQmDropdownInput KeyboardInput;
	if(Active)
	{
		KeyboardInput.m_KeyUp = pUI->ConsumeHotkey(HOTKEY_UP);
		KeyboardInput.m_KeyDown = pUI->ConsumeHotkey(HOTKEY_DOWN);
		KeyboardInput.m_KeyEnter = pUI->ConsumeHotkey(HOTKEY_ENTER);
		KeyboardInput.m_KeyEscape = pUI->ConsumeHotkey(HOTKEY_ESCAPE);
	}
	const int PreviousActiveIndex = pSelectionPopup->m_ActiveIndex;
	const SQmDropdownUpdateResult KeyboardResult = QmUpdateDropdownPopupSelection(KeyboardInput, pSelectionPopup->m_vEntries.size(), Active, pSelectionPopup->m_ActiveIndex);
	if(KeyboardResult.m_Selected)
	{
		pSelectionPopup->m_SelectionIndex = KeyboardResult.m_SelectedIndex;
		pSelectionPopup->m_pSelection = &pSelectionPopup->m_vEntries[KeyboardResult.m_SelectedIndex];
	}
	if(KeyboardResult.m_Closed)
		return CUi::POPUP_CLOSE_CURRENT;
	if(QmDropdownShouldRequestActiveScroll(true, PreviousActiveIndex, pSelectionPopup->m_ActiveIndex))
		pSelectionPopup->m_ScrollToActiveItem = true;

	vec2 ScrollOffset(0.0f, 0.0f);
	SQmScrollRequest ScrollRequest;
	ScrollRequest.m_Profile = EQmScrollProfile::POPUP_LIST;
	ScrollRequest.m_RowExtent = pSelectionPopup->m_EntryHeight + pSelectionPopup->m_EntrySpacing;
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest);
	CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	ScrollParams.m_HideScrollbar = !pSelectionPopup->m_Scrollable;
	ScrollParams.m_ScrollbarNoOuterMargin = true;
	ScrollParams.m_pWheelOwnerId = pSelectionPopup;
	ScrollParams.m_WheelOwnerPreRegistered = true;
	const float PopupOuterHeight = (SPopupMenu::POPUP_BORDER + SPopupMenu::POPUP_MARGIN) * 2.0f;
	pScrollRegion->SetContentHeightForNextFrame(std::max(0.0f, pSelectionPopup->m_PopupPolicy.m_ContentHeight - PopupOuterHeight));
	pScrollRegion->Begin(&View, &ScrollOffset, &ScrollParams);
	View.y += ScrollOffset.y;

	CUIRect Slot;
	if(pSelectionPopup->m_aMessage[0] != '\0')
	{
		const STextBoundingBox TextBoundingBox = pUI->TextRender()->TextBoundingBox(pSelectionPopup->m_FontSize, pSelectionPopup->m_aMessage, -1, pSelectionPopup->m_Width);
		View.HSplitTop(TextBoundingBox.m_H, &Slot, &View);
		if(pScrollRegion->AddRect(Slot))
		{
			pUI->TextRender()->Text(Slot.x, Slot.y, pSelectionPopup->m_FontSize, pSelectionPopup->m_aMessage, Slot.w);
		}
	}

	pSelectionPopup->m_vButtonContainers.resize(pSelectionPopup->m_vEntries.size());

	size_t Index = 0;
	int VisibleEntries = 0;
	for(const auto &Entry : pSelectionPopup->m_vEntries)
	{
		if(pSelectionPopup->m_aMessage[0] != '\0' || Index != 0)
			View.HSplitTop(pSelectionPopup->m_EntrySpacing, nullptr, &View);
		View.HSplitTop(pSelectionPopup->m_EntryHeight, &Slot, &View);
		const bool ActiveEntry = pSelectionPopup->m_ActiveIndex == static_cast<int>(Index);
		if(pScrollRegion->AddRect(Slot, QmDropdownActiveItemShouldScrollIntoView(pSelectionPopup->m_ScrollToActiveItem, ActiveEntry)))
		{
			++VisibleEntries;
			// TClient: 字体预览只对实际渲染的条目切换字体面。此前对所有条目
			// （含滚动区外的）每帧切换，条目多时每帧反复解析字面并触发字重
			// 应用，是字体下拉框打开即卡死的直接原因。
			// QmClient: m_FontFaceAvailabilityCheck 供商店搜索弹层使用——条目是
			// 未安装的在线字体，只有预览面已加载的条目才切换，避免逐帧失败日志。
			if(pSelectionPopup->m_SpecialFontRenderMode && (!pSelectionPopup->m_FontFaceAvailabilityCheck || pUI->TextRender()->QmHasCustomFace(Entry.c_str())))
				pUI->TextRender()->SetCustomFace(Entry.c_str());
			// 活动项与悬浮项使用同一种整行背景，避免左侧竖条与条目背景重叠。
			const std::optional<ColorRGBA> ActiveColor = ActiveEntry ? std::optional<ColorRGBA>(pSelectionPopup->m_ActiveEntryColor) : std::nullopt;
			if(pUI->DoButton_PopupMenu(&pSelectionPopup->m_vButtonContainers[Index], Entry.c_str(), &Slot, pSelectionPopup->m_FontSize, TEXTALIGN_ML, pSelectionPopup->m_EntryPadding, pSelectionPopup->m_TransparentButtons, true, ActiveColor))
			{
				pSelectionPopup->m_pSelection = &Entry;
				pSelectionPopup->m_SelectionIndex = Index;
			}
		}
		++Index;
	}
	// TClient
	if(pSelectionPopup->m_SpecialFontRenderMode)
		pUI->TextRender()->SetCustomFace(g_Config.m_TcCustomFont);

	pScrollRegion->End();
	pSelectionPopup->m_ScrollToActiveItem = false;
	if(!pSelectionPopup->m_MenuUiFirstWheelLogged && pScrollRegion->WheelConsumedThisFrame())
	{
		pUI->m_MenuUiFirstWheelPerf = MenuUiPerfEnabled;
		SQmMenuUiFramePerf MenuUiPerf;
		MenuUiPerf.m_pPage = "dropdown";
		MenuUiPerf.m_pOperation = "dropdown_first_wheel";
		MenuUiPerf.m_ItemsTotal = (int)pSelectionPopup->m_vEntries.size();
		MenuUiPerf.m_ItemsVisible = VisibleEntries;
		MenuUiPerf.m_ItemsProcessed = VisibleEntries;
		MenuUiPerf.m_ItemsSkipped = maximum(0, MenuUiPerf.m_ItemsTotal - VisibleEntries);
		MenuUiPerf.m_UiMs = MenuUiPerfEnabled ? std::chrono::duration<double, std::milli>(time_get_nanoseconds() - MenuUiStartTime).count() : -1.0;
		QmLogMenuUiFramePerf(MenuUiPerf, pUI->Client());
		pSelectionPopup->m_MenuUiFirstWheelLogged = true;
	}

	return pSelectionPopup->m_pSelection == nullptr ? CUi::POPUP_KEEP_OPEN : CUi::POPUP_CLOSE_CURRENT;
}

void CUi::ShowPopupSelection(float X, float Y, SSelectionPopupContext *pContext)
{
	const bool HasMessage = pContext->m_aMessage[0] != '\0';
	const STextBoundingBox TextBoundingBox = TextRender()->TextBoundingBox(pContext->m_FontSize, pContext->m_aMessage, -1, pContext->m_Width);
	const float OuterHeight = (SPopupMenu::POPUP_BORDER + SPopupMenu::POPUP_MARGIN) * 2;
	pContext->m_PopupPolicy = QmResolveDropdownPopupPolicy(pContext->m_vEntries.size(), pContext->m_EntryHeight, pContext->m_EntrySpacing, HasMessage, TextBoundingBox.m_H, OuterHeight);
	const float PopupHeight = pContext->m_PopupPolicy.m_PreferredHeight;
	if(pContext->m_Viewport.w <= 0.0f || pContext->m_Viewport.h <= 0.0f)
		pContext->m_Viewport = *Screen();
	const CUIRect &Viewport = pContext->m_Viewport;
	pContext->m_pUI = this;
	pContext->m_pSelection = nullptr;
	pContext->m_SelectionIndex = -1;
	pContext->m_Props.m_Corners = IGraphics::CORNER_ALL;
	// 对齐模式下记录锚点（触发按钮），让弹窗外框把按钮与列表包成一个整体。
	pContext->m_Props.m_HasAnchorSurface = pContext->m_AlignmentHeight >= 0.0f;
	pContext->m_Props.m_AnchorSurface = CUIRect{X, Y, pContext->m_Width, pContext->m_Props.m_HasAnchorSurface ? pContext->m_AlignmentHeight : 0.0f};
	// 阻断底层指针输入：点击弹窗（含触发按钮）以外区域时立即关闭弹窗。
	// 几何判定不可见的路径会在下方直接 ClosePopupMenu，阻断标志无副作用。
	pContext->m_Props.m_BlockUnderlyingPointerInput = true;
	float PopupWidth = pContext->m_Width;
	float PopupHeightResolved = PopupHeight;
	if(pContext->m_AlignmentHeight >= 0.0f)
	{
		constexpr float Margin = SPopupMenu::POPUP_BORDER + SPopupMenu::POPUP_MARGIN;
		CUIRect AnchorRect;
		AnchorRect.x = X;
		AnchorRect.y = Y;
		AnchorRect.w = pContext->m_Width;
		AnchorRect.h = pContext->m_AlignmentHeight;
		SQmDropdownGeometryConfig GeometryConfig;
		GeometryConfig.m_Width = pContext->m_Width;
		GeometryConfig.m_Height = PopupHeight;
		GeometryConfig.m_Margin = Margin;
		GeometryConfig.m_RowHeight = pContext->m_EntryHeight;
		GeometryConfig.m_RowSpacing = pContext->m_EntrySpacing;
		GeometryConfig.m_FixedHeight = QmDropdownFixedHeight(HasMessage, TextBoundingBox.m_H, OuterHeight);
		GeometryConfig.m_LeadingRowSpacing = HasMessage ? pContext->m_EntrySpacing : 0.0f;
		// 外框包裹模式：弹层与锚点按钮严格对齐（宽度/左缘），渲染端的
		// HasAlignedAnchor 判定才能稳定成立，边框连成一个整体。
		GeometryConfig.m_AlignToAnchor = true;
		const SQmDropdownGeometryResult Geometry = QmComputeDropdownPopupGeometry(AnchorRect, Viewport, GeometryConfig);
		pContext->m_AnchorVisible = Geometry.m_AnchorVisible;
		pContext->m_PopupVisible = Geometry.m_PopupVisible;
		if(!pContext->m_AnchorVisible || !pContext->m_PopupVisible)
		{
			ClosePopupMenu(pContext);
			return;
		}
		X = Geometry.m_Rect.x;
		Y = Geometry.m_Rect.y;
		PopupWidth = Geometry.m_Rect.w;
		PopupHeightResolved = Geometry.m_Rect.h;
		pContext->m_Props.m_AutoReposition = false;
		pContext->m_Props.m_Corners = Geometry.m_PlacedBelow ? IGraphics::CORNER_B : IGraphics::CORNER_T;
	}
	const CUIRect PopupRect{X, Y, PopupWidth, PopupHeightResolved};
	const bool Scrollable = pContext->m_PopupVisible && QmDropdownPopupScrollable(pContext->m_PopupPolicy, PopupHeightResolved);
	const bool BlockUnderlying = QmDropdownPopupBlocksUnderlying(pContext->m_PopupVisible);
	RegisterWheelOwner(pContext, EUiWheelOwnerPriority::POPUP, PopupRect, BlockUnderlying);
	pContext->m_Scrollable = Scrollable;
	pContext->m_BlockUnderlyingScroll = BlockUnderlying;
	pContext->m_Props.m_ClipToViewport = true;
	pContext->m_Props.m_BlockUnderlyingScroll = BlockUnderlying;
	pContext->m_Props.m_Viewport = Viewport;
	DoPopupMenu(pContext, X, Y, PopupWidth, PopupHeightResolved, pContext, PopupSelection, pContext->m_Props);
}

int CUi::DoDropDown(CUIRect *pRect, int CurSelection, const char *const *pStrs, int Num, SDropDownState &State, const SDropDownProperties &DropDownProps)
{
	// 防御：选中索引必须落在 [-1, Num) 才能安全用作 pStrs 下标。索引来源包括
	// g_Config 与跨帧 UI 状态，一旦被越界写破坏（2026-09-30 崩溃报告：按钮文字
	// lambda 捕获块在两次调用之间被栈上野写覆盖成 0x2D6D6C67），直接下标访问
	// 会野读崩溃。这里收敛到合法区间：越界正值按最后一项处理，非法负值按未选中处理。
	if(Num <= 0)
	{
		CurSelection = -1;
	}
	else if(CurSelection >= Num)
	{
		CurSelection = Num - 1;
	}
	else if(CurSelection < -1)
	{
		CurSelection = -1;
	}
	const float ResolvedFontSize = DropDownProps.m_FontSize > 0.0f ? DropDownProps.m_FontSize : m_DropDownFontSize > 0.0f ? m_DropDownFontSize :
																pRect->h * ms_FontmodHeight * 0.8f;
	if(RenderOnly())
	{
		if(pRect != nullptr && pStrs != nullptr && CurSelection >= 0 && CurSelection < Num)
			DoLabel(pRect, pStrs[CurSelection], ResolvedFontSize, TEXTALIGN_MC);
		return CurSelection;
	}

	if(!State.m_Init)
	{
		State.m_UiElement.Init(this, -1);
		State.m_pOwnedScrollRegion = std::make_shared<CScrollRegion>();
		State.m_pScrollRegion = State.m_SelectionPopupContext.m_pScrollRegion != nullptr ? State.m_SelectionPopupContext.m_pScrollRegion : State.m_pOwnedScrollRegion.get();
		State.m_SelectionPopupContext.m_pScrollRegion = State.m_pScrollRegion;
		State.m_Init = true;
	}
	else if(State.m_SelectionPopupContext.m_pScrollRegion != nullptr && State.m_SelectionPopupContext.m_pScrollRegion != State.m_pScrollRegion)
		State.m_pScrollRegion = State.m_SelectionPopupContext.m_pScrollRegion;

	bool PopupOpen = IsPopupOpen(&State.m_SelectionPopupContext);
	// 弹窗使用设置页最外层裁剪区，不能越过 Tab 或页面容器；卡片内容裁剪区
	// 只判断锚点是否仍完整可见，锚点滚出卡片后应关闭弹窗。
	const CUIRect Viewport = DropDownProps.m_pPopupViewport != nullptr ? *DropDownProps.m_pPopupViewport : IsClipped() ? *OutermostClipArea() :
															     *Screen();
	const CUIRect AnchorViewport = DropDownProps.m_pAnchorViewport != nullptr ? *DropDownProps.m_pAnchorViewport : IsClipped() ? *ClipArea() :
																     Viewport;
	const uint64_t SourceFrame = Client()->PerfFrame();
	if(PopupOpen && !QmDropdownAnchorFullyVisible(*pRect, AnchorViewport))
	{
		ClosePopupMenu(&State.m_SelectionPopupContext);
		State.m_DropDownState.Reset();
		State.m_SelectionPopupContext.Reset();
		PopupOpen = false;
	}
	if(State.m_DropDownState.IsOpen() && !PopupOpen)
		State.m_DropDownState.Reset();

	const auto LabelFunc = [CurSelection, pStrs]() {
		return CurSelection > -1 ? pStrs[CurSelection] : "";
	};
	if(!DropDownProps.m_Enabled)
	{
		if(DropDownProps.m_ClosePopupWhenDisabled)
		{
			if(State.m_DropDownState.Disable(PopupOpen))
				ClosePopupMenu(&State.m_SelectionPopupContext);
			State.m_SelectionPopupContext.m_SelectionIndex = -1;
			State.m_SelectionPopupContext.m_ActiveIndex = -1;
		}
		else if(PopupOpen)
		{
			// 上层弹窗（如聊天翻译菜单）在本下拉的选择弹层打开期间会失去
			// Active，令本下拉切入禁用分支；选择弹层的来源帧保活刷新只存在
			// 于启用分支，若此处不补上，RenderPopupMenus 会在下一帧按
			// 「来源失效」关闭刚打开的弹层，表现为下拉点开即一闪而过。
			State.m_SelectionPopupContext.m_Props.m_RequireSourceRefresh = DropDownProps.m_RequireSourceRefresh;
			State.m_SelectionPopupContext.m_Props.m_SourceFrame = SourceFrame;
		}
		SMenuButtonProperties ButtonProps;
		ButtonProps.m_Enabled = false;
		ButtonProps.m_HintRequiresStringCheck = true;
		ButtonProps.m_HintCanChangePositionOrSize = true;
		ButtonProps.m_ShowDropDownIcon = true;
		ButtonProps.m_FontSize = ResolvedFontSize;
		ButtonProps.m_Color = DropDownProps.m_VisualStyle.m_TriggerColor;
		DoButton_Menu(State.m_UiElement, &State.m_ButtonContainer, LabelFunc, pRect, ButtonProps);
		return CurSelection;
	}

	SMenuButtonProperties Props;
	Props.m_HintRequiresStringCheck = true;
	Props.m_HintCanChangePositionOrSize = true;
	Props.m_ShowDropDownIcon = true;
	Props.m_FontSize = ResolvedFontSize;
	Props.m_Color = DropDownProps.m_VisualStyle.m_TriggerColor;
	if(PopupOpen)
	{
		State.m_SelectionPopupContext.m_Props.m_RequireSourceRefresh = DropDownProps.m_RequireSourceRefresh;
		State.m_SelectionPopupContext.m_Props.m_SourceFrame = SourceFrame;
		Props.m_Corners = IGraphics::CORNER_ALL & (~State.m_SelectionPopupContext.m_Props.m_Corners);
	}
	const bool TogglePressed = DoButton_Menu(State.m_UiElement, &State.m_ButtonContainer, LabelFunc, pRect, Props);

	SQmDropdownInput DropDownInput;
	DropDownInput.m_TogglePressed = TogglePressed;
	DropDownInput.m_InitialIndex = CurSelection;
	// 子弹层在自身回调中更新选择，父层只同步活动条目并处理触发按钮。
	if(PopupOpen)
		DropDownInput.m_HoveredIndex = State.m_SelectionPopupContext.m_ActiveIndex;
	const int PreviousActiveIndex = State.m_DropDownState.ActiveIndex();
	const SQmDropdownUpdateResult DropDownResult = State.m_DropDownState.Update(DropDownInput, Num);
	State.m_SelectionPopupContext.m_ActiveIndex = State.m_DropDownState.ActiveIndex();
	if(QmDropdownShouldRequestActiveScroll(PopupOpen, PreviousActiveIndex, State.m_DropDownState.ActiveIndex()))
		State.m_SelectionPopupContext.m_ScrollToActiveItem = true;
	if(PopupOpen)
	{
		State.m_SelectionPopupContext.m_FontSize = ResolvedFontSize;
		State.m_SelectionPopupContext.m_EntryHeight = pRect->h;
		State.m_SelectionPopupContext.m_EntryPadding = pRect->h >= 20.0f ? 2.0f : 1.0f;
		State.m_SelectionPopupContext.m_Width = pRect->w;
		State.m_SelectionPopupContext.m_AlignmentHeight = pRect->h;
		State.m_SelectionPopupContext.m_Viewport = Viewport;
		State.m_SelectionPopupContext.m_Props.m_BorderColor = DropDownProps.m_VisualStyle.m_PopupBorderColor;
		State.m_SelectionPopupContext.m_Props.m_BackgroundColor = DropDownProps.m_VisualStyle.m_PopupBackgroundColor;
		State.m_SelectionPopupContext.m_ActiveEntryColor = DropDownProps.m_VisualStyle.m_ActiveEntryColor;
		State.m_SelectionPopupContext.m_TransparentButtons = DropDownProps.m_VisualStyle.m_TransparentEntries;
		ShowPopupSelection(pRect->x, pRect->y, &State.m_SelectionPopupContext);
		PopupOpen = IsPopupOpen(&State.m_SelectionPopupContext);
		if(State.m_DropDownState.IsOpen() && !PopupOpen)
			State.m_DropDownState.Reset();
	}
	if(DropDownResult.m_Opened)
	{
		CScrollRegion *pScrollRegion = State.m_SelectionPopupContext.m_pScrollRegion;
		const bool SpecialFontRenderMode = State.m_SelectionPopupContext.m_SpecialFontRenderMode;
		const bool FontFaceAvailabilityCheck = State.m_SelectionPopupContext.m_FontFaceAvailabilityCheck;
		State.m_SelectionPopupContext.Reset();
		State.m_SelectionPopupContext.m_pScrollRegion = pScrollRegion != nullptr ? pScrollRegion : State.m_pScrollRegion;
		State.m_SelectionPopupContext.m_SpecialFontRenderMode = SpecialFontRenderMode;
		State.m_SelectionPopupContext.m_FontFaceAvailabilityCheck = FontFaceAvailabilityCheck;
		State.m_SelectionPopupContext.m_Props.m_BorderColor = DropDownProps.m_VisualStyle.m_PopupBorderColor;
		State.m_SelectionPopupContext.m_Props.m_BackgroundColor = DropDownProps.m_VisualStyle.m_PopupBackgroundColor;
		State.m_SelectionPopupContext.m_ActiveEntryColor = DropDownProps.m_VisualStyle.m_ActiveEntryColor;
		for(int i = 0; i < Num; ++i)
			State.m_SelectionPopupContext.m_vEntries.emplace_back(pStrs[i]);
		State.m_SelectionPopupContext.m_EntryHeight = pRect->h;
		State.m_SelectionPopupContext.m_EntryPadding = pRect->h >= 20.0f ? 2.0f : 1.0f;
		State.m_SelectionPopupContext.m_FontSize = ResolvedFontSize;
		State.m_SelectionPopupContext.m_Width = pRect->w;
		State.m_SelectionPopupContext.m_AlignmentHeight = pRect->h;
		State.m_SelectionPopupContext.m_TransparentButtons = DropDownProps.m_VisualStyle.m_TransparentEntries;
		State.m_SelectionPopupContext.m_ActiveIndex = State.m_DropDownState.ActiveIndex();
		State.m_SelectionPopupContext.m_Props.m_RequireSourceRefresh = DropDownProps.m_RequireSourceRefresh;
		State.m_SelectionPopupContext.m_Props.m_SourceFrame = SourceFrame;
		State.m_SelectionPopupContext.m_ScrollToActiveItem = true;
		State.m_SelectionPopupContext.m_Viewport = Viewport;
		ShowPopupSelection(pRect->x, pRect->y, &State.m_SelectionPopupContext);
	}
	if(DropDownResult.m_Selected)
	{
		ClosePopupMenu(&State.m_SelectionPopupContext);
		State.m_SelectionPopupContext.Reset();
		return DropDownResult.m_SelectedIndex;
	}
	else if(DropDownResult.m_Closed)
	{
		ClosePopupMenu(&State.m_SelectionPopupContext);
		State.m_SelectionPopupContext.Reset();
	}
	else if(State.m_SelectionPopupContext.m_SelectionIndex >= 0)
	{
		const int NewSelection = State.m_SelectionPopupContext.m_SelectionIndex;
		State.m_DropDownState.Reset();
		State.m_SelectionPopupContext.Reset();
		return NewSelection;
	}

	return CurSelection;
}

int CUi::DoDropDown(CUIRect *pRect, int CurSelection, const char *const *pStrs, int Num, SDropDownState &State, bool Enabled)
{
	SDropDownProperties DropDownProps;
	DropDownProps.m_Enabled = Enabled;
	DropDownProps.m_ClosePopupWhenDisabled = false;
	return DoDropDown(pRect, CurSelection, pStrs, Num, State, DropDownProps);
}

CUi::EPopupMenuFunctionResult CUi::PopupColorPicker(void *pContext, CUIRect View, bool Active)
{
	SColorPickerPopupContext *pColorPicker = static_cast<SColorPickerPopupContext *>(pContext);
	CUi *pUI = pColorPicker->m_pUI;
	pColorPicker->m_State = EEditState::NONE;

	// 弹窗背板（含高斯模糊）已在 RenderPopupMenus 中绘制完成，弹窗内容一律画在
	// 背板之上。若不抑制模糊，饱和度/明度渐变里透明→黑色的明度叠加层会因顶角
	// alpha < 1 触发 CUIRect::Draw4 的 DrawRectBackdrop，把已画好的色相底色覆盖
	// 成模糊背板，选择方块因此显示为灰度渐变、不随所选色相变化。
	CUiScopedGaussianBlurSuppression PickerBlurSuppression(pUI);

	CUIRect ColorsArea, HueArea, BottomArea, ModeButtonArea, HueRect, SatRect, ValueRect, HexRect, AlphaRect;

	View.HSplitTop(140.0f, &ColorsArea, &BottomArea);
	ColorsArea.VSplitRight(20.0f, &ColorsArea, &HueArea);
	const CUIRect ColorsHitArea = ColorsArea;

	BottomArea.HSplitTop(3.0f, nullptr, &BottomArea);
	HueArea.VSplitLeft(3.0f, nullptr, &HueArea);
	const CUIRect HueHitArea = HueArea;

	BottomArea.HSplitTop(20.0f, &HueRect, &BottomArea);
	BottomArea.HSplitTop(3.0f, nullptr, &BottomArea);

	constexpr float ValuePadding = 5.0f;
	const float HsvValueWidth = (HueRect.w - ValuePadding * 2) / 3.0f;
	const float HexValueWidth = HsvValueWidth * 2 + ValuePadding;

	HueRect.VSplitLeft(HsvValueWidth, &HueRect, &SatRect);
	SatRect.VSplitLeft(ValuePadding, nullptr, &SatRect);
	SatRect.VSplitLeft(HsvValueWidth, &SatRect, &ValueRect);
	ValueRect.VSplitLeft(ValuePadding, nullptr, &ValueRect);

	BottomArea.HSplitTop(20.0f, &HexRect, &BottomArea);
	BottomArea.HSplitTop(3.0f, nullptr, &BottomArea);
	HexRect.VSplitLeft(HexValueWidth, &HexRect, &AlphaRect);
	AlphaRect.VSplitLeft(ValuePadding, nullptr, &AlphaRect);
	BottomArea.HSplitTop(20.0f, &ModeButtonArea, &BottomArea);

	const ColorRGBA BlackColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f);

	HueArea.Draw(BlackColor, IGraphics::CORNER_NONE, 0.0f);
	HueArea.Margin(1.0f, &HueArea);

	ColorsArea.Draw(BlackColor, IGraphics::CORNER_NONE, 0.0f);
	ColorsArea.Margin(1.0f, &ColorsArea);

	ColorHSVA PickerColorHSV = pColorPicker->m_HsvaColor;
	ColorRGBA PickerColorRGB = pColorPicker->m_RgbaColor;
	ColorHSLA PickerColorHSL = pColorPicker->m_HslaColor;

	// Color Area
	ColorRGBA TL, TR, BL, BR;
	TL = BL = color_cast<ColorRGBA>(ColorHSVA(PickerColorHSV.x, 0.0f, 1.0f));
	TR = BR = color_cast<ColorRGBA>(ColorHSVA(PickerColorHSV.x, 1.0f, 1.0f));
	ColorsArea.Draw4(TL, TR, BL, BR, IGraphics::CORNER_NONE, 0.0f);

	TL = TR = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
	BL = BR = ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f);
	ColorsArea.Draw4(TL, TR, BL, BR, IGraphics::CORNER_NONE, 0.0f);

	// Hue Area
	static const float s_aaColorIndices[7][3] = {
		{1.0f, 0.0f, 0.0f}, // red
		{1.0f, 0.0f, 1.0f}, // magenta
		{0.0f, 0.0f, 1.0f}, // blue
		{0.0f, 1.0f, 1.0f}, // cyan
		{0.0f, 1.0f, 0.0f}, // green
		{1.0f, 1.0f, 0.0f}, // yellow
		{1.0f, 0.0f, 0.0f}, // red
	};

	const float HuePickerOffset = HueArea.h / 6.0f;
	CUIRect HuePartialArea = HueArea;
	HuePartialArea.h = HuePickerOffset;

	for(size_t j = 0; j < std::size(s_aaColorIndices) - 1; j++)
	{
		TL = ColorRGBA(s_aaColorIndices[j][0], s_aaColorIndices[j][1], s_aaColorIndices[j][2], 1.0f);
		BL = ColorRGBA(s_aaColorIndices[j + 1][0], s_aaColorIndices[j + 1][1], s_aaColorIndices[j + 1][2], 1.0f);

		HuePartialArea.y = HueArea.y + HuePickerOffset * j;
		HuePartialArea.Draw4(TL, TL, BL, BL, IGraphics::CORNER_NONE, 0.0f);
	}

	SValueSelectorProperties ColorValueProps;
	ColorValueProps.m_UseScroll = false;

	const auto &&RenderAlphaSelector = [&](unsigned OldA) -> SEditResult<int64_t> {
		if(pColorPicker->m_Alpha)
		{
			return pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[3], &AlphaRect, "A:", OldA, 0, 255, ColorValueProps);
		}
		else
		{
			char aBuf[8];
			str_format(aBuf, sizeof(aBuf), "A: %d", OldA);
			pUI->DoLabel(&AlphaRect, aBuf, 10.0f, TEXTALIGN_MC);
			AlphaRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.65f), IGraphics::CORNER_ALL, 3.0f);
			return {EEditState::NONE, OldA};
		}
	};

	// Editboxes Area
	if(pColorPicker->m_ColorMode == SColorPickerPopupContext::MODE_HSVA)
	{
		const unsigned OldH = round_to_int(PickerColorHSV.h * 255.0f);
		const unsigned OldS = round_to_int(PickerColorHSV.s * 255.0f);
		const unsigned OldV = round_to_int(PickerColorHSV.v * 255.0f);
		const unsigned OldA = round_to_int(PickerColorHSV.a * 255.0f);

		const auto [StateH, H] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[0], &HueRect, "H:", OldH, 0, 255, ColorValueProps);
		const auto [StateS, S] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[1], &SatRect, "S:", OldS, 0, 255, ColorValueProps);
		const auto [StateV, V] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[2], &ValueRect, "V:", OldV, 0, 255, ColorValueProps);
		const auto [StateA, A] = RenderAlphaSelector(OldA);

		if(OldH != H || OldS != S || OldV != V || OldA != A)
		{
			PickerColorHSV = ColorHSVA(H / 255.0f, S / 255.0f, V / 255.0f, A / 255.0f);
			PickerColorHSL = color_cast<ColorHSLA>(PickerColorHSV);
			PickerColorRGB = color_cast<ColorRGBA>(PickerColorHSL);
		}

		for(auto State : {StateH, StateS, StateV, StateA})
		{
			if(State != EEditState::NONE)
			{
				pColorPicker->m_State = State;
				break;
			}
		}
	}
	else if(pColorPicker->m_ColorMode == SColorPickerPopupContext::MODE_RGBA)
	{
		const unsigned OldR = round_to_int(PickerColorRGB.r * 255.0f);
		const unsigned OldG = round_to_int(PickerColorRGB.g * 255.0f);
		const unsigned OldB = round_to_int(PickerColorRGB.b * 255.0f);
		const unsigned OldA = round_to_int(PickerColorRGB.a * 255.0f);

		const auto [StateR, R] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[0], &HueRect, "R:", OldR, 0, 255, ColorValueProps);
		const auto [StateG, G] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[1], &SatRect, "G:", OldG, 0, 255, ColorValueProps);
		const auto [StateB, B] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[2], &ValueRect, "B:", OldB, 0, 255, ColorValueProps);
		const auto [StateA, A] = RenderAlphaSelector(OldA);

		if(OldR != R || OldG != G || OldB != B || OldA != A)
		{
			PickerColorRGB = ColorRGBA(R / 255.0f, G / 255.0f, B / 255.0f, A / 255.0f);
			PickerColorHSL = color_cast<ColorHSLA>(PickerColorRGB);
			PickerColorHSV = color_cast<ColorHSVA>(PickerColorHSL);
		}

		for(auto State : {StateR, StateG, StateB, StateA})
		{
			if(State != EEditState::NONE)
			{
				pColorPicker->m_State = State;
				break;
			}
		}
	}
	else if(pColorPicker->m_ColorMode == SColorPickerPopupContext::MODE_HSLA)
	{
		const unsigned OldH = round_to_int(PickerColorHSL.h * 255.0f);
		const unsigned OldS = round_to_int(PickerColorHSL.s * 255.0f);
		const unsigned OldL = round_to_int(PickerColorHSL.l * 255.0f);
		const unsigned OldA = round_to_int(PickerColorHSL.a * 255.0f);

		const auto [StateH, H] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[0], &HueRect, "H:", OldH, 0, 255, ColorValueProps);
		const auto [StateS, S] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[1], &SatRect, "S:", OldS, 0, 255, ColorValueProps);
		const auto [StateL, L] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[2], &ValueRect, "L:", OldL, 0, 255, ColorValueProps);
		const auto [StateA, A] = RenderAlphaSelector(OldA);

		if(OldH != H || OldS != S || OldL != L || OldA != A)
		{
			PickerColorHSL = ColorHSLA(H / 255.0f, S / 255.0f, L / 255.0f, A / 255.0f);
			PickerColorHSV = color_cast<ColorHSVA>(PickerColorHSL);
			PickerColorRGB = color_cast<ColorRGBA>(PickerColorHSL);
		}

		for(auto State : {StateH, StateS, StateL, StateA})
		{
			if(State != EEditState::NONE)
			{
				pColorPicker->m_State = State;
				break;
			}
		}
	}
	else
	{
		dbg_assert_failed("Color picker mode invalid: %d", (int)pColorPicker->m_ColorMode);
	}

	SValueSelectorProperties Props;
	Props.m_UseScroll = false;
	Props.m_IsHex = true;
	Props.m_HexPrefix = pColorPicker->m_Alpha ? 8 : 6;
	const unsigned OldHex = PickerColorRGB.PackAlphaLast(pColorPicker->m_Alpha);
	auto [HexState, Hex] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[4], &HexRect, "Hex:", OldHex, 0, pColorPicker->m_Alpha ? 0xFFFFFFFFll : 0xFFFFFFll, Props);
	if(OldHex != Hex)
	{
		const float OldAlpha = PickerColorRGB.a;
		PickerColorRGB = ColorRGBA::UnpackAlphaLast<ColorRGBA>(Hex, pColorPicker->m_Alpha);
		if(!pColorPicker->m_Alpha)
			PickerColorRGB.a = OldAlpha;
		PickerColorHSL = color_cast<ColorHSLA>(PickerColorRGB);
		PickerColorHSV = color_cast<ColorHSVA>(PickerColorHSL);
	}

	if(HexState != EEditState::NONE)
		pColorPicker->m_State = HexState;

	// Logic
	float PickerX, PickerY;
	EEditState ColorPickerRes = pUI->DoPickerLogic(&pColorPicker->m_ColorPickerId, &ColorsHitArea, &PickerX, &PickerY);
	if(ColorPickerRes != EEditState::NONE)
	{
		const float ColorX = std::clamp(PickerX - (ColorsArea.x - ColorsHitArea.x), 0.0f, ColorsArea.w);
		const float ColorY = std::clamp(PickerY - (ColorsArea.y - ColorsHitArea.y), 0.0f, ColorsArea.h);
		PickerColorHSV.y = ColorX / ColorsArea.w;
		PickerColorHSV.z = 1.0f - ColorY / ColorsArea.h;
		PickerColorHSL = color_cast<ColorHSLA>(PickerColorHSV);
		PickerColorRGB = color_cast<ColorRGBA>(PickerColorHSL);
		pColorPicker->m_State = ColorPickerRes;
	}

	EEditState HuePickerRes = pUI->DoPickerLogic(&pColorPicker->m_HuePickerId, &HueHitArea, &PickerX, &PickerY);
	if(HuePickerRes != EEditState::NONE)
	{
		const float HueY = std::clamp(PickerY - (HueArea.y - HueHitArea.y), 0.0f, HueArea.h);
		PickerColorHSV.x = 1.0f - HueY / HueArea.h;
		PickerColorHSL = color_cast<ColorHSLA>(PickerColorHSV);
		PickerColorRGB = color_cast<ColorRGBA>(PickerColorHSL);
		pColorPicker->m_State = HuePickerRes;
	}

	// Marker Color Area
	const float MarkerX = ColorsArea.x + ColorsArea.w * PickerColorHSV.y;
	const float MarkerY = ColorsArea.y + ColorsArea.h * (1.0f - PickerColorHSV.z);

	const float MarkerOutlineInd = PickerColorHSV.z > 0.5f ? 0.0f : 1.0f;
	const ColorRGBA MarkerOutline = ColorRGBA(MarkerOutlineInd, MarkerOutlineInd, MarkerOutlineInd, 1.0f);

	const CUIRect ColorMarker{MarkerX - 4.5f, MarkerY - 4.5f, 9.0f, 9.0f};
	DrawRoundedSurface(pUI, ColorMarker, PickerColorRGB, MarkerOutline, 4.5f, 1.0f);

	// Marker Hue Area
	CUIRect HueMarker;
	HueArea.Margin(-2.5f, &HueMarker);
	HueMarker.h = 6.5f;
	HueMarker.y = (HueArea.y + HueArea.h * (1.0f - PickerColorHSV.x)) - HueMarker.h / 2.0f;

	const ColorRGBA HueMarkerColor = color_cast<ColorRGBA>(ColorHSVA(PickerColorHSV.x, 1.0f, 1.0f, 1.0f));
	const float HueMarkerOutlineColor = PickerColorHSV.x > 0.75f ? 1.0f : 0.0f;
	const ColorRGBA HueMarkerOutline = ColorRGBA(HueMarkerOutlineColor, HueMarkerOutlineColor, HueMarkerOutlineColor, 1.0f);

	DrawRoundedSurface(pUI, HueMarker, HueMarkerColor, HueMarkerOutline, 1.2f, 1.2f);

	pColorPicker->m_HsvaColor = PickerColorHSV;
	pColorPicker->m_RgbaColor = PickerColorRGB;
	pColorPicker->m_HslaColor = PickerColorHSL;
	if(pColorPicker->m_pHslaColor != nullptr)
		*pColorPicker->m_pHslaColor = PickerColorHSL.Pack(pColorPicker->m_Alpha);

	static constexpr SColorPickerPopupContext::EColorPickerMode PICKER_MODES[] = {SColorPickerPopupContext::MODE_HSVA, SColorPickerPopupContext::MODE_RGBA, SColorPickerPopupContext::MODE_HSLA};
	static constexpr const char *PICKER_MODE_LABELS[] = {"HSVA", "RGBA", "HSLA"};
	static_assert(std::size(PICKER_MODES) == std::size(PICKER_MODE_LABELS));
	for(SColorPickerPopupContext::EColorPickerMode Mode : PICKER_MODES)
	{
		CUIRect ModeButton;
		ModeButtonArea.VSplitLeft(HsvValueWidth, &ModeButton, &ModeButtonArea);
		ModeButtonArea.VSplitLeft(ValuePadding, nullptr, &ModeButtonArea);
		if(pUI->DoButton_PopupMenu(&pColorPicker->m_aModeButtons[(int)Mode], PICKER_MODE_LABELS[Mode], &ModeButton, 10.0f, TEXTALIGN_MC, 2.0f, false, pColorPicker->m_ColorMode != Mode))
		{
			pColorPicker->m_ColorMode = Mode;
		}
	}

	return CUi::POPUP_KEEP_OPEN;
}

void CUi::ShowPopupColorPicker(float X, float Y, SColorPickerPopupContext *pContext)
{
	pContext->m_pUI = this;
	if(pContext->m_ColorMode == SColorPickerPopupContext::MODE_UNSET)
		pContext->m_ColorMode = SColorPickerPopupContext::MODE_HSVA;
	SPopupMenuProperties PopupProps;
	PopupProps.m_BlockUnderlyingPointerInput = true;
	PopupProps.m_BlockUnderlyingScroll = true;
	DoPopupMenu(pContext, X, Y, 160.0f + 10.0f, 209.0f + 10.0f, pContext, PopupColorPicker, PopupProps);
}
