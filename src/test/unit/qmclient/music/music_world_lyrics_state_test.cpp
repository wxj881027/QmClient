#include <game/client/components/qmclient/music_lyrics/music_world_lyrics_state.h>

#include <gtest/gtest.h>

using namespace QmMusicLyrics;

namespace
{
	SSourceSnapshot Source(int64_t Start = 1000, std::string_view Text = "first")
	{
		return {42, 1, 100, true, nullptr, Text, "译文", Start};
	}
}

TEST(QmWorldLyrics, MovingThroughTheMapDoesNotMoveAnExistingAnchor)
{
	CWorldLyricsState State;
	State.Update(true, 1, Source(), 0, vec2(100, 200), 0.0);
	State.Update(true, 1, Source(), 0, vec2(800, 600), 1.0);
	EXPECT_EQ(State.Current().m_Anchor, vec2(100, 200));
	EXPECT_DOUBLE_EQ(State.Current().m_CreatedAt, 0.0);
	const auto Visual = WorldLyricVisual(State.Current(), 1.0);
	EXPECT_FLOAT_EQ(Visual.m_Position.x, 100.0f);
	EXPECT_NEAR(Visual.m_Position.y, 200.0f, 3.0f);
	EXPECT_FLOAT_EQ(Visual.m_Alpha, 1.0f);
}

TEST(QmWorldLyrics, NewLineUsesTheNewSceneWhilePreviousLineRisesAndFades)
{
	CWorldLyricsState State;
	State.Update(true, 1, Source(), 0, vec2(100, 200), 0.0);
	State.Update(true, 1, Source(2000, "second"), 0, vec2(500, 600), 1.0);
	EXPECT_EQ(State.Current().m_Anchor, vec2(500, 600));
	EXPECT_EQ(State.Previous().m_Anchor, vec2(100, 200));
	EXPECT_FLOAT_EQ(WorldLyricVisual(State.Current(), 1.0).m_Alpha, 0.0f);
	const auto Fading = WorldLyricVisual(State.Previous(), 1.6);
	EXPECT_GT(Fading.m_Alpha, 0.0f);
	EXPECT_LT(Fading.m_Alpha, 1.0f);
	EXPECT_LT(Fading.m_Position.y, 190.0f);
	State.Update(true, 1, Source(2000, "second"), 0, vec2(900, 900), 2.3);
	EXPECT_FALSE(State.Previous().m_Active);
	EXPECT_EQ(State.Current().m_Anchor, vec2(500, 600));
}

TEST(QmWorldLyrics, RepeatedTextAtAnotherTimestampCreatesANewLine)
{
	CWorldLyricsState State;
	State.Update(true, 1, Source(), 0, vec2(100, 200), 0.0);
	State.Update(true, 1, Source(5000), 0, vec2(500, 600), 1.0);
	EXPECT_EQ(State.Current().m_Anchor, vec2(500, 600));
	EXPECT_EQ(State.Previous().m_LineStartMs, 1000);
	EXPECT_EQ(State.Current().m_LineStartMs, 5000);
}

TEST(QmWorldLyrics, ChangingLanguageOrReceivingTranslationKeepsTheAnchor)
{
	CWorldLyricsState State;
	State.Update(true, 1, Source(), 0, vec2(100, 200), 0.0);
	auto Updated = Source();
	Updated.m_Translation = "新的译文";
	State.Update(true, 1, Updated, 2, vec2(500, 600), 1.0);
	EXPECT_EQ(State.Current().m_Anchor, vec2(100, 200));
	EXPECT_EQ(State.Current().m_RowCount, 2);
	EXPECT_EQ(State.Current().m_aRows[0], "first");
	EXPECT_EQ(State.Current().m_aRows[1], "新的译文");
	EXPECT_FALSE(State.Previous().m_Active);
}

