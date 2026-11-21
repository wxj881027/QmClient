#include <game/client/components/qmclient/auto_team_lock.h>

#include <gtest/gtest.h>

namespace
{
	constexpr int TICK_SPEED = 50;
	const std::array<int, NUM_DUMMIES> TEAM_SUPERS = {TEAM_SUPER, TEAM_SUPER};

	SQmAutoTeamLockPlan Update(
		CQmAutoTeamLockState &State,
		const std::array<int, NUM_DUMMIES> &aTeams,
		const std::array<int64_t, NUM_DUMMIES> &aGameTicks,
		const std::array<bool, NUM_DUMMIES> &aConnected,
		bool Enabled = true,
		int DelaySeconds = 0)
	{
		return State.Update(Enabled, DelaySeconds, TICK_SPEED, aTeams, TEAM_SUPERS, aGameTicks, aConnected);
	}
}

TEST(QmAutoTeamLock, LocksMainAndDummyTeamsIndependently)
{
	CQmAutoTeamLockState State;
	const SQmAutoTeamLockPlan Plan = Update(
		State,
		{1, 2},
		{100, 100},
		{true, true});

	ASSERT_EQ(Plan.m_RequestCount, 2);
	EXPECT_EQ(Plan.m_aRequests[0].m_Connection, 0);
	EXPECT_EQ(Plan.m_aRequests[0].m_Team, 1);
	EXPECT_EQ(Plan.m_aRequests[1].m_Connection, 1);
	EXPECT_EQ(Plan.m_aRequests[1].m_Team, 2);
}

TEST(QmAutoTeamLock, LocksSharedTeamOnlyOnce)
{
	CQmAutoTeamLockState State;
	const SQmAutoTeamLockPlan Plan = Update(
		State,
		{3, 3},
		{100, 100},
		{true, true});

	ASSERT_EQ(Plan.m_RequestCount, 1);
	EXPECT_EQ(Plan.m_aRequests[0].m_Connection, 0);
	EXPECT_EQ(Plan.m_aRequests[0].m_Team, 3);
}

TEST(QmAutoTeamLock, StaggeredSharedTeamDoesNotRelock)
{
	CQmAutoTeamLockState State;
	const std::array<int, NUM_DUMMIES> aTeams = {3, 3};

	EXPECT_EQ(Update(State, aTeams, {0, 0}, {true, false}, true, 1).m_RequestCount, 0);
	EXPECT_EQ(Update(State, aTeams, {30, 30}, {true, true}, true, 1).m_RequestCount, 0);
	const SQmAutoTeamLockPlan Plan = Update(State, aTeams, {50, 50}, {true, true}, true, 1);
	ASSERT_EQ(Plan.m_RequestCount, 1);
	EXPECT_EQ(Plan.m_aRequests[0].m_Team, 3);
	EXPECT_EQ(Update(State, aTeams, {80, 80}, {true, true}, true, 1).m_RequestCount, 0);
}

TEST(QmAutoTeamLock, WaitsForConfiguredDelayPerConnection)
{
	CQmAutoTeamLockState State;
	const std::array<int, NUM_DUMMIES> aTeams = {1, TEAM_FLOCK};
	const std::array<bool, NUM_DUMMIES> aConnected = {true, false};

	EXPECT_EQ(Update(State, aTeams, {100, 0}, aConnected, true, 2).m_RequestCount, 0);
	EXPECT_EQ(Update(State, aTeams, {199, 0}, aConnected, true, 2).m_RequestCount, 0);
	const SQmAutoTeamLockPlan Plan = Update(State, aTeams, {200, 0}, aConnected, true, 2);
	ASSERT_EQ(Plan.m_RequestCount, 1);
	EXPECT_EQ(Plan.m_aRequests[0].m_Connection, 0);
	EXPECT_EQ(Plan.m_aRequests[0].m_Team, 1);
}

TEST(QmAutoTeamLock, DoesNotRelockUnchangedTeam)
{
	CQmAutoTeamLockState State;
	const std::array<int, NUM_DUMMIES> aTeams = {1, 2};
	const std::array<int64_t, NUM_DUMMIES> aTicks = {100, 100};
	const std::array<bool, NUM_DUMMIES> aConnected = {true, true};

	ASSERT_EQ(Update(State, aTeams, aTicks, aConnected).m_RequestCount, 2);
	EXPECT_EQ(Update(State, aTeams, aTicks, aConnected).m_RequestCount, 0);
}

