// 请抬头享受阳光｜日子很好 我很我---------致咩子
// Qm monitoring 源码合同：设置界面结构、卡片甲板与注册表导航。
// 文本计划/预热调度合同见 qmclient_monitoring_settings_plan_contract_test.cpp；
// 性能窗口遥测合同见 qmclient_monitoring_settings_perf_contract_test.cpp。
#define CONF_TEST 1

#include <game/client/QmUi/QmCardRegistry.h>

#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

TEST(QmMonitoringSettingsStructureContract, SettingsUiMigrationFinalStructureContract)
{
	const std::array<const char *, 11> apRetiredSymbols = {
		"RenderQmSettingsGlassCard",
		"BeginSettingsCardDeck",
		"RegisterSettingsCardDeckItemFromFrame",
		"LegacyTextFieldEx",
		"DoSettingsSliderInputField",
		"m_TClientSettingsCardDragState",
		"m_SettingsCardDeckOrders",
		"ForceShowScrollbar",
		"CachedHeightForStableCardId",
		"DrawTClientCacheSectionBox",
		"InsetTClientCacheSectionContent",
	};
	const std::filesystem::path ClientPath = TestSourcePath("src/game/client");
	for(const std::filesystem::directory_entry &Entry : std::filesystem::recursive_directory_iterator(ClientPath))
	{
		if(!Entry.is_regular_file() || (Entry.path().extension() != ".cpp" && Entry.path().extension() != ".h"))
			continue;
		std::ifstream File(Entry.path());
		ASSERT_TRUE(File.good()) << Entry.path().string();
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();
		for(const char *pSymbol : apRetiredSymbols)
			EXPECT_EQ(Source.find(pSymbol), std::string::npos) << Entry.path().string() << ": " << pSymbol;
	}

	const std::string ScrollHeader = ReadRepoFile("src/game/client/ui_scrollregion.h");
	EXPECT_EQ(CountSubstring(ScrollHeader, "CQmScrollState m_ScrollState;"), 1u);
	EXPECT_EQ(ScrollHeader.find("float m_ScrollPos;"), std::string::npos);
	EXPECT_EQ(ScrollHeader.find("float m_AnimTargetScrollPos;"), std::string::npos);

	const std::array<std::pair<const char *, const char *>, 4> aNonCardScopes = {{
		{"src/game/client/components/menus_browser.cpp", "void CMenus::RenderServerbrowserStatusBox(CUIRect StatusBox, bool WasListboxItemActivated)"},
		{"src/game/client/components/menus_browser.cpp", "void CMenus::RenderServerbrowserFilters(CUIRect View)"},
		{"src/game/client/components/menus_browser.cpp", "void CMenus::RenderServerbrowserFriends(CUIRect View)"},
		{"src/game/client/components/menus_settings.cpp", "bool CMenus::RenderLanguageSelection(CUIRect MainView, const SSettingsContentMetrics *pMetrics)"},
	}};
	for(const auto &[pPath, pSignature] : aNonCardScopes)
	{
		const std::string Body = ExtractSourceFunctionBody(ReadRepoFile(pPath), pSignature);
		ASSERT_FALSE(Body.empty()) << pSignature;
		EXPECT_EQ(Body.find("SettingsCard("), std::string::npos) << pSignature;
		EXPECT_EQ(Body.find("m_SettingsCardDeck.Render("), std::string::npos) << pSignature;
		EXPECT_EQ(Body.find("m_SettingsCardDeck.RenderCached("), std::string::npos) << pSignature;
	}
}

TEST(QmMonitoringSettingsStructureContract, SettingsCardDeckSkipsAnimationRuntimeOnStableFrames)
{
	const std::string Source = ReadRepoFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	const std::string CardSource = ReadRepoFile("src/game/client/QmUi/SettingsCard.cpp");

	EXPECT_NE(Source.find("ResolveSettingsCardAnimationWork("), std::string::npos);
	EXPECT_NE(Source.find("ResolveSettingsCardHeightAnimationWork("), std::string::npos);
	EXPECT_NE(Source.find("if(m_FrameRuntime.EntryWasActive() && Motion.m_EntryDuration > 0.0f)"), std::string::npos);
	EXPECT_NE(Source.find("State.m_DrawOffsetY = DeckEntryOffsetY;"), std::string::npos);
	EXPECT_NE(Source.find("else if(AnimationWork.m_ResolveReflow)"), std::string::npos);
	EXPECT_NE(Source.find("State.m_ClipContent = SettingsCardDeckShouldClipContent(Card.m_Frame.m_ContentRect.w > 0.0f && Card.m_Frame.m_ContentRect.h > 0.0f, Card.m_ContentHeightAnimationActive);"), std::string::npos);
	EXPECT_EQ(Source.find("State.m_ClipContent = ContentHeightAnimationActive;"), std::string::npos);
	EXPECT_NE(CardSource.find("const CUIRect ClipRect = ResolveSettingsCardContentClipRect(DrawFrame.m_ContentRect, DrawFrame.m_Rect, UiScale);"), std::string::npos);
	EXPECT_NE(CardSource.find("Ctx.m_pUi->ClipEnable(&ClipRect);"), std::string::npos);
}

