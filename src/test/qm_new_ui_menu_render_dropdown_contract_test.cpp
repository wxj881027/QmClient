// QmNewUi 菜单源码合同：render 下拉与列表控件。运行时行为保留在 qm_new_ui_menu_branch_test.cpp.
#include <engine/client/backend/vulkan/backend_vulkan.h>
#include <engine/client/backend_sdl.h>
#include <engine/client/plausible_sizes.h>
#include <engine/client/rounded_rect_geometry.h>
#include <engine/storage.h>

#include <game/client/QmUi/UiSurface.h>
#include <game/client/components/camera.h>
#include <game/client/components/controls.h>
#include <game/client/components/menus.h>
#include <game/client/components/nameplate_text_effects.h>
#include <game/client/components/nameplates.h>
#include <game/client/components/qmclient/axiom_auto_login.h>
#include <game/client/components/tclient/statusbar.h>
#include <game/client/components/tooltips.h>
#include <game/client/prediction/gameworld.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>
#include <test/test.h>

#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>
#include <string>

TEST(QmNewUiMenuRenderDropdownContract, DropDownPopupFollowsScrolledControlRect)
{
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp") + ReadTextFile("src/game/client/ui_popups.cpp");
	const std::string UiHeader = ReadTextFile("src/game/client/ui.h");
	const std::string DoDropDown = FunctionBody(UiSource, "int CUi::DoDropDown(CUIRect *pRect, int CurSelection, const char *const *pStrs, int Num, SDropDownState &State, const SDropDownProperties &DropDownProps)");
	const std::string DoDropDownActive = FunctionBody(UiSource, "int CUi::DoDropDown(CUIRect *pRect, int CurSelection, const char *const *pStrs, int Num, SDropDownState &State, bool Enabled)");
	const std::string DoPopupMenu = FunctionBody(UiSource, "void CUi::DoPopupMenu(");

	ASSERT_FALSE(DoDropDown.empty());
	ASSERT_FALSE(DoDropDownActive.empty());
	ASSERT_FALSE(DoPopupMenu.empty());
	EXPECT_NE(DoDropDown.find("bool PopupOpen = IsPopupOpen(&State.m_SelectionPopupContext);"), std::string::npos);
	EXPECT_NE(DoDropDown.find("if(PopupOpen)"), std::string::npos);
	EXPECT_NE(DoDropDown.find("ShowPopupSelection(pRect->x, pRect->y, &State.m_SelectionPopupContext);"), std::string::npos);
	EXPECT_NE(DoDropDown.find("PopupOpen = IsPopupOpen(&State.m_SelectionPopupContext);"), std::string::npos);
	EXPECT_NE(DoDropDown.find("if(State.m_DropDownState.IsOpen() && !PopupOpen)"), std::string::npos);
	// Popup 以设置页最外层 viewport 定位，并在锚点离开所属容器时关闭。
	EXPECT_NE(DoDropDown.find("DropDownProps.m_pPopupViewport != nullptr"), std::string::npos);
	EXPECT_NE(DoDropDown.find("QmDropdownAnchorFullyVisible(*pRect, AnchorViewport)"), std::string::npos);
	EXPECT_NE(DoDropDown.find("SQmDropdownInput DropDownInput;"), std::string::npos);
	EXPECT_NE(DoDropDown.find("State.m_DropDownState.Update(DropDownInput, Num);"), std::string::npos);
	EXPECT_NE(DoDropDown.find("DropDownInput.m_KeyUp = ConsumeHotkey(HOTKEY_UP);"), std::string::npos);
	EXPECT_NE(DoDropDown.find("DropDownInput.m_KeyDown = ConsumeHotkey(HOTKEY_DOWN);"), std::string::npos);
	EXPECT_NE(DoDropDown.find("DropDownInput.m_KeyEnter = ConsumeHotkey(HOTKEY_ENTER);"), std::string::npos);
	EXPECT_NE(DoDropDown.find("DropDownInput.m_KeyEscape = ConsumeHotkey(HOTKEY_ESCAPE);"), std::string::npos);
	EXPECT_NE(DoDropDown.find("State.m_SelectionPopupContext.m_ActiveIndex = State.m_DropDownState.ActiveIndex();"), std::string::npos);
	const size_t SelectedBranch = DoDropDown.find("if(DropDownResult.m_Selected)");
	const size_t ClosedBranch = DoDropDown.find("else if(DropDownResult.m_Closed)");
	ASSERT_NE(SelectedBranch, std::string::npos);
	ASSERT_NE(ClosedBranch, std::string::npos);
	EXPECT_LT(SelectedBranch, ClosedBranch);
	EXPECT_NE(DoPopupMenu.find("std::find_if(m_vPopupMenus.begin(), m_vPopupMenus.end()"), std::string::npos);
	EXPECT_NE(DoPopupMenu.find("ExistingPopupMenu->m_Rect.x = X;"), std::string::npos);
	EXPECT_NE(DoPopupMenu.find("ExistingPopupMenu->m_Rect.y = Y;"), std::string::npos);
	const size_t DisabledBranch = DoDropDown.find("if(!DropDownProps.m_Enabled)");
	const size_t CloseWhenDisabled = DoDropDown.find("if(DropDownProps.m_ClosePopupWhenDisabled)", DisabledBranch);
	const size_t CloseDisabledPopup = DoDropDown.find("ClosePopupMenu(&State.m_SelectionPopupContext);", DisabledBranch);
	ASSERT_NE(DisabledBranch, std::string::npos);
	ASSERT_NE(CloseWhenDisabled, std::string::npos);
	ASSERT_NE(CloseDisabledPopup, std::string::npos);
	EXPECT_LT(CloseWhenDisabled, CloseDisabledPopup);
	EXPECT_LT(DisabledBranch, CloseDisabledPopup);
	EXPECT_NE(UiHeader.find("m_ClosePopupWhenDisabled(true),"), std::string::npos);
	EXPECT_NE(DoDropDownActive.find("DropDownProps.m_ClosePopupWhenDisabled = false;"), std::string::npos);
}

