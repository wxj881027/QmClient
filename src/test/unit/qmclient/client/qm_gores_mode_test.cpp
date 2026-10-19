#include <generated/protocol.h>

#include <game/client/components/qmclient/modes.h>
#include <game/client/components/qmclient/weapon_animation.h>
#include <game/gamecore.h>

#include <gtest/gtest.h>
#include <test/test.h>

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

TEST(QmGoresMode, CurrentSnapshotDisablesCycleBeforePredictionAcquiresWeapon)
{
	CCharacterCore Core{};
	for(const int Flag : {CHARACTERFLAG_WEAPON_SHOTGUN, CHARACTERFLAG_WEAPON_GRENADE, CHARACTERFLAG_WEAPON_LASER, CHARACTERFLAG_WEAPON_NINJA})
	{
		SCOPED_TRACE(Flag);
		CNetObj_DDNetCharacter Extended{};
		Extended.m_Flags = Flag;
		const bool HasExtraWeapon = QmHasExtraGoresWeapon(WEAPON_GUN, &Extended, &Core);
		EXPECT_TRUE(HasExtraWeapon);
		EXPECT_FALSE(ShouldEnableQmGoresWeaponCycle(true, true, HasExtraWeapon, true));
	}
}

TEST(QmGoresMode, RemovedWeaponsInSnapshotAllowCycleDespiteStaleCoreInventory)
{
	CCharacterCore Core{};
	Core.m_aWeapons[WEAPON_LASER].m_Got = true;
	CNetObj_DDNetCharacter Extended{};
	Extended.m_Flags = CHARACTERFLAG_WEAPON_HAMMER | CHARACTERFLAG_WEAPON_GUN;
	const bool HasExtraWeapon = QmHasExtraGoresWeapon(WEAPON_GUN, &Extended, &Core);
	EXPECT_FALSE(HasExtraWeapon);
	EXPECT_TRUE(ShouldEnableQmGoresWeaponCycle(true, true, HasExtraWeapon, true));
}

TEST(QmGoresMode, CurrentExtraWeaponStopsCycleWithoutExtendedInventory)
{
	for(const int Weapon : {WEAPON_SHOTGUN, WEAPON_GRENADE, WEAPON_LASER, WEAPON_NINJA})
	{
		SCOPED_TRACE(Weapon);
		EXPECT_TRUE(QmHasExtraGoresWeapon(Weapon, nullptr, nullptr));
	}
}

TEST(QmGoresMode, LegacySnapshotUsesCharacterCoreInventory)
{
	CCharacterCore Core{};
	Core.m_aWeapons[WEAPON_LASER].m_Got = true;
	EXPECT_TRUE(QmHasExtraGoresWeapon(WEAPON_GUN, nullptr, &Core));
	Core.m_aWeapons[WEAPON_LASER].m_Got = false;
	EXPECT_FALSE(QmHasExtraGoresWeapon(WEAPON_GUN, nullptr, &Core));
}

TEST(QmGoresMode, HammerAndGunOnlyDoNotCountAsExtraWeapons)
{
	for(const int Weapon : {WEAPON_HAMMER, WEAPON_GUN})
	{
		SCOPED_TRACE(Weapon);
		EXPECT_FALSE(QmHasExtraGoresWeapon(Weapon, nullptr, nullptr));
	}
}

TEST(QmGoresMode, ExtraWeaponsKeepTheCycleWhenPickupDisablingIsOff)
{
	EXPECT_TRUE(ShouldEnableQmGoresWeaponCycle(true, true, true, false));
	EXPECT_TRUE(ShouldEnableQmGoresWeaponCycle(true, true, false, false));
}

TEST(QmGoresMode, PickupDisablingStopsTheCycleOnlyWithExtraWeapons)
{
	EXPECT_FALSE(ShouldEnableQmGoresWeaponCycle(true, true, true, true));
	EXPECT_TRUE(ShouldEnableQmGoresWeaponCycle(true, true, false, true));
}

