#include <game/client/components/qmclient/netease/netease_lyric_parser.h>

#include <gtest/gtest.h>

using namespace NeteaseLyrics;

TEST(NeteaseLyricParser, ParsesLrcAndDerivesBoundaries)
{
	STimeline Timeline;
	ASSERT_TRUE(ParseLrc("[00:01.00]第一句\n[00:03.500][00:04.000]第二句\n", &Timeline));
	ASSERT_EQ(Timeline.m_vLines.size(), 3u);
	EXPECT_EQ(Timeline.m_vLines[0].m_StartMs, 1000);
	EXPECT_EQ(Timeline.m_vLines[0].m_EndMs, 3500);
	EXPECT_EQ(Timeline.m_vLines[1].m_StartMs, 3500);
	EXPECT_EQ(Timeline.m_vLines[2].m_StartMs, 4000);
	EXPECT_EQ(Timeline.m_vLines.back().m_EndMs, -1);
}

TEST(NeteaseLyricParser, AcceptsColonSeparatedCentiseconds)
{
	// [mm:ss:xx] 变体:第三段是百分秒。LX Music 自己生成的提示行就用这种写法。
	STimeline Timeline;
	ASSERT_TRUE(ParseLrc("[00:00:00]此歌曲为没有填词的纯音乐，请您欣赏\n[00:12:34]第二句\n", &Timeline));
	ASSERT_EQ(Timeline.m_vLines.size(), 2u);
	EXPECT_EQ(Timeline.m_vLines[0].m_StartMs, 0);
	EXPECT_EQ(Timeline.m_vLines[0].m_Text, "此歌曲为没有填词的纯音乐，请您欣赏");
	EXPECT_EQ(Timeline.m_vLines[1].m_StartMs, 12340);
}

TEST(NeteaseLyricParser, MixesColonAndDotTimestampForms)
{
	// 同一个文件里两种写法混用是合法的,不能因为一行是冒号格式就整体失败。
	STimeline Timeline;
	ASSERT_TRUE(ParseLrc("[00:01.50]点号格式\n[00:02:25]冒号格式\n[00:03.00]再回点号\n", &Timeline));
	ASSERT_EQ(Timeline.m_vLines.size(), 3u);
	EXPECT_EQ(Timeline.m_vLines[0].m_StartMs, 1500);
	EXPECT_EQ(Timeline.m_vLines[1].m_StartMs, 2250);
	EXPECT_EQ(Timeline.m_vLines[2].m_StartMs, 3000);
}

TEST(NeteaseLyricParser, SkipsMetadataAndEmptyLines)
{
	STimeline Timeline;
	EXPECT_TRUE(ParseLrc("[ar:歌手]\n[ti:歌曲]\n\n[00:00.00]你好 😀\n", &Timeline));
	ASSERT_EQ(Timeline.m_vLines.size(), 1u);
	EXPECT_EQ(Timeline.m_vLines[0].m_Text, "你好 😀");
}

TEST(NeteaseLyricParser, TimedEmptyLineEndsPreviousLyric)
{
	STimeline Timeline;
	ASSERT_TRUE(ParseLrc("[00:01.00]first\n[00:03.00]\n[00:05.00]second\n", &Timeline));
	ASSERT_EQ(Timeline.m_vLines.size(), 2u);
	EXPECT_EQ(Timeline.m_vLines[0].m_EndMs, 3000);
	EXPECT_EQ(Timeline.m_vLines[1].m_StartMs, 5000);
}

TEST(NeteaseLyricParser, RejectsMalformedOrUntimedInput)
{
	STimeline Timeline;
	EXPECT_FALSE(ParseLrc("plain text\n", &Timeline));
	EXPECT_FALSE(ParseLrc("[00:99.00]bad\n", &Timeline));
	std::string Invalid = "[00:01.00]";
	Invalid.push_back((char)0xFF);
	EXPECT_FALSE(ParseLrc(Invalid, &Timeline));
}

TEST(NeteaseLyricParser, ParsesYrcWordTiming)
{
	STimeline Timeline;
	ASSERT_TRUE(ParseYrc("[1000,2000](1000,500,0)你(1500,500,0)好\n", &Timeline));
	ASSERT_EQ(Timeline.m_vLines.size(), 1u);
	ASSERT_EQ(Timeline.m_vLines[0].m_vWords.size(), 2u);
	EXPECT_EQ(Timeline.m_vLines[0].m_StartMs, 1000);
	EXPECT_EQ(Timeline.m_vLines[0].m_EndMs, 3000);
	EXPECT_EQ(Timeline.m_vLines[0].m_vWords[1].m_StartMs, 1500);
	EXPECT_EQ(Timeline.m_vLines[0].m_vWords[1].m_EndMs, 2000);
	EXPECT_EQ(Timeline.m_vLines[0].m_Text, "你好");
}

TEST(NeteaseLyricParser, AcceptsRelativeYrcWordOffsets)
{
	STimeline Timeline;
	ASSERT_TRUE(ParseYrc("[1000,3000](0,500)你(1500,500)好\n", &Timeline));
	ASSERT_EQ(Timeline.m_vLines[0].m_vWords.size(), 2u);
	EXPECT_EQ(Timeline.m_vLines[0].m_vWords[0].m_StartMs, 1000);
	EXPECT_EQ(Timeline.m_vLines[0].m_vWords[1].m_StartMs, 2500);
}

TEST(NeteaseLyricParser, YrcFallsBackToLrcAndDoesNotInventTiming)
{
	STimeline Timeline;
	SRawLyrics Raw;
	Raw.m_Yrc = "untimed";
	Raw.m_Lrc = "[00:02.00]fallback";
	ASSERT_TRUE(ParseRawLyrics(Raw, &Timeline));
	EXPECT_EQ(Timeline.m_vLines[0].m_StartMs, 2000);
	EXPECT_FALSE(ParseRawLyrics({"plain", ""}, &Timeline));
	EXPECT_FALSE(ParseYrc("[1000,2000](1000,bad,0)broken", &Timeline));
}

TEST(NeteaseLyricParser, TruncatesOnlyAtUtf8Boundary)
{
	EXPECT_EQ(TruncateUtf8("中文😀abc", 7), "中文");
	EXPECT_TRUE(IsValidUtf8(TruncateUtf8("中文😀abc", 7)));
	EXPECT_FALSE(IsValidUtf8(std::string("\xED\xA0\x80", 3)));
}

TEST(NeteaseLyricParser, KeepsVeryLongTimedLyricForAbiBoundaryTruncation)
{
	const std::string LongText(64 * 1024, 'a');
	STimeline Timeline;
	ASSERT_TRUE(ParseLrc("[00:01.00]" + LongText, &Timeline));
	ASSERT_EQ(Timeline.m_vLines.size(), 1u);
	EXPECT_EQ(Timeline.m_vLines[0].m_Text, LongText);
}
