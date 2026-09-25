// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "test.h"

#include <game/client/components/qmclient/qm_item_culling_logic.h>

#include <gtest/gtest.h>

using namespace qm_item_culling;

namespace
{
	constexpr SRect SCREEN{0.0f, 0.0f, 100.0f, 100.0f};
}

TEST(QmItemCulling, MarginsMatchUpstreamValues)
{
	EXPECT_FLOAT_EQ(TILE_SIZE, 64.0f);
	EXPECT_FLOAT_EQ(PROJECTILE_MARGIN, 64.0f);
	EXPECT_FLOAT_EQ(LASER_MARGIN, 32.0f);
	EXPECT_FLOAT_EQ(PICKUP_MARGIN_X, 112.0f);
	EXPECT_FLOAT_EQ(PICKUP_MARGIN_Y, 48.0f);
	EXPECT_FLOAT_EQ(GHOST_MARGIN, 100.0f);
}

TEST(QmItemCulling, PointInsideIsInclusiveOnEdges)
{
	EXPECT_TRUE(IsPointInside(SCREEN, 0.0f, 0.0f));
	EXPECT_TRUE(IsPointInside(SCREEN, 100.0f, 100.0f));
	EXPECT_TRUE(IsPointInside(SCREEN, 50.0f, 50.0f));
	EXPECT_FALSE(IsPointInside(SCREEN, -0.001f, 50.0f));
	EXPECT_FALSE(IsPointInside(SCREEN, 50.0f, 100.001f));
}

TEST(QmItemCulling, ProjectileKeepsOneTileMargin)
{
	EXPECT_TRUE(IsProjectileInside(SCREEN, vec2(0.0f, 0.0f)));
	EXPECT_TRUE(IsProjectileInside(SCREEN, vec2(-64.0f, -64.0f)));
	EXPECT_TRUE(IsProjectileInside(SCREEN, vec2(164.0f, 164.0f)));
	EXPECT_FALSE(IsProjectileInside(SCREEN, vec2(-64.001f, 50.0f)));
	EXPECT_FALSE(IsProjectileInside(SCREEN, vec2(50.0f, 164.001f)));
}

TEST(QmItemCulling, PickupKeepsAsymmetricMargin)
{
	EXPECT_TRUE(IsPickupInside(SCREEN, vec2(-112.0f, -48.0f)));
	EXPECT_TRUE(IsPickupInside(SCREEN, vec2(212.0f, 148.0f)));
	EXPECT_FALSE(IsPickupInside(SCREEN, vec2(-112.001f, 50.0f)));
	EXPECT_FALSE(IsPickupInside(SCREEN, vec2(50.0f, 148.001f)));
}

TEST(QmItemCulling, LaserOnlyCulledWhenFullyOutsideOneSide)
{
	// 横穿屏幕
	EXPECT_TRUE(IsLaserInside(SCREEN, vec2(-10.0f, -10.0f), vec2(110.0f, 110.0f)));
	// 贴着扩边后的边界（闭区间）
	EXPECT_TRUE(IsLaserInside(SCREEN, vec2(-32.0f, 50.0f), vec2(50.0f, 50.0f)));
	// 整段在左侧之外
	EXPECT_FALSE(IsLaserInside(SCREEN, vec2(-200.0f, 50.0f), vec2(-100.0f, 50.0f)));
	// 整段在左侧之外且越过扩边值
	EXPECT_FALSE(IsLaserInside(SCREEN, vec2(-40.0f, 50.0f), vec2(-32.5f, 50.0f)));
	// 整段在上方之外
	EXPECT_FALSE(IsLaserInside(SCREEN, vec2(0.0f, -100.0f), vec2(100.0f, -100.0f)));
}

TEST(QmItemCulling, GhostKeepsTwoHundredUnitBox)
{
	EXPECT_TRUE(IsGhostInside(SCREEN, vec2(-100.0f, 50.0f)));
	EXPECT_TRUE(IsGhostInside(SCREEN, vec2(50.0f, 200.0f)));
	EXPECT_FALSE(IsGhostInside(SCREEN, vec2(-100.5f, 50.0f)));
	EXPECT_FALSE(IsGhostInside(SCREEN, vec2(50.0f, 200.5f)));
}