TEST(QmGoresMode, DisabledModeOrAutoSwitchDoesNotTakeOverWeapons)
{
	EXPECT_FALSE(ShouldEnableQmGoresWeaponCycle(false, true, false, false));
	EXPECT_FALSE(ShouldEnableQmGoresWeaponCycle(true, false, false, false));
}

TEST(QmGoresMode, PickingUpExtraWeaponsPreservesTheGunAndPendingHammer)
{
	for(const int Weapon : {WEAPON_SHOTGUN, WEAPON_GRENADE, WEAPON_LASER})
	{
		SCOPED_TRACE(Weapon);
		CNetObj_PlayerInput Input{};
		Input.m_WantedWeapon = WEAPON_GUN + 1;
		Input.m_Fire = 2;
		ApplyQmGoresWeaponPickupInput(Input, Weapon, true, true);
		EXPECT_EQ(Input.m_WantedWeapon, WEAPON_GUN + 1);
		EXPECT_EQ(Input.m_Fire, 2);

		Input.m_WantedWeapon = WEAPON_HAMMER + 1;
		ApplyQmGoresWeaponPickupInput(Input, Weapon, true, true);
		EXPECT_EQ(Input.m_WantedWeapon, WEAPON_HAMMER + 1);
	}
}

TEST(QmGoresMode, InactiveCycleKeepsNormalPickupAutoSwitch)
{
	CNetObj_PlayerInput Input{};
	Input.m_WantedWeapon = WEAPON_GUN + 1;
	ApplyQmGoresWeaponPickupInput(Input, WEAPON_GRENADE, true, false);
	EXPECT_EQ(Input.m_WantedWeapon, WEAPON_GRENADE + 1);
	ApplyQmGoresWeaponPickupInput(Input, WEAPON_LASER, false, false);
	EXPECT_EQ(Input.m_WantedWeapon, WEAPON_GRENADE + 1);
}

class CQmGoresWeaponCycleTest : public ::testing::Test
{
protected:
	CQmGoresWeaponCycle m_Cycle;
	CNetObj_PlayerInput m_RawInput{};
	int m_PreviousRawFire = 0;

	CNetObj_PlayerInput Sample(int Weapon, int SnapshotTick, int InputTick, bool Active = true, bool InFreeze = false, bool Wakeup = false)
	{
		CNetObj_PlayerInput Input = m_RawInput;
		m_Cycle.Apply(Input, m_PreviousRawFire, Active, Weapon, InFreeze, Wakeup, SnapshotTick, InputTick);
		m_PreviousRawFire = m_RawInput.m_Fire;
		return Input;
	}
};

TEST_F(CQmGoresWeaponCycleTest, FirstAttackSendsOnlyHammerSelection)
{
	m_RawInput.m_Fire = 1;
	m_RawInput.m_WantedWeapon = WEAPON_GUN + 1;
	const auto Selection = Sample(WEAPON_GUN, 100, 110);
	EXPECT_EQ(Selection.m_WantedWeapon, WEAPON_HAMMER + 1);
	EXPECT_EQ(CountInput(0, Selection.m_Fire).m_Presses, 0);
	EXPECT_EQ(Selection.m_Fire & 1, 0);
	EXPECT_EQ(m_RawInput.m_Fire, 1);
	EXPECT_EQ(m_RawInput.m_WantedWeapon, WEAPON_GUN + 1);
}

TEST_F(CQmGoresWeaponCycleTest, ExtraWeaponCannotFireWhileWaitingForHammer)
{
	m_RawInput.m_Fire = 1;
	m_RawInput.m_WantedWeapon = WEAPON_GRENADE + 1;
	const auto Selection = Sample(WEAPON_GRENADE, 100, 110);
	EXPECT_EQ(Selection.m_WantedWeapon, WEAPON_HAMMER + 1);
	EXPECT_EQ(CountInput(0, Selection.m_Fire).m_Presses, 0);
	const auto Waiting = Sample(WEAPON_GRENADE, 112, 113);
	EXPECT_EQ(Waiting.m_WantedWeapon, WEAPON_HAMMER + 1);
	EXPECT_EQ(CountInput(Selection.m_Fire, Waiting.m_Fire).m_Presses, 0);
	const auto Attack = Sample(WEAPON_HAMMER, 113, 114);
	EXPECT_EQ(Attack.m_WantedWeapon, WEAPON_HAMMER + 1);
	EXPECT_EQ(CountInput(Waiting.m_Fire, Attack.m_Fire).m_Presses, 1);
}