TEST(QmMonitoringSettingsStructureContract, RenderOnlyNumericFieldsAndDropDownsDoNotMutateControlState)
{
	const std::string Forms = ReadRepoFile("src/game/client/QmUi/UiForms.cpp");
	const std::string Ui = ReadRepoFile("src/game/client/ui.cpp") + ReadRepoFile("src/game/client/ui_popups.cpp");
	const std::string IntegerField = ExtractSourceFunctionBody(Forms, "SInputFieldResult IntegerField(");
	const std::string NumericField = ExtractSourceFunctionBody(Forms, "bool NumericField(");
	const std::string DropDown = ExtractSourceFunctionBody(Ui, "int CUi::DoDropDown(CUIRect *pRect, int CurSelection, const char *const *pStrs, int Num, SDropDownState &State, const SDropDownProperties &DropDownProps)");
	ASSERT_FALSE(IntegerField.empty());
	ASSERT_FALSE(NumericField.empty());
	ASSERT_FALSE(DropDown.empty());
	EXPECT_NE(DropDown.find("m_DropDownFontSize > 0.0f ? m_DropDownFontSize"), std::string::npos);
	EXPECT_NE(DropDown.find("State.m_SelectionPopupContext.m_FontSize = ResolvedFontSize;"), std::string::npos);
	EXPECT_NE(DropDown.find("ButtonProps.m_FontSize = ResolvedFontSize;"), std::string::npos);

	const size_t IntegerRenderOnly = IntegerField.find("if(Ctx.m_pUi->RenderOnly())");
	const size_t IntegerWrite = IntegerField.find("*pValue = ClampedValue;");
	ASSERT_NE(IntegerRenderOnly, std::string::npos);
	ASSERT_NE(IntegerWrite, std::string::npos);
	EXPECT_LT(IntegerRenderOnly, IntegerWrite);

	const size_t NumericRenderOnly = NumericField.find("if(RenderOnly)");
	const size_t NumericStateWrite = NumericField.find("pState->m_LastSyncedStoredValue = *pValue;");
	const size_t NumericInputWrite = NumericField.find("pInput->SetInteger(DisplayValue);");
	ASSERT_NE(NumericRenderOnly, std::string::npos);
	ASSERT_NE(NumericStateWrite, std::string::npos);
	ASSERT_NE(NumericInputWrite, std::string::npos);
	EXPECT_LT(NumericRenderOnly, NumericStateWrite);
	EXPECT_LT(NumericRenderOnly, NumericInputWrite);

	const size_t DropDownRenderOnly = DropDown.find("if(RenderOnly())");
	const size_t DropDownStateInit = DropDown.find("if(!State.m_Init)");
	ASSERT_NE(DropDownRenderOnly, std::string::npos);
	ASSERT_NE(DropDownStateInit, std::string::npos);
	EXPECT_LT(DropDownRenderOnly, DropDownStateInit);
}

