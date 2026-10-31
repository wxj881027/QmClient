#include <engine/shared/packer.h>

#include <generated/protocol.h>

#include <game/client/components/qmclient/item_render_math.h>
#include <game/client/projectile_data.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
	template<typename T>
	CProjectileData ExtractSecured(int Type, const T &Input)
	{
		CUnpacker Unpacker;
		Unpacker.Reset(&Input, sizeof(Input));
		CNetObjHandler Handler;
		const void *pSecured = Handler.SecureUnpackObj(Type, &Unpacker);
		EXPECT_NE(pSecured, nullptr);
		return pSecured ? ExtractProjectileInfo(Type, pSecured, nullptr, nullptr) : CProjectileData{};
	}
}

TEST(ProjectilePositionInputs, SecureLegacyExtremeStartIsRejectedAfterFloatConversion)
{
	CNetObj_Projectile Input{};
	Input.m_X = std::numeric_limits<int>::max();
	Input.m_VelY = -100;
	Input.m_Type = WEAPON_GUN;
	const auto Data = ExtractSecured(NETOBJTYPE_PROJECTILE, Input);
	EXPECT_FALSE(Data.HasValidStart());
	EXPECT_EQ(Data.m_TuneZone, 0);
}

TEST(ProjectilePositionInputs, SecureLegacyRepresentableBoundsRemainAccepted)
{
	CNetObj_Projectile Input{};
	Input.m_X = std::numeric_limits<int>::min();
	Input.m_Y = 2147483520;
	Input.m_VelY = -100;
	Input.m_Type = WEAPON_GUN;
	const auto Data = ExtractSecured(NETOBJTYPE_PROJECTILE, Input);
	EXPECT_TRUE(Data.HasValidStart());
	EXPECT_EQ(Data.m_StartPos, vec2(-2147483648.0f, 2147483520.0f));
}

TEST(ProjectilePositionInputs, DDRaceFixedPointExtremeIsStillAValidPosition)
{
	CNetObj_DDRaceProjectile Input{};
	Input.m_X = std::numeric_limits<int>::max();
	Input.m_Y = std::numeric_limits<int>::min();
	Input.m_Type = WEAPON_GRENADE;
	Input.m_Data = LEGACYPROJECTILEFLAG_NO_OWNER;
	const auto Data = ExtractSecured(NETOBJTYPE_DDRACEPROJECTILE, Input);
	EXPECT_TRUE(Data.HasValidStart());
	EXPECT_EQ(Data.m_StartPos, vec2(21474836.0f, -21474836.0f));
	EXPECT_EQ(Data.m_Owner, -1);
}

TEST(ProjectilePositionInputs, ExtendedNormalizedZeroVelocityRemainsValid)
{
	CNetObj_DDNetProjectile Input{};
	Input.m_Owner = -1;
	Input.m_Type = WEAPON_GUN;
	Input.m_Flags = PROJECTILEFLAG_NORMALIZE_VEL;
	const auto Data = ExtractSecured(NETOBJTYPE_DDNETPROJECTILE, Input);
	EXPECT_TRUE(Data.HasValidStart());
	EXPECT_EQ(Data.m_StartVel, vec2(0, 0));
}

TEST(ProjectilePositionInputs, ExtendedOwnerKeepsExistingVelocityUnits)
{
	CNetObj_DDNetProjectile Input{};
	Input.m_Owner = -1;
	Input.m_Type = WEAPON_GUN;
	Input.m_VelX = 1000000;
	Input.m_VelY = -1000000;
	EXPECT_EQ(ExtractSecured(NETOBJTYPE_DDNETPROJECTILE, Input).m_StartVel, vec2(1, -1));
	Input.m_Owner = 3;
	Input.m_VelX = 2;
	Input.m_VelY = -3;
	EXPECT_EQ(ExtractSecured(NETOBJTYPE_DDNETPROJECTILE, Input).m_StartVel, vec2(2, -3));
}

TEST(ProjectilePositionInputs, LegacyEmbeddedExtraInfoKeepsItsPositionAndFlags)
{
	CNetObj_Projectile Input{};
	Input.m_X = 10000;
	Input.m_Y = -2500;
	Input.m_VelY = LEGACYPROJECTILEFLAG_IS_DDNET | LEGACYPROJECTILEFLAG_NO_OWNER | LEGACYPROJECTILEFLAG_EXPLOSIVE;
	Input.m_Type = WEAPON_GRENADE;
	const auto Data = ExtractSecured(NETOBJTYPE_PROJECTILE, Input);
	EXPECT_TRUE(Data.HasValidStart());
	EXPECT_EQ(Data.m_StartPos, vec2(100, -25));
	EXPECT_EQ(Data.m_StartVel, vec2(0, 1));
	EXPECT_EQ(Data.m_Owner, -1);
	EXPECT_TRUE(Data.m_ExtraInfo);
	EXPECT_TRUE(Data.m_Explosive);
}

TEST(ProjectilePositionInputs, DemoExtraRemovalKeepsCompatibleBaseFields)
{
	CNetObj_Projectile Demo{};
	Demo.m_X = 10000;
	Demo.m_Y = -2500;
	Demo.m_VelY = LEGACYPROJECTILEFLAG_IS_DDNET | LEGACYPROJECTILEFLAG_NO_OWNER | LEGACYPROJECTILEFLAG_EXPLOSIVE;
	Demo.m_Type = WEAPON_GRENADE;
	Demo.m_StartTick = 123;
	DemoObjectRemoveExtraProjectileInfo(&Demo);
	EXPECT_EQ(Demo.m_X, 100);
	EXPECT_EQ(Demo.m_Y, -25);
	EXPECT_EQ(Demo.m_VelX, 0);
	EXPECT_EQ(Demo.m_VelY, 100);
	EXPECT_EQ(Demo.m_Type, WEAPON_GRENADE);
	EXPECT_EQ(Demo.m_StartTick, 123);
}

TEST(ProjectilePositionInputs, ExtremeDecodedTickUsesWideDisplaySubtraction)
{
	CNetObj_DDNetProjectile Input{};
	Input.m_StartTick = std::numeric_limits<int>::min();
	Input.m_Owner = -1;
	Input.m_Type = WEAPON_GUN;
	const auto Data = ExtractSecured(NETOBJTYPE_DDNETPROJECTILE, Input);
	ASSERT_EQ(Data.m_StartTick, std::numeric_limits<int>::min());
	EXPECT_FLOAT_EQ(QmItemTickDelta(std::numeric_limits<int>::max(), Data.m_StartTick, -1), 4294967296.0f);
	EXPECT_FLOAT_EQ(QmItemTickDelta(std::numeric_limits<int>::min(), Data.m_StartTick, -1), -1.0f);
}
