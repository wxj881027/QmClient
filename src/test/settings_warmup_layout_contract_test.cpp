// 设置预热源码合同：设置页响应式度量与闲置重测量。运行时行为保留在 settings_warmup_test.cpp。
#include <game/client/components/menus.h>
#include <game/client/components/settings_resource_jobs.h>
#include <game/client/components/settings_warmup.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <fstream>
#include <limits>
#include <sstream>

TEST(SettingsWarmupLayoutContract, SixupTeeUsesUnifiedOuterAndNestedGridScrollProfiles)
{
	std::ifstream Tee7File(TestSourcePath("src/game/client/components/menus_settings7.cpp"));
	ASSERT_TRUE(Tee7File.good());
	std::stringstream Tee7Buffer;
	Tee7Buffer << Tee7File.rdbuf();
	const std::string Tee7Source = Tee7Buffer.str();
	EXPECT_NE(Tee7Source.find("SettingsPageLayout(MainView, UiScale)"), std::string::npos);
	EXPECT_NE(Tee7Source.find("EQmScrollProfile::SETTINGS_OUTER"), std::string::npos);
	EXPECT_NE(Tee7Source.find("SetScrollProfile(EQmScrollProfile::SETTINGS_GRID)"), std::string::npos);
	EXPECT_NE(Tee7Source.find("RenderSettingsTee7Content(Content, Metrics)"), std::string::npos);
	EXPECT_NE(Tee7Source.find("SettingsCardDeckForRenderPass().RenderCached("), std::string::npos);
	EXPECT_NE(Tee7Source.find("SettingsCardOrderModelForRenderPass()"), std::string::npos);
	EXPECT_NE(Tee7Source.find("RenderOnly ? nullptr : &s_Tee7SettingsScrollRegion"), std::string::npos);
}

TEST(SettingsWarmupLayoutContract, RemainingSettingsPagesUseResponsiveContentMetrics)
{
	const std::string TClient = ReadTestSourceFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Controls = ReadTestSourceFile("src/game/client/components/menus_settings_controls.cpp");
	const std::string Assets = ReadTestSourceFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Settings = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");

	EXPECT_NE(TClient.find("const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(ContentWidth);"), std::string::npos);
	EXPECT_NE(TClient.find("FontSize = Metrics.m_BodySize;"), std::string::npos);
	EXPECT_NE(TClient.find("LineSize = Metrics.m_LineHeight;"), std::string::npos);
	EXPECT_NE(TClient.find("MarginSmall = Metrics.m_LineSpacing;"), std::string::npos);
	EXPECT_NE(Controls.find("ApplyControlsContentMetrics(MainView.w);"), std::string::npos);
	EXPECT_NE(Controls.find("BUTTON_HEIGHT = Metrics.m_LineHeight;"), std::string::npos);
	EXPECT_NE(Controls.find("BUTTON_SPACING = Metrics.m_LineSpacing;"), std::string::npos);
	EXPECT_NE(Assets.find("const SSettingsContentMetrics ContentMetrics = ResolveSettingsContentMetrics(MainView.w);"), std::string::npos);
	EXPECT_NE(Assets.find("Localize(\"Loading assets...\"), ContentMetrics.m_BodySize"), std::string::npos);
	EXPECT_NE(Assets.find("Localize(\"No assets\"), ContentMetrics.m_BodySize"), std::string::npos);
	EXPECT_NE(Settings.find("pCheckBoxValue, float LineHeight, float LineSpacing, float BodySize, float ButtonHeight)"), std::string::npos);
	EXPECT_NE(Settings.find("const float ResolvedButtonHeight = ButtonHeight > 0.0f ? ButtonHeight : LineHeight;"), std::string::npos);
	EXPECT_EQ(Settings.find("Localize(\"Text\"), BodySize"), std::string::npos);
	EXPECT_NE(Settings.find("DoSettingsMenuLabel(SETTINGS_APPEARANCE, Tab, Tab, pLabelTextId, &Label, pLabel, BodySize"), std::string::npos);
	EXPECT_NE(Settings.find("const float LineHeight = SoundMetrics.m_LineHeight;"), std::string::npos);
	EXPECT_NE(Settings.find("const float LineSpacing = SoundMetrics.m_LineSpacing;"), std::string::npos);
	EXPECT_NE(Settings.find("SoundToggleCardHeight = ToggleChromeHeight + LineHeight * ToggleRowCount"), std::string::npos);
	EXPECT_EQ(Settings.find("s_SoundToggleCardHeight"), std::string::npos);
}