TEST(QmWorldLyrics, UnavailablePlaybackClearsBothLinesAndResumeUsesCurrentScene)
{
	CWorldLyricsState State;
	State.Update(true, 1, Source(), 0, vec2(100, 200), 0.0);
	State.Update(true, 1, Source(2000, "second"), 0, vec2(500, 600), 1.0);
	State.Update(false, 1, Source(), 0, vec2(0, 0), 1.5);
	EXPECT_FALSE(State.Current().m_Active);
	EXPECT_FALSE(State.Previous().m_Active);
	State.Update(true, 1, Source(), 0, vec2(700, 800), 2.0);
	EXPECT_EQ(State.Current().m_Anchor, vec2(700, 800));
}

TEST(QmWorldLyrics, InstrumentalGapFadesTheLastLineWithoutSpawningEmptyText)
{
	CWorldLyricsState State;
	State.Update(true, 1, Source(), 0, vec2(100, 200), 0.0);
	State.Update(true, 1, Source(2000, ""), 0, vec2(500, 600), 1.0);
	EXPECT_FALSE(State.Current().m_Active);
	EXPECT_TRUE(State.Previous().m_Active);
	State.Update(true, 1, Source(2000, ""), 0, vec2(500, 600), 2.3);
	EXPECT_FALSE(State.Previous().m_Active);
}

TEST(QmWorldLyrics, SourceIdentityChangesDiscardOldLyrics)
{
	for(int Field = 0; Field < 4; ++Field)
	{
		SCOPED_TRACE(Field);
		CWorldLyricsState State;
		State.Update(true, 1, Source(), 0, vec2(100, 200), 0.0);
		auto Changed = Source();
		if(Field == 1)
			++Changed.m_SongId;
		else if(Field == 2)
			++Changed.m_Generation;
		else if(Field == 3)
			++Changed.m_ProducerPid;
		State.Update(true, Field == 0 ? 2 : 1, Changed, 0, vec2(500, 600), 1.0);
		EXPECT_FALSE(State.Previous().m_Active);
		EXPECT_EQ(State.Current().m_Anchor, vec2(500, 600));
	}
}

TEST(QmWorldLyrics, RapidChangesKeepOnlyCurrentAndPreviousLine)
{
	CWorldLyricsState State;
	State.Update(true, 1, Source(), 0, vec2(100, 200), 0.0);
	State.Update(true, 1, Source(2000, "second"), 0, vec2(500, 600), 0.1);
	State.Update(true, 1, Source(3000, "third"), 0, vec2(700, 800), 0.2);
	EXPECT_EQ(State.Current().m_Original, "third");
	EXPECT_EQ(State.Previous().m_Original, "second");
	EXPECT_NEAR(WorldLyricVisual(State.Previous(), 0.2).m_Alpha, 0.4f, 0.001f);
}

TEST(QmWorldLyrics, ResetDropsAnchorsBeforeEnteringAnotherMap)
{
	CWorldLyricsState State;
	State.Update(true, 1, Source(), 0, vec2(100, 200), 0.0);
	State.Reset();
	EXPECT_FALSE(State.Current().m_Active);
	EXPECT_FALSE(State.Previous().m_Active);
	State.Update(true, 1, Source(), 0, vec2(500, 600), 1.0);
	EXPECT_EQ(State.Current().m_Anchor, vec2(500, 600));
}

TEST(QmWorldLyrics, SnapshotTextIsOwnedAndLongUtf8RowsStayBounded)
{
	CWorldLyricsState State;
	std::string Text;
	for(int Index = 0; Index < 600; ++Index)
		Text += "歌";
	State.Update(true, 1, Source(1000, Text), 0, vec2(100, 200), 0.0);
	Text.clear();
	EXPECT_FALSE(State.Current().m_aRows[0].empty());
	EXPECT_LE(State.Current().m_aRows[0].size(), 1023u);
	EXPECT_TRUE(NeteaseLyrics::IsValidUtf8(State.Current().m_aRows[0]));
}

