#include <game/client/components/qmclient/music_lyrics/music_singing_state.h>

#include <gtest/gtest.h>

#include <iterator>

using namespace QmMusicLyrics;

namespace
{
	SHummingIdentity Identity()
	{
		return {1, 42, 7, 100, 0, 5};
	}

	CSingingState AvailableState()
	{
		CSingingState State;
		State.Synchronize(true, Identity());
		return State;
	}
}

TEST(QmSingingState, DoublePressSendsOnlyTheLyricAtTheSecondPress)
{
	auto State = AvailableState();
	EXPECT_TRUE(State.OnStroke(true, 1000, "first line").empty());
	EXPECT_TRUE(State.OnStroke(false, 1050, "first line").empty());
	EXPECT_EQ(State.OnStroke(true, 1200, "current line"), "♪ current line");
	EXPECT_TRUE(State.OnStroke(false, 1250, "current line").empty());
}

TEST(QmSingingState, HoldingTheKeyDoesNotCountAsAnotherPress)
{
	auto State = AvailableState();
	EXPECT_TRUE(State.OnStroke(true, 1000, "line").empty());
	EXPECT_TRUE(State.OnStroke(true, 1100, "line").empty());
	EXPECT_TRUE(State.OnStroke(true, 1300, "line").empty());
	EXPECT_TRUE(State.OnStroke(true, 2000, "line").empty());
	State.OnStroke(false, 2100, {});
	EXPECT_TRUE(State.OnStroke(true, 2200, "line").empty());
	State.OnStroke(false, 2250, {});
	EXPECT_EQ(State.OnStroke(true, 2300, "line"), "♪ line");
}

TEST(QmSingingState, FourHundredMillisecondsIsTheInclusiveDoublePressLimit)
{
	for(const int DelayMs : {399, 400, 401})
	{
		SCOPED_TRACE(DelayMs);
		auto State = AvailableState();
		State.OnStroke(true, 1000, "line");
		State.OnStroke(false, 1050, {});
		EXPECT_EQ(State.OnStroke(true, 1000 + DelayMs, "line"), DelayMs <= 400 ? "♪ line" : "");
	}
}

TEST(QmSingingState, ExpiredPressStartsANewPair)
{
	auto State = AvailableState();
	State.OnStroke(true, 1000, "line");
	State.OnStroke(false, 1050, {});
	EXPECT_TRUE(State.OnStroke(true, 1500, "line").empty());
	State.OnStroke(false, 1550, {});
	EXPECT_EQ(State.OnStroke(true, 1700, "new line"), "♪ new line");
}

TEST(QmSingingState, EachPairIsConsumedBeforeAnotherMessageCanBeSent)
{
	auto State = AvailableState();
	State.OnStroke(true, 1000, "one");
	State.OnStroke(false, 1050, {});
	EXPECT_EQ(State.OnStroke(true, 1100, "one"), "♪ one");
	State.OnStroke(false, 1150, {});
	EXPECT_TRUE(State.OnStroke(true, 1200, "two").empty());
	State.OnStroke(false, 1250, {});
	EXPECT_EQ(State.OnStroke(true, 1300, "two"), "♪ two");
}

TEST(QmSingingState, UnavailableStateIgnoresPresses)
{
	CSingingState State;
	EXPECT_TRUE(State.OnStroke(true, 1000, "line").empty());
	State.OnStroke(false, 1050, {});
	EXPECT_TRUE(State.OnStroke(true, 1100, "line").empty());
}

TEST(QmSingingState, LosingAvailabilityCancelsTheFirstPress)
{
	auto State = AvailableState();
	State.OnStroke(true, 1000, "old line");
	State.OnStroke(false, 1050, {});
	State.Synchronize(false, Identity());
	State.Synchronize(true, Identity());
	EXPECT_TRUE(State.OnStroke(true, 1100, "new line").empty());
	State.OnStroke(false, 1150, {});
	EXPECT_EQ(State.OnStroke(true, 1200, "new line"), "♪ new line");
}

