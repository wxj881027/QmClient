#include <game/client/components/qmclient/music_lyrics/music_lyrics_community_format.h>
#include <game/client/components/qmclient/music_lyrics/music_lyrics_qrc.h>
// SelectCurrentLine / SelectLatestStartedLine / SSelectedLine 声明在独立的 timeline 头里，
// qrc 头只给出 STimeline 与解析入口。
#include <game/client/components/qmclient/netease/netease_lyric_timeline.h>

#include <gtest/gtest.h>
#include <test/support/qrc_test_fixture.h>

#include <string>
#include <vector>

using namespace QmMusicLyrics;
using QrcTestFixture::ENCRYPTED_RLRC_HEX;
using QrcTestFixture::FromHex;

TEST(MusicLyricsQrc, DecryptsFixedVectorToXml)
{
	const std::vector<uint8_t> Data = FromHex(ENCRYPTED_RLRC_HEX);
	std::string Text;
	std::string Error;
	ASSERT_TRUE(DecryptQrc(std::string_view((const char *)Data.data(), Data.size()), &Text, &Error)) << Error;
	EXPECT_NE(Text.find("<QrcInfos>"), std::string::npos);
}

TEST(MusicLyricsQrc, RejectsBadMagic)
{
	std::string Error;
	std::string Text;
	std::vector<uint8_t> Bad = {(uint8_t)'x', (uint8_t)'x', (uint8_t)'x', (uint8_t)'x', (uint8_t)'x', (uint8_t)'x', (uint8_t)'x', (uint8_t)'x', (uint8_t)'x', (uint8_t)'x', (uint8_t)'x'};
	EXPECT_FALSE(DecryptQrc(std::string_view((const char *)Bad.data(), Bad.size()), &Text, &Error));
	EXPECT_EQ(Error, "bad qrc magic");
}

TEST(MusicLyricsQrc, RejectsTruncatedInput)
{
	std::string Error;
	std::string Text;
	std::vector<uint8_t> Short = {(uint8_t)'\x98', (uint8_t)'\x25'};
	EXPECT_FALSE(DecryptQrc(std::string_view((const char *)Short.data(), Short.size()), &Text, &Error));
}

TEST(MusicLyricsQrc, ExtractsLyricContentFromXml)
{
	const std::string Xml = "<?xml version=\"1.0\"?><QrcInfos><LyricInfo LyricCount=\"1\"><Lyric_1 LyricType=\"1\" LyricContent=\"[ti:测试]&quot;引用&quot;\n[0,100]你(0,50)好(50,50)\"/></LyricInfo></QrcInfos>";
	std::string Content;
	std::string Error;
	ASSERT_TRUE(ExtractQrcLyricContent(Xml, &Content, &Error)) << Error;
	EXPECT_EQ(Content, "[ti:测试]\"引用\"\n[0,100]你(0,50)好(50,50)");
}

TEST(MusicLyricsQrc, ExtractFailsWithoutLyricContent)
{
	std::string Error;
	std::string Content;
	EXPECT_FALSE(ExtractQrcLyricContent("<QrcInfos/>", &Content, &Error));
}

TEST(MusicLyricsQrc, ParsesRlrcWordTiming)
{
	const char *Rlrc = "[ti:测试]\n[ar:歌手]\n[0,4390]停(0,274)下(274,274)脚(548,274)步(822,274)吧(1096,274)\n[4390,2000]第二(4390,500)行(4890,600)歌(5490,700)词(6190,300)\n";
	NeteaseLyrics::STimeline Timeline;
	std::string Error;
	ASSERT_TRUE(ParseQrcRlrc(Rlrc, &Timeline, &Error)) << Error;
	ASSERT_EQ(Timeline.m_vLines.size(), 2u);
	EXPECT_EQ(Timeline.m_vLines[0].m_StartMs, 0);
	EXPECT_EQ(Timeline.m_vLines[0].m_EndMs, 4390);
	EXPECT_EQ(Timeline.m_vLines[0].m_Text, "停下脚步吧");
	ASSERT_EQ(Timeline.m_vLines[0].m_vWords.size(), 5u);
	EXPECT_EQ(Timeline.m_vLines[0].m_vWords[0].m_StartMs, 0);
	EXPECT_EQ(Timeline.m_vLines[0].m_vWords[0].m_EndMs, 274);
	EXPECT_EQ(Timeline.m_vLines[0].m_vWords[1].m_StartMs, 274);
	const char *apFirstWords[] = {"停", "下", "脚", "步", "吧"};
	for(size_t Index = 0; Index < Timeline.m_vLines[0].m_vWords.size(); ++Index)
		EXPECT_EQ(Timeline.m_vLines[0].m_vWords[Index].m_Text, apFirstWords[Index]);
	EXPECT_EQ(Timeline.m_vLines[1].m_StartMs, 4390);
	EXPECT_EQ(Timeline.m_vLines[1].m_EndMs, 6390);
	EXPECT_EQ(Timeline.m_vLines[1].m_Text, "第二行歌词");
	ASSERT_EQ(Timeline.m_vLines[1].m_vWords.size(), 4u);
	EXPECT_EQ(Timeline.m_vLines[1].m_vWords[3].m_StartMs, 6190);
	EXPECT_EQ(Timeline.m_vLines[1].m_vWords[3].m_EndMs, 6490);
	const char *apSecondWords[] = {"第二", "行", "歌", "词"};
	for(size_t Index = 0; Index < Timeline.m_vLines[1].m_vWords.size(); ++Index)
		EXPECT_EQ(Timeline.m_vLines[1].m_vWords[Index].m_Text, apSecondWords[Index]);
}

