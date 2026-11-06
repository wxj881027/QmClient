#include <game/client/components/qmclient/rank_view_camera.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using qmclient::rank_ghost::PanFreeCamera;

TEST(QmRankViewCamera, NormalPanKeepsBothAxisDirections)
{
	vec2 Center(10.0f, 20.0f);
	ASSERT_TRUE(PanFreeCamera(Center, vec2(2.0f, -4.0f), 1000.0f, 100));
	EXPECT_EQ(Center, vec2(30.0f, -20.0f));
}

TEST(QmRankViewCamera, FractionalPanKeepsOriginalFloatResult)
{
	vec2 Center(10.1f, -20.3f);
	const vec2 Delta(0.7f, -0.3f);
	const vec2 Expected = Center + Delta * (1875.0f / 1920.0f);
	ASSERT_TRUE(PanFreeCamera(Center, Delta, 1875.0f, 1920));
	EXPECT_EQ(Center, Expected);
}

TEST(QmRankViewCamera, InfiniteWorldWidthDoesNotPoisonTheZeroDeltaAxis)
{
	vec2 Center(128.0f, 256.0f);
	EXPECT_FALSE(PanFreeCamera(Center, vec2(0.0f, 1.0f), std::numeric_limits<float>::infinity(), 1920));
	EXPECT_EQ(Center, vec2(128.0f, 256.0f));
}

TEST(QmRankViewCamera, InvalidWorldWidthsPreserveTheWholeCenter)
{
	for(float Width : {0.0f, -1.0f, -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		SCOPED_TRACE(Width);
		vec2 Center(128.0f, 256.0f);
		EXPECT_FALSE(PanFreeCamera(Center, vec2(1.0f, 2.0f), Width, 1920));
		EXPECT_EQ(Center, vec2(128.0f, 256.0f));
	}
}

TEST(QmRankViewCamera, NonFiniteMouseAxisDoesNotPartiallyMoveTheOtherAxis)
{
	for(float Bad : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		for(vec2 Delta : {vec2(Bad, 2.0f), vec2(1.0f, Bad)})
		{
			SCOPED_TRACE(Bad);
			vec2 Center(128.0f, 256.0f);
			EXPECT_FALSE(PanFreeCamera(Center, Delta, 1500.0f, 1500));
			EXPECT_EQ(Center, vec2(128.0f, 256.0f));
		}
	}
}

TEST(QmRankViewCamera, FiniteWidthWithOverflowingDeltaPreservesBothAxes)
{
	vec2 Center(128.0f, 256.0f);
	EXPECT_FALSE(PanFreeCamera(Center, vec2(2.0f, 0.5f), std::numeric_limits<float>::max(), 1));
	EXPECT_EQ(Center, vec2(128.0f, 256.0f));
}

TEST(QmRankViewCamera, OverflowingAdditionDoesNotPublishAnOtherwiseValidAxis)
{
	constexpr float Max = std::numeric_limits<float>::max();
	vec2 Center(Max, 256.0f);
	EXPECT_FALSE(PanFreeCamera(Center, vec2(Max, 1.0f), 1500.0f, 1500));
	EXPECT_EQ(Center, vec2(Max, 256.0f));
}

TEST(QmRankViewCamera, ARepresentableLargePanHasNoArtificialCoordinateLimit)
{
	constexpr float Max = std::numeric_limits<float>::max();
	vec2 Center(0.0f, 0.0f);
	ASSERT_TRUE(PanFreeCamera(Center, vec2(0.25f, -0.5f), Max, 1));
	EXPECT_EQ(Center, vec2(Max * 0.25f, -Max * 0.5f));
}

TEST(QmRankViewCamera, NonPositiveWindowWidthKeepsTheExistingOnePixelFallback)
{
	for(int Width : {0, -1})
	{
		SCOPED_TRACE(Width);
		vec2 Center(10.0f, 20.0f);
		ASSERT_TRUE(PanFreeCamera(Center, vec2(1.0f, -2.0f), 100.0f, Width));
		EXPECT_EQ(Center, vec2(110.0f, -180.0f));
	}
}

TEST(QmRankViewCamera, FailedPanDoesNotBlockTheNextValidPan)
{
	vec2 Center(128.0f, 256.0f);
	EXPECT_FALSE(PanFreeCamera(Center, vec2(0.0f, 1.0f), std::numeric_limits<float>::infinity(), 1920));
	ASSERT_TRUE(PanFreeCamera(Center, vec2(3.0f, -4.0f), 1000.0f, 1000));
	EXPECT_EQ(Center, vec2(131.0f, 252.0f));
}

TEST(QmRankViewCamera, AlreadyInvalidCenterIsRejectedWithoutRepairingOtherState)
{
	vec2 Center(std::numeric_limits<float>::quiet_NaN(), 256.0f);
	EXPECT_FALSE(PanFreeCamera(Center, vec2(1.0f, 2.0f), 1000.0f, 1000));
	EXPECT_TRUE(std::isnan(Center.x));
	EXPECT_EQ(Center.y, 256.0f);
}
