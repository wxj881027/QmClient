// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "UiDogfood.h"

#include "UiButtons.h"
#include "UiContainers.h"
#include "UiForms.h"
#include "UiNavigation.h"
#include "UiOverlays.h"
#include "UiTokens.h"

#include <engine/graphics.h>
#include <engine/textrender.h>

#include <game/client/lineinput.h>
#include <game/client/qm_icon.h>
#include <game/client/ui.h>
#include <game/client/ui_rect.h>
#include <game/localization.h>

#include <array>

namespace
{
	constexpr int COLUMN_COUNT = 2;

	std::array<CButtonContainer, COLUMN_COUNT> s_aPrimaryBtn;
	std::array<CButtonContainer, COLUMN_COUNT> s_aSecondaryBtn;
	std::array<CButtonContainer, COLUMN_COUNT> s_aDisabledBtn;
	std::array<CButtonContainer, COLUMN_COUNT> s_aIconBtn;
	std::array<CLineInputBuffered<128>, COLUMN_COUNT> s_aTextField;
	std::array<CLineInputBuffered<128>, COLUMN_COUNT> s_aClearableTextField;
	std::array<CLineInputBuffered<128>, COLUMN_COUNT> s_aSearchField;
	std::array<bool, COLUMN_COUNT> s_aToggleOn = {true, true};
	std::array<bool, COLUMN_COUNT> s_aToggleOff = {false, false};
	std::array<float, COLUMN_COUNT> s_aSliderValue = {0.4f, 0.4f};
	std::array<int, COLUMN_COUNT> s_aTabActive = {0, 0};
	std::array<int, COLUMN_COUNT> s_aListSelected = {1, 1};
	bool s_ModalOpen = false;
	std::array<CButtonContainer, COLUMN_COUNT> s_aModalOpenBtn;
	std::array<CButtonContainer, COLUMN_COUNT> s_aTooltipBtn;
	std::array<CButtonContainer, COLUMN_COUNT> s_aToastBtn;
	bool s_ToastVisible = true;
	ui_widget::SToastState s_ToastState;
	CQmScrollState s_DogfoodScrollState;
	CQmScrollContainer s_DogfoodScroll;
} // namespace

