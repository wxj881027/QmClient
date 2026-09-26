#include "test.h"

#include <generated/protocol.h>

#include <game/client/components/qmclient/modes.h>
#include <game/client/components/qmclient/weapon_animation.h>

#include <gtest/gtest.h>

TEST(QmGoresMode, ManualGuideRevealOverridesAutomaticGuideHiding)
{
	EXPECT_TRUE(ShouldHideGoresGuide(true, true, false));
	EXPECT_FALSE(ShouldHideGoresGuide(true, true, true));
	EXPECT_FALSE(ShouldHideGoresGuide(true, false, false));
	EXPECT_FALSE(ShouldHideGoresGuide(false, true, false));
}

TEST(QmGoresMode, DebugRouteDoesNotUseHideGuidesGate)
{
	EXPECT_TRUE(ShouldRenderGoresDebugRoute(true, true, true));
	EXPECT_FALSE(ShouldRenderGoresDebugRoute(false, true, true));
	EXPECT_FALSE(ShouldRenderGoresDebugRoute(true, false, true));
	EXPECT_FALSE(ShouldRenderGoresDebugRoute(true, true, false));
}

TEST(QmGoresMode, MovingWaterTilesRequireAxiomOrGoresContext)
{
	EXPECT_TRUE(ShouldEnableQmMovingWaterTiles("Gores", "", "", ""));
	EXPECT_TRUE(ShouldEnableQmMovingWaterTiles("", "DDNet Gores", "", ""));
	EXPECT_TRUE(ShouldEnableQmMovingWaterTiles("", "", "axiom-cn", ""));
	EXPECT_TRUE(ShouldEnableQmMovingWaterTiles("", "", "", "Axiom"));
	EXPECT_FALSE(ShouldEnableQmMovingWaterTiles("DDRaceNetwork", "DDNet", "kog", "DDNet"));
	EXPECT_FALSE(ShouldEnableQmMovingWaterTiles(nullptr, nullptr, nullptr, nullptr));
}

TEST(QmGoresMode, LinkedFastInputTemporarilyOverridesAndRestoresThePreviousValue)
{
	SQmFocusConfigOverrideState State;
	bool Changed = false;
	EXPECT_EQ(ApplyQmGoresLinkedConfig(State, true, true, 0, Changed), 1);
	EXPECT_TRUE(Changed);
	EXPECT_EQ(ApplyQmGoresLinkedConfig(State, true, true, 1, Changed), 1);
	EXPECT_FALSE(Changed);
	EXPECT_EQ(ApplyQmGoresLinkedConfig(State, false, true, 1, Changed), 0);
	EXPECT_TRUE(Changed);
}

TEST(QmGoresMode, LinkedFastInputKeepsManualChangesMadeDuringGores)
{
	SQmFocusConfigOverrideState State;
	bool Changed = false;
	EXPECT_EQ(ApplyQmGoresLinkedConfig(State, true, true, 0, Changed), 1);
	EXPECT_EQ(ApplyQmGoresLinkedConfig(State, true, true, 0, Changed), 0);
	EXPECT_FALSE(Changed);
	EXPECT_EQ(ApplyQmGoresLinkedConfig(State, false, true, 0, Changed), 0);
	EXPECT_FALSE(Changed);
}

TEST(QmGoresMode, LinkedFastInputKeepsManualReenableMadeDuringGores)
{
	SQmFocusConfigOverrideState State;
	bool Changed = false;
	EXPECT_EQ(ApplyQmGoresLinkedConfig(State, true, true, 0, Changed), 1);
	EXPECT_EQ(ApplyQmGoresLinkedConfig(State, true, true, 0, Changed), 0);
	EXPECT_EQ(ApplyQmGoresLinkedConfig(State, true, true, 1, Changed), 1);
	EXPECT_EQ(ApplyQmGoresLinkedConfig(State, false, true, 1, Changed), 1);
	EXPECT_FALSE(Changed);
}

TEST(QmGoresMode, DisablingLinkedFastInputRestoresOnlyAutomaticChanges)
{
	SQmFocusConfigOverrideState State;
	bool Changed = false;
	EXPECT_EQ(ApplyQmGoresLinkedConfig(State, true, true, 0, Changed), 1);
	EXPECT_EQ(ApplyQmGoresLinkedConfig(State, true, false, 1, Changed), 0);
	EXPECT_TRUE(Changed);
}

