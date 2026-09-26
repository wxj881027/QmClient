// QmNewUi 菜单源码合同：菜单外壳框架域。
// 运行时行为保留在 qm_new_ui_menu_branch_test.cpp。
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

namespace
{

	[[maybe_unused]] size_t MatchingBrace(const std::string &Source, size_t BodyStart)
	{
		int Depth = 0;
		for(size_t Index = BodyStart; Index < Source.size(); ++Index)
		{
			if(Source[Index] == '{')
				++Depth;
			else if(Source[Index] == '}')
			{
				--Depth;
				if(Depth == 0)
					return Index;
			}
		}
		return std::string::npos;
	}

} // namespace

TEST(QmNewUiMenuShellChromeContract, CallvoteFiltersUseSharedBoundedSemantics)
{
	EXPECT_TRUE(QmTextMatchesIncludeExcludeFilter("Deep Freeze", "deep", ""));
	EXPECT_TRUE(QmTextMatchesIncludeExcludeFilter("Deep Freeze", "", "race"));
	EXPECT_FALSE(QmTextMatchesIncludeExcludeFilter("Deep Freeze", "race", ""));
	EXPECT_FALSE(QmTextMatchesIncludeExcludeFilter("Deep Freeze", "deep", "FREEZE"));
	EXPECT_FALSE(QmTextMatchesIncludeExcludeFilter(nullptr, "", ""));
}