void RenderQmUiDogfood(const IUiContext &Ctx, const CUIRect &Rect)
{
	if(Ctx.m_pUi == nullptr)
		return;

	// Header strip
	CUIRect Header, Body;
	Rect.HSplitTop(36.0f, &Header, &Body);
	Ctx.m_pUi->DoLabel(&Header, Localize("feat-003 dogfood: tokens + 12 widgets"), ui_token::font::HEADLINE_LG, TEXTALIGN_ML);

	const char *apTabLabels[] = {Localize("Overview"), Localize("Forms"), Localize("Data")};
	const char *pItemLabel = Localize("Item");
	const float ContentHeight = 520.0f;
	ui_widget::SScrollContainerProps ScrollProps;
	ScrollProps.m_Style = QmScrollContainerStyleForSize(EQmScrollSize::SMALL, 1.0f);

	ui_widget::ScrollContainer(Ctx, s_DogfoodScrollState, s_DogfoodScroll, Body, ContentHeight, ScrollProps, [&](CUIRect BodyContent) {
		Body = BodyContent;

		// Two columns: 1x and 0.78x to verify UiScale-style downscale
		CUIRect ColLeft, ColRight;
		Body.VSplitMid(&ColLeft, &ColRight);
		ColLeft.VSplitRight(ui_token::spacing::SM, &ColLeft, nullptr);
		ColRight.VSplitLeft(ui_token::spacing::SM, nullptr, &ColRight);

		auto RenderColumn = [&](const CUIRect &Col, float Scale, int Column) {
			// Card with all the controls inside
			ui_widget::SCardProps Card;
			Card.m_pTitle = Scale > 0.9f ? Localize("Native scale (1.0)") : Localize("Downscaled (0.78)");
			Card.m_TitleFontSize = ui_token::font::HEADLINE * Scale;
			Card.m_Padding = ui_token::spacing::MD * Scale;

			ui_widget::DrawCard(Ctx, Col, Card, [&](CUIRect &Content) {
				const float RowH = 28.0f * Scale;
				const float Gap = ui_token::spacing::SM * Scale;
				CUIRect Row;

				// Row 1: Primary + Secondary buttons
				Content.HSplitTop(RowH, &Row, &Content);
				{
					CUIRect L, R;
					Row.VSplitMid(&L, &R);
					L.VSplitRight(Gap * 0.5f, &L, nullptr);
					R.VSplitLeft(Gap * 0.5f, nullptr, &R);
					ui_widget::PrimaryButton(Ctx, &s_aPrimaryBtn[Column], Localize("Primary"), L);
					ui_widget::SecondaryButton(Ctx, &s_aSecondaryBtn[Column], Localize("Secondary"), R);
				}
				Content.HSplitTop(Gap, nullptr, &Content);

				// Row 2: Disabled + Icon button
				Content.HSplitTop(RowH, &Row, &Content);
				{
					CUIRect L, R;
					Row.VSplitMid(&L, &R);
					L.VSplitRight(Gap * 0.5f, &L, nullptr);
					R.VSplitLeft(Gap * 0.5f, nullptr, &R);
					ui_widget::PrimaryButton(Ctx, &s_aDisabledBtn[Column], Localize("Disabled"), L, true);
					ui_widget::IconButton(Ctx, &s_aIconBtn[Column], EQmIcon::STAR, FontIcons::FONT_ICON_STAR, R);
				}
				Content.HSplitTop(Gap, nullptr, &Content);

				// Row 3: TextField
				Content.HSplitTop(RowH, &Row, &Content);
				ui_widget::SInputFieldOptions TextOptions;
				TextOptions.m_pPlaceholder = Localize("Type something...");
				TextOptions.m_FontSize = ui_token::font::BODY * Scale;
				ui_widget::InputField(Ctx, &s_aTextField[Column], Row, TextOptions);
				Content.HSplitTop(Gap, nullptr, &Content);

				// Row 4: 带清除按钮的输入框
				Content.HSplitTop(RowH, &Row, &Content);
				ui_widget::SInputFieldOptions ClearableOptions;
				ClearableOptions.m_pPlaceholder = Localize("Clearable text...");
				ClearableOptions.m_Clearable = true;
				ClearableOptions.m_FontSize = ui_token::font::BODY * Scale;
				ui_widget::InputField(Ctx, &s_aClearableTextField[Column], Row, ClearableOptions);
				Content.HSplitTop(Gap, nullptr, &Content);

				// Row 5: 搜索输入框
				Content.HSplitTop(RowH, &Row, &Content);
				ui_widget::SInputFieldOptions SearchOptions;
				SearchOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
				SearchOptions.m_Clearable = true;
				SearchOptions.m_SearchHotkeyEnabled = true;
				SearchOptions.m_FontSize = ui_token::font::BODY * Scale;
				ui_widget::InputField(Ctx, &s_aSearchField[Column], Row, SearchOptions);
				Content.HSplitTop(Gap, nullptr, &Content);

				// Row 6: Two toggles
				Content.HSplitTop(RowH, &Row, &Content);
				{
					CUIRect L, R;
					Row.VSplitMid(&L, &R);
					L.VSplitRight(Gap * 0.5f, &L, nullptr);
					R.VSplitLeft(Gap * 0.5f, nullptr, &R);
					ui_widget::Toggle(Ctx, &s_aToggleOn[Column], &s_aToggleOn[Column], L);
					ui_widget::Toggle(Ctx, &s_aToggleOff[Column], &s_aToggleOff[Column], R);
				}
				Content.HSplitTop(Gap, nullptr, &Content);

				// Row 7: Slider
				Content.HSplitTop(RowH, &Row, &Content);
				ui_widget::Slider(Ctx, &s_aSliderValue[Column], &s_aSliderValue[Column], 0.0f, 1.0f, Row, "");
				Content.HSplitTop(Gap, nullptr, &Content);

				// Row 8: TabBar
				Content.HSplitTop(RowH, &Row, &Content);
				ui_widget::TabBar(Ctx, apTabLabels, std::size(apTabLabels), &s_aTabActive[Column], Row);
				Content.HSplitTop(Gap, nullptr, &Content);

				// Rows 9-11: List items
				for(int i = 0; i < 3; ++i)
				{
					Content.HSplitTop(RowH, &Row, &Content);
					char aLabel[32];
					str_format(aLabel, sizeof(aLabel), "%s %d", pItemLabel, i + 1);
					ui_widget::SListItemProps ItemProps;
					ItemProps.m_Selected = (s_aListSelected[Column] == i);
					ItemProps.m_pTrailingText = (i == 1) ? Localize("new") : nullptr;
					if(ui_widget::ListItem(Ctx, (const void *)(uintptr_t)(0x100 + Column * 0x100 + i), aLabel, Row, ItemProps))
						s_aListSelected[Column] = i;
					Content.HSplitTop(2.0f, nullptr, &Content);
				}
				Content.HSplitTop(Gap, nullptr, &Content);

				// Row 10: Modal trigger + Tooltip target
				Content.HSplitTop(RowH, &Row, &Content);
				{
					CUIRect L, R;
					Row.VSplitMid(&L, &R);
					L.VSplitRight(Gap * 0.5f, &L, nullptr);
					R.VSplitLeft(Gap * 0.5f, nullptr, &R);
					if(ui_widget::PrimaryButton(Ctx, &s_aModalOpenBtn[Column], Localize("Open modal"), L))
						s_ModalOpen = true;
					ui_widget::SecondaryButton(Ctx, &s_aTooltipBtn[Column], Localize("Hover for tooltip"), R);
					ui_widget::Tooltip(Ctx, &s_aTooltipBtn[Column], R, Localize("This tooltip is provided by ui_widget::Tooltip — a thin shim over CTooltips."));
				}
				Content.HSplitTop(Gap, nullptr, &Content);

				// Row 11: Toast trigger
				Content.HSplitTop(RowH, &Row, &Content);
				if(ui_widget::SecondaryButton(Ctx, &s_aToastBtn[Column], s_ToastVisible ? Localize("Hide toast") : Localize("Show toast"), Row))
					s_ToastVisible = !s_ToastVisible;
			});
		};

		RenderColumn(ColLeft, 1.0f, 0);
		RenderColumn(ColRight, 0.78f, 1);
	});

	// Modal — rendered on top of everything
	ui_widget::SModalProps ModalProps;
	ModalProps.m_pTitle = Localize("Hello from ui_widget::Modal");
	ModalProps.m_Width = 360.0f;
	ModalProps.m_Height = 180.0f;
	ui_widget::Modal(Ctx, &s_ModalOpen, &s_ModalOpen, Rect, ModalProps, [&](CUIRect &Content) {
		Ctx.m_pUi->DoLabel(&Content, Localize("Press ESC to close. Watch the scale-in spring on open."), ui_token::font::BODY, TEXTALIGN_TL);
	});

	ui_widget::SToastProps ToastProps;
	ToastProps.m_pText = Localize("Toast uses UiTokens + QmMotion slide");
	ui_widget::Toast(Ctx, &s_ToastState, &s_ToastState, s_ToastVisible, Rect, ToastProps);
}
