// QmNewUi 菜单源码合同：render 标准设置页栈与滚轮归属。运行时行为保留在 qm_new_ui_menu_branch_test.cpp.
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

TEST(QmNewUiMenuRenderSettingsPagesContract, GeneralStandardPageUsesUnifiedSettingsStack)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Menus = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string General = FunctionBody(Source, "void CMenus::RenderSettingsGeneral(CUIRect MainView)");
	const std::string NumericLabelBridge = FunctionBody(Menus, "bool CMenus::PrepareSettingsNumericFieldLabel(");
	ASSERT_FALSE(General.empty());
	ASSERT_FALSE(NumericLabelBridge.empty());
	EXPECT_NE(General.find("SettingsPageLayout("), std::string::npos);
	EXPECT_NE(General.find("SSettingsCardDefinition"), std::string::npos);
	EXPECT_NE(General.find("SettingsCardDeckForRenderPass().RenderCached("), std::string::npos);
	EXPECT_NE(General.find("CQmScrollState"), std::string::npos);
	EXPECT_NE(General.find("const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy("), std::string::npos);
	EXPECT_NE(General.find("QmScrollRegionParamsFromPolicy(ScrollPolicy)"), std::string::npos);
	EXPECT_EQ(General.find("(void)QmResolveScrollPolicy("), std::string::npos);
	EXPECT_NE(General.find("PrepareSettingsNumericFieldLabel("), std::string::npos);
	EXPECT_NE(NumericLabelBridge.find("if(m_MenuTextPlanCollecting)"), std::string::npos);
	EXPECT_NE(NumericLabelBridge.find("CollectMenuTextPlanItem(MENU_TEXT_SCOPE_SETTINGS"), std::string::npos);
	EXPECT_NE(General.find("ui_widget::NumericField("), std::string::npos);
	EXPECT_NE(General.find("const auto IsGeneralDynamicCameraEnabled"), std::string::npos);
	EXPECT_NE(General.find("ResolveSettingsGeneralGameContentHeight(GeneralMetrics, IsGeneralDynamicCameraEnabled())"), std::string::npos);
	EXPECT_NE(General.find("vCards.back().m_PreLayoutInput = [this, GeneralMetrics, IsGeneralDynamicCameraEnabled]"), std::string::npos);
	EXPECT_NE(General.find("ResolveSettingsGeneralLanguageListGeometry("), std::string::npos);
	EXPECT_NE(General.find("ResolveSettingsGeneralThemeListGeometry("), std::string::npos);
	EXPECT_NE(General.find("RenderLanguageSelection(Content, &GeneralMetrics);"), std::string::npos);
	EXPECT_NE(General.find("RenderThemeSelection(Content, &GeneralMetrics);"), std::string::npos);
	EXPECT_NE(General.find("ResolveSettingsGeneralLayoutRevision("), std::string::npos);
	EXPECT_NE(General.find("Content.h = std::min(Content.h, GeneralLanguageListHeight);"), std::string::npos);
	EXPECT_NE(General.find("Content.h = std::min(Content.h, GeneralThemeListHeight);"), std::string::npos);
	EXPECT_NE(General.find("Row.VSplitMid(&LeftButton, &RightButton, GeneralMetrics.m_LineSpacing);"), std::string::npos);
	EXPECT_EQ(General.find("maximum(300.0f * UiScale, GeneralPage.m_ScrollViewport.h - 100.0f * UiScale)"), std::string::npos);
	EXPECT_NE(General.find("deck:general-game"), std::string::npos);
	EXPECT_NE(General.find("deck:general-language"), std::string::npos);
	EXPECT_NE(General.find("deck:general-client"), std::string::npos);
	EXPECT_NE(General.find("deck:general-recording"), std::string::npos);
	EXPECT_NE(General.find("RecordingDefinition.m_MeasureRevision"), std::string::npos);
	EXPECT_NE(General.find("RecordingDefinition.m_PreLayoutInput"), std::string::npos);
	EXPECT_NE(General.find("RecordingDefinition.m_VisibilityController = true;"), std::string::npos);
	EXPECT_NE(General.find("return 4.0f * GeneralMetrics.m_RowStep + EnabledRows * (GeneralMetrics.m_RowStep + GeneralMetrics.m_LineSpacing);"), std::string::npos);
	EXPECT_EQ(General.find("AddCard(RecordingSpec"), std::string::npos);
	EXPECT_EQ(General.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(General.find("DoSettingsScrollbarOption("), std::string::npos);
	EXPECT_EQ(General.find("Ui()->DoEditBox("), std::string::npos);
	EXPECT_EQ(General.find("Ui()->DoScrollbarH("), std::string::npos);
}

