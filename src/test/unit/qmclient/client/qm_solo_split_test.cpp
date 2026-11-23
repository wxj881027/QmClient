#include <game/client/components/qmclient/solo_split_state.h>

#include <gtest/gtest.h>

namespace
{
	constexpr int64_t TICK_FREQUENCY = 100;
	const std::array<int, 2> PREVIOUS_TEAMS = {3, 4};
	const std::array<int, 2> TARGET_TEAMS = {1, 2};
}

TEST(QmSoloSplitTeamSelection, ReusesDistinctCurrentTeams)
{
	const std::array<bool, NUM_DDRACE_TEAMS> aOccupied{};
	const SQmSoloSplitTeamSelection Selection = QmSelectSoloSplitTeams(5, 6, TEAM_SUPER, aOccupied);

	EXPECT_TRUE(Selection.Valid());
	EXPECT_EQ(Selection.m_Main, 5);
	EXPECT_EQ(Selection.m_Dummy, 6);
}

TEST(QmSoloSplitTeamSelection, KeepsMainTeamAndMovesDummyWhenTheyShare)
{
	std::array<bool, NUM_DDRACE_TEAMS> aOccupied{};
	aOccupied[1] = true;
	const SQmSoloSplitTeamSelection Selection = QmSelectSoloSplitTeams(5, 5, TEAM_SUPER, aOccupied);

	EXPECT_TRUE(Selection.Valid());
	EXPECT_EQ(Selection.m_Main, 5);
	EXPECT_EQ(Selection.m_Dummy, 2);
}

TEST(QmSoloSplitTeamSelection, ReusesMainTeamAndFindsEmptyDummyTeam)
{
	std::array<bool, NUM_DDRACE_TEAMS> aOccupied{};
	aOccupied[1] = true;
	aOccupied[2] = true;
	const SQmSoloSplitTeamSelection Selection = QmSelectSoloSplitTeams(5, TEAM_FLOCK, TEAM_SUPER, aOccupied);

	EXPECT_TRUE(Selection.Valid());
	EXPECT_EQ(Selection.m_Main, 5);
	EXPECT_EQ(Selection.m_Dummy, 3);
}

TEST(QmSoloSplitTeamSelection, IgnoresLocalOldTeamsWhenFindingTwoEmptyTeams)
{
	std::array<bool, NUM_DDRACE_TEAMS> aOccupied{};
	aOccupied[1] = true;
	const SQmSoloSplitTeamSelection Selection = QmSelectSoloSplitTeams(TEAM_FLOCK, TEAM_FLOCK, TEAM_SUPER, aOccupied);

	EXPECT_TRUE(Selection.Valid());
	EXPECT_EQ(Selection.m_Main, 2);
	EXPECT_EQ(Selection.m_Dummy, 3);
}

