#include <game/client/components/qmclient/scoreboard_team_modes.h>

#include <gtest/gtest.h>

namespace
{
	using TModes = std::array<SQmScoreboardTeamModeState, NUM_DDRACE_TEAMS>;
	using TPresence = std::array<bool, NUM_DDRACE_TEAMS>;
}

TEST(QmScoreboardTeamModeLifecycle, DisbandThenTeamIdReuseDoesNotRestoreOldIcons)
{
	constexpr int Team = 3;
	TModes Modes{}, Cached{};
	TPresence Present{};
	Present[Team] = true;
	AccumulateQmScoreboardTeamModeState(Modes[Team], true, CHARACTERFLAG_PRACTICE_MODE | CHARACTERFLAG_LOCK_MODE);
	CacheAndRestoreQmScoreboardTeamModes(Modes, Present, Cached);
	ASSERT_TRUE(Cached[Team].Practice());
	Modes = {};
	Present = {};
	CacheAndRestoreQmScoreboardTeamModes(Modes, Present, Cached);
	EXPECT_FALSE(Cached[Team].m_Known);
	EXPECT_EQ(Cached[Team].m_Flags, 0);
	Present[Team] = true;
	CacheAndRestoreQmScoreboardTeamModes(Modes, Present, Cached);
	EXPECT_FALSE(Modes[Team].m_Known);
	EXPECT_FALSE(Modes[Team].Practice());
	EXPECT_FALSE(Modes[Team].Locked());
}

TEST(QmScoreboardTeamModeLifecycle, FilteredRowsLeaveOtherTeamCachesUntouched)
{
	TModes Modes{}, Cached{};
	TPresence Present{};
	Present[2] = Present[4] = true;
	AccumulateQmScoreboardTeamModeState(Modes[2], true, CHARACTERFLAG_PRACTICE_MODE);
	AccumulateQmScoreboardTeamModeState(Modes[4], true, CHARACTERFLAG_LOCK_MODE);
	CacheAndRestoreQmScoreboardTeamModes(Modes, Present, Cached);
	TModes RedRows{}, BlueRows{};
	TPresence RedPresent{}, BluePresent{};
	RedPresent[2] = true;
	BluePresent[4] = true;
	RestoreQmScoreboardTeamModes(RedRows, RedPresent, Cached);
	RestoreQmScoreboardTeamModes(BlueRows, BluePresent, Cached);
	EXPECT_TRUE(RedRows[2].Practice());
	EXPECT_FALSE(RedRows[4].m_Known);
	EXPECT_TRUE(BlueRows[4].Locked());
	EXPECT_FALSE(BlueRows[2].m_Known);
	EXPECT_TRUE(Cached[2].Practice());
	EXPECT_TRUE(Cached[4].Locked());
}

TEST(QmScoreboardTeamModeLifecycle, PartialKnownRowFlagsDoNotReplaceCompleteTeamAggregation)
{
	constexpr int Team = 3;
	TModes Modes{}, Cached{};
	TPresence Present{};
	Present[Team] = true;
	AccumulateQmScoreboardTeamModeState(Modes[Team], true, CHARACTERFLAG_PRACTICE_MODE | CHARACTERFLAG_LOCK_MODE);
	CacheAndRestoreQmScoreboardTeamModes(Modes, Present, Cached);
	TModes Filtered{};
	AccumulateQmScoreboardTeamModeState(Filtered[Team], true, CHARACTERFLAG_PRACTICE_MODE);
	RestoreQmScoreboardTeamModes(Filtered, Present, Cached);
	EXPECT_FALSE(Filtered[Team].Locked());
	EXPECT_TRUE(Cached[Team].Locked());
	Modes = {};
	CacheAndRestoreQmScoreboardTeamModes(Modes, Present, Cached);
	EXPECT_TRUE(Modes[Team].Practice());
	EXPECT_TRUE(Modes[Team].Locked());
}

TEST(QmScoreboardTeamModeLifecycle, ConfirmedModeClearOverridesCachedIconsWhenCharactersDisappear)
{
	constexpr int Team = 3;
	TModes Modes{}, Cached{};
	TPresence Present{};
	Present[Team] = true;
	AccumulateQmScoreboardTeamModeState(Modes[Team], true, CHARACTERFLAG_PRACTICE_MODE | CHARACTERFLAG_TEAM0_MODE | CHARACTERFLAG_LOCK_MODE);
	CacheAndRestoreQmScoreboardTeamModes(Modes, Present, Cached);
	Modes = {};
	AccumulateQmScoreboardTeamModeState(Modes[Team], true, 0);
	CacheAndRestoreQmScoreboardTeamModes(Modes, Present, Cached);
	Modes = {};
	CacheAndRestoreQmScoreboardTeamModes(Modes, Present, Cached);
	EXPECT_TRUE(Modes[Team].m_Known);
	EXPECT_EQ(Modes[Team].m_Flags, 0);
}

TEST(QmScoreboardTeamModeLifecycle, FilterHidingWholeTeamKeepsCacheUntilCompleteSnapshotConfirmsAbsence)
{
	constexpr int Team = NUM_DDRACE_TEAMS - 1;
	TModes Modes{}, Cached{};
	TPresence Present{};
	Present[Team] = true;
	AccumulateQmScoreboardTeamModeState(Modes[Team], true, CHARACTERFLAG_LOCK_MODE);
	CacheAndRestoreQmScoreboardTeamModes(Modes, Present, Cached);
	Modes = {};
	Present = {};
	RestoreQmScoreboardTeamModes(Modes, Present, Cached);
	EXPECT_FALSE(Modes[Team].m_Known);
	EXPECT_TRUE(Cached[Team].Locked());
	Present[Team] = true;
	RestoreQmScoreboardTeamModes(Modes, Present, Cached);
	EXPECT_TRUE(Modes[Team].Locked());
	Modes = {};
	Present = {};
	CacheAndRestoreQmScoreboardTeamModes(Modes, Present, Cached);
	EXPECT_FALSE(Cached[Team].m_Known);
}
