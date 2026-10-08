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