TEST(SettingsWarmupLayoutContract, ControlsCardMeasurementsAvoidIdleBindingRescan)
{
	const std::string Controls = ReadTestSourceFile("src/game/client/components/menus_settings_controls.cpp");
	EXPECT_EQ(Controls.find("Definition.m_MeasureEachFrame = true;"), std::string::npos);
	const size_t ControllerCard = Controls.find("AddCard(vCards, \"deck:controls-controller\"");
	ASSERT_NE(ControllerCard, std::string::npos);
	const size_t ControllerCardEnd = Controls.find(";", ControllerCard);
	ASSERT_NE(ControllerCardEnd, std::string::npos);
	EXPECT_EQ(Controls.substr(ControllerCard, ControllerCardEnd - ControllerCard).find("true"), std::string::npos);
	const size_t RevisionStart = Controls.find("uint64_t CardLayoutRevision");
	const size_t DefinitionsStart = Controls.find("const auto BuildDefinitions", RevisionStart);
	ASSERT_NE(RevisionStart, std::string::npos);
	ASSERT_NE(DefinitionsStart, std::string::npos);
	const std::string RevisionBody = Controls.substr(RevisionStart, DefinitionsStart - RevisionStart);
	EXPECT_EQ(RevisionBody.find("MeasureSettingsMouseHeight()"), std::string::npos);
	EXPECT_EQ(RevisionBody.find("MeasureSettingsJoystickHeight()"), std::string::npos);
	EXPECT_NE(RevisionBody.find("g_Config.m_InpControllerEnable"), std::string::npos);
	EXPECT_NE(RevisionBody.find("pActiveJoystick->GetNumAxes()"), std::string::npos);
	EXPECT_NE(Controls.find("Definition.m_MeasureRevision = MeasureRevision;"), std::string::npos);
	EXPECT_NE(Controls.find("m_BindLayoutRevision"), std::string::npos);
	EXPECT_NE(Controls.find("++m_BindLayoutRevision;"), std::string::npos);
	EXPECT_NE(Controls.find("SettingsCardDeckForRenderPass().RenderCached("), std::string::npos);
	EXPECT_NE(Controls.find("SettingsCardOrderModelForRenderPass()"), std::string::npos);
	EXPECT_NE(Controls.find("ReadOnly ? nullptr : &m_SettingsScrollRegion"), std::string::npos);
	EXPECT_NE(Controls.find("if(!ReadOnly && ui_widget::InputField("), std::string::npos);
	EXPECT_NE(Controls.find("else if(!ReadOnly && !m_vSearchMatches.empty()"), std::string::npos);
	EXPECT_NE(Controls.find("if(!ReadOnly && m_SearchMatchReveal"), std::string::npos);
	EXPECT_NE(Controls.find("if(!ReadOnly && DeckResult.m_OrderChanged)"), std::string::npos);
}

TEST(SettingsWarmupLayoutContract, SettingsCardsAvoidIdlePerFrameMeasurement)
{
	const std::string Settings = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	const std::string QmClient = ReadTestSourceFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string TClient = ReadTestSourceFile("src/game/client/components/tclient/menus_tclient.cpp");

	EXPECT_EQ(Settings.find("Definition.m_MeasureEachFrame = true;"), std::string::npos);
	EXPECT_EQ(QmClient.find("m_MeasureEachFrame = true;"), std::string::npos);
	EXPECT_EQ(TClient.find("m_MeasureEachFrame = true;"), std::string::npos);
	EXPECT_NE(Settings.find("Definition.m_MeasureRevision = static_cast<uint64_t>"), std::string::npos);
	EXPECT_EQ(QmClient.find("Definition.m_MeasureRevision = static_cast<uint64_t>"), std::string::npos);
	// 卡片定义装配已迁入全局卡片目录（N3）：Qm 侧三个页面不再各自出现该赋值行，
	// 但「每张卡有独立重测版本、避免逐帧测量」这一不变量仍成立，改在目录的构造器中断言。
	const std::string CardCatalog = ReadTestSourceFile("src/game/client/QmUi/cards/QmCardCatalog.cpp");
	EXPECT_NE(CardCatalog.find("Out.m_MeasureRevision = MeasureRevision;"), std::string::npos);
}