TEST(QmNewUiMenuRenderDropdownContract, SettingsDropdownsUseTheSharedWrapper)
{
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string ControlsSource = ReadTextFile("src/game/client/components/menus_settings_controls.cpp");
	const std::string Wrapper = FunctionBody(MenusSource, "int CMenus::DoSettingsDropDown(CUIRect *pRect, const int CurSelection, const char *const *ppStrs, const int Num, CUi::SDropDownState &State, CUi::SDropDownProperties Properties)");

	ASSERT_FALSE(Wrapper.empty());
	EXPECT_NE(Wrapper.find("Properties.m_pAnchorViewport"), std::string::npos);
	EXPECT_NE(Wrapper.find("Properties.m_pPopupViewport"), std::string::npos);
	EXPECT_EQ(ControlsSource.find("Ui()->DoDropDown(&JoystickDropDown"), std::string::npos);
	EXPECT_NE(ControlsSource.find("GameClient()->m_Menus.DoSettingsDropDown(&JoystickDropDown"), std::string::npos);

	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp") + ReadTextFile("src/game/client/ui_popups.cpp");
	const std::string DoDropDown = FunctionBody(UiSource, "int CUi::DoDropDown(CUIRect *pRect, int CurSelection, const char *const *pStrs, int Num, SDropDownState &State, const SDropDownProperties &DropDownProps)");
	ASSERT_FALSE(DoDropDown.empty());
	EXPECT_EQ(DoDropDown.find("static CScrollRegion"), std::string::npos);
	EXPECT_NE(DoDropDown.find("State.m_pOwnedScrollRegion = std::make_shared<CScrollRegion>();"), std::string::npos);
	EXPECT_NE(DoDropDown.find("State.m_pScrollRegion = State.m_SelectionPopupContext.m_pScrollRegion != nullptr"), std::string::npos);
	EXPECT_NE(DoDropDown.find("pScrollRegion != nullptr ? pScrollRegion : State.m_pScrollRegion"), std::string::npos);
}

TEST(QmNewUiMenuRenderDropdownContract, SettingsDropdownWrapperAndNestedListsKeepSharedVisualAndScrollContracts)
{
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string ListBoxHeader = ReadTextFile("src/game/client/ui_listbox.h");
	const std::string ListBoxSource = ReadTextFile("src/game/client/ui_listbox.cpp");
	const std::string Tee = FunctionBody(SettingsSource, "void CMenus::RenderSettingsTee(CUIRect MainView)");
	const std::string Wrapper = FunctionBody(MenusSource, "int CMenus::DoSettingsDropDown(CUIRect *pRect, const int CurSelection, const char *const *ppStrs, const int Num, CUi::SDropDownState &State, CUi::SDropDownProperties Properties)");

	ASSERT_FALSE(Tee.empty());
	ASSERT_FALSE(Wrapper.empty());
	EXPECT_NE(Wrapper.find("Properties.m_VisualStyle = QmSettingsDropdownVisualStyle(m_SettingsUiTheme);"), std::string::npos);
	EXPECT_NE(ListBoxHeader.find("void SetScrollbarAlwaysReserved(bool AlwaysReserved)"), std::string::npos);
	EXPECT_NE(ListBoxSource.find("ScrollParams.m_ScrollbarAlwaysReserved = m_ScrollbarAlwaysReserved;"), std::string::npos);
	EXPECT_NE(Tee.find("s_QueueListBox.SetScrollbarAlwaysReserved(true);"), std::string::npos);
	EXPECT_NE(Tee.find("s_PresetListBox.SetScrollbarAlwaysReserved(true);"), std::string::npos);
}