TEST(QmGoresMode, AutoEnableOnlyRunsOnModeEntryAndRestoresOnExit)
{
	SQmFocusConfigOverrideState State;
	bool Changed = false;
	EXPECT_EQ(ApplyQmGoresAutoEnableConfig(State, true, false, true, 0, Changed), 1);
	EXPECT_EQ(ApplyQmGoresAutoEnableConfig(State, false, false, true, 1, Changed), 1);
	EXPECT_FALSE(Changed);
	EXPECT_EQ(ApplyQmGoresAutoEnableConfig(State, false, true, true, 1, Changed), 0);
	EXPECT_TRUE(Changed);
}

TEST(QmGoresMode, AutoEnableRespectsManualDisable)
{
	SQmFocusConfigOverrideState State;
	bool Changed = false;
	EXPECT_EQ(ApplyQmGoresAutoEnableConfig(State, true, false, true, 0, Changed), 1);
	EXPECT_EQ(ApplyQmGoresAutoEnableConfig(State, false, false, true, 0, Changed), 0);
	EXPECT_FALSE(Changed);
	// 用户中途手动关闭后离开 Gores 服务器：仍恢复进入前的值（本来就是关，结果不变）。
	EXPECT_EQ(ApplyQmGoresAutoEnableConfig(State, false, true, true, 0, Changed), 0);
	EXPECT_TRUE(Changed);
}

TEST(QmGoresMode, AutoEnableTurnsOffWhenLeavingGameModeEvenAfterManualToggles)
{
	// 自动启用是位置相关的一次性语义：进 Gores 服自动开，离开一律恢复进入前的值，
	// 期间的手动开关（关了又开）不阻止自动关。
	SQmFocusConfigOverrideState State;
	bool Changed = false;
	EXPECT_EQ(ApplyQmGoresAutoEnableConfig(State, true, false, true, 0, Changed), 1);
	EXPECT_TRUE(Changed);
	EXPECT_EQ(ApplyQmGoresAutoEnableConfig(State, false, false, true, 0, Changed), 0);
	EXPECT_FALSE(Changed);
	EXPECT_EQ(ApplyQmGoresAutoEnableConfig(State, false, false, true, 1, Changed), 1);
	EXPECT_FALSE(Changed);
	EXPECT_EQ(ApplyQmGoresAutoEnableConfig(State, false, true, true, 1, Changed), 0);
	EXPECT_TRUE(Changed);
}

TEST(QmGoresMode, AutoEnableAndFastInputLinkComposeWithoutLockingTheUserToggle)
{
	SQmFocusConfigOverrideState GoresState;
	SQmFocusConfigOverrideState FastInputState;
	bool GoresChanged = false;
	bool FastInputChanged = false;

	int Gores = ApplyQmGoresAutoEnableConfig(GoresState, true, false, true, 0, GoresChanged);
	EXPECT_EQ(Gores, 1);
	EXPECT_TRUE(GoresChanged);
	int FastInput = ApplyQmGoresLinkedConfig(FastInputState, Gores != 0, true, 0, FastInputChanged);
	EXPECT_EQ(FastInput, 1);
	EXPECT_TRUE(FastInputChanged);

	// 用户在 Gores 模式中关闭快速输入后，联动层必须放弃恢复责任。
	FastInput = ApplyQmGoresLinkedConfig(FastInputState, true, true, 0, FastInputChanged);
	EXPECT_EQ(FastInput, 0);
	EXPECT_FALSE(FastInputChanged);

	Gores = ApplyQmGoresAutoEnableConfig(GoresState, false, true, true, Gores, GoresChanged);
	EXPECT_EQ(Gores, 0);
	EXPECT_TRUE(GoresChanged);
	FastInput = ApplyQmGoresLinkedConfig(FastInputState, false, true, FastInput, FastInputChanged);
	EXPECT_EQ(FastInput, 0);
	EXPECT_FALSE(FastInputChanged);
}

TEST(QmGoresMode, DisconnectResetRestoresAutomaticValuesAndClearsState)
{
	SQmFocusConfigOverrideState State;
	bool Changed = false;
	EXPECT_EQ(ApplyQmGoresLinkedConfig(State, true, true, 0, Changed), 1);

	EXPECT_EQ(ResetQmConfigOverride(State, 1, 1, Changed), 0);
	EXPECT_TRUE(Changed);
	EXPECT_FALSE(State.m_WasActive);
	EXPECT_FALSE(State.m_AutoChangedValue);
}

