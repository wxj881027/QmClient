#include <game/client/components/qmclient/gores_drown_tracker.h>

#include <gtest/gtest.h>

namespace
{
	void Observe(CQmGoresDrownTracker &Tracker, int ClientId, bool Frozen, bool Hooking = false, int Team = 1, bool IncludeTeamZero = true)
	{
		Tracker.Observe(ClientId, true, "player", "clan", Team, true, Frozen, Hooking, IncludeTeamZero);
	}
}

TEST(QmGoresDrownTracker, TeamZeroCountsAndAppearsWhenIncluded)
{
	CQmGoresDrownTracker Tracker;
	Observe(Tracker, 1, false, false, TEAM_FLOCK);
	Observe(Tracker, 1, true, false, TEAM_FLOCK);
	Observe(Tracker, 1, true, true, TEAM_FLOCK);
	EXPECT_EQ(Tracker.Count(1), 1);
	EXPECT_TRUE(CQmGoresDrownTracker::IsBoardVisible(true, true, TEAM_FLOCK, true));
	EXPECT_TRUE(CQmGoresDrownTracker::IsSameTrackedTeam(TEAM_FLOCK, TEAM_FLOCK, true));
	EXPECT_FALSE(CQmGoresDrownTracker::IsSameTrackedTeam(TEAM_FLOCK, 1, true));
	Observe(Tracker, 1, false, false, TEAM_FLOCK);
	Observe(Tracker, 1, false, true, TEAM_FLOCK);
	Observe(Tracker, 1, true, false, TEAM_FLOCK);
	EXPECT_EQ(Tracker.Count(1), 2);
}

TEST(QmGoresDrownTracker, TeamZeroDoesNotCountOrAppearWhenExcluded)
{
	CQmGoresDrownTracker Tracker;
	Observe(Tracker, 1, false, false, TEAM_FLOCK, false);
	Observe(Tracker, 1, true, false, TEAM_FLOCK, false);
	EXPECT_EQ(Tracker.Count(1), 0);
	EXPECT_FALSE(CQmGoresDrownTracker::IsBoardVisible(true, true, TEAM_FLOCK, false));
	EXPECT_FALSE(CQmGoresDrownTracker::IsSameTrackedTeam(TEAM_FLOCK, TEAM_FLOCK, false));
}

TEST(QmGoresDrownTracker, ExcludingThenIncludingTeamZeroClearsCountAndRebuildsBaseline)
{
	CQmGoresDrownTracker Tracker;
	Observe(Tracker, 1, false, false, TEAM_FLOCK);
	Observe(Tracker, 1, true, false, TEAM_FLOCK);
	ASSERT_EQ(Tracker.Count(1), 1);
	Observe(Tracker, 1, true, true, TEAM_FLOCK, false);
	EXPECT_EQ(Tracker.Count(1), 0);
	Observe(Tracker, 1, true, true, TEAM_FLOCK);
	Observe(Tracker, 1, false, false, TEAM_FLOCK);
	Observe(Tracker, 1, true, false, TEAM_FLOCK);
	EXPECT_EQ(Tracker.Count(1), 0);
	Observe(Tracker, 1, false, false, TEAM_FLOCK);
	Observe(Tracker, 1, false, true, TEAM_FLOCK);
	Observe(Tracker, 1, true, false, TEAM_FLOCK);
	EXPECT_EQ(Tracker.Count(1), 1);
}

TEST(QmGoresDrownTracker, TeamZeroPolicyChangesPreserveNonZeroCountAndRearming)
{
	CQmGoresDrownTracker Tracker;
	Observe(Tracker, 1, false);
	Observe(Tracker, 1, true);
	Observe(Tracker, 1, true, false, 1, false);
	EXPECT_EQ(Tracker.Count(1), 1);
	Observe(Tracker, 1, false, false, 1, false);
	Observe(Tracker, 1, false, true, 1, false);
	Observe(Tracker, 1, true);
	EXPECT_EQ(Tracker.Count(1), 2);
}

TEST(QmGoresDrownTracker, BoardIncludesOnlyMatchingDdraceTeamWithEitherTeamZeroPolicy)
{
	for(bool IncludeTeamZero : {false, true})
	{
		SCOPED_TRACE(IncludeTeamZero);
		EXPECT_TRUE(CQmGoresDrownTracker::IsSameTrackedTeam(3, 3, IncludeTeamZero));
		EXPECT_FALSE(CQmGoresDrownTracker::IsSameTrackedTeam(3, 4, IncludeTeamZero));
		EXPECT_FALSE(CQmGoresDrownTracker::IsSameTrackedTeam(3, TEAM_FLOCK, IncludeTeamZero));
	}
}

