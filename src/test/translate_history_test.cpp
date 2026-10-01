#include <game/client/components/qmclient/translate/translate_jobs.h>

#include <gtest/gtest.h>

#include <array>

TEST(TranslateHistory, ZeroIndexIsValidAndNewestCandidateWins)
{
	EXPECT_EQ(SelectTranslateHistoryLine(0, 4, [](int) { return 0; }), 0);
	EXPECT_EQ(SelectTranslateHistoryLine(0, 4, [](int Index) { return Index == 0 ? -1 : 0; }), 3);
}

TEST(TranslateHistory, SearchTraversesFullRingInReverseOrder)
{
	std::vector<int> Visited;
	EXPECT_EQ(SelectTranslateHistoryLine(1, 4, [&](int Index) { Visited.push_back(Index); return -1; }), -1);
	EXPECT_EQ(Visited, (std::vector<int>{1, 0, 3, 2}));
}

TEST(TranslateHistory, ExactNameWinsOverNewerCaseInsensitiveMatch)
{
	const char *apNames[] = {"alice", "Alice", "Bob"};
	EXPECT_EQ(SelectTranslateHistoryLine(0, 3, [&](int Index) { return TranslateNameMatchScore("Alice", apNames[Index], nullptr); }), 1);
	EXPECT_EQ(TranslateNameMatchScore("Bob", "title Bob", "Bob"), 2);
	EXPECT_EQ(TranslateNameMatchScore("Bob", "title", "bob"), 1);
}

TEST(TranslateHistory, LocalDummyServerAndUninitializedLinesAreExcluded)
{
	const int aLocalIds[] = {2, 5, -1};
	for(int Id : {2, 5, -1, -2})
		EXPECT_FALSE(IsTranslatePlayerCandidate(Id, true, true, false, aLocalIds, 3));
	EXPECT_TRUE(IsTranslatePlayerCandidate(3, true, true, false, aLocalIds, 3));
	EXPECT_FALSE(IsTranslatePlayerCandidate(3, false, true, false, aLocalIds, 3));
	EXPECT_FALSE(IsTranslatePlayerCandidate(3, true, false, false, aLocalIds, 3));
	EXPECT_FALSE(IsTranslatePlayerCandidate(3, true, true, true, aLocalIds, 3));
}

TEST(TranslateHistory, InvalidHistoryBoundsDoNotReadCandidates)
{
	int Calls = 0;
	auto Score = [&](int) { ++Calls; return 0; };
	EXPECT_EQ(SelectTranslateHistoryLine(-1, 4, Score), -1);
	EXPECT_EQ(SelectTranslateHistoryLine(4, 4, Score), -1);
	EXPECT_EQ(SelectTranslateHistoryLine(0, 0, Score), -1);
	EXPECT_EQ(Calls, 0);
}

TEST(TranslateHistory, EmptyAndUnmatchedHistoryReturnNoLine)
{
	EXPECT_EQ(SelectTranslateHistoryLine(0, 4, [](int) { return -1; }), -1);
	EXPECT_EQ(TranslateNameMatchScore("unknown", "Alice", "Bob"), -1);
	EXPECT_EQ(TranslateNameMatchScore(nullptr, "Alice", "Bob"), 0);
}
