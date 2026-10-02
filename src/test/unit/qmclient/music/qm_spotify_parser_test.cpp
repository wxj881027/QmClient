#include <game/client/components/qmclient/music_lyrics/qm_spotify_parser.h>

#include <gtest/gtest.h>

#include <string>

using namespace QmMusicLyrics;

namespace
{
	const char *LINE_SYNCED_JSON =
		"{\"lyrics\":{\"syncType\":\"LINE_SYNCED\",\"lines\":["
		"{\"startTimeMs\":\"1000\",\"endTimeMs\":\"3000\",\"words\":\"Hello world\",\"syllables\":[]},"
		"{\"startTimeMs\":\"3500\",\"endTimeMs\":\"5500\",\"words\":\"Second line\",\"syllables\":[]}"
		"],\"alternatives\":[{\"language\":\"zh-Hans\",\"lines\":[\"你好世界\",\"第二行\"]}],"
		"\"provider\":\"musixmatch\"},\"colors\":{\"background\":0,\"text\":0,\"highlightText\":0},\"hasVocalRemoval\":false}";

	const char *SYLLABLE_SYNCED_JSON =
		"{\"lyrics\":{\"syncType\":\"SYLLABLE_SYNCED\",\"lines\":["
		"{\"startTimeMs\":\"1000\",\"endTimeMs\":\"5000\",\"words\":\"\xe6\x98\x9f\xe5\x85\x89\xe9\x97\xaa\xe7\x83\x81\",\"syllables\":["
		"{\"startTimeMs\":\"1000\",\"endTimeMs\":\"2000\",\"numChars\":\"1\"},"
		"{\"startTimeMs\":\"2000\",\"endTimeMs\":\"3000\",\"numChars\":\"1\"},"
		"{\"startTimeMs\":\"3000\",\"endTimeMs\":\"4000\",\"numChars\":\"2\"}"
		"]}]}}";

	const char *UNSYNCED_JSON =
		"{\"lyrics\":{\"syncType\":\"UNSYNCED\",\"lines\":["
		"{\"startTimeMs\":\"0\",\"endTimeMs\":\"0\",\"words\":\"plain text\"}"
		"]}}";
}

TEST(QmSpotifyParser, ColorLyricsLineSynced)
{
	SLyricsData Lyrics;
	ASSERT_TRUE(QmSpotify::ParseColorLyrics(LINE_SYNCED_JSON, &Lyrics));
	ASSERT_TRUE(Lyrics.HasLyrics());
	ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 2);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_Text, "Hello world");
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_StartMs, 1000);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_EndMs, 3000);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[1].m_StartMs, 3500);
	EXPECT_TRUE(Lyrics.m_Timeline.m_vLines[0].m_vWords.empty());
	// 翻译轨按下标对齐。
	ASSERT_TRUE(Lyrics.HasTranslation());
	ASSERT_EQ(Lyrics.m_vTranslations.size(), 2);
	EXPECT_EQ(Lyrics.m_vTranslations[0], "你好世界");
	EXPECT_EQ(Lyrics.m_vTranslations[1], "第二行");
}

TEST(QmSpotifyParser, ColorLyricsSortsLines)
{
	// 乱序行应被稳定排序,保证 SelectCurrentLine 的二分可用。
	const std::string Json =
		"{\"lyrics\":{\"syncType\":\"LINE_SYNCED\",\"lines\":["
		"{\"startTimeMs\":\"5000\",\"endTimeMs\":\"6000\",\"words\":\"later\"},"
		"{\"startTimeMs\":\"1000\",\"endTimeMs\":\"2000\",\"words\":\"earlier\"}"
		"]}}";
	SLyricsData Lyrics;
	ASSERT_TRUE(QmSpotify::ParseColorLyrics(Json, &Lyrics));
	ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 2);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_Text, "earlier");
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[1].m_Text, "later");
}

