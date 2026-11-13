#include <game/client/components/qmclient/emoticon_projectile.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
	QmEmoticon::CAlphaMask MakeOpaqueMask()
	{
		const unsigned char aPixel[] = {255, 255, 255, 255};
		QmEmoticon::CAlphaMask Mask;
		Mask.Build(aPixel, 1, 1);
		return Mask;
	}
}

TEST(QmEmoticonCollisionBounds, RoundedIntMaximumDoesNotReachSolidCallback)
{
	const auto Mask = MakeOpaqueMask();
	int Calls = 0;
	EXPECT_TRUE(Mask.Overlaps(vec2(2147483648.0f, 0.0f), 64.0f, 0.0f, [&](int, int) { ++Calls; return false; }));
	EXPECT_EQ(Calls, 0);
}

TEST(QmEmoticonCollisionBounds, BelowIntMinimumDoesNotReachSolidCallback)
{
	const auto Mask = MakeOpaqueMask();
	const float Below = std::nextafter(-2147483648.0f, -std::numeric_limits<float>::infinity());
	int Calls = 0;
	EXPECT_TRUE(Mask.Overlaps(vec2(0.0f, Below), 64.0f, 0.0f, [&](int, int) { ++Calls; return false; }));
	EXPECT_EQ(Calls, 0);
}

TEST(QmEmoticonCollisionBounds, ValidCenterStillRejectsUnsafeRotatedFootprint)
{
	const auto Mask = MakeOpaqueMask();
	const vec2 Center(std::nextafter(2147483648.0f, 0.0f), 0.0f);
	ASSERT_TRUE(QmCollision::CanRoundPosition(Center));
	int Calls = 0;
	EXPECT_TRUE(Mask.Overlaps(Center, 150.4f, pi / 4.0f, [&](int, int) { ++Calls; return false; }));
	EXPECT_EQ(Calls, 0);
}

TEST(QmEmoticonCollisionBounds, FiniteOffMapFootprintStillQueriesTiles)
{
	const auto Mask = MakeOpaqueMask();
	int Calls = 0;
	EXPECT_FALSE(Mask.Overlaps(vec2(-100000.0f, 100000.0f), 64.0f, pi / 4.0f, [&](int, int) { ++Calls; return false; }));
	EXPECT_GT(Calls, 0);
}

TEST(QmEmoticonCollisionBounds, NonFinitePositionDoesNotReachSolidCallback)
{
	const auto Mask = MakeOpaqueMask();
	int Calls = 0;
	EXPECT_TRUE(Mask.Overlaps(vec2(std::numeric_limits<float>::quiet_NaN(), 0.0f), 64.0f, 0.0f, [&](int, int) { ++Calls; return false; }));
	EXPECT_EQ(Calls, 0);
}

TEST(QmEmoticonCollisionBounds, InvalidAnchorIsRejectedBeforeRayQuery)
{
	const auto Mask = MakeOpaqueMask();
	CEmoticonProjectile Projectile;
	Projectile.Init(vec2(16.0f, 16.0f), vec2(1200.0f, -400.0f), 0);
	int Rays = 0, Solids = 0;
	EXPECT_FALSE(Projectile.PlaceOutside(Mask, [&](int, int) { ++Solids; return false; }, vec2(2147483648.0f, 36.0f), [&](vec2, vec2) { ++Rays; return true; }));
	EXPECT_EQ(Rays, 0);
	EXPECT_EQ(Solids, 0);
}

TEST(QmEmoticonCollisionBounds, InvalidLaunchPositionIsRejectedBeforeRayQuery)
{
	const auto Mask = MakeOpaqueMask();
	CEmoticonProjectile Projectile;
	Projectile.Init(vec2(2147483648.0f, 16.0f), vec2(1200.0f, -400.0f), 0);
	int Rays = 0, Solids = 0;
	EXPECT_FALSE(Projectile.PlaceOutside(Mask, [&](int, int) { ++Solids; return false; }, vec2(16.0f, 36.0f), [&](vec2, vec2) { ++Rays; return true; }));
	EXPECT_EQ(Rays, 0);
	EXPECT_EQ(Solids, 0);
}

TEST(QmEmoticonCollisionBounds, PlacementSearchOnlyQueriesRepresentablePoints)
{
	const auto Mask = MakeOpaqueMask();
	const vec2 Origin(std::nextafter(2147483648.0f, 0.0f), 20.0f);
	CEmoticonProjectile Projectile;
	Projectile.Init(Origin - vec2(0.0f, 20.0f), vec2(1200.0f, -400.0f), 0, 2.35f);
	int Rays = 0, Solids = 0;
	const auto Solid = [&](int X, int Y) {
		++Solids;
		EXPECT_TRUE(QmCollision::CanRoundPosition(vec2(X * 32.0f + 16.0f, Y * 32.0f + 16.0f)));
		return true;
	};
	const auto CanReach = [&](vec2 From, vec2 To) {
		++Rays;
		EXPECT_TRUE(QmCollision::CanRoundPosition(From));
		EXPECT_TRUE(QmCollision::CanRoundPosition(To));
		return true;
	};
	EXPECT_FALSE(Projectile.PlaceOutside(Mask, Solid, Origin, CanReach));
	EXPECT_GT(Rays, 1);
	EXPECT_GT(Solids, 0);
}

TEST(QmEmoticonCollisionBounds, UnsafeCurrentFootprintStopsUpdateBeforeTileQuery)
{
	const auto Mask = MakeOpaqueMask();
	CEmoticonProjectile Projectile;
	Projectile.Init(vec2(std::nextafter(2147483648.0f, 0.0f), 0.0f), vec2(1200.0f, -400.0f), 0, 2.35f);
	Projectile.m_Angle = pi / 4.0f;
	int Calls = 0;
	Projectile.Update(static_cast<float>(CEmoticonProjectile::STEP), Mask, [&](int, int) { ++Calls; return false; });
	EXPECT_FALSE(Projectile.m_Active);
	EXPECT_EQ(Calls, 0);
}

TEST(QmEmoticonCollisionBounds, ClearPlacementRetainsPositionAndContinuesFlight)
{
	const auto Mask = MakeOpaqueMask();
	CEmoticonProjectile Projectile;
	Projectile.Init(vec2(16.0f, 16.0f), vec2(1200.0f, -400.0f), 0);
	const auto Solid = [](int, int) { return false; };
	ASSERT_TRUE(Projectile.PlaceOutside(Mask, Solid, vec2(16.0f, 36.0f), [](vec2, vec2) { return true; }));
	EXPECT_EQ(Projectile.m_Pos, vec2(16.0f, 16.0f));
	Projectile.Update(0.01f, Mask, Solid);
	EXPECT_TRUE(Projectile.m_Active);
	EXPECT_GT(Projectile.m_Pos.x, 16.0f);
	EXPECT_LT(Projectile.m_LifeTime, 5.0f);
}
