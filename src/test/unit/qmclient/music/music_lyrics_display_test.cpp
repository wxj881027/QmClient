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
