#include <game/client/components/settings_resource_jobs.h>

#include <gtest/gtest.h>

TEST(SettingsAssetLoading, DoesNotBlockVisibleEntries)
{
	EXPECT_TRUE(SettingsAssetListShouldShowBlockingLoading(true, 0));
	EXPECT_FALSE(SettingsAssetListShouldShowBlockingLoading(true, 1));
	EXPECT_FALSE(SettingsAssetListShouldShowBlockingLoading(false, 0));
}
