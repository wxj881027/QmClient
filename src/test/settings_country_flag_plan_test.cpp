#include <game/client/components/settings_resource_jobs.h>

#include <gtest/gtest.h>

TEST(SettingsCountryFlagPlan, DeduplicatesAndKeepsOrder)
{
	const std::vector<int> vPlan = BuildSettingsCountryFlagWarmupPlan({156, 840, 156, -1});
	ASSERT_EQ(vPlan.size(), 3u);
	EXPECT_EQ(vPlan[0], 156);
	EXPECT_EQ(vPlan[1], 840);
	EXPECT_EQ(vPlan[2], -1);
}
