#include <game/map/render_screen.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using MapRenderScreen::GroupClip;
using MapRenderScreen::SClipRect;

TEST(MapGroupClip, OrdinaryWorldRectangleMapsToPixelRectangle)
{
	const auto Clip = GroupClip({0, 0, 100, 100}, 1000, 1000, {10, 20, 30, 40});
	ASSERT_TRUE(Clip);
	EXPECT_EQ(Clip->m_X, 100);
	EXPECT_EQ(Clip->m_Y, 200);
	EXPECT_EQ(Clip->m_Width, 300);
	EXPECT_EQ(Clip->m_Height, 400);
}

TEST(MapGroupClip, NegativeOriginKeepsRoundAwayFromZero)
{
	const auto Clip = GroupClip({0, 0, 2, 2}, 1, 1, {-1, -1, 2, 2});
	ASSERT_TRUE(Clip);
	EXPECT_EQ(Clip->m_X, -1);
	EXPECT_EQ(Clip->m_Y, -1);
	EXPECT_EQ(Clip->m_Width, 2);
	EXPECT_EQ(Clip->m_Height, 2);
}

TEST(MapGroupClip, WorldEndpointAdditionCanExceedIntWithoutOverflow)
{
	constexpr int Max = std::numeric_limits<int>::max();
	const auto Clip = GroupClip({0, 0, 4294967296.0f, 100}, 1024, 100, {Max, 0, Max, 100});
	ASSERT_TRUE(Clip);
	EXPECT_EQ(Clip->m_X, 512);
	EXPECT_EQ(Clip->m_Width, 512);
}

TEST(MapGroupClip, NonFiniteWorldScreenCoordinatesAreRejected)
{
	for(float Bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		SCOPED_TRACE(Bad);
		for(int Axis = 0; Axis < 4; ++Axis)
		{
			std::array<float, 4> Screen{0, 0, 100, 100};
			Screen[Axis] = Bad;
			EXPECT_FALSE(GroupClip(Screen, 1000, 1000, {0, 0, 10, 10}));
		}
	}
}

TEST(MapGroupClip, CollapsedAndInvertedWorldScreensAreRejected)
{
	EXPECT_FALSE(GroupClip({0, 0, 0, 100}, 1000, 1000, {0, 0, 10, 10}));
	EXPECT_FALSE(GroupClip({0, 0, 100, 0}, 1000, 1000, {0, 0, 10, 10}));
	EXPECT_FALSE(GroupClip({100, 0, 0, 100}, 1000, 1000, {0, 0, 10, 10}));
}

TEST(MapGroupClip, FiniteEndpointsWithOverflowingExtentAreRejected)
{
	constexpr float Max = std::numeric_limits<float>::max();
	EXPECT_FALSE(GroupClip({-Max, 0, Max, 100}, 1000, 1000, {0, 0, 10, 10}));
}

TEST(MapGroupClip, NonPositivePixelDimensionsAreRejected)
{
	EXPECT_FALSE(GroupClip({0, 0, 100, 100}, 0, 1000, {0, 0, 10, 10}));
	EXPECT_FALSE(GroupClip({0, 0, 100, 100}, 1000, -1, {0, 0, 10, 10}));
}

TEST(MapGroupClip, NegativeWorldClipDimensionsAreRejected)
{
	EXPECT_FALSE(GroupClip({0, 0, 100, 100}, 1000, 1000, {0, 0, -1, 10}));
	EXPECT_FALSE(GroupClip({0, 0, 100, 100}, 1000, 1000, {0, 0, 10, -1}));
}

TEST(MapGroupClip, ZeroWorldClipWidthRetainsTheZeroPixelMask)
{
	const auto Clip = GroupClip({0, 0, 100, 100}, 1000, 1000, {10, 20, 0, 40});
	ASSERT_TRUE(Clip);
	EXPECT_EQ(Clip->m_X, 100);
	EXPECT_EQ(Clip->m_Width, 0);
	EXPECT_EQ(Clip->m_Height, 400);
}

TEST(MapGroupClip, FullyOutsideWorldClipIsRejected)
{
	for(const SClipRect Clip : {SClipRect{-10, 0, 1, 10}, SClipRect{101, 0, 10, 10}, SClipRect{0, -10, 10, 1}, SClipRect{0, 101, 10, 10}})
		EXPECT_FALSE(GroupClip({0, 0, 100, 100}, 1000, 1000, Clip));
}

TEST(MapGroupClip, FinitePixelCoordinatesOutsideIntAreRejected)
{
	EXPECT_FALSE(GroupClip({0, 0, 100, 100}, 1920, 1080, {-2000000000, 0, std::numeric_limits<int>::max(), 10}));
}

TEST(MapGroupClip, PixelMultiplicationOverflowIsRejectedBeforeConversion)
{
	EXPECT_FALSE(GroupClip({-std::numeric_limits<float>::max(), 0, 0, 100}, 2, 1000, {0, 0, 1, 10}));
}

TEST(MapGroupClip, RoundedFloatIntMaximumCannotPassAnInexactFloatLimit)
{
	EXPECT_FALSE(GroupClip({0, 0, 1, 100}, std::numeric_limits<int>::max(), 1000, {0, 0, 1, 10}));
}

TEST(MapGroupClip, RepresentablePixelEdgesCanStillHaveAnOverflowingWidth)
{
	EXPECT_FALSE(GroupClip({0, 0, 1, 100}, 2, 1000, {-1000000000, 0, 2000000000, 10}));
}

TEST(MapGroupClip, FailedPreparationDoesNotAffectTheNextRectangle)
{
	EXPECT_FALSE(GroupClip({0, 0, std::numeric_limits<float>::infinity(), 100}, 1000, 1000, {0, 0, 10, 10}));
	EXPECT_TRUE(GroupClip({0, 0, 100, 100}, 1000, 1000, {0, 0, 10, 10}));
}

TEST(MapGroupClip, OrdinaryFractionalWorldViewKeepsOriginalFloatPixelRounding)
{
	const std::array<float, 4> Screen{-100.3f, -50.7f, 150.9f, 80.1f};
	const auto Clip = GroupClip(Screen, 1920, 1080, {-40, -20, 90, 60});
	ASSERT_TRUE(Clip);
	const int Left = static_cast<int>(std::round((-40 - Screen[0]) * 1920 / (Screen[2] - Screen[0])));
	const int Top = static_cast<int>(std::round((-20 - Screen[1]) * 1080 / (Screen[3] - Screen[1])));
	EXPECT_EQ(Clip->m_X, Left);
	EXPECT_EQ(Clip->m_Y, Top);
	EXPECT_EQ(Clip->m_Width, static_cast<int>(std::round((50 - Screen[0]) * 1920 / (Screen[2] - Screen[0]))) - Left);
	EXPECT_EQ(Clip->m_Height, static_cast<int>(std::round((40 - Screen[1]) * 1080 / (Screen[3] - Screen[1]))) - Top);
}
