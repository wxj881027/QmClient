#include <game/client/components/qmclient/collision_hitbox_logic.h>
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

TEST(QmCollisionHitboxVisibility, RacingCanShowFreezeVolumesWithoutRegularWeaponRanges)
{
	CConfig Config{};
	Config.m_QmHitboxMode = 1;
	Config.m_QmHitboxShowFreezeProjectiles = 1;

	const auto Visibility = ResolveQmCollisionHitboxVisibility(Config, false);
	EXPECT_FALSE(Visibility.m_HammerAndHook);
	EXPECT_TRUE(Visibility.ShowLaser(true));
	EXPECT_FALSE(Visibility.ShowLaser(false));

	CProjectileData FreezeProjectile{};
	FreezeProjectile.m_Freeze = true;
	FreezeProjectile.m_Explosive = true;
	const auto Freeze = ResolveQmProjectileHitboxDisplay(FreezeProjectile, Visibility.m_ProjectilesAndLasers, Visibility.m_FreezeProjectilesAndLasers, true);
	EXPECT_GT(Freeze.m_FreezeRadius, 0.0f);
	EXPECT_FALSE(Freeze.m_ShowTrail);
	EXPECT_FALSE(Freeze.m_ShowExplosion);
}

TEST(QmCollisionHitboxVisibility, RegularWeaponsCanStayVisibleWithFreezeVolumesDisabled)
{
	CConfig Config{};
	Config.m_QmHitboxMode = 1;
	Config.m_QmHitboxShowHammer = 1;
	Config.m_QmHitboxShowProjectiles = 1;

	const auto Visibility = ResolveQmCollisionHitboxVisibility(Config, false);
	EXPECT_TRUE(Visibility.m_HammerAndHook);
	EXPECT_FALSE(Visibility.ShowLaser(true));
	EXPECT_TRUE(Visibility.ShowLaser(false));

	CProjectileData Grenade{};
	Grenade.m_Type = WEAPON_GRENADE;
	const auto Regular = ResolveQmProjectileHitboxDisplay(Grenade, Visibility.m_ProjectilesAndLasers, Visibility.m_FreezeProjectilesAndLasers, true);
	EXPECT_FLOAT_EQ(Regular.m_FreezeRadius, 0.0f);
	EXPECT_TRUE(Regular.m_ShowTrail);
	EXPECT_TRUE(Regular.m_ShowExplosion);
}

TEST(QmCollisionHitboxVisibility, ReplayViewHidesPlayerPredictionOverlays)
{
	CConfig Config{};
	Config.m_QmHitboxMode = 1;
	Config.m_QmHitboxShowMap = 1;
	Config.m_QmHitboxShowTeeCollision = 1;
	Config.m_QmHitboxShowTeeFreeze = 1;
	Config.m_QmHitboxShowPickups = 1;
	Config.m_QmHitboxShowHammer = 1;
	Config.m_QmHitboxShowProjectiles = 1;
	Config.m_QmHitboxShowFreezeProjectiles = 1;

	const auto Visibility = ResolveQmCollisionHitboxVisibility(Config, true);
	EXPECT_TRUE(Visibility.m_MapBorders);
	EXPECT_TRUE(Visibility.m_ShieldPickups);
	EXPECT_FALSE(Visibility.m_TeeCircle);
	EXPECT_FALSE(Visibility.m_TeeCheckPoints);
	EXPECT_FALSE(Visibility.m_HammerAndHook);
	EXPECT_TRUE(Visibility.ShowLaser(true));
	EXPECT_TRUE(Visibility.ShowLaser(false));
}

TEST(QmCollisionHitboxVisibility, TeeCircleStaysVisibleWhenCheckPointsAreTurnedOff)
{
	CConfig Config{};
	Config.m_QmHitboxMode = 1;
	Config.m_QmHitboxShowTeeCollision = 1;
	Config.m_QmHitboxShowTeeFreeze = 1;
	EXPECT_TRUE(ResolveQmCollisionHitboxVisibility(Config, false).m_TeeCheckPoints);

	Config.m_QmHitboxShowTeeFreeze = 0;
	const auto Visibility = ResolveQmCollisionHitboxVisibility(Config, false);
	EXPECT_FALSE(Visibility.m_TeeCheckPoints);
	EXPECT_TRUE(Visibility.m_TeeCircle);
}