TEST(QmNewUiMenuRenderDropdownContract, DropDownKeyboardActiveIndexIsRendered)
{
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp") + ReadTextFile("src/game/client/ui_popups.cpp");
	const std::string UiHeader = ReadTextFile("src/game/client/ui.h");
	const std::string SelectionReset = FunctionBody(UiSource, "void CUi::SSelectionPopupContext::Reset()");
	const std::string PopupSelection = FunctionBody(UiSource, "CUi::EPopupMenuFunctionResult CUi::PopupSelection(void *pContext, CUIRect View, bool Active)");
	const std::string PopupButton = FunctionBody(UiSource, "int CUi::DoButton_PopupMenu(CButtonContainer *pButtonContainer");
	const std::string DoDropDown = FunctionBody(UiSource, "int CUi::DoDropDown(CUIRect *pRect, int CurSelection, const char *const *pStrs, int Num, SDropDownState &State, const SDropDownProperties &DropDownProps)");

	ASSERT_FALSE(SelectionReset.empty());
	ASSERT_FALSE(PopupSelection.empty());
	ASSERT_FALSE(PopupButton.empty());
	ASSERT_FALSE(DoDropDown.empty());
	EXPECT_NE(UiHeader.find("int m_ActiveIndex;"), std::string::npos);
	EXPECT_NE(SelectionReset.find("m_ActiveIndex = -1;"), std::string::npos);
	EXPECT_NE(PopupButton.find("ButtonColor.has_value() || !TransparentInactive"), std::string::npos);
	EXPECT_NE(PopupSelection.find("const bool ActiveEntry = pSelectionPopup->m_ActiveIndex == static_cast<int>(Index);"), std::string::npos);
	EXPECT_NE(PopupSelection.find("ActiveEntry ? std::optional<ColorRGBA>"), std::string::npos);
	EXPECT_EQ(PopupSelection.find("Accent.VSplitLeft(2.0f"), std::string::npos);
	EXPECT_NE(PopupSelection.find("pSelectionPopup->m_TransparentButtons, true, ActiveColor"), std::string::npos);
	const size_t UpdateResult = DoDropDown.find("const SQmDropdownUpdateResult DropDownResult = State.m_DropDownState.Update(DropDownInput, Num);");
	const size_t ActiveIndexSync = DoDropDown.find("State.m_SelectionPopupContext.m_ActiveIndex = State.m_DropDownState.ActiveIndex();");
	const size_t PopupRender = DoDropDown.find("ShowPopupSelection(pRect->x, pRect->y, &State.m_SelectionPopupContext);");
	ASSERT_NE(UpdateResult, std::string::npos);
	ASSERT_NE(ActiveIndexSync, std::string::npos);
	ASSERT_NE(PopupRender, std::string::npos);
	EXPECT_LT(UpdateResult, ActiveIndexSync);
	EXPECT_LT(ActiveIndexSync, PopupRender);
}

