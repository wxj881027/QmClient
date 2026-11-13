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

TEST(NeteaseLyricParser, KeepsOrdinaryParenthesesInsideTimedYrcWords)
{
	for(const char *pFirstWord : {"A(x)", "A(2026)", "A((x))", "A(", "你(轻声)"})
	{
		SCOPED_TRACE(pFirstWord);
		STimeline Timeline;
		ASSERT_TRUE(ParseYrc(std::string("[1000,1000](1000,500,0)") + pFirstWord + "(1500,500,0) B", &Timeline));
		ASSERT_EQ(Timeline.m_vLines.size(), 1u);
		const auto &Line = Timeline.m_vLines[0];
		EXPECT_EQ(Line.m_Text, std::string(pFirstWord) + " B");
		ASSERT_EQ(Line.m_vWords.size(), 2u);
		EXPECT_EQ(Line.m_vWords[0].m_Text, pFirstWord);
		EXPECT_EQ(Line.m_vWords[0].m_StartMs, 1000);
		EXPECT_EQ(Line.m_vWords[0].m_EndMs, 1500);
		EXPECT_EQ(Line.m_vWords[1].m_Text, " B");
		EXPECT_EQ(Line.m_vWords[1].m_StartMs, 1500);
		EXPECT_EQ(Line.m_vWords[1].m_EndMs, 2000);
	}
}

TEST(NeteaseLyricParser, KeepsZeroDurationPunctuationAndSpacesInYrcWords)
{
	STimeline Timeline;
	ASSERT_TRUE(ParseYrc("[1000,1000](1000,500,0)Hello(1500,0,0), (1500,500,0)world(2000,0,0)!", &Timeline));
	ASSERT_EQ(Timeline.m_vLines.size(), 1u);
	const auto &Line = Timeline.m_vLines[0];
	EXPECT_EQ(Line.m_Text, "Hello, world!");
	ASSERT_EQ(Line.m_vWords.size(), 4u);
	EXPECT_EQ(Line.m_vWords[0].m_Text, "Hello");
	EXPECT_EQ(Line.m_vWords[1].m_Text, ", ");
	EXPECT_EQ(Line.m_vWords[1].m_StartMs, 1500);
	EXPECT_EQ(Line.m_vWords[1].m_EndMs, 1500);
	EXPECT_EQ(Line.m_vWords[2].m_Text, "world");
	EXPECT_EQ(Line.m_vWords[3].m_Text, "!");
	EXPECT_EQ(Line.m_vWords[3].m_StartMs, 2000);
	EXPECT_EQ(Line.m_vWords[3].m_EndMs, 2000);
}

TEST(NeteaseLyricParser, MalformedYrcMarkersAfterAWordStillFallBackToLrc)
{
	for(const char *pMarker : {"(1500,bad,0)", "(1500,500,bad)", "(1500,500"})
	{
		SCOPED_TRACE(pMarker);
		STimeline Timeline;
		SRawLyrics Raw;
		Raw.m_Yrc = std::string("[1000,1000](1000,500,0)first") + pMarker + "broken";
		Raw.m_Lrc = "[00:01.00]fallback";
		EXPECT_FALSE(ParseYrc(Raw.m_Yrc, &Timeline));
		ASSERT_TRUE(ParseRawLyrics(Raw, &Timeline));
		ASSERT_EQ(Timeline.m_vLines.size(), 1u);
		EXPECT_EQ(Timeline.m_vLines[0].m_Text, "fallback");
		EXPECT_TRUE(Timeline.m_vLines[0].m_vWords.empty());
	}
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

TEST(NeteaseLyricParser, FinalizeUsesTheNextDistinctStartAndKeepsStableOrderAndExplicitEnds)
{
	std::vector<SLine> Lines{
		{3000, -1, "last first", {}},
		{1000, -1, "first", {{1000, 1100, "word"}}},
		{2000, 2200, "explicit middle", {}},
		{1000, 0, "second", {}},
		{1000, 9000, "explicit overlap", {}},
		{3000, 3000, "last second", {}},
	};
	FinalizeTimelineLines(Lines);
	ASSERT_EQ(Lines.size(), 6u);
	EXPECT_EQ(Lines[0].m_Text, "first");
	EXPECT_EQ(Lines[0].m_EndMs, 2000);
	ASSERT_EQ(Lines[0].m_vWords.size(), 1u);
	EXPECT_EQ(Lines[0].m_vWords[0].m_Text, "word");
	EXPECT_EQ(Lines[1].m_Text, "second");
	EXPECT_EQ(Lines[1].m_EndMs, 2000);
	EXPECT_EQ(Lines[2].m_Text, "explicit overlap");
	EXPECT_EQ(Lines[2].m_EndMs, 9000);
	EXPECT_EQ(Lines[3].m_Text, "explicit middle");
	EXPECT_EQ(Lines[3].m_EndMs, 2200);
	EXPECT_EQ(Lines[4].m_Text, "last first");
	EXPECT_EQ(Lines[4].m_EndMs, -1);
	EXPECT_EQ(Lines[5].m_Text, "last second");
	EXPECT_EQ(Lines[5].m_EndMs, 3000);
}

TEST(NeteaseLyricParser, RepeatedLrcTimestampsStillEndAtTheFilteredEmptyGroup)
{
	STimeline Timeline;
	ASSERT_TRUE(ParseLrc("[00:01]first\n[00:01]second\n[00:02]\n[00:03]third\n[00:03]fourth", &Timeline));
	ASSERT_EQ(Timeline.m_vLines.size(), 4u);
	EXPECT_EQ(Timeline.m_vLines[0].m_Text, "first");
	EXPECT_EQ(Timeline.m_vLines[0].m_EndMs, 2000);
	EXPECT_EQ(Timeline.m_vLines[1].m_Text, "second");
	EXPECT_EQ(Timeline.m_vLines[1].m_EndMs, 2000);
	EXPECT_EQ(Timeline.m_vLines[2].m_Text, "third");
	EXPECT_EQ(Timeline.m_vLines[2].m_EndMs, -1);
	EXPECT_EQ(Timeline.m_vLines[3].m_Text, "fourth");
	EXPECT_EQ(Timeline.m_vLines[3].m_EndMs, -1);
}