TEST(QmSoloSplitTeamSelection, RejectsWhenNoTwoTeamsAreFree)
{
	std::array<bool, NUM_DDRACE_TEAMS> aOccupied{};
	for(int Team = TEAM_FLOCK + 1; Team < TEAM_SUPER; ++Team)
		aOccupied[Team] = true;
	const SQmSoloSplitTeamSelection Selection = QmSelectSoloSplitTeams(TEAM_FLOCK, TEAM_FLOCK, TEAM_SUPER, aOccupied);

	EXPECT_FALSE(Selection.Valid());
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

TEST(QmSoloSplit, EnterSendsFirstTeamCommandEvenWhenStateAlreadyMatches)
{
	CQmSoloSplitState State;
	State.StartTeams(1, TARGET_TEAMS, TARGET_TEAMS, 0);
	EXPECT_EQ(State.UpdateTeams(true, true, true, TARGET_TEAMS, 0, TICK_FREQUENCY), TARGET_TEAMS);
	EXPECT_FALSE(State.UpdateTeams(true, true, true, TARGET_TEAMS, 1, TICK_FREQUENCY).has_value());
	EXPECT_FALSE(State.Pending());
}

TEST(QmSoloSplit, ReachedEnterTargetRequiresBothTeamsAndEnterAction)
{
	CQmSoloSplitState State;
	State.StartTeams(1, PREVIOUS_TEAMS, TARGET_TEAMS, 0);
	EXPECT_FALSE(State.ReachedEnterTarget(PREVIOUS_TEAMS));
	EXPECT_FALSE(State.ReachedEnterTarget({1, 4}));
	EXPECT_TRUE(State.ReachedEnterTarget(TARGET_TEAMS));

	State.Reset();
	State.StartTeams(2, TARGET_TEAMS, PREVIOUS_TEAMS, 0);
	EXPECT_FALSE(State.ReachedEnterTarget(PREVIOUS_TEAMS));
}

TEST(QmSoloSplit, EnterTeamCommandIncludesImmediateLock)
{
	char aCmd[64];

	ASSERT_TRUE(QmBuildSoloSplitTeamCommand(aCmd, sizeof(aCmd), TARGET_TEAMS[0], true));
	EXPECT_STREQ(aCmd, "/mc;team 1; lock 1");

	ASSERT_TRUE(QmBuildSoloSplitTeamCommand(aCmd, sizeof(aCmd), TARGET_TEAMS[1], false));
	EXPECT_STREQ(aCmd, "/team 2");
}

TEST(QmSoloSplit, ParsesEnglishAndChineseLockConfirmationsForOnlyTheLocalPlayer)
{
	const SQmSoloSplitTeamLockMessage EnglishLock = QmParseSoloSplitTeamLockMessage("'SoloMain' locked your team. After the race starts, killing will kill everyone in your team.", "SoloMain");
	EXPECT_TRUE(EnglishLock.m_Recognized);
	EXPECT_TRUE(EnglishLock.m_Locked);

	const SQmSoloSplitTeamLockMessage ChineseLock = QmParseSoloSplitTeamLockMessage("'SoloMain' 锁定了你们的队伍。比赛开始后，任何人 kill 都会导致整队死亡", "SoloMain");
	EXPECT_TRUE(ChineseLock.m_Recognized);
	EXPECT_TRUE(ChineseLock.m_Locked);

	const SQmSoloSplitTeamLockMessage EnglishUnlock = QmParseSoloSplitTeamLockMessage("'SoloMain' unlocked your team", "SoloMain");
	EXPECT_TRUE(EnglishUnlock.m_Recognized);
	EXPECT_FALSE(EnglishUnlock.m_Locked);

	const SQmSoloSplitTeamLockMessage ChineseUnlock = QmParseSoloSplitTeamLockMessage("'SoloMain' 解锁了你们的队伍", "SoloMain");
	EXPECT_TRUE(ChineseUnlock.m_Recognized);
	EXPECT_FALSE(ChineseUnlock.m_Locked);

	EXPECT_FALSE(QmParseSoloSplitTeamLockMessage("'SoloDummy' 锁定了你们的队伍", "SoloMain").m_Recognized);
	EXPECT_FALSE(QmParseSoloSplitTeamLockMessage("'SoloMain' joined team 1", "SoloMain").m_Recognized);
}

TEST(QmSoloSplit, LockHintsRequireServerConfirmationAndTrackBothConnections)
{
	CQmSoloSplitLockHints Hints;
	Hints.ExpectLock(0, 1);
	Hints.ExpectLock(1, 2);
	EXPECT_FALSE(Hints.IsTeamLocked(1));
	EXPECT_FALSE(Hints.IsTeamLocked(2));

	EXPECT_TRUE(Hints.ApplyConfirmation(0, true));
	EXPECT_TRUE(Hints.IsTeamLocked(1));
	EXPECT_FALSE(Hints.IsTeamLocked(2));

	EXPECT_TRUE(Hints.ApplyConfirmation(1, true));
	EXPECT_TRUE(Hints.IsTeamLocked(2));

	Hints.ExpectLock(0, TEAM_FLOCK);
	Hints.ExpectLock(0, TEAM_SUPER);
	EXPECT_FALSE(Hints.ApplyConfirmation(0, false));
	EXPECT_TRUE(Hints.IsTeamLocked(1));

	Hints.ExpectLock(0, 1);
	EXPECT_TRUE(Hints.ApplyConfirmation(0, false));
	EXPECT_FALSE(Hints.IsTeamLocked(1));

	Hints.ResetConnection(1);
	EXPECT_FALSE(Hints.IsTeamLocked(2));
}

TEST(QmSoloSplit, LeaveModeNeverLocksRestoredTeam)
{
	CQmSoloSplitState State;
	State.StartTeams(2, TARGET_TEAMS, PREVIOUS_TEAMS, 0);
	EXPECT_FALSE(State.LocksEnteredTeams());

	State.Reset();
	State.StartTeams(1, PREVIOUS_TEAMS, TARGET_TEAMS, 0);
	EXPECT_TRUE(State.LocksEnteredTeams());
}

TEST(QmSoloSplit, LeavingSplitSendsTeamZeroForBothConnections)
{
	CQmSoloSplitState State;
	const std::array<int, 2> aRestoreTeams = {TEAM_FLOCK, TEAM_FLOCK};
	State.StartTeams(2, TARGET_TEAMS, aRestoreTeams, 0);
	const auto Teams = State.UpdateTeams(true, true, true, TARGET_TEAMS, 0, TICK_FREQUENCY);
	ASSERT_TRUE(Teams.has_value());
	for(int Team : *Teams)
	{
		char aCmd[64];
		ASSERT_TRUE(QmBuildSoloSplitTeamCommand(aCmd, sizeof(aCmd), Team, State.LocksEnteredTeams()));
		EXPECT_STREQ(aCmd, "/team 0");
	}
	EXPECT_FALSE(State.UpdateTeams(true, true, true, aRestoreTeams, 1, TICK_FREQUENCY).has_value());
	EXPECT_FALSE(State.Pending());
}

TEST(QmSoloSplit, CannotBuildLockCommandForTeamZero)
{
	char aCmd[64];
	EXPECT_FALSE(QmBuildSoloSplitTeamCommand(aCmd, sizeof(aCmd), TEAM_FLOCK, true));
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
