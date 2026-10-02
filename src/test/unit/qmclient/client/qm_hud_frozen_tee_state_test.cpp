#include <game/client/components/hud_frozen_tee_state.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>

TEST(QmHudFrozenTeeState, ConfirmedDeathSuppressesStaleTimedAndDeepFreeze)
{
	SHudFrozenTeeState State;
	EXPECT_TRUE(QmHudTeeIsFrozen(State, 200, false));
	EXPECT_TRUE(QmHudTeeIsFrozen(State, -1, true));

	QmHudMarkTeeDead(State, 100, 105);

	EXPECT_TRUE(State.m_DeathOverride);
	EXPECT_EQ(State.m_DeathBarrierTick, 105);
	EXPECT_FALSE(QmHudTeeIsFrozen(State, 200, false));
	EXPECT_FALSE(QmHudTeeIsFrozen(State, -1, true));
}

TEST(QmHudFrozenTeeState, BufferedPreDeathCharacterCannotRemoveDeathOverride)
{
	SHudFrozenTeeState State;
	QmHudMarkTeeDead(State, 100, 105);

	QmHudObserveTeeCharacterSnapshot(State, true, 104);
	EXPECT_TRUE(State.m_DeathOverride);
	QmHudObserveTeeCharacterSnapshot(State, true, 105);
	EXPECT_TRUE(State.m_DeathOverride);
}

TEST(QmHudFrozenTeeState, NetworkClippingDoesNotPretendTheTeeRespawned)
{
	SHudFrozenTeeState AliveState;
	QmHudObserveTeeCharacterSnapshot(AliveState, false, 200);
	EXPECT_TRUE(QmHudTeeIsFrozen(AliveState, 250, false));

	SHudFrozenTeeState DeadState;
	QmHudMarkTeeDead(DeadState, 100, 105);
	QmHudObserveTeeCharacterSnapshot(DeadState, false, 200);
	EXPECT_TRUE(DeadState.m_DeathOverride);
	EXPECT_FALSE(QmHudTeeIsFrozen(DeadState, 250, false));
}

TEST(QmHudFrozenTeeState, FreshRespawnUsesTheNewFreezeState)
{
	SHudFrozenTeeState State;
	QmHudMarkTeeDead(State, 100, 105);

	QmHudObserveTeeCharacterSnapshot(State, true, 106);

	EXPECT_FALSE(State.m_DeathOverride);
	EXPECT_EQ(State.m_DeathBarrierTick, -1);
	EXPECT_FALSE(QmHudTeeIsFrozen(State, 0, false));
	EXPECT_TRUE(QmHudTeeIsFrozen(State, 200, false));
	EXPECT_TRUE(QmHudTeeIsFrozen(State, -1, true));
}
