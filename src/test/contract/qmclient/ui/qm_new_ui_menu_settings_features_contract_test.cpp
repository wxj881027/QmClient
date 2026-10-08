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

// 卡片实现迁移终态：生产目标注册独立模块，宿主和迷你功能模块不再承载它。
TEST(QmNewUiMenuSettingsFeaturesContract, ScoreboardSettingsLiveInDedicatedCardModule)
{
	const std::string Build = ReadTextFile("CMakeLists.txt");
	const std::string Menus = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string FunctionCards = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogFunctionContent.cpp");
	EXPECT_NE(Build.find("QmUi/cards/QmCardCatalogBetterScoreboard.cpp"), std::string::npos);
	EXPECT_EQ(Menus.find("void CMenus::RenderQmFunctionBetterScoreboardContent("), std::string::npos);
	EXPECT_EQ(FunctionCards.find("void CMenus::RenderQmFunctionBetterScoreboardContent("), std::string::npos);
}
