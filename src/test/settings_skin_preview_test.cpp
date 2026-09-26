#include <game/client/components/settings_resource_jobs.h>

#include <gtest/gtest.h>

TEST(SettingsSkinPreview, FitsInsideListRow)
{
	EXPECT_FLOAT_EQ(SettingsSkinPreviewSize(50.0f, 60.0f, 50.0f), 40.0f);
	EXPECT_FLOAT_EQ(SettingsSkinPreviewSize(34.0f, 60.0f, 50.0f), 24.0f);
	EXPECT_FLOAT_EQ(SettingsSkinPreviewSize(50.0f, 24.0f, 50.0f), 14.0f);
	EXPECT_FLOAT_EQ(SettingsSkinPreviewSize(50.0f, 60.0f, 50.0f, 50.0f, 70.0f), 50.0f * (40.0f / 70.0f));
	EXPECT_FLOAT_EQ(SettingsSkinPreviewSize(50.0f, 35.0f, 50.0f, 70.0f, 40.0f), 50.0f * (25.0f / 70.0f));
	EXPECT_FLOAT_EQ(SettingsSkinPreviewCenterOffset(-10.0f, 30.0f), -10.0f);
	EXPECT_FLOAT_EQ(SettingsSkinPreviewCenterOffset(-35.0f, 15.0f), 10.0f);
}

TEST(SettingsSkinPreview, CachedPreviewCountsAsReady)
{
	EXPECT_FALSE(SettingsSkinListEntryReady(false, false, false));
	EXPECT_TRUE(SettingsSkinListEntryReady(true, false, false));
	EXPECT_TRUE(SettingsSkinListEntryReady(false, true, false));
	EXPECT_TRUE(SettingsSkinListEntryReady(false, false, true));
}
