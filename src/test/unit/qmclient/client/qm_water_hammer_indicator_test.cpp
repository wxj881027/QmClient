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
