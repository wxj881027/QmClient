#include <game/client/components/qmclient/practice_hook_input.h>

#include <gtest/gtest.h>

TEST(QmPracticeHookInput, EnteringPracticeStillWaitsForTheInitialHeldHookToRelease)
{
	CQmPracticeHookInput State;
	State.Capture(1);
	EXPECT_EQ(State.Filter(1, 1), 0);
	EXPECT_EQ(State.Filter(0, 0), 0);
	EXPECT_EQ(State.Filter(1, 1), 1);
}

TEST(QmPracticeHookInput, ReturningFromFreeSpectatingKeepsTheHeldHook)
{
	CQmPracticeHookInput State;
	State.Capture(1, true);
	EXPECT_EQ(State.Filter(1, 1), 1);
	EXPECT_EQ(State.Filter(1, 1), 1);
}

TEST(QmPracticeHookInput, ReleasingTheHookWhilePausedStillReleasesItOnReturn)
{
	CQmPracticeHookInput State;
	State.Capture(1, true);
	EXPECT_EQ(State.Filter(0, 0), 0);
	EXPECT_EQ(State.Filter(1, 1), 1);
}

TEST(QmPracticeHookInput, RepeatedPauseFramesDoNotRearmTheReleaseGate)
{
	CQmPracticeHookInput State;
	for(int Frame = 0; Frame < 3; ++Frame)
	{
		State.Capture(1, true);
		EXPECT_EQ(State.Filter(1, 1), 1);
	}
}

TEST(QmPracticeHookInput, ANewPracticeEntryRestoresItsInitialReleaseRequirement)
{
	CQmPracticeHookInput State;
	State.Capture(1, true);
	EXPECT_EQ(State.Filter(1, 1), 1);
	State.Capture(1);
	EXPECT_EQ(State.Filter(1, 1), 0);
}