TEST(QmGoresDrownTracker, FirstFallCountsOnceWhileFreezeContinues)
{
	CQmGoresDrownTracker Tracker;
	Observe(Tracker, 1, false);
	Observe(Tracker, 1, true);
	Observe(Tracker, 1, true);
	Observe(Tracker, 1, true, true);
	EXPECT_EQ(Tracker.Count(1), 1);
}

TEST(QmGoresDrownTracker, InitiallyFrozenPlayerEstablishesBaseline)
{
	CQmGoresDrownTracker Tracker;
	Observe(Tracker, 1, true);
	EXPECT_EQ(Tracker.Count(1), 0);
	Observe(Tracker, 1, false);
	Observe(Tracker, 1, true);
	EXPECT_EQ(Tracker.Count(1), 0);
	Observe(Tracker, 1, false);
	Observe(Tracker, 1, false, true);
	Observe(Tracker, 1, true);
	EXPECT_EQ(Tracker.Count(1), 1);
}

TEST(QmGoresDrownTracker, RescueNeedsNewHookBeforeNextFall)
{
	CQmGoresDrownTracker Tracker;
	Observe(Tracker, 1, false);
	Observe(Tracker, 1, true);
	Observe(Tracker, 1, false);
	Observe(Tracker, 1, true);
	EXPECT_EQ(Tracker.Count(1), 1);
	Observe(Tracker, 1, false);
	Observe(Tracker, 1, false, true);
	Observe(Tracker, 1, true);
	EXPECT_EQ(Tracker.Count(1), 2);
}

TEST(QmGoresDrownTracker, HookStartedWhileFrozenDoesNotRearmAfterRescue)
{
	CQmGoresDrownTracker Tracker;
	Observe(Tracker, 1, false);
	Observe(Tracker, 1, true);
	Observe(Tracker, 1, true, true);
	Observe(Tracker, 1, false, true);
	Observe(Tracker, 1, true, true);
	EXPECT_EQ(Tracker.Count(1), 1);
	Observe(Tracker, 1, false, true);
	Observe(Tracker, 1, false, false);
	Observe(Tracker, 1, false, true);
	Observe(Tracker, 1, true);
	EXPECT_EQ(Tracker.Count(1), 2);
}

TEST(QmGoresDrownTracker, TeammatesHaveIndependentCounts)
{
	CQmGoresDrownTracker Tracker;
	Observe(Tracker, 1, false);
	Observe(Tracker, 2, false);
	Observe(Tracker, 1, true);
	Observe(Tracker, 2, true);
	Observe(Tracker, 1, false);
	Observe(Tracker, 1, false, true);
	Observe(Tracker, 1, true);
	EXPECT_EQ(Tracker.Count(1), 2);
	EXPECT_EQ(Tracker.Count(2), 1);
}

TEST(QmGoresDrownTracker, ChangingDdraceTeamStartsNewCount)
{
	CQmGoresDrownTracker Tracker;
	Observe(Tracker, 1, false);
	Observe(Tracker, 1, true);
	Observe(Tracker, 1, true, false, 2);
	EXPECT_EQ(Tracker.Count(1), 0);
	Observe(Tracker, 1, false, false, 2);
	Observe(Tracker, 1, false, true, 2);
	Observe(Tracker, 1, true, false, 2);
	EXPECT_EQ(Tracker.Count(1), 1);
}

TEST(QmGoresDrownTracker, ReusedClientIdCannotInheritDepartedPlayersCount)
{
	CQmGoresDrownTracker Tracker;
	Observe(Tracker, 1, false);
	Observe(Tracker, 1, true);
	Tracker.Observe(1, false, "player", "clan", 1, false, false, false, true);
	Observe(Tracker, 1, true);
	EXPECT_EQ(Tracker.Count(1), 0);
}

TEST(QmGoresDrownTracker, IdentityChangeCannotInheritPreviousCount)
{
	CQmGoresDrownTracker Tracker;
	Observe(Tracker, 1, false);
	Observe(Tracker, 1, true);
	Tracker.Observe(1, true, "replacement", "clan", 1, true, true, false, true);
	EXPECT_EQ(Tracker.Count(1), 0);
}

