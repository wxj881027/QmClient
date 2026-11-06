#include <game/client/components/qmclient/water_hammer_indicator_logic.h>

#include <gtest/gtest.h>

TEST(QmWaterHammerIndicator, TimedFreezeAndHammerFireMarksPlayer)
{
	EXPECT_TRUE(QmShouldMarkWaterHammer(200, false, true, true));
}

TEST(QmWaterHammerIndicator, UnfrozenHammerFireDoesNotMarkPlayer)
{
	EXPECT_FALSE(QmShouldMarkWaterHammer(0, false, true, true));
}

TEST(QmWaterHammerIndicator, DeepFreezeAndHammerFireMarksPlayer)
{
	EXPECT_TRUE(QmShouldMarkWaterHammer(-1, false, true, true));
}

TEST(QmWaterHammerIndicator, LiveFreezeAndHammerFireMarksPlayer)
{
	EXPECT_TRUE(QmShouldMarkWaterHammer(0, true, true, true));
}

TEST(QmWaterHammerIndicator, NonHammerInputDoesNotMarkPlayer)
{
	EXPECT_FALSE(QmShouldMarkWaterHammer(200, false, false, true));
}

TEST(QmWaterHammerIndicator, ReleasedFireDoesNotMarkPlayer)
{
	EXPECT_FALSE(QmShouldMarkWaterHammer(200, false, true, false));
}

TEST(QmWaterHammerIndicator, RetainedLocalHammerFireMarksWithoutPreInput)
{
	CNetObj_PlayerInput LocalInput{};
	LocalInput.m_Fire = 1;

	EXPECT_TRUE(QmShouldMarkWaterHammer(200, false, WEAPON_HAMMER, SQmWaterHammerInputState{}, &LocalInput));
}

TEST(QmWaterHammerIndicator, LocalHeldFireOverridesReleasedPreInput)
{
	const SQmWaterHammerInputState PreInput{true, WEAPON_HAMMER + 1, 2};
	CNetObj_PlayerInput LocalInput{};
	LocalInput.m_Fire = 3;

	EXPECT_TRUE(QmShouldMarkWaterHammer(200, false, WEAPON_HAMMER, PreInput, &LocalInput));
}

TEST(QmWaterHammerIndicator, LocalFireReleaseClearsMarkDespiteHeldPreInput)
{
	const SQmWaterHammerInputState PreInput{true, WEAPON_HAMMER + 1, 1};
	CNetObj_PlayerInput LocalInput{};
	LocalInput.m_Fire = 1;
	ASSERT_TRUE(QmShouldMarkWaterHammer(200, false, WEAPON_HAMMER, PreInput, &LocalInput));

	LocalInput.m_Fire = 2;
	EXPECT_FALSE(QmShouldMarkWaterHammer(200, false, WEAPON_HAMMER, PreInput, &LocalInput));
}

TEST(QmWaterHammerIndicator, ResettingLocalInputClearsRetainedHammerFire)
{
	CNetObj_PlayerInput LocalInput{};
	LocalInput.m_Fire = 1;
	ASSERT_TRUE(QmShouldMarkWaterHammer(200, false, WEAPON_HAMMER, SQmWaterHammerInputState{}, &LocalInput));

	LocalInput = {};
	EXPECT_FALSE(QmShouldMarkWaterHammer(200, false, WEAPON_HAMMER, SQmWaterHammerInputState{}, &LocalInput));
}

TEST(QmWaterHammerIndicator, LocalHeldHammerFireStillRequiresActualFreeze)
{
	CNetObj_PlayerInput LocalInput{};
	LocalInput.m_Fire = 1;

	EXPECT_FALSE(QmShouldMarkWaterHammer(0, false, WEAPON_HAMMER, SQmWaterHammerInputState{}, &LocalInput));
}

TEST(QmWaterHammerIndicator, LocalNonHammerInputDoesNotUseStaleHammerRequest)
{
	const SQmWaterHammerInputState PreInput{true, WEAPON_HAMMER + 1, 1};
	CNetObj_PlayerInput LocalInput{};
	LocalInput.m_WantedWeapon = WEAPON_GUN + 1;
	LocalInput.m_Fire = 1;

	EXPECT_FALSE(QmShouldMarkWaterHammer(200, false, WEAPON_GUN, PreInput, &LocalInput));
}

TEST(QmWaterHammerIndicator, LocalHammerRequestMarksBeforeRenderedWeaponChanges)
{
	CNetObj_PlayerInput LocalInput{};
	LocalInput.m_WantedWeapon = WEAPON_HAMMER + 1;
	LocalInput.m_Fire = 1;

	EXPECT_TRUE(QmShouldMarkWaterHammer(200, false, WEAPON_GUN, SQmWaterHammerInputState{}, &LocalInput));
}

