#include <game/client/components/qmclient/public_word_filter.h>

#include <engine/shared/protocol.h>

#include <gtest/gtest.h>

namespace
{
	std::string Response(const char *pWords)
	{
		return std::string(QmPublicWordFilter::RESPONSE_HEADER) + pWords;
	}
}

TEST(QmPublicWordFilter, OnlyIncomingPlayerChatIsEligible)
{
	EXPECT_TRUE(QmPublicWordFilter::IsIncomingPlayerChat(3, false, false, false));
	EXPECT_FALSE(QmPublicWordFilter::IsIncomingPlayerChat(-1, false, false, false));
	EXPECT_FALSE(QmPublicWordFilter::IsIncomingPlayerChat(-2, false, false, false));
	EXPECT_FALSE(QmPublicWordFilter::IsIncomingPlayerChat(MAX_CLIENTS, false, false, false));
	EXPECT_FALSE(QmPublicWordFilter::IsIncomingPlayerChat(3, true, false, false));
	EXPECT_FALSE(QmPublicWordFilter::IsIncomingPlayerChat(3, false, true, false));
	EXPECT_FALSE(QmPublicWordFilter::IsIncomingPlayerChat(3, false, false, true));
}

TEST(QmPublicWordFilter, MatchesLiteralWordsWithoutInterpretingRegexSyntax)
{
	QmPublicWordFilter::CState State;
	State.SetEnabled(true);
	ASSERT_TRUE(State.Restore("测试词\na.b\nSPAM"));
	std::string Text = "测试词 aXb a.b spam";
	EXPECT_TRUE(State.Filter(Text, "*", false));
	EXPECT_EQ(Text, "* aXb * *");
}

TEST(QmPublicWordFilter, KeepsUnmatchedTextAndReportsEachMatchedWordOnce)
{
	QmPublicWordFilter::CState State;
	State.SetEnabled(true);
	ASSERT_TRUE(State.Restore("bad\nbad\nother"));
	std::string Text = "bad bad, hello";
	std::vector<std::string> vMatched;
	EXPECT_TRUE(State.Filter(Text, "*", false, &vMatched));
	EXPECT_EQ(Text, "* *, hello");
	EXPECT_EQ(vMatched, (std::vector<std::string>{"bad"}));
	Text = "hello";
	EXPECT_FALSE(State.Filter(Text, "*", false));
	EXPECT_EQ(Text, "hello");
}

TEST(QmPublicWordFilter, MergesOverlappingWordsBeforeReplacingOriginalText)
{
	QmPublicWordFilter::CState State;
	State.SetEnabled(true);
	ASSERT_TRUE(State.Restore("bad\nbadword\nword\n*"));
	std::string Text = "badword";
	EXPECT_TRUE(State.Filter(Text, "*", false));
	EXPECT_EQ(Text, "*");
}

TEST(QmPublicWordFilter, RepeatsWholeReplacementCharactersByUnicodeWordLength)
{
	QmPublicWordFilter::CState State;
	State.SetEnabled(true);
	ASSERT_TRUE(State.Restore("测试"));
	std::string Text = "a测试b";
	EXPECT_TRUE(State.Filter(Text, "■", true));
	EXPECT_EQ(Text, "a■■b");
	Text = "测试";
	EXPECT_TRUE(State.Filter(Text, "", false));
	EXPECT_EQ(Text, "*");
}

TEST(QmPublicWordFilter, DisablingPreservesSnapshotButStopsFilteringAndRequests)
{
	QmPublicWordFilter::CState State;
	State.SetEnabled(true);
	ASSERT_TRUE(State.Restore("bad"));
	State.SetEnabled(false);
	std::string Text = "bad";
	EXPECT_FALSE(State.Filter(Text, "*", false));
	EXPECT_EQ(Text, "bad");
	EXPECT_EQ(State.StartRefresh(100), 0u);
	State.SetEnabled(true);
	EXPECT_TRUE(State.Filter(Text, "*", false));
	EXPECT_NE(State.StartRefresh(100), 0u);
}

TEST(QmPublicWordFilter, StartsOneRequestAndRefreshesAfterSuccessfulInterval)
{
	QmPublicWordFilter::CState State;
	State.SetEnabled(true);
	const uint64_t Request = State.StartRefresh(100);
	ASSERT_NE(Request, 0u);
	EXPECT_EQ(State.StartRefresh(101), 0u);
	ASSERT_TRUE(State.CompleteRefresh(Request, Response("bad"), 102));
	EXPECT_TRUE(State.HasSnapshot());
	EXPECT_FALSE(State.Refreshing());
	EXPECT_EQ(State.StartRefresh(102 + QmPublicWordFilter::REFRESH_SECONDS - 1), 0u);
	EXPECT_NE(State.StartRefresh(102 + QmPublicWordFilter::REFRESH_SECONDS), 0u);
}

