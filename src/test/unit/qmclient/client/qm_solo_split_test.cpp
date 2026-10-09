#include <game/client/components/qmclient/solo_split_state.h>

#include <gtest/gtest.h>

namespace
{
	constexpr int64_t TICK_FREQUENCY = 100;
	const std::array<int, 2> PREVIOUS_TEAMS = {3, 4};
	const std::array<int, 2> TARGET_TEAMS = {1, 2};
}

TEST(QmSoloSplit, ConnectionReadyBeforeSnapshotKeepsRequestedAction)
{
	CQmSoloSplitState State;
	State.WaitForPlayers(1, 0, TICK_FREQUENCY);
	EXPECT_EQ(State.TakeReadyAction(true, true, false, 100), 0);
	EXPECT_TRUE(State.Pending());
	EXPECT_EQ(State.TakeReadyAction(true, true, true, 200), 1);
	EXPECT_FALSE(State.Pending());
	EXPECT_EQ(State.TakeReadyAction(true, true, true, 201), 0);
}

TEST(QmSoloSplit, SnapshotWaitExpiresEvenWithConnectedDummy)
{
	CQmSoloSplitState State;
	State.WaitForPlayers(1, 0, TICK_FREQUENCY);
	EXPECT_EQ(State.TakeReadyAction(true, true, false, 999), 0);
	EXPECT_TRUE(State.Pending());
	EXPECT_EQ(State.TakeReadyAction(true, true, false, 1000), 0);
	EXPECT_FALSE(State.Pending());
	EXPECT_EQ(State.TakeReadyAction(true, true, true, 1001), 0);
}

TEST(QmSoloSplit, ConnectionTimeoutAllowsAnotherRequest)
{
	CQmSoloSplitState State;
	State.WaitForPlayers(1, 0, TICK_FREQUENCY);
	EXPECT_EQ(State.TakeReadyAction(true, false, false, 1000), 0);
	EXPECT_FALSE(State.Pending());
	State.WaitForPlayers(1, 1001, TICK_FREQUENCY);
	EXPECT_EQ(State.TakeReadyAction(true, true, true, 1002), 1);
}

TEST(QmSoloSplit, OfflineStateCancelsSnapshotWait)
{
	CQmSoloSplitState State;
	State.WaitForPlayers(1, 0, TICK_FREQUENCY);
	EXPECT_EQ(State.TakeReadyAction(false, true, true, 1), 0);
	EXPECT_FALSE(State.Pending());
	EXPECT_EQ(State.TakeReadyAction(true, true, true, 2), 0);
}

TEST(QmSoloSplit, RepeatedRequestDoesNotReplacePendingActionOrExtendTimeout)
{
	CQmSoloSplitState State;
	State.WaitForPlayers(1, 0, TICK_FREQUENCY);
	State.WaitForPlayers(2, 500, TICK_FREQUENCY);
	EXPECT_EQ(State.TakeReadyAction(true, true, true, 600), 1);
	State.WaitForPlayers(1, 1000, TICK_FREQUENCY);
	State.WaitForPlayers(1, 1500, TICK_FREQUENCY);
	EXPECT_EQ(State.TakeReadyAction(true, true, false, 2000), 0);
	EXPECT_FALSE(State.Pending());
}

TEST(QmSoloSplit, RetriesTwiceThenRestoresPreviousTeams)
{
	CQmSoloSplitState State;
	State.StartTeams(1, PREVIOUS_TEAMS, TARGET_TEAMS, 0);
	EXPECT_EQ(State.UpdateTeams(true, true, true, PREVIOUS_TEAMS, 0, TICK_FREQUENCY), TARGET_TEAMS);
	EXPECT_FALSE(State.UpdateTeams(true, true, true, PREVIOUS_TEAMS, 199, TICK_FREQUENCY).has_value());
	EXPECT_EQ(State.UpdateTeams(true, true, true, PREVIOUS_TEAMS, 200, TICK_FREQUENCY), TARGET_TEAMS);
	EXPECT_EQ(State.UpdateTeams(true, true, true, {1, 4}, 400, TICK_FREQUENCY), PREVIOUS_TEAMS);
	EXPECT_FALSE(State.Pending());
	EXPECT_FALSE(State.UpdateTeams(true, true, true, PREVIOUS_TEAMS, 401, TICK_FREQUENCY).has_value());
}

TEST(QmSoloSplit, ReachingBothTargetTeamsFinishesWithoutFurtherCommands)
{
	CQmSoloSplitState State;
	State.StartTeams(1, PREVIOUS_TEAMS, TARGET_TEAMS, 0);
	ASSERT_TRUE(State.UpdateTeams(true, true, true, PREVIOUS_TEAMS, 0, TICK_FREQUENCY).has_value());
	EXPECT_FALSE(State.UpdateTeams(true, true, true, TARGET_TEAMS, 1, TICK_FREQUENCY).has_value());
	EXPECT_FALSE(State.Pending());
}

TEST(QmSoloSplit, DummyDisconnectCancelsRetryAndCannotResumeOldTeams)
{
	CQmSoloSplitState State;
	State.StartTeams(1, PREVIOUS_TEAMS, TARGET_TEAMS, 0);
	ASSERT_TRUE(State.UpdateTeams(true, true, true, PREVIOUS_TEAMS, 0, TICK_FREQUENCY).has_value());
	EXPECT_FALSE(State.UpdateTeams(true, false, false, PREVIOUS_TEAMS, 1, TICK_FREQUENCY).has_value());
	EXPECT_FALSE(State.Pending());
	EXPECT_FALSE(State.UpdateTeams(true, true, true, {8, 9}, 400, TICK_FREQUENCY).has_value());
	State.StartTeams(2, {8, 9}, {0, 0}, 401);
	EXPECT_EQ(State.UpdateTeams(true, true, true, {8, 9}, 401, TICK_FREQUENCY), (std::array<int, 2>{0, 0}));
}

TEST(QmSoloSplit, OfflineStateCancelsRetryWithoutRollbackOnNewServer)
{
	CQmSoloSplitState State;
	State.StartTeams(1, PREVIOUS_TEAMS, TARGET_TEAMS, 0);
	EXPECT_FALSE(State.UpdateTeams(false, true, true, PREVIOUS_TEAMS, 0, TICK_FREQUENCY).has_value());
	EXPECT_FALSE(State.Pending());
	EXPECT_FALSE(State.UpdateTeams(true, true, true, PREVIOUS_TEAMS, 500, TICK_FREQUENCY).has_value());
}

TEST(QmSoloSplit, InvalidatedPlayerIdsCancelRetryWithoutSendingCommands)
{
	CQmSoloSplitState State;
	State.StartTeams(1, PREVIOUS_TEAMS, TARGET_TEAMS, 0);
	EXPECT_FALSE(State.UpdateTeams(true, true, false, PREVIOUS_TEAMS, 0, TICK_FREQUENCY).has_value());
	EXPECT_FALSE(State.Pending());
}

TEST(QmSoloSplit, ResetCancelsWaitAndRetryBeforeConnectionReplacement)
{
	CQmSoloSplitState State;
	State.WaitForPlayers(1, 0, TICK_FREQUENCY);
	State.Reset();
	EXPECT_EQ(State.TakeReadyAction(true, true, true, 1), 0);
	State.StartTeams(1, PREVIOUS_TEAMS, TARGET_TEAMS, 2);
	State.Reset();
	EXPECT_FALSE(State.UpdateTeams(true, true, true, PREVIOUS_TEAMS, 500, TICK_FREQUENCY).has_value());
	EXPECT_FALSE(State.Pending());
}
