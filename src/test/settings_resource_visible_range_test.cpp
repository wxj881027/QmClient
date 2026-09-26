#include <game/client/components/settings_resource_jobs.h>

#include <gtest/gtest.h>

TEST(SettingsResourceVisibleRange, KeepsTotalLengthStable)
{
	const SSettingsSkinListVisibleRange Range = SettingsSkinListVisibleRangeForScroll(125.0f, 300.0f, 50.0f, 4, 101, 1);
	EXPECT_EQ(Range.m_TotalItems, 101);
	EXPECT_EQ(Range.m_TotalRows, 26);
	EXPECT_EQ(Range.m_FirstVisibleRow, 1);
	EXPECT_EQ(Range.m_LastVisibleRow, 9);
	EXPECT_EQ(Range.m_FirstItem, 4);
	EXPECT_EQ(Range.m_EndItem, 40);
	EXPECT_EQ(Range.m_VisibleRows, 9);
	EXPECT_EQ(Range.m_RenderedItems, 36);
	EXPECT_EQ(Range.m_SkippedItems, 65);
}

TEST(SettingsResourceVisibleRange, HandlesShortAndEmptyLists)
{
	const SSettingsSkinListVisibleRange Empty = SettingsSkinListVisibleRangeForScroll(0.0f, 300.0f, 50.0f, 4, 0, 1);
	EXPECT_EQ(Empty.m_TotalItems, 0);
	EXPECT_EQ(Empty.m_TotalRows, 0);
	EXPECT_EQ(Empty.m_FirstItem, 0);
	EXPECT_EQ(Empty.m_EndItem, 0);
	EXPECT_EQ(Empty.m_SkippedItems, 0);

	const SSettingsSkinListVisibleRange Short = SettingsSkinListVisibleRangeForScroll(500.0f, 300.0f, 50.0f, 4, 7, 1);
	EXPECT_EQ(Short.m_TotalItems, 7);
	EXPECT_EQ(Short.m_TotalRows, 2);
	EXPECT_EQ(Short.m_FirstItem, 0);
	EXPECT_EQ(Short.m_EndItem, 7);
	EXPECT_EQ(Short.m_SkippedItems, 0);
}
