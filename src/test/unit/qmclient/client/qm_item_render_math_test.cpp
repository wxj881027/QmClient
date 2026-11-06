#include <game/client/components/qmclient/item_render_math.h>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <vector>

TEST(QmLaserFade, PositiveDelayKeepsExistingTimeConversion)
{
	EXPECT_FLOAT_EQ(QmLaserFadeProgress(0.0f, 50, 150.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmLaserFadeProgress(3.75f, 50, 150.0f), 0.5f);
	EXPECT_FLOAT_EQ(QmLaserFadeProgress(7.5f, 50, 150.0f), 1.0f);
}

TEST(QmLaserFade, ClampsOrdinaryPastAndFutureTimes)
{
	EXPECT_FLOAT_EQ(QmLaserFadeProgress(-1.0f, 50, 150.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmLaserFadeProgress(100.0f, 50, 150.0f), 1.0f);
}

TEST(QmLaserFade, ZeroDelayImmediatelyCompletesEvenAtFirstFrame)
{
	for(float Tick : {-1.0f, 0.0f, 1.0f})
		EXPECT_FLOAT_EQ(QmLaserFadeProgress(Tick, 50, 0.0f), 1.0f);
}

TEST(QmLaserFade, NegativeZeroDelayAlsoCompletesImmediately)
{
	EXPECT_FLOAT_EQ(QmLaserFadeProgress(0.0f, 50, -0.0f), 1.0f);
}

TEST(QmLaserFade, NonzeroNegativeDelayRetainsPreviousFormula)
{
	EXPECT_FLOAT_EQ(QmLaserFadeProgress(3.75f, 50, -150.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmLaserFadeProgress(-3.75f, 50, -150.0f), 0.5f);
}

TEST(QmItemTickDelta, PreservesOrdinaryElapsedAndFutureTicks)
{
	EXPECT_FLOAT_EQ(QmItemTickDelta(150, 100), 50.0f);
	EXPECT_FLOAT_EQ(QmItemTickDelta(100, 100), 0.0f);
	EXPECT_FLOAT_EQ(QmItemTickDelta(100, 101), -1.0f);
	EXPECT_FLOAT_EQ(QmItemTickDelta(150, 100, -1), 49.0f);
}

TEST(QmItemTickDelta, WidensBeforeSubtractingExtremeStartTicks)
{
	EXPECT_FLOAT_EQ(QmItemTickDelta(std::numeric_limits<int>::max(), std::numeric_limits<int>::min()), 4294967296.0f);
	EXPECT_FLOAT_EQ(QmItemTickDelta(std::numeric_limits<int>::min(), std::numeric_limits<int>::max()), -4294967296.0f);
}

TEST(QmItemTickDelta, WidensBeforeApplyingOffset)
{
	EXPECT_FLOAT_EQ(QmItemTickDelta(std::numeric_limits<int>::min(), std::numeric_limits<int>::min(), -1), -1.0f);
	EXPECT_FLOAT_EQ(QmItemTickDelta(std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), 1), 1.0f);
}

TEST(QmLaserSplatFrame, RetainsNormalTruncationAndAnimationPeriod)
{
	const auto First = QmLaserSplatFrame(5.75f);
	EXPECT_EQ(First.m_Index, 2);
	EXPECT_FLOAT_EQ(First.m_Rotation, 5.0f);
	EXPECT_EQ(QmLaserSplatFrame(8.25f).m_Index, First.m_Index);
}

TEST(QmLaserSplatFrame, NormalizesNegativeRemainder)
{
	EXPECT_EQ(QmLaserSplatFrame(-1.75f).m_Index, 2);
	EXPECT_EQ(QmLaserSplatFrame(-2.75f).m_Index, 1);
	EXPECT_EQ(QmLaserSplatFrame(-3.75f).m_Index, 0);
	EXPECT_FLOAT_EQ(QmLaserSplatFrame(-1.75f).m_Rotation, -1.0f);
	EXPECT_EQ(QmLaserSplatFrame(-0.75f).m_Index, 0);
}

TEST(QmLaserSplatFrame, HandlesTickRoundedAboveIntMaximum)
{
	const auto Frame = QmLaserSplatFrame(2147483648.0f);
	EXPECT_EQ(Frame.m_Index, 2);
	EXPECT_FLOAT_EQ(Frame.m_Rotation, 2147483648.0f);
}

TEST(QmLaserSplatFrame, FiniteExtremeAlwaysSelectsExistingSprite)
{
	for(float Tick : {std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()})
	{
		SCOPED_TRACE(Tick);
		const auto Frame = QmLaserSplatFrame(Tick);
		EXPECT_GE(Frame.m_Index, 0);
		EXPECT_LT(Frame.m_Index, 3);
		EXPECT_TRUE(std::isfinite(Frame.m_Rotation));
	}
}

TEST(QmLaserSplatFrame, NonFiniteTickUsesSafeFrameAndRotation)
{
	for(float Tick : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		const auto Frame = QmLaserSplatFrame(Tick);
		EXPECT_EQ(Frame.m_Index, 0);
		EXPECT_FLOAT_EQ(Frame.m_Rotation, 0.0f);
	}
}

TEST(QmItemTrailRange, PreservesOpenSamplingEndpoints)
{
	const auto Range = QmItemTrailRange(0.25f, 1.0f, 0.125f);
	EXPECT_EQ(Range.m_First, 3);
	EXPECT_EQ(Range.m_End, 8);
}

TEST(QmItemTrailRange, NegativeFractionStillTruncatesTowardZero)
{
	const auto Range = QmItemTrailRange(-0.1875f, 0.5f, 0.125f);
	EXPECT_EQ(Range.m_First, 0);
	EXPECT_EQ(Range.m_End, 4);
}

TEST(QmItemTrailRange, SupportsCountersAboveIntMaximum)
{
	const auto Range = QmItemTrailRange(2147483648.0f, 2147484160.0f, 1.0f);
	EXPECT_EQ(Range.m_First, INT64_C(2147483649));
	EXPECT_EQ(Range.m_End, INT64_C(2147484160));
}

TEST(QmItemTrailRange, RejectsNonPositiveAndNonFiniteStep)
{
	for(float Step : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		const auto Range = QmItemTrailRange(0.0f, 1.0f, Step);
		EXPECT_EQ(Range.m_First, Range.m_End);
	}
}

TEST(QmItemTrailRange, RejectsNonFiniteTimes)
{
	for(float Time : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		const auto From = QmItemTrailRange(Time, 1.0f, 1.0f);
		const auto To = QmItemTrailRange(0.0f, Time, 1.0f);
		EXPECT_EQ(From.m_First, From.m_End);
		EXPECT_EQ(To.m_First, To.m_End);
	}
}

TEST(QmItemTrailRange, RejectsOverflowedQuotient)
{
	const auto Range = QmItemTrailRange(0.0f, std::numeric_limits<float>::max(), std::numeric_limits<float>::min());
	EXPECT_EQ(Range.m_First, Range.m_End);
}

TEST(QmItemTrailRange, RejectsRoundedInt64UpperBoundary)
{
	constexpr float Upper = 9223372036854775808.0f;
	const auto From = QmItemTrailRange(Upper, 0.0f, 1.0f);
	const auto To = QmItemTrailRange(0.0f, Upper, 1.0f);
	EXPECT_EQ(From.m_First, From.m_End);
	EXPECT_EQ(To.m_First, To.m_End);
}

TEST(QmItemTrailRange, AcceptsInt64LowerBoundaryBeforeIncrement)
{
	const auto Range = QmItemTrailRange(-9223372036854775808.0f, 0.0f, 1.0f);
	EXPECT_EQ(Range.m_First, std::numeric_limits<int64_t>::min() + 1);
	EXPECT_EQ(Range.m_End, 0);
	const auto Invalid = QmItemTrailRange(std::nextafter(-9223372036854775808.0f, -std::numeric_limits<float>::infinity()), 0.0f, 1.0f);
	EXPECT_EQ(Invalid.m_First, Invalid.m_End);
}

TEST(QmItemTrailSamples, VisitsAcceptedSamplesInOrder)
{
	std::vector<int64_t> vSamples;
	QmForEachItemTrailSample(QmItemTrailRange(0.25f, 1.0f, 0.125f), [&](int64_t Index) {
		vSamples.push_back(Index);
		return true;
	});
	EXPECT_EQ(vSamples, (std::vector<int64_t>{3, 4, 5, 6, 7}));
}

TEST(QmItemTrailSamples, StopsAtFirstRejectedSample)
{
	std::vector<int64_t> vSamples;
	QmForEachItemTrailSample({3, 100}, [&](int64_t Index) {
		vSamples.push_back(Index);
		return Index < 5;
	});
	EXPECT_EQ(vSamples, (std::vector<int64_t>{3, 4, 5}));
}

TEST(QmItemTrailSamples, EmptyAndReversedRangesDoNotEmit)
{
	int Calls = 0;
	for(const auto Range : std::array<SQmItemTrailRange, 3>{{{}, {5, 5}, {8, 2}}})
		QmForEachItemTrailSample(Range, [&](int64_t) { ++Calls; return true; });
	EXPECT_EQ(Calls, 0);
}

TEST(QmItemTrailSamples, RejectionEndsAnExtremeRangeImmediately)
{
	int Calls = 0;
	QmForEachItemTrailSample({std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max()}, [&](int64_t Index) {
		++Calls;
		EXPECT_EQ(Index, std::numeric_limits<int64_t>::min());
		return false;
	});
	EXPECT_EQ(Calls, 1);
}
