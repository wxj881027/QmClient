// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "test.h"

#include <engine/shared/config.h>

#include <game/client/components/qmclient/weapon_animation.h>
#include <game/client/components/qmclient/weapon_trajectory.h>

#include <gtest/gtest.h>

TEST(QmWeaponTrajectorySource, ReusesRenderScratchBuffers)
{
	const std::string Header = ReadTestSourceFile("src/game/client/components/qmclient/weapon_trajectory.h");
	const std::string Source = ReadTestSourceFile("src/game/client/components/qmclient/weapon_trajectory.cpp");

	EXPECT_NE(Header.find("m_vPoints"), std::string::npos);
	EXPECT_NE(Header.find("m_vLineSegments"), std::string::npos);
	EXPECT_NE(Header.find("m_vLineQuadSegments"), std::string::npos);
	EXPECT_EQ(Source.find("std::vector<vec2> vPoints"), std::string::npos);
	EXPECT_EQ(Source.find("std::vector<IGraphics::CLineItem> vLineSegments"), std::string::npos);
}

TEST(QmWeaponReloadAnimation, UsesFlipOnlyForShotgunGrenadeAndLaser)
{
	EXPECT_FALSE(QmWeaponUsesReloadFlip(WEAPON_HAMMER));
	EXPECT_FALSE(QmWeaponUsesReloadFlip(WEAPON_GUN));
	EXPECT_TRUE(QmWeaponUsesReloadFlip(WEAPON_SHOTGUN));
	EXPECT_TRUE(QmWeaponUsesReloadFlip(WEAPON_GRENADE));
	EXPECT_TRUE(QmWeaponUsesReloadFlip(WEAPON_LASER));
	EXPECT_FALSE(QmWeaponUsesReloadFlip(WEAPON_NINJA));
}

TEST(QmWeaponReloadAnimation, RequiresReloadBelowOneAndAHalfTimesDefault)
{
	EXPECT_TRUE(QmWeaponReloadRotation(0.02f, 0.149f, 0.1f, 50, false).m_Active);
	EXPECT_FALSE(QmWeaponReloadRotation(0.02f, 0.15f, 0.1f, 50, false).m_Active);
	EXPECT_FALSE(QmWeaponReloadRotation(0.02f, 0.151f, 0.1f, 50, false).m_Active);
}

TEST(QmWeaponReloadAnimation, FollowsAnticipationFlipCatchAndSettleKeyframes)
{
	constexpr float DegreesToRadians = pi / 180.0f;
	EXPECT_NEAR(QmWeaponFlipAngle(0.0f, false), 0.0f, 0.00001f);
	EXPECT_NEAR(QmWeaponFlipAngle(0.10f, false), -8.0f * DegreesToRadians, 0.00001f);
	EXPECT_NEAR(QmWeaponFlipAngle(0.35f, false), 120.0f * DegreesToRadians, 0.00001f);
	EXPECT_NEAR(QmWeaponFlipAngle(0.60f, false), 240.0f * DegreesToRadians, 0.00001f);
	EXPECT_NEAR(QmWeaponFlipAngle(0.78f, false), 370.0f * DegreesToRadians, 0.00001f);
	EXPECT_NEAR(QmWeaponFlipAngle(0.90f, false), 355.0f * DegreesToRadians, 0.00001f);
	EXPECT_NEAR(QmWeaponFlipAngle(1.0f, false), 360.0f * DegreesToRadians, 0.00001f);

	const float AnticipationMidpoint = QmWeaponFlipAngle(0.05f, false);
	EXPECT_LT(AnticipationMidpoint, 0.0f);
	EXPECT_GT(AnticipationMidpoint, -8.0f * DegreesToRadians);
}

TEST(QmWeaponReloadAnimation, MirrorsTheFlipForLeftFacingWeapons)
{
	EXPECT_FLOAT_EQ(QmWeaponFlipAngle(0.35f, true), -QmWeaponFlipAngle(0.35f, false));
}

TEST(QmWeaponReloadAnimation, EndsAtTheTickQuantizedReloadBoundary)
{
	const SQmWeaponReloadRotation Start = QmWeaponReloadRotation(0.0f, 0.5f, 0.5f, 50, false);
	const SQmWeaponReloadRotation Flip = QmWeaponReloadRotation(0.175f, 0.5f, 0.5f, 50, false);
	const SQmWeaponReloadRotation End = QmWeaponReloadRotation(0.5f, 0.5f, 0.5f, 50, false);

	ASSERT_TRUE(Start.m_Active);
	EXPECT_FLOAT_EQ(Start.m_Angle, 0.0f);
	ASSERT_TRUE(Flip.m_Active);
	EXPECT_NEAR(Flip.m_Angle, 120.0f * pi / 180.0f, 0.00001f);
	EXPECT_FALSE(End.m_Active);
	EXPECT_FLOAT_EQ(End.m_Angle, 0.0f);
}