TEST(QmMonitoringSettingsStructureContract, RegistryNavigationBridgeOwnsSettingsTarget)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string SetPageBody = ExtractSourceFunctionBody(Source, "bool CMenus::SetSettingsPageFromCardTab(const char *pTab)");
	const std::string NavigateBody = ExtractSourceFunctionBody(Source, "void CMenus::NavigateToSettingsCard(const qm_card_registry::SCardNavigationTarget &Target)");
	ASSERT_FALSE(SetPageBody.empty());
	ASSERT_FALSE(NavigateBody.empty());

	EXPECT_NE(Header.find("bool SetSettingsPageFromCardTab(const char *pTab);"), std::string::npos);
	EXPECT_NE(Header.find("void NavigateToSettingsCard(const qm_card_registry::SCardNavigationTarget &Target);"), std::string::npos);
	EXPECT_NE(SetPageBody.find("str_comp(pTab, \"graphics\") == 0"), std::string::npos);
	EXPECT_NE(SetPageBody.find("str_comp(pTab, \"player\") == 0"), std::string::npos);
	EXPECT_NE(SetPageBody.find("str_comp(pTab, \"tee\") == 0"), std::string::npos);
	EXPECT_NE(SetPageBody.find("str_comp(pTab, \"tclient-chat-binds\") == 0"), std::string::npos);
	EXPECT_NE(SetPageBody.find("str_comp(pTab, \"tclient-warlist\") == 0"), std::string::npos);
	EXPECT_NE(SetPageBody.find("str_comp(pTab, \"tclient-info\") == 0"), std::string::npos);
	EXPECT_NE(SetPageBody.find("str_comp(pTab, \"tclient-profiles\") == 0"), std::string::npos);
	EXPECT_NE(SetPageBody.find("str_comp(pTab, \"tclient-configs\") == 0"), std::string::npos);
	EXPECT_NE(SetPageBody.find("g_Config.m_UiSettingsPage = SETTINGS_GRAPHICS;"), std::string::npos);
	EXPECT_NE(SetPageBody.find("str_comp(pTab, \"appearance-hud\") == 0"), std::string::npos);
	EXPECT_NE(SetPageBody.find("return false;"), std::string::npos);
	EXPECT_NE(NavigateBody.find("SetSettingsPageFromCardTab(Target.m_pTab)"), std::string::npos);
	EXPECT_NE(NavigateBody.find("m_SettingsCardDeck.RequestReveal(Target.m_pStableId);"), std::string::npos);
}

