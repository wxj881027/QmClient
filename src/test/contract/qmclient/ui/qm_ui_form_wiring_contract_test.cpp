#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>

#include <array>
#include <fstream>
#include <sstream>
#include <string>

TEST(QmTooltips, FriendNotesUseBoundedTooltipRects)
{
	const std::string BrowserSource = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string FriendsRender = FunctionBody(BrowserSource, "void CMenus::RenderServerbrowserFriends(CUIRect View)");
	EXPECT_NE(FriendsRender.find("DoToolTip(pListItemId, &Rect, TooltipText.c_str(), 320.0f);"), std::string::npos);
	EXPECT_NE(FriendsRender.find("DoToolTip(pSkinTooltipId, &Skin, Friend.Skin());"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsNumericFieldRectanglesInitializeBeforeLayoutBranching)
{
	const std::string Source = ReadTextFile("src/game/client/QmUi/UiForms.cpp");
	const std::string Body = FunctionBody(Source, "bool NumericField(const IUiContext &Ctx");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("CUIRect Label{}, Controls{}, ValueRect{}, ScrollBar{}, InputField{};"), std::string::npos);
	EXPECT_NE(Body.find("Controls.VSplitRight(ValueWidth, &ScrollBar, &InputField);"), std::string::npos);
	EXPECT_NE(Body.find("FieldOptions.m_TextAlign = TEXTALIGN_MC;"), std::string::npos);
	EXPECT_NE(Body.find("FieldOptions.m_pTrailingText = HasSuffix ? Options.m_pSuffix : nullptr;"), std::string::npos);
	EXPECT_NE(Body.find("FieldOptions.m_InlineTrailingText = HasSuffix;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NumericInputKeepsValueAndUnitInOneGeometry)
{
	const std::string Forms = ReadTextFile("src/game/client/QmUi/UiForms.cpp");
	const std::string Header = ReadTextFile("src/game/client/QmUi/UiForms.h");
	EXPECT_NE(Header.find("struct SInlineTrailingTextLayout"), std::string::npos);
	EXPECT_NE(Header.find("ResolveInlineTrailingTextLayout("), std::string::npos);
	EXPECT_NE(Forms.find("const SInlineTrailingTextLayout InlineLayout = ResolveInlineTrailingTextLayout"), std::string::npos);
	EXPECT_NE(Forms.find("FieldOptions.m_TextAlign = TEXTALIGN_MC;"), std::string::npos);
	EXPECT_NE(Forms.find("FieldOptions.m_InlineTrailingText = HasSuffix;"), std::string::npos);
	EXPECT_EQ(Forms.find("m_pInactiveDisplayText"), std::string::npos);
}

TEST(QmNewUiMenuBranches, EditBoxesRequirePressInsideConfiguredHitRect)
{
	const std::string Source = ReadTextFile("src/game/client/ui.cpp");
	const std::string Body = FunctionBody(Source, "bool CUi::DoEditBox(CLineInput *pLineInput, const CUIRect *pRect, float FontSize, int Corners, const std::vector<STextColorSplit> &vColorSplits, int Align, const SEditBoxRenderOptions &RenderOptions)");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("QmEditBoxShouldStartActivation(Inside, MouseButtonClicked(0))"), std::string::npos);
	EXPECT_EQ(Body.find("else if(Inside)"), std::string::npos);
	EXPECT_EQ(Body.find("else if(HotItem() == pLineInput)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, GraphicsFsaaSelectionDefersBackendReconfigure)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("static constexpr int s_aFsaaSamples[] = {0, 2, 4, 8, 16, 32, 64};"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_GfxFsaaSamples = s_aFsaaSamples[NewValue];"), std::string::npos);
	EXPECT_NE(Body.find("CheckSettings = true;"), std::string::npos);
	EXPECT_EQ(Body.find("Graphics()->SetMultiSampling"), std::string::npos);
	EXPECT_NE(Body.find("m_NeedRestartGraphics = !(s_GfxFsaaSamples == g_Config.m_GfxFsaaSamples"), std::string::npos);
}

TEST(QmNewUiMenuBranches, AnimationControlsExposeIndependentScopes)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("Localize(\"UI motion level\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Card list entry animation\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Card height animation\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Card reflow animation\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Presentation animations\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Animate chat box, emote selector, scoreboard, and spectate selection\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Off: disables all interface animations while preserving the options below\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Reduced: uses shorter transitions and disables presentation animations\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Full: each enabled animation category uses its complete transition\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Text input focus ring color\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TClientScaledInputsUseSettingsThemeAndPreciseFreezeLabels)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string SliderBody = FunctionBody(Source, "bool CMenus::DoSliderWithScaledValue(");
	ASSERT_FALSE(SliderBody.empty());

	EXPECT_NE(SliderBody.find("IUiContext InputCtx = SettingsUiContext(\"tclient_slider_input\""), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Base prediction margin\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Maximum reduction\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Delay before reduction\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Frozen prediction margin\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SharedListEntryRevealUsesElapsedGapWithoutMovingScrollGeometry)
{
	const std::string ListBoxSource = ReadTextFile("src/game/client/ui_listbox.cpp");
	const std::string ListBoxHeader = ReadTextFile("src/game/client/ui_listbox.h");

	EXPECT_EQ(ListBoxSource.find("PerfFrame()"), std::string::npos);
	EXPECT_EQ(ListBoxSource.find("QmListBoxShouldRearmInitialScroll"), std::string::npos);
	EXPECT_EQ(ListBoxHeader.find("m_LastRenderFrame"), std::string::npos);
	EXPECT_NE(ListBoxHeader.find("m_InitialScrollPending = true;"), std::string::npos);
	EXPECT_NE(ListBoxHeader.find("m_EntryAnimationStartTime"), std::string::npos);
	EXPECT_NE(ListBoxHeader.find("QmListBoxEntryAnimatedRect"), std::string::npos);
	EXPECT_NE(ListBoxSource.find("QmListBoxShouldStartEntryAnimation"), std::string::npos);
	EXPECT_NE(ListBoxSource.find("QmListBoxEntryOffset"), std::string::npos);
	const std::string DoNextRowBody = FunctionBody(ListBoxSource, "CListboxItem CListBox::DoNextRow()");
	const size_t AddRectPos = DoNextRowBody.find("m_ScrollRegion.AddRect(m_RowView);");
	const size_t AnimateRectPos = DoNextRowBody.find("QmListBoxEntryAnimatedRect");
	ASSERT_NE(AddRectPos, std::string::npos);
	ASSERT_NE(AnimateRectPos, std::string::npos);
	EXPECT_LT(AddRectPos, AnimateRectPos);
	const std::string DoCustomRowBody = FunctionBody(ListBoxSource, "CListboxItem CListBox::DoCustomRow(float Height, bool ScrollHere)");
	const size_t CustomAddRectPos = DoCustomRowBody.find("m_ScrollRegion.AddRect(Item.m_Rect, ScrollHere);");
	const size_t CustomAnimateRectPos = DoCustomRowBody.find("QmListBoxEntryAnimatedRect");
	ASSERT_NE(CustomAddRectPos, std::string::npos);
	ASSERT_NE(CustomAnimateRectPos, std::string::npos);
	EXPECT_LT(CustomAddRectPos, CustomAnimateRectPos);
	EXPECT_NE(ListBoxSource.find("if(!RenderOnly && EntryAnimationEnabled"), std::string::npos);
}

TEST(QmNewUiMenuBranches, StatusBarOpacityUsesPercentAndAxiomUsesInputTrailingActions)
{
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string QmClientSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");

	EXPECT_NE(TClientSource.find("\"tclient-statusbar-alpha\""), std::string::npos);
	EXPECT_NE(TClientSource.find("\"tclient-statusbar-text-alpha\""), std::string::npos);
	EXPECT_NE(TClientSource.find("&CUi::ms_LinearScrollbarScale, 0, \"%\""), std::string::npos);
	EXPECT_NE(QmClientSource.find("Options.m_pTrailingActionId = &ToggleButton;"), std::string::npos);
	EXPECT_NE(QmClientSource.find("Options.m_pTrailingActionIcon = Visible ? FONT_ICON_EYE_SLASH : FONT_ICON_EYE;"), std::string::npos);
	EXPECT_NE(QmClientSource.find("Options.m_TrailingActionQmIcon = static_cast<int>(Visible ? EQmIcon::EYE_OFF : EQmIcon::EYE);"), std::string::npos);
	EXPECT_EQ(QmClientSource.find("PasswordToggleRect"), std::string::npos);
}

TEST(QmNewUiMenuBranches, KeyReaderUsesOneOuterShellForValueAndDeleteAction)
{
	const std::string Source = ReadTextFile("src/game/client/components/key_binder.cpp");
	const std::string Body = FunctionBody(Source, "CKeyBinder::CKeyReaderResult CKeyBinder::DoKeyReader(");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiSurface.h>"), std::string::npos);
	EXPECT_NE(Body.find("DrawRoundedSurface(Ui(), *pRect, ReaderBaseColor"), std::string::npos);
	EXPECT_NE(Body.find("if(ClearChecked == 0)"), std::string::npos);
	EXPECT_NE(Body.find("const float ClearSurfaceAlpha = 0.22f * Ui()->ButtonColorMul(pClearButton);"), std::string::npos);
	EXPECT_NE(Body.find("DrawRoundedSurface(Ui(), ClearButton"), std::string::npos);
	EXPECT_NE(Body.find("ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f)"), std::string::npos);
	EXPECT_NE(Body.find("if(m_pKeyReaderId == pReaderButton && m_TakeKey)"), std::string::npos);
}
