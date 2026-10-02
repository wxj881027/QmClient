// 设置资源任务测试：按资源任务生命周期独立组织。
#include <game/client/components/assets_preview_scale.h>
#include <game/client/components/menus.h>
#include <game/client/components/settings_resource_jobs.h>
#include <game/client/components/settings_warmup.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

TEST(SettingsResourceJobsAssets, VisibleNormalSizedUploadStillUsesByteBudget)
{
	SSettingsResourceFrameContext Stable{};
	Stable.m_ScrollActive = false;
	Stable.m_PostScrollRecoveryFrames = 0;

	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(Stable, false, ESettingsResourcePriority::VISIBLE, 1, 512 * 1024, 1 * 1024 * 1024));
	EXPECT_FALSE(SettingsResourceUploadWithinByteBudget(1, 768 * 1024, 512 * 1024, 1 * 1024 * 1024));
}

TEST(SettingsResourceJobsAssets, VisibleReadyPreviewKeepsUploadPriority)
{
	EXPECT_TRUE(SettingsAssetPreviewShouldPrioritizeVisibleRange(3, 3, 5));
	EXPECT_FALSE(SettingsAssetPreviewShouldPrioritizeVisibleRange(2, 3, 5));
	EXPECT_TRUE(SettingsAssetPreviewShouldUploadHighPriorityFirst(false, true));
	EXPECT_FALSE(SettingsAssetPreviewShouldUploadHighPriorityFirst(true, false));
	EXPECT_FALSE(SettingsAssetPreviewShouldUploadHighPriorityFirst(false, false));
}

TEST(SettingsResourceJobsAssets, InactiveWindowBlocksAllNewAssetWorkStarts)
{
	EXPECT_FALSE(SettingsAssetWorkAllowedWhileWindowInactive(false, false));
	EXPECT_FALSE(SettingsAssetWorkAllowedWhileWindowInactive(false, true));
	EXPECT_TRUE(SettingsAssetWorkAllowedWhileWindowInactive(true, false));
	EXPECT_TRUE(SettingsAssetWorkAllowedWhileWindowInactive(true, true));
}

TEST(SettingsResourceJobsAssets, BudgetedPreviewCanUpgradeTierWhenHigherBudgetReturns)
{
	EXPECT_FALSE(SettingsAssetPreviewResidentTextureSatisfiesRequest(true, PreviewTextureSizeBytesEstimate(512), 1024));
	EXPECT_TRUE(SettingsAssetPreviewResidentTextureSatisfiesRequest(true, PreviewTextureSizeBytesEstimate(1024), 1024));
	EXPECT_TRUE(SettingsAssetPreviewDecodeStartNeeded(false, true, PreviewTextureSizeBytesEstimate(512), 1024, false));
	EXPECT_FALSE(SettingsAssetPreviewDecodeStartNeeded(false, true, PreviewTextureSizeBytesEstimate(1024), 1024, false));
	EXPECT_FALSE(SettingsAssetPreviewDecodeStartNeeded(true, false, 0, 1024, false));
	EXPECT_FALSE(SettingsAssetPreviewDecodeStartNeeded(false, false, 0, 1024, true));
}

TEST(SettingsResourceJobsAssets, AssetWarmupTracksAllTabsAndCycles)
{
	bool aReadyTabs[] = {true, false, true};
	EXPECT_FALSE(SettingsAssetWarmupAllTabsReady(aReadyTabs, 3));
	aReadyTabs[1] = true;
	EXPECT_TRUE(SettingsAssetWarmupAllTabsReady(aReadyTabs, 3));
	EXPECT_TRUE(SettingsAssetWarmupAllTabsReady(nullptr, 0));

	EXPECT_EQ(SettingsAssetWarmupNextTab(-1, 3), 0);
	EXPECT_EQ(SettingsAssetWarmupNextTab(0, 3), 1);
	EXPECT_EQ(SettingsAssetWarmupNextTab(2, 3), 0);
	EXPECT_EQ(SettingsAssetWarmupNextTab(0, 0), -1);
}