TEST(SettingsWarmupLayoutContract, TClientSettingsUseTwoLevelFontScale)
{
	const std::string MenusHeader = ReadTestSourceFile("src/game/client/components/menus.h");
	const std::string TClient = ReadTestSourceFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Menus = ReadTestSourceFile("src/game/client/components/menus.cpp");
	const std::string Ui = ReadTestSourceFile("src/game/client/ui.cpp") + ReadTestSourceFile("src/game/client/ui_popups.cpp");
	const std::string KeyBinder = ReadTestSourceFile("src/game/client/components/key_binder.cpp");

	EXPECT_NE(MenusHeader.find("static constexpr float TCLIENT_SETTINGS_BODY_FONT_SIZE = 11.2f;"), std::string::npos);
	EXPECT_NE(TClient.find("constexpr float TCLIENT_BODY_FONT_SIZE = CMenus::TCLIENT_SETTINGS_BODY_FONT_SIZE;"), std::string::npos);
	EXPECT_NE(TClient.find("constexpr float TCLIENT_HEADLINE_FONT_SIZE = 20.0f;"), std::string::npos);
	EXPECT_NE(TClient.find("Props.m_MinimumFontSize = FontSize;"), std::string::npos);
	EXPECT_NE(TClient.find("Props.m_EllipsisAtEnd = true;"), std::string::npos);
	EXPECT_NE(TClient.find("EditBoxFontSize = Metrics.m_BodySize;"), std::string::npos);
	EXPECT_NE(TClient.find("ColorPickerLabelSize = Metrics.m_BodySize;"), std::string::npos);
	EXPECT_EQ(TClient.find("const float EditBoxFontSize = 12.0f;"), std::string::npos);
	EXPECT_EQ(TClient.find("const float ColorPickerLabelSize = 13.0f;"), std::string::npos);
	EXPECT_EQ(TClient.find("Ui()->DoLabel(&Help, pVar->m_pHelp ? pVar->m_pHelp : \"\", 11.0f"), std::string::npos);
	EXPECT_EQ(TClient.find("Ui()->DoEditBox(&s_NameInput, &ButtonL, 12.0f)"), std::string::npos);
	EXPECT_EQ(TClient.find("Height / LineSize * FontSize"), std::string::npos);
	EXPECT_NE(TClient.find("Ui()->DoLabel(&Label, Profile.m_Name, ProfileMetrics.m_BodySize, TEXTALIGN_ML);"), std::string::npos);
	EXPECT_NE(TClient.find("Ui()->DoLabel(&Label, Profile.m_Clan, ProfileMetrics.m_BodySize, TEXTALIGN_ML);"), std::string::npos);
	EXPECT_NE(TClient.find("DoLine_ColorPicker(&ResetId, CurrentSettingsContentMetrics(), &ColorRect, \"\", &ColState.m_Working, DefaultColor, false, nullptr, pCol->m_Alpha);"), std::string::npos);
	EXPECT_NE(TClient.find("DoTClientSettingsButton_Menu(&ResetBtn, \"tclient-config-reset\", Localize(\"Reset\")"), std::string::npos);
	EXPECT_NE(TClient.find("DoKeyReader(&ReaderButton, &ClearButton, &KeyButton, Bind, false, TCLIENT_BODY_FONT_SIZE)"), std::string::npos);
	EXPECT_NE(TClient.find("DoSettingsDropDown(&Button, FontSelectedOld, s_FontDropDownNames.data(), s_FontDropDownNames.size(), s_FontDropDownState)"), std::string::npos);
	EXPECT_NE(TClient.find("DoButton_MenuTab(&s_aPageTabs[Tab], s_apTClientTabNames[Tab], ActiveTab == Tab, &Button, Corners"), std::string::npos);
	EXPECT_NE(TClient.find("ui_widget::InputField(TClientWarListEntriesSearchCtx, &s_EntriesFilterInput, EntriesSearch, FontSize"), std::string::npos);

	EXPECT_NE(Menus.find("const float BodySize = RequestedFontSize > 0.0f ? RequestedFontSize : CurrentSettingsContentMetrics().m_BodySize;"), std::string::npos);
	EXPECT_NE(Menus.find("const bool FixedFontSize = LabelFontSize > 0.0f;"), std::string::npos);
	EXPECT_NE(Menus.find("const float FontSize = LabelFontSize > 0.0f ? LabelFontSize : Box.h * CUi::ms_FontmodHeight;"), std::string::npos);
	EXPECT_NE(Menus.find("const float ResolvedBodySize = BodySize > 0.0f ? BodySize : CurrentSettingsContentMetrics().m_BodySize;"), std::string::npos);
	EXPECT_EQ(Menus.find("Page == SETTINGS_TCLIENT ? 14.0f"), std::string::npos);
	EXPECT_NE(Menus.find("Props.m_MinimumFontSize = FixedFontSize ? FontSize : FontSize * 0.7f;"), std::string::npos);
	EXPECT_NE(Menus.find("Props.m_EllipsisAtEnd = FixedFontSize;"), std::string::npos);
	EXPECT_NE(Menus.find("ResolveSettingsCheckboxFontSize(BodySize, RequestedFontSize, pRect->h, Box.h, CUi::ms_FontmodHeight)"), std::string::npos);
	EXPECT_NE(Menus.find("return DoButton_Menu(pBC, pText, Checked, pRect, Flags, nullptr, Corners, Rounding, FontFactor, Color, &TextElement, ResolvedBodySize);"), std::string::npos);
	EXPECT_NE(KeyBinder.find("Props.m_MinimumFontSize = FontSize;"), std::string::npos);
	EXPECT_NE(KeyBinder.find("Props.m_EllipsisAtEnd = true;"), std::string::npos);

	EXPECT_NE(Ui.find("Props.m_FontSize = ResolvedFontSize;"), std::string::npos);
	EXPECT_NE(Ui.find("RectEl.m_LabelFlags = Flags;"), std::string::npos);
	EXPECT_NE(Ui.find("UIElement.Rect(0)->m_FontSize != FontSize"), std::string::npos);
	EXPECT_NE(Ui.find("RectEl.m_LabelMaxWidth != LabelProps.m_MaxWidth"), std::string::npos);
	EXPECT_NE(Ui.find("RectEl.m_LabelFlags != Flags"), std::string::npos);
	EXPECT_NE(Ui.find("State.m_SelectionPopupContext.m_FontSize = ResolvedFontSize;"), std::string::npos);
}

