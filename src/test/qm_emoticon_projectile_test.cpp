#include <game/client/components/qmclient/emoticon_projectile.h>

#include <gtest/gtest.h>

#include <array>

namespace
{
	std::array<unsigned char, 16> OpaquePixel()
	{
		return {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
	}
}

TEST(QmEmoticonProjectile, TransparentMaskDoesNotCollide)
{
	std::array<unsigned char, 16> Pixels{};
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(Pixels.data(), 2, 2);
	EXPECT_FALSE(Mask.Overlaps(vec2(16.0f, 16.0f), 32.0f, 0.0f, [](int, int) { return true; }));
}

TEST(QmEmoticonProjectile, OpaqueMaskCollidesOnlyWithSolidTile)
{
	const auto Pixels = OpaquePixel();
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(Pixels.data(), 2, 2);
	EXPECT_TRUE(Mask.Overlaps(vec2(16.0f, 16.0f), 32.0f, 0.0f, [](int X, int Y) { return X == 0 && Y == 0; }));
	EXPECT_FALSE(Mask.Overlaps(vec2(16.0f, 16.0f), 32.0f, 0.0f, [](int, int) { return false; }));
}

TEST(QmEmoticonProjectile, ProjectileBouncesAndExpires)
{
	const auto Pixels = OpaquePixel();
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(Pixels.data(), 2, 2);
	CEmoticonProjectile Projectile;
	Projectile.Init(vec2(23.8f, 16.0f), vec2(100.0f, 0.0f), 0, 0.25f);
	Projectile.m_AngVel = 0.0f;
	const vec2 Before = Projectile.m_Pos;
	const auto Solid = [](int X, int) { return X >= 1; };
	ASSERT_FALSE(Mask.Overlaps(Before, Projectile.Size(), Projectile.m_Angle, Solid));
	Projectile.Update((float)CEmoticonProjectile::STEP, Mask, Solid);
	EXPECT_TRUE(Projectile.m_Active);
	EXPECT_FLOAT_EQ(Projectile.m_Pos.x, Before.x);
	EXPECT_LT(Projectile.m_Vel.x, 0.0f);
	Projectile.Update(4.0f, Mask, [](int, int) { return false; });
	EXPECT_FALSE(Projectile.m_Active);
}

TEST(QmEmoticonProjectile, PlayerCollisionExcludesOwner)
{
	const auto Pixels = OpaquePixel();
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(Pixels.data(), 2, 2);
	const QmEmoticon::SPlayerBox Boxes[] = {
		{7, vec2(16.0f, 16.0f), 16.0f},
		{8, vec2(16.0f, 16.0f), 16.0f},
	};
	EXPECT_TRUE(QmEmoticon::OverlapsPlayerBoxes(Mask, vec2(16.0f, 16.0f), 32.0f, 0.0f, 7, Boxes, std::size(Boxes)));
	EXPECT_FALSE(QmEmoticon::OverlapsPlayerBoxes(Mask, vec2(16.0f, 16.0f), 32.0f, 0.0f, 7, Boxes, 1));
}

TEST(QmEmoticonProjectile, ExpansionFreezesAtLastClearSizeInNarrowCorridor)
{
	const auto Pixels = OpaquePixel();
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(Pixels.data(), 2, 2);
	const auto Solid = [](int, int Y) { return Y != 0; };
	CEmoticonProjectile Projectile;
	Projectile.Init(vec2(16.0f, 8.01f), vec2(0.0f, 0.0f), 0, 0.25f);
	Projectile.m_AngVel = 0.0f;
	Projectile.m_LifeTime = 0.5f;
	const float ClearSize = Projectile.Size();
	ASSERT_FALSE(Mask.Overlaps(Projectile.m_Pos, ClearSize, Projectile.m_Angle, Solid));

	Projectile.Update((float)CEmoticonProjectile::STEP, Mask, Solid);
	EXPECT_TRUE(Projectile.m_Active);
	EXPECT_LT(Projectile.m_LifeTime, 0.5f);
	EXPECT_FLOAT_EQ(Projectile.Size(), ClearSize);
	EXPECT_FLOAT_EQ(Projectile.m_SizeLimit, ClearSize);
	EXPECT_FALSE(Mask.Overlaps(Projectile.m_Pos, Projectile.Size(), Projectile.m_Angle, Solid));

	Projectile.Update((float)CEmoticonProjectile::STEP, Mask, Solid);
	EXPECT_TRUE(Projectile.m_Active);
	EXPECT_FLOAT_EQ(Projectile.Size(), ClearSize);
}

TEST(QmEmoticonProjectile, ExpansionContinuesWithClearSpace)
{
	const auto Pixels = OpaquePixel();
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(Pixels.data(), 2, 2);
	CEmoticonProjectile Projectile;
	Projectile.Init(vec2(16.0f, 16.0f), vec2(0.0f, 0.0f), 0, 0.25f);
	Projectile.m_AngVel = 0.0f;
	Projectile.m_LifeTime = 0.5f;
	const float InitialSize = Projectile.Size();

	Projectile.Update((float)CEmoticonProjectile::STEP, Mask, [](int, int) { return false; });
	EXPECT_TRUE(Projectile.m_Active);
	EXPECT_GT(Projectile.Size(), InitialSize);
	EXPECT_FLOAT_EQ(Projectile.m_SizeLimit, 32.0f);
}

TEST(QmEmoticonProjectile, ExistingWallOverlapStopsProjectile)
{
	const auto Pixels = OpaquePixel();
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(Pixels.data(), 2, 2);
	CEmoticonProjectile Projectile;
	Projectile.Init(vec2(16.0f, 16.0f), vec2(0.0f, 0.0f), 0, 0.25f);
	Projectile.m_LifeTime = 0.5f;

	Projectile.Update((float)CEmoticonProjectile::STEP, Mask, [](int X, int Y) { return X == 0 && Y == 0; });
	EXPECT_FALSE(Projectile.m_Active);
}

TEST(QmEmoticonProjectile, UpdateBouncesOffOtherPlayerButNotOwner)
{
	const auto Pixels = OpaquePixel();
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(Pixels.data(), 2, 2);
	const QmEmoticon::SPlayerBox Owner = {7, vec2(16.0f, 16.0f), 16.0f};
	CEmoticonProjectile OwnerOnly;
	OwnerOnly.Init(vec2(16.0f, 16.0f), vec2(100.0f, 0.0f), 0, 0.25f, 7);
	OwnerOnly.m_AngVel = 0.0f;
	OwnerOnly.Update((float)CEmoticonProjectile::STEP, Mask, [](int, int) { return false; }, &Owner, 1);
	EXPECT_GT(OwnerOnly.m_Pos.x, 16.0f);
	EXPECT_GT(OwnerOnly.m_Vel.x, 0.0f);

	const QmEmoticon::SPlayerBox Other = {8, vec2(16.0f, 16.0f), 16.0f};
	CEmoticonProjectile WithOther;
	WithOther.Init(vec2(16.0f, 16.0f), vec2(100.0f, 0.0f), 0, 0.25f, 7);
	WithOther.m_AngVel = 0.0f;
	WithOther.Update((float)CEmoticonProjectile::STEP, Mask, [](int, int) { return false; }, &Other, 1);
	EXPECT_FLOAT_EQ(WithOther.m_Pos.x, 16.0f);
	EXPECT_LT(WithOther.m_Vel.x, 0.0f);
}
