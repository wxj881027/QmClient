#include <game/client/components/qmclient/gores_drown_tracker.h>

#include <gtest/gtest.h>

namespace
{
	void Observe(CQmGoresDrownTracker &Tracker, int ClientId, bool Frozen, bool Hooking = false, int Team = 1)
	{
		Tracker.Observe(ClientId, true, "player", "clan", Team, true, Frozen, Hooking);
	}
}

TEST(QmGoresDrownTracker, TeamZeroDoesNotCountOrAppearInBoard)
{
	CQmGoresDrownTracker Tracker;
	Observe(Tracker, 1, false, false, TEAM_FLOCK);
	Observe(Tracker, 1, true, false, TEAM_FLOCK);
	EXPECT_EQ(Tracker.Count(1), 0);
	EXPECT_FALSE(CQmGoresDrownTracker::IsSameTrackedTeam(TEAM_FLOCK, TEAM_FLOCK));
	EXPECT_FALSE(CQmGoresDrownTracker::IsSameTrackedTeam(-1, -1));
}

TEST(QmGoresDrownTracker, BoardIncludesOnlyMatchingDdraceTeam)
{
	EXPECT_TRUE(CQmGoresDrownTracker::IsSameTrackedTeam(3, 3));
	EXPECT_FALSE(CQmGoresDrownTracker::IsSameTrackedTeam(3, 4));
	EXPECT_FALSE(CQmGoresDrownTracker::IsSameTrackedTeam(3, TEAM_FLOCK));
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
	Tracker.Observe(1, false, "player", "clan", 1, false, false, false);
	Observe(Tracker, 1, true);
	EXPECT_EQ(Tracker.Count(1), 0);
}

TEST(QmGoresDrownTracker, IdentityChangeCannotInheritPreviousCount)
{
	CQmGoresDrownTracker Tracker;
	Observe(Tracker, 1, false);
	Observe(Tracker, 1, true);
	Tracker.Observe(1, true, "replacement", "clan", 1, true, true, false);
	EXPECT_EQ(Tracker.Count(1), 0);
}

TEST(QmGoresDrownTracker, MissingCharacterKeepsCountAndRebuildsFreezeBaseline)
{
	CQmGoresDrownTracker Tracker;
	Observe(Tracker, 1, false);
	Observe(Tracker, 1, true);
	Tracker.Observe(1, true, "player", "clan", 1, false, false, false);
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

TEST(QmGoresDrownTracker, BoardExplainsUnsupportedContextAndRecoversOnTeamJoin)
{
	EXPECT_NE(CQmGoresDrownTracker::UnavailableReason(false, true, 1), nullptr);
	EXPECT_NE(CQmGoresDrownTracker::UnavailableReason(true, false, -1), nullptr);
	EXPECT_NE(CQmGoresDrownTracker::UnavailableReason(true, true, TEAM_FLOCK), nullptr);
	EXPECT_NE(CQmGoresDrownTracker::UnavailableReason(true, true, NUM_DDRACE_TEAMS), nullptr);
	EXPECT_EQ(CQmGoresDrownTracker::UnavailableReason(true, true, 1), nullptr);
	EXPECT_EQ(CQmGoresDrownTracker::UnavailableReason(true, true, NUM_DDRACE_TEAMS - 1), nullptr);
	EXPECT_NE(CQmGoresDrownTracker::UnavailableReason(true, true, TEAM_FLOCK), nullptr);
}

TEST(QmGoresDrownTracker, ManualGoresModeEnablesBoardWhenServerGameTypeIsGeneric)
{
	EXPECT_TRUE(CQmGoresDrownTracker::ModeEnabled(true, false));
	EXPECT_TRUE(CQmGoresDrownTracker::ModeEnabled(false, true));
	EXPECT_FALSE(CQmGoresDrownTracker::ModeEnabled(false, false));
	EXPECT_EQ(CQmGoresDrownTracker::UnavailableReason(CQmGoresDrownTracker::ModeEnabled(true, false), true, 1), nullptr);
}
