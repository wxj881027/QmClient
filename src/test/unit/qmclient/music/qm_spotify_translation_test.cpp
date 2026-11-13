#include <game/client/components/qmclient/music_lyrics/qm_spotify_parser.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

TEST(QmSpotifyTranslation, ReverseOrderMovesUtf8SyllablesAndTranslationsTogether)
{
	const char *pJson = R"({"lyrics":{"syncType":"SYLLABLE_SYNCED","lines":[
		{"startTimeMs":"3000","endTimeMs":"5000","words":"晚行"},
		{"startTimeMs":"1000","endTimeMs":"2500","words":"星光","syllables":[
			{"startTimeMs":"1000","endTimeMs":"1500","numChars":"1"},
			{"startTimeMs":"1500","endTimeMs":"2000","numChars":"1"}]}],
		"alternatives":[{"lines":["晚译","星译"]}]}})";
	QmMusicLyrics::SLyricsData Lyrics;
	ASSERT_TRUE(QmSpotify::ParseColorLyrics(pJson, &Lyrics));
	ASSERT_TRUE(Lyrics.HasLyrics());
	ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 2u);
	ASSERT_EQ(Lyrics.m_vTranslations.size(), 2u);
	const auto &Earlier = Lyrics.m_Timeline.m_vLines[0];
	EXPECT_EQ(Earlier.m_Text, "星光");
	EXPECT_EQ(Earlier.m_StartMs, 1000);
	EXPECT_EQ(Earlier.m_EndMs, 2500);
	EXPECT_EQ(Lyrics.m_vTranslations[0], "星译");
	ASSERT_EQ(Earlier.m_vWords.size(), 2u);
	EXPECT_EQ(Earlier.m_vWords[0].m_Text, "星");
	EXPECT_EQ(Earlier.m_vWords[0].m_StartMs, 1000);
	EXPECT_EQ(Earlier.m_vWords[0].m_EndMs, 1500);
	EXPECT_EQ(Earlier.m_vWords[1].m_Text, "光");
	EXPECT_EQ(Earlier.m_vWords[1].m_StartMs, 1500);
	EXPECT_EQ(Earlier.m_vWords[1].m_EndMs, 2000);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[1].m_Text, "晚行");
	EXPECT_EQ(Lyrics.m_vTranslations[1], "晚译");
}

TEST(QmSpotifyTranslation, EmptyAndInvalidMainRowsKeepSurvivingSourceTranslations)
{
	const char *pJson = R"({"lyrics":{"syncType":"LINE_SYNCED","lines":[
		{"startTimeMs":"1000","words":"first"},null,42,{},
		{"startTimeMs":"1500","words":""},{"startTimeMs":"2000","words":false},
		{"startTimeMs":"3000","words":"last"}],
		"alternatives":[{"lines":["first-t","null-t","number-t","object-t","empty-t","boolean-t","last-t"]}]}})";
	QmMusicLyrics::SLyricsData Lyrics;
	ASSERT_TRUE(QmSpotify::ParseColorLyrics(pJson, &Lyrics));
	ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 2u);
	ASSERT_EQ(Lyrics.m_vTranslations.size(), 2u);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_Text, "first");
	EXPECT_EQ(Lyrics.m_vTranslations[0], "first-t");
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[1].m_Text, "last");
	EXPECT_EQ(Lyrics.m_vTranslations[1], "last-t");
}

TEST(QmSpotifyTranslation, InvalidTimedRowsDoNotShiftSurvivingTranslations)
{
	const char *pJson = R"({"lyrics":{"syncType":"LINE_SYNCED","lines":[
		{"startTimeMs":"1000","endTimeMs":1e400,"words":"first"},
		{"words":"missing"},{"startTimeMs":-1,"words":"negative"},
		{"startTimeMs":"18446744073709551616","words":"overflow-string"},
		{"startTimeMs":1e400,"words":"infinite"},
		{"startTimeMs":"3000","words":"last"}],
		"alternatives":[{"lines":["first-t","missing-t","negative-t","overflow-t","infinite-t","last-t"]}]}})";
	QmMusicLyrics::SLyricsData Lyrics;
	ASSERT_TRUE(QmSpotify::ParseColorLyrics(pJson, &Lyrics));
	ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 2u);
	ASSERT_EQ(Lyrics.m_vTranslations.size(), 2u);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_Text, "first");
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_EndMs, -1);
	EXPECT_EQ(Lyrics.m_vTranslations[0], "first-t");
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[1].m_Text, "last");
	EXPECT_EQ(Lyrics.m_vTranslations[1], "last-t");
}

