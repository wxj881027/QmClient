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
		State.Schedule(0, 30000);
		return State;
	}
}

TEST(QmHummingState, EnablingWaitsBeforeFirstGroup)
{
	auto State = DueState();
	EXPECT_FALSE(State.GroupDue(29999));
	EXPECT_TRUE(State.GroupDue(30000));
}

TEST(QmHummingState, RandomChoiceSelectsAConsecutiveGroup)
{
	auto State = DueState();
	ASSERT_TRUE(State.BeginGroup(Song(), 1, 30000));
	ASSERT_NE(State.PendingLine(30000), nullptr);
	EXPECT_EQ(*State.PendingLine(30000), "♪ two");
	State.FinishLine(true, 30000, 60000);
	ASSERT_NE(State.PendingLine(33000), nullptr);
	EXPECT_EQ(*State.PendingLine(33000), "♪ three");
	State.FinishLine(true, 33000, 60000);
	ASSERT_NE(State.PendingLine(36000), nullptr);
	EXPECT_EQ(*State.PendingLine(36000), "♪ four");
}

TEST(QmHummingState, LinesWaitThreeSecondsAfterSuccessfulSend)
{
	auto State = DueState();
	ASSERT_TRUE(State.BeginGroup(Song(), 0, 30000));
	State.FinishLine(true, 30000, 60000);
	EXPECT_EQ(State.PendingLine(32999), nullptr);
	EXPECT_NE(State.PendingLine(33000), nullptr);
}

TEST(QmHummingState, LongFrameDoesNotBurstRemainingLines)
{
	auto State = DueState();
	ASSERT_TRUE(State.BeginGroup(Song(), 0, 30000));
	State.FinishLine(true, 30000, 60000);
	ASSERT_NE(State.PendingLine(90000), nullptr);
	State.FinishLine(true, 90000, 60000);
	EXPECT_EQ(State.PendingLine(90000), nullptr);
	EXPECT_NE(State.PendingLine(93000), nullptr);
}

TEST(QmHummingState, CompletedGroupWaitsForTheNextInterval)
{
	auto State = DueState();
	ASSERT_TRUE(State.BeginGroup(Song(), 0, 30000));
	State.FinishLine(true, 30000, 60000);
	State.FinishLine(true, 33000, 60000);
	State.FinishLine(true, 36000, 60000);
	EXPECT_EQ(State.PendingLine(96000), nullptr);
	EXPECT_FALSE(State.GroupDue(95999));
	EXPECT_TRUE(State.GroupDue(96000));
}

TEST(QmHummingState, FailedSendCancelsTheRestOfTheGroup)
{
	auto State = DueState();
	ASSERT_TRUE(State.BeginGroup(Song(), 0, 30000));
	State.FinishLine(false, 30000, 60000);
	EXPECT_EQ(State.PendingLine(33000), nullptr);
	EXPECT_FALSE(State.GroupDue(89999));
	EXPECT_TRUE(State.GroupDue(90000));
}

TEST(QmHummingState, DisableOrUnavailableSongCancelsPendingLines)
{
	auto State = DueState();
	ASSERT_TRUE(State.BeginGroup(Song(), 0, 30000));
	State.Synchronize(false, Identity());
	EXPECT_EQ(State.PendingLine(30000), nullptr);
	EXPECT_FALSE(State.GroupDue(90000));
	State.Synchronize(true, Identity());
	EXPECT_TRUE(State.NeedsSchedule());
	EXPECT_EQ(State.PendingLine(90000), nullptr);
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
		ASSERT_TRUE(State.BeginGroup(Song(), 0, 30000));
		State.Synchronize(true, Variants[Index]);
		EXPECT_EQ(State.PendingLine(33000), nullptr);
		EXPECT_TRUE(State.NeedsSchedule());
	}
}

TEST(QmHummingState, RepeatedUpdatesOfTheSameSongPreservePendingLines)
{
	auto State = DueState();
	ASSERT_TRUE(State.BeginGroup(Song(), 0, 30000));
	State.Synchronize(true, Identity());
	EXPECT_FALSE(State.NeedsSchedule());
	EXPECT_NE(State.PendingLine(30000), nullptr);
}

TEST(QmHummingState, FewerThanThreeSungLinesCannotStartAGroup)
{
	auto State = DueState();
	auto Timeline = Song();
	Timeline.m_vLines.resize(2);
	EXPECT_FALSE(State.BeginGroup(Timeline, 0, 30000));
	EXPECT_EQ(State.PendingLine(30000), nullptr);
}

TEST(QmHummingState, EmptyLinesDoNotBreakTheSequenceOfSungLines)
{
	auto State = DueState();
	auto Timeline = Song();
	Timeline.m_vLines[1].m_Text = " \n ";
	ASSERT_TRUE(State.BeginGroup(Timeline, 0, 30000));
	EXPECT_EQ(*State.PendingLine(30000), "♪ one");
	State.FinishLine(true, 30000, 60000);
	EXPECT_EQ(*State.PendingLine(33000), "♪ three");
}

TEST(QmHummingState, GroupIntervalsStayWithinConfirmedRange)
{
	for(const auto [Requested, Expected] : {std::pair{-1, 30000}, std::pair{100000, 90000}})
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