TEST(QmGoresMode, DisconnectResetDoesNotOverwriteUserValue)
{
	SQmFocusConfigOverrideState State;
	bool Changed = false;
	EXPECT_EQ(ApplyQmGoresLinkedConfig(State, true, true, 0, Changed), 1);

	EXPECT_EQ(ResetQmConfigOverride(State, 0, 1, Changed), 0);
	EXPECT_FALSE(Changed);
	EXPECT_FALSE(State.m_WasActive);
	EXPECT_FALSE(State.m_AutoChangedValue);
}

TEST(QmGoresMode, DummyHammerIsClearedOnceOnGoresEntry)
{
	// 进入 Gores 模式的那一帧按选项一次性关闭分身锤；期间帧不接管，用户改值即释放接管。
	SQmFocusConfigOverrideState State;
	bool Changed = false;
	EXPECT_EQ(ApplyQmGoresDummyHammerConfig(State, true, false, true, 1, Changed), 0);
	EXPECT_TRUE(Changed);

	// 用户重新打开：保持打开，接管随之释放，退出 Gores 模式时不再恢复。
	EXPECT_EQ(ApplyQmGoresDummyHammerConfig(State, false, false, true, 1, Changed), 1);
	EXPECT_FALSE(Changed);
	EXPECT_EQ(ApplyQmGoresDummyHammerConfig(State, false, true, true, 1, Changed), 1);
	EXPECT_FALSE(Changed);

	// 已经关闭时不重复写入；未开启该选项时不干预。
	EXPECT_EQ(ApplyQmGoresDummyHammerConfig(State, true, false, true, 0, Changed), 0);
	EXPECT_FALSE(Changed);
	EXPECT_EQ(ApplyQmGoresDummyHammerConfig(State, true, false, false, 1, Changed), 1);
	EXPECT_FALSE(Changed);
}

TEST(QmGoresMode, DummyHammerRestoresWhenLeavingGoresMode)
{
	// 接管未被用户动过：退出 Gores 模式恢复进入前的值，并可再次进入/退出。
	SQmFocusConfigOverrideState State;
	bool Changed = false;
	EXPECT_EQ(ApplyQmGoresDummyHammerConfig(State, true, false, true, 1, Changed), 0);
	EXPECT_TRUE(Changed);
	EXPECT_EQ(ApplyQmGoresDummyHammerConfig(State, false, true, true, 0, Changed), 1);
	EXPECT_TRUE(Changed);
	EXPECT_EQ(ApplyQmGoresDummyHammerConfig(State, true, false, true, 1, Changed), 0);
	EXPECT_TRUE(Changed);
	EXPECT_EQ(ApplyQmGoresDummyHammerConfig(State, false, true, true, 0, Changed), 1);
	EXPECT_TRUE(Changed);
}

TEST(QmGoresMode, HammerSwitchAnimationCanBeSkipped)
{
	// 只有选项开启、Gores 武器循环确实接管武器、并且这次切换涉及锤子时才跳过动画。
	EXPECT_TRUE(QmShouldSkipGoresHammerSwitchAnimation(true, true, WEAPON_GUN, WEAPON_HAMMER));
	EXPECT_TRUE(QmShouldSkipGoresHammerSwitchAnimation(true, true, WEAPON_HAMMER, WEAPON_GUN));
	EXPECT_FALSE(QmShouldSkipGoresHammerSwitchAnimation(true, true, WEAPON_GUN, WEAPON_LASER));
	EXPECT_FALSE(QmShouldSkipGoresHammerSwitchAnimation(false, true, WEAPON_GUN, WEAPON_HAMMER));
	EXPECT_FALSE(QmShouldSkipGoresHammerSwitchAnimation(true, false, WEAPON_GUN, WEAPON_HAMMER));
	// 首次观察到该玩家时上一次武器为 -1：切到锤子仍算涉及锤子，切到别的武器不算。
	EXPECT_TRUE(QmShouldSkipGoresHammerSwitchAnimation(true, true, -1, WEAPON_HAMMER));
	EXPECT_FALSE(QmShouldSkipGoresHammerSwitchAnimation(true, true, -1, WEAPON_GUN));
}

TEST(QmGoresMode, HammerWakeupRequiresHeldHammerAndExternalWakeup)
{
	EXPECT_TRUE(ShouldTriggerQmGoresHammerWakeup(true, true, true));
	EXPECT_FALSE(ShouldTriggerQmGoresHammerWakeup(false, true, true));
	EXPECT_FALSE(ShouldTriggerQmGoresHammerWakeup(true, false, true));
	EXPECT_FALSE(ShouldTriggerQmGoresHammerWakeup(true, true, false));
}