TEST(QmNewUiMenuShellChromeContract, MenubarUsesExplicitQmNewUiColorBranch)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string DoMenuTabV2 = FunctionBody(Source, "int CMenus::DoMenuTabV2Internal(");
	const std::string RenderMenubar = FunctionBody(Source, "void CMenus::RenderMenubar(");
	const size_t UseNewUiIfPos = RenderMenubar.find("if(UseNewUi)");
	ASSERT_NE(UseNewUiIfPos, std::string::npos);
	const size_t UseNewUiBodyStart = RenderMenubar.find("{", UseNewUiIfPos);
	ASSERT_NE(UseNewUiBodyStart, std::string::npos);
	const size_t UseNewUiBodyEnd = MatchingBrace(RenderMenubar, UseNewUiBodyStart);
	ASSERT_NE(UseNewUiBodyEnd, std::string::npos);
	const std::string UseNewUiBlock = RenderMenubar.substr(UseNewUiBodyStart, UseNewUiBodyEnd - UseNewUiBodyStart);
	const size_t OldUiElsePos = RenderMenubar.find("else", UseNewUiBodyEnd);
	ASSERT_NE(OldUiElsePos, std::string::npos);
	const size_t OldUiBodyStart = RenderMenubar.find("{", OldUiElsePos);
	ASSERT_NE(OldUiBodyStart, std::string::npos);
	const size_t OldUiBodyEnd = MatchingBrace(RenderMenubar, OldUiBodyStart);
	ASSERT_NE(OldUiBodyEnd, std::string::npos);
	const std::string OldUiBlock = RenderMenubar.substr(OldUiBodyStart, OldUiBodyEnd - OldUiBodyStart);
	const size_t HoverBranch = DoMenuTabV2.find("if(Hover)");
	const size_t ActiveBranch = DoMenuTabV2.find("else if(Active)");

	EXPECT_NE(Source.find("const bool UseNewUi = g_Config.m_QmNewUi != 0;"), std::string::npos);
	EXPECT_NE(Source.find("MenuTabDefaultColor("), std::string::npos);
	EXPECT_NE(Source.find("MenuTabActiveColor("), std::string::npos);
	EXPECT_NE(Source.find("MenuTabHoverColor("), std::string::npos);
	EXPECT_NE(Source.find("MenuIconButtonDefaultColor("), std::string::npos);
	ASSERT_NE(HoverBranch, std::string::npos);
	ASSERT_NE(ActiveBranch, std::string::npos);
	EXPECT_LT(HoverBranch, ActiveBranch);
	EXPECT_NE(DoMenuTabV2.find("Target = pCustomHover != nullptr ? *pCustomHover : HoverColor;"), std::string::npos);
	EXPECT_NE(DoMenuTabV2.find("Target = pCustomActive != nullptr ? *pCustomActive : ActiveColor;"), std::string::npos);
	EXPECT_NE(Source.find("return UseNewUi ? MenuTabDefaultColor() : ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);"), std::string::npos);
	EXPECT_NE(Source.find("const ColorRGBA DefaultColor = UseNewUi ? MenuTabDefaultColor() : ms_ColorTabbarInactive;"), std::string::npos);
	EXPECT_NE(Source.find("const ColorRGBA ActiveColor = UseNewUi ? MenuTabActiveColor() : ms_ColorTabbarActive;"), std::string::npos);
	EXPECT_NE(Source.find("const ColorRGBA HoverColor = UseNewUi ? MenuTabHoverColor() : ms_ColorTabbarHover;"), std::string::npos);
	EXPECT_NE(DoMenuTabV2.find("DrawRoundedSurface(Ui(), *pRect, Resolved, ColorRGBA(), UseNewUi ? 7.0f * ContentScale : 10.0f, 0.0f, Corners);"), std::string::npos);
	EXPECT_NE(DoMenuTabV2.find("const float LabelFontSize = UseNewUi ? ui_token::settings::TAB_FONT_SIZE * ContentScale : Label.h * CUi::ms_FontmodHeight;"), std::string::npos);
	EXPECT_NE(DoMenuTabV2.find("Ui()->DoLabel(&Label, pText, LabelFontSize, TEXTALIGN_MC);"), std::string::npos);
	EXPECT_NE(Source.find("const bool UseNewUi = g_Config.m_QmNewUi != 0;"), std::string::npos);
	EXPECT_NE(Source.find("ColorRGBA InactiveColor = MenuTabDefaultColor();"), std::string::npos);
	EXPECT_NE(Source.find("ColorRGBA ActiveColor = MenuTabActiveColor();"), std::string::npos);
	EXPECT_NE(Source.find("ColorRGBA HoverColor = MenuMenubarHoverColor();"), std::string::npos);
	EXPECT_NE(Source.find("ColorRGBA InactiveColor = ms_ColorTabbarInactive;"), std::string::npos);
	EXPECT_NE(Source.find("ColorRGBA ActiveColor = ms_ColorTabbarActive;"), std::string::npos);
	EXPECT_NE(Source.find("ColorRGBA HoverColor = ms_ColorTabbarHover;"), std::string::npos);
	// 新 UI 的激活位置改由各排胶囊 Tabbar 的滑块表达，页签下方的下划线指示块已移除。
	EXPECT_EQ(Source.find("if(UseNewUi && MenubarHaveActive && !Ui()->RenderOnly())"), std::string::npos);
	EXPECT_NE(Source.find("ui_widget::CapsuleTabBarChrome(TabBarCtx, MakeUiScopeHash(\"menubar_capsule_ingame_tabs\")"), std::string::npos);
	EXPECT_NE(RenderMenubar.find("if(!UseNewUi && MenubarHaveActive && !Ui()->RenderOnly())"), std::string::npos);
	EXPECT_NE(RenderMenubar.find("if(UseNewUi)"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("Box.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.12f)"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("Box.VMargin(MenubarOuterInsetX, &Box);"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("Box.HMargin(MenubarOuterInsetY, &Box);"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("const float BrowserButtonWidth = 58.0f * MENU_MENUBAR_CONTENT_SCALE_NEW;"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("const float GameButtonWidth = (CompactOnlineMenuTabs ? 56.0f : 64.0f) * MENU_MENUBAR_CONTENT_SCALE_NEW;"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("const float ServerInfoButtonWidth = (CompactOnlineMenuTabs ? 94.0f : 104.0f) * MENU_MENUBAR_CONTENT_SCALE_NEW;"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("const float OnlineTabGap = 4.0f;"), std::string::npos);
	// 在线页签同样改为「收集槽位 → 画胶囊容器与滑块 → 画页签」的循环。
	EXPECT_NE(UseNewUiBlock.find("if(DoIngameMenuTab(&s_aOnlineTabButtons[DrawnOnlineTabs], Tab.m_Page, Tab.m_pTextId, Tab.m_pText, ActivePage == Tab.m_Page, &TabRect, IGraphics::CORNER_ALL))"), std::string::npos);
	EXPECT_EQ(UseNewUiBlock.find("DoIngameMenuTab(&s_GameButton, PAGE_GAME, \"ingame-tab-game\", Localize(\"Game\"), ActivePage == PAGE_GAME, &Button, IGraphics::CORNER_TL)"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("if(DoMenuTabV2_QmIcon(&s_SettingsButton"), std::string::npos);
	// 起始页签改为「先收集槽位、再画胶囊容器与滑块、最后画图标」的循环，槽位数组取代了逐页签的内联绘制。
	EXPECT_NE(UseNewUiBlock.find("if(DoMenuTabV2_QmIcon(&s_aStartTabButtons[TabIndex], Tab.m_Icon, Tab.m_pIcon, TabActive, &aStartTabSlots[TabIndex]"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("ui_widget::CapsuleTabBarChrome(TabBarCtx, MakeUiScopeHash(\"menubar_capsule_start_tabs\")"), std::string::npos);
	EXPECT_EQ(UseNewUiBlock.find("DoButton_MenuTab(&s_SettingsButton"), std::string::npos);
	EXPECT_EQ(UseNewUiBlock.find("DoButton_MenuTab(&s_InternetButton"), std::string::npos);
	EXPECT_EQ(OldUiBlock.find("Box.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.12f)"), std::string::npos);
	EXPECT_EQ(OldUiBlock.find("Box.VMargin(MenubarOuterInsetX, &Box);"), std::string::npos);
	EXPECT_EQ(OldUiBlock.find("Box.HMargin(MenubarOuterInsetY, &Box);"), std::string::npos);
	EXPECT_NE(OldUiBlock.find("if(DoButton_MenuTab_QmIcon(&s_SettingsButton"), std::string::npos);
	EXPECT_NE(OldUiBlock.find("if(DoButton_MenuTab_QmIcon(&s_InternetButton"), std::string::npos);
	const std::string OldOnlineBlock = BlockBodyAfter(OldUiBlock, "if(ClientState == IClient::STATE_ONLINE)");
	ASSERT_FALSE(OldOnlineBlock.empty());
	EXPECT_NE(OldOnlineBlock.find("Box.VSplitRight(10.0f, &Box, nullptr);"), std::string::npos);
	EXPECT_NE(OldOnlineBlock.find("Box.VSplitRight(33.0f, &Box, &Button);"), std::string::npos);
	EXPECT_NE(OldOnlineBlock.find("DoButton_MenuTab_QmIcon(&s_DemoButton, EQmIcon::CLAPPERBOARD, FONT_ICON_CLAPPERBOARD"), std::string::npos);
	EXPECT_EQ(OldUiBlock.find("if(Box.w >= 10.0f + 33.0f + 10.0f)"), std::string::npos);
	EXPECT_EQ(OldUiBlock.find("DoMenuTabV2(&s_SettingsButton"), std::string::npos);
	EXPECT_EQ(OldUiBlock.find("DoMenuTabV2(&s_InternetButton"), std::string::npos);
	EXPECT_NE(OldUiBlock.find("DoIngameMenuTab(&s_GameButton, PAGE_GAME, \"ingame-tab-game\", Localize(\"Game\"), ActivePage == PAGE_GAME, &Button, IGraphics::CORNER_TL)"), std::string::npos);
}

TEST(QmNewUiMenuShellChromeContract, IngameGameButtonBarRoundsAllCornersOnlyInNewUi)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_ingame.cpp");
	const std::string RenderGame = FunctionBody(Source, "void CMenus::RenderGame(CUIRect MainView)");

	EXPECT_NE(RenderGame.find("const int ButtonBarsCorners = g_Config.m_QmNewUi != 0 ? IGraphics::CORNER_ALL : IGraphics::CORNER_B;"), std::string::npos);
	EXPECT_NE(RenderGame.find("ButtonBars.Draw(ms_ColorTabbarActive, ButtonBarsCorners, 10.0f);"), std::string::npos);
	EXPECT_EQ(RenderGame.find("ButtonBars.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);"), std::string::npos);
}

TEST(QmNewUiMenuShellChromeContract, MenuDefersGaussianBlurPreparationOnFirstOpenFrame)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string Render = FunctionBody(Source, "void CMenus::Render()");

	EXPECT_NE(Render.find("const int MenuOpenFrame = m_MenuOpenFrame++;"), std::string::npos);
	EXPECT_NE(Render.find("CUiScopedGaussianBlur GaussianBlurScope(Ui(), MenuOpenFrame == 0 ? 0.0f : 1.0f);"), std::string::npos);
	EXPECT_NE(Render.find("if(CanPrewarmSettings && MenuOpenFrame > 0)"), std::string::npos);
}

TEST(QmNewUiMenuShellChromeContract, LegacyMenusKeepTabAndPanelShellConnected)
{
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string MenuShellSplit = "const bool UseNewUi = g_Config.m_QmNewUi != 0;\n\t\t\tScreen.HSplitTop(MenuMenubarHeight(UseNewUi), &TabBar, &MainView);\n\t\t\tif(UseNewUi)\n\t\t\t\tMainView.HSplitTop(6.0f, nullptr, &MainView);";
	EXPECT_NE(MenusSource.find("constexpr float MENU_MENUBAR_HEIGHT_NEW = 24.0f;"), std::string::npos);
	EXPECT_NE(MenusSource.find("constexpr float MENU_MENUBAR_HEIGHT_LEGACY = 30.0f;"), std::string::npos);
	EXPECT_NE(MenusSource.find("constexpr float MenuMenubarHeight(bool UseNewUi)"), std::string::npos);
	EXPECT_NE(MenusSource.find(MenuShellSplit), std::string::npos);
	EXPECT_NE(MenusSource.find("case IClient::STATE_ONLINE:"), std::string::npos);
	EXPECT_NE(MenusSource.find(MenuShellSplit, MenusSource.find("case IClient::STATE_ONLINE:")), std::string::npos);

	const std::string QmClientSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	// 设置页不再缓存 UseNewUi 局部量，直接读配置，避免未使用变量。
	EXPECT_EQ(QmClientSource.find("const bool UseNewUi"), std::string::npos);
	EXPECT_EQ(QmClientSource.find("if(UseNewUi)\n\t\t\tMainView.HSplitTop(Margin, nullptr, &MainView);"), std::string::npos);
}
