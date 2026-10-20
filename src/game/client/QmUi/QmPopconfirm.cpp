#include "QmPopconfirm.h"

#include "UiSurface.h"
#include "UiTheme.h"
#include "UiTokens.h"

#include <base/math.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float POPCONFIRM_MAX_TEXT_WIDTH = 220.0f;
	constexpr float POPCONFIRM_FONT_SIZE = 10.0f;
	constexpr float POPCONFIRM_BUTTON_HEIGHT = 14.0f;
	constexpr float POPCONFIRM_PADDING = 8.0f;
	constexpr float POPCONFIRM_BUTTON_SPACING = 5.0f;
	constexpr float POPCONFIRM_LABEL_GAP = 4.0f;

	CUi::EPopupMenuFunctionResult PopconfirmDraw(void *pContext, CUIRect View, bool Active)
	{
		SQmPopconfirmContext *pPopconfirm = static_cast<SQmPopconfirmContext *>(pContext);
		CUi *pUI = pPopconfirm->m_pUI;

		CUIRect Label, ButtonBar, CancelButton, ConfirmButton;
		View.HSplitBottom(POPCONFIRM_BUTTON_HEIGHT, &Label, &ButtonBar);
		Label.h = std::max(0.0f, Label.h - POPCONFIRM_LABEL_GAP);
		ButtonBar.VSplitMid(&CancelButton, &ConfirmButton, POPCONFIRM_BUTTON_SPACING);

		pUI->TextRender()->Text(Label.x, Label.y, POPCONFIRM_FONT_SIZE, pPopconfirm->m_aMessage, Label.w);
		if(!Active)
			return CUi::POPUP_KEEP_OPEN;

		if(pUI->DoButton_PopupMenu(&pPopconfirm->m_CancelButton, pPopconfirm->m_aCancel, &CancelButton, POPCONFIRM_FONT_SIZE, TEXTALIGN_MC))
		{
			pPopconfirm->m_Canceled = true;
			return CUi::POPUP_CLOSE_CURRENT;
		}

		const std::optional<ColorRGBA> ConfirmColor =
			pPopconfirm->m_ToneDanger ? std::optional<ColorRGBA>(ui_token::color::DANGER) : std::nullopt;
		if(pUI->DoButton_PopupMenu(&pPopconfirm->m_ConfirmButton, pPopconfirm->m_aConfirm, &ConfirmButton, POPCONFIRM_FONT_SIZE, TEXTALIGN_MC, 0.0f, false, true, ConfirmColor))
		{
			pPopconfirm->m_Confirmed = true;
			return CUi::POPUP_CLOSE_CURRENT;
		}
		return CUi::POPUP_KEEP_OPEN;
	}
} // namespace

void SQmPopconfirmContext::Reset()
{
	m_Confirmed = false;
	m_Canceled = false;
}

void SQmPopconfirmContext::DefaultButtons()
{
	str_copy(m_aConfirm, Localize("Confirm"));
	str_copy(m_aCancel, Localize("Cancel"));
}

void QmShowPopconfirm(CUi *pUi, SQmPopconfirmContext *pContext, const CUIRect &Anchor)
{
	if(pUi == nullptr || pContext == nullptr)
		return;

	const float TextWidth = std::min(std::ceil(pUi->TextRender()->TextWidth(POPCONFIRM_FONT_SIZE, pContext->m_aMessage, -1, -1.0f) + 0.5f), POPCONFIRM_MAX_TEXT_WIDTH);
	float TextHeight = 0.0f;
	STextSizeProperties TextSizeProps{};
	TextSizeProps.m_pHeight = &TextHeight;
	pUi->TextRender()->TextWidth(POPCONFIRM_FONT_SIZE, pContext->m_aMessage, -1, TextWidth, 0, TextSizeProps);

	const float Width = TextWidth + 2.0f * POPCONFIRM_PADDING;
	const float Height = TextHeight + POPCONFIRM_LABEL_GAP + POPCONFIRM_BUTTON_HEIGHT + 2.0f * POPCONFIRM_PADDING;
	const CUIRect Rect = qm_popconfirm_layout::ResolveRect(Anchor, Width, Height, *pUi->Screen());

	pContext->m_pUI = pUi;

	SPopupMenuProperties Props;
	Props.m_CenterInViewport = false;
	// 与模态确认的分界点：不画遮罩、不锁页面滚动、不打断焦点链。
	Props.m_BlockUnderlyingPointerInput = false;
	Props.m_BlockUnderlyingScroll = false;
	Props.m_Animate = true;
	Props.m_AutoReposition = false;
	Props.m_IgnoreOpeningClick = true;
	const SUiTheme Theme = ResolveConfiguredDropdownListTheme();
	Props.m_BackgroundColor = Theme.m_Surface;
	Props.m_BorderColor = Theme.m_Border;
	// 关闭路径统一收口：没点确认就当作取消（Esc / 点外部 / 来源消失）。
	Props.m_OnClosed = [pContext]() {
		if(!pContext->m_Confirmed)
			pContext->m_Canceled = true;
	};

	pUi->DoPopupMenu(pContext, Rect.x, Rect.y, Rect.w, Rect.h, pContext, PopconfirmDraw, Props);
}
