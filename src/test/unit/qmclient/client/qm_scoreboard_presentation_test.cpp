#include <game/client/components/qmclient/scoreboard_layout.h>

#include <gtest/gtest.h>

#include <limits>

TEST(QmScoreboardPresentation, LegacyRaceScoreCanFormatMostNegativeScore)
{
	char aTime[32];
	ScoreboardFormatLegacyTimeScore(std::numeric_limits<int>::min(), aTime, sizeof(aTime));
	EXPECT_STREQ("596523:14:08", aTime);
}

TEST(QmScoreboardPresentation, LegacyRaceScoreKeepsUnfinishedTimeBlank)
{
	char aTime[32] = "00:12";
	ScoreboardFormatLegacyTimeScore(FinishTime::NOT_FINISHED_TIMESCORE, aTime, sizeof(aTime));
	EXPECT_STREQ("", aTime);
}

TEST(QmScoreboardPresentation, SoundInfoStaysHiddenWhenWindowIsTooNarrow)
{
	const CUIRect Screen{0.0f, 0.0f, 133.33334f, 400.0f};
	const CUIRect Anchor{5.0f, 270.0f, 28.0f, 28.0f};
	const CUIRect Info = ScoreboardSoundInfoRect(Screen, Anchor, 170.0f, 35.0f, 0.0f);
	EXPECT_FLOAT_EQ(0.0f, Info.w);
	EXPECT_FLOAT_EQ(0.0f, Info.h);
}

TEST(QmScoreboardPresentation, SoundInfoOpensLeftWhenRightSideHasNoRoom)
{
	const CUIRect Screen{0.0f, 0.0f, 400.0f, 300.0f};
	const CUIRect Anchor{340.0f, 270.0f, 28.0f, 28.0f};
	const CUIRect Info = ScoreboardSoundInfoRect(Screen, Anchor, 170.0f, 35.0f, 0.0f);
	EXPECT_FLOAT_EQ(162.0f, Info.x);
	EXPECT_FLOAT_EQ(260.0f, Info.y);
	EXPECT_FLOAT_EQ(170.0f, Info.w);
	EXPECT_FLOAT_EQ(35.0f, Info.h);
}

TEST(QmScoreboardPresentation, HeaderButtonsKeepEqualHeightsAndUnshrunkLabelSpace)
{
	for(float TextWidth : {20.0f, 40.3f, 60.7f, 130.0f})
	{
		SCOPED_TRACE(TextWidth);
		const CUIRect Slot{0.0f, 20.0f, QmScoreboardHeaderButtonWidth(TextWidth), 40.0f};
		const auto Button = QmScoreboardHeaderButtonRect(Slot);
		const auto Label = ui_token::dropdown::EntryLabelRect(Button, 0.0f, QmScoreboardHeaderFontSize);
		EXPECT_GE(Label.w, TextWidth);
		EXPECT_FLOAT_EQ(Button.h, QmScoreboardHeaderButtonHeight);
		EXPECT_FLOAT_EQ(Button.y + Button.h * 0.5f, Slot.y + Slot.h * 0.5f);
	}
}

TEST(QmScoreboardPresentation, AdjacentTeamsOnlyRoundOuterEnds)
{
	const CUIRect Band{0.0f, 0.0f, 200.0f, 60.0f};
	const auto First = ResolveRoundedSurfacePlan(Band, QmScoreboardTeamBandSurface(TEAM_FLOCK, 50, 5.0f), true);
	const auto Middle = ResolveRoundedSurfacePlan(Band, QmScoreboardTeamBandSurface(1, 100, 5.0f), true);
	const auto Last = ResolveRoundedSurfacePlan(Band, QmScoreboardTeamBandSurface(50, TEAM_FLOCK, 5.0f), true);
	EXPECT_EQ(First.m_CornerRadii, vec4(5.0f, 5.0f, 0.0f, 0.0f));
	EXPECT_EQ(Middle.m_CornerRadii, vec4(0.0f, 0.0f, 0.0f, 0.0f));
	EXPECT_EQ(Last.m_CornerRadii, vec4(0.0f, 0.0f, 5.0f, 5.0f));
}

TEST(QmScoreboardPresentation, BottomPaddingIsReservedOnlyWhenColumnEndsWithATeam)
{
	const float Padding = QmScoreboardTeamBottomPadding(100);
	EXPECT_GT(Padding, 0.0f);
	EXPECT_FLOAT_EQ(QmScoreboardTeamBottomPadding(TEAM_FLOCK), 0.0f);
	const auto Metrics = QmScoreboardRowMetrics(400.0f - Padding, 32, 3, 1, false);
	const float Height = ScoreboardRowsHeight(32, 3, 1, Metrics.m_LineHeight, Metrics.m_Spacing,
		Metrics.m_FontSize / 1.5f, minimum(SCOREBOARD_TEAM_MODE_ICON_SIZE, Metrics.m_FontSize));
	EXPECT_LE(Height + Padding, 400.001f);
}
