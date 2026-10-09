#include <game/client/components/qmclient/collision_hitbox_projectile_logic.h>

#include <gtest/gtest.h>

TEST(QmProjectileHitbox, FreezeTurretShowsCollisionVolumeWithoutOrdinaryProjectileDisplay)
{
	CProjectileData Data{};
	Data.m_Type = WEAPON_SHOTGUN;
	Data.m_ExtraInfo = true;
	Data.m_Owner = -1;
	Data.m_Freeze = true;
	Data.m_Bouncing = 1;

	const auto Display = ResolveQmProjectileHitboxDisplay(Data, false, true, true);
	EXPECT_FLOAT_EQ(Display.m_FreezeRadius, 29.0f);
	EXPECT_FALSE(Display.m_ShowTrail);
	EXPECT_FALSE(Display.m_ShowExplosion);
}

TEST(QmProjectileHitbox, DisablingFreezeVolumeKeepsOrdinaryProjectileDisplay)
{
	CProjectileData Data{};
	Data.m_Type = WEAPON_SHOTGUN;
	Data.m_Freeze = true;
	Data.m_Explosive = true;

	const auto Display = ResolveQmProjectileHitboxDisplay(Data, true, false, true);
	EXPECT_FLOAT_EQ(Display.m_FreezeRadius, 0.0f);
	EXPECT_TRUE(Display.m_ShowTrail);
	EXPECT_TRUE(Display.m_ShowExplosion);
}

TEST(QmProjectileHitbox, ExplosiveFreezeTurretKeepsFreezeAndExplosionSwitchesIndependent)
{
	CProjectileData Data{};
	Data.m_Type = WEAPON_SHOTGUN;
	Data.m_Freeze = true;
	Data.m_Explosive = true;

	const auto FreezeOnly = ResolveQmProjectileHitboxDisplay(Data, false, true, true);
	EXPECT_FLOAT_EQ(FreezeOnly.m_FreezeRadius, 29.0f);
	EXPECT_FALSE(FreezeOnly.m_ShowExplosion);
	const auto Both = ResolveQmProjectileHitboxDisplay(Data, true, true, true);
	EXPECT_FLOAT_EQ(Both.m_FreezeRadius, 29.0f);
	EXPECT_TRUE(Both.m_ShowExplosion);
}

TEST(QmProjectileHitbox, InactiveSwitchHidesFreezeVolumeAndReactivationRestoresIt)
{
	CProjectileData Data{};
	Data.m_Type = WEAPON_SHOTGUN;
	Data.m_Freeze = true;
	Data.m_SwitchNumber = 7;

	EXPECT_FLOAT_EQ(ResolveQmProjectileHitboxDisplay(Data, false, true, true).m_FreezeRadius, 29.0f);
	EXPECT_FLOAT_EQ(ResolveQmProjectileHitboxDisplay(Data, false, true, false).m_FreezeRadius, 0.0f);
	EXPECT_FLOAT_EQ(ResolveQmProjectileHitboxDisplay(Data, false, true, true).m_FreezeRadius, 29.0f);
	EXPECT_TRUE(ResolveQmProjectileHitboxDisplay(Data, true, true, false).m_ShowTrail);
}

TEST(QmProjectileHitbox, OrdinaryAndLegacyShotgunBulletsDoNotBecomeFreezeHazards)
{
	CProjectileData Data{};
	Data.m_Type = WEAPON_SHOTGUN;

	for(const bool ExtraInfo : {false, true})
	{
		SCOPED_TRACE(ExtraInfo);
		Data.m_ExtraInfo = ExtraInfo;
		const auto Display = ResolveQmProjectileHitboxDisplay(Data, false, true, true);
		EXPECT_FLOAT_EQ(Display.m_FreezeRadius, 0.0f);
		EXPECT_FALSE(Display.m_ShowTrail);
		EXPECT_FALSE(Display.m_ShowExplosion);
	}
}

TEST(QmProjectileHitbox, FreezeVolumeCanBeDisabledAndReenabled)
{
	CProjectileData Data{};
	Data.m_Type = WEAPON_SHOTGUN;
	Data.m_Freeze = true;

	const auto Disabled = ResolveQmProjectileHitboxDisplay(Data, false, false, true);
	EXPECT_FLOAT_EQ(Disabled.m_FreezeRadius, 0.0f);
	EXPECT_FALSE(Disabled.m_ShowTrail);
	EXPECT_FALSE(Disabled.m_ShowExplosion);
	EXPECT_FLOAT_EQ(ResolveQmProjectileHitboxDisplay(Data, false, true, true).m_FreezeRadius, 29.0f);
}

TEST(QmProjectileHitbox, OrdinaryGrenadeRetainsExplosionRange)
{
	CProjectileData Data{};
	Data.m_Type = WEAPON_GRENADE;

	const auto Display = ResolveQmProjectileHitboxDisplay(Data, true, true, true);
	EXPECT_TRUE(Display.m_ShowTrail);
	EXPECT_TRUE(Display.m_ShowExplosion);
	EXPECT_FLOAT_EQ(Display.m_FreezeRadius, 0.0f);
}