TEST(QmWorldLyrics, ClockRewindRestartsAppearanceWithoutKeepingOldFade)
{
	CWorldLyricsState State;
	State.Update(true, 1, Source(), 0, vec2(100, 200), 5.0);
	State.Update(true, 1, Source(), 0, vec2(500, 600), 2.0);
	EXPECT_FALSE(State.Previous().m_Active);
	EXPECT_EQ(State.Current().m_Anchor, vec2(500, 600));
	EXPECT_FLOAT_EQ(WorldLyricVisual(State.Current(), 2.0).m_Alpha, 0.0f);
}

TEST(QmWorldLyrics, CreditLinesAreSkippedAndTheNextLyricUsesItsOwnScene)
{
	CWorldLyricsState State;
	State.Update(true, 1, Source(1000, "作曲：Tomas"), 2, vec2(100, 200), 0.0);
	EXPECT_FALSE(State.Current().m_Active);
	State.Update(true, 1, Source(2000, "歌词正文"), 2, vec2(500, 600), 1.0);
	EXPECT_EQ(State.Current().m_Anchor, vec2(500, 600));
	EXPECT_EQ(State.Current().m_aRows[0], "歌词正文");
	EXPECT_FALSE(State.Previous().m_Active);
}

TEST(QmWorldLyrics, AFollowingCreditFadesThePreviousLyricWithoutRenderingCredits)
{
	CWorldLyricsState State;
	State.Update(true, 1, Source(), 0, vec2(100, 200), 0.0);
	State.Update(true, 1, Source(2000, "编曲 李四"), 0, vec2(500, 600), 1.0);
	EXPECT_FALSE(State.Current().m_Active);
	EXPECT_EQ(State.Previous().m_Original, "first");
	State.Update(true, 1, Source(2000, "编曲 李四"), 0, vec2(500, 600), 2.3);
	EXPECT_FALSE(State.Previous().m_Active);
}

TEST(QmWorldLyrics, TypewriterCountsUtf8CharactersAndKeepsTheCompleteLayoutText)
{
	CWorldLyricsState State;
	State.Update(true, 1, Source(1000, "你好吗"), 0, vec2(100, 200), 0.0);
	EXPECT_EQ(State.Current().m_aCharacterCounts[0], 3);
	EXPECT_EQ(WorldLyricVisibleCharacters(State.Current(), 0, 0.0), 0);
	EXPECT_EQ(WorldLyricVisibleCharacters(State.Current(), 0, 0.02), 1);
	EXPECT_EQ(WorldLyricVisibleCharacters(State.Current(), 0, 0.07), 2);
	EXPECT_EQ(WorldLyricVisibleCharacters(State.Current(), 0, 0.2), 3);
	EXPECT_EQ(State.Current().m_aRows[0], "你好吗");
}

TEST(QmWorldLyrics, LongLinesFinishRevealingWithinOneAndAHalfSeconds)
{
	CWorldLyricsState State;
	const std::string Text(200, 'a');
	State.Update(true, 1, Source(1000, Text), 0, vec2(100, 200), 0.0);
	EXPECT_LT(WorldLyricVisibleCharacters(State.Current(), 0, 0.7), 200);
	EXPECT_EQ(WorldLyricVisibleCharacters(State.Current(), 0, 1.5), 200);
}

TEST(QmWorldLyrics, FadingPreviousLineFreezesItsTypewriterPosition)
{
	CWorldLyricsState State;
	State.Update(true, 1, Source(1000, "abcdefghij"), 0, vec2(100, 200), 0.0);
	const int Visible = WorldLyricVisibleCharacters(State.Current(), 0, 0.1);
	State.Update(true, 1, Source(2000, "second"), 0, vec2(500, 600), 0.1);
	EXPECT_EQ(WorldLyricVisibleCharacters(State.Previous(), 0, 0.8), Visible);
	EXPECT_EQ(WorldLyricVisibleCharacters(State.Current(), 0, 0.1), 0);
}