TEST(QmWaterHammerIndicator, RemoteHammerFireUsesReceivedPreInput)
{
	const SQmWaterHammerInputState PreInput{true, 0, 1};

	EXPECT_TRUE(QmShouldMarkWaterHammer(200, false, WEAPON_HAMMER, PreInput, nullptr));
}

TEST(QmWaterHammerIndicator, RemoteHammerFireRequiresReceivedPreInput)
{
	const SQmWaterHammerInputState PreInput{false, WEAPON_HAMMER + 1, 1};

	EXPECT_FALSE(QmShouldMarkWaterHammer(200, false, WEAPON_HAMMER, PreInput, nullptr));
}

TEST(QmWaterHammerIndicator, RemovingLocalInputReturnsToRemoteFireState)
{
	const SQmWaterHammerInputState PreInput{true, WEAPON_HAMMER + 1, 2};
	CNetObj_PlayerInput LocalInput{};
	LocalInput.m_Fire = 1;
	ASSERT_TRUE(QmShouldMarkWaterHammer(200, false, WEAPON_HAMMER, PreInput, &LocalInput));

	EXPECT_FALSE(QmShouldMarkWaterHammer(200, false, WEAPON_HAMMER, PreInput, nullptr));
}

TEST(QmWaterHammerIndicator, AllDeathAndFreezeTilesArePenaltyTiles)
{
	EXPECT_TRUE(QmIsWaterHammerPenaltyTile(TILE_DEATH));
	EXPECT_TRUE(QmIsWaterHammerPenaltyTile(TILE_FREEZE));
	EXPECT_TRUE(QmIsWaterHammerPenaltyTile(TILE_DFREEZE));
	EXPECT_TRUE(QmIsWaterHammerPenaltyTile(TILE_LFREEZE));
	EXPECT_FALSE(QmIsWaterHammerPenaltyTile(TILE_AIR));
}

TEST(QmWaterHammerIndicator, RemoteHookRequiresActualFreeze)
{
	const SQmWaterHammerInputState Input{true, 0, 0, 1};
	EXPECT_TRUE(QmShouldMarkWaterHook(200, false, Input, nullptr));
	EXPECT_TRUE(QmShouldMarkWaterHook(-1, false, Input, nullptr));
	EXPECT_TRUE(QmShouldMarkWaterHook(0, true, Input, nullptr));
	EXPECT_FALSE(QmShouldMarkWaterHook(0, false, Input, nullptr));
}

TEST(QmWaterHammerIndicator, RemoteHookReleaseClearsMark)
{
	SQmWaterHammerInputState Input{true, 0, 0, 1};
	ASSERT_TRUE(QmShouldMarkWaterHook(200, false, Input, nullptr));
	Input.m_Hook = 0;
	EXPECT_FALSE(QmShouldMarkWaterHook(200, false, Input, nullptr));
}

TEST(QmWaterHammerIndicator, RemoteHookRequiresReceivedInput)
{
	const SQmWaterHammerInputState Input{false, 0, 0, 1};
	EXPECT_FALSE(QmShouldMarkWaterHook(200, false, Input, nullptr));
}

TEST(QmWaterHammerIndicator, LocalHookWorksWithoutPreInputAndClearsOnRelease)
{
	CNetObj_PlayerInput Input{};
	Input.m_Hook = 1;
	ASSERT_TRUE(QmShouldMarkWaterHook(200, false, {}, &Input));
	Input.m_Hook = 0;
	EXPECT_FALSE(QmShouldMarkWaterHook(200, false, {}, &Input));
}

TEST(QmWaterHammerIndicator, LocalHookReleaseOverridesStalePreInput)
{
	const SQmWaterHammerInputState PreInput{true, 0, 0, 1};
	CNetObj_PlayerInput Input{};
	EXPECT_FALSE(QmShouldMarkWaterHook(200, false, PreInput, &Input));
}

TEST(QmWaterHammerIndicator, LocalHeldHookOverridesReleasedPreInput)
{
	const SQmWaterHammerInputState PreInput{true, 0, 0, 0};
	CNetObj_PlayerInput Input{};
	Input.m_Hook = 1;
	EXPECT_TRUE(QmShouldMarkWaterHook(200, false, PreInput, &Input));
	EXPECT_FALSE(QmShouldMarkWaterHook(200, false, PreInput, nullptr));
	EXPECT_FALSE(QmShouldMarkWaterHook(0, false, PreInput, &Input));
}