TEST(QmMonitoringSettingsStructureContract, PublicSettingsCardDeckCoordinatesCanonicalDefinitions)
{
	const std::string Header = ReadRepoFile("src/game/client/QmUi/SettingsCardDeck.h");
	const std::string Source = ReadRepoFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	const std::string LogicHeader = ReadRepoFile("src/game/client/QmUi/SettingsCardDeckLogic.h");

	EXPECT_NE(Header.find("struct SSettingsCardDefinition"), std::string::npos);
	EXPECT_NE(Header.find("struct SSettingsCardDeckInput"), std::string::npos);
	EXPECT_NE(Header.find("struct SSettingsCardDeckResult"), std::string::npos);
	EXPECT_NE(Header.find("class CSettingsCardDeck"), std::string::npos);
	EXPECT_NE(Header.find("SSettingsCardDeckResult Render("), std::string::npos);
	EXPECT_NE(Header.find("void RequestReveal("), std::string::npos);
	EXPECT_NE(Header.find("void BeginDisplayCycle(uint64_t DisplayCycle, bool AnimateEntry)"), std::string::npos);
	EXPECT_NE(Header.find("CProjectionCache m_ProjectionCache"), std::string::npos);
	EXPECT_NE(Source.find("m_ProjectionCache.Resolve("), std::string::npos);
	EXPECT_NE(Source.find("ApplySettingsCardDeckDragPlacement("), std::string::npos);
	EXPECT_NE(Source.find("ResolveSettingsCardDeckDropOrder("), std::string::npos);
	EXPECT_NE(Source.find("SettingsCardDeckAutoScrollDelta("), std::string::npos);
	EXPECT_NE(Source.find("CommitSettingsCardDeckDrop("), std::string::npos);
	EXPECT_NE(Source.find("SettingsCard("), std::string::npos);
	EXPECT_NE(Source.find("bool PointerInsideDrawFrame = false;"), std::string::npos);
	EXPECT_NE(Header.find("std::string m_PendingRevealStableId"), std::string::npos);
	EXPECT_EQ(Header.find("m_ReflowCompleteFeedbackRemaining"), std::string::npos);
	EXPECT_NE(Header.find("m_CollapsedInitialized"), std::string::npos);
	EXPECT_NE(Header.find("m_vContentHeights"), std::string::npos);
	EXPECT_NE(Header.find("m_MeasureEachFrame"), std::string::npos);
	EXPECT_NE(Header.find("m_MeasureRevision"), std::string::npos);
	EXPECT_NE(Header.find("m_RenderMeasured"), std::string::npos);
	EXPECT_NE(Source.find("SettingsCardEntryNodeKey"), std::string::npos);
	EXPECT_NE(Source.find("SettingsCardEntryNodeKey(pTab)"), std::string::npos);
	EXPECT_EQ(Source.find("SettingsCardEntryNodeKey(pTab, pStableId)"), std::string::npos);
	EXPECT_EQ(Source.find("SettingsCardEntryNodeKey(pTab, pStableId, m_DisplayCycle)"), std::string::npos);
	EXPECT_NE(Source.find("SettingsCardHeightNodeKey"), std::string::npos);
	EXPECT_NE(Source.find("SettingsCardReflowNodeKey"), std::string::npos);
	EXPECT_NE(Source.find("SettingsCardDeckAllowsDragStart(EntryPending, EntryPositionActive, ReflowTargetChanged, ReflowPositionActive || ContentHeightAnimationActive)"), std::string::npos);
	EXPECT_NE(Source.find("CSettingsCardColumnFramePlan"), std::string::npos);
	EXPECT_EQ(Source.find("CursorY = ResolveSettingsCardColumnFrame"), std::string::npos);
	EXPECT_NE(Source.find("SettingsCardDeckShouldSnapReflow(GeometryStateChanged, m_Drag.Active())"), std::string::npos);
	EXPECT_NE(Source.find("if(SnapReflow)"), std::string::npos);
	EXPECT_EQ(Source.find("|| m_Drag.Active() || ContentHeightTargetChanged"), std::string::npos);
	EXPECT_EQ(Source.find("SnapReflow = true;"), std::string::npos);
	EXPECT_EQ(Source.find("EUiAnimProperty::ALPHA"), std::string::npos);
	EXPECT_NE(Header.find("CSettingsCardDeckFrameRuntime m_FrameRuntime"), std::string::npos);
	EXPECT_NE(LogicHeader.find("uint64_t m_DisplayCycle"), std::string::npos);
	EXPECT_NE(Source.find("m_FrameRuntime.AnimateEntry() ? Motion.m_EntryDistance : 0.0f"), std::string::npos);
	EXPECT_NE(Source.find("m_SuppressHoverFeedbackOnce = true"), std::string::npos);
	EXPECT_NE(Source.find("State.m_HoverFeedbackEnabled = !m_SuppressHoverFeedbackOnce"), std::string::npos);
	EXPECT_NE(Source.find("std::abs(Input.m_MouseX - m_LastPointerX)"), std::string::npos);
	EXPECT_NE(Source.find("SettingsCard(Ctx, Card.m_Frame"), std::string::npos);
	EXPECT_NE(Source.find("Card.m_pDefinition->m_RenderMeasured"), std::string::npos);
	EXPECT_NE(Source.find("ResolveSettingsPageLayoutForScrollViewport"), std::string::npos);
	EXPECT_NE(Source.find("MouseInScrollViewport"), std::string::npos);
	EXPECT_NE(Source.find("Input.m_MouseX >= DrawLayout.m_aColumns[0].x"), std::string::npos);
	EXPECT_NE(Source.find("m_vContentHeights[StateIndex]"), std::string::npos);
	EXPECT_EQ(Source.find("m_vContentHeights.assign(Model.Count(), -1.0f)"), std::string::npos);
	EXPECT_NE(Header.find("m_vContentWidths"), std::string::npos);
	EXPECT_NE(Source.find("std::abs(CachedContentWidth - ContentWidth)"), std::string::npos);
	EXPECT_NE(Source.find("pDefinition->m_MeasureEachFrame"), std::string::npos);
	EXPECT_NE(Source.find("CachedMeasureRevision != pDefinition->m_MeasureRevision"), std::string::npos);
	EXPECT_NE(Source.find("SettingsCardDeckNeedsContentMeasure(Collapsed, pDefinition->m_MeasureEachFrame, CachedContentHeight)"), std::string::npos);
	EXPECT_NE(Source.find("for(const int StateIndex : m_vBoundDefinitionStateIndices)"), std::string::npos);
	const size_t ActiveSetStart = Source.find("auto RebuildActiveStateIndices = [&]() {");
	const size_t ActiveSetEnd = Source.find("};", ActiveSetStart);
	ASSERT_NE(ActiveSetStart, std::string::npos);
	ASSERT_NE(ActiveSetEnd, std::string::npos);
	EXPECT_EQ(Source.substr(ActiveSetStart, ActiveSetEnd - ActiveSetStart).find("StateIndexForStableId"), std::string::npos);
	EXPECT_NE(Source.find("SettingsCardDeckRendersContent(Collapsed) ? Card.m_pDefinition->m_Render : FSettingsCardRender{}"), std::string::npos);
	EXPECT_NE(Source.find("Model.Entry(Card.m_StateIndex).m_pStableId"), std::string::npos);
	EXPECT_EQ(Source.find("m_Drag.m_pStableId"), std::string::npos);

	const size_t AddRect = Source.find("pScrollRegion->AddRect(Card.m_Frame.m_Rect, Reveal)");
	const size_t Render = Source.find("SettingsCard(Ctx, Card.m_Frame");
	ASSERT_NE(AddRect, std::string::npos);
	ASSERT_NE(Render, std::string::npos);
	EXPECT_LT(AddRect, Render);

	const std::string CardSource = ReadRepoFile("src/game/client/QmUi/SettingsCard.cpp");
	EXPECT_NE(CardSource.find("HeaderAction(DrawFrame, DrawState.m_Collapsed)"), std::string::npos);
}

