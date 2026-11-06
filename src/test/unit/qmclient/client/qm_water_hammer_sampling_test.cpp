#include <game/client/components/qmclient/water_hammer_indicator_logic.h>

#include <gtest/gtest.h>

#include <array>
#include <initializer_list>
#include <limits>

TEST(QmWaterHammerSampling, InvalidCentersDoNotInvokeCollisionQueries)
{
	const float Nan = std::numeric_limits<float>::quiet_NaN();
	const float Inf = std::numeric_limits<float>::infinity();
	const float DecodedMax = static_cast<float>(std::numeric_limits<int>::max());
	for(vec2 Position : {vec2(Nan, 0), vec2(0, Inf), vec2(-Inf, 0), vec2(DecodedMax, 0), vec2(0, DecodedMax)})
	{
		int Queries = 0;
		EXPECT_FALSE(QmSampleWaterHammerPenaltyArea(Position, [&](vec2) {
			++Queries;
			return true;
		}));
		EXPECT_EQ(Queries, 0);
	}
}

TEST(QmWaterHammerSampling, EmptyAreaQueriesTheCenterThenTheFourOriginalCorners)
{
	std::array<vec2, 5> aPositions{};
	int Queries = 0;
	EXPECT_FALSE(QmSampleWaterHammerPenaltyArea(vec2(100, 200), [&](vec2 Position) {
		if(Queries < 5)
			aPositions[Queries] = Position;
		++Queries;
		return false;
	}));
	ASSERT_EQ(Queries, 5);
	EXPECT_EQ(aPositions[0], vec2(100, 200));
	EXPECT_NEAR(aPositions[1].x, 90.66667f, 0.0001f);
	EXPECT_NEAR(aPositions[1].y, 190.66667f, 0.0001f);
	EXPECT_NEAR(aPositions[2].x, 109.33333f, 0.0001f);
	EXPECT_NEAR(aPositions[2].y, 190.66667f, 0.0001f);
	EXPECT_NEAR(aPositions[3].x, 90.66667f, 0.0001f);
	EXPECT_NEAR(aPositions[3].y, 209.33333f, 0.0001f);
	EXPECT_NEAR(aPositions[4].x, 109.33333f, 0.0001f);
	EXPECT_NEAR(aPositions[4].y, 209.33333f, 0.0001f);
}

TEST(QmWaterHammerSampling, AHitStopsFurtherSampling)
{
	for(int Hit = 1; Hit <= 5; ++Hit)
	{
		int Queries = 0;
		EXPECT_TRUE(QmSampleWaterHammerPenaltyArea(vec2(100, 200), [&](vec2) { return ++Queries == Hit; }));
		EXPECT_EQ(Queries, Hit);
	}
}

TEST(QmWaterHammerSampling, InvalidSampleDoesNotPreventTheNextValidQuery)
{
	int Queries = 0;
	auto Query = [&](vec2) {
		++Queries;
		return true;
	};
	EXPECT_FALSE(QmSampleWaterHammerPenaltyArea(vec2(std::numeric_limits<float>::infinity(), 0), Query));
	EXPECT_TRUE(QmSampleWaterHammerPenaltyArea(vec2(100, 200), Query));
	EXPECT_EQ(Queries, 1);
}
