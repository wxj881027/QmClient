#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>

#include <string>

TEST(QmNewUiMenuBranches, RemovedQmToolboxPageApiStaysRemoved)
{
	// 迁移终态无法由点击行为证明；导航选择与动画由 QmNavigationSelection 行为测试覆盖。
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	EXPECT_EQ(Source.find("UI_TOOLBOX_PAGE_QM"), std::string::npos);
	EXPECT_EQ(Source.find("RenderServerbrowserQm"), std::string::npos);
}
