// QmNewUi 菜单源码合同：设置卡片甲板状态与生命周期。
// 卡片视觉表面与共享样式合同见 qm_new_ui_menu_cards_surface_contract_test.cpp。
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

TEST(QmNewUiMenuCardsDeckContract, SettingsCardDeckResetsStateWhenDefinitionViewChanges)
{
	const std::string SettingsDeck = ReadTextFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	ASSERT_FALSE(SettingsDeck.empty());
	EXPECT_NE(SettingsDeck.find("void CSettingsCardDeck::ResetDefinitionViewState()"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("if(TabChanged || StableIdsChanged || ModelCountChanged || StateIndexChanged)"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("m_PreparedDefinitionStateIndexRevision != Model.StateIndexRevision()"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("m_vContentHeights.clear();"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("m_ProjectionCache = {};"), std::string::npos);
}

TEST(QmNewUiMenuCardsDeckContract, SettingsCardDeckPreLayoutUsesTheLastVisibleAnimatedFrame)
{
	const std::string SettingsDeck = ReadTextFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const std::string ButtonLogic = FunctionBody(UiSource, "int CUi::DoButtonLogic(");
	ASSERT_FALSE(SettingsDeck.empty());
	ASSERT_FALSE(ButtonLogic.empty());
	EXPECT_NE(SettingsDeck.find("const SSettingsCardFrame PreLayoutFrame = ResolveSettingsCardDrawFrame(Card.m_Frame, Runtime.m_LastDrawOffsetX, Runtime.m_LastDrawOffsetY);"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("m_PreLayoutHeaderInput(PreLayoutFrame, CollapsedBeforeHeader)"), std::string::npos);
	const size_t CardLoop = SettingsDeck.find("for(const SPreparedCard &Card : m_vPreparedCards)");
	const size_t ActiveHeaderContinuation = SettingsDeck.find("const bool HasActiveHeaderContinuation = SettingsCardDeckHasActiveItemContinuation", CardLoop);
	const size_t ActiveContentContinuation = SettingsDeck.find("const bool HasActiveContentContinuation = SettingsCardDeckHasActiveItemContinuation", ActiveHeaderContinuation);
	ASSERT_NE(CardLoop, std::string::npos);
	ASSERT_NE(ActiveHeaderContinuation, std::string::npos);
	ASSERT_NE(ActiveContentContinuation, std::string::npos);
	EXPECT_LT(CardLoop, ActiveHeaderContinuation);
	EXPECT_LT(ActiveHeaderContinuation, ActiveContentContinuation);
	EXPECT_NE(SettingsDeck.find("(ControllerVisible || HasActiveHeaderContinuation) && Card.m_pDefinition->m_PreLayoutHeaderInput"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("SettingsCardDeckShouldRunPreLayoutInput(HasPointerInput, HasPendingPreLayoutInput, HasActiveContentContinuation"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("SettingsCardDeckUsesDefaultCollapseControl(HasCustomCollapsedState, static_cast<bool>(Card.m_pDefinition->m_PreLayoutHeaderInput))"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("Card.m_pDefinition->m_HeaderAction"), std::string::npos);
	// 自定义折叠状态的卡片改由 m_IsCollapsed 解析，公共按钮与回调都不再改写它。
	EXPECT_NE(SettingsDeck.find("SettingsCardDeckResolveCollapsed(HasCustomCollapsedState, HasCustomCollapsedState && Card.m_pDefinition->m_IsCollapsed(), Runtime.m_DefaultCollapsed)"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("m_PreLayoutInput(PreLayoutFrame.m_ContentRect)"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("Runtime.m_LastDrawOffsetY = State.m_DrawOffsetY;"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("if(PreLayoutInput() && Inside && !IsPopupOpen())"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("m_pHotItem = pId;"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("m_pBecomingHotItem = pId;"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("PreLayoutCurrentFramePress && MouseButtonClicked(Button)"), std::string::npos);
	EXPECT_LT(ButtonLogic.find("m_pHotItem = pId;"), ButtonLogic.find("SetActiveItem(pId);"));
}

TEST(QmNewUiMenuCardsDeckContract, DeckPreLayoutPressClearsStaleActiveInput)
{
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const size_t PreLayoutPress = UiSource.find("if(PreLayoutInput() && Inside && !IsPopupOpen())");
	ASSERT_NE(PreLayoutPress, std::string::npos);
	const size_t PreLayoutPressEnd = UiSource.find("int CUi::DoDraggableButtonLogic", PreLayoutPress);
	ASSERT_NE(PreLayoutPressEnd, std::string::npos);
	const std::string ButtonLogic = UiSource.substr(PreLayoutPress, PreLayoutPressEnd - PreLayoutPress);
	EXPECT_NE(ButtonLogic.find("CLineInput::GetActiveInput()"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("pActiveInput->Deactivate()"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("m_pLastActiveItem = nullptr"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("SetActiveItem(nullptr)"), std::string::npos);
}

TEST(QmNewUiMenuCardsDeckContract, SettingsDisplayCycleUpdatesAfterTabInputBeforePageRender)
{
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string DeckSource = ReadTextFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	const std::string RenderSettings = FunctionBody(SettingsSource, "void CMenus::RenderSettings(CUIRect MainView)");
	ASSERT_FALSE(RenderSettings.empty());
	const size_t TabInput = RenderSettings.find("DoButton_MenuTab(&m_aSettingsTabButtons[i]");
	const size_t DisplayCycle = RenderSettings.find("m_SettingsCardDeck.BeginDisplayCycle(");
	const size_t PageRender = RenderSettings.find("RenderSettingsGeneral(ContentView)");
	ASSERT_NE(TabInput, std::string::npos);
	ASSERT_NE(DisplayCycle, std::string::npos);
	ASSERT_NE(PageRender, std::string::npos);
	EXPECT_LT(TabInput, DisplayCycle);
	EXPECT_LT(DisplayCycle, PageRender);
	EXPECT_EQ(RenderSettings.find("Accent.VSplitLeft(3.0f"), std::string::npos);
	EXPECT_NE(DeckSource.find("const bool TabChanged = m_LastRenderedTab != pTab;"), std::string::npos);
	EXPECT_NE(DeckSource.find("if(TabChanged || StableIdsChanged || ModelCountChanged || StateIndexChanged)"), std::string::npos);
	EXPECT_NE(DeckSource.find("m_SuppressHoverFeedbackOnce = true;"), std::string::npos);
}

TEST(QmNewUiMenuCardsDeckContract, TClientSettingsCardsUseSharedQmCardStyle)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string HeaderSource = ReadTextFile("src/game/client/components/menus.h");
	const std::string RenderSettingsTClientSettings = FunctionBody(Source, "void CMenus::RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly)");
	const std::string RenderSettingsTClientChatBinds = FunctionBody(Source, "void CMenus::RenderSettingsTClientChatBinds(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(RenderSettingsTClientSettings.empty());
	ASSERT_FALSE(RenderSettingsTClientChatBinds.empty());

	EXPECT_EQ(Source.find("RenderQmSettingsGlassCard(TClientCacheSectionBoxRect(BoxRect), QmSettingsCardStyle(1.0f));"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientSettings.find("m_SettingsCardDeck.RenderCached(SettingsUiContext(\"settings_tclient_main\""), std::string::npos);
	EXPECT_NE(RenderSettingsTClientChatBinds.find("SettingsPageLayout(MainView, UiScale);"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientChatBinds.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientChatBinds.find("BeginSettingsScrollRegion("), std::string::npos);
	EXPECT_EQ(Source.find("ScrollParams.m_ScrollUnit = 60.0f;"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientSettings.find("ScrollParams.m_ScrollbarMargin = 5.0f;"), std::string::npos);
	EXPECT_EQ(Source.find("BoxRect.Draw(Ui()->ScaleBackgroundAlpha(MenuPanelColor(0.92f))"), std::string::npos);
	EXPECT_EQ(HeaderSource.find("void DrawTClientCacheSectionBox(CUIRect BoxRect);"), std::string::npos);
}

TEST(QmNewUiMenuCardsDeckContract, TClientInputCardSharesLayoutForMeasureAndRender)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	// 卡片测量和两种渲染入口共用布局函数；行交互不由源码合同证明。
	EXPECT_NE(Source.find("S.m_MeasureFn = [&LayoutInputSection]"), std::string::npos);
	EXPECT_NE(Source.find("S.m_RenderCompactFn = [&LayoutInputSection"), std::string::npos);
	EXPECT_NE(Source.find("S.m_RenderFullFn = [&LayoutInputSection"), std::string::npos);
}

TEST(QmNewUiMenuCardsDeckContract, DDNetSettingsPageUsesSharedQmCards)
{
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string RenderSettingsDDNet = FunctionBody(SettingsSource, "void CMenus::RenderSettingsDDNet(CUIRect MainView)");
	ASSERT_FALSE(RenderSettingsDDNet.empty());

	EXPECT_NE(RenderSettingsDDNet.find("const SSettingsPageLayoutFrame DDNetPage = SettingsPageLayout(MainView, UiScale);"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("const IUiContext DDNetCardCtx = SettingsUiContext(\"settings_ddnet\", UiScale);"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("static CScrollRegion s_DDNetSettingsCardScrollRegion;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy("), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("SettingsCardDeckForRenderPass().RenderCached(DDNetCardCtx, DDNetPage, \"ddnet\""), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(DemoSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("const bool ReplaysLayout = g_Config.m_ClReplays != 0;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("const bool RaceGhostLayout = g_Config.m_ClRaceGhost != 0;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("const bool RaceSaveGhostLayout = g_Config.m_ClRaceSaveGhost != 0;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("ResolveDDNetDemoRows(ReplaysLayout, RaceGhostLayout, RaceSaveGhostLayout)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("vCards.back().m_Measure = [DDNetRowPitch]"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("ResolveDDNetDemoRows(g_Config.m_ClReplays != 0, g_Config.m_ClRaceGhost != 0, g_Config.m_ClRaceSaveGhost != 0)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("vCards.back().m_PreLayoutInput = ProcessDemoPreLayoutInput;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("DDNetLayoutRevision = DDNetLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClReplays != 0);"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("if(g_Config.m_ClRaceGhost)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("if(g_Config.m_ClRaceSaveGhost)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(GameplaySpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("const bool TextEntitiesLayout = g_Config.m_ClTextEntities != 0;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("const bool AntiPingLayout = g_Config.m_ClAntiPing != 0;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("ResolveDDNetGameplayRows(TextEntitiesLayout, AntiPingLayout)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("vCards.back().m_PreLayoutInput = ProcessGameplayPreLayoutInput;"), std::string::npos);
	const std::string GameplayPreLayout = BlockBodyAfter(RenderSettingsDDNet, "const auto ProcessGameplayPreLayoutInput");
	ASSERT_FALSE(GameplayPreLayout.empty());
	EXPECT_NE(GameplayPreLayout.find("SplitDDNetRow(Gameplay, &GameplayRow);"), std::string::npos);
	EXPECT_NE(GameplayPreLayout.find("GameplayRow.VSplitLeft(std::clamp(GameplayRow.w * 0.38f"), std::string::npos);
	EXPECT_NE(GameplayPreLayout.find("Ui()->DoButtonLogic(&g_Config.m_ClTextEntities"), std::string::npos);
	EXPECT_NE(GameplayPreLayout.find("g_Config.m_ClTextEntities ^= 1;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("((uint64_t)(g_Config.m_ClTextEntities != 0) << 0)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("if(g_Config.m_ClTextEntities)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("if(g_Config.m_ClAntiPing)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("CUIRect GameplayRow;"), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("Gameplay.VSplitMid(&Left, &Right, 20.0f);"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(BackgroundSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(MiscellaneousSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("SaveSettingsCardOrderModel();"), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("EndSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("s_PrevDDNetSettingsScrollY"), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("MainView.HSplitTop(130.0f, &Demo, &MainView);"), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("MainView.HSplitTop(GameplayHeight, &Gameplay, &MainView);"), std::string::npos);
}

TEST(QmNewUiMenuCardsDeckContract, SettingsCardFeedbackFixesUseStableLayouts)
{
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string RenderSettingsDDNet = FunctionBody(SettingsSource, "void CMenus::RenderSettingsDDNet(CUIRect MainView)");
	ASSERT_FALSE(RenderSettingsDDNet.empty());
	EXPECT_NE(RenderSettingsDDNet.find("SettingsPageLayout("), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(DemoSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(GameplaySpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(BackgroundSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(MiscellaneousSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("SettingsCardDeckForRenderPass().RenderCached(DDNetCardCtx, DDNetPage, \"ddnet\""), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("BeginDDNetCard(MainView"), std::string::npos);

	const std::string NamePlateBranch = BlockBodyAfter(SettingsSource, "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_NAME_PLATE)");
	ASSERT_FALSE(NamePlateBranch.empty());
	EXPECT_EQ(NamePlateBranch.find("BeginSettingsScrollRegion(s_NameplateTextCardScrollRegion"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("FinishSettingsScrollRegion(s_NameplateTextCardScrollRegion"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("Localize(\"Nameplate text\")"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("NameplateTextExpandedMinHeight"), std::string::npos);

	const std::string QmSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	EXPECT_EQ(QmSource.find("RenderNameplateTextSettings(CardContent);"), std::string::npos);
}

TEST(QmNewUiMenuCardsDeckContract, TClientSettingsTabsPreserveHiddenStateAndVisibleCorners)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string RenderSettingsTClient = FunctionBody(Source, "void CMenus::RenderSettingsTClient(CUIRect MainView, bool PrewarmOnly)");
	const std::string RenderSettingsTClientInfo = FunctionBody(Source, "void CMenus::RenderSettingsTClientInfo(CUIRect MainView, bool PrewarmOnly)");

	EXPECT_NE(RenderSettingsTClient.find("if(TabCount <= 0)"), std::string::npos);
	EXPECT_NE(RenderSettingsTClient.find("FirstVisibleTab"), std::string::npos);
	EXPECT_NE(RenderSettingsTClient.find("VisibleTabIndex"), std::string::npos);
	EXPECT_NE(RenderSettingsTClient.find("VisibleTabIndex == 0"), std::string::npos);
	EXPECT_NE(RenderSettingsTClient.find("VisibleTabIndex == TabCount - 1"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientInfo.find("s_aShowTabs[i] = IsFlagSet(g_Config.m_TcTClientSettingsTabs, i);"), std::string::npos);
}

TEST(QmNewUiMenuCardsDeckContract, TClientInfoUsesPublicCardDeck)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Registry = ReadTextFile("src/game/client/QmUi/QmCardRegistry.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsTClientInfo(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
	EXPECT_NE(Body.find("SettingsPageLayout(MainView, UiScale);"), std::string::npos);
	EXPECT_NE(Body.find("CSettingsCardDeck &CardDeck = ReadOnly ? s_InfoPrewarmDeck : m_SettingsCardDeck;"), std::string::npos);
	EXPECT_NE(Body.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_EQ(Body.find("MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-info-links\", \"tclient-info\", ECardColumn::Left, 0"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-info-files\", \"tclient-info\", ECardColumn::Left, 1"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-info-developers\", \"tclient-info\", ECardColumn::Right, 0"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-info-tabs\", \"tclient-info\", ECardColumn::Right, 1"), std::string::npos);
}

TEST(QmNewUiMenuCardsDeckContract, TClientProfilesUsesPublicCardDeck)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Registry = ReadTextFile("src/game/client/QmUi/QmCardRegistry.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsTClientProfiles(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
	EXPECT_NE(Body.find("SettingsPageLayout(MainView, UiScale);"), std::string::npos);
	EXPECT_NE(Body.find("ProfilesListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);"), std::string::npos);
	EXPECT_NE(Body.find("CSettingsCardDeck &CardDeck = ReadOnly ? s_ProfilesPrewarmDeck : m_SettingsCardDeck;"), std::string::npos);
	EXPECT_NE(Body.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_NE(Body.find("const float ProfileActionsHeight = s_AllowDelete ? ProfileMetrics.m_ButtonHeight * 5.0f"), std::string::npos);
	EXPECT_NE(Body.find("Rect.VSplitLeft(ProfileMetrics.m_LineSpacing, nullptr, &Rect);"), std::string::npos);
	EXPECT_NE(Body.find("DrawRoundedSurface(Ui(), Skin, ColorRGBA(1.0f, 1.0f, 1.0f, 0.035f)"), std::string::npos);
	EXPECT_NE(Body.find("Skin.VMargin(std::max(0.0f, (Skin.w - PreviewRowWidth) * 0.5f), &Skin);"), std::string::npos);
	EXPECT_NE(Body.find("ResolveSettingsInlineRowMinimumWidth(ProfileMetrics.m_LabelWidth"), std::string::npos);
	EXPECT_NE(Body.find("ProfileMetrics.m_ListRowHeight"), std::string::npos);
	EXPECT_NE(Body.find("static_cast<uint64_t>(s_AllowDelete != 0) << 1"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-profiles-actions\", \"tclient-profiles\", ECardColumn::Left, 0"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-profiles-options\", \"tclient-profiles\", ECardColumn::Right, 0"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-profiles-list\", \"tclient-profiles\", ECardColumn::Left, 1"), std::string::npos);
	EXPECT_NE(Body.find("CTClientSettingsRowAllocator IdentityRows(View);"), std::string::npos);
	EXPECT_NE(Body.find("Row = IdentityRows.Next();"), std::string::npos);
}

TEST(QmNewUiMenuCardsDeckContract, TClientConfigsUsesPublicCardDeck)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Registry = ReadTextFile("src/game/client/QmUi/QmCardRegistry.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsTClientConfigs(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
	EXPECT_NE(Body.find("CSettingsCardDeck &CardDeck = ReadOnly ? s_ConfigsPrewarmDeck : m_SettingsCardDeck;"), std::string::npos);
	EXPECT_NE(Body.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_NE(Body.find("static CScrollRegion s_ConfigListScrollRegion;"), std::string::npos);
	EXPECT_NE(Body.find("ConfigListScrollRequest.m_Profile = EQmScrollProfile::SETTINGS_INNER;"), std::string::npos);
	EXPECT_EQ(Body.find("QmSettingsScrollRegionParams(1.0f)"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-configs-actions\", \"tclient-configs\", ECardColumn::Full, 0"), std::string::npos);
	EXPECT_EQ(Registry.find("deck:tclient-configs-filters"), std::string::npos);
	EXPECT_EQ(Registry.find("deck:tclient-configs-list"), std::string::npos);
	EXPECT_NE(Body.find("ResolveSettingsInlineRowMinimumWidth("), std::string::npos);
	EXPECT_EQ(Body.find("ContentWidth < 760.0f"), std::string::npos);
}

TEST(QmNewUiMenuCardsDeckContract, QmClientDecksIsolateRenderOnlyState)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	for(const char *pSignature : {
		    "void CMenus::RenderSettingsQmClientHudDeck(CUIRect MainView, bool PrewarmOnly)",
		    "void CMenus::RenderSettingsQmClientFunctionDeck(CUIRect MainView, bool PrewarmOnly)",
		    "void CMenus::RenderSettingsQmClientVisualDeck(CUIRect MainView, bool PrewarmOnly)",
		    "void CMenus::RenderSettingsQmClientContributors(CUIRect MainView, bool PrewarmOnly)",
	    })
	{
		const std::string Body = FunctionBody(Source, pSignature);
		ASSERT_FALSE(Body.empty());
		EXPECT_NE(Body.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
		EXPECT_NE(Body.find("CSettingsCardDeck &CardDeck = ReadOnly ?"), std::string::npos);
		EXPECT_NE(Body.find("qm_card_order::CModel &CardOrderModel = ReadOnly ?"), std::string::npos);
		EXPECT_NE(Body.find("InputState.m_AllowHeaderDrag = !ReadOnly;"), std::string::npos);
		EXPECT_NE(Body.find("ReadOnly ? nullptr : &"), std::string::npos);
		EXPECT_NE(Body.find("if(!ReadOnly && DeckResult.m_OrderChanged)"), std::string::npos);
	}
}
