#include <game/client/components/qmclient/music_lyrics/music_humming_state.h>

#include <gtest/gtest.h>

using namespace QmMusicLyrics;

namespace
{
	NeteaseLyrics::STimeline Song()
	{
		NeteaseLyrics::STimeline Timeline;
		NeteaseLyrics::ParseLrc("[00:01]one\n[00:02]two\n[00:03]three\n[00:04]four", &Timeline);
		return Timeline;
	}

	SHummingIdentity Identity()
	{
		return {1, 42, 7, 100, 0, 5};
	}

	CHummingState DueState()
	{
		CHummingState State;
		State.Synchronize(true, Identity());
		State.Schedule(0, 90000);
		return State;
	}
}

TEST(QmHummingState, EnablingWaitsBeforeFirstGroup)
{
	auto State = DueState();
	EXPECT_FALSE(State.GroupDue(89999));
	EXPECT_TRUE(State.GroupDue(90000));
}

TEST(QmHummingState, RandomChoiceSelectsAConsecutiveGroup)
{
	auto State = DueState();
	ASSERT_TRUE(State.BeginGroup(Song(), 1, 90000));
	ASSERT_NE(State.PendingLine(90000), nullptr);
	EXPECT_EQ(*State.PendingLine(90000), "♪ two");
	State.FinishLine(true, 90000, 180000);
	ASSERT_NE(State.PendingLine(93000), nullptr);
	EXPECT_EQ(*State.PendingLine(93000), "♪ three");
	State.FinishLine(true, 93000, 180000);
	ASSERT_NE(State.PendingLine(96000), nullptr);
	EXPECT_EQ(*State.PendingLine(96000), "♪ four");
}

TEST(QmHummingState, LinesWaitThreeSecondsAfterSuccessfulSend)
{
	auto State = DueState();
	ASSERT_TRUE(State.BeginGroup(Song(), 0, 90000));
	State.FinishLine(true, 90000, 180000);
	EXPECT_EQ(State.PendingLine(92999), nullptr);
	EXPECT_NE(State.PendingLine(93000), nullptr);
}

TEST(QmHummingState, LongFrameDoesNotBurstRemainingLines)
{
	auto State = DueState();
	ASSERT_TRUE(State.BeginGroup(Song(), 0, 90000));
	State.FinishLine(true, 90000, 180000);
	ASSERT_NE(State.PendingLine(150000), nullptr);
	State.FinishLine(true, 150000, 180000);
	EXPECT_EQ(State.PendingLine(150000), nullptr);
	EXPECT_NE(State.PendingLine(153000), nullptr);
}

TEST(QmHummingState, CompletedGroupWaitsForTheNextInterval)
{
	auto State = DueState();
	ASSERT_TRUE(State.BeginGroup(Song(), 0, 90000));
	State.FinishLine(true, 90000, 180000);
	State.FinishLine(true, 93000, 180000);
	State.FinishLine(true, 96000, 180000);
	EXPECT_EQ(State.PendingLine(276000), nullptr);
	EXPECT_FALSE(State.GroupDue(275999));
	EXPECT_TRUE(State.GroupDue(276000));
}

TEST(QmHummingState, FailedSendCancelsTheRestOfTheGroup)
{
	auto State = DueState();
	ASSERT_TRUE(State.BeginGroup(Song(), 0, 90000));
	State.FinishLine(false, 90000, 180000);
	EXPECT_EQ(State.PendingLine(93000), nullptr);
	EXPECT_FALSE(State.GroupDue(269999));
	EXPECT_TRUE(State.GroupDue(270000));
}

TEST(QmHummingState, DisableOrUnavailableSongCancelsPendingLines)
{
	auto State = DueState();
	ASSERT_TRUE(State.BeginGroup(Song(), 0, 90000));
	State.Synchronize(false, Identity());
	EXPECT_EQ(State.PendingLine(90000), nullptr);
	EXPECT_FALSE(State.GroupDue(270000));
	State.Synchronize(true, Identity());
	EXPECT_TRUE(State.NeedsSchedule());
	EXPECT_EQ(State.PendingLine(270000), nullptr);
}

