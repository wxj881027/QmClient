#include <game/client/components/camera.h>
#include <game/client/components/qmclient/bg_draw_geometry.h>
#include <game/client/components/qmclient/bg_draw_state.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

TEST(QmBgDrawCameraGeometry, AcceptedFiniteZoomCannotAppendAnOverflowedWorldPosition)
{
	const float Zoom = std::numeric_limits<float>::max() / 2.0f;
	ASSERT_TRUE(QmCameraEffects::IsValidZoom(Zoom, 0.01f, std::numeric_limits<float>::max()));
	const vec2 Pos = QmBgDrawWorldPosition({4, 1}, {0, 0}, Zoom, false);
	const auto Invalid = QmBgDrawMakePoint(Pos, Zoom, 1, {1, 1, 1, 1});
	EXPECT_TRUE(std::isinf(Invalid.x));
	EXPECT_TRUE(std::isfinite(Invalid.w));
	EXPECT_FALSE(QmBgDrawPointIsFinite(Invalid));
	CQmBgDrawStroke Stroke(CBgDrawItemDataPoint(0, 0, 1, 1, 1, 1, 1));
	EXPECT_FALSE(Stroke.MoveTo(Invalid));
	ASSERT_EQ(Stroke.Data().size(), 1u);
	EXPECT_EQ(Stroke.Data().front().Pos(), vec2(0, 0));
	const auto Recovered = QmBgDrawMakePoint(QmBgDrawWorldPosition({4, 1}, {0, 0}, 1, false), 1, 1, {1, 1, 1, 1});
	EXPECT_TRUE(Stroke.MoveTo(Recovered));
	ASSERT_EQ(Stroke.Data().size(), 2u);
	EXPECT_EQ(Stroke.Data().back().Pos(), vec2(4, 1));
}

TEST(QmBgDrawCameraGeometry, AcceptedFiniteZoomCannotAppendAnOverflowedSpectatorWidth)
{
	const float Zoom = std::numeric_limits<float>::max() / 2.0f;
	ASSERT_TRUE(QmCameraEffects::IsValidZoom(Zoom, 0.01f, std::numeric_limits<float>::max()));
	const vec2 Pos = QmBgDrawWorldPosition({4, 1}, {10, 20}, Zoom, true);
	EXPECT_EQ(Pos, vec2(10, 20));
	const auto Invalid = QmBgDrawMakePoint(Pos, Zoom, 50, {1, 1, 1, 1});
	EXPECT_TRUE(std::isinf(Invalid.w));
	EXPECT_FALSE(QmBgDrawPointIsFinite(Invalid));
	CQmBgDrawStroke Stroke(CBgDrawItemDataPoint(0, 0, 1, 1, 1, 1, 1));
	EXPECT_FALSE(Stroke.MoveTo(Invalid));
	ASSERT_EQ(Stroke.Data().size(), 1u);
	EXPECT_EQ(Stroke.Min(), vec2(-1, -1));
	EXPECT_EQ(Stroke.Max(), vec2(1, 1));
}
