#include <game/client/components/qmclient/screenshot_manager.h>

#include <gtest/gtest.h>

TEST(QmScreenshotWatermarkText, ComposeKeepsEnabledPartsInStableOrder)
{
	EXPECT_EQ(CQmScreenshotManager::ComposeWatermarkText("2026-09-27 12:34:56", "dm1", "我的截图"), "2026-09-27 12:34:56 | MAP: dm1 | 我的截图");
	EXPECT_EQ(CQmScreenshotManager::ComposeWatermarkText(nullptr, "dm1", "我的截图"), "MAP: dm1 | 我的截图");
}

TEST(QmScreenshotWatermarkText, ComposeOmitsEmptyPartsWithoutDanglingSeparators)
{
	EXPECT_TRUE(CQmScreenshotManager::ComposeWatermarkText(nullptr, nullptr, "").empty());
	EXPECT_EQ(CQmScreenshotManager::ComposeWatermarkText("", "", "只保留这段"), "只保留这段");
}
