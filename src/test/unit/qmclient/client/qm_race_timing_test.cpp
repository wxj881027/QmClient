#include <game/client/components/qmclient/race_timing.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
	constexpr int MIN_TICK = std::numeric_limits<int>::min();
	constexpr int MAX_TICK = std::numeric_limits<int>::max();
}

TEST(QmRaceTiming, UnrepresentableWarmupHasNoStartTick)
{
	EXPECT_FALSE(qm_race_timing::DecodeStartTick(MIN_TICK));
}

TEST(QmRaceTiming, RepresentableWarmupBoundariesKeepTheirMeaning)
{
	EXPECT_EQ(qm_race_timing::DecodeStartTick(MIN_TICK + 1), MAX_TICK);
	EXPECT_EQ(qm_race_timing::DecodeStartTick(0), 0);
	EXPECT_EQ(qm_race_timing::DecodeStartTick(MAX_TICK), -MAX_TICK);
	EXPECT_EQ(qm_race_timing::DecodeStartTick(2), -2);
}

TEST(QmRaceTiming, InvalidInitialWarmupKeepsInactiveHistory)
{
	EXPECT_EQ(qm_race_timing::UpdateLastTick(-1, true, MIN_TICK), -1);
}

TEST(QmRaceTiming, BadUpdatePreservesHistoryUntilValidRecovery)
{
	int Last = qm_race_timing::UpdateLastTick(-1, true, -100);
	ASSERT_EQ(Last, 100);
	Last = qm_race_timing::UpdateLastTick(Last, true, MIN_TICK);
	EXPECT_EQ(Last, 100);
	Last = qm_race_timing::UpdateLastTick(Last, true, -150);
	EXPECT_EQ(Last, 150);
	EXPECT_EQ(qm_race_timing::UpdateLastTick(Last, true, -150), 150);
}

TEST(QmRaceTiming, RemovingRaceFlagClearsHistoryEvenForInvalidWarmup)
{
	EXPECT_EQ(qm_race_timing::UpdateLastTick(100, false, MIN_TICK), -1);
	EXPECT_EQ(qm_race_timing::UpdateLastTick(100, false, -150), -1);
}

TEST(QmRaceTiming, InvalidWarmupDoesNotTriggerServerStart)
{
	EXPECT_FALSE(qm_race_timing::IsNewServerStart(-1, qm_race_timing::DecodeStartTick(MIN_TICK), 100, 50));
}

TEST(QmRaceTiming, UnchangedStartDoesNotRestartRecording)
{
	EXPECT_FALSE(qm_race_timing::IsNewServerStart(100, 100, 101, 50));
}

TEST(QmRaceTiming, ServerStartWindowRemainsStrictlyLessThanOneSecond)
{
	EXPECT_TRUE(qm_race_timing::IsNewServerStart(-1, 100, 149, 50));
	EXPECT_FALSE(qm_race_timing::IsNewServerStart(-1, 100, 150, 50));
}

TEST(QmRaceTiming, FutureStartRetainsExistingServerComparison)
{
	EXPECT_TRUE(qm_race_timing::IsNewServerStart(-1, 150, 100, 50));
}

TEST(QmRaceTiming, DistantStartDoesNotWrapIntoRecentWindow)
{
	EXPECT_FALSE(qm_race_timing::IsNewServerStart(-1, -MAX_TICK, MAX_TICK, 50));
	EXPECT_EQ(qm_race_timing::TickDifference(MAX_TICK, -MAX_TICK), 4294967294LL);
}

TEST(QmRaceTiming, InactiveHistoryHasNoElapsedTime)
{
	EXPECT_EQ(qm_race_timing::ElapsedSeconds(100, -1, 50), 0);
	EXPECT_EQ(qm_race_timing::ElapsedSeconds(100, -2, 50), 0);
}

TEST(QmRaceTiming, InvalidTickSpeedDoesNotDivide)
{
	EXPECT_EQ(qm_race_timing::ElapsedSeconds(100, 0, 0), 0);
	EXPECT_EQ(qm_race_timing::ElapsedSeconds(100, 0, -1), 0);
}

TEST(QmRaceTiming, ElapsedTimeKeepsWholeSecondTruncation)
{
	EXPECT_EQ(qm_race_timing::ElapsedSeconds(249, 100, 50), 2);
	EXPECT_EQ(qm_race_timing::ElapsedSeconds(250, 100, 50), 3);
	EXPECT_EQ(qm_race_timing::ElapsedSeconds(51, 100, 50), 0);
	EXPECT_EQ(qm_race_timing::ElapsedSeconds(50, 100, 50), -1);
}

TEST(QmRaceTiming, ExtremeElapsedTimeUsesWideDifference)
{
	EXPECT_EQ(qm_race_timing::ElapsedSeconds(MIN_TICK, MAX_TICK, 50), -85899345);
	EXPECT_EQ(qm_race_timing::ElapsedSeconds(MAX_TICK, 0, 50), 42949672);
}

TEST(QmRaceTiming, ElapsedTimeClampsOnlyUnrepresentableSeconds)
{
	EXPECT_EQ(qm_race_timing::ElapsedSeconds(MIN_TICK, MAX_TICK, 1), MIN_TICK);
	EXPECT_EQ(qm_race_timing::ElapsedSeconds(MAX_TICK, 0, 1), MAX_TICK);
}