TEST(QmPublicWordFilter, FailedDownloadKeepsCachedWordsAndRetriesAfterDelay)
{
	QmPublicWordFilter::CState State;
	State.SetEnabled(true);
	ASSERT_TRUE(State.Restore("cached"));
	const uint64_t Request = State.StartRefresh(100);
	ASSERT_FALSE(State.CompleteRefresh(Request, std::nullopt, 101));
	EXPECT_EQ(State.Words(), (std::vector<std::string>{"cached"}));
	EXPECT_FALSE(State.Error().empty());
	EXPECT_EQ(State.StartRefresh(101 + QmPublicWordFilter::RETRY_SECONDS - 1), 0u);
	const uint64_t Retry = State.StartRefresh(101 + QmPublicWordFilter::RETRY_SECONDS);
	ASSERT_NE(Retry, 0u);
	EXPECT_TRUE(State.CompleteRefresh(Retry, Response("updated"), 200));
	EXPECT_EQ(State.Words(), (std::vector<std::string>{"updated"}));
	EXPECT_TRUE(State.Error().empty());
}

TEST(QmPublicWordFilter, InvalidDownloadDoesNotPublishPartialWords)
{
	QmPublicWordFilter::CState State;
	State.SetEnabled(true);
	ASSERT_TRUE(State.Restore("cached"));
	const uint64_t Request = State.StartRefresh(100);
	EXPECT_FALSE(State.CompleteRefresh(Request, Response("new\n\"unclosed"), 101));
	EXPECT_EQ(State.Words(), (std::vector<std::string>{"cached"}));
	EXPECT_FALSE(State.Error().empty());
}

TEST(QmPublicWordFilter, RejectsOversizedOrInvalidUtf8Snapshots)
{
	QmPublicWordFilter::CState State;
	ASSERT_TRUE(State.Restore("cached"));
	EXPECT_FALSE(State.Restore(std::string(QmWordFilterList::MAX_FILE_BYTES + 1, 'a')));
	EXPECT_FALSE(State.Restore(std::string("\xff")));
	EXPECT_EQ(State.Words(), (std::vector<std::string>{"cached"}));
}

TEST(QmPublicWordFilter, ReenableRejectsResultsFromCancelledGeneration)
{
	QmPublicWordFilter::CState State;
	State.SetEnabled(true);
	ASSERT_TRUE(State.Restore("cached"));
	const uint64_t Cancelled = State.StartRefresh(100);
	State.SetEnabled(false);
	State.SetEnabled(true);
	const uint64_t Current = State.StartRefresh(101);
	ASSERT_NE(Current, 0u);
	EXPECT_FALSE(State.CompleteRefresh(Cancelled, Response("stale"), 102));
	EXPECT_TRUE(State.Refreshing());
	EXPECT_EQ(State.Words(), (std::vector<std::string>{"cached"}));
	EXPECT_TRUE(State.CompleteRefresh(Current, Response("current"), 103));
	EXPECT_EQ(State.Words(), (std::vector<std::string>{"current"}));
}

TEST(QmPublicWordFilter, EmptyPublishedSnapshotClearsPreviousWords)
{
	QmPublicWordFilter::CState State;
	State.SetEnabled(true);
	ASSERT_TRUE(State.Restore("cached"));
	const uint64_t Request = State.StartRefresh(100);
	ASSERT_TRUE(State.CompleteRefresh(Request, Response("# no public words\n"), 101));
	EXPECT_TRUE(State.HasSnapshot());
	EXPECT_TRUE(State.Words().empty());
	std::string Text = "cached";
	EXPECT_FALSE(State.Filter(Text, "*", false));
}

TEST(QmPublicWordFilter, RejectsHtmlOrUnidentifiedHttpSuccessWithoutReplacingCache)
{
	QmPublicWordFilter::CState State;
	State.SetEnabled(true);
	ASSERT_TRUE(State.Restore("cached"));
	const uint64_t Request = State.StartRefresh(100);
	EXPECT_FALSE(State.CompleteRefresh(Request, "<html>fallback page</html>", 101));
	EXPECT_EQ(State.Words(), (std::vector<std::string>{"cached"}));
	EXPECT_FALSE(State.Error().empty());
}
