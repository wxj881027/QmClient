#include <game/client/components/tclient/qm_outline_viewport.h>

#include <gtest/gtest.h>

#include <array>
#include <limits>

namespace
{
	void ExpectRange(const std::optional<SQmOutlineTileRange> &Range, ivec2 Begin, ivec2 End)
	{
		ASSERT_TRUE(Range);
		EXPECT_EQ(Range->m_Begin, Begin);
		EXPECT_EQ(Range->m_End, End);
	}
}

TEST(QmOutlineViewport, OrdinaryViewKeepsItsOneTilePaddingOutsideTheMap)
{
	ExpectRange(QmCalcOutlineTileRange({0, 0}, {320, 160}, {1920, 1080}), {-1, -1}, {11, 6});
}

TEST(QmOutlineViewport, NegativeFractionalTilesKeepTruncationTowardZero)
{
	ExpectRange(QmCalcOutlineTileRange({-16, -48}, {16, 48}, {1920, 1080}), {-1, -2}, {1, 2});
}

TEST(QmOutlineViewport, OneOversizedAxisStillAdjustsBothAxes)
{
	ExpectRange(QmCalcOutlineTileRange({0, 0}, {3200, 64}, {120, 120}), {45, -4}, {55, 6});
}

TEST(QmOutlineViewport, OddDensityExcessRetainsTheExtraTile)
{
	ExpectRange(QmCalcOutlineTileRange({0, 0}, {352, 256}, {120, 120}), {0, -1}, {11, 9});
}

TEST(QmOutlineViewport, TileSpanBeyondSignedIntegerRangeStillCentersSafely)
{
	ExpectRange(QmCalcOutlineTileRange({-51200000000.0f, -51200000000.0f}, {51200000000.0f, 51200000000.0f}, {1920, 1080}), {-80, -45}, {80, 45});
}

TEST(QmOutlineViewport, CoordinatesBeyondIntegerDomainAreCenteredBeforeConversion)
{
	ExpectRange(QmCalcOutlineTileRange({-1.0e12f, -1.0e12f}, {1.0e12f, 1.0e12f}, {120, 120}), {-5, -5}, {5, 5});
}

TEST(QmOutlineViewport, NonfiniteScreenCoordinatesDoNotProduceARange)
{
	for(float Bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		SCOPED_TRACE(Bad);
		for(int Field = 0; Field < 4; ++Field)
		{
			SCOPED_TRACE(Field);
			std::array<float, 4> aBounds{0, 0, 100, 100};
			aBounds[Field] = Bad;
			EXPECT_FALSE(QmCalcOutlineTileRange({aBounds[0], aBounds[1]}, {aBounds[2], aBounds[3]}, {120, 120}));
		}
	}
}

TEST(QmOutlineViewport, FiniteOffCenterViewBeyondIntegerDomainIsSkipped)
{
	const float Max = std::numeric_limits<float>::max();
	EXPECT_FALSE(QmCalcOutlineTileRange({Max / 2, Max / 2}, {Max, Max}, {120, 120}));
	EXPECT_FALSE(QmCalcOutlineTileRange({-Max, -Max}, {-Max / 2, -Max / 2}, {120, 120}));
}

TEST(QmOutlineViewport, LowerIntegerEdgeMustLeaveRoomForNeighborSubtraction)
{
	const float Edge = static_cast<float>(std::numeric_limits<int>::min()) * 32.0f;
	EXPECT_FALSE(QmCalcOutlineTileRange({Edge, 0}, {Edge, 32}, {120, 120}));
}

TEST(QmOutlineViewport, RoundedUpperIntegerEdgeMustLeaveRoomForNeighborAddition)
{
	const float Edge = static_cast<float>(std::numeric_limits<int>::max()) * 32.0f;
	EXPECT_FALSE(QmCalcOutlineTileRange({Edge, 0}, {Edge, 32}, {120, 120}));
}

TEST(QmOutlineViewport, EmptyOrNegativePixelDimensionsDoNotProduceARange)
{
	for(const ivec2 Pixels : {ivec2(0, 120), ivec2(120, 0), ivec2(-1, 120), ivec2(120, -1)})
	{
		SCOPED_TRACE(Pixels.x);
		SCOPED_TRACE(Pixels.y);
		EXPECT_FALSE(QmCalcOutlineTileRange({0, 0}, {100, 100}, Pixels));
	}
}

TEST(QmOutlineViewport, ReversedAxesDoNotProduceARange)
{
	EXPECT_FALSE(QmCalcOutlineTileRange({100, 0}, {0, 100}, {120, 120}));
	EXPECT_FALSE(QmCalcOutlineTileRange({0, 100}, {100, 0}, {120, 120}));
}

TEST(QmOutlineViewport, SmallPixelBudgetKeepsTheOriginalOddSingleTile)
{
	ExpectRange(QmCalcOutlineTileRange({0, 0}, {32, 32}, {1, 1}), {0, 0}, {1, 1});
}

TEST(QmOutlineViewport, ZeroWorldExtentAndSmallPixelBudgetKeepAnEmptyRange)
{
	ExpectRange(QmCalcOutlineTileRange({0, 0}, {0, 0}, {1, 1}), {0, 0}, {0, 0});
}
