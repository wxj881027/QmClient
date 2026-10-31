#include <game/client/components/qmclient/rank_view_camera.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>

#include <cmath>
#include <limits>

TEST(RankViewCameraScreen, NormalScreenWidthPreservesRawPixelPan)
{
	CIconBenchmarkGraphics Graphics;
	float Width, Height;
	Graphics.CalcScreenParams(2.0f, 1.25f, &Width, &Height);
	vec2 Center(10.0f, 20.0f);
	ASSERT_TRUE(qmclient::rank_ghost::PanFreeCamera(Center, vec2(128.0f, -64.0f), Width, 1920));
	EXPECT_EQ(Center, vec2(135.0f, -42.5f));
}

TEST(RankViewCameraScreen, OverflowingWorldWidthPreservesCenterAndLaterZoomAllowsPanning)
{
	CIconBenchmarkGraphics Graphics;
	float Width, Height;
	Graphics.CalcScreenParams(2.0f, std::numeric_limits<float>::max(), &Width, &Height);
	ASSERT_TRUE(std::isinf(Width));
	vec2 Center(128.0f, 256.0f);
	EXPECT_FALSE(qmclient::rank_ghost::PanFreeCamera(Center, vec2(0.0f, 1.0f), Width, 1500));
	EXPECT_EQ(Center, vec2(128.0f, 256.0f));
	Graphics.CalcScreenParams(2.0f, 1.0f, &Width, &Height);
	ASSERT_TRUE(qmclient::rank_ghost::PanFreeCamera(Center, vec2(4.0f, 0.0f), Width, 1500));
	EXPECT_EQ(Center, vec2(132.0f, 256.0f));
}

TEST(RankViewCameraScreen, FiniteScreenWidthStillRejectsAnUnrepresentableMouseTranslation)
{
	CIconBenchmarkGraphics Graphics;
	float Width, Height;
	Graphics.CalcScreenParams(2.0f, 1.0e32f, &Width, &Height);
	ASSERT_TRUE(std::isfinite(Width));
	vec2 Center(128.0f, 256.0f);
	EXPECT_FALSE(qmclient::rank_ghost::PanFreeCamera(Center, vec2(std::numeric_limits<float>::max(), 1.0f), Width, 1920));
	EXPECT_EQ(Center, vec2(128.0f, 256.0f));
}