TEST(QmWaterHammerIndicator, HammerAndHookCanBeHeldIndependently)
{
	SQmWaterHammerInputState Input{true, WEAPON_HAMMER + 1, 1, 1};
	EXPECT_TRUE(QmShouldMarkWaterHammer(200, false, WEAPON_HAMMER, Input, nullptr));
	EXPECT_TRUE(QmShouldMarkWaterHook(200, false, Input, nullptr));
	Input.m_Fire = 2;
	EXPECT_FALSE(QmShouldMarkWaterHammer(200, false, WEAPON_HAMMER, Input, nullptr));
	EXPECT_TRUE(QmShouldMarkWaterHook(200, false, Input, nullptr));
	Input.m_Fire = 3;
	Input.m_Hook = 0;
	EXPECT_TRUE(QmShouldMarkWaterHammer(200, false, WEAPON_HAMMER, Input, nullptr));
	EXPECT_FALSE(QmShouldMarkWaterHook(200, false, Input, nullptr));
}

TEST(QmWaterHammerIndicator, FrozenDirectionUsesReceivedLeftAndJumpInsteadOfClearedSnapshot)
{
	CNetMsg_Sv_PreInput Message{};
	Message.m_IntendedTick = 100;
	Message.m_Direction = -1;
	Message.m_Jump = 1;
	SQmWaterHammerInputState Input;
	Input.Update(Message);

	const auto Direction = QmResolveWaterDirection(200, false, 0, 0, Input);
	EXPECT_TRUE(Direction.m_Left);
	EXPECT_TRUE(Direction.m_Jump);
	EXPECT_FALSE(Direction.m_Right);
}

TEST(QmWaterHammerIndicator, DeepAndLiveFreezeUseReceivedRightInput)
{
	CNetMsg_Sv_PreInput Message{};
	Message.m_Direction = 1;
	SQmWaterHammerInputState Input;
	Input.Update(Message);

	const auto DeepFrozen = QmResolveWaterDirection(-1, false, 0, 0, Input);
	EXPECT_FALSE(DeepFrozen.m_Left);
	EXPECT_FALSE(DeepFrozen.m_Jump);
	EXPECT_TRUE(DeepFrozen.m_Right);

	const auto LiveFrozen = QmResolveWaterDirection(0, true, 0, 0, Input);
	EXPECT_FALSE(LiveFrozen.m_Left);
	EXPECT_FALSE(LiveFrozen.m_Jump);
	EXPECT_TRUE(LiveFrozen.m_Right);
}

TEST(QmWaterHammerIndicator, FrozenDirectionChangeReplacesPreviousArrow)
{
	CNetMsg_Sv_PreInput Message{};
	Message.m_IntendedTick = 100;
	Message.m_Direction = -1;
	Message.m_Jump = 1;
	SQmWaterHammerInputState Input;
	Input.Update(Message);
	ASSERT_TRUE(QmResolveWaterDirection(200, false, 0, 0, Input).m_Left);

	Message.m_IntendedTick = 101;
	Message.m_Direction = 1;
	Input.Update(Message);
	const auto Direction = QmResolveWaterDirection(200, false, 0, 0, Input);
	EXPECT_FALSE(Direction.m_Left);
	EXPECT_TRUE(Direction.m_Jump);
	EXPECT_TRUE(Direction.m_Right);
}

TEST(QmWaterHammerIndicator, FrozenReleaseClearsArrowsDespiteHeldSnapshot)
{
	CNetMsg_Sv_PreInput Message{};
	Message.m_IntendedTick = 100;
	Message.m_Direction = -1;
	Message.m_Jump = 1;
	SQmWaterHammerInputState Input;
	Input.Update(Message);
	ASSERT_TRUE(QmResolveWaterDirection(200, false, 0, 0, Input).m_Jump);

	Message.m_IntendedTick = 101;
	Message.m_Direction = 0;
	Message.m_Jump = 0;
	Input.Update(Message);
	const auto Direction = QmResolveWaterDirection(200, false, -1, 1, Input);
	EXPECT_FALSE(Direction.m_Left);
	EXPECT_FALSE(Direction.m_Jump);
	EXPECT_FALSE(Direction.m_Right);
}

TEST(QmWaterHammerIndicator, MissingPreInputFallsBackToSnapshot)
{
	const auto Direction = QmResolveWaterDirection(200, false, 1, 1, {});
	EXPECT_FALSE(Direction.m_Left);
	EXPECT_TRUE(Direction.m_Jump);
	EXPECT_TRUE(Direction.m_Right);
}