TEST(SettingsWarmupLayoutContract, PassiveTooltipOnlyUiHelpersStayOutOfButtonLogic)
{
	const std::string Header = ReadTestSourceFile("src/game/client/ui.h");
	EXPECT_NE(Header.find("void RegisterPassiveHotItem(const void *pId, const CUIRect *pRect);"), std::string::npos);

	const std::string UiSource = ReadTestSourceFile("src/game/client/ui.cpp");
	const size_t HelperPos = UiSource.find("void CUi::RegisterPassiveHotItem(const void *pId, const CUIRect *pRect)");
	ASSERT_NE(HelperPos, std::string::npos);
	const size_t NextFunctionPos = UiSource.find("int CUi::DoButtonLogic", HelperPos);
	ASSERT_NE(NextFunctionPos, std::string::npos);
	const std::string HelperBody = UiSource.substr(HelperPos, NextFunctionPos - HelperPos);

	EXPECT_NE(HelperBody.find("MouseHovered(pRect)"), std::string::npos);
	EXPECT_NE(HelperBody.find("SetHotItem(pId);"), std::string::npos);
	EXPECT_EQ(HelperBody.find("SetActiveItem"), std::string::npos);
	EXPECT_EQ(HelperBody.find("MouseButton("), std::string::npos);

	const std::string MenusSettingsSource = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	EXPECT_NE(MenusSettingsSource.find("Ui()->RegisterPassiveHotItem(pStatusTooltipId, &StatusIcon);"), std::string::npos);
	EXPECT_NE(MenusSettingsSource.find("Ui()->RegisterPassiveHotItem(&s_HookCollToolTip, &LeftView);"), std::string::npos);
	EXPECT_EQ(MenusSettingsSource.find("Ui()->DoButtonLogic(pStatusTooltipId, 0, &StatusIcon, BUTTONFLAG_NONE);"), std::string::npos);
	EXPECT_EQ(MenusSettingsSource.find("Ui()->DoButtonLogic(&s_HookCollToolTip, 0, &LeftView, BUTTONFLAG_NONE);"), std::string::npos);

	const std::string MenusQmClientSource = ReadTestSourceFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	EXPECT_EQ(MenusQmClientSource.find("RegisterPassiveHotItem("), std::string::npos);
}
