#include <game/client/components/qmclient/race_timing.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
	constexpr int MIN_TICK = std::numeric_limits<int>::min();
	constexpr int MAX_TICK = std::numeric_limits<int>::max();
}

TEST(QmGameTimer, CountdownKeepsWholeSecondTruncation)
{
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(249, 100, 0, 1, 50, false, false), 58);
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(250, 100, 0, 1, 50, false, false), 57);
}

TEST(QmGameTimer, ActiveCountdownTakesPrecedenceOverRaceFlag)
{
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(250, 100, -200, 1, 50, true, false), 57);
}

TEST(QmGameTimer, PositiveWarmupSkipsCountdownAndKeepsSelectedClock)
{
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(200, 100, 100, 3, 50, true, false), 6);
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(200, 100, 100, 3, 50, false, false), 2);
}

TEST(QmGameTimer, GameOverClearsOnlyTheCountdownBranch)
{
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(250, 100, -200, 1, 50, true, true), 0);
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(250, 100, -200, 0, 50, true, true), 1);
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(250, 100, -200, 0, 50, false, true), 3);
}

TEST(QmGameTimer, RaceClockUsesWarmupInsteadOfRoundStart)
{
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(249, 200, -100, 0, 50, true, false), 2);
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(249, 200, -100, 0, 50, false, false), 0);
}

TEST(QmGameTimer, RaceClockWidensBeforeAddingExtremeWarmup)
{
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(MAX_TICK, 0, MAX_TICK, 0, 50, true, false), 85899345);
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(MIN_TICK, 0, MIN_TICK, 0, 50, true, false), -85899345);
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(0, 0, MIN_TICK, 0, 50, true, false), -42949672);
}

TEST(QmGameTimer, RoundClockWidensBeforeSubtractingExtremeStart)
{
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(MIN_TICK, MAX_TICK, 0, 0, 50, false, false), -85899345);
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(MAX_TICK, MIN_TICK, 0, 0, 50, false, false), 85899345);
}

TEST(QmGameTimer, ExtremeTimeLimitStaysWideInCountdown)
{
	const auto Seconds = qm_race_timing::GameTimerSeconds(0, 0, 0, MAX_TICK, 50, false, false);
	EXPECT_EQ(Seconds, 128849018820LL);
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(MAX_TICK, 0, 0, MAX_TICK, 50, false, false), 128806069148LL);
}

TEST(QmGameTimer, CountdownWithFutureRoundStartDoesNotOverflow)
{
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(MIN_TICK, MAX_TICK, 0, MAX_TICK, 1, false, false), 133143986115LL);
}

TEST(QmGameTimer, NegativeRaceTimeKeepsTruncationTowardZero)
{
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(51, 0, -100, 0, 50, true, false), 0);
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(50, 0, -100, 0, 50, true, false), -1);
}

TEST(QmGameTimer, ExpiredCountdownRemainsNegativeForExistingCriticalDisplay)
{
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(3050, 0, 0, 1, 50, false, false), -1);
}

TEST(QmGameTimer, InvalidTickSpeedReturnsZeroWithoutDivision)
{
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(100, 0, 0, 1, 0, false, false), 0);
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(100, 0, -50, 0, -1, true, false), 0);
	EXPECT_EQ(qm_race_timing::GameTimerSeconds(100, 0, 0, 0, 0, false, false), 0);
}
