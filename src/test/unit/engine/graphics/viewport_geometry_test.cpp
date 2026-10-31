#include <engine/gfx/viewport.h>

#include <gtest/gtest.h>

#include <limits>

using GraphicsViewport::RecoverWorldScreen;

TEST(ViewportGeometry, ZeroParallaxRecoversConstantSizeAtLargestZoom)
{
	const auto View = RecoverWorldScreen(1500, 750, 0, 0, 100, 100, 0, 0, 0, std::numeric_limits<float>::max());
	ASSERT_TRUE(View);
	EXPECT_EQ(*View, (std::array<float, 4>{-750, -375, 750, 375}));
}

TEST(ViewportGeometry, SmallParallaxCanRecoverARepresentableHugeView)
{
	const auto View = RecoverWorldScreen(1500, 750, 0, 0, 100, 100, 1, 0, 0, 1.0e36f);
	ASSERT_TRUE(View);
	EXPECT_FLOAT_EQ((*View)[2] - (*View)[0], 1.5e37f);
	EXPECT_FLOAT_EQ((*View)[3] - (*View)[1], 7.5e36f);
}

TEST(ViewportGeometry, TinyPositiveZoomDoesNotDisappearThroughSubtractAndAddOne)
{
	const auto View = RecoverWorldScreen(100, 50, 0, 0, 100, 100, 100, 0, 0, 1.0e-9f);
	ASSERT_TRUE(View);
	EXPECT_FLOAT_EQ((*View)[0], -5.0e-8f);
	EXPECT_FLOAT_EQ((*View)[2], 5.0e-8f);
}

TEST(ViewportGeometry, RecoveredViewKeepsCenterParallaxAndOffsets)
{
	const auto View = RecoverWorldScreen(100, 50, 120, -80, 50, 25, 0, 10, 20, std::numeric_limits<float>::max());
	ASSERT_TRUE(View);
	EXPECT_EQ(*View, (std::array<float, 4>{20, -25, 120, 25}));
}

TEST(ViewportGeometry, UnrepresentableEndpointsAreRejectedWithoutClampingGeometry)
{
	EXPECT_FALSE(RecoverWorldScreen(1500, 750, 0, 0, 100, 100, 100, 0, 0, std::numeric_limits<float>::max()));
}

TEST(ViewportGeometry, FloatCollapsedEndpointPairIsNotPublished)
{
	EXPECT_FALSE(RecoverWorldScreen(100, 50, 1.0e30f, 0, 100, 100, 100, 0, 0, 1));
}

TEST(ViewportGeometry, NonPositiveBaseDimensionsAreRejected)
{
	EXPECT_FALSE(RecoverWorldScreen(0, 750, 0, 0, 100, 100, 0, 0, 0, 1));
	EXPECT_FALSE(RecoverWorldScreen(-1, 750, 0, 0, 100, 100, 0, 0, 0, 1));
	EXPECT_FALSE(RecoverWorldScreen(1500, 0, 0, 0, 100, 100, 0, 0, 0, 1));
}

TEST(ViewportGeometry, NonPositiveZoomIsRejected)
{
	EXPECT_FALSE(RecoverWorldScreen(1500, 750, 0, 0, 100, 100, 0, 0, 0, 0));
	EXPECT_FALSE(RecoverWorldScreen(1500, 750, 0, 0, 100, 100, 0, 0, 0, -1));
}

TEST(ViewportGeometry, EveryNonFiniteInputIsRejected)
{
	for(float Bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		for(int Index = 0; Index < 10; ++Index)
		{
			SCOPED_TRACE(Index);
			std::array<float, 10> Args{1500, 750, 0, 0, 100, 100, 0, 0, 0, 1};
			Args[Index] = Bad;
			EXPECT_FALSE(RecoverWorldScreen(Args[0], Args[1], Args[2], Args[3], Args[4], Args[5], Args[6], Args[7], Args[8], Args[9]));
		}
	}
}

TEST(ViewportGeometry, NegativeEffectiveZoomDoesNotBecomeAnInvertedViewport)
{
	EXPECT_FALSE(RecoverWorldScreen(1500, 750, 0, 0, 100, 100, 200, 0, 0, 0.25f));
}

TEST(ViewportGeometry, LargeFiniteGeometryHasNoArbitraryCoordinateLimit)
{
	constexpr float Max = std::numeric_limits<float>::max();
	const auto View = RecoverWorldScreen(Max * 0.5f, Max * 0.25f, 0, 0, 100, 100, 100, 0, 0, 1);
	ASSERT_TRUE(View);
	EXPECT_EQ((*View)[2], Max * 0.25f);
	EXPECT_EQ((*View)[3], Max * 0.125f);
}