TEST(QmWeaponReloadAnimation, ReloadRotationOverridesWeaponSwitchRotation)
{
	const SQmWeaponReloadRotation ReloadRotation = QmWeaponReloadRotation(0.175f, 0.5f, 0.5f, 50, false);
	EXPECT_NEAR(QmResolveWeaponAnimationRotation(0.75f, ReloadRotation, true), 120.0f * pi / 180.0f, 0.00001f);
	EXPECT_FLOAT_EQ(QmResolveWeaponAnimationRotation(0.75f, SQmWeaponReloadRotation(), true), 0.0f);
	EXPECT_FLOAT_EQ(QmResolveWeaponAnimationRotation(0.75f, SQmWeaponReloadRotation(), false), 0.75f);
}

TEST(QmWeaponReloadAnimation, KeepsAnAttackBoundToItsWeaponAndTuneZone)
{
	SQmWeaponReloadAnimationState State;
	State.ObserveAttack(100, WEAPON_GUN, 2, true);
	EXPECT_TRUE(State.MatchesAttack(100, WEAPON_GUN));
	EXPECT_EQ(State.m_AttackTuneZone, 2);

	State.ObserveAttack(100, WEAPON_LASER, 3, false);
	EXPECT_FALSE(State.MatchesAttack(100, WEAPON_LASER));
	EXPECT_EQ(State.m_AttackTuneZone, 2);

	State.ObserveAttack(101, WEAPON_LASER, 3, true);
	EXPECT_TRUE(State.MatchesAttack(101, WEAPON_LASER));
	EXPECT_EQ(State.m_AttackTuneZone, 3);
}

TEST(QmWeaponReloadAnimation, SelectsAnAttackOnceFromTheConfiguredProbability)
{
	EXPECT_FALSE(QmWeaponReloadAnimationSelected(0, 0.0f));
	EXPECT_TRUE(QmWeaponReloadAnimationSelected(100, 1.0f));
	EXPECT_TRUE(QmWeaponReloadAnimationSelected(25, 0.249f));
	EXPECT_FALSE(QmWeaponReloadAnimationSelected(25, 0.25f));

	SQmWeaponReloadAnimationState State;
	State.ObserveAttack(100, WEAPON_LASER, 2, false);
	EXPECT_FALSE(State.MatchesAttack(100, WEAPON_LASER));
	State.ObserveAttack(100, WEAPON_LASER, 2, true);
	EXPECT_FALSE(State.MatchesAttack(100, WEAPON_LASER));
	State.ObserveAttack(101, WEAPON_LASER, 2, true);
	EXPECT_TRUE(State.MatchesAttack(101, WEAPON_LASER));
}

TEST(QmWeaponTrajectory, SelectsWeaponSpecificBaseColors)
{
	const ColorRGBA GrenadeColor(0.1f, 0.2f, 0.3f, 0.4f);
	const ColorRGBA RifleInnerColor(0.2f, 0.3f, 0.4f, 0.5f);
	const ColorRGBA ShotgunInnerColor(0.3f, 0.4f, 0.5f, 0.6f);

	EXPECT_EQ(QmWeaponTrajectoryBaseColor(WEAPON_GRENADE, GrenadeColor, RifleInnerColor, ShotgunInnerColor), GrenadeColor);
	EXPECT_EQ(QmWeaponTrajectoryBaseColor(WEAPON_LASER, GrenadeColor, RifleInnerColor, ShotgunInnerColor), RifleInnerColor);
	EXPECT_EQ(QmWeaponTrajectoryBaseColor(WEAPON_SHOTGUN, GrenadeColor, RifleInnerColor, ShotgunInnerColor), ShotgunInnerColor);
	EXPECT_EQ(QmWeaponTrajectoryBaseColor(WEAPON_GUN, GrenadeColor, RifleInnerColor, ShotgunInnerColor), ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
}

TEST(QmWeaponTrajectory, InvertsRgbAndPreservesAlpha)
{
	const ColorRGBA Inverted = QmInvertWeaponTrajectoryColor(ColorRGBA(1.0f, 0.25f, 0.0f, 0.35f));
	EXPECT_FLOAT_EQ(Inverted.r, 0.0f);
	EXPECT_FLOAT_EQ(Inverted.g, 0.75f);
	EXPECT_FLOAT_EQ(Inverted.b, 1.0f);
	EXPECT_FLOAT_EQ(Inverted.a, 0.35f);
}

TEST(QmWeaponTrajectory, UsesLaserLineStyleForGun)
{
	EXPECT_TRUE(QmWeaponTrajectoryUsesLineStyle(WEAPON_GUN));
	EXPECT_TRUE(QmWeaponTrajectoryUsesLineStyle(WEAPON_SHOTGUN));
	EXPECT_TRUE(QmWeaponTrajectoryUsesLineStyle(WEAPON_LASER));
	EXPECT_FALSE(QmWeaponTrajectoryUsesLineStyle(WEAPON_GRENADE));
}

TEST(QmWeaponTrajectory, PistolGuideToggleOnlyAffectsGun)
{
	EXPECT_EQ(DefaultConfig::QmWeaponTrajectoryGun, 1);
	EXPECT_TRUE(QmWeaponTrajectoryEnabledForWeapon(WEAPON_GUN, true));
	EXPECT_FALSE(QmWeaponTrajectoryEnabledForWeapon(WEAPON_GUN, false));
	EXPECT_TRUE(QmWeaponTrajectoryEnabledForWeapon(WEAPON_GRENADE, false));
	EXPECT_TRUE(QmWeaponTrajectoryEnabledForWeapon(WEAPON_SHOTGUN, false));
	EXPECT_TRUE(QmWeaponTrajectoryEnabledForWeapon(WEAPON_LASER, false));
	EXPECT_FALSE(QmWeaponTrajectoryEnabledForWeapon(WEAPON_HAMMER, true));
}

TEST(QmWeaponTrajectory, NinjaPredictionRequiresEnabledLocalNinja)
{
	EXPECT_EQ(DefaultConfig::QmWeaponTrajectoryNinja, 0);
	EXPECT_TRUE(QmWeaponTrajectoryNinjaEnabled(WEAPON_NINJA, true, true));
	EXPECT_FALSE(QmWeaponTrajectoryNinjaEnabled(WEAPON_NINJA, false, true));
	EXPECT_FALSE(QmWeaponTrajectoryNinjaEnabled(WEAPON_NINJA, true, false));
	EXPECT_FALSE(QmWeaponTrajectoryNinjaEnabled(WEAPON_GUN, true, true));
}

TEST(QmWeaponTrajectorySource, PredictsNinjaEndpointUsingCharacterCollision)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/qmclient/weapon_trajectory.cpp");

	EXPECT_NE(Source.find("g_pData->m_Weapons.m_Ninja.m_Movetime"), std::string::npos);
	EXPECT_NE(Source.find("g_pData->m_Weapons.m_Ninja.m_Velocity"), std::string::npos);
	EXPECT_NE(Source.find("Collision()->MoveBox"), std::string::npos);
	EXPECT_NE(Source.find("CCharacterCore::PhysicalSizeVec2()"), std::string::npos);
}

