#include <game/client/components/settings_resource_jobs.h>

#include <gtest/gtest.h>

#include <optional>
#include <vector>

TEST(SettingsSkinPlan, KeepsSelectedFavoritesThenSorted)
{
	std::vector<SSettingsSkinListEntry> vEntries = {
		{"zeta", false, false},
		{"alpha", false, true},
		{"selected", true, false},
	};
	const SSettingsSkinListPlan Plan = BuildSettingsSkinListPlan(vEntries, 0);
	ASSERT_EQ(Plan.m_vNames.size(), 3u);
	EXPECT_EQ(Plan.m_vNames[0], "selected");
	EXPECT_EQ(Plan.m_vNames[1], "alpha");
	EXPECT_EQ(Plan.m_vNames[2], "zeta");
}

TEST(SettingsSkinPlan, TimeSortKeepsFavoritesThenOrdersGroupsByOfficialDate)
{
	std::vector<SSettingsSkinListEntry> vEntries = {
		{"old_regular", false, false, {}, 20200101, 40},
		{"new_regular", false, false, {}, 20250614, 10},
		{"old_favorite", false, true, {}, 20200102, 30},
		{"new_favorite", false, true, {}, 20250615, 20},
	};
	const SSettingsSkinListPlan Plan = BuildSettingsSkinListPlan(vEntries, 1);
	ASSERT_EQ(Plan.m_vNames.size(), 4u);
	EXPECT_EQ(Plan.m_vNames[0], "new_favorite");
	EXPECT_EQ(Plan.m_vNames[1], "old_favorite");
	EXPECT_EQ(Plan.m_vNames[2], "new_regular");
	EXPECT_EQ(Plan.m_vNames[3], "old_regular");
}

TEST(SettingsSkinPlan, SameNameQueuedColorVariantsRemainDistinctAfterSorting)
{
	const std::vector<SSettingsSkinListEntry> vEntries = {
		{"default", false, false, SSettingsSkinListColorKey{true, 123, 456}},
		{"default", true, false, SSettingsSkinListColorKey{true, 789, 1011}},
		{"default", false, false, std::nullopt},
	};
	const auto Plan = BuildSettingsSkinListPlan(vEntries, 0);
	ASSERT_EQ(Plan.m_vEntries.size(), 3u);
	ASSERT_EQ(Plan.m_vNames.size(), 3u);
	ASSERT_TRUE(Plan.m_vEntries[0].m_ColorKey.has_value());
	EXPECT_TRUE(Plan.m_vEntries[0].m_Selected);
	EXPECT_TRUE(Plan.m_vEntries[0].m_ColorKey->m_UseCustomColor);
	EXPECT_EQ(Plan.m_vEntries[0].m_ColorKey->m_ColorBody, 789);
	EXPECT_EQ(Plan.m_vEntries[0].m_ColorKey->m_ColorFeet, 1011);
	ASSERT_TRUE(Plan.m_vEntries[1].m_ColorKey.has_value());
	EXPECT_EQ(Plan.m_vEntries[1].m_ColorKey->m_ColorBody, 123);
	EXPECT_EQ(Plan.m_vEntries[1].m_ColorKey->m_ColorFeet, 456);
	EXPECT_FALSE(Plan.m_vEntries[2].m_ColorKey.has_value());
}