TEST(QmGoresDrownTracker, MissingCharacterKeepsCountAndRebuildsFreezeBaseline)
{
	CQmGoresDrownTracker Tracker;
	Observe(Tracker, 1, false);
	Observe(Tracker, 1, true);
	Tracker.Observe(1, true, "player", "clan", 1, false, false, false, true);
	EXPECT_EQ(Tracker.Count(1), 1);
	Observe(Tracker, 1, true);
	Observe(Tracker, 1, false);
	Observe(Tracker, 1, true);
	EXPECT_EQ(Tracker.Count(1), 1);
}

TEST(QmGoresDrownTracker, ResetClearsCountsAndFirstObservationState)
{
	CQmGoresDrownTracker Tracker;
	Observe(Tracker, 1, false);
	Observe(Tracker, 1, true);
	Tracker.Reset();
	EXPECT_EQ(Tracker.Count(1), 0);
	Observe(Tracker, 1, false);
	Observe(Tracker, 1, true);
	EXPECT_EQ(Tracker.Count(1), 1);
}

TEST(QmGoresDrownTracker, BoardHidesOutsideGoresOrWithoutLocalPlayer)
{
	for(bool IncludeTeamZero : {false, true})
	{
		SCOPED_TRACE(IncludeTeamZero);
		EXPECT_FALSE(CQmGoresDrownTracker::IsBoardVisible(false, true, 1, IncludeTeamZero));
		EXPECT_FALSE(CQmGoresDrownTracker::IsBoardVisible(false, true, TEAM_FLOCK, IncludeTeamZero));
		EXPECT_FALSE(CQmGoresDrownTracker::IsBoardVisible(true, false, 1, IncludeTeamZero));
		EXPECT_FALSE(CQmGoresDrownTracker::IsBoardVisible(true, true, -1, IncludeTeamZero));
		EXPECT_FALSE(CQmGoresDrownTracker::IsBoardVisible(true, true, NUM_DDRACE_TEAMS, IncludeTeamZero));
		EXPECT_TRUE(CQmGoresDrownTracker::IsBoardVisible(true, true, 1, IncludeTeamZero));
		EXPECT_TRUE(CQmGoresDrownTracker::IsBoardVisible(true, true, NUM_DDRACE_TEAMS - 1, IncludeTeamZero));
	}
}

TEST(QmGoresDrownTracker, InvalidTeamsCannotCountOrMatchWithEitherTeamZeroPolicy)
{
	for(bool IncludeTeamZero : {false, true})
	{
		SCOPED_TRACE(IncludeTeamZero);
		for(int Team : {-1, static_cast<int>(NUM_DDRACE_TEAMS)})
		{
			SCOPED_TRACE(Team);
			CQmGoresDrownTracker Tracker;
			Observe(Tracker, 1, false, false, Team, IncludeTeamZero);
			Observe(Tracker, 1, true, false, Team, IncludeTeamZero);
			EXPECT_EQ(Tracker.Count(1), 0);
			EXPECT_FALSE(CQmGoresDrownTracker::IsSameTrackedTeam(Team, Team, IncludeTeamZero));
		}
	}
}

TEST(QmGoresDrownTracker, MovingBetweenTeamZeroAndNonZeroStartsNewBaseline)
{
	CQmGoresDrownTracker Tracker;
	Observe(Tracker, 1, false, false, TEAM_FLOCK);
	Observe(Tracker, 1, true, false, TEAM_FLOCK);
	ASSERT_EQ(Tracker.Count(1), 1);
	Observe(Tracker, 1, true);
	EXPECT_EQ(Tracker.Count(1), 0);
	Observe(Tracker, 1, false);
	Observe(Tracker, 1, false, true);
	Observe(Tracker, 1, true);
	ASSERT_EQ(Tracker.Count(1), 1);
	Observe(Tracker, 1, true, false, TEAM_FLOCK);
	EXPECT_EQ(Tracker.Count(1), 0);
	Observe(Tracker, 1, false, false, TEAM_FLOCK);
	Observe(Tracker, 1, false, true, TEAM_FLOCK);
	Observe(Tracker, 1, true, false, TEAM_FLOCK);
	EXPECT_EQ(Tracker.Count(1), 1);
}
