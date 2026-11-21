#include <game/client/components/qmclient/practice_rescue.h>

#include <gtest/gtest.h>

TEST(QmPracticeRescue, GroundedAndNotFrozenAllowsSavingARescuePoint)
{
	EXPECT_TRUE(QmPracticeCanSetRescuePoint(false, false, true, false));
}

TEST(QmPracticeRescue, FreezeZoneBlocksSavingARescuePoint)
{
	EXPECT_FALSE(QmPracticeCanSetRescuePoint(true, false, true, false));
}

TEST(QmPracticeRescue, DeepFreezeBlocksSavingARescuePoint)
{
	EXPECT_FALSE(QmPracticeCanSetRescuePoint(false, true, true, false));
}

TEST(QmPracticeRescue, AirborneTeeCannotSaveARescuePoint)
{
	EXPECT_FALSE(QmPracticeCanSetRescuePoint(false, false, false, false));
}

TEST(QmPracticeRescue, FreezePickupRangeBlocksSavingARescuePoint)
{
	EXPECT_FALSE(QmPracticeCanSetRescuePoint(false, false, true, true));
}

TEST(QmPracticeRescue, AutoModeTeleportsToTheSavedPoint)
{
	const SQmPracticeRescueDecision Decision = QmEvaluatePracticeRescue(false, false, true, false);
	EXPECT_FALSE(Decision.m_SavePoint);
	EXPECT_TRUE(Decision.m_Teleport);
}

TEST(QmPracticeRescue, AutoModeDoesNothingWithoutASavedPoint)
{
	const SQmPracticeRescueDecision Decision = QmEvaluatePracticeRescue(false, false, false, false);
	EXPECT_FALSE(Decision.m_SavePoint);
	EXPECT_FALSE(Decision.m_Teleport);
}

TEST(QmPracticeRescue, AutoModeDoesNotTeleportWhileSuperOrInvincible)
{
	const SQmPracticeRescueDecision Decision = QmEvaluatePracticeRescue(false, false, true, true);
	EXPECT_FALSE(Decision.m_SavePoint);
	EXPECT_FALSE(Decision.m_Teleport);
}

TEST(QmPracticeRescue, ManualModeSavesInPlaceInsteadOfTeleporting)
{
	const SQmPracticeRescueDecision Decision = QmEvaluatePracticeRescue(true, true, true, false);
	EXPECT_TRUE(Decision.m_SavePoint);
	EXPECT_FALSE(Decision.m_Teleport);
}

TEST(QmPracticeRescue, ManualModeStillSavesWhileTeleportIsBlocked)
{
	const SQmPracticeRescueDecision Decision = QmEvaluatePracticeRescue(true, true, true, true);
	EXPECT_TRUE(Decision.m_SavePoint);
	EXPECT_FALSE(Decision.m_Teleport);
}

TEST(QmPracticeRescue, ManualModeTeleportsWhenItCannotSetAndAPointExists)
{
	const SQmPracticeRescueDecision Decision = QmEvaluatePracticeRescue(true, false, true, false);
	EXPECT_FALSE(Decision.m_SavePoint);
	EXPECT_TRUE(Decision.m_Teleport);
}

TEST(QmPracticeRescue, ManualModeDoesNothingWhenItCannotSetAndHasNoPoint)
{
	const SQmPracticeRescueDecision Decision = QmEvaluatePracticeRescue(true, false, false, false);
	EXPECT_FALSE(Decision.m_SavePoint);
	EXPECT_FALSE(Decision.m_Teleport);
}

TEST(QmPracticeRescue, ManualModeDoesNotTeleportWhileSuperOrInvincible)
{
	const SQmPracticeRescueDecision Decision = QmEvaluatePracticeRescue(true, false, true, true);
	EXPECT_FALSE(Decision.m_SavePoint);
	EXPECT_FALSE(Decision.m_Teleport);
}

TEST(QmPracticeRescue, CoreSnapshotRestoresTheSavedTeeState)
{
	CCharacterCore Source{};
	Source.m_Pos = vec2(320.0f, 640.0f);
	Source.m_ActiveWeapon = WEAPON_LASER;
	Source.m_aWeapons[WEAPON_LASER].m_Got = true;
	Source.m_aWeapons[WEAPON_LASER].m_Ammocost = 12;
	Source.m_aWeapons[WEAPON_LASER].m_AmmoRegenStart = 7;
	Source.m_Jumps = 3;
	Source.m_Jumped = 1;
	Source.m_JumpedTotal = 2;
	Source.m_Jetpack = true;
	Source.m_EndlessHook = true;
	Source.m_DeepFrozen = true;
	Source.m_LiveFrozen = true;
	Source.m_HammerHitDisabled = true;
	Source.m_HasTelegunGun = true;

	SQmPracticeSavedTee Tee;
	QmCapturePracticeRescueCore(Source, 1000, Tee);

	CCharacterCore Target{};
	QmApplyPracticeRescueCore(Target, 1000, Tee);

	EXPECT_EQ(Target.m_Pos, Source.m_Pos);
	EXPECT_EQ(Target.m_ActiveWeapon, WEAPON_LASER);
	EXPECT_TRUE(Target.m_aWeapons[WEAPON_LASER].m_Got);
	EXPECT_EQ(Target.m_aWeapons[WEAPON_LASER].m_Ammocost, 12);
	EXPECT_EQ(Target.m_aWeapons[WEAPON_LASER].m_AmmoRegenStart, 7);
	EXPECT_EQ(Target.m_aWeapons[WEAPON_LASER].m_Ammo, -1);
	EXPECT_EQ(Target.m_Jumps, 3);
	EXPECT_EQ(Target.m_Jumped, 1);
	EXPECT_EQ(Target.m_JumpedTotal, 2);
	EXPECT_TRUE(Target.m_Jetpack);
	EXPECT_TRUE(Target.m_EndlessHook);
	EXPECT_TRUE(Target.m_DeepFrozen);
	EXPECT_TRUE(Target.m_LiveFrozen);
	EXPECT_TRUE(Target.m_HammerHitDisabled);
	EXPECT_TRUE(Target.m_HasTelegunGun);
}

