#include <engine/shared/packer.h>

#include <generated/protocol.h>

#include <game/client/components/qmclient/emoticon_projectile.h>

#include <gtest/gtest.h>

#include <limits>

TEST(EmoticonCollisionInputs, SecureExtremeCharacterCannotEnterPlacementRay)
{
	CNetObj_Character Input{};
	Input.m_X = std::numeric_limits<int>::max();
	Input.m_Y = std::numeric_limits<int>::max();
	CUnpacker Unpacker;
	Unpacker.Reset(&Input, sizeof(Input));
	CNetObjHandler Handler;
	const auto *pSecured = static_cast<const CNetObj_Character *>(Handler.SecureUnpackObj(NETOBJTYPE_CHARACTER, &Unpacker));
	ASSERT_NE(pSecured, nullptr);
	ASSERT_EQ(pSecured->m_X, std::numeric_limits<int>::max());
	const vec2 Origin(pSecured->m_X, pSecured->m_Y);
	const unsigned char aPixel[] = {255, 255, 255, 255};
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(aPixel, 1, 1);
	CEmoticonProjectile Projectile;
	Projectile.Init(Origin - vec2(0.0f, 20.0f), vec2(1200.0f, -400.0f), 0);
	int Rays = 0, Solids = 0;
	EXPECT_FALSE(Projectile.PlaceOutside(Mask, [&](int, int) { ++Solids; return false; }, Origin, [&](vec2, vec2) { ++Rays; return true; }));
	EXPECT_EQ(Rays, 0);
	EXPECT_EQ(Solids, 0);
}

TEST(EmoticonCollisionInputs, SecureOffMapCharacterInsideNumericRangeCanLaunch)
{
	CNetObj_Character Input{};
	Input.m_X = -100000;
	Input.m_Y = 100000;
	CUnpacker Unpacker;
	Unpacker.Reset(&Input, sizeof(Input));
	CNetObjHandler Handler;
	const auto *pSecured = static_cast<const CNetObj_Character *>(Handler.SecureUnpackObj(NETOBJTYPE_CHARACTER, &Unpacker));
	ASSERT_NE(pSecured, nullptr);
	const vec2 Origin(pSecured->m_X, pSecured->m_Y);
	const unsigned char aPixel[] = {255, 255, 255, 255};
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(aPixel, 1, 1);
	CEmoticonProjectile Projectile;
	Projectile.Init(Origin - vec2(0.0f, 20.0f), vec2(1200.0f, -400.0f), 0);
	int Rays = 0;
	ASSERT_TRUE(Projectile.PlaceOutside(Mask, [](int, int) { return false; }, Origin, [&](vec2, vec2) { ++Rays; return true; }));
	EXPECT_EQ(Projectile.m_Pos, Origin - vec2(0.0f, 20.0f));
	EXPECT_GT(Rays, 0);
}
