#include <game/client/components/qmclient/features/player_indicator/qm_player_indicator_logic.h>

#include <gtest/gtest.h>

TEST(QmPlayerIndicator, FiltersCandidates)
{
	SQmPlayerIndicatorCandidate Candidate;
	Candidate.m_OtherActive = true;
	Candidate.m_OtherTeam = 1;
	Candidate.m_LocalTeam = 1;
	Candidate.m_LocalRaceTeam = 1;
	EXPECT_TRUE(QmPlayerIndicatorShouldRender(Candidate));

	Candidate.m_OtherIsLocal = true;
	EXPECT_FALSE(QmPlayerIndicatorShouldRender(Candidate));
	Candidate.m_OtherIsLocal = false;
	Candidate.m_OtherTeam = 2;
	EXPECT_FALSE(QmPlayerIndicatorShouldRender(Candidate));

	Candidate.m_TeamOnly = false;
	Candidate.m_OtherTeam = 1;
	EXPECT_TRUE(QmPlayerIndicatorShouldRender(Candidate));
	Candidate.m_OtherTeam = 2;
	EXPECT_FALSE(QmPlayerIndicatorShouldRender(Candidate));

	Candidate.m_OtherTeam = 1;
	Candidate.m_TeamOnly = true;
	Candidate.m_LocalRaceTeam = 0;
	EXPECT_FALSE(QmPlayerIndicatorShouldRender(Candidate));
	Candidate.m_LocalRaceTeam = 1;
	Candidate.m_OtherTeam = 1;
	Candidate.m_FrozenOnly = true;
	EXPECT_FALSE(QmPlayerIndicatorShouldRender(Candidate));
	Candidate.m_OtherFrozen = true;
	EXPECT_TRUE(QmPlayerIndicatorShouldRender(Candidate));
}

TEST(QmPlayerIndicator, HidesVisibleCandidates)
{
	SQmPlayerIndicatorCandidate Candidate;
	Candidate.m_OtherActive = true;
	Candidate.m_LocalRaceTeam = 1;
	Candidate.m_HideVisible = true;
	Candidate.m_OtherVisible = true;
	EXPECT_FALSE(QmPlayerIndicatorShouldRender(Candidate));
	Candidate.m_OtherVisible = false;
	EXPECT_TRUE(QmPlayerIndicatorShouldRender(Candidate));
}

TEST(QmPlayerIndicator, DetectsUnfreezingPlayers)
{
	EXPECT_FALSE(QmPlayerIndicatorIsUnfreezing(false, false));
	EXPECT_FALSE(QmPlayerIndicatorIsUnfreezing(false, true));
	EXPECT_FALSE(QmPlayerIndicatorIsUnfreezing(true, true));
	EXPECT_TRUE(QmPlayerIndicatorIsUnfreezing(true, false));
}

TEST(QmPlayerIndicator, PreservesFreezeStateSemantics)
{
	EXPECT_FALSE(QmPlayerIndicatorIsFrozen(0, false));
	EXPECT_TRUE(QmPlayerIndicatorIsFrozen(1, false));
	EXPECT_FALSE(QmPlayerIndicatorIsFrozen(-1, false));
	EXPECT_TRUE(QmPlayerIndicatorIsFrozen(-1, true));
}

TEST(QmPlayerIndicator, CalculatesDistanceAndPosition)
{
	EXPECT_FLOAT_EQ(QmPlayerIndicatorOffset(42.0f, 100.0f, true, 1000, 500.0f), 71.0f);
	EXPECT_FLOAT_EQ(QmPlayerIndicatorOffset(42.0f, 100.0f, true, 1000, 1500.0f), 100.0f);
	EXPECT_EQ(QmPlayerIndicatorPosition(vec2(0.0f, 0.0f), vec2(0.0f, 0.0f), 42.0f), vec2(0.0f, 0.0f));
	EXPECT_EQ(QmPlayerIndicatorPosition(vec2(10.0f, 10.0f), vec2(0.0f, 10.0f), 42.0f), vec2(-32.0f, 10.0f));
}