TEST(QmHummingState, SourceSongGenerationProcessAndConnectionChangesCancelTheGroup)
{
	const SHummingIdentity Variants[] = {
		{2, 42, 7, 100, 0, 5}, {1, 43, 7, 100, 0, 5}, {1, 42, 8, 100, 0, 5},
		{1, 42, 7, 101, 0, 5}, {1, 42, 7, 100, 1, 5}, {1, 42, 7, 100, 0, 6}};
	for(size_t Index = 0; Index < std::size(Variants); ++Index)
	{
		SCOPED_TRACE(Index);
		auto State = DueState();
		ASSERT_TRUE(State.BeginGroup(Song(), 0, 90000));
		State.Synchronize(true, Variants[Index]);
		EXPECT_EQ(State.PendingLine(93000), nullptr);
		EXPECT_TRUE(State.NeedsSchedule());
	}
}

TEST(QmHummingState, RepeatedUpdatesOfTheSameSongPreservePendingLines)
{
	auto State = DueState();
	ASSERT_TRUE(State.BeginGroup(Song(), 0, 90000));
	State.Synchronize(true, Identity());
	EXPECT_FALSE(State.NeedsSchedule());
	EXPECT_NE(State.PendingLine(90000), nullptr);
}

TEST(QmHummingState, FewerThanThreeSungLinesCannotStartAGroup)
{
	auto State = DueState();
	auto Timeline = Song();
	Timeline.m_vLines.resize(2);
	EXPECT_FALSE(State.BeginGroup(Timeline, 0, 90000));
	EXPECT_EQ(State.PendingLine(90000), nullptr);
}

TEST(QmHummingState, EmptyLinesDoNotBreakTheSequenceOfSungLines)
{
	auto State = DueState();
	auto Timeline = Song();
	Timeline.m_vLines[1].m_Text = " \n ";
	ASSERT_TRUE(State.BeginGroup(Timeline, 0, 90000));
	EXPECT_EQ(*State.PendingLine(90000), "♪ one");
	State.FinishLine(true, 90000, 180000);
	EXPECT_EQ(*State.PendingLine(93000), "♪ three");
}

TEST(QmHummingState, CreditsAndColonLinesAreSkippedBeforeChoosingAGroup)
{
	NeteaseLyrics::STimeline Timeline;
	ASSERT_TRUE(NeteaseLyrics::ParseLrc(
		"[00:00]作词 张三\n[00:01]one\n[00:02]角色: 台词\n"
		"[00:03]two\n[00:04]编曲 / 李四\n[00:05]three\n"
		"[00:06]提示：内容\n[00:07]four\n[00:08]Produced by Someone",
		&Timeline));
	auto State = DueState();
	ASSERT_TRUE(State.BeginGroup(Timeline, 1, 90000));
	const char *apExpected[] = {"♪ two", "♪ three", "♪ four"};
	for(size_t Index = 0; Index < std::size(apExpected); ++Index)
	{
		SCOPED_TRACE(Index);
		const int64_t NowMs = 90000 + static_cast<int64_t>(Index) * 3000;
		ASSERT_NE(State.PendingLine(NowMs), nullptr);
		EXPECT_EQ(*State.PendingLine(NowMs), apExpected[Index]);
		State.FinishLine(true, NowMs, 180000);
	}
	EXPECT_EQ(State.PendingLine(99000), nullptr);
}

