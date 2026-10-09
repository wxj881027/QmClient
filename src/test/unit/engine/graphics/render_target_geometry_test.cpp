#include <engine/client/backend/render_target_geometry.h>

#include <gtest/gtest.h>

#include <limits>

TEST(RenderTargetGeometry, MapGroupClipScalesToSmallTarget)
{
	const auto Clip = render_target_geometry::MapScreenClip(480, 270, 960, 540, 1920, 1080, 256, 128);
	EXPECT_EQ(Clip.m_X, 64);
	EXPECT_EQ(Clip.m_Y, 32);
	EXPECT_EQ(Clip.m_W, 128);
	EXPECT_EQ(Clip.m_H, 64);
}

TEST(RenderTargetGeometry, TopScreenClipStaysAtTopOfTarget)
{
	const auto Clip = render_target_geometry::MapScreenClip(0, 810, 1920, 270, 1920, 1080, 256, 128);
	EXPECT_EQ(Clip.m_X, 0);
	EXPECT_EQ(Clip.m_Y, 0);
	EXPECT_EQ(Clip.m_W, 256);
	EXPECT_EQ(Clip.m_H, 32);
}

TEST(RenderTargetGeometry, FractionalEdgesRoundOutwardWithoutLosingThinClip)
{
	const auto Clip = render_target_geometry::MapScreenClip(11, 87, 1, 1, 100, 100, 10, 10);
	EXPECT_EQ(Clip.m_X, 1);
	EXPECT_EQ(Clip.m_Y, 1);
	EXPECT_EQ(Clip.m_W, 1);
	EXPECT_EQ(Clip.m_H, 1);
}

TEST(RenderTargetGeometry, ClipOutsideScreenIntersectsBeforeScaling)
{
	const auto Clip = render_target_geometry::MapScreenClip(-100, -100, 300, 300, 100, 100, 32, 16);
	EXPECT_EQ(Clip.m_X, 0);
	EXPECT_EQ(Clip.m_Y, 0);
	EXPECT_EQ(Clip.m_W, 32);
	EXPECT_EQ(Clip.m_H, 16);
	const auto Outside = render_target_geometry::MapScreenClip(101, 0, 10, 100, 100, 100, 32, 16);
	EXPECT_EQ(Outside.m_W, 0);
	EXPECT_EQ(Outside.m_H, 0);
}

TEST(RenderTargetGeometry, EmptyOrUnavailableSurfaceProducesEmptyClip)
{
	EXPECT_EQ(render_target_geometry::MapScreenClip(0, 0, 100, 100, 0, 100, 32, 16).m_W, 0);
	EXPECT_EQ(render_target_geometry::MapScreenClip(0, 0, 100, 100, 100, 100, 32, 0).m_H, 0);
	EXPECT_EQ(render_target_geometry::MapScreenClip(0, 0, 0, 100, 100, 100, 32, 16).m_W, 0);
	EXPECT_EQ(render_target_geometry::MapScreenClip(0, 0, 100, -1, 100, 100, 32, 16).m_H, 0);
}

TEST(RenderTargetGeometry, LargeClipEndpointsDoNotOverflow)
{
	const auto Clip = render_target_geometry::MapScreenClip(1, 1, std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), 100, 100, 100, 100);
	EXPECT_EQ(Clip.m_X, 1);
	EXPECT_EQ(Clip.m_Y, 0);
	EXPECT_EQ(Clip.m_W, 99);
	EXPECT_EQ(Clip.m_H, 99);
}
