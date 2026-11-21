#include <game/client/components/qmclient/rank_demo_manifest.h>
#include <game/client/components/qmclient/rank_demo_sort.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
	qmclient::rank_demo::SEntry MakeSearchEntry(const char *pMap, const char *pDemo, const char *pTime, int64_t Ts, int Rank = 1)
	{
		qmclient::rank_demo::SEntry Entry;
		Entry.m_Map = pMap;
		Entry.m_Rank = Rank;
		Entry.m_Demo = pDemo;
		Entry.m_Time = pTime;
		Entry.m_Ts = Ts;
		Entry.m_Kind = "solo";
		return Entry;
	}

	std::vector<qmclient::rank_demo::SEntry> Collect(const std::vector<qmclient::rank_demo::SEntry> &Entries, const char *pQuery, int Rank = 1)
	{
		std::vector<qmclient::rank_demo::SEntry> Out;
		qmclient::rank_demo::CollectSearchEntries(Entries, pQuery, Rank, Out);
		return Out;
	}
} // namespace

TEST(QmRankDemoSearch, MatchesMapSubstringCaseInsensitively)
{
	const std::vector<qmclient::rank_demo::SEntry> vEntries = {
		MakeSearchEntry("Just Fly 2", "just.demo", "60.0", 1),
		MakeSearchEntry("Teleport", "tele.demo", "70.0", 2),
		MakeSearchEntry("Fly Away", "away.demo", "80.0", 3),
	};
	const auto Matches = Collect(vEntries, "fLy");
	ASSERT_EQ(Matches.size(), 2U);
	EXPECT_EQ(Matches[0].m_Demo, "just.demo");
	EXPECT_EQ(Matches[1].m_Demo, "away.demo");
}

TEST(QmRankDemoSearch, FiltersByRankBeforeMatching)
{
	const std::vector<qmclient::rank_demo::SEntry> vEntries = {
		MakeSearchEntry("Teleport", "rank1.demo", "60.0", 1, 1),
		MakeSearchEntry("Teleport", "rank2.demo", "70.0", 2, 2),
	};
	ASSERT_EQ(Collect(vEntries, "teleport", 1).size(), 1U);
	EXPECT_EQ(Collect(vEntries, "teleport", 1)[0].m_Demo, "rank1.demo");
	ASSERT_EQ(Collect(vEntries, "teleport", 2).size(), 1U);
	EXPECT_EQ(Collect(vEntries, "teleport", 2)[0].m_Demo, "rank2.demo");
	EXPECT_TRUE(Collect(vEntries, "teleport", 3).empty());
}

TEST(QmRankDemoSearch, DeduplicatesTeamMemberRecordsSharingOneDemo)
{
	const std::vector<qmclient::rank_demo::SEntry> vEntries = {
		MakeSearchEntry("Teleport", "team.demo", "60.0", 1),
		MakeSearchEntry("Teleport", "team.demo", "60.0", 2),
		MakeSearchEntry("Teleport", "solo.demo", "70.0", 3),
	};
	const auto Matches = Collect(vEntries, "teleport");
	ASSERT_EQ(Matches.size(), 2U);
	EXPECT_EQ(Matches[0].m_Demo, "team.demo");
	EXPECT_EQ(Matches[1].m_Demo, "solo.demo");
}

TEST(QmRankDemoSearch, EmptyOrNullQueryMatchesNothing)
{
	const std::vector<qmclient::rank_demo::SEntry> vEntries = {
		MakeSearchEntry("Teleport", "tele.demo", "60.0", 1),
	};
	EXPECT_TRUE(Collect(vEntries, "").empty());
	EXPECT_TRUE(Collect(vEntries, nullptr).empty());
	EXPECT_TRUE(Collect({}, "teleport").empty());
}

TEST(QmRankDemoSearch, KeepsAllMatchesSoSortingCoversWholeManifest)
{
	// 搜索本身不截断：先收集全部匹配项，排序后才能得到全局最优顺序，
	// 而不是只对清单前若干条做局部排序。
	std::vector<qmclient::rank_demo::SEntry> vEntries;
	for(int Index = 0; Index < 200; Index++)
	{
		char aMap[32];
		str_format(aMap, sizeof(aMap), "Map %d", Index);
		char aDemo[32];
		str_format(aDemo, sizeof(aDemo), "demo%03d.demo", Index);
		char aTime[32];
		str_format(aTime, sizeof(aTime), "%d.0", 1000 - Index);
		vEntries.push_back(MakeSearchEntry(aMap, aDemo, aTime, Index));
	}
	auto Matches = Collect(vEntries, "map");
	ASSERT_EQ(Matches.size(), 200U);

	qmclient::rank_demo::SSortState State;
	State.m_Column = qmclient::rank_demo::ESortColumn::TIME;
	qmclient::rank_demo::SortEntries(Matches, State);
	ASSERT_EQ(Matches.size(), 200U);
	EXPECT_EQ(Matches.front().m_Demo, "demo199.demo");
	EXPECT_EQ(Matches.back().m_Demo, "demo000.demo");
}
