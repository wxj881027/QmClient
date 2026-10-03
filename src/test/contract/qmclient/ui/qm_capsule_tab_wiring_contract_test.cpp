#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>

#include <array>
#include <fstream>
#include <sstream>
#include <string>

TEST(QmNewUiMenuBranches, CapsuleTabBarChromeDrawsContainerThenSpringIndicatorUnderLabels)
{
	// 意图：滑块胶囊必须由容器的同一入口画在文字之前，并且由弹簧驱动、可被打断续接，
	// 否则切换 Tab 时会瞬移或盖住经过的页签文字。
	const std::string Source = ReadTextFile("src/game/client/QmUi/UiNavigation.cpp");
	const std::string Header = ReadTextFile("src/game/client/QmUi/UiNavigation.h");
	ASSERT_NE(Source.find("void CapsuleTabBarChrome("), std::string::npos);

	// ui_widget::TabBar（UiDogfood 调试页）同样走胶囊，不再保留下划线小块。
	const std::string DogfoodTabBar = FunctionBody(Source, "int TabBar(");
	ASSERT_FALSE(DogfoodTabBar.empty());
	EXPECT_EQ(DogfoodTabBar.find("Underline"), std::string::npos);
	EXPECT_EQ(DogfoodTabBar.find("Indicator.Draw("), std::string::npos);
	EXPECT_NE(DogfoodTabBar.find("std::vector<CUIRect> vTabSlots(static_cast<std::size_t>(Count));"), std::string::npos);
	EXPECT_LT(DogfoodTabBar.find("CapsuleTabBarChrome(Ctx, BuildUiAnimNodeKey(MakeUiScopeHash(\"ui_widget_tabbar_capsule\")"), DogfoodTabBar.find("DoButton_MenuTab(&ButtonPool[i], ppLabels[i], Checked, &vTabSlots[static_cast<std::size_t>(i)]"));

	EXPECT_NE(Source.find("if(Ctx.m_pUi->RenderOnly())\n\t\t\treturn;"), std::string::npos);
	const size_t CapsuleDraw = Source.find("DrawRoundedSurface(Ctx, Capsule, Style.m_CapsuleColor, ColorRGBA(), ui_token::radius::PILL);");
	const size_t IndicatorDraw = Source.find("DrawRoundedSurface(Ctx, Indicator, Style.m_IndicatorColor, ColorRGBA(), ui_token::radius::PILL);");
	ASSERT_NE(CapsuleDraw, std::string::npos);
	ASSERT_NE(IndicatorDraw, std::string::npos);
	EXPECT_LT(CapsuleDraw, IndicatorDraw);
	EXPECT_NE(Header.find("inline CUIRect CapsuleTabBarRowRect(const CUIRect *pSlots, int Count)"), std::string::npos);
	// 配色自适应由 QmUi 统一提供，各 Tabbar 只传自己的容器表面色。
	EXPECT_NE(Header.find("inline bool CapsuleTabBarSurfaceIsLight(const ColorRGBA &SurfaceColor)"), std::string::npos);
	EXPECT_NE(Header.find("inline ColorRGBA CapsuleTabBarIndicatorColor(const ColorRGBA &SurfaceColor)"), std::string::npos);
	EXPECT_NE(Header.find("inline ColorRGBA CapsuleTabBarActiveLabelColor(const ColorRGBA &SurfaceColor)"), std::string::npos);
	EXPECT_NE(Header.find("inline ColorRGBA CapsuleTabBarInactiveLabelColor(const ColorRGBA &SurfaceColor)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsSubTabRowsUseCapsuleTabBar)
{
	// 意图：设置页各子 Tab 行（外观 / Assets / TClient / QmClient）统一走
	// 「槽位预布局 → 容器与滑块 → 页签文字」。
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string MenusHeader = ReadTextFile("src/game/client/components/menus.h");
	const std::string Settings = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Assets = ReadTextFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string TClient = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string QmClient = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");

	// 公共零件：绘制上下文与设置页胶囊配色。
	EXPECT_NE(MenusHeader.find("IUiContext TabBarUiContext() const;"), std::string::npos);
	EXPECT_NE(MenusHeader.find("ui_widget::SCapsuleTabBarStyle SettingsCapsuleTabBarStyle() const;"), std::string::npos);
	EXPECT_NE(MenusSource.find("IUiContext CMenus::TabBarUiContext() const"), std::string::npos);
	EXPECT_NE(MenusSource.find("ui_widget::SCapsuleTabBarStyle CMenus::SettingsCapsuleTabBarStyle() const"), std::string::npos);

	const std::string RenderAppearance = FunctionBody(Settings, "void CMenus::RenderSettingsAppearance(CUIRect MainView)");
	ASSERT_FALSE(RenderAppearance.empty());
	const size_t AppearanceGrid = RenderAppearance.find("AppearanceTabsRemainder.VSplitLeft(TabWidth, &aAppearanceTabSlots[Tab], &AppearanceTabsRemainder);");
	const size_t AppearanceChrome = RenderAppearance.find("ui_widget::CapsuleTabBarChrome(AppearanceTabBarCtx, MakeUiScopeHash(\"settings_appearance_tabs_capsule\")");
	const size_t AppearanceDraw = RenderAppearance.find("DoButton_MenuTab(&s_aPageTabs[Tab], s_apAppearanceTabNames[Tab], m_AppearanceSettingsTab == Tab, &aAppearanceTabSlots[Tab]");
	ASSERT_NE(AppearanceGrid, std::string::npos);
	ASSERT_NE(AppearanceChrome, std::string::npos);
	ASSERT_NE(AppearanceDraw, std::string::npos);
	EXPECT_LT(AppearanceGrid, AppearanceChrome);
	EXPECT_LT(AppearanceChrome, AppearanceDraw);
	EXPECT_NE(RenderAppearance.find("nullptr, nullptr, -1.0f, true))"), std::string::npos);

	const std::string RenderAssets = FunctionBody(Assets, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(RenderAssets.empty());
	const size_t AssetsGrid = RenderAssets.find("AssetsTabsRemainder.VSplitLeft(TabWidth, &aAssetsTabSlots[Tab], &AssetsTabsRemainder);");
	const size_t AssetsChrome = RenderAssets.find("ui_widget::CapsuleTabBarChrome(AssetsTabBarCtx, MakeUiScopeHash(\"settings_assets_tabs_capsule\")");
	const size_t AssetsDraw = RenderAssets.find("DoButton_MenuTab(&s_aPageTabs[Tab], s_apAssetsTabNames[Tab], s_CurCustomTab == Tab, &aAssetsTabSlots[Tab]");
	ASSERT_NE(AssetsGrid, std::string::npos);
	ASSERT_NE(AssetsChrome, std::string::npos);
	ASSERT_NE(AssetsDraw, std::string::npos);
	EXPECT_LT(AssetsGrid, AssetsChrome);
	EXPECT_LT(AssetsChrome, AssetsDraw);

	const std::string RenderTClient = FunctionBody(TClient, "void CMenus::RenderSettingsTClient(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(RenderTClient.empty());
	const size_t TClientGrid = RenderTClient.find("TabsRemainder.VSplitLeft(TabWidth, &aTClientTabSlots[NumTClientTabs], &TabsRemainder);");
	const size_t TClientChrome = RenderTClient.find("ui_widget::CapsuleTabBarChrome(TClientTabBarCtx, MakeUiScopeHash(\"settings_tclient_tabs_capsule\")");
	const size_t TClientDraw = RenderTClient.find("DoButton_MenuTab(&s_aPageTabs[Tab], s_apTClientTabNames[Tab], ActiveTab == Tab, &aTClientTabSlots[TabIndex]");
	ASSERT_NE(TClientGrid, std::string::npos);
	ASSERT_NE(TClientChrome, std::string::npos);
	ASSERT_NE(TClientDraw, std::string::npos);
	EXPECT_LT(TClientGrid, TClientChrome);
	EXPECT_LT(TClientChrome, TClientDraw);

	const std::string RenderQmClient = FunctionBody(QmClient, "void CMenus::RenderSettingsQmClientContent(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(RenderQmClient.empty());
	const size_t QmGrid = RenderQmClient.find("QmTabsRemainder.VSplitLeft(TabWidth, &aQmTabSlots[Tab], &QmTabsRemainder);");
	const size_t QmChrome = RenderQmClient.find("ui_widget::CapsuleTabBarChrome(QmTabBarCtx, MakeUiScopeHash(\"settings_qmclient_tabs_capsule\")");
	const size_t QmDraw = RenderQmClient.find("DoButton_MenuTab(&s_aPageTabs[PageTab], apQmTabNames[PageTab], m_QmClientSettingsTab == PageTab, &aQmTabSlots[Tab]");
	ASSERT_NE(QmGrid, std::string::npos);
	ASSERT_NE(QmChrome, std::string::npos);
	ASSERT_NE(QmDraw, std::string::npos);
	EXPECT_LT(QmGrid, QmChrome);
	EXPECT_LT(QmChrome, QmDraw);
	EXPECT_LT(QmDraw, RenderQmClient.find("LogQmPerfStage(Client(), \"tabbar\", StageTimer.ElapsedMs(), false, aTabExtra);"));

	// 玩家/Dummy 行同样先画胶囊再画文字。
	const std::string RenderPlayer = FunctionBody(Settings, "void CMenus::RenderSettingsPlayer(CUIRect MainView)");
	ASSERT_FALSE(RenderPlayer.empty());
	const size_t PlayerChrome = RenderPlayer.find("ui_widget::CapsuleTabBarChrome(TabBarUiContext(), MakeUiScopeHash(\"settings_player_dummy_tabs_capsule\"), aPlayerTabSlots, std::size(aPlayerTabSlots), m_Dummy ? 1 : 0, SettingsCapsuleTabBarStyle());");
	const size_t PlayerDraw = RenderPlayer.find("if(DoButton_MenuTab(&s_PlayerTabButton, Localize(\"Player\"), !m_Dummy, &PlayerTab, IGraphics::CORNER_ALL");
	ASSERT_NE(PlayerChrome, std::string::npos);
	ASSERT_NE(PlayerDraw, std::string::npos);
	EXPECT_LT(PlayerChrome, PlayerDraw);
}

TEST(QmNewUiMenuBranches, ServerBrowserToolboxUsesCapsuleTabBar)
{
	// 意图：服务器浏览器工具箱页签（过滤器 / 信息 / 好友）同样使用胶囊。
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderServerbrowserTabBar(CUIRect TabBar)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("#include <game/client/QmUi/UiNavigation.h>"), std::string::npos);
	const size_t Chrome = Body.find("ui_widget::CapsuleTabBarChrome(ToolboxTabBarCtx, MakeUiScopeHash(\"browser_toolbox_tabs_capsule\")");
	const size_t Draw = Body.find("if(DoButton_MenuTab(&s_FilterTabButton, FONT_ICON_LIST_UL, g_Config.m_UiToolboxPage == UI_TOOLBOX_PAGE_FILTERS, &FilterTabButton, IGraphics::CORNER_ALL, &m_aAnimatorsSmallPage[SMALL_TAB_BROWSER_FILTER], nullptr, nullptr, nullptr, 10.0f, nullptr, nullptr, -1.0f, true))");
	ASSERT_NE(Chrome, std::string::npos);
	ASSERT_NE(Draw, std::string::npos);
	EXPECT_LT(Chrome, Draw);
	EXPECT_NE(Body.find("CapsuleTabBarStyleFor(BrowserPanelColor(1.0f))"), std::string::npos);
	EXPECT_EQ(Source.find("UI_TOOLBOX_PAGE_QM"), std::string::npos);
	EXPECT_EQ(Source.find("RenderServerbrowserQm"), std::string::npos);

	// 通用配色零件：轨道压暗 + 滑块/文字自适应。
	EXPECT_NE(ReadTextFile("src/game/client/components/menus.h").find("ui_widget::SCapsuleTabBarStyle CapsuleTabBarStyleFor(const ColorRGBA &SurfaceColor) const;"), std::string::npos);
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	EXPECT_NE(MenusSource.find("ui_widget::SCapsuleTabBarStyle CMenus::CapsuleTabBarStyleFor(const ColorRGBA &SurfaceColor) const"), std::string::npos);
	EXPECT_NE(MenusSource.find("return CapsuleTabBarStyleFor(SettingsTabbarColor());"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ServerControlTabsUseCapsuleTabBarInNewUi)
{
	// 意图：游戏中"服务器控制"页的三个页签（改设置 / 踢人 / 移到观察者）
	// 同样先画胶囊容器与滑块，再画页签文字。
	const std::string Source = ReadTextFile("src/game/client/components/menus_ingame.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderServerControl(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("#include <game/client/QmUi/UiNavigation.h>"), std::string::npos);
	const size_t Chrome = Body.find("ui_widget::CapsuleTabBarChrome(TabBarUiContext(), MakeUiScopeHash(\"ingame_server_control_tabs_capsule\"), aControlTabSlots, 3, ActiveControlTab, CapsuleTabBarStyleFor(ms_ColorTabbarActive));");
	const size_t Draw = Body.find("if(DoButton_MenuTab(&s_Button0, Localize(\"Change settings\"), s_ControlPage == EServerControlTab::SETTINGS, &aControlTabSlots[0], IGraphics::CORNER_ALL");
	ASSERT_NE(Chrome, std::string::npos);
	ASSERT_NE(Draw, std::string::npos);
	EXPECT_LT(Chrome, Draw);
	EXPECT_NE(Body.find("ControlTabsRemainder.VSplitLeft(ControlTabsRemainder.w / 3.0f, &aControlTabSlots[0], &ControlTabsRemainder);"), std::string::npos);
	EXPECT_NE(Body.find("ControlTabsRemainder.VSplitMid(&aControlTabSlots[1], &aControlTabSlots[2]);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, Tee7SubTabsUseCapsuleTabBar)
{
	// 意图：Tee7 皮肤的「玩家/Dummy」「Basic/Custom」「皮肤部位」三行子 Tab 同样
	// 先画胶囊容器与滑块、再画文字。
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings7.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsTee7Content(CUIRect MainView, const SSettingsContentMetrics &Metrics)");
	ASSERT_FALSE(Body.empty());

	// 三个锚点在整份源码里各只出现一次，直接按文件位置比较先后。
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiNavigation.h>"), std::string::npos);
	const size_t PlayerDummyChrome = Source.find("ui_widget::CapsuleTabBarChrome(TabBarUiContext(), MakeUiScopeHash(\"settings_tee7_player_dummy_tabs_capsule\"), aPlayerDummySlots, std::size(aPlayerDummySlots), m_Dummy ? 1 : 0, SettingsCapsuleTabBarStyle());");
	const size_t PlayerDummyDraw = Source.find("if(DoButton_MenuTab(&s_PlayerTabButton, Localize(\"Player\"), !m_Dummy, &LeftTab, IGraphics::CORNER_ALL");
	const size_t ModeChrome = Source.find("ui_widget::CapsuleTabBarChrome(TabBarUiContext(), MakeUiScopeHash(\"settings_tee7_mode_tabs_capsule\"), aModeTabSlots, std::size(aModeTabSlots), m_CustomSkinMenu ? 1 : 0, SettingsCapsuleTabBarStyle());");
	const size_t ModeDraw = Source.find("ClickedBasicTab = DoButton_MenuTab(&s_BasicTabButton");
	const size_t SkinPartChrome = Source.find("ui_widget::CapsuleTabBarChrome(TabBarUiContext(), MakeUiScopeHash(\"settings_tee7_skin_part_tabs_capsule\"), aSkinPartSlots, protocol7::NUM_SKINPARTS, ActiveSkinPart, SettingsCapsuleTabBarStyle());");
	const size_t SkinPartDraw = Source.find("if(DoButton_MenuTab(&s_aSkinPartButtons[i], Localize(CSkins7::ms_apSkinPartNamesLocalized[i], \"skins\"), m_TeePartSelected == i, &aSkinPartSlots[i], IGraphics::CORNER_ALL");
	ASSERT_NE(PlayerDummyChrome, std::string::npos);
	ASSERT_NE(PlayerDummyDraw, std::string::npos);
	ASSERT_NE(ModeChrome, std::string::npos);
	ASSERT_NE(ModeDraw, std::string::npos);
	ASSERT_NE(SkinPartChrome, std::string::npos);
	ASSERT_NE(SkinPartDraw, std::string::npos);
	EXPECT_LT(PlayerDummyChrome, PlayerDummyDraw);
	EXPECT_LT(ModeChrome, ModeDraw);
	EXPECT_LT(SkinPartChrome, SkinPartDraw);
}