TEST_F(CQmGoresWeaponCycleTest, ReleasedShortClickFiresAfterHammerConfirmation)
{
	m_RawInput.m_Fire = 2;
	const auto Selection = Sample(WEAPON_GUN, 100, 110);
	EXPECT_EQ(CountInput(0, Selection.m_Fire).m_Presses, 0);
	const auto Attack = Sample(WEAPON_HAMMER, 110, 111);
	EXPECT_EQ(Attack.m_WantedWeapon, WEAPON_HAMMER + 1);
	EXPECT_EQ(CountInput(Selection.m_Fire, Attack.m_Fire).m_Presses, 1);
	EXPECT_EQ(Attack.m_Fire & 1, 0);
}

TEST_F(CQmGoresWeaponCycleTest, OldHammerSnapshotCannotReleaseBufferedAttack)
{
	m_RawInput.m_Fire = 1;
	const auto Selection = Sample(WEAPON_HAMMER, 100, 110);
	const auto Waiting = Sample(WEAPON_HAMMER, 109, 111);
	EXPECT_EQ(CountInput(0, Selection.m_Fire).m_Presses, 0);
	EXPECT_EQ(CountInput(Selection.m_Fire, Waiting.m_Fire).m_Presses, 0);
	const auto Attack = Sample(WEAPON_HAMMER, 110, 112);
	EXPECT_EQ(CountInput(Waiting.m_Fire, Attack.m_Fire).m_Presses, 1);
}

TEST_F(CQmGoresWeaponCycleTest, ReleasingWhileSwitchingPreservesOneClick)
{
	m_RawInput.m_Fire = 1;
	const auto Selection = Sample(WEAPON_GUN, 100, 110);
	m_RawInput.m_Fire = 2;
	const auto Waiting = Sample(WEAPON_GUN, 109, 111);
	EXPECT_EQ(CountInput(Selection.m_Fire, Waiting.m_Fire).m_Presses, 0);
	const auto Attack = Sample(WEAPON_HAMMER, 111, 112);
	EXPECT_EQ(CountInput(Waiting.m_Fire, Attack.m_Fire).m_Presses, 1);
	EXPECT_EQ(Attack.m_Fire & 1, 0);
}

TEST_F(CQmGoresWeaponCycleTest, FireCounterWrapRetainsReleasedClick)
{
	m_PreviousRawFire = INPUT_STATE_MASK - 1;
	m_RawInput.m_Fire = 0;
	const auto Selection = Sample(WEAPON_GUN, 100, 110);
	EXPECT_EQ(CountInput(INPUT_STATE_MASK - 1, Selection.m_Fire).m_Presses, 0);
	const auto Attack = Sample(WEAPON_HAMMER, 110, 111);
	EXPECT_EQ(CountInput(Selection.m_Fire, Attack.m_Fire).m_Presses, 1);
	EXPECT_EQ(Attack.m_Fire & 1, 0);
}

TEST_F(CQmGoresWeaponCycleTest, RepeatedSamplesDoNotDuplicateAttack)
{
	m_RawInput.m_Fire = 1;
	const auto Selection = Sample(WEAPON_GUN, 100, 110);
	const auto Waiting = Sample(WEAPON_GUN, 109, 111);
	EXPECT_EQ(Selection.m_Fire, Waiting.m_Fire);
	const auto Attack = Sample(WEAPON_HAMMER, 110, 112);
	const auto Repeated = Sample(WEAPON_HAMMER, 110, 113);
	EXPECT_EQ(CountInput(Waiting.m_Fire, Attack.m_Fire).m_Presses, 1);
	EXPECT_EQ(CountInput(Attack.m_Fire, Repeated.m_Fire).m_Presses, 0);
	EXPECT_EQ(Repeated.m_WantedWeapon, WEAPON_HAMMER + 1);
}

