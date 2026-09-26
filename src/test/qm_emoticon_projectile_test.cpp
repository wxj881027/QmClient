#include <game/client/components/qmclient/emoticon_projectile.h>

#include <gtest/gtest.h>

#include <array>

namespace
{
	std::array<unsigned char, 16> OpaquePixel()
	{
		return {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
	}

	std::array<unsigned char, 6 * 64 * 4> SplitColumnPixels()
	{
		std::array<unsigned char, 6 * 64 * 4> Pixels{};
		for(int Y = 0; Y < 64; ++Y)
			for(int X = 0; X < 6; ++X)
				if(X < 2 || X >= 4)
					Pixels[(Y * 6 + X) * 4 + 3] = 255;
		return Pixels;
	}

	struct SCountingMask
	{
		const QmEmoticon::CAlphaMask &m_Mask;
		mutable int m_NumBoxChecks = 0;

		bool OverlapsBox(vec2 Pos, float Size, float Angle, vec2 BoxCenter, vec2 BoxHalf) const
		{
			++m_NumBoxChecks;
			return m_Mask.OverlapsBox(Pos, Size, Angle, BoxCenter, BoxHalf);
		}
	};
}

TEST(QmEmoticonProjectile, SeparateOpaqueColumnsMergeAcrossRows)
{
	const auto Pixels = SplitColumnPixels();
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(Pixels.data(), 6, 64);
	// 多个不透明区间也应纵向合并，不能随图片高度增加精细碰撞的矩形数量。
	EXPECT_EQ(Mask.NumRects(), 2U);
}

TEST(QmEmoticonProjectile, MergedColumnsKeepTransparentGapWhenRotated)
{
	const auto Pixels = SplitColumnPixels();
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(Pixels.data(), 6, 64);
	for(const float Angle : {0.0f, pi / 4.0f, pi / 2.0f})
	{
		SCOPED_TRACE(Angle);
		EXPECT_FALSE(Mask.OverlapsBox(vec2(0, 0), 64.0f, Angle, vec2(0, 0), vec2(3, 3)));
		EXPECT_TRUE(Mask.OverlapsBox(vec2(0, 0), 64.0f, Angle, direction(Angle) * 24.0f, vec2(3, 3)));
	}
}

TEST(QmEmoticonProjectile, TransparentRowSeparatesOpaqueRuns)
{
	std::array<unsigned char, 2 * 3 * 4> Pixels{};
	for(int X = 0; X < 2; ++X)
	{
		Pixels[X * 4 + 3] = 255;
		Pixels[(4 + X) * 4 + 3] = 255;
	}
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(Pixels.data(), 2, 3);
	EXPECT_EQ(Mask.NumRects(), 2U);
	EXPECT_FALSE(Mask.OverlapsBox(vec2(0, 0), 60.0f, 0.0f, vec2(0, 0), vec2(4, 4)));
	EXPECT_TRUE(Mask.OverlapsBox(vec2(0, 0), 60.0f, 0.0f, vec2(0, 20), vec2(4, 4)));
}

TEST(QmEmoticonProjectile, ChangedRunWidthKeepsTransparentCorner)
{
	std::array<unsigned char, 2 * 2 * 4> Pixels{};
	Pixels[3] = Pixels[7] = Pixels[11] = 255;
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(Pixels.data(), 2, 2);
	EXPECT_EQ(Mask.NumRects(), 2U);
	EXPECT_FALSE(Mask.OverlapsBox(vec2(0, 0), 40.0f, 0.0f, vec2(10, 10), vec2(4, 4)));
	EXPECT_TRUE(Mask.OverlapsBox(vec2(0, 0), 40.0f, 0.0f, vec2(-10, 10), vec2(4, 4)));
}

TEST(QmEmoticonProjectile, RebuildingMaskReplacesMergedRuns)
{
	const auto Columns = SplitColumnPixels();
	const auto SolidPixels = OpaquePixel();
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(Columns.data(), 6, 64);
	ASSERT_FALSE(Mask.OverlapsBox(vec2(0, 0), 64.0f, 0.0f, vec2(0, 0), vec2(3, 3)));
	Mask.Build(SolidPixels.data(), 2, 2);
	EXPECT_EQ(Mask.NumRects(), 1U);
	EXPECT_TRUE(Mask.OverlapsBox(vec2(0, 0), 64.0f, 0.0f, vec2(0, 0), vec2(3, 3)));
	Mask.Build(Columns.data(), 6, 64);
	EXPECT_EQ(Mask.NumRects(), 2U);
	EXPECT_FALSE(Mask.OverlapsBox(vec2(0, 0), 64.0f, 0.0f, vec2(0, 0), vec2(3, 3)));
}

TEST(QmEmoticonProjectile, DistantPlayersSkipNarrowPhase)
{
	const auto Pixels = OpaquePixel();
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(Pixels.data(), 2, 2);
	SCountingMask CountedMask{Mask};
	std::array<QmEmoticon::SPlayerBox, 128> Boxes{};
	for(std::size_t Index = 0; Index < Boxes.size(); ++Index)
		Boxes[Index] = {static_cast<int>(Index), vec2(1000.0f + static_cast<float>(Index) * 32.0f, 1000.0f), 14.0f};
	EXPECT_FALSE(QmEmoticon::OverlapsPlayerBoxes(CountedMask, vec2(0, 0), 64.0f * 2.35f, pi / 4.0f, -1, Boxes.data(), static_cast<int>(Boxes.size())));
	EXPECT_EQ(CountedMask.m_NumBoxChecks, 0);
}

TEST(QmEmoticonProjectile, RotatedSuperEmoticonCornerStillHitsNearbyPlayer)
{
	const auto Pixels = OpaquePixel();
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(Pixels.data(), 2, 2);
	SCountingMask CountedMask{Mask};
	const QmEmoticon::SPlayerBox Player{8, vec2(110, 0), 14.0f};
	EXPECT_TRUE(QmEmoticon::OverlapsPlayerBoxes(CountedMask, vec2(0, 0), 64.0f * 2.35f, pi / 4.0f, 7, &Player, 1));
	EXPECT_EQ(CountedMask.m_NumBoxChecks, 1);
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
