#include <game/client/components/qmclient/voting_hud.h>

#include <gtest/gtest.h>

#include <iterator>

static_assert(ui_token::vote_hud::RADIUS == ui_token::radius::TIGHT,
	"vote panels use tight corners in both compact and full layouts");

TEST(QmVotingHud, ScoreboardOwnsVotePresentationWhileOpen)
{
	EXPECT_EQ(QmVoteHudMode(true, 0, false, false, false, false), EVoteHudMode::FULL);
	EXPECT_EQ(QmVoteHudMode(true, 0, true, false, false, false), EVoteHudMode::NONE);
	EXPECT_EQ(QmVoteHudMode(true, 1, true, true, false, true), EVoteHudMode::NONE);
	EXPECT_EQ(QmVoteHudMode(false, 0, false, false, false, false), EVoteHudMode::NONE);
}

TEST(QmVotingHud, CompactHudIsVisibleUntilVoteCast)
{
	EXPECT_EQ(QmVoteHudMode(true, 0, false, false, false, true), EVoteHudMode::MINI);
	for(const int Choice : {-1, 1})
	{
		SCOPED_TRACE(Choice);
		EXPECT_EQ(QmVoteHudMode(true, Choice, false, false, false, true), EVoteHudMode::NONE);
		EXPECT_EQ(QmVoteHudMode(true, Choice, false, true, false, true), EVoteHudMode::MINI);
		EXPECT_EQ(QmVoteHudMode(true, Choice, true, false, false, true), EVoteHudMode::NONE);
	}
}

TEST(QmVotingHud, FullHudDisappearsAfterVoteUnlessKept)
{
	for(const int Choice : {-1, 1})
	{
		SCOPED_TRACE(Choice);
		EXPECT_EQ(QmVoteHudMode(true, Choice, false, false, false, false), EVoteHudMode::NONE);
		EXPECT_EQ(QmVoteHudMode(true, Choice, false, true, false, false), EVoteHudMode::FULL);
	}
}

TEST(QmVotingHud, EditorPreviewRemainsAvailableWithoutActiveVote)
{
	EXPECT_EQ(QmVoteHudMode(false, 0, false, false, true, false), EVoteHudMode::FULL);
	EXPECT_EQ(QmVoteHudMode(true, 1, true, false, true, true), EVoteHudMode::MINI);
}

TEST(QmVotingHud, FullHudMatchesScoreboardPhysicalWidth)
{
	constexpr float GraphicsWidth = 800.0f;
	constexpr float UiWidth = 1600.0f;
	const float CoordScale = QmVoteHudCoordScale(GraphicsWidth, UiWidth);
	EXPECT_FLOAT_EQ(CoordScale, 0.5f);

	const CUIRect Full = QmVoteHudDefaultRect(EVoteHudMode::FULL, CoordScale);
	const CUIRect Scoreboard = QmVoteHudDefaultRect(EVoteHudMode::FULL, 1.0f);
	EXPECT_FLOAT_EQ(Full.w / GraphicsWidth, Scoreboard.w / UiWidth);
	EXPECT_FLOAT_EQ(Full.h / GraphicsWidth, Scoreboard.h / UiWidth);
}

TEST(QmVotingHud, MiniHudKeepsLegacyCompactRect)
{
	const CUIRect Mini = QmVoteHudDefaultRect(EVoteHudMode::MINI, 0.5f);
	EXPECT_FLOAT_EQ(Mini.w, ui_token::vote_hud::MINI_PANEL_WIDTH);
	EXPECT_FLOAT_EQ(Mini.h, ui_token::vote_hud::MINI_PANEL_HEIGHT);
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

TEST(QmVotingHud, ScoreboardPanelFitsAcrossAvailableSizes)
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
		const SQmVoteHudLayout Layout = QmScoreboardVoteLayout(Scenario.m_Screen, Scenario.m_Scoreboard);
		ASSERT_GT(Layout.m_Scale, 0.0f);
		EXPECT_GE(Layout.m_Panel.x, Scenario.m_Screen.x);
		EXPECT_GE(Layout.m_Panel.y, Scenario.m_Screen.y);
		EXPECT_LE(Layout.m_Panel.x + Layout.m_Panel.w, Scenario.m_Screen.x + Scenario.m_Screen.w);
		EXPECT_LE(Layout.m_Panel.y + Layout.m_Panel.h, Scenario.m_Scoreboard.y);
		EXPECT_LE(Layout.m_Panel.y + Layout.m_Panel.h, Scenario.m_Screen.y + Scenario.m_Screen.h);
		for(const CUIRect &Child : {Layout.m_Header, Layout.m_Description, Layout.m_Time,
			    Layout.m_Reason, Layout.m_Bars, Layout.m_Yes, Layout.m_No})
		{
			EXPECT_GT(Child.w, 0.0f);
			EXPECT_GT(Child.h, 0.0f);
			EXPECT_GE(Child.x, Layout.m_Panel.x);
			EXPECT_GE(Child.y, Layout.m_Panel.y);
			EXPECT_LE(Child.x + Child.w, Layout.m_Panel.x + Layout.m_Panel.w);
			EXPECT_LE(Child.y + Child.h, Layout.m_Panel.y + Layout.m_Panel.h);
		}
		EXPECT_FLOAT_EQ(Layout.m_Description.y, Layout.m_Time.y);
		EXPECT_FLOAT_EQ(Layout.m_Description.h, Layout.m_Time.h);
		EXPECT_LT(Layout.m_Yes.x + Layout.m_Yes.w, Layout.m_No.x);
		EXPECT_LT(Layout.m_Bars.y + Layout.m_Bars.h, Layout.m_Yes.y);
	}
}

