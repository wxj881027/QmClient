// QmNewUi 菜单源码合同：Qm 功能开关域：动态岛行预布局、武器轨迹/动画、皮肤切换过渡、进程优先级与 IME、表情阴影。
// 运行时行为保留在 qm_new_ui_menu_branch_test.cpp。
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
#include <test/support/qmclient_source_contract_test.h>
#include <test/test.h>

TEST(QmNewUiMenuSettingsFeaturesContract, DynamicIslandSettingsOmitsEdgeMarginControl)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderQmHudDynamicIslandContent(");
	ASSERT_FALSE(Body.empty());

	EXPECT_EQ(Body.find("QmHudIslandEdgeMargin"), std::string::npos);
	EXPECT_EQ(Body.find("Localize(\"Edge margin\")"), std::string::npos);
}

TEST(QmNewUiMenuSettingsFeaturesContract, DynamicIslandEdgeMarginIsOnlyAnIgnoredLegacyCommand)
{
	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string GameClient = ReadTextFile("src/game/client/gameclient.cpp");
	const std::string Callback = FunctionBody(GameClient, "void ConDiscardLegacyHudIslandEdgeMargin(");
	const std::string OnConsoleInit = FunctionBody(GameClient, "void CGameClient::OnConsoleInit()");

	EXPECT_EQ(Config.find("qm_hud_island_edge_margin"), std::string::npos);
	ASSERT_FALSE(Callback.empty());
	EXPECT_EQ(Callback.find("g_Config"), std::string::npos);
	EXPECT_NE(OnConsoleInit.find("pConsole->Register(\"qm_hud_island_edge_margin\", \"?i[value]\", CFGFLAG_CLIENT, ConDiscardLegacyHudIslandEdgeMargin, nullptr"), std::string::npos);
}

TEST(QmNewUiMenuSettingsFeaturesContract, ProcessPrioritySettingIsRemoved)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string ClientSource = ReadTextFile("src/engine/client/client.cpp");

	// 仅约束已删除配置与入口的迁移终态；IME 页面行为不使用源码合同证明。
	EXPECT_EQ(ConfigSource.find("QmProcessHighPriority"), std::string::npos);
	EXPECT_EQ(ClientSource.find("ApplyProcessPriorityConfig"), std::string::npos);
	EXPECT_EQ(ClientSource.find("qm_process_high_priority"), std::string::npos);
}

TEST(QmNewUiMenuSettingsFeaturesContract, ScoreboardSettingsLiveInDedicatedCardModule)
{
	const std::string MenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string ScoreboardCardSource = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogBetterScoreboard.cpp");
	const std::string MiniFeaturesBody = FunctionBody(MenusSource, "void CMenus::RenderQmFunctionMiniFeaturesContent(");
	ASSERT_FALSE(MiniFeaturesBody.empty());
	ASSERT_FALSE(ScoreboardCardSource.empty());

	for(const char *pConfig : {"m_QmBetterScoreboard", "m_QmScoreboardPoints", "m_QmScoreboardOnDeath", "m_QmScoreboardScroll", "m_QmScoreboardFilter"})
	{
		EXPECT_EQ(MiniFeaturesBody.find(pConfig), std::string::npos) << pConfig;
		EXPECT_NE(ScoreboardCardSource.find(pConfig), std::string::npos) << pConfig;
	}
	EXPECT_NE(ScoreboardCardSource.find("void CMenus::RenderQmFunctionBetterScoreboardContent("), std::string::npos);
	size_t PreviousControlPosition = 0;
	for(const char *pControl : {"Better scoreboard", "Scoreboard point check", "Show scoreboard after death", "Fixed-size scoreboard rows with mouse wheel scrolling for crowded servers", "Scoreboard filter: only show players whose name or clan contains this text"})
	{
		const size_t Position = ScoreboardCardSource.find(pControl);
		ASSERT_NE(Position, std::string::npos) << pControl;
		EXPECT_GT(Position, PreviousControlPosition) << pControl;
		PreviousControlPosition = Position;
	}
}