TEST(QmHummingState, FilteringBelowThreeSungLinesLeavesNoPendingMessage)
{
	NeteaseLyrics::STimeline Timeline;
	ASSERT_TRUE(NeteaseLyrics::ParseLrc(
		"[00:00]作词 张三\n[00:01]one\n[00:02]作曲：李四\n[00:03]two", &Timeline));
	auto State = DueState();
	EXPECT_FALSE(State.BeginGroup(Timeline, 0, 90000));
	EXPECT_EQ(State.PendingLine(90000), nullptr);
	State.Schedule(90000, 180000);
	EXPECT_FALSE(State.GroupDue(269999));
	ASSERT_TRUE(State.BeginGroup(Song(), 0, 270000));
	ASSERT_NE(State.PendingLine(270000), nullptr);
	EXPECT_EQ(*State.PendingLine(270000), "♪ one");
}

TEST(QmHummingState, GroupIntervalsStayWithinConfirmedRange)
{
	const std::pair<int, int> aIntervals[] = {
		{-1, 90000},
		{90000, 90000},
		{180000, 180000},
		{270000, 270000},
		{300000, 270000}};
	for(const auto [Requested, Expected] : aIntervals)
	{
		SCOPED_TRACE(Requested);
		auto State = DueState();
		State.Schedule(0, Requested);
		EXPECT_FALSE(State.GroupDue(Expected - 1));
		EXPECT_TRUE(State.GroupDue(Expected));
	}
}

TEST(QmHummingMessage, SlashAndControlCharactersStayInAnOrdinaryChatMessage)
{
	EXPECT_EQ(HummingMessage(" /command\nline\t "), "♪ /command line");
	EXPECT_TRUE(HummingMessage(" \t\r\n").empty());
}

TEST(QmHummingMessage, LongUtf8TextKeepsCompleteCharactersWithinMessageLimit)
{
	std::string Long;
	for(int Index = 0; Index < 200; ++Index)
		Long += "歌";
	const auto Message = HummingMessage(Long);
	EXPECT_LE(Message.size(), 255u);
	EXPECT_TRUE(NeteaseLyrics::IsValidUtf8(Message));
	EXPECT_EQ(Message.substr(0, 4), "♪ ");
}

TEST(QmHummingMessage, InvalidUtf8IsNotSent)
{
	EXPECT_TRUE(HummingMessage(std::string("\xff", 1)).empty());
}

TEST(QmHummingMessage, ColonAnywhereRejectsTheWholeLineBeforeTruncation)
{
	for(const char *pText : {"作词: 张三", "角色：歌词", ":", "：", "歌词里也有:冒号"})
	{
		SCOPED_TRACE(pText);
		EXPECT_TRUE(HummingMessage(pText).empty());
	}
	const std::string Long(300, 'a');
	EXPECT_TRUE(HummingMessage(Long + ": credit").empty());
	EXPECT_TRUE(HummingMessage(Long + "：署名").empty());
}

TEST(QmHummingMessage, CommonCreditsWithoutColonsAreNotSent)
{
	const char *apCredits[] = {
		"作词 张三", "作曲/李四", "编曲 - 王五", "词曲 张三", "填詞 張三",
		"制作人 王五", "監製 / 李四", "录音 张三", "混音 张三", "母帶 / 李四",
		"和声 李四", "吉他 王五", "演唱 张三", "发行 某公司",
		"作词", "\t【作詞】 張三", "　（編曲） 李四", "[作词] 张三",
		"Lyrics by Someone", "MUSIC BY Someone", "Produced by Someone", "Mixed by Someone"};
	for(const char *pText : apCredits)
	{
		SCOPED_TRACE(pText);
		EXPECT_TRUE(HummingMessage(pText).empty());
	}
}

TEST(QmHummingMessage, OrdinaryLyricsAndNonCreditBracketsArePreserved)
{
	for(const char *pText : {"我想为你作词", "录音机里的旧旋律", "吉他声还在回响", "和声伴着你的歌声", "Music brings us together", "[", "（", "【["})
	{
		SCOPED_TRACE(pText);
		EXPECT_EQ(HummingMessage(pText), std::string("♪ ") + pText);
	}
}
