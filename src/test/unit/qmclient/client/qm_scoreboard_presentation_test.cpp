#include <game/client/components/scoreboard.h>

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