TEST(QmSpotifyParser, ColorLyricsSyllableSyncedUtf8)
{
	// 逐字切分按 UTF-8 码点进行:"星光闪烁" 4 个码点,音节切 1/1/2。
	SLyricsData Lyrics;
	ASSERT_TRUE(QmSpotify::ParseColorLyrics(SYLLABLE_SYNCED_JSON, &Lyrics));
	ASSERT_TRUE(Lyrics.HasLyrics());
	ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 1);
	const NeteaseLyrics::SLine &Line = Lyrics.m_Timeline.m_vLines[0];
	EXPECT_EQ(Line.m_Text, "星光闪烁");
	ASSERT_EQ(Line.m_vWords.size(), 3);
	EXPECT_EQ(Line.m_vWords[0].m_Text, "星");
	EXPECT_EQ(Line.m_vWords[0].m_StartMs, 1000);
	EXPECT_EQ(Line.m_vWords[0].m_EndMs, 2000);
	EXPECT_EQ(Line.m_vWords[1].m_Text, "光");
	EXPECT_EQ(Line.m_vWords[1].m_StartMs, 2000);
	EXPECT_EQ(Line.m_vWords[2].m_Text, "闪烁");
	EXPECT_EQ(Line.m_vWords[2].m_StartMs, 3000);
	EXPECT_EQ(Line.m_vWords[2].m_EndMs, 4000);
}

TEST(QmSpotifyParser, ColorLyricsUnsyncedHasNoTiming)
{
	SLyricsData Lyrics;
	ASSERT_TRUE(QmSpotify::ParseColorLyrics(UNSYNCED_JSON, &Lyrics));
	EXPECT_FALSE(Lyrics.HasLyrics());
	ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 1);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_Text, "plain text");
}

TEST(QmSpotifyParser, ColorLyricsMalformed)
{
	SLyricsData Lyrics;
	EXPECT_FALSE(QmSpotify::ParseColorLyrics("not json", &Lyrics));
	// 合法 JSON 但无歌词字段:解析成功、内容为空。
	ASSERT_TRUE(QmSpotify::ParseColorLyrics("{}", &Lyrics));
	EXPECT_FALSE(Lyrics.HasLyrics());
	EXPECT_TRUE(Lyrics.m_Timeline.m_vLines.empty());
}

TEST(QmSpotifyParser, SearchPathfinderShape)
{
	const std::string Json =
		"{\"data\":{\"searchV2\":{\"tracks\":{\"items\":["
		"{\"data\":{\"id\":\"abc123\",\"name\":\"Song One\",\"albumOfTrack\":{\"name\":\"Album X\"},"
		"\"artists\":{\"items\":[{\"profile\":{\"name\":\"Artist A\"}},{\"profile\":{\"name\":\"Artist B\"}}]}}}"
		"]}}}}";
	std::vector<QmSpotify::STrackCandidate> Candidates;
	ASSERT_TRUE(QmSpotify::ParseSearchResponse(Json, &Candidates));
	ASSERT_EQ(Candidates.size(), 1);
	EXPECT_EQ(Candidates[0].m_Id, "abc123");
	EXPECT_EQ(Candidates[0].m_Title, "Song One");
	EXPECT_EQ(Candidates[0].m_Artist, "Artist A, Artist B");
	EXPECT_EQ(Candidates[0].m_Album, "Album X");
}

TEST(QmSpotifyParser, SearchPathfinderUriAndTrackFallback)
{
	const std::string Json =
		"{\"data\":{\"search\":{\"tracks\":{\"items\":["
		"{\"data\":{\"uri\":\"spotify:track:def456\",\"name\":\"Song Two\","
		"\"track\":{\"name\":\"Song Two\",\"artists\":{\"items\":[{\"name\":\"Solo\"}]}}}}"
		"]}}}}";
	std::vector<QmSpotify::STrackCandidate> Candidates;
	ASSERT_TRUE(QmSpotify::ParseSearchResponse(Json, &Candidates));
	ASSERT_EQ(Candidates.size(), 1);
	EXPECT_EQ(Candidates[0].m_Id, "def456");
	EXPECT_EQ(Candidates[0].m_Artist, "Solo");
}