TEST(QmSingingState, RepeatedUpdatesPreserveTheFirstPress)
{
	auto State = AvailableState();
	State.OnStroke(true, 1000, "line");
	State.Synchronize(true, Identity());
	State.OnStroke(false, 1050, {});
	State.Synchronize(true, Identity());
	EXPECT_EQ(State.OnStroke(true, 1100, "line"), "♪ line");
}

TEST(QmSingingState, SourceSongGenerationProcessAndConnectionChangesCancelTheFirstPress)
{
	const SHummingIdentity Variants[] = {
		{2, 42, 7, 100, 0, 5}, {1, 43, 7, 100, 0, 5}, {1, 42, 8, 100, 0, 5},
		{1, 42, 7, 101, 0, 5}, {1, 42, 7, 100, 1, 5}, {1, 42, 7, 100, 0, 6}};
	for(size_t Index = 0; Index < std::size(Variants); ++Index)
	{
		SCOPED_TRACE(Index);
		auto State = AvailableState();
		State.OnStroke(true, 1000, "old line");
		State.OnStroke(false, 1050, {});
		State.Synchronize(true, Variants[Index]);
		EXPECT_TRUE(State.OnStroke(true, 1100, "new line").empty());
		State.OnStroke(false, 1150, {});
		EXPECT_EQ(State.OnStroke(true, 1200, "new line"), "♪ new line");
	}
}

TEST(QmSingingState, ResetRequiresAFreshDoublePress)
{
	auto State = AvailableState();
	State.OnStroke(true, 1000, "line");
	State.Reset();
	State.Synchronize(true, Identity());
	EXPECT_TRUE(State.OnStroke(true, 1100, "line").empty());
	State.OnStroke(false, 1150, {});
	EXPECT_EQ(State.OnStroke(true, 1200, "line"), "♪ line");
}

TEST(QmSingingState, EmptyCurrentLyricConsumesThePairWithoutSavingAnOlderLine)
{
	auto State = AvailableState();
	State.OnStroke(true, 1000, "previous line");
	State.OnStroke(false, 1050, {});
	EXPECT_TRUE(State.OnStroke(true, 1100, {}).empty());
	State.OnStroke(false, 1150, {});
	EXPECT_TRUE(State.OnStroke(true, 1200, "next line").empty());
	State.OnStroke(false, 1250, {});
	EXPECT_EQ(State.OnStroke(true, 1300, "next line"), "♪ next line");
}

TEST(QmSingingState, ClockMovingBackwardsStartsANewPair)
{
	auto State = AvailableState();
	State.OnStroke(true, 1000, "line");
	State.OnStroke(false, 1050, {});
	EXPECT_TRUE(State.OnStroke(true, 900, "line").empty());
	State.OnStroke(false, 950, {});
	EXPECT_EQ(State.OnStroke(true, 1100, "line"), "♪ line");
}

TEST(QmSingingMessage, CurrentLyricPunctuationIsPreserved)
{
	EXPECT_EQ(LyricChatMessage("角色：一句歌词"), "♪ 角色：一句歌词");
	EXPECT_EQ(LyricChatMessage("A: B"), "♪ A: B");
}

TEST(QmSingingMessage, SlashAndControlCharactersRemainOrdinaryChatText)
{
	EXPECT_EQ(LyricChatMessage(" /command\nline\t "), "♪ /command line");
	EXPECT_TRUE(LyricChatMessage(" \t\r\n").empty());
	EXPECT_TRUE(LyricChatMessage(std::string("\xff", 1)).empty());
}

TEST(QmSingingMessage, LongUtf8TextStaysWithinTheMessageLimit)
{
	std::string Long;
	for(int Index = 0; Index < 200; ++Index)
		Long += "歌";
	const std::string Message = LyricChatMessage(Long);
	EXPECT_LE(Message.size(), 255u);
	EXPECT_TRUE(NeteaseLyrics::IsValidUtf8(Message));
	EXPECT_EQ(Message.substr(0, 4), "♪ ");
}