TEST(QmVotingHud, WidthLimitedPanelKeepsBothMarginsAfterScaling)
{
	const CUIRect Screen{0.0f, 0.0f, 133.33334f, 400.0f};
	const CUIRect Scoreboard{-33.33333f, 75.0f, 200.0f, 220.0f};
	const SQmVoteHudLayout Layout = QmScoreboardVoteLayout(Screen, Scoreboard);
	ASSERT_GT(Layout.m_Panel.w, 0.0f);
	EXPECT_GE(Layout.m_Panel.x, 6.0f);
	EXPECT_LE(Layout.m_Panel.w, Screen.w - 12.0f);
	EXPECT_LE(Layout.m_Panel.x + Layout.m_Panel.w, Screen.w - 6.0f);
}

TEST(QmVotingHud, NoTopSpaceProducesNoOverlappingPanel)
{
	const CUIRect Screen = {0.0f, 0.0f, 800.0f, 600.0f};
	for(const CUIRect &Scoreboard : {CUIRect{5.0f, 0.0f, 790.0f, 385.0f}, CUIRect{5.0f, 12.0f, 790.0f, 385.0f}, CUIRect{5.0f, 75.0f, 0.0f, 385.0f}})
	{
		const SQmVoteHudLayout Layout = QmScoreboardVoteLayout(Screen, Scoreboard);
		EXPECT_FLOAT_EQ(Layout.m_Scale, 0.0f);
		EXPECT_FLOAT_EQ(Layout.m_Panel.w, 0.0f);
		EXPECT_FLOAT_EQ(Layout.m_Panel.h, 0.0f);
	}
}

TEST(QmVotingHud, MiniHudFitsLegacyRowsWithoutOverflow)
{
	const SQmMiniVoteHudLayout Layout = QmMiniVoteHudLayoutFromRect(QmVoteHudDefaultRect(EVoteHudMode::MINI));
	ASSERT_GT(Layout.m_Panel.w, 0.0f);
	ASSERT_GT(Layout.m_Panel.h, 0.0f);
	for(const CUIRect &Child : {Layout.m_Description, Layout.m_Reason, Layout.m_Time,
		    Layout.m_Spinner, Layout.m_Bars, Layout.m_Yes, Layout.m_No})
	{
		EXPECT_GT(Child.w, 0.0f);
		EXPECT_GT(Child.h, 0.0f);
		EXPECT_GE(Child.x, Layout.m_Panel.x);
		EXPECT_GE(Child.y, Layout.m_Panel.y);
		EXPECT_LE(Child.x + Child.w, Layout.m_Panel.x + Layout.m_Panel.w);
		EXPECT_LE(Child.y + Child.h, Layout.m_Panel.y + Layout.m_Panel.h);
	}
	EXPECT_GT(Layout.m_Yes.y, Layout.m_Bars.y + Layout.m_Bars.h);
}

TEST(QmVotingHud, ScoreboardAlwaysUsesFullVotePanel)
{
	const CUIRect Screen = {0.0f, 0.0f, 1066.0f, 600.0f};
	const CUIRect Scoreboard = {108.0f, 75.0f, 850.0f, 385.0f};
	const SQmVoteHudLayout Layout = QmScoreboardVoteLayout(Screen, Scoreboard);
	ASSERT_FLOAT_EQ(Layout.m_Scale, 1.0f);
	EXPECT_FLOAT_EQ(Layout.m_Panel.w, ui_token::vote_hud::PANEL_WIDTH);
	EXPECT_FLOAT_EQ(Layout.m_Panel.h, ui_token::vote_hud::PANEL_HEIGHT);
}

TEST(QmVotingHud, RowsFillTheContentAreaWithoutOverflow)
{
	const SQmVoteHudLayout Layout = QmVoteHudLayoutFromRect(QmVoteHudDefaultRect(EVoteHudMode::FULL), 1.0f);
	ASSERT_GT(Layout.m_Scale, 0.0f);

	const float ContentBottom = Layout.m_Panel.y + Layout.m_Panel.h - ui_token::vote_hud::PADDING;
	EXPECT_FLOAT_EQ(Layout.m_Yes.y + Layout.m_Yes.h, ContentBottom);
	EXPECT_FLOAT_EQ(Layout.m_No.y + Layout.m_No.h, ContentBottom);
	EXPECT_FLOAT_EQ(Layout.m_Yes.w, Layout.m_No.w);

	// 每行必须容得下自己的字号，否则文字会被上下裁切。
	EXPECT_GE(Layout.m_Header.h, ui_token::vote_hud::TITLE_FONT_SIZE);
	EXPECT_GE(Layout.m_Reason.h, ui_token::vote_hud::REASON_FONT_SIZE);
	EXPECT_GE(Layout.m_Yes.h, ui_token::vote_hud::TITLE_FONT_SIZE);
}