TEST(QmSpotifyTranslation, EqualTicksKeepOriginalMainAndTranslationOrder)
{
	const char *pJson = R"({"lyrics":{"syncType":"LINE_SYNCED","lines":[
		{"startTimeMs":"3000","words":"late"},
		{"startTimeMs":"1000","words":"first-tie"},
		{"startTimeMs":"1000","words":"second-tie"},
		{"startTimeMs":"1000","words":"third-tie"}],
		"alternatives":[{"lines":["late-t","first-t","second-t","third-t"]}]}})";
	QmMusicLyrics::SLyricsData Lyrics;
	ASSERT_TRUE(QmSpotify::ParseColorLyrics(pJson, &Lyrics));
	ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 4u);
	ASSERT_EQ(Lyrics.m_vTranslations.size(), 4u);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_Text, "first-tie");
	EXPECT_EQ(Lyrics.m_vTranslations[0], "first-t");
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[1].m_Text, "second-tie");
	EXPECT_EQ(Lyrics.m_vTranslations[1], "second-t");
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[2].m_Text, "third-tie");
	EXPECT_EQ(Lyrics.m_vTranslations[2], "third-t");
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[3].m_Text, "late");
	EXPECT_EQ(Lyrics.m_vTranslations[3], "late-t");
}

TEST(QmSpotifyTranslation, ShortFirstAlternativeLeavesItsMissingSlotsEmpty)
{
	const char *pJson = R"({"lyrics":{"syncType":"LINE_SYNCED","lines":[
		{"startTimeMs":"3000","words":"late"},
		{"startTimeMs":"1000","words":"early"},
		{"startTimeMs":"2000","words":"middle"}],
		"alternatives":[{"lines":["late-t",17]},
			{"lines":["other-late","other-early","other-middle"]}]}})";
	QmMusicLyrics::SLyricsData Lyrics;
	ASSERT_TRUE(QmSpotify::ParseColorLyrics(pJson, &Lyrics));
	ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 3u);
	ASSERT_EQ(Lyrics.m_vTranslations.size(), 3u);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_Text, "early");
	EXPECT_TRUE(Lyrics.m_vTranslations[0].empty());
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[1].m_Text, "middle");
	EXPECT_TRUE(Lyrics.m_vTranslations[1].empty());
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[2].m_Text, "late");
	EXPECT_EQ(Lyrics.m_vTranslations[2], "late-t");
}

TEST(QmSpotifyTranslation, TrackShapePreservesAbsentAndPresentEmptySemantics)
{
	struct SCase
	{
		const char *m_pAlternatives;
		std::size_t m_ExpectedSlots;
	};
	const SCase aCases[] = {
		{"", 0},
		{R"(,"alternatives":null)", 0},
		{R"(,"alternatives":[])", 0},
		{R"(,"alternatives":[null,{"lines":["ignored","ignored"]}])", 0},
		{R"(,"alternatives":[{}, {"lines":["ignored","ignored"]}])", 0},
		{R"(,"alternatives":[{"lines":42}])", 0},
		{R"(,"alternatives":[{"lines":[]}])", 2},
	};
	const std::string Main = R"({"lyrics":{"syncType":"LINE_SYNCED","lines":[
		{"startTimeMs":1000,"words":"first"},{"startTimeMs":2000,"words":"second"}])";
	QmMusicLyrics::SLyricsData Lyrics;
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_pAlternatives);
		Lyrics.m_vTranslations = {"old-first", "old-second", "old-extra"};
		ASSERT_TRUE(QmSpotify::ParseColorLyrics(Main + Case.m_pAlternatives + "}}", &Lyrics));
		ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 2u);
		ASSERT_EQ(Lyrics.m_vTranslations.size(), Case.m_ExpectedSlots);
		for(const auto &Translation : Lyrics.m_vTranslations)
			EXPECT_TRUE(Translation.empty());
	}
}

TEST(QmSpotifyTranslation, UnsyncedRowsKeepSourceOrderAndTheirTranslations)
{
	const char *pJson = R"({"lyrics":{"syncType":"UNSYNCED","lines":[
		{"startTimeMs":"3000","words":"later"},{"words":"untimed"},
		{"words":""},{"startTimeMs":"1000","words":"earlier"}],
		"alternatives":[{"lines":["later-t","untimed-t","empty-t","earlier-t"]}]}})";
	QmMusicLyrics::SLyricsData Lyrics;
	ASSERT_TRUE(QmSpotify::ParseColorLyrics(pJson, &Lyrics));
	EXPECT_FALSE(Lyrics.m_Timeline.m_HasTiming);
	ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 3u);
	ASSERT_EQ(Lyrics.m_vTranslations.size(), 3u);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_Text, "later");
	EXPECT_EQ(Lyrics.m_vTranslations[0], "later-t");
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[1].m_Text, "untimed");
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[1].m_StartMs, -1);
	EXPECT_EQ(Lyrics.m_vTranslations[1], "untimed-t");
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[2].m_Text, "earlier");
	EXPECT_EQ(Lyrics.m_vTranslations[2], "earlier-t");
}
