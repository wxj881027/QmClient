#include <game/client/components/qmclient/race_timing.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
	float Width(int64_t Centiseconds)
	{
		return qm_race_timing::GameTimerTextWidth(Centiseconds, 10.0f, 20.0f, 30.0f, 40.0f, 50.0f);
	}
}

TEST(QmGameTimerWidth, HourWidthStartsAtOneHourInCentiseconds)
{
	EXPECT_FLOAT_EQ(Width(359999), 10.0f);
	EXPECT_FLOAT_EQ(Width(360000), 20.0f);
}

TEST(QmGameTimerWidth, DayWidthStartsAtOneDayInCentiseconds)
{
	EXPECT_FLOAT_EQ(Width(8639999), 20.0f);
	EXPECT_FLOAT_EQ(Width(8640000), 30.0f);
}

TEST(QmGameTimerWidth, TwoDigitDayWidthStartsAtTenDays)
{
	EXPECT_FLOAT_EQ(Width(86399999), 30.0f);
	EXPECT_FLOAT_EQ(Width(86400000), 40.0f);
}

TEST(QmGameTimerWidth, ThreeDigitDayWidthStartsAtOneHundredDays)
{
	EXPECT_FLOAT_EQ(Width(863999999), 40.0f);
	EXPECT_FLOAT_EQ(Width(864000000), 50.0f);
}

TEST(QmGameTimerWidth, RankCentisecondsDoNotTriggerTheOldSecondsThresholds)
{
	EXPECT_FLOAT_EQ(Width(3600), 10.0f);
	EXPECT_FLOAT_EQ(Width(86400), 10.0f);
	EXPECT_FLOAT_EQ(Width(864000), 20.0f);
}

TEST(QmGameTimerWidth, NonpositiveTimesKeepMinuteWidthWithoutNegating)
{
	EXPECT_FLOAT_EQ(Width(0), 10.0f);
	EXPECT_FLOAT_EQ(Width(-1), 10.0f);
	EXPECT_FLOAT_EQ(Width(std::numeric_limits<int64_t>::min()), 10.0f);
}

TEST(QmGameTimerWidth, LargeWideTimesUseTheLargestExistingWidth)
{
	EXPECT_FLOAT_EQ(Width(std::numeric_limits<int64_t>::max()), 50.0f);
}
