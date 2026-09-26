#include <game/client/components/settings_resource_jobs.h>

#include <gtest/gtest.h>

TEST(SettingsAssetPriority, PreviewPrioritizesCurrentVisibleRange)
{
	EXPECT_FALSE(SettingsAssetPreviewShouldPrioritizeVisibleRange(9, 10, 20));
	EXPECT_TRUE(SettingsAssetPreviewShouldPrioritizeVisibleRange(10, 10, 20));
	EXPECT_TRUE(SettingsAssetPreviewShouldPrioritizeVisibleRange(15, 10, 20));
	EXPECT_TRUE(SettingsAssetPreviewShouldPrioritizeVisibleRange(20, 10, 20));
	EXPECT_FALSE(SettingsAssetPreviewShouldPrioritizeVisibleRange(21, 10, 20));
	EXPECT_FALSE(SettingsAssetPreviewShouldPrioritizeVisibleRange(10, -1, 20));
}

TEST(SettingsAssetPriority, WorkshopThumbDecodePrioritizesVisibleDownloadableItems)
{
	EXPECT_TRUE(SettingsWorkshopThumbShouldStartHighPriority(0, 0, 3));
	EXPECT_TRUE(SettingsWorkshopThumbShouldStartHighPriority(3, 0, 3));
	EXPECT_FALSE(SettingsWorkshopThumbShouldStartHighPriority(4, 0, 3));
	EXPECT_FALSE(SettingsWorkshopThumbShouldStartHighPriority(0, -1, 3));
}
