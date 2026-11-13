#include <engine/client/replay_save.h>

#include <gtest/gtest.h>

#include <limits>

TEST(ReplaySaveStartTick, OrdinaryLookbackKeepsItsTickRange)
{
	EXPECT_EQ(replay_save::StartTick(1000, 3, 50), 850);
}

TEST(ReplaySaveStartTick, LongerLookbackStartsAtTheFirstRepresentableDemoTick)
{
	EXPECT_EQ(replay_save::StartTick(100, 30, 50), MIN_TICK);
}

TEST(ReplaySaveStartTick, MaximumSecondsCannotOverflowMultiplication)
{
	EXPECT_EQ(replay_save::StartTick(1000, std::numeric_limits<int>::max(), 50), MIN_TICK);
}

TEST(ReplaySaveStartTick, ProductOfMaximumInputsCannotOverflow)
{
	EXPECT_EQ(replay_save::StartTick(MAX_TICK - 1, std::numeric_limits<int>::max(), std::numeric_limits<int>::max()), MIN_TICK);
}

TEST(ReplaySaveStartTick, FirstTickDoesNotUnderflow)
{
	EXPECT_EQ(replay_save::StartTick(MIN_TICK, 1, 50), MIN_TICK);
}

TEST(ReplaySaveStartTick, LastValidTickKeepsAnOrdinaryLookback)
{
	EXPECT_EQ(replay_save::StartTick(MAX_TICK - 1, 1, 50), MAX_TICK - 51);
}
