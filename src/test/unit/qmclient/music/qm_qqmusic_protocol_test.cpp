#include <gtest/gtest.h>
#include <qm/music/qm_qqmusic_protocol.h>

#include <array>
#include <cstring>

using namespace QmMusicHook::QQMusic;

TEST(QQMusicProtocol, UnknownVersionsNeverBorrowOffsets)
{
	EXPECT_NE(FindOffsets(22, 60), nullptr);
	EXPECT_NE(FindOffsets(20, 5), nullptr);
	EXPECT_EQ(FindOffsets(22, 61), nullptr);
	EXPECT_EQ(FindOffsets(0, 0), nullptr);
}

TEST(QQMusicProtocol, MidMustComeFromAnExactPlayerField)
{
	EXPECT_EQ(ExtractSongMid("https://stream.qqmusic.qq.com/00281PXu4DHKNp.wma", ""), "00281PXu4DHKNp");
	EXPECT_EQ(ExtractSongMid("", "0=00281PXu4DHKNp&2=123|456"), "00281PXu4DHKNp");
	EXPECT_TRUE(ExtractSongMid("C:/Music/local-song.flac", "").empty());
	EXPECT_TRUE(ExtractSongMid("https://example.com/00281PXu4DHKNp.wma", "").empty());
	EXPECT_TRUE(ExtractSongMid("", "10=00281PXu4DHKNp").empty());
}

TEST(QQMusicProtocol, ClearedHeapSsoIsNeverReadAsInlineText)
{
	std::array<unsigned char, 24> aData{};
	const uint32_t Pointer = 0x12345678;
	const uint32_t Capacity = 128;
	std::memcpy(aData.data(), &Pointer, sizeof(Pointer));
	std::memcpy(aData.data() + 20, &Capacity, sizeof(Capacity));
	SSsoLayout Layout;
	EXPECT_FALSE(DecodeSsoLayout(aData.data(), aData.size(), Layout));
	aData[16] = 9;
	ASSERT_TRUE(DecodeSsoLayout(aData.data(), aData.size(), Layout));
	EXPECT_EQ(Layout.m_Pointer, Pointer);
	EXPECT_EQ(Layout.m_Length, 9u);
}

TEST(QQMusicProtocol, ApiResponseMustBelongToRequestedSong)
{
	SLyrics Lyrics;
	EXPECT_FALSE(ParseLyricsResponse(R"({"req_0":{"code":0,"data":{"songID":2,"lyric":"[00:01.00]wrong"}}})", 1, false, Lyrics));
	EXPECT_TRUE(Lyrics.m_Content.empty());
	// 歌词/翻译里含 `你好")`，默认分隔符会提前终止字面量，这里用自定义分隔符 json。
	ASSERT_TRUE(ParseLyricsResponse(R"json({"req_0":{"code":0,"data":{"songID":1,"crypt":1,"lyric":"[0,1000]hello(0,1000)","trans":"[00:00.00]你好"}}})json", 1, false, Lyrics));
	EXPECT_EQ(Lyrics.m_Type, "qrc");
	EXPECT_EQ(Lyrics.m_Translation, "[00:00.00]你好");
}

TEST(QQMusicProtocol, EmptyLyricsAndErrorsHaveDifferentOutcomes)
{
	SLyrics Lyrics;
	EXPECT_TRUE(ParseLyricsResponse(R"({"req_0":{"code":0,"data":{"songID":7,"lyric":""}}})", 7, false, Lyrics));
	EXPECT_TRUE(Lyrics.m_Content.empty());
	EXPECT_FALSE(ParseLyricsResponse(R"({"req_0":{"code":0}})", 7, false, Lyrics));
	EXPECT_FALSE(ParseLyricsResponse(R"({"retcode":-1,"lyric":""})", 0, true, Lyrics));
	EXPECT_FALSE(ParseLyricsResponse(R"({"req_0":{"code":0,"data":{"songID":7,"crypt":1,"lyric":"0123456789abcdef0123456789abcdef"}}})", 7, false, Lyrics));
}

TEST(QQMusicProtocol, DecryptsNetworkQrcHexWithoutLocalFileEnvelope)
{
	// 从 music_lyrics_qrc_test 的固定文件向量逆 QMC1 XOR，再移除 11 字节头，得到接口密文。
	const char *pHex =
		"0c8d67dd3e549974b64ed2680459f13881aa15d10db4cc8324b86311d0d741bd6af5d8724f2b75716c3a763afd2e1295b3cff519e519a8452bd4adef858f341ee513f672c9a048fe071bd4104bd0b390cd1ce4cf8e78ef35d4b28e652174291c58dbff887b1a5f903d5b3170a53be1c31866a43373ecd554e5593f10336dd9802100317eb0d4981e1e8be1567dac673d7c49601705ffec85e8cde00212cc1d01d529338ca029d41112a1bb96516438aa10003f1dda2a26593de198fae28edaa82f899b30f89a97adc838179f0e6e02f7e1dae83fba6ff54ce6178784a19b448605a8a439202f0f1fdc31bbb27079f8d50f05f30c4cfa570a09c3588084f8cf82";
	SLyrics Lyrics;
	const std::string Response = "{\"req_0\":{\"code\":0,\"data\":{\"songID\":7,\"crypt\":1,\"lyric\":\"" + std::string(pHex) + "\"}}}";
	ASSERT_TRUE(ParseLyricsResponse(Response, 7, false, Lyrics));
	EXPECT_EQ(Lyrics.m_Type, "qrc");
	EXPECT_NE(Lyrics.m_Content.find("停(0,274)下(274,274)"), std::string::npos);
	EXPECT_NE(Lyrics.m_Content.find("[4390,2000]"), std::string::npos);
}

