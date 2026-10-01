#include <game/client/components/settings_resource_jobs.h>

#include <gtest/gtest.h>

TEST(SettingsAssetPreviewLifecycle, DecodeCanStartWhileMerging)
{
	EXPECT_FALSE(SettingsAssetListCanStartPreviewDecode(true, false, false));
	EXPECT_FALSE(SettingsAssetListCanStartPreviewDecode(false, true, false));
	EXPECT_TRUE(SettingsAssetListCanStartPreviewDecode(false, false, true));
	EXPECT_FALSE(SettingsAssetListCanStartPreviewDecode(false, false, false));
}

TEST(SettingsAssetPreviewLifecycle, FinalizeBudgetDefersAfterLimit)
{
	EXPECT_FALSE(SettingsAssetPreviewShouldDeferFinalize(0, 10.0, 2, 4.0));
	EXPECT_FALSE(SettingsAssetPreviewShouldDeferFinalize(1, 3.5, 2, 4.0));
	EXPECT_TRUE(SettingsAssetPreviewShouldDeferFinalize(1, 4.0, 2, 4.0));
	EXPECT_TRUE(SettingsAssetPreviewShouldDeferFinalize(2, 0.0, 2, 4.0));
}
