#include <game/map/render_screen.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using MapRenderScreen::ETileRangeMode;
using MapRenderScreen::TileRange;

TEST(MapRenderScreen, BufferedRangeKeepsFloorAndCeil)
{
	const auto Range = TileRange(-65.0f, -33.0f, 65.0f, 33.0f, 32.0f, ETileRangeMode::BUFFERED);
	ASSERT_TRUE(Range);
	EXPECT_EQ(Range->m_X0, -3);
	EXPECT_EQ(Range->m_Y0, -2);
	EXPECT_EQ(Range->m_X1, 3);
	EXPECT_EQ(Range->m_Y1, 2);
}

TEST(MapRenderScreen, ImmediateRangeKeepsTruncationAndOneTilePadding)
{
	const auto Range = TileRange(-65.0f, -33.0f, 65.0f, 33.0f, 32.0f, ETileRangeMode::IMMEDIATE);
	ASSERT_TRUE(Range);
	EXPECT_EQ(Range->m_X0, -3);
	EXPECT_EQ(Range->m_Y0, -2);
	EXPECT_EQ(Range->m_X1, 3);
	EXPECT_EQ(Range->m_Y1, 2);
}

TEST(MapRenderScreen, NegativeFractionalEndpointsRetainTheDifferentPathSemantics)
{
	const auto Buffered = TileRange(-31.0f, -31.0f, -1.0f, -1.0f, 32.0f, ETileRangeMode::BUFFERED);
	const auto Immediate = TileRange(-31.0f, -31.0f, -1.0f, -1.0f, 32.0f, ETileRangeMode::IMMEDIATE);
	ASSERT_TRUE(Buffered);
	ASSERT_TRUE(Immediate);
	EXPECT_EQ(Buffered->m_X1, 0);
	EXPECT_EQ(Immediate->m_X1, 1);
}

TEST(MapRenderScreen, NonFiniteCoordinatesNeverProduceAnIntegerRange)
{
	for(float Bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		SCOPED_TRACE(Bad);
		for(auto Mode : {ETileRangeMode::BUFFERED, ETileRangeMode::IMMEDIATE})
		{
			EXPECT_FALSE(TileRange(Bad, 0, 32, 32, 32, Mode));
			EXPECT_FALSE(TileRange(0, Bad, 32, 32, 32, Mode));
			EXPECT_FALSE(TileRange(0, 0, Bad, 32, 32, Mode));
			EXPECT_FALSE(TileRange(0, 0, 32, Bad, 32, Mode));
		}
	}
}

TEST(MapRenderScreen, NonPositiveOrNonFiniteScaleIsRejected)
{
	for(float Scale : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		SCOPED_TRACE(Scale);
		EXPECT_FALSE(TileRange(0, 0, 32, 32, Scale, ETileRangeMode::IMMEDIATE));
	}
}

TEST(MapRenderScreen, CollapsedAndInvertedRectanglesAreRejected)
{
	EXPECT_FALSE(TileRange(32, 0, 0, 32, 32, ETileRangeMode::BUFFERED));
	EXPECT_FALSE(TileRange(0, 32, 32, 0, 32, ETileRangeMode::IMMEDIATE));
	EXPECT_FALSE(TileRange(0, 0, 0, 32, 32, ETileRangeMode::BUFFERED));
	EXPECT_FALSE(TileRange(0, 0, 32, 0, 32, ETileRangeMode::IMMEDIATE));
}

TEST(MapRenderScreen, FiniteEndpointsWhoseExtentOverflowsAreRejected)
{
	constexpr float Max = std::numeric_limits<float>::max();
	EXPECT_FALSE(TileRange(-Max, 0, Max, 32, Max, ETileRangeMode::BUFFERED));
}

TEST(MapRenderScreen, FiniteCoordinatesOutsideTheIntegerDomainAreRejected)
{
	EXPECT_FALSE(TileRange(0, 0, 1.0e30f, 32, 32, ETileRangeMode::BUFFERED));
	EXPECT_FALSE(TileRange(-1.0e30f, 0, 32, 32, 32, ETileRangeMode::IMMEDIATE));
}

TEST(MapRenderScreen, DivisionOverflowIsRejectedBeforeRounding)
{
	EXPECT_FALSE(TileRange(0, 0, 32, 32, std::numeric_limits<float>::min(), ETileRangeMode::BUFFERED));
}

TEST(MapRenderScreen, IntegerMinimumIsRejectedBeforeBorderAbsoluteValue)
{
	const float Min = static_cast<float>(std::numeric_limits<int>::min());
	EXPECT_FALSE(TileRange(Min, 0, Min + 1024.0f, 32, 1, ETileRangeMode::BUFFERED));
}

TEST(MapRenderScreen, RoundedFloatIntegerMaximumCannotSlipThroughAFloatComparison)
{
	const float RoundedMax = static_cast<float>(std::numeric_limits<int>::max());
	EXPECT_FALSE(TileRange(0, 0, RoundedMax, 32, 1, ETileRangeMode::BUFFERED));
}

TEST(MapRenderScreen, PaddedIntegerBoundaryRetainsRepresentableCoordinates)
{
	const float BelowMax = std::nextafter(static_cast<float>(std::numeric_limits<int>::max()), 0.0f);
	const auto Range = TileRange(BelowMax - 1024, 0, BelowMax, 32, 1, ETileRangeMode::IMMEDIATE);
	ASSERT_TRUE(Range);
	EXPECT_EQ(Range->m_X1, static_cast<int>(BelowMax) + 1);
}

TEST(MapRenderScreen, IntervalDifferenceMustAlsoFitTheConsumerIntegerType)
{
	EXPECT_FALSE(TileRange(-2.0e9f, 0, 2.0e9f, 32, 1, ETileRangeMode::BUFFERED));
}

TEST(MapRenderScreen, LargeRepresentableTraversalIsNotGivenAnArbitraryBudget)
{
	const auto Range = TileRange(-1.0e9f, 0, 1.0e9f, 32, 1, ETileRangeMode::BUFFERED);
	ASSERT_TRUE(Range);
	EXPECT_EQ(Range->m_X1 - Range->m_X0, 2000000000);
}

TEST(MapRenderScreen, ARejectedRangeDoesNotPreventPreparingTheNextFrame)
{
	EXPECT_FALSE(TileRange(0, 0, std::numeric_limits<float>::infinity(), 32, 32, ETileRangeMode::BUFFERED));
	EXPECT_TRUE(TileRange(0, 0, 64, 64, 32, ETileRangeMode::BUFFERED));
}
