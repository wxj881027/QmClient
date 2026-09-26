// 请抬头享受阳光｜日子很好 我很我---------致咩子
// Qm monitoring 源码合同：设置界面性能窗口与预算遥测。
// 设置界面结构/甲板合同见 qmclient_monitoring_settings_structure_contract_test.cpp；
// 文本计划/预热调度合同见 qmclient_monitoring_settings_plan_contract_test.cpp。
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
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

TEST(QmMonitoringSettingsPerfContract, SettingsUiBudgetTelemetryExists)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string TClient = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Assets = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");

	EXPECT_NE(Header.find("struct SSettingsUiBudgetFrame"), std::string::npos);
	EXPECT_NE(Header.find("LogSettingsUiBudget("), std::string::npos);
	const size_t UiBudgetEvent = Menus.find("event=settings_ui_budget");
	ASSERT_NE(UiBudgetEvent, std::string::npos);
	const std::string UiBudgetFormat = Menus.substr(UiBudgetEvent, 512);
	EXPECT_TRUE(ContainsAll(UiBudgetFormat, {"layout_ms=", "text_ms=", "text_new=", "text_reused=", "draw_calls=", "vertices=", "indices=", "heap_allocs=", "visible_widgets="}));
	EXPECT_NE(TClient.find("LogSettingsUiBudget(\"settings:tclient\""), std::string::npos);
	EXPECT_NE(Assets.find("LogSettingsUiBudget(\"settings:assets\""), std::string::npos);
}

TEST(QmMonitoringSettingsPerfContract, SettingsPerfWindowFlushesActiveSamplesOnShutdown)
{
	const std::string MenusSource = ReadTestSourceFile("src/game/client/components/menus.cpp");

	EXPECT_NE(MenusSource.find("void CMenus::OnShutdown()"), std::string::npos);
	EXPECT_NE(MenusSource.find("if(m_SettingsPerfWindowTracker.HasActiveWindow())"), std::string::npos);
	EXPECT_NE(MenusSource.find("const SQmSettingsPerfWindowSummary Summary = m_SettingsPerfWindowTracker.FinishActiveWindow();"), std::string::npos);
	EXPECT_NE(MenusSource.find("LogSettingsPerfWindowSummary(Summary);"), std::string::npos);
}

TEST(QmMonitoringSettingsPerfContract, SettingsOpenWindowIsProtectedFromStalePreviousSettingsState)
{
	const std::string MenusSource = ReadTestSourceFile("src/game/client/components/menus.cpp");
	const std::string SettingsSource = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");

	EXPECT_NE(MenusSource.find("OldPage != NewPage && NewPage == PAGE_SETTINGS"), std::string::npos);
	EXPECT_NE(MenusSource.find("m_SettingsPerfLastPage = -1;"), std::string::npos);
	EXPECT_NE(SettingsSource.find("if(m_SettingsPerfLastPage != -1)"), std::string::npos);
	EXPECT_NE(SettingsSource.find("StartSettingsPerfFixedWindow(\"settings_tab_switch\""), std::string::npos);
}

TEST(QmMonitoringSettingsPerfContract, SettingsPerfWindowLogsOnePercentLowFps)
{
	const std::string Header = ReadRepoFile("src/game/client/components/qmclient/settings_perf_windows.h");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");

	EXPECT_NE(Header.find("m_FpsOnePctLow"), std::string::npos);
	EXPECT_EQ(Header.find("m_FpsOnePctLow = m_Summary.m_FrameMsP99 > 0.0f ? 1000.0f / m_Summary.m_FrameMsP99 : 0.0f;"), std::string::npos);
	EXPECT_NE(Header.find("OnePercentLowFpsFromFrameMs"), std::string::npos);
	EXPECT_NE(Menus.find("fps_1pct_low=%.3f"), std::string::npos);
	EXPECT_NE(Menus.find("fps_1pct_source=real_sampled"), std::string::npos);
	EXPECT_NE(Menus.find("window_start_frame=%"), std::string::npos);
	EXPECT_NE(Menus.find("window_end_frame=%"), std::string::npos);
	EXPECT_NE(Menus.find("Summary.m_FpsOnePctLow"), std::string::npos);
}