TEST(QmNewUiMenuRenderSettingsPagesContract, SettingsCardContentHeightsExcludeSharedHeaderChrome)
{
	const std::string ControlsSource = ReadTextFile("src/game/client/components/menus_settings_controls.cpp");
	const std::string ContributorsSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string MouseMeasure = FunctionBody(ControlsSource, "float CMenusSettingsControls::MeasureSettingsMouseHeight() const");
	const std::string Contributors = FunctionBody(ContributorsSource, "void CMenus::RenderSettingsQmClientContributors(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(MouseMeasure.empty());
	ASSERT_FALSE(Contributors.empty());
	EXPECT_NE(MouseMeasure.find("return 2.0f * BUTTON_HEIGHT + BUTTON_SPACING;"), std::string::npos);
	EXPECT_EQ(MouseMeasure.find("CARD_HEADER"), std::string::npos);
	EXPECT_NE(Contributors.find("Community.m_Measure = [LineHeight, LineSpacing](float) { return ResolveSettingsRowsHeight(3, LineHeight, LineSpacing); };"), std::string::npos);
}

TEST(QmNewUiMenuRenderSettingsPagesContract, Tee7NestedGridsOwnWheelAndCacheRefreshes)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings7.cpp");
	const std::string Tee7 = FunctionBody(Source, "void CMenus::RenderSettingsTee7Content(CUIRect MainView, const SSettingsContentMetrics &Metrics)");
	const std::string SkinSelection = FunctionBody(Source, "void CMenus::RenderSkinSelection7(CUIRect MainView, float BodySize)");
	const std::string SkinPartSelection = FunctionBody(Source, "void CMenus::RenderSkinPartSelection7(CUIRect MainView, float BodySize)");
	ASSERT_FALSE(Tee7.empty());
	ASSERT_FALSE(SkinSelection.empty());
	ASSERT_FALSE(SkinPartSelection.empty());
	EXPECT_EQ(Tee7.find("Buttons.VSplitLeft(220.0f, &QuickSearch, &Buttons);"), std::string::npos);
	EXPECT_NE(Tee7.find("Buttons.VSplitRight(120.0f, &QuickSearch, &SaveDeleteButton);"), std::string::npos);

	const size_t SkinPriority = SkinSelection.find("s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t SkinStart = SkinSelection.find("s_ListBox.DoStart(", SkinPriority);
	ASSERT_NE(SkinPriority, std::string::npos);
	ASSERT_NE(SkinStart, std::string::npos);
	EXPECT_LT(SkinPriority, SkinStart);
	EXPECT_NE(SkinSelection.find("SetScrollProfile(EQmScrollProfile::SETTINGS_GRID)"), std::string::npos);
	EXPECT_NE(SkinSelection.find("std::vector<std::string>"), std::string::npos);
	EXPECT_EQ(SkinSelection.find("std::vector<const CSkins7::CSkin *>"), std::string::npos);
	EXPECT_NE(SkinSelection.find("m_SkinList7LastRefreshTime.value() != RefreshTime"), std::string::npos);
	EXPECT_NE(SkinSelection.find("m_SkinList7LastRefreshTime = RefreshTime;"), std::string::npos);
	EXPECT_EQ(SkinSelection.find("m_SkinList7LastRefreshTime.value() != m_SkinList7LastRefreshTime"), std::string::npos);

	const size_t SkinPartPriority = SkinPartSelection.find("s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t SkinPartStart = SkinPartSelection.find("s_ListBox.DoStart(", SkinPartPriority);
	ASSERT_NE(SkinPartPriority, std::string::npos);
	ASSERT_NE(SkinPartStart, std::string::npos);
	EXPECT_LT(SkinPartPriority, SkinPartStart);
	EXPECT_NE(SkinPartSelection.find("SetScrollProfile(EQmScrollProfile::SETTINGS_GRID)"), std::string::npos);
	EXPECT_NE(SkinPartSelection.find("std::vector<std::string>"), std::string::npos);
	EXPECT_EQ(SkinPartSelection.find("std::vector<const CSkins7::CSkinPart *>"), std::string::npos);
	EXPECT_NE(SkinPartSelection.find("m_SkinPartsList7LastRefreshTime.value() != RefreshTime"), std::string::npos);
	EXPECT_NE(SkinPartSelection.find("m_SkinPartsList7LastRefreshTime = RefreshTime;"), std::string::npos);
	EXPECT_EQ(SkinPartSelection.find("m_SkinList7LastRefreshTime"), std::string::npos);
}

TEST(QmNewUiMenuRenderSettingsPagesContract, CountryPopupOwnsWheelAndBlocksTheSettingsPage)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Popup = FunctionBody(Source, "CUi::EPopupMenuFunctionResult CMenus::PopupSettingsCountrySelection(void *pContext, CUIRect View, bool Active)");
	const std::string Identity = FunctionBody(Source, "void CMenus::RenderSettingsTeeIdentity(CUIRect MainView, CUIRect *pFlagButton, float BodySize)");
	const std::string MapPopup = FunctionBody(Source, "CUi::EPopupMenuFunctionResult CMenus::PopupMapPicker(void *pContext, CUIRect View, bool Active)");
	const std::string DDNet = FunctionBody(Source, "void CMenus::RenderSettingsDDNet(CUIRect MainView)");
	ASSERT_FALSE(Popup.empty());
	ASSERT_FALSE(Identity.empty());
	ASSERT_FALSE(MapPopup.empty());
	ASSERT_FALSE(DDNet.empty());
	EXPECT_NE(Popup.find("s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::POPUP);"), std::string::npos);
	EXPECT_NE(Popup.find("s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_GRID);"), std::string::npos);
	EXPECT_NE(Identity.find("PopupProps.m_BlockUnderlyingScroll = true;"), std::string::npos);
	EXPECT_NE(Identity.find("PopupSettingsCountrySelection, PopupProps"), std::string::npos);
	EXPECT_NE(MapPopup.find("s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::POPUP);"), std::string::npos);
	EXPECT_NE(MapPopup.find("s_ListBox.SetScrollProfile(EQmScrollProfile::POPUP_LIST);"), std::string::npos);
	const size_t MapPickerId = DDNet.find("s_PopupMapPickerId");
	ASSERT_NE(MapPickerId, std::string::npos);
	EXPECT_NE(DDNet.find("QmResolveDropdownPopupPolicy", MapPickerId), std::string::npos);
	EXPECT_NE(DDNet.find("CUi::PopupMenuContentInset()", MapPickerId), std::string::npos);
	EXPECT_NE(DDNet.find("PopupPolicy.m_PreferredHeight", MapPickerId), std::string::npos);
	EXPECT_NE(DDNet.find("PopupProps.m_BlockUnderlyingScroll = true;", MapPickerId), std::string::npos);
	EXPECT_NE(DDNet.find("PopupMapPicker, PopupProps", MapPickerId), std::string::npos);
}

TEST(QmNewUiMenuRenderSettingsPagesContract, TeeOptionsMeasureAllRowsAndPlayerDummyChangeDisplayCycle)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Tee = FunctionBody(Source, "void CMenus::RenderSettingsTee(CUIRect MainView)");
	const std::string Settings = FunctionBody(Source, "void CMenus::RenderSettings(CUIRect MainView)");
	ASSERT_FALSE(Tee.empty());
	ASSERT_FALSE(Settings.empty());
	EXPECT_NE(Tee.find("ResolveSettingsRowsHeight(4, ControlLineHeight, ControlSpacing)"), std::string::npos);
	EXPECT_NE(Tee.find("ResolveSettingsRowsHeight(6, ControlLineHeight, ControlSpacing)"), std::string::npos);
	EXPECT_NE(Tee.find("ResolveSettingsTeeCustomColorsLayout"), std::string::npos);
	EXPECT_NE(Tee.find("g_Config.m_QmSkinShowMetadata != 0"), std::string::npos);
	EXPECT_EQ(Tee.find("g_Config.m_QmSkinSortMode == 1 && g_Config.m_QmSkinShowMetadata"), std::string::npos);
	EXPECT_NE(Tee.find("SkinSortDropDownProps.m_FontSize = BodySize;"), std::string::npos);
	EXPECT_NE(Tee.find("const float SortLabelWidth = std::clamp(SortModeControl.w * 0.36f"), std::string::npos);
	EXPECT_NE(Tee.find("SortDropDown.VSplitLeft(ControlSpacing, nullptr, &SortDropDown);"), std::string::npos);
	EXPECT_EQ(Tee.find("settings_tee_skin_sort_dropdown"), std::string::npos);
	EXPECT_EQ(Tee.find("SkinSortDropDownProps.m_VisualStyle"), std::string::npos);
	EXPECT_NE(ConfigSource.find("\"Show skin release date and author\""), std::string::npos);
	EXPECT_EQ(ConfigSource.find("\"Show release date and author when sorted by date\""), std::string::npos);
	EXPECT_NE(Tee.find("const auto NextCheckboxRow"), std::string::npos);
	EXPECT_NE(Tee.find("const auto NextPrefixRow"), std::string::npos);
	EXPECT_NE(Tee.find("s_QueueListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);"), std::string::npos);
	EXPECT_NE(Tee.find("s_PresetListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);"), std::string::npos);
	const size_t DummyInput = Tee.find("s_TeeSubTab = 1;");
	const size_t DisplayCycle = Tee.find("const uint64_t TeeDisplayKey", DummyInput);
	ASSERT_NE(DummyInput, std::string::npos);
	ASSERT_NE(DisplayCycle, std::string::npos);
	EXPECT_LT(DummyInput, DisplayCycle);
	EXPECT_NE(Tee.find("m_SettingsCardDeckDisplayState.EnterView(TeeDisplayKey)", DisplayCycle), std::string::npos);
	EXPECT_NE(Settings.find("g_Config.m_UiSettingsPage != SETTINGS_TEE"), std::string::npos);
	EXPECT_EQ(Settings.find("m_Dummy + 1"), std::string::npos);
}

TEST(QmNewUiMenuRenderSettingsPagesContract, SettingsSubTabPagesUseTheSharedLayoutContract)
{
	const std::string Settings = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Assets = ReadTextFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string TClient = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string QmClient = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	EXPECT_NE(FunctionBody(Settings, "void CMenus::RenderSettingsPlayer(CUIRect MainView)").find("ResolveSettingsSubTabLayout("), std::string::npos);
	EXPECT_NE(FunctionBody(Settings, "void CMenus::RenderSettingsTee(CUIRect MainView)").find("ResolveSettingsSubTabLayout("), std::string::npos);
	EXPECT_NE(FunctionBody(Settings, "void CMenus::RenderSettingsAppearance(CUIRect MainView)").find("ResolveSettingsSubTabLayout("), std::string::npos);
	EXPECT_NE(FunctionBody(Assets, "void CMenus::RenderSettingsCustom(CUIRect MainView)").find("ResolveSettingsSubTabLayout("), std::string::npos);
	EXPECT_NE(FunctionBody(TClient, "void CMenus::RenderSettingsTClient(CUIRect MainView, bool PrewarmOnly)").find("TClientSettingsContentView("), std::string::npos);
	EXPECT_NE(TClient.find("ResolveSettingsSubTabLayout(MainView, Metrics.m_UiScale)"), std::string::npos);
	EXPECT_NE(FunctionBody(QmClient, "void CMenus::RenderSettingsQmClientContent(CUIRect MainView, bool ContributorsPage, bool PrewarmOnly)").find("ResolveSettingsSubTabLayout("), std::string::npos);
}

TEST(QmNewUiMenuRenderSettingsPagesContract, GraphicsAndSoundNestedListsOwnWheel)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Graphics = FunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	const std::string Sound = FunctionBody(Source, "void CMenus::RenderSettingsSound(CUIRect MainView)");
	ASSERT_FALSE(Graphics.empty());
	ASSERT_FALSE(Sound.empty());
	const size_t GraphicsPriority = Graphics.find("s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t GraphicsStart = Graphics.find("s_ListBox.DoStart(RowHeightResList", GraphicsPriority);
	const size_t SoundPriority = Sound.find("s_AudioPackListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t SoundStart = Sound.find("s_AudioPackListBox.DoStart(", SoundPriority);
	ASSERT_NE(GraphicsPriority, std::string::npos);
	ASSERT_NE(GraphicsStart, std::string::npos);
	ASSERT_NE(SoundPriority, std::string::npos);
	ASSERT_NE(SoundStart, std::string::npos);
	EXPECT_LT(GraphicsPriority, GraphicsStart);
	EXPECT_LT(SoundPriority, SoundStart);
	EXPECT_NE(Graphics.find("s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);"), std::string::npos);
	EXPECT_NE(Sound.find("s_AudioPackListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);"), std::string::npos);
	EXPECT_NE(Graphics.find("ResolveSettingsGraphicsModesGeometry("), std::string::npos);
	EXPECT_NE(Sound.find("ResolveSettingsSoundAudioPackGeometry("), std::string::npos);
	EXPECT_NE(Sound.find("ResolveSettingsSoundAudioPackGeometry(AudioPackCount, SoundMetrics)"), std::string::npos);
	EXPECT_NE(Sound.find("s_AudioPackListBox.SetItemColors(ui_token::color::LIST_ITEM_SELECTED"), std::string::npos);
	EXPECT_NE(Sound.find("ResolveSettingsSoundLayoutRevision(RenderOnly, g_Config.m_SndEnable != 0, AudioPackCount)"), std::string::npos);
	EXPECT_NE(Graphics.find("const int GraphicsBackendRowCount"), std::string::npos);
	EXPECT_NE(Graphics.find("GraphicsDisplayRowCount = 5 + (Graphics()->GetNumScreens() > 1 ? 1 : 0) + GraphicsBackendRowCount"), std::string::npos);
	EXPECT_EQ(Graphics.find("const auto NextBackendRow"), std::string::npos);
	EXPECT_NE(Graphics.find("GraphicsModesMeasureRevision"), std::string::npos);
	EXPECT_NE(Graphics.find("s_ListBox.SetHideScrollbar(true);"), std::string::npos);
	EXPECT_NE(Graphics.find("s_ListBox.SetItemColors(ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_HOVER);"), std::string::npos);
	EXPECT_EQ(Graphics.find("GraphicsBackendMinCardHeight = 104.0f"), std::string::npos);
	EXPECT_EQ(Graphics.find("Localize(\"Graphics card\"), 16.0f"), std::string::npos);
}

TEST(QmNewUiMenuRenderSettingsPagesContract, GraphicsPilotHasNoRemainingLegacyInputOrScrollPath)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Graphics = FunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	ASSERT_FALSE(Graphics.empty());
	EXPECT_NE(Graphics.find("SettingsPageLayout("), std::string::npos);
	EXPECT_NE(Graphics.find("SSettingsCardDefinition"), std::string::npos);
	EXPECT_NE(Graphics.find("SettingsCardDeckForRenderPass().RenderCached("), std::string::npos);
	EXPECT_NE(Graphics.find("ui_widget::NumericField("), std::string::npos);
	EXPECT_NE(Graphics.find("QmResolveScrollPolicy("), std::string::npos);
	EXPECT_NE(Graphics.find("CQmScrollState"), std::string::npos);
	EXPECT_NE(Graphics.find("deck:graphics-display"), std::string::npos);
	EXPECT_NE(Graphics.find("deck:graphics-visual"), std::string::npos);
	EXPECT_EQ(Graphics.find("deck:graphics-backend"), std::string::npos);
	EXPECT_NE(Graphics.find("deck:graphics-modes"), std::string::npos);
	EXPECT_EQ(Graphics.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(Graphics.find("DoSliderWithValueInput("), std::string::npos);
	EXPECT_EQ(Graphics.find("Ui()->DoScrollbarH("), std::string::npos);
	EXPECT_EQ(Graphics.find("Ui()->DoValueSelectorWithState("), std::string::npos);
	EXPECT_EQ(Graphics.find("s_GraphicsSettingsScrollRegion"), std::string::npos);
}

TEST(QmNewUiMenuRenderSettingsPagesContract, NestedLanguageListWheelOwnerOutranksGeneralPage)
{
	EXPECT_TRUE(QmHotScrollRegionPriorityWins(EUiWheelOwnerPriority::PAGE, EUiWheelOwnerPriority::COMPOSITE_CONTROL));
	EXPECT_FALSE(QmHotScrollRegionPriorityWins(EUiWheelOwnerPriority::COMPOSITE_CONTROL, EUiWheelOwnerPriority::PAGE));
	const std::string ScrollRegionSource = ReadTextFile("src/game/client/ui_scrollregion.cpp");
	EXPECT_NE(ScrollRegionSource.find("Ui()->SetHotScrollRegion(this, m_Params.m_WheelOwnerPriority);"), std::string::npos);

	CScrollWheelOwnership Ownership;
	int OuterOwner = 0;
	int InnerOwner = 0;
	ASSERT_TRUE(Ownership.BeginFrame(1, 1.0f, false));
	Ownership.Register(&OuterOwner, EUiWheelOwnerPriority::PAGE, true);
	Ownership.Register(&InnerOwner, EUiWheelOwnerPriority::COMPOSITE_CONTROL, true);
	float WheelDelta = 0.0f;
	EXPECT_FALSE(Ownership.TryConsume(&OuterOwner, &WheelDelta));
	EXPECT_TRUE(Ownership.TryConsume(&InnerOwner, &WheelDelta));
	EXPECT_EQ(WheelDelta, 1.0f);

	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string LanguageSelection = FunctionBody(Source, "bool CMenus::RenderLanguageSelection(CUIRect MainView, const SSettingsContentMetrics *pMetrics)");
	ASSERT_FALSE(LanguageSelection.empty());
	EXPECT_NE(LanguageSelection.find("ScrollParams.m_WheelOwnerPriority = EUiWheelOwnerPriority::COMPOSITE_CONTROL;"), std::string::npos);
}

TEST(QmNewUiMenuRenderSettingsPagesContract, ControlsControllerCardUsesDynamicHeightPreLayout)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings_controls.cpp");
	const size_t CardStart = Source.find("AddCard(vCards, \"deck:controls-controller\"");
	ASSERT_NE(CardStart, std::string::npos);
	const size_t CardEnd = Source.find("const std::pair<EBindOptionGroup", CardStart);
	ASSERT_NE(CardEnd, std::string::npos);
	const std::string CardBody = Source.substr(CardStart, CardEnd - CardStart);
	EXPECT_NE(CardBody.find("ControllerMeasureRevision"), std::string::npos);
	EXPECT_NE(CardBody.find("m_PreLayoutInput"), std::string::npos);
	EXPECT_NE(CardBody.find("const bool WasJoystickEnabled"), std::string::npos);
	EXPECT_NE(CardBody.find("ResolveSettingsRadioRowLayout(Content, 2, Metrics)"), std::string::npos);
	EXPECT_NE(CardBody.find("m_vJoystickIngameModeButtonContainers"), std::string::npos);
	EXPECT_NE(CardBody.find("if(!WasAbsolute)"), std::string::npos);
}