TEST(QmWeaponTrajectory, RespectsWeaponSpecificPlayerHitDisables)
{
	EXPECT_FALSE(QmWeaponTrajectoryCanHitOtherPlayers(WEAPON_GUN, true, false, false));
	EXPECT_FALSE(QmWeaponTrajectoryCanHitOtherPlayers(WEAPON_GRENADE, true, false, false));
	EXPECT_FALSE(QmWeaponTrajectoryCanHitOtherPlayers(WEAPON_LASER, false, true, false));
	EXPECT_FALSE(QmWeaponTrajectoryCanHitOtherPlayers(WEAPON_SHOTGUN, false, false, true));
	EXPECT_TRUE(QmWeaponTrajectoryCanHitOtherPlayers(WEAPON_GUN, false, true, true));
}

TEST(QmWeaponTrajectory, AcceptsFrontLayerTeleGunWallsForSupportedWeapons)
{
	EXPECT_TRUE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_GUN, TILE_ALLOW_TELE_GUN, 0, 0));
	EXPECT_TRUE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_GRENADE, TILE_ALLOW_BLUE_TELE_GUN, 0, 0));
	EXPECT_TRUE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_LASER, TILE_ALLOW_TELE_GUN, 0, 0));
	EXPECT_FALSE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_SHOTGUN, TILE_ALLOW_TELE_GUN, 0, 0));
}

TEST(QmWeaponTrajectory, RequiresTheCurrentWeaponTeleGunCapability)
{
	EXPECT_TRUE(QmWeaponTrajectoryHasTeleGun(WEAPON_GUN, true, false, false));
	EXPECT_TRUE(QmWeaponTrajectoryHasTeleGun(WEAPON_GRENADE, false, true, false));
	EXPECT_TRUE(QmWeaponTrajectoryHasTeleGun(WEAPON_LASER, false, false, true));
	EXPECT_FALSE(QmWeaponTrajectoryHasTeleGun(WEAPON_SHOTGUN, true, true, true));
	EXPECT_FALSE(QmWeaponTrajectoryHasTeleGun(WEAPON_GUN, false, true, true));
}

TEST(QmWeaponTrajectory, AppliesSwitchLayerWeaponDelay)
{
	EXPECT_TRUE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_GUN, 0, TILE_ALLOW_TELE_GUN, 0));
	EXPECT_TRUE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_GUN, 0, TILE_ALLOW_TELE_GUN, 1));
	EXPECT_FALSE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_GUN, 0, TILE_ALLOW_TELE_GUN, 2));

	EXPECT_TRUE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_GRENADE, 0, TILE_ALLOW_BLUE_TELE_GUN, 2));
	EXPECT_FALSE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_GRENADE, 0, TILE_ALLOW_BLUE_TELE_GUN, 3));

	EXPECT_TRUE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_LASER, 0, TILE_ALLOW_TELE_GUN, 3));
	EXPECT_FALSE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_LASER, 0, TILE_ALLOW_TELE_GUN, 1));
	EXPECT_FALSE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_SHOTGUN, 0, TILE_ALLOW_TELE_GUN, 0));
}
