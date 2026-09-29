#include <gtest/gtest.h>
#include <qm/music/qm_kugou_protocol.h>

using namespace QmMusicHook;

TEST(QmKugouProtocol, ReadsNativeHashAndHundredNanosecondProgress)
{
	SPlayback State;
	ASSERT_TRUE(ParseKugouPlayback(R"({"hash":"ABCDEF0123456789ABCDEF0123456789","filename":"歌手 - 歌曲","play_status":"playing","progress":"125000000","duration":"180000","mix_song_id":"77"})", &State));
	EXPECT_EQ(State.m_MediaId, "abcdef0123456789abcdef0123456789");
	EXPECT_EQ(State.m_Title, "歌曲");
	EXPECT_EQ(State.m_Artist, "歌手");
	EXPECT_TRUE(State.m_HasSong);
	EXPECT_TRUE(State.m_Playing);
	EXPECT_TRUE(State.m_PositionValid);
	EXPECT_EQ(State.m_PositionMs, 12500);
	EXPECT_EQ(State.m_DurationMs, 180000);
}

TEST(QmKugouProtocol, PauseSeekAndStopDoNotReuseOldPositionOrSong)
{
	SPlayback State;
	ASSERT_TRUE(ParseKugouPlayback(R"({"hash":"0123456789abcdef0123456789abcdef","filename":"Song","play_status":"paused","progress":420000000,"duration":180000})", &State));
	EXPECT_FALSE(State.m_Playing);
	EXPECT_EQ(State.m_PositionMs, 42000);
	ASSERT_TRUE(ParseKugouPlayback(R"({"hash":"0123456789abcdef0123456789abcdef","filename":"Song","play_status":"playing","progress":"70000000","duration":"180000"})", &State));
	EXPECT_EQ(State.m_PositionMs, 7000);
	ASSERT_TRUE(ParseKugouPlayback(R"({"hash":"0123456789abcdef0123456789abcdef","play_status":"stopped"})", &State));
	EXPECT_FALSE(State.m_HasSong);
	EXPECT_TRUE(State.m_MediaId.empty());
}

TEST(QmKugouProtocol, RejectsMalformedIdentityAndMarksTransientProgressInvalid)
{
	SPlayback State;
	EXPECT_FALSE(ParseKugouPlayback("[]", &State));
	EXPECT_FALSE(ParseKugouPlayback(R"({"hash":"name&hash=other","play_status":"playing"})", &State));
	ASSERT_TRUE(ParseKugouPlayback(R"({"hash":"0123456789abcdef0123456789abcdef","play_status":"playing","progress":"18446744073709551615","duration":"180000"})", &State));
	EXPECT_TRUE(State.m_HasSong);
	EXPECT_FALSE(State.m_PositionValid);
	ASSERT_TRUE(ParseKugouPlayback(R"({"hash":"0123456789abcdef0123456789abcdef","play_status":"playing","progress":"-10","duration":"-1"})", &State));
	EXPECT_EQ(State.m_DurationMs, 0);
	EXPECT_FALSE(State.m_PositionValid);
	ASSERT_TRUE(ParseKugouPlayback("{}", &State));
	EXPECT_FALSE(State.m_HasSong);
}

TEST(QmKugouProtocol, SelectsServerRecommendationAndReportsNoLyrics)
{
	std::string Id, Key;
	ASSERT_TRUE(SelectKugouLyricCandidate(R"({"status":200,"proposal":"2","candidates":[{"id":"1","accesskey":"first"},{"id":"2","accesskey":"recommended"}]})", &Id, &Key));
	EXPECT_EQ(Id, "2");
	EXPECT_EQ(Key, "recommended");
	ASSERT_TRUE(SelectKugouLyricCandidate(R"({"status":200,"candidates":[]})", &Id, &Key));
	EXPECT_TRUE(Id.empty());
	EXPECT_TRUE(Key.empty());
	EXPECT_FALSE(SelectKugouLyricCandidate("not json", &Id, &Key));
}

TEST(QmKugouProtocol, DecodesOriginalLrcAndRejectsBrokenBase64)
{
	std::string Text;
	ASSERT_TRUE(DecodeKugouLyricResponse(R"({"status":200,"content":"WzAwOjAxLjAwXWhlbGxv"})", false, &Text));
	EXPECT_EQ(Text, "[00:01.00]hello");
	EXPECT_FALSE(DecodeKugouLyricResponse(R"({"status":200,"content":"@@@="})", false, &Text));
	EXPECT_TRUE(Text.empty());
}
