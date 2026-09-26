#include <generated/protocol.h>

#include <game/teamscore.h>

#include <gtest/gtest.h>

TEST(GameInfoEx, OldLaserFlagAppendsWithoutShiftingOtherFlags)
{
	// 官方 79184e826：OLD_LASER 追加在 GameInfoFlags2 末尾，旧客户端忽略未知位。
	// 这里锁住位序，避免以后插入新标志时把已有标志的位挤走。
	EXPECT_EQ(GAMEINFOFLAG2_ALLOW_X_SKINS, 1 << 0);
	EXPECT_EQ(GAMEINFOFLAG2_HUD_DDRACE, 1 << 6);
	EXPECT_EQ(GAMEINFOFLAG2_NO_WEAK_HOOK, 1 << 7);
	EXPECT_EQ(GAMEINFOFLAG2_DDRACE_TEAM, 1 << 9);
	EXPECT_EQ(GAMEINFOFLAG2_PREDICT_EVENTS, 1 << 10);
	EXPECT_EQ(GAMEINFOFLAG2_OLD_LASER, 1 << 11);
}

TEST(GameInfoEx, NormalizesTeamSchema)
{
	EXPECT_EQ(QmNormalizeNumDDRaceTeams(VANILLA_MAX_CLIENTS + 1), VANILLA_MAX_CLIENTS + 1);
	EXPECT_EQ(QmNormalizeNumDDRaceTeams(LEGACY_MAX_CLIENTS + 1), LEGACY_MAX_CLIENTS + 1);
	EXPECT_EQ(QmNormalizeNumDDRaceTeams(NUM_DDRACE_TEAMS), NUM_DDRACE_TEAMS);
	EXPECT_EQ(QmNormalizeNumDDRaceTeams(0), LEGACY_MAX_CLIENTS + 1);
	EXPECT_EQ(QmNormalizeNumDDRaceTeams(VANILLA_MAX_CLIENTS), LEGACY_MAX_CLIENTS + 1);
	EXPECT_EQ(QmNormalizeNumDDRaceTeams(NUM_DDRACE_TEAMS + 1), LEGACY_MAX_CLIENTS + 1);
}

TEST(GameInfoEx, TeamSuperFollowsAdvertisedSchema)
{
	CTeamsCore Teams;
	EXPECT_EQ(Teams.TeamSuper(), NUM_DDRACE_TEAMS - 1);

	Teams.m_NumDDRaceTeams = VANILLA_MAX_CLIENTS + 1;
	EXPECT_EQ(Teams.TeamSuper(), VANILLA_TEAM_SUPER);

	Teams.m_NumDDRaceTeams = LEGACY_MAX_CLIENTS + 1;
	EXPECT_EQ(Teams.TeamSuper(), LEGACY_TEAM_SUPER);

	Teams.m_NumDDRaceTeams = NUM_DDRACE_TEAMS;
	EXPECT_EQ(Teams.TeamSuper(), TEAM_SUPER);
}
