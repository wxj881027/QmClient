#include <engine/graphics.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>

namespace
{
	IGraphics::SRenderTargetDrawParams Card()
	{
		IGraphics::SRenderTargetDrawParams Params;
		Params.m_X = 100.0f;
		Params.m_Y = 50.0f;
		Params.m_W = 720.0f;
		Params.m_H = 324.0f;
		return Params;
	}
}

TEST(RenderTargetProjection, ZeroTiltPreservesPositionExactly)
{
	const auto Params = Card();
	const vec2 Point(123.5f, 271.25f);
	EXPECT_EQ(Params.ProjectPoint(Point), Point);
}

TEST(RenderTargetProjection, TiltKeepsCardCenterFixed)
{
	auto Params = Card();
	Params.m_TiltX = 0.10f;
	Params.m_TiltY = -0.08f;
	const vec2 Center(460.0f, 212.0f);
	EXPECT_EQ(Params.ProjectPoint(Center), Center);
}

TEST(RenderTargetProjection, HorizontalTiltChangesNearAndFarEdgeScale)
{
	auto Params = Card();
	Params.m_TiltY = 0.10f;
	const float LeftHeight = Params.ProjectPoint(vec2(100.0f, 374.0f)).y - Params.ProjectPoint(vec2(100.0f, 50.0f)).y;
	const float RightHeight = Params.ProjectPoint(vec2(820.0f, 374.0f)).y - Params.ProjectPoint(vec2(820.0f, 50.0f)).y;
	EXPECT_GT(RightHeight, 324.0f);
	EXPECT_LT(LeftHeight, 324.0f);
	Params.m_TiltY = -0.10f;
	const float ReversedLeftHeight = Params.ProjectPoint(vec2(100.0f, 374.0f)).y - Params.ProjectPoint(vec2(100.0f, 50.0f)).y;
	EXPECT_NEAR(ReversedLeftHeight, RightHeight, 0.0001f);
}

TEST(RenderTargetProjection, ExcessiveTiltIsLimitedToFiniteProjection)
{
	auto Params = Card();
	Params.m_TiltX = 10.0f;
	Params.m_TiltY = -10.0f;
	auto Limited = Params;
	Limited.m_TiltX = 0.25f;
	Limited.m_TiltY = -0.25f;
	for(const vec2 Point : std::array{vec2(100.0f, 50.0f), vec2(820.0f, 50.0f), vec2(820.0f, 374.0f), vec2(100.0f, 374.0f)})
	{
		const vec2 Projected = Params.ProjectPoint(Point);
		EXPECT_TRUE(std::isfinite(Projected.x));
		EXPECT_TRUE(std::isfinite(Projected.y));
		EXPECT_EQ(Projected, Limited.ProjectPoint(Point));
	}
}

TEST(RenderTargetProjection, EmptySurfaceKeepsCoordinates)
{
	auto Params = Card();
	Params.m_W = 0.0f;
	Params.m_TiltY = 0.10f;
	const vec2 Point(100.0f, 50.0f);
	EXPECT_EQ(Params.ProjectPoint(Point), Point);
}