TEST(QmAutoTeamLock, DisabledDoesNotLock)
{
	CQmAutoTeamLockState State;
	EXPECT_EQ(Update(State, {1, 2}, {100, 100}, {true, true}, false).m_RequestCount, 0);
}

TEST(QmAutoTeamLock, SuppressedSoloTeamDoesNotBecomeDelayedRequest)
{
	CQmAutoTeamLockState State;
	const std::array<int, NUM_DUMMIES> aTeams = {1, 2};
	const std::array<int64_t, NUM_DUMMIES> aTicks = {100, 100};
	const std::array<bool, NUM_DUMMIES> aConnected = {true, true};
	const std::array<bool, NUM_DUMMIES> aSuppress = {true, true};

	EXPECT_EQ(State.Update(true, 0, TICK_SPEED, aTeams, TEAM_SUPERS, aTicks, aConnected, aSuppress).m_RequestCount, 0);
	EXPECT_EQ(State.Update(true, 0, TICK_SPEED, aTeams, TEAM_SUPERS, aTicks, aConnected).m_RequestCount, 0);
}

TEST(QmAutoTeamLock, IgnoresUnlockableTeams)
{
	CQmAutoTeamLockState State;
	EXPECT_EQ(Update(State, {TEAM_FLOCK, TEAM_SUPER}, {100, 100}, {true, true}).m_RequestCount, 0);
}

TEST(QmAutoTeamLock, UsesPerConnectionTeamSuper)
{
	CQmAutoTeamLockState State;
	const std::array<int, NUM_DUMMIES> aTeams = {2, 2};
	const std::array<int, NUM_DUMMIES> aTeamSupers = {3, 2};
	const std::array<int64_t, NUM_DUMMIES> aTicks = {100, 100};
	const std::array<bool, NUM_DUMMIES> aConnected = {true, true};

	const SQmAutoTeamLockPlan Plan = State.Update(true, 0, TICK_SPEED, aTeams, aTeamSupers, aTicks, aConnected);
	ASSERT_EQ(Plan.m_RequestCount, 1);
	EXPECT_EQ(Plan.m_aRequests[0].m_Connection, 0);
	EXPECT_EQ(Plan.m_aRequests[0].m_Team, 2);
}

TEST(QmAutoTeamLock, TeamChangeRestartsDelay)
{
	CQmAutoTeamLockState State;
	const std::array<bool, NUM_DUMMIES> aConnected = {true, false};

	EXPECT_EQ(Update(State, {1, TEAM_FLOCK}, {100, 0}, aConnected, true, 1).m_RequestCount, 0);
	EXPECT_EQ(Update(State, {2, TEAM_FLOCK}, {120, 0}, aConnected, true, 1).m_RequestCount, 0);
	EXPECT_EQ(Update(State, {2, TEAM_FLOCK}, {169, 0}, aConnected, true, 1).m_RequestCount, 0);
	const SQmAutoTeamLockPlan Plan = Update(State, {2, TEAM_FLOCK}, {170, 0}, aConnected, true, 1);
	ASSERT_EQ(Plan.m_RequestCount, 1);
	EXPECT_EQ(Plan.m_aRequests[0].m_Team, 2);
}

TEST(QmAutoTeamLock, DisconnectResetsPendingState)
{
	CQmAutoTeamLockState State;
	const std::array<int, NUM_DUMMIES> aTeams = {TEAM_FLOCK, 4};

	EXPECT_EQ(Update(State, aTeams, {0, 100}, {true, true}, true, 2).m_RequestCount, 0);
	EXPECT_EQ(Update(State, aTeams, {0, 300}, {true, false}, true, 2).m_RequestCount, 0);
	EXPECT_EQ(Update(State, aTeams, {0, 400}, {true, true}, true, 2).m_RequestCount, 0);
	const SQmAutoTeamLockPlan Plan = Update(State, aTeams, {0, 500}, {true, true}, true, 2);
	ASSERT_EQ(Plan.m_RequestCount, 1);
	EXPECT_EQ(Plan.m_aRequests[0].m_Connection, 1);
	EXPECT_EQ(Plan.m_aRequests[0].m_Team, 4);
}
