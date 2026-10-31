#include <game/client/components/qmclient/collision_position.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

TEST(CollisionLineDomain, AcceptsOrdinaryOffMapAndStationaryLines)
{
	EXPECT_TRUE(QmCollision::CanQueryLine(vec2(0, 0), vec2(100, -200)));
	EXPECT_TRUE(QmCollision::CanQueryLine(vec2(-100000, 100000), vec2(-99000, 101000)));
	EXPECT_TRUE(QmCollision::CanQueryLine(vec2(-2147483648.0f, 0), vec2(-2147483648.0f, 0)));
}

TEST(CollisionLineDomain, RejectsNonfiniteAndUnrepresentableEndpoints)
{
	const float Infinity = std::numeric_limits<float>::infinity();
	const float Nan = std::numeric_limits<float>::quiet_NaN();
	for(const vec2 Invalid : {vec2(Infinity, 0), vec2(0, -Infinity), vec2(Nan, 0), vec2(0, Nan), vec2(2147483648.0f, 0)})
	{
		EXPECT_FALSE(QmCollision::CanQueryLine(Invalid, vec2(0, 0)));
		EXPECT_FALSE(QmCollision::CanQueryLine(vec2(0, 0), Invalid));
	}
}

TEST(CollisionLineDomain, RejectsUnrepresentableStepCountWithValidEndpoints)
{
	const vec2 From(-1073741824.0f, 0), To(1073741824.0f, 0);
	ASSERT_TRUE(QmCollision::CanRoundPosition(From));
	ASSERT_TRUE(QmCollision::CanRoundPosition(To));
	EXPECT_FALSE(QmCollision::CanQueryLine(From, To));
}

TEST(CollisionLineDomain, AcceptsRepresentableLargeCountWithoutTraversingIt)
{
	// 此处仅调用常数时间的数值检查，不执行碰撞步进，也不证明帧预算。
	EXPECT_TRUE(QmCollision::CanQueryLine(vec2(0, 0), vec2(std::nextafter(2147483648.0f, 0.0f), 0)));
}

TEST(CollisionLineDomain, RejectsRoundedMixEndpointEvenWhenBothInputsAreValid)
{
	const vec2 From(192.0f, 0), To(std::nextafter(2147483648.0f, 0.0f), 0);
	ASSERT_TRUE(QmCollision::CanRoundPosition(From));
	ASSERT_TRUE(QmCollision::CanRoundPosition(To));
	ASSERT_FALSE(QmCollision::CanRoundPosition(mix(From, To, 1.0f)));
	EXPECT_FALSE(QmCollision::CanQueryLine(From, To));
}
