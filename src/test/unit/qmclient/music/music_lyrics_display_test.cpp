#include <game/client/components/qmclient/music_lyrics/music_lyrics_display.h>

#include <gtest/gtest.h>

using namespace QmMusicLyrics;

TEST(QmLyricsDisplay, OriginalModeKeepsOriginal)
{
	const auto Rows = DisplayRows("original", "译文", 0);
	ASSERT_EQ(Rows.m_Count, 1);
	EXPECT_EQ(Rows.m_aRows[0], "original");
}

TEST(QmLyricsDisplay, TranslationModeUsesCorrespondingTranslation)
{
	const auto Rows = DisplayRows("original", "译文", 1);
	ASSERT_EQ(Rows.m_Count, 1);
	EXPECT_EQ(Rows.m_aRows[0], "译文");
}

TEST(QmLyricsDisplay, MissingTranslationFallsBackToOriginalInBothModes)
{
	for(int Mode : {1, 2})
	{
		SCOPED_TRACE(Mode);
		const auto Rows = DisplayRows("original", "", Mode);
		ASSERT_EQ(Rows.m_Count, 1);
		EXPECT_EQ(Rows.m_aRows[0], "original");
	}
}

TEST(QmLyricsDisplay, BilingualModePlacesTranslationBelowOriginal)
{
	const auto Rows = DisplayRows("original", "译文", 2);
	ASSERT_EQ(Rows.m_Count, 2);
	EXPECT_EQ(Rows.m_aRows[0], "original");
	EXPECT_EQ(Rows.m_aRows[1], "译文");
}

TEST(QmLyricsDisplay, IdenticalTranslationDoesNotDuplicateTheLine)
{
	EXPECT_EQ(DisplayRows("same", "same", 2).m_Count, 1);
}

TEST(QmLyricsDisplay, MissingOriginalDoesNotDisplayAnUnpairedTranslation)
{
	EXPECT_EQ(DisplayRows("", "old translation", 2).m_Count, 0);
}

TEST(QmLyricsDisplay, MissingTranslationIndexReturnsEmpty)
{
	SLyricsData Data;
	Data.m_vTranslations = {"first"};
	EXPECT_TRUE(TranslationAt(Data, -1).empty());
	EXPECT_EQ(TranslationAt(Data, 0), "first");
	EXPECT_TRUE(TranslationAt(Data, 1).empty());
}

TEST(QmLyricsDisplay, IndependentTranslationTrackAlignsByTimestamp)
{
	SLyricsData Data;
	ASSERT_TRUE(NeteaseLyrics::ParseLrc("[00:01.00]one\n[00:03.00]two\n[00:05.00]three", &Data.m_Timeline));
	AlignTranslations(Data.m_Timeline, "[00:03.12]二\n[00:01.00]一\n[00:07.00]不匹配", &Data.m_vTranslations);
	ASSERT_EQ(Data.m_vTranslations.size(), 3u);
	EXPECT_EQ(Data.m_vTranslations[0], "一");
	EXPECT_EQ(Data.m_vTranslations[1], "二");
	EXPECT_TRUE(Data.m_vTranslations[2].empty());
}

TEST(QmLyricsDisplay, InvalidTranslationTrackClearsPreviousTranslations)
{
	SLyricsData Data;
	Data.m_vTranslations = {"old"};
	AlignTranslations(Data.m_Timeline, "invalid", &Data.m_vTranslations);
	EXPECT_TRUE(Data.m_vTranslations.empty());
}

TEST(QmLyricsDisplay, CreditsDoNotAppearInAnyLanguageMode)
{
	for(const char *pText : {"作曲：Tomas", "作词: 张三", "【編曲】 李四", "Produced by Someone", "混音 / Alice"})
	{
		SCOPED_TRACE(pText);
		for(int Language = 0; Language < 3; ++Language)
			EXPECT_EQ(DisplayRows(pText, "translated credits", Language).m_Count, 0);
	}
}

TEST(QmLyricsDisplay, NormalLyricsWithColonsOrCreditWordsRemainVisible)
{
	for(const char *pText : {"我想为你作词", "录音机里的旧旋律", "Music brings us together", "角色：歌词"})
	{
		SCOPED_TRACE(pText);
		EXPECT_EQ(DisplayRows(pText, "", 0).m_aRows[0], pText);
	}
}

TEST(QmLyricsDisplay, TranslationCreditFallsBackToTheOriginal)
{
	EXPECT_EQ(DisplayRows("normal lyric", "作曲：某某", 1).m_aRows[0], "normal lyric");
	EXPECT_EQ(DisplayRows("normal lyric", "作曲：某某", 2).m_Count, 1);
}

TEST(QmLyricsDisplay, CreditClassifierRespectsStringViewBounds)
{
	const std::string Text = "作曲家在写歌";
	EXPECT_TRUE(IsLyricCredit(std::string_view(Text.data(), std::string_view("作曲").size())));
	EXPECT_FALSE(IsLyricCredit(Text));
	EXPECT_FALSE(IsLyricCredit({}));
}

TEST(QmLyricProgress, TimestampProgressClampsBeforeAndAfterTheLine)
{
	SSourceSnapshot Source{42, 1, 100, true, nullptr, "lyric", "", 1000, 5000, 3000};
	EXPECT_FLOAT_EQ(LyricProgress(Source), 0.5f);
	Source.m_PositionMs = 500;
	EXPECT_FLOAT_EQ(LyricProgress(Source), 0.0f);
	Source.m_PositionMs = 6000;
	EXPECT_FLOAT_EQ(LyricProgress(Source), 1.0f);
}

TEST(QmLyricProgress, PauseAndSeekFollowTheReportedPlaybackPosition)
{
	SSourceSnapshot Source{42, 1, 100, true, nullptr, "lyric", "", 1000, 5000, 4000};
	const float Paused = LyricProgress(Source);
	EXPECT_FLOAT_EQ(LyricProgress(Source), Paused);
	Source.m_PositionMs = 2000;
	EXPECT_FLOAT_EQ(LyricProgress(Source), 0.25f);
	EXPECT_LT(LyricProgress(Source), Paused);
}

TEST(QmLyricProgress, NextTimestampSuppliesAMissingEnd)
{
	SLyricsData Data;
	ASSERT_TRUE(NeteaseLyrics::ParseLrc("[00:01]first\n[00:05]second", &Data.m_Timeline));
	SSourceSnapshot Source{42, 1, 100, true, &Data, "first", "", 1000, -1, 3000};
	EXPECT_FLOAT_EQ(LyricProgress(Source), 0.5f);
	Source.m_LineStartMs = 5000;
	EXPECT_FLOAT_EQ(LyricProgress(Source), -1.0f);
}

TEST(QmLyricProgress, UnknownPositionOrMissingTimingDoesNotInventProgress)
{
	SSourceSnapshot Source{42, 1, 100, true, nullptr, "lyric", "", 1000, 5000, -1};
	EXPECT_FLOAT_EQ(LyricProgress(Source), -1.0f);
	Source.m_PositionMs = 3000;
	Source.m_LineEndMs = -1;
	EXPECT_FLOAT_EQ(LyricProgress(Source), -1.0f);
	Source.m_LineEndMs = 5000;
	Source.m_Active = false;
	EXPECT_FLOAT_EQ(LyricProgress(Source), -1.0f);
}
