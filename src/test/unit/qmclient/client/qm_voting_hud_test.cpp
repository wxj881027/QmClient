#include <game/client/components/qmclient/voting_hud.h>

#include <gtest/gtest.h>

#include <iterator>

TEST(QmVotingHud, ScoreboardOwnsVotePresentationWhileOpen)
{
	EXPECT_TRUE(QmVoteHudVisible(true, 0, false, false, false));
	EXPECT_FALSE(QmVoteHudVisible(true, 0, true, false, false));
	EXPECT_FALSE(QmVoteHudVisible(true, 1, true, true, false));
	EXPECT_FALSE(QmVoteHudVisible(false, 0, false, false, false));
}

TEST(QmVotingHud, VotingHidesNormalHudUnlessPlayerChoseToKeepIt)
{
	for(const int Choice : {-1, 1})
	{
		SCOPED_TRACE(Choice);
		EXPECT_FALSE(QmVoteHudVisible(true, Choice, false, false, false));
		EXPECT_TRUE(QmVoteHudVisible(true, Choice, false, true, false));
	}
}

TEST(QmVotingHud, EditorPreviewRemainsAvailableWithoutActiveVote)
{
	EXPECT_TRUE(QmVoteHudVisible(false, 0, false, false, true));
	EXPECT_TRUE(QmVoteHudVisible(true, 1, true, false, true));
}

TEST(QmVotingHud, InteractiveScoreboardAllowsChangingChoice)
{
	EXPECT_TRUE(QmScoreboardVoteCanSubmit(true, true, 0, 1));
	EXPECT_TRUE(QmScoreboardVoteCanSubmit(true, true, 0, -1));
	EXPECT_TRUE(QmScoreboardVoteCanSubmit(true, true, 1, -1));
	EXPECT_TRUE(QmScoreboardVoteCanSubmit(true, true, -1, 1));
	EXPECT_FALSE(QmScoreboardVoteCanSubmit(true, true, 1, 1));
	EXPECT_FALSE(QmScoreboardVoteCanSubmit(true, true, -1, -1));
}

TEST(QmVotingHud, InactiveOrLockedScoreboardCannotSendVote)
{
	EXPECT_FALSE(QmScoreboardVoteCanSubmit(false, true, 0, 1));
	EXPECT_FALSE(QmScoreboardVoteCanSubmit(true, false, 0, -1));
	EXPECT_FALSE(QmScoreboardVoteCanSubmit(true, true, 0, 0));
	EXPECT_FALSE(QmScoreboardVoteCanSubmit(true, true, 0, 2));
}

TEST(QmVotingHud, BothLayoutsFitAboveScoreboardAcrossAvailableSizes)
{
	struct SScenario
	{
		CUIRect m_Screen;
		CUIRect m_Scoreboard;
	};
	const SScenario aScenarios[] = {
		{{0.0f, 0.0f, 1066.0f, 600.0f}, {108.0f, 75.0f, 850.0f, 385.0f}},
		{{0.0f, 0.0f, 800.0f, 600.0f}, {5.0f, 75.0f, 790.0f, 385.0f}},
		{{0.0f, 0.0f, 420.0f, 300.0f}, {6.0f, 45.0f, 408.0f, 220.0f}},
		{{20.0f, 30.0f, 400.0f, 300.0f}, {340.0f, 78.0f, 120.0f, 220.0f}},
	};
	for(size_t Index = 0; Index < std::size(aScenarios); ++Index)
	{
		SCOPED_TRACE(Index);
		const SScenario &Scenario = aScenarios[Index];
		for(const bool Mini : {false, true})
		{
			SCOPED_TRACE(Mini);
			const SQmScoreboardVoteLayout Layout = QmScoreboardVoteLayout(Scenario.m_Screen, Scenario.m_Scoreboard, Mini);
			ASSERT_GT(Layout.m_Scale, 0.0f);
			EXPECT_GE(Layout.m_Panel.x, Scenario.m_Screen.x);
			EXPECT_GE(Layout.m_Panel.y, Scenario.m_Screen.y);
			EXPECT_LE(Layout.m_Panel.x + Layout.m_Panel.w, Scenario.m_Screen.x + Scenario.m_Screen.w);
			EXPECT_LE(Layout.m_Panel.y + Layout.m_Panel.h, Scenario.m_Scoreboard.y);
			EXPECT_LE(Layout.m_Panel.y + Layout.m_Panel.h, Scenario.m_Screen.y + Scenario.m_Screen.h);
			for(const CUIRect &Child : {Layout.m_Header, Layout.m_Reason, Layout.m_Bars, Layout.m_Yes, Layout.m_No})
			{
				EXPECT_GT(Child.w, 0.0f);
				EXPECT_GT(Child.h, 0.0f);
				EXPECT_GE(Child.x, Layout.m_Panel.x);
				EXPECT_GE(Child.y, Layout.m_Panel.y);
				EXPECT_LE(Child.x + Child.w, Layout.m_Panel.x + Layout.m_Panel.w);
				EXPECT_LE(Child.y + Child.h, Layout.m_Panel.y + Layout.m_Panel.h);
			}
			EXPECT_LT(Layout.m_Yes.x + Layout.m_Yes.w, Layout.m_No.x);
			EXPECT_LT(Layout.m_Bars.y + Layout.m_Bars.h, Layout.m_Yes.y);
		}
	}
}

TEST(QmVotingHud, NoTopSpaceProducesNoOverlappingPanel)
{
	const CUIRect Screen = {0.0f, 0.0f, 800.0f, 600.0f};
	for(const CUIRect &Scoreboard : {CUIRect{5.0f, 0.0f, 790.0f, 385.0f}, CUIRect{5.0f, 12.0f, 790.0f, 385.0f}, CUIRect{5.0f, 75.0f, 0.0f, 385.0f}})
	{
		const SQmScoreboardVoteLayout Layout = QmScoreboardVoteLayout(Screen, Scoreboard, false);
		EXPECT_FLOAT_EQ(Layout.m_Scale, 0.0f);
		EXPECT_FLOAT_EQ(Layout.m_Panel.w, 0.0f);
		EXPECT_FLOAT_EQ(Layout.m_Panel.h, 0.0f);
	}
}