TEST(QmWaterHammerIndicator, ThawReturnsToSnapshotDirectionAndJumpBits)
{
	CNetMsg_Sv_PreInput Message{};
	Message.m_Direction = -1;
	Message.m_Jump = 1;
	SQmWaterHammerInputState Input;
	Input.Update(Message);
	ASSERT_TRUE(QmResolveWaterDirection(200, false, 0, 0, Input).m_Left);

	const auto Direction = QmResolveWaterDirection(0, false, 1, 2, Input);
	EXPECT_FALSE(Direction.m_Left);
	EXPECT_FALSE(Direction.m_Jump);
	EXPECT_TRUE(Direction.m_Right);
	EXPECT_TRUE(QmResolveWaterDirection(0, false, 1, 3, Input).m_Jump);
}

TEST(QmWaterHammerIndicator, DelayedPreInputCannotRestoreReleasedKeys)
{
	CNetMsg_Sv_PreInput Message{};
	Message.m_IntendedTick = 101;
	SQmWaterHammerInputState Input;
	Input.Update(Message);

	Message.m_IntendedTick = 100;
	Message.m_Direction = -1;
	Message.m_Jump = 1;
	Input.Update(Message);
	const auto Direction = QmResolveWaterDirection(200, false, 0, 0, Input);
	EXPECT_FALSE(Direction.m_Left);
	EXPECT_FALSE(Direction.m_Jump);
	EXPECT_FALSE(Direction.m_Right);
}

TEST(QmWaterHammerIndicator, SameTickReleaseUpdatesHeldKeys)
{
	CNetMsg_Sv_PreInput Message{};
	Message.m_IntendedTick = 100;
	Message.m_Direction = -1;
	Message.m_Jump = 1;
	SQmWaterHammerInputState Input;
	Input.Update(Message);

	Message.m_Direction = 0;
	Message.m_Jump = 0;
	Input.Update(Message);
	const auto Direction = QmResolveWaterDirection(200, false, -1, 1, Input);
	EXPECT_FALSE(Direction.m_Left);
	EXPECT_FALSE(Direction.m_Jump);
	EXPECT_FALSE(Direction.m_Right);
}

TEST(QmWaterHammerIndicator, ResetDiscardsHeldKeysAndAcceptsNewSessionTick)
{
	CNetMsg_Sv_PreInput Message{};
	Message.m_IntendedTick = 100;
	Message.m_Direction = -1;
	Message.m_Jump = 1;
	SQmWaterHammerInputState Input;
	Input.Update(Message);

	Input = {};
	const auto ResetDirection = QmResolveWaterDirection(200, false, 0, 0, Input);
	EXPECT_FALSE(ResetDirection.m_Left);
	EXPECT_FALSE(ResetDirection.m_Jump);
	EXPECT_FALSE(ResetDirection.m_Right);

	Message.m_IntendedTick = 1;
	Message.m_Direction = 1;
	Message.m_Jump = 0;
	Input.Update(Message);
	EXPECT_TRUE(QmResolveWaterDirection(200, false, 0, 0, Input).m_Right);
}

TEST(QmWaterHammerIndicator, PreInputUpdateRetainsHammerAndHookState)
{
	CNetMsg_Sv_PreInput Message{};
	Message.m_WantedWeapon = WEAPON_HAMMER + 1;
	Message.m_Fire = 1;
	Message.m_Hook = 1;
	SQmWaterHammerInputState Input;
	Input.Update(Message);
	EXPECT_TRUE(QmShouldMarkWaterHammer(200, false, WEAPON_GUN, Input, nullptr));
	EXPECT_TRUE(QmShouldMarkWaterHook(200, false, Input, nullptr));

	Message.m_Fire = 2;
	Message.m_Hook = 0;
	Input.Update(Message);
	EXPECT_FALSE(QmShouldMarkWaterHammer(200, false, WEAPON_GUN, Input, nullptr));
	EXPECT_FALSE(QmShouldMarkWaterHook(200, false, Input, nullptr));
}

TEST(QmWaterHammerIndicator, InvalidPreInputTickDoesNotReplaceSnapshotFallback)
{
	CNetMsg_Sv_PreInput Message{};
	Message.m_IntendedTick = -1;
	Message.m_Direction = -1;
	Message.m_Jump = 1;
	SQmWaterHammerInputState Input;
	Input.Update(Message);
	const auto Direction = QmResolveWaterDirection(200, false, 1, 0, Input);
	EXPECT_FALSE(Direction.m_Left);
	EXPECT_FALSE(Direction.m_Jump);
	EXPECT_TRUE(Direction.m_Right);
}