TEST(QmSpotifyParser, SearchWebApiShapeWithIsrc)
{
	const std::string Json =
		"{\"tracks\":{\"items\":["
		"{\"id\":\"abc\",\"name\":\"T\",\"artists\":[{\"name\":\"A\"}],\"album\":{\"name\":\"Al\"},"
		"\"duration_ms\":123456,\"external_ids\":{\"isrc\":\"USRC17607839\"}}"
		"]}}";
	std::vector<QmSpotify::STrackCandidate> Candidates;
	ASSERT_TRUE(QmSpotify::ParseSearchResponse(Json, &Candidates));
	ASSERT_EQ(Candidates.size(), 1);
	EXPECT_EQ(Candidates[0].m_Id, "abc");
	EXPECT_EQ(Candidates[0].m_Isrc, "USRC17607839");
	EXPECT_EQ(Candidates[0].m_Album, "Al");
}

TEST(QmSpotifyParser, ServerTime)
{
	int64_t ServerTime = 0;
	ASSERT_TRUE(QmSpotify::ParseServerTime("{\"serverTime\":1788179298}", &ServerTime));
	EXPECT_EQ(ServerTime, 1788179298);
	EXPECT_FALSE(QmSpotify::ParseServerTime("{\"serverTime\":\"x\"}", &ServerTime));
	EXPECT_FALSE(QmSpotify::ParseServerTime("garbage", &ServerTime));
}

TEST(QmSpotifyParser, TokenResponse)
{
	std::string Token;
	int64_t ExpirationMs = 0;
	ASSERT_TRUE(QmSpotify::ParseTokenResponse(
		"{\"accessToken\":\"tok123\",\"accessTokenExpirationTimestampMs\":1788179298000,\"isAnonymous\":false}",
		&Token, &ExpirationMs));
	EXPECT_EQ(Token, "tok123");
	EXPECT_EQ(ExpirationMs, 1788179298000);
	// 匿名 token 视为无效。
	EXPECT_FALSE(QmSpotify::ParseTokenResponse(
		"{\"accessToken\":\"tok\",\"accessTokenExpirationTimestampMs\":1788179298000,\"isAnonymous\":true}",
		&Token, &ExpirationMs));
	EXPECT_FALSE(QmSpotify::ParseTokenResponse("{\"accessToken\":\"\"}", &Token, &ExpirationMs));
}

TEST(QmSpotifyParser, LrclibSynced)
{
	const std::string Json =
		"{\"trackName\":\"T\",\"artistName\":\"A\",\"albumName\":\"Al\",\"duration\":180000,"
		"\"syncedLyrics\":\"[00:01.00]line one\\n[00:03.00]line two\\n\"}";
	SLyricsData Lyrics;
	ASSERT_TRUE(QmSpotify::ParseLrclib(Json, &Lyrics));
	ASSERT_TRUE(Lyrics.HasLyrics());
	ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 2);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_Text, "line one");
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_StartMs, 1000);
	EXPECT_EQ(Lyrics.m_Song.m_Title, "T");
	EXPECT_EQ(Lyrics.m_Song.m_Artist, "A");
	EXPECT_EQ(Lyrics.m_Song.m_DurationMs, 180000);
}

TEST(QmSpotifyParser, LrclibUnsyncedOnly)
{
	const std::string Json =
		"{\"trackName\":\"T\",\"artistName\":\"A\",\"syncedLyrics\":null,\"plainLyrics\":\"no timing\"}";
	SLyricsData Lyrics;
	ASSERT_TRUE(QmSpotify::ParseLrclib(Json, &Lyrics));
	EXPECT_FALSE(Lyrics.HasLyrics());
}