TEST(QmGoresMode, KeepsHammerRequestWhileFrozen)
{
	EXPECT_TRUE(ShouldKeepQmGoresHammerInFreeze(true, true, true));
	EXPECT_FALSE(ShouldKeepQmGoresHammerInFreeze(false, true, true));
	EXPECT_FALSE(ShouldKeepQmGoresHammerInFreeze(true, false, true));
	EXPECT_FALSE(ShouldKeepQmGoresHammerInFreeze(true, true, false));
}

TEST(QmGoresMode, HammerWakeupFireStateCreatesNewPressWhileHeld)
{
	EXPECT_EQ(QmGoresHammerWakeupFireState(0), 1);
	EXPECT_EQ(QmGoresHammerWakeupFireState(1), 3);
	EXPECT_EQ(QmGoresHammerWakeupFireState(2), 3);
	EXPECT_EQ(QmGoresHammerWakeupFireState(3), 5);
}

TEST(QmGoresMode, HammerWakeupReleaseClearsOnlyPendingAutomaticPress)
{
	EXPECT_TRUE(ShouldReleaseQmGoresHammerWakeupFire(true, 1));
	EXPECT_TRUE(ShouldReleaseQmGoresHammerWakeupFire(true, 3));
	EXPECT_FALSE(ShouldReleaseQmGoresHammerWakeupFire(false, 1));
	EXPECT_FALSE(ShouldReleaseQmGoresHammerWakeupFire(true, 2));
	EXPECT_EQ(QmGoresHammerWakeupReleaseFireState(1), 2);
	EXPECT_EQ(QmGoresHammerWakeupReleaseFireState(3), 4);
}

TEST(QmGoresMode, RestoreWeaponAfterHammerUsesRecordedWeapon)
{
	EXPECT_EQ(GoresRestoreWeaponAfterHammer(WEAPON_LASER, true), WEAPON_LASER);
	EXPECT_EQ(GoresRestoreWeaponAfterHammer(WEAPON_GRENADE, true), WEAPON_GRENADE);
	EXPECT_EQ(GoresRestoreWeaponAfterHammer(WEAPON_GUN, false), WEAPON_GUN);
}

TEST(QmGoresMode, FireKeydownPulseRequiresActiveCycleAndNonHammerWeapon)
{
	EXPECT_TRUE(ShouldPulseGoresHammerOnFire(true, true, false, false));
	EXPECT_FALSE(ShouldPulseGoresHammerOnFire(false, true, false, false));
	EXPECT_FALSE(ShouldPulseGoresHammerOnFire(true, false, false, false));
	EXPECT_FALSE(ShouldPulseGoresHammerOnFire(true, true, true, false));
	EXPECT_FALSE(ShouldPulseGoresHammerOnFire(true, true, false, true));
}

TEST(QmGoresMode, RestoresRecordedWeaponEvenWhenTwoWeaponCycleIsInactive)
{
	EXPECT_TRUE(ShouldRestoreGoresWeaponAfterHammer(true, true));
	EXPECT_FALSE(ShouldRestoreGoresWeaponAfterHammer(false, true));
	EXPECT_FALSE(ShouldRestoreGoresWeaponAfterHammer(true, false));
}

TEST(QmGoresMode, BudgetedWorkConsumesAtMostBudget)
{
	int Cursor = 0;
	EXPECT_TRUE(ConsumeQmBudgetedWork(Cursor, 10, 3));
	EXPECT_EQ(Cursor, 3);
	EXPECT_TRUE(ConsumeQmBudgetedWork(Cursor, 10, 4));
	EXPECT_EQ(Cursor, 7);
	EXPECT_FALSE(ConsumeQmBudgetedWork(Cursor, 10, 8));
	EXPECT_EQ(Cursor, 10);
}

TEST(QmGoresMode, BudgetedWorkDoesNotAdvanceWithoutPositiveBudget)
{
	int Cursor = 2;
	EXPECT_TRUE(ConsumeQmBudgetedWork(Cursor, 5, 0));
	EXPECT_EQ(Cursor, 2);
	EXPECT_TRUE(ConsumeQmBudgetedWork(Cursor, 5, -4));
	EXPECT_EQ(Cursor, 2);
	EXPECT_FALSE(ConsumeQmBudgetedWork(Cursor, 2, 10));
}