TEST(MusicLyricsQrc, KeepsUntimedFragmentsAndPermissiveMarkers)
{
	struct SCase
	{
		const char *m_pBody;
		const char *m_pText;
		std::vector<const char *> m_vWords;
		bool m_HasWordTiming;
	};
	const SCase aCases[] = {
		{"没有逐字标记", "没有逐字标记", {}, false},
		{"前词(60000,500)尾文", "前词尾文", {"前词"}, false},
		{"(60000,100)前词(60100,400)(60500,100)", "前词", {"", "前词", ""}, true},
		{"前)词(60000,500)", "前)词", {"前)词"}, true},
		{"零时长(60000,0)正常(60000,500)", "零时长正常", {"正常"}, false},
		{"溢出(9223372036854775807,1)正常(60000,500)", "溢出正常", {"正常"}, false},
	};
	for(const SCase &Case : aCases)
	{
		SCOPED_TRACE(Case.m_pBody);
		SLyricsData Lyrics;
		ASSERT_TRUE(ParseQrcRlrc(std::string("[60000,2000]") + Case.m_pBody, &Lyrics.m_Timeline));
		ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 1u);
		const auto &Line = Lyrics.m_Timeline.m_vLines.front();
		EXPECT_EQ(Line.m_StartMs, 60000);
		EXPECT_EQ(Line.m_EndMs, 62000);
		EXPECT_EQ(Line.m_Text, Case.m_pText);
		ASSERT_EQ(Line.m_vWords.size(), Case.m_vWords.size());
		for(size_t Index = 0; Index < Case.m_vWords.size(); ++Index)
			EXPECT_EQ(Line.m_vWords[Index].m_Text, Case.m_vWords[Index]);
		EXPECT_EQ(HasWordTiming(Lyrics), Case.m_HasWordTiming);
	}
}

TEST(MusicLyricsQrc, DiscardsMalformedMarkerLineAndKeepsFollowingLine)
{
	const char *apBodies[] = {
		"缺尾括号(60000,100",
		"缺逗号(60000)",
		"负数(-1,100)",
		"坏值(60000,no)",
		"超范围(9223372036854775808,1)",
		"额外逗号(60000,100,0)",
	};
	for(const char *pBody : apBodies)
	{
		SCOPED_TRACE(pBody);
		SLyricsData Lyrics;
		const std::string Rlrc = std::string("[60000,2000]") + pBody + "\n[63000,500]保留(63000,500)\n";
		ASSERT_TRUE(ParseQrcRlrc(Rlrc, &Lyrics.m_Timeline));
		ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 1u);
		const auto &Line = Lyrics.m_Timeline.m_vLines.front();
		EXPECT_EQ(Line.m_StartMs, 63000);
		EXPECT_EQ(Line.m_EndMs, 63500);
		EXPECT_EQ(Line.m_Text, "保留");
		ASSERT_EQ(Line.m_vWords.size(), 1u);
		EXPECT_EQ(Line.m_vWords[0].m_Text, "保留");
		EXPECT_EQ(Line.m_vWords[0].m_StartMs, 63000);
		EXPECT_EQ(Line.m_vWords[0].m_EndMs, 63500);
		EXPECT_TRUE(HasWordTiming(Lyrics));
	}
}

TEST(MusicLyricsQrc, SelectsPreviousLineDuringInterlude)
{
	const char *Rlrc = "[0,1000]第一(0,1000)\n[2000,1000]第二(2000,1000)\n";
	NeteaseLyrics::STimeline Timeline;
	std::string Error;
	ASSERT_TRUE(ParseQrcRlrc(Rlrc, &Timeline, &Error)) << Error;
	EXPECT_EQ(NeteaseLyrics::SelectCurrentLine(Timeline, 1500).m_Index, -1);
	const NeteaseLyrics::SSelectedLine Selected = NeteaseLyrics::SelectLatestStartedLine(Timeline, 1500);
	EXPECT_EQ(Selected.m_Index, 0);
	EXPECT_FALSE(Selected.m_InTimedRange);
}

TEST(MusicLyricsQrc, RoundTripsFixedVectorThroughDataEntry)
{
	const std::vector<uint8_t> Data = FromHex(ENCRYPTED_RLRC_HEX);
	SLyricsData Lyrics;
	std::string Error;
	ASSERT_TRUE(ParseQrcData(std::string_view((const char *)Data.data(), Data.size()), &Lyrics, &Error)) << Error;
	EXPECT_TRUE(Lyrics.HasLyrics());
	ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 2u);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_Text, "停下脚步吧");
	ASSERT_EQ(Lyrics.m_Timeline.m_vLines[0].m_vWords.size(), 5u);
	const char *apWords[] = {"停", "下", "脚", "步", "吧"};
	for(size_t Index = 0; Index < Lyrics.m_Timeline.m_vLines[0].m_vWords.size(); ++Index)
		EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_vWords[Index].m_Text, apWords[Index]);
	EXPECT_TRUE(HasWordTiming(Lyrics));
	EXPECT_TRUE(Lyrics.m_vTranslations.empty());
}

TEST(MusicLyricsQrc, RejectsUntimedRlrc)
{
	NeteaseLyrics::STimeline Timeline;
	std::string Error;
	EXPECT_FALSE(ParseQrcRlrc("plain text\n", &Timeline, &Error));
	EXPECT_FALSE(ParseQrcRlrc("[ti:只有元数据]\n", &Timeline, &Error));
}
