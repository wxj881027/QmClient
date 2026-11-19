#include <game/client/components/envelope_state.h>

#include <gtest/gtest.h>

#include <limits>

using namespace std::chrono_literals;

TEST(EnvelopeStateTime, SnapshotSamplingKeepsTickSpacingAndMapOffset)
{
	const auto Sample = EnvelopeStateTime::OnlineSampleTime(990, 1002, 1000, 0.25, false, -2);
	ASSERT_TRUE(Sample);
	EXPECT_EQ(*Sample, 208ms);
}

TEST(EnvelopeStateTime, PredictionSamplesBetweenThePreviousAndPredictedTick)
{
	const auto Sample = EnvelopeStateTime::OnlineSampleTime(990, 1002, 0, 0.25, true, 0);
	ASSERT_TRUE(Sample);
	EXPECT_EQ(*Sample, 225ms);
}

TEST(EnvelopeStateTime, RoundStartDifferenceCanExceedTheSnapshotIntegerRange)
{
	const auto Sample = EnvelopeStateTime::OnlineSampleTime(std::numeric_limits<int>::min(), 101, 100, 0.5, false, 0);
	ASSERT_TRUE(Sample);
	EXPECT_EQ(*Sample, 42949674970ms);
}

TEST(EnvelopeStateTime, TickSpacingIsWidenedBeforeInterpolation)
{
	const auto Sample = EnvelopeStateTime::OnlineSampleTime(0, std::numeric_limits<int>::max(), std::numeric_limits<int>::min(), 0.5, false, 0);
	ASSERT_TRUE(Sample);
	EXPECT_EQ(*Sample, -10ms);
}

TEST(EnvelopeStateTime, InvalidFractionDoesNotProduceASample)
{
	EXPECT_FALSE(EnvelopeStateTime::OnlineSampleTime(0, 1, 0, std::numeric_limits<double>::infinity(), false, 0));
	EXPECT_FALSE(EnvelopeStateTime::OnlineSampleTime(0, 1, 0, std::numeric_limits<double>::quiet_NaN(), true, 0));
}

TEST(EnvelopeStateTime, OutOfRangeNanosecondSumDoesNotProduceASample)
{
	EXPECT_FALSE(EnvelopeStateTime::OnlineSampleTime(std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), std::numeric_limits<int>::max() - 1, 4.6e11, false, 0));
}
