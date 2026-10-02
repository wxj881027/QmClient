#include <game/client/components/qmclient/music_lyrics/music_lyrics_qrc.h>
// SelectCurrentLine / SelectLatestStartedLine / SSelectedLine 声明在独立的 timeline 头里，
// qrc 头只给出 STimeline 与解析入口。
#include <game/client/components/qmclient/netease/netease_lyric_timeline.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace QmMusicLyrics;

namespace
{
	// 由 Python 验证脚本(tmp/verify_qrc.py 算法)构造的固定向量:
	// 明文(XML 包裹 rlrc) -> zlib -> 3DES 加密 -> QMC1 XOR(全局偏移) -> 魔数头。
	// 明文:
	//   <?xml version="1.0" encoding="utf-8"?>
	//   <QrcInfos>
	//   <LyricInfo LyricCount="1">
	//   <Lyric_1 LyricType="1" LyricContent="[ti:测试歌曲]
	//   [ar:测试歌手]
	//   [0,4390]停(0,274)下(274,274)脚(548,274)步(822,274)吧(1096,274)
	//   [4390,2000]第二(4390,500)行(4890,600)歌(5490,700)词(6190,300)"/>
	//   </LyricInfo>
	//   </QrcInfos>
	const char *ENCRYPTED_RLRC_HEX =
		"9825b0ace3028368e8fc6c6e123cd43e97c7e195d1c1797a8163073d3aaea503778bf71928c92e810fb5acee6a06e752e8b378b9a58c5c04f6e262135f54cf16da5b938a440d187557cd781f8c237b0f63556bd9845001bf08e2af678cd9bbc9bbe1416f22325ab3ac570d4b44dc1d25d95f9966c4531604e3b3347ff66ee5392f9f822fc958e761b578e6439f6a77b017c68b3d14f2470374f502c0d9db630b3cabf1d55d4a3d4314e91051b6ed19bdea1218c73e41f0a8bcc85db0909ecb29e9d58f9c71380d125623ced5164e393e598a3816a7938efab653c84b4ad54bfdacfb385d873bbb33433a97163787ac7eec0f1687aed9d4d1a1aa22689539da06391ddcc3533f77d6206ee4";

	std::vector<uint8_t> FromHex(const char *pH)
	{
		std::vector<uint8_t> Result;
		while(pH[0] != '\0' && pH[1] != '\0')
		{
			auto Digit = [](char C) -> int {
				if(C >= '0' && C <= '9')
					return C - '0';
				if(C >= 'a' && C <= 'f')
					return C - 'a' + 10;
				if(C >= 'A' && C <= 'F')
					return C - 'A' + 10;
				return 0;
			};
			Result.push_back((uint8_t)((Digit(pH[0]) << 4) | Digit(pH[1])));
			pH += 2;
		}
		return Result;
	}
}

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
	EXPECT_EQ(Timeline.m_vLines[1].m_StartMs, 4390);
	EXPECT_EQ(Timeline.m_vLines[1].m_EndMs, 6390);
	ASSERT_EQ(Timeline.m_vLines[1].m_vWords.size(), 4u);
	EXPECT_EQ(Timeline.m_vLines[1].m_vWords[3].m_StartMs, 6190);
	EXPECT_EQ(Timeline.m_vLines[1].m_vWords[3].m_EndMs, 6490);
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
	EXPECT_TRUE(Lyrics.m_vTranslations.empty());
}

TEST(MusicLyricsQrc, RejectsUntimedRlrc)
{
	NeteaseLyrics::STimeline Timeline;
	std::string Error;
	EXPECT_FALSE(ParseQrcRlrc("plain text\n", &Timeline, &Error));
	EXPECT_FALSE(ParseQrcRlrc("[ti:只有元数据]\n", &Timeline, &Error));
}
