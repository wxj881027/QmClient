#include <game/client/components/qmclient/checkpoint_guide_geometry.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

TEST(QmCheckpointGuideGeometry, OrdinaryWorldPositionsAndRestrictionProbesRemainUsable)
{
	for(const vec2 Position : {vec2(0, 0), vec2(-0.5f, 31.5f), vec2(16, 16), vec2(4096, -256)})
	{
		EXPECT_TRUE(QmCheckpointGuide::IsSafeCollisionPosition(Position));
		EXPECT_TRUE(QmCheckpointGuide::IsSafeCollisionPosition(Position, QmCheckpointGuide::MOVE_RESTRICTION_DISTANCE));
	}
}

TEST(QmCheckpointGuideGeometry, NonFiniteOrIntegerOverflowingCoordinatesAreRejectedOnEitherAxis)
{
	const float Infinity = std::numeric_limits<float>::infinity();
	const float Minimum = static_cast<float>(std::numeric_limits<int>::min());
	for(const float Value : {Infinity, -Infinity, std::numeric_limits<float>::quiet_NaN(),
		    static_cast<float>(std::numeric_limits<int>::max()), std::nextafter(Minimum, -Infinity), std::numeric_limits<float>::max()})
	{
		SCOPED_TRACE(Value);
		EXPECT_FALSE(QmCheckpointGuide::IsSafeCollisionPosition({Value, 0}));
		EXPECT_FALSE(QmCheckpointGuide::IsSafeCollisionPosition({0, Value}));
		EXPECT_FALSE(QmCheckpointGuide::IsSafeCollisionPosition({Value, 0}, QmCheckpointGuide::MOVE_RESTRICTION_DISTANCE));
	}
}

TEST(QmCheckpointGuideGeometry, NearestRepresentableSafeCoordinatesKeepTheirRestrictionProbes)
{
	const float Maximum = std::nextafter(static_cast<float>(std::numeric_limits<int>::max()), 0.0f);
	const float Minimum = static_cast<float>(std::numeric_limits<int>::min());
	EXPECT_TRUE(QmCheckpointGuide::IsSafeCollisionPosition({Maximum, Minimum}));
	EXPECT_TRUE(QmCheckpointGuide::IsSafeCollisionPosition({Maximum, Minimum}, QmCheckpointGuide::MOVE_RESTRICTION_DISTANCE));
}

TEST(QmCheckpointGuideGeometry, UnsupportedProbeDistancesCannotReachCollision)
{
	for(const float Distance : {-1.0f, std::nextafter(32.0f, 33.0f), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		SCOPED_TRACE(Distance);
		EXPECT_FALSE(QmCheckpointGuide::IsSafeCollisionPosition({16, 16}, Distance));
	}
	EXPECT_TRUE(QmCheckpointGuide::IsSafeCollisionPosition({16, 16}, 32.0f));
}

TEST(QmCheckpointGuideGeometry, TileCentersRetainTheExistingMapCoordinatesAndProbeDomain)
{
	const auto First = QmCheckpointGuide::TileCenter(7, 3, 0);
	const auto Middle = QmCheckpointGuide::TileCenter(7, 3, 8);
	const auto Last = QmCheckpointGuide::TileCenter(7, 3, 20);
	ASSERT_TRUE(First.has_value());
	ASSERT_TRUE(Middle.has_value());
	ASSERT_TRUE(Last.has_value());
	EXPECT_EQ(*First, vec2(16, 16));
	EXPECT_EQ(*Middle, vec2(48, 48));
	EXPECT_EQ(*Last, vec2(208, 80));
	for(const vec2 Center : {*First, *Middle, *Last})
		EXPECT_TRUE(QmCheckpointGuide::IsSafeCollisionPosition(Center, QmCheckpointGuide::MOVE_RESTRICTION_DISTANCE));
}

TEST(QmCheckpointGuideGeometry, InvalidMapDimensionsOrIndicesHaveNoTileCenter)
{
	EXPECT_FALSE(QmCheckpointGuide::TileCenter(0, 1, 0));
	EXPECT_FALSE(QmCheckpointGuide::TileCenter(1, 0, 0));
	EXPECT_FALSE(QmCheckpointGuide::TileCenter(-1, 1, 0));
	EXPECT_FALSE(QmCheckpointGuide::TileCenter(1, -1, 0));
	EXPECT_FALSE(QmCheckpointGuide::TileCenter(7, 3, -1));
	EXPECT_FALSE(QmCheckpointGuide::TileCenter(7, 3, 21));
	EXPECT_FALSE(QmCheckpointGuide::TileCenter(std::numeric_limits<int>::max(), 2, 0));
}

TEST(QmCheckpointGuideGeometry, LargeNarrowMapsRejectUnsafeCentersWithoutAllocatingTheMap)
{
	constexpr int Width = 67108865;
	ASSERT_TRUE(QmCheckpointGuide::TileCenter(Width, 1, 0));
	EXPECT_FALSE(QmCheckpointGuide::TileCenter(Width, 1, Width - 1));
	EXPECT_FALSE(QmCheckpointGuide::TileCenter(1, Width, Width - 1));
	// 该中心的整数坐标仍在 int 范围，转成 float 后已舍入到 2^31。
	EXPECT_FALSE(QmCheckpointGuide::TileCenter(Width, 1, 67108863));
	EXPECT_TRUE(QmCheckpointGuide::TileCenter(std::numeric_limits<int>::max(), 1, 0));
	EXPECT_FALSE(QmCheckpointGuide::TileCenter(std::numeric_limits<int>::max(), 1, std::numeric_limits<int>::max() - 1));
}