TEST_F(CQmGoresWeaponCycleTest, GunRestoreWaitsForAttackInputAndReleasesFire)
{
	m_RawInput.m_Fire = 1;
	Sample(WEAPON_GUN, 100, 110);
	const auto Attack = Sample(WEAPON_HAMMER, 110, 111);
	const auto Waiting = Sample(WEAPON_HAMMER, 110, 112);
	EXPECT_EQ(Waiting.m_WantedWeapon, WEAPON_HAMMER + 1);
	const auto Restore = Sample(WEAPON_HAMMER, 111, 113);
	EXPECT_EQ(Restore.m_WantedWeapon, WEAPON_GUN + 1);
	EXPECT_EQ(Restore.m_Fire & 1, 0);
	EXPECT_EQ(CountInput(Attack.m_Fire, Restore.m_Fire).m_Presses, 0);
	const auto Idle = Sample(WEAPON_GUN, 113, 114);
	EXPECT_EQ(CountInput(Restore.m_Fire, Idle.m_Fire).m_Presses, 0);
	EXPECT_EQ(m_RawInput.m_Fire, 1);
}

TEST_F(CQmGoresWeaponCycleTest, NextAttackRepeatsSelectionBeforeFiring)
{
	m_RawInput.m_Fire = 1;
	Sample(WEAPON_GUN, 100, 110);
	Sample(WEAPON_HAMMER, 110, 111);
	const auto Restore = Sample(WEAPON_HAMMER, 111, 112);
	m_RawInput.m_Fire = 3;
	const auto Selection = Sample(WEAPON_GUN, 112, 113);
	EXPECT_EQ(Selection.m_WantedWeapon, WEAPON_HAMMER + 1);
	EXPECT_EQ(CountInput(Restore.m_Fire, Selection.m_Fire).m_Presses, 0);
	const auto Attack = Sample(WEAPON_HAMMER, 113, 114);
	EXPECT_EQ(CountInput(Selection.m_Fire, Attack.m_Fire).m_Presses, 1);
}

TEST_F(CQmGoresWeaponCycleTest, FrozenHammerWakeupCompletesOneReleasedClick)
{
	m_RawInput.m_Fire = 2;
	Sample(WEAPON_GUN, 100, 110);
	Sample(WEAPON_HAMMER, 110, 111, true, true);
	const auto Frozen = Sample(WEAPON_HAMMER, 111, 112, true, true);
	EXPECT_EQ(Frozen.m_WantedWeapon, WEAPON_HAMMER + 1);
	const auto Selection = Sample(WEAPON_HAMMER, 112, 113, true, false, true);
	EXPECT_EQ(CountInput(Frozen.m_Fire, Selection.m_Fire).m_Presses, 0);
	const auto Wakeup = Sample(WEAPON_HAMMER, 113, 114);
	EXPECT_EQ(CountInput(Selection.m_Fire, Wakeup.m_Fire).m_Presses, 1);
	EXPECT_EQ(Wakeup.m_Fire & 1, 0);
	EXPECT_EQ(m_RawInput.m_Fire, 2);
}

TEST_F(CQmGoresWeaponCycleTest, ManualPressFollowingWakeupRemainsHeld)
{
	m_RawInput.m_WantedWeapon = WEAPON_HAMMER + 1;
	Sample(WEAPON_HAMMER, 100, 110, true, true);
	const auto Selection = Sample(WEAPON_HAMMER, 110, 111, true, false, true);
	m_RawInput.m_Fire = 1;
	const auto Attack = Sample(WEAPON_HAMMER, 111, 112);
	EXPECT_EQ(CountInput(Selection.m_Fire, Attack.m_Fire).m_Presses, 1);
	EXPECT_EQ(Attack.m_Fire & 1, 1);
	EXPECT_EQ(m_RawInput.m_Fire, 1);
}

