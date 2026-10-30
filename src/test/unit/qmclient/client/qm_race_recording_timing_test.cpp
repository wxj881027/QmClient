#include <game/client/components/qmclient/race_timing.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
	constexpr int MIN_TICK = std::numeric_limits<int>::min();
	constexpr int MAX_TICK = std::numeric_limits<int>::max();
}

TEST(QmRaceRecordingTiming, RestartStillRequiresStrictlyMoreThanTenSeconds)
{
	EXPECT_FALSE(qm_race_timing::CanRestartRecording(599, 100, 50));
	EXPECT_FALSE(qm_race_timing::CanRestartRecording(600, 100, 50));
	EXPECT_TRUE(qm_race_timing::CanRestartRecording(601, 100, 50));
}

TEST(QmRaceRecordingTiming, PreparationStillExpiresAtTwentySeconds)
{
	EXPECT_FALSE(qm_race_timing::PreparationExpired(1099, 100, 50));
	EXPECT_TRUE(qm_race_timing::PreparationExpired(1100, 100, 50));
}

TEST(QmRaceRecordingTiming, DeadlineAdditionNearMaximumDoesNotEnablePrematureRestart)
{
	EXPECT_FALSE(qm_race_timing::CanRestartRecording(MAX_TICK, MAX_TICK - 100, 50));
	EXPECT_FALSE(qm_race_timing::PreparationExpired(MAX_TICK, MAX_TICK - 100, 50));
}

TEST(QmRaceRecordingTiming, LargeElapsedDifferenceDoesNotWrapAwayTimeout)
{
	EXPECT_TRUE(qm_race_timing::CanRestartRecording(MAX_TICK, MIN_TICK, 50));
	EXPECT_TRUE(qm_race_timing::PreparationExpired(MAX_TICK, MIN_TICK, 50));
}

TEST(QmRaceRecordingTiming, FutureStartDoesNotWrapIntoElapsedTimeout)
{
	EXPECT_FALSE(qm_race_timing::CanRestartRecording(MIN_TICK, MAX_TICK, 50));
	EXPECT_FALSE(qm_race_timing::PreparationExpired(MIN_TICK, MAX_TICK, 50));
}

TEST(QmRaceRecordingTiming, LargeTickSpeedThresholdIsMultipliedInWideType)
{
	EXPECT_FALSE(qm_race_timing::CanRestartRecording(MAX_TICK, 0, MAX_TICK));
	EXPECT_FALSE(qm_race_timing::PreparationExpired(MAX_TICK, 0, MAX_TICK));
}

TEST(QmRaceRecordingTiming, NormalFinishDeadlineKeepsOneSecondDelay)
{
	const int Stop = qm_race_timing::RecordingStopTick(100, 50);
	EXPECT_EQ(Stop, 150);
}

TEST(QmRaceRecordingTiming, ExactMaximumDeadlineStaysRepresentable)
{
	EXPECT_EQ(qm_race_timing::RecordingStopTick(MAX_TICK - 50, 50), MAX_TICK);
}

TEST(QmRaceRecordingTiming, OverflowingDeadlineStopsAtLastRepresentableTick)
{
	const int Stop = qm_race_timing::RecordingStopTick(MAX_TICK - 10, 50);
	EXPECT_EQ(Stop, MAX_TICK);
	EXPECT_EQ(qm_race_timing::RecordingStopTick(MAX_TICK, MAX_TICK), MAX_TICK);
}

TEST(QmRaceRecordingTiming, NonpositiveTickSpeedDoesNotCreateFutureDeadline)
{
	EXPECT_EQ(qm_race_timing::RecordingStopTick(100, 0), 100);
	EXPECT_EQ(qm_race_timing::RecordingStopTick(100, -1), 100);
	EXPECT_EQ(qm_race_timing::RecordingStopTick(MIN_TICK, 0), MIN_TICK);
}
