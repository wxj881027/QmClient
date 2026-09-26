#include <game/client/components/settings_resource_jobs.h>

#include <gtest/gtest.h>

TEST(SettingsLoadingPrewarm, AdvanceRequiresNoProgressToFinish)
{
	SSettingsLoadingPrewarmState State;
	State.m_LastBuiltTextContainers = 10;
	State.m_LastMissingTextPlanItems = 2;
	State.m_LastMissingTextPlanCollectionUnits = 1;

	SettingsLoadingPrewarmAdvance(State, 18, 1, 1);
	EXPECT_EQ(State.m_CompletedSteps, 1);
	EXPECT_EQ(State.m_ConsecutiveNoProgressSteps, 0);
	EXPECT_EQ(State.m_LastMissingTextPlanItems, 1);
	EXPECT_FALSE(State.m_WarmupReady);

	SettingsLoadingPrewarmAdvance(State, 18, 1, 1);
	EXPECT_EQ(State.m_CompletedSteps, 2);
	EXPECT_EQ(State.m_ConsecutiveNoProgressSteps, 1);
	EXPECT_FALSE(State.m_WarmupReady);

	SettingsLoadingPrewarmAdvance(State, 18, 0, 1);
	EXPECT_EQ(State.m_CompletedSteps, 3);
	EXPECT_EQ(State.m_ConsecutiveNoProgressSteps, 0);
	EXPECT_EQ(State.m_LastMissingTextPlanItems, 0);
	EXPECT_FALSE(State.m_WarmupReady);

	SettingsLoadingPrewarmAdvance(State, 18, 0, 0);
	EXPECT_EQ(State.m_CompletedSteps, 4);
	EXPECT_EQ(State.m_ConsecutiveNoProgressSteps, 0);
	EXPECT_TRUE(State.m_WarmupReady);
}

TEST(SettingsLoadingPrewarm, ProgressDetectionIsStrictlyIncreasing)
{
	EXPECT_TRUE(SettingsLoadingPrewarmMadeProgress(3, 4));
	EXPECT_FALSE(SettingsLoadingPrewarmMadeProgress(4, 4));
	EXPECT_FALSE(SettingsLoadingPrewarmMadeProgress(5, 4));
	EXPECT_TRUE(SettingsLoadingPrewarmMadeProgress(4, 4, 3, 2));
	EXPECT_TRUE(SettingsLoadingPrewarmMadeProgress(4, 4, -1, 2));
	EXPECT_FALSE(SettingsLoadingPrewarmMadeProgress(4, 4, 2, 2));
	EXPECT_TRUE(SettingsLoadingPrewarmMadeProgress(4, 4, 2, 2, 3, 2));
	EXPECT_TRUE(SettingsLoadingPrewarmMadeProgress(4, 4, 2, 2, -1, 2));
	EXPECT_FALSE(SettingsLoadingPrewarmMadeProgress(4, 4, 2, 2, 2, 2));
}