TEST(QQMusicProtocol, MidResponseUnescapesLyrics)
{
	SLyrics Lyrics;
	ASSERT_TRUE(ParseLyricsResponse(R"({"retcode":0,"lyric":"[00:01.00]Tom &amp; Jerry &#39;ok&#39;"})", 0, true, Lyrics));
	EXPECT_EQ(Lyrics.m_Type, "lrc");
	EXPECT_EQ(Lyrics.m_Content, "[00:01.00]Tom & Jerry 'ok'");
}

TEST(QQMusicProtocol, PlaybackClockStopsResumesAndSeeksWithoutInventingProgress)
{
	CPlaybackClock Clock;
	EXPECT_FALSE(Clock.Update("one", 1000, 0));
	EXPECT_TRUE(Clock.Update("one", 1100, 100));
	EXPECT_TRUE(Clock.Update("one", 1100, 500));
	EXPECT_FALSE(Clock.Update("one", 1100, 1300));
	EXPECT_TRUE(Clock.Update("one", 1200, 1400));
	EXPECT_FALSE(Clock.Update("one", 80000, 1500));
	EXPECT_TRUE(Clock.Update("one", 80100, 1600));
	EXPECT_FALSE(Clock.Update("two", 0, 1700));
}

TEST(QQMusicProtocol, SongSwitchRejectsPreviousIdUntilItCatchesUp)
{
	CSongIdentity Identity;
	EXPECT_TRUE(Identity.Update("title A", "artist A", 180000, "id:1", 0).empty());
	EXPECT_EQ(Identity.Update("title A", "artist A", 180000, "id:1", 300), "id:1");
	EXPECT_TRUE(Identity.Update("title B", "artist B", 180000, "id:1", 400).empty());
	EXPECT_TRUE(Identity.Update("title B", "artist B", 180000, "id:1", 800).empty());
	EXPECT_TRUE(Identity.Update("title B", "artist B", 180000, "id:2", 900).empty());
	EXPECT_EQ(Identity.Update("title B", "artist B", 180000, "id:2", 1200), "id:2");
}

TEST(QQMusicProtocol, StartupMetadataMayFinishBeforeIdentityIsAccepted)
{
	CSongIdentity Identity;
	EXPECT_TRUE(Identity.Update("title", "", 180000, "id:1", 0).empty());
	EXPECT_TRUE(Identity.Update("title", "artist", 180000, "id:1", 100).empty());
	EXPECT_EQ(Identity.Update("title", "artist", 180000, "id:1", 400), "id:1");
}

TEST(QQMusicProtocol, MultiStageSongSwitchKeepsRejectingLastConfirmedId)
{
	CSongIdentity Identity;
	Identity.Update("title A", "artist A", 180000, "id:1", 0);
	ASSERT_EQ(Identity.Update("title A", "artist A", 180000, "id:1", 300), "id:1");
	EXPECT_TRUE(Identity.Update("title B temporary", "", 200000, "id:1", 400).empty());
	Identity.Suspend();
	EXPECT_TRUE(Identity.Update("title B", "artist B", 210000, "id:1", 600).empty());
	EXPECT_TRUE(Identity.Update("title B", "artist B", 210000, "id:1", 900).empty());
	EXPECT_TRUE(Identity.Update("title B", "artist B", 210000, "id:2", 1000).empty());
	EXPECT_EQ(Identity.Update("title B", "artist B", 210000, "id:2", 1300), "id:2");
}

TEST(QQMusicProtocol, AcceptedSongAllowsArtistToArriveLater)
{
	CSongIdentity Identity;
	Identity.Update("title", "", 180000, "id:1", 0);
	ASSERT_EQ(Identity.Update("title", "", 180000, "id:1", 300), "id:1");
	EXPECT_EQ(Identity.Update("title", "artist", 180000, "id:1", 400), "id:1");
	EXPECT_EQ(Identity.Update("title", "", 180000, "id:1", 500), "id:1");
	EXPECT_TRUE(Identity.Update("title", "different artist", 180000, "id:1", 600).empty());
	EXPECT_TRUE(Identity.Update("title", "different artist", 180000, "id:1", 900).empty());
}

TEST(QQMusicProtocol, SuspendedSongCanResumeWithTheSameIdentity)
{
	CSongIdentity Identity;
	Identity.Update("title", "artist", 180000, "id:1", 0);
	ASSERT_EQ(Identity.Update("title", "artist", 180000, "id:1", 300), "id:1");
	Identity.Suspend();
	EXPECT_TRUE(Identity.Update("title", "artist", 180000, "id:1", 400).empty());
	EXPECT_EQ(Identity.Update("title", "artist", 180000, "id:1", 700), "id:1");
}