TEST(QmMonitoringSettingsStructureContract, SettingsLoadingPrewarmApiIsPublic)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const size_t StartLoading = Header.find("void StartLoading(int Total);");
	const size_t PrewarmSettingsPages = Header.find("void PrewarmSettingsPages();");
	const size_t IsInit = Header.find("bool IsInit() const");
	ASSERT_NE(StartLoading, std::string::npos);
	ASSERT_NE(PrewarmSettingsPages, std::string::npos);
	ASSERT_NE(IsInit, std::string::npos);

	EXPECT_GT(PrewarmSettingsPages, StartLoading);
	EXPECT_LT(PrewarmSettingsPages, IsInit);
}

TEST(QmMonitoringSettingsStructureContract, JumpHintSettingsCardExposesAllQmFields)
{
	const std::string Source = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderQmFunctionJumpHintContent(");
	ASSERT_FALSE(Body.empty());

	// Jump hint migrated from tc_jump_hint* to qm_jump_hint*. The settings page
	// must expose the whole qm_* surface, not just the enable toggle, otherwise
	// users can render the HUD element but cannot adjust its color, position, or
	// size without using console commands.
	EXPECT_NE(Body.find("RenderQmFunctionCheckbox(&g_Config.m_QmJumpHint"), std::string::npos);
	EXPECT_NE(Body.find("&g_Config.m_QmJumpHintColor"), std::string::npos);
	EXPECT_NE(Body.find("&g_Config.m_QmJumpHintX"), std::string::npos);
	EXPECT_NE(Body.find("&g_Config.m_QmJumpHintY"), std::string::npos);
	EXPECT_NE(Body.find("&g_Config.m_QmJumpHintSize"), std::string::npos);
	EXPECT_NE(Body.find("RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue"), std::string::npos);
	EXPECT_NE(Body.find("&s_QmJumpHintXInputId, &g_Config.m_QmJumpHintX"), std::string::npos);
	EXPECT_NE(Body.find("&s_QmJumpHintYInputId, &g_Config.m_QmJumpHintY"), std::string::npos);
	EXPECT_NE(Body.find("&s_QmJumpHintSizeInputId, &g_Config.m_QmJumpHintSize"), std::string::npos);
	EXPECT_EQ(Body.find("m_TcJumpHint"), std::string::npos);
}

TEST(QmMonitoringSettingsStructureContract, SettingsCardsKeepStableBackgroundPositionDuringPageSwitches)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Settings = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string RenderSettings = ExtractSourceFunctionBody(Settings, "void CMenus::RenderSettings(CUIRect MainView)");
	const std::string RenderAppearance = ExtractSourceFunctionBody(Settings, "void CMenus::RenderSettingsAppearance(CUIRect MainView)");

	EXPECT_NE(Menus.find("const bool ContentTransitionActive = TransitionActive && m_MenuPage != PAGE_SETTINGS;"), std::string::npos);
	EXPECT_NE(Menus.find("const bool ContentTransitionActive = TransitionActive && m_GamePage != PAGE_SETTINGS;"), std::string::npos);
	EXPECT_EQ(RenderSettings.find("settings_main_page_switch"), std::string::npos);
	EXPECT_EQ(RenderSettings.find("ApplyUiSwitchOffset(ContentView"), std::string::npos);
	EXPECT_NE(RenderSettings.find("m_SettingsPageSwitchActive = false;"), std::string::npos);
	EXPECT_EQ(RenderAppearance.find("settings_appearance_tab_switch"), std::string::npos);
	EXPECT_EQ(RenderAppearance.find("ApplyUiSwitchOffset(ContentView"), std::string::npos);
}