TEST_F(CQmGoresWeaponCycleTest, PreviewDoesNotConsumeSelectionOrBufferedAttack)
{
	m_RawInput.m_Fire = 2;
	CQmGoresWeaponCycle Preview = m_Cycle;
	CNetObj_PlayerInput PreviewInput = m_RawInput;
	Preview.Apply(PreviewInput, 0, true, WEAPON_GUN, false, false, 100, 110);
	const auto Selection = Sample(WEAPON_GUN, 100, 110);
	EXPECT_EQ(PreviewInput.m_Fire, Selection.m_Fire);
	EXPECT_EQ(CountInput(0, Selection.m_Fire).m_Presses, 0);
	Preview = m_Cycle;
	PreviewInput = m_RawInput;
	Preview.Apply(PreviewInput, 2, true, WEAPON_HAMMER, false, false, 110, 111);
	const auto Attack = Sample(WEAPON_HAMMER, 110, 111);
	EXPECT_EQ(PreviewInput.m_Fire, Attack.m_Fire);
	EXPECT_EQ(CountInput(Selection.m_Fire, Attack.m_Fire).m_Presses, 1);
}

TEST_F(CQmGoresWeaponCycleTest, DisablingCycleCancelsReleasedBufferedClick)
{
	m_RawInput.m_Fire = 1;
	Sample(WEAPON_GUN, 100, 110);
	m_RawInput.m_Fire = 2;
	m_RawInput.m_WantedWeapon = WEAPON_LASER + 1;
	const auto Cancelled = Sample(WEAPON_LASER, 110, 111, false);
	EXPECT_EQ(Cancelled.m_WantedWeapon, WEAPON_LASER + 1);
	EXPECT_EQ(CountInput(0, Cancelled.m_Fire).m_Presses, 0);
	const auto Later = Sample(WEAPON_HAMMER, 111, 112, false);
	EXPECT_EQ(CountInput(Cancelled.m_Fire, Later.m_Fire).m_Presses, 0);
}

TEST_F(CQmGoresWeaponCycleTest, InactiveCyclePreservesManualFireAndWeapon)
{
	m_RawInput.m_Fire = 1;
	m_RawInput.m_WantedWeapon = WEAPON_GRENADE + 1;
	const auto Input = Sample(WEAPON_GRENADE, 100, 110, false);
	EXPECT_EQ(Input.m_Fire, m_RawInput.m_Fire);
	EXPECT_EQ(Input.m_WantedWeapon, m_RawInput.m_WantedWeapon);
}

TEST_F(CQmGoresWeaponCycleTest, ResetDropsBufferedClick)
{
	m_RawInput.m_Fire = 1;
	Sample(WEAPON_GUN, 100, 110);
	m_RawInput.m_Fire = 2;
	m_PreviousRawFire = 2;
	m_Cycle.Reset();
	const auto Input = Sample(WEAPON_GUN, 110, 111);
	EXPECT_EQ(CountInput(2, Input.m_Fire).m_Presses, 0);
	EXPECT_EQ(Input.m_WantedWeapon, m_RawInput.m_WantedWeapon);
}

TEST_F(CQmGoresWeaponCycleTest, OpeningMenuCancelsHeldBufferedClickWithoutFiring)
{
	m_RawInput.m_Fire = 1;
	Sample(WEAPON_GUN, 100, 110);
	CNetObj_PlayerInput Input = m_RawInput;
	m_Cycle.Apply(Input, 1, false, WEAPON_GUN, false, false, 110, 111, false);
	EXPECT_EQ(CountInput(0, Input.m_Fire).m_Presses, 0);
	EXPECT_EQ(Input.m_Fire & 1, 0);
	const auto Resumed = Sample(WEAPON_HAMMER, 111, 112);
	EXPECT_EQ(CountInput(Input.m_Fire, Resumed.m_Fire).m_Presses, 0);
}

TEST_F(CQmGoresWeaponCycleTest, SwappedConnectionFireBaselineDoesNotCreateAttack)
{
	m_RawInput.m_Fire = 1;
	Sample(WEAPON_GUN, 100, 110);
	m_RawInput.m_Fire = 24;
	m_Cycle.Reset(m_RawInput.m_Fire);
	const auto Input = Sample(WEAPON_GUN, 110, 111);
	EXPECT_EQ(Input.m_Fire, 24);
	EXPECT_EQ(Input.m_WantedWeapon, m_RawInput.m_WantedWeapon);
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