TEST(QmPracticeRescue, RestoringDropsWeaponsPickedUpAfterTheSnapshot)
{
	CCharacterCore Source{};
	Source.m_ActiveWeapon = WEAPON_HAMMER;
	Source.m_aWeapons[WEAPON_HAMMER].m_Got = true;

	SQmPracticeSavedTee Tee;
	QmCapturePracticeRescueCore(Source, 1000, Tee);

	CCharacterCore Target{};
	Target.m_ActiveWeapon = WEAPON_LASER;
	Target.m_aWeapons[WEAPON_LASER].m_Got = true;
	QmApplyPracticeRescueCore(Target, 1000, Tee);

	EXPECT_TRUE(Target.m_aWeapons[WEAPON_HAMMER].m_Got);
	EXPECT_FALSE(Target.m_aWeapons[WEAPON_LASER].m_Got);
	EXPECT_EQ(Target.m_ActiveWeapon, WEAPON_HAMMER);
}

TEST(QmPracticeRescue, RestoringClearsVelocityHookAndPendingEvents)
{
	CCharacterCore Source{};
	Source.m_Pos = vec2(10.0f, 20.0f);

	SQmPracticeSavedTee Tee;
	QmCapturePracticeRescueCore(Source, 100, Tee);

	CCharacterCore Target{};
	Target.m_Vel = vec2(9.0f, 9.0f);
	Target.m_HookState = HOOK_GRABBED;
	Target.m_NewHook = true;
	Target.m_TriggeredEvents = COREEVENT_HOOK_ATTACH_PLAYER;
	QmApplyPracticeRescueCore(Target, 100, Tee);

	EXPECT_EQ(Target.m_Vel, vec2(0.0f, 0.0f));
	EXPECT_EQ(Target.m_HookState, HOOK_IDLE);
	EXPECT_FALSE(Target.m_NewHook);
	EXPECT_EQ(Target.HookedPlayer(), -1);
	EXPECT_TRUE(Target.m_AttachedPlayers.empty());
	EXPECT_EQ(Target.m_TriggeredEvents, 0);
}

TEST(QmPracticeRescue, NinjaActivationKeepsItsRemainingDuration)
{
	CCharacterCore Source{};
	Source.m_Ninja.m_ActivationTick = 900;
	Source.m_Ninja.m_CurrentMoveTime = 12;

	SQmPracticeSavedTee Tee;
	QmCapturePracticeRescueCore(Source, 1000, Tee);

	CCharacterCore Target{};
	QmApplyPracticeRescueCore(Target, 1050, Tee);

	// 保存时还剩 100 tick，50 tick 之后再使用仍然剩 100 tick。
	EXPECT_EQ(Target.m_Ninja.m_ActivationTick, 950);
	EXPECT_EQ(Target.m_Ninja.m_CurrentMoveTime, 12);
}

TEST(QmPracticeRescue, FreezeStartKeepsItsRemainingDuration)
{
	CCharacterCore Source{};
	Source.m_FreezeStart = 960;

	SQmPracticeSavedTee Tee;
	QmCapturePracticeRescueCore(Source, 1000, Tee);

	CCharacterCore Target{};
	QmApplyPracticeRescueCore(Target, 1020, Tee);

	EXPECT_EQ(Target.m_FreezeStart, 980);
}

TEST(QmPracticeRescue, RestoringClearsTheStaleDeepFreezeRenderState)
{
	// 救援点只能在家园以外的安全状态下保存，所以恢复后必须回到"未冻结"。
	// FreezeEnd 是渲染层判断冻结皮肤的依据（-1 = 深冻），服务端不保存它，
	// 恢复时必须现算，否则 /deep 后 /r 会留下"能动却画成冻结"的状态。
	CCharacterCore Source{};
	Source.m_Pos = vec2(10.0f, 20.0f);

	SQmPracticeSavedTee Tee;
	QmCapturePracticeRescueCore(Source, 100, Tee);

	CCharacterCore Target{};
	Target.m_DeepFrozen = true;
	Target.m_FreezeStart = 90;
	Target.m_FreezeEnd = -1;
	QmApplyPracticeRescueCore(Target, 200, Tee);

	EXPECT_FALSE(Target.m_DeepFrozen);
	EXPECT_EQ(Target.m_FreezeStart, 0);
	EXPECT_EQ(Target.m_FreezeEnd, 0);
}

TEST(QmPracticeRescue, RestoringUsesTheRemainingFreezeDurationForFreezeEnd)
{
	SQmPracticeSavedTee Tee;
	Tee.m_Valid = true;
	Tee.m_FreezeTime = 60;

	CCharacterCore Target{};
	Target.m_FreezeEnd = 0;
	QmApplyPracticeRescueCore(Target, 200, Tee);

	EXPECT_EQ(Target.m_FreezeEnd, 260);
}
