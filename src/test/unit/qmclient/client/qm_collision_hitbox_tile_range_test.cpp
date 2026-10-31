#include <game/client/components/qmclient/collision_hitbox_logic.h>

#include <gtest/gtest.h>

#include <array>
#include <initializer_list>
#include <limits>
#include <optional>

namespace
{
	void ExpectRange(const std::optional<SQmHitboxTileRange> &Range, ivec2 Begin, ivec2 End)
	{
		ASSERT_TRUE(Range);
		EXPECT_EQ(Range->m_Begin, Begin);
		EXPECT_EQ(Range->m_End, End);
	}
}

TEST(QmHitboxTileRange, OrdinaryViewportKeepsOneTilePadding)
{
	ExpectRange(QmCalcHitboxTileRange(vec2(64, 96), vec2(160, 192), ivec2(1200, 600), ivec2(100, 100)), ivec2(1, 2), ivec2(6, 7));
}

TEST(QmHitboxTileRange, NegativeViewportKeepsTruncationTowardZero)
{
	ExpectRange(QmCalcHitboxTileRange(vec2(-65, -65), vec2(-1, -1), ivec2(1200, 600), ivec2(100, 100)), ivec2(0, 0), ivec2(1, 1));
}

TEST(QmHitboxTileRange, ClippingRemovesOnlyMapOutsideTraversal)
{
	ExpectRange(QmCalcHitboxTileRange(vec2(64, 64), vec2(10000, 10000), ivec2(12000, 12000), ivec2(4, 3)), ivec2(1, 1), ivec2(4, 3));
	ExpectRange(QmCalcHitboxTileRange(vec2(1000, 1000), vec2(2000, 2000), ivec2(1200, 600), ivec2(4, 3)), ivec2(4, 3), ivec2(4, 3));
}

TEST(QmHitboxTileRange, OneOversizedAxisStillAdjustsBothAxes)
{
	ExpectRange(QmCalcHitboxTileRange(vec2(0, 0), vec2(640, 160), ivec2(120, 120), ivec2(100, 100)), ivec2(5, 0), ivec2(15, 7));
}

TEST(QmHitboxTileRange, OddDensityDifferenceRetainsTheExtraTile)
{
	ExpectRange(QmCalcHitboxTileRange(vec2(0, 0), vec2(608, 320), ivec2(120, 120), ivec2(100, 100)), ivec2(4, 0), ivec2(15, 10));
}

TEST(QmHitboxTileRange, InvalidScreenAndDimensionsDoNotProduceIntegerBounds)
{
	const float Nan = std::numeric_limits<float>::quiet_NaN();
	const float Inf = std::numeric_limits<float>::infinity();
	for(const vec2 Invalid : {vec2(Nan, 0), vec2(0, Nan), vec2(Inf, 0), vec2(0, -Inf)})
	{
		EXPECT_FALSE(QmCalcHitboxTileRange(Invalid, vec2(100, 100), ivec2(120, 120), ivec2(100, 100)));
		EXPECT_FALSE(QmCalcHitboxTileRange(vec2(0, 0), Invalid, ivec2(120, 120), ivec2(100, 100)));
	}
	EXPECT_FALSE(QmCalcHitboxTileRange(vec2(100, 0), vec2(0, 100), ivec2(120, 120), ivec2(100, 100)));
	for(const ivec2 Invalid : {ivec2(0, 100), ivec2(100, 0), ivec2(-1, 100), ivec2(100, -1)})
	{
		EXPECT_FALSE(QmCalcHitboxTileRange(vec2(0, 0), vec2(100, 100), Invalid, ivec2(100, 100)));
		EXPECT_FALSE(QmCalcHitboxTileRange(vec2(0, 0), vec2(100, 100), ivec2(120, 120), Invalid));
	}
}

TEST(QmHitboxTileRange, ExtremeFiniteViewportEitherSkipsOrKeepsMapAndDensityBounds)
{
	const float Max = std::numeric_limits<float>::max();
	const std::array<std::array<vec2, 2>, 3> aScreens = {{
		{vec2(-Max, -Max), vec2(Max, Max)},
		{vec2(Max / 2, Max / 2), vec2(Max, Max)},
		{vec2(-Max, -Max), vec2(-Max / 2, -Max / 2)},
	}};
	for(const auto &Screen : aScreens)
	{
		const auto Range = QmCalcHitboxTileRange(Screen[0], Screen[1], ivec2(120, 120), ivec2(100000, 100000));
		if(!Range)
			continue;
		EXPECT_GE(Range->m_Begin.x, 0);
		EXPECT_GE(Range->m_Begin.y, 0);
		EXPECT_GE(Range->m_End.x, Range->m_Begin.x);
		EXPECT_GE(Range->m_End.y, Range->m_Begin.y);
		EXPECT_LE(Range->m_End.x, 100000);
		EXPECT_LE(Range->m_End.y, 100000);
		EXPECT_LE(Range->m_End.x - Range->m_Begin.x, 11);
		EXPECT_LE(Range->m_End.y - Range->m_Begin.y, 11);
	}
}

TEST(QmHitboxTileRange, ViewportBeyondIntegerTileDomainStillCentersBeforeClipping)
{
	ExpectRange(QmCalcHitboxTileRange(vec2(-1.0e12f, -1.0e12f), vec2(1.0e12f, 1.0e12f), ivec2(120, 120), ivec2(100, 100)), ivec2(0, 0), ivec2(5, 5));
}