TEST(QmNewUiMenuRenderDropdownContract, ValueSelectorUsesOneFittedTextLayoutForDisplayAndEditing)
{
	EXPECT_FLOAT_EQ(QmFitSingleLineFontSize(10.0f, 6.0f, 40.0f, 80.0f), 10.0f);
	EXPECT_FLOAT_EQ(QmFitSingleLineFontSize(10.0f, 6.0f, 100.0f, 80.0f), 8.0f);
	EXPECT_FLOAT_EQ(QmFitSingleLineFontSize(10.0f, 6.0f, 200.0f, 80.0f), 6.0f);

	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const std::string Selector = FunctionBody(UiSource, "SEditResult<int64_t> CUi::DoValueSelectorWithState");
	ASSERT_FALSE(Selector.empty());
	EXPECT_NE(Selector.find("QmFitSingleLineFontSize("), std::string::npos);
	EXPECT_NE(Selector.find("pRect->VMargin(2.0f, &Textbox);"), std::string::npos);
	EXPECT_NE(Selector.find("DoLabel(&Textbox, pDisplayText, ValueFontSize"), std::string::npos);
	EXPECT_NE(Selector.find("m_ActiveValueSelectorState.m_NumberInput.Render(&Textbox, EditFontSize, Props.m_TextAlign"), std::string::npos);
	EXPECT_NE(Selector.find("auto RenderValueSelectorDisplay = [&](bool RenderText = true)"), std::string::npos);
	EXPECT_NE(Selector.find("RenderValueSelectorDisplay(false);"), std::string::npos);
	EXPECT_EQ(Selector.find("TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));"), std::string::npos);
	EXPECT_NE(Selector.find("const ColorRGBA PreviousTextColor = TextRender()->GetTextColor();"), std::string::npos);
	EXPECT_NE(Selector.find("TextRender()->TextColor(PreviousTextColor);"), std::string::npos);
	EXPECT_EQ(Selector.find("m_NumberInput.Render(pRect, 10.0f"), std::string::npos);
	const std::string EditorSource = ReadTestSourceFile("src/game/editor/editor_ui.cpp");
	const std::string EditorSelector = FunctionBody(EditorSource, "SEditResult<int> CEditor::UiDoValueSelector");
	ASSERT_FALSE(EditorSelector.empty());
	EXPECT_NE(EditorSelector.find("DoEditBox(&s_NumberInput, pRect, 10.0f, Corners);"), std::string::npos);
	EXPECT_NE(EditorSelector.find("pRect->VMargin(2.0f, &Textbox);"), std::string::npos);
	EXPECT_NE(EditorSelector.find("Ui()->DoLabel(&Textbox, aBuf, 10, TEXTALIGN_MC);"), std::string::npos);
	const size_t EditingBranch = Selector.find("if(m_ActiveValueSelectorState.m_pLastTextId == pId)");
	const size_t DisplayBeforeOverlay = Selector.find("RenderValueSelectorDisplay(false);", EditingBranch);
	const size_t InputOverlay = Selector.find("m_ActiveValueSelectorState.m_NumberInput.Render(", EditingBranch);
	ASSERT_NE(DisplayBeforeOverlay, std::string::npos);
	ASSERT_NE(InputOverlay, std::string::npos);
	const size_t RestoreAfterInput = Selector.find("TextRender()->TextColor(PreviousTextColor);", InputOverlay);
	ASSERT_NE(RestoreAfterInput, std::string::npos);
	EXPECT_LT(DisplayBeforeOverlay, InputOverlay);
	EXPECT_LT(InputOverlay, RestoreAfterInput);
	const size_t FormatLambda = Selector.find("auto RenderValueSelectorDisplay = [&](bool RenderText = true)");
	const size_t FormatCurrent = Selector.find("Props.m_pfnFormatValue(Current", FormatLambda);
	const size_t ScrollUpdate = Selector.find("Current += Props.m_Step * Count;");
	const size_t FinalDisplay = Selector.rfind("RenderValueSelectorDisplay();");
	ASSERT_NE(FormatLambda, std::string::npos);
	ASSERT_NE(FormatCurrent, std::string::npos);
	ASSERT_NE(ScrollUpdate, std::string::npos);
	ASSERT_NE(FinalDisplay, std::string::npos);
	EXPECT_GT(FormatCurrent, FormatLambda);
	EXPECT_LT(ScrollUpdate, FinalDisplay);
}

TEST(QmNewUiMenuRenderDropdownContract, DisplayModesHideOnlyTheirVisualScrollbar)
{
	const std::string ListBoxHeader = ReadTextFile("src/game/client/ui_listbox.h");
	const std::string ListBoxSource = ReadTextFile("src/game/client/ui_listbox.cpp");
	EXPECT_NE(ListBoxHeader.find("bool m_HideScrollbar;"), std::string::npos);
	EXPECT_NE(ListBoxHeader.find("void SetHideScrollbar(bool HideScrollbar)"), std::string::npos);
	EXPECT_NE(ListBoxSource.find("m_HideScrollbar = false;"), std::string::npos);
	EXPECT_NE(ListBoxSource.find("ScrollParams.m_HideScrollbar = m_HideScrollbar;"), std::string::npos);
}
