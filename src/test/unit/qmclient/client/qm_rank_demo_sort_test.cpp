#include <game/client/components/qmclient/rank_demo_manifest.h>
#include <game/client/components/qmclient/rank_demo_sort.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{
	qmclient::rank_demo::SEntry MakeEntry(const char *pDemo, const char *pNames, const char *pTime, int64_t Ts, const char *pKind = "solo")
	{
		qmclient::rank_demo::SEntry Entry;
		Entry.m_Map = "Map";
		Entry.m_Rank = 1;
		Entry.m_Demo = pDemo;
		Entry.m_Names = pNames;
		Entry.m_Time = pTime;
		Entry.m_Ts = Ts;
		Entry.m_Kind = pKind;
		return Entry;
	}

	std::vector<std::string> DemoOrder(const std::vector<qmclient::rank_demo::SEntry> &Entries)
	{
		std::vector<std::string> Demos;
		for(const auto &Entry : Entries)
			Demos.push_back(Entry.m_Demo);
		return Demos;
	}
} // namespace

TEST(QmRankDemoSort, ToggleFlipsSameColumnAndStartsAscendingOnNewColumn)
{
	qmclient::rank_demo::SSortState State;
	EXPECT_EQ(State.m_Column, qmclient::rank_demo::ESortColumn::NONE);

	ASSERT_TRUE(qmclient::rank_demo::ToggleSort(State, qmclient::rank_demo::ESortColumn::TIME));
	EXPECT_EQ(State.m_Column, qmclient::rank_demo::ESortColumn::TIME);
	EXPECT_FALSE(State.m_Descending);

	ASSERT_TRUE(qmclient::rank_demo::ToggleSort(State, qmclient::rank_demo::ESortColumn::TIME));
	EXPECT_TRUE(State.m_Descending);

	ASSERT_TRUE(qmclient::rank_demo::ToggleSort(State, qmclient::rank_demo::ESortColumn::DATE));
	EXPECT_EQ(State.m_Column, qmclient::rank_demo::ESortColumn::DATE);
	EXPECT_FALSE(State.m_Descending);
}

TEST(QmRankDemoSort, NoneColumnIgnoresClicks)
{
	qmclient::rank_demo::SSortState State;
	State.m_Column = qmclient::rank_demo::ESortColumn::PLAYERS;
	State.m_Descending = true;

	EXPECT_FALSE(qmclient::rank_demo::ToggleSort(State, qmclient::rank_demo::ESortColumn::NONE));
	EXPECT_EQ(State.m_Column, qmclient::rank_demo::ESortColumn::PLAYERS);
	EXPECT_TRUE(State.m_Descending);
}

TEST(QmRankDemoSort, NoneStateKeepsCollectionOrder)
{
	std::vector<qmclient::rank_demo::SEntry> vEntries = {
		MakeEntry("b.demo", "Bravo", "90.0", 20),
		MakeEntry("a.demo", "Alpha", "120.0", 10),
	};
	const qmclient::rank_demo::SSortState State;
	qmclient::rank_demo::SortEntries(vEntries, State);
	EXPECT_EQ(DemoOrder(vEntries), (std::vector<std::string>{"b.demo", "a.demo"}));
}

TEST(QmRankDemoSort, ModeSortPutsSoloFirstThenTeam)
{
	std::vector<qmclient::rank_demo::SEntry> vEntries = {
		MakeEntry("team1.demo", "t1", "60.0", 30, "team"),
		MakeEntry("solo1.demo", "s1", "70.0", 20, "solo"),
		MakeEntry("team0.demo", "t0", "50.0", 10, "team"),
		MakeEntry("solo0.demo", "s0", "80.0", 40, "solo"),
	};
	qmclient::rank_demo::SSortState State;
	State.m_Column = qmclient::rank_demo::ESortColumn::MODE;
	qmclient::rank_demo::SortEntries(vEntries, State);

	const auto Demos = DemoOrder(vEntries);
	ASSERT_EQ(Demos.size(), 4U);
	EXPECT_EQ(Demos[0], "solo0.demo");
	EXPECT_EQ(Demos[1], "solo1.demo");
	EXPECT_EQ(Demos[2], "team0.demo");
	EXPECT_EQ(Demos[3], "team1.demo");
}

TEST(QmRankDemoSort, PlayersSortIsCaseInsensitiveAndReversible)
{
	std::vector<qmclient::rank_demo::SEntry> vEntries = {
		MakeEntry("c.demo", "charlie", "60.0", 1),
		MakeEntry("a.demo", "Alpha", "70.0", 2),
		MakeEntry("b.demo", "bravo", "80.0", 3),
	};
	qmclient::rank_demo::SSortState State;
	State.m_Column = qmclient::rank_demo::ESortColumn::PLAYERS;
	qmclient::rank_demo::SortEntries(vEntries, State);
	EXPECT_EQ(DemoOrder(vEntries), (std::vector<std::string>{"a.demo", "b.demo", "c.demo"}));

	State.m_Descending = true;
	qmclient::rank_demo::SortEntries(vEntries, State);
	EXPECT_EQ(DemoOrder(vEntries), (std::vector<std::string>{"c.demo", "b.demo", "a.demo"}));
}

TEST(QmRankDemoSort, TimeSortComparesNumericNotLexicographic)
{
	// 字符串序会把 "100.0" 排在 "90.0" 前面，数值序必须反过来。
	std::vector<qmclient::rank_demo::SEntry> vEntries = {
		MakeEntry("long.demo", "long", "100.0", 1),
		MakeEntry("short.demo", "short", "90.0", 2),
	};
	qmclient::rank_demo::SSortState State;
	State.m_Column = qmclient::rank_demo::ESortColumn::TIME;
	qmclient::rank_demo::SortEntries(vEntries, State);
	EXPECT_EQ(DemoOrder(vEntries), (std::vector<std::string>{"short.demo", "long.demo"}));
}

TEST(QmRankDemoSort, DateSortUsesTimestampAndBreaksTiesByDemoName)
{
	std::vector<qmclient::rank_demo::SEntry> vEntries = {
		MakeEntry("z.demo", "z", "10.0", 200),
		MakeEntry("b.demo", "b", "30.0", 100),
		MakeEntry("a.demo", "a", "20.0", 100),
	};
	qmclient::rank_demo::SSortState State;
	State.m_Column = qmclient::rank_demo::ESortColumn::DATE;
	qmclient::rank_demo::SortEntries(vEntries, State);
	EXPECT_EQ(DemoOrder(vEntries), (std::vector<std::string>{"a.demo", "b.demo", "z.demo"}));

	State.m_Descending = true;
	qmclient::rank_demo::SortEntries(vEntries, State);
	EXPECT_EQ(DemoOrder(vEntries), (std::vector<std::string>{"z.demo", "b.demo", "a.demo"}));
}

TEST(QmRankDemoSort, SortIsStableForEqualKeys)
{
	std::vector<qmclient::rank_demo::SEntry> vEntries = {
		MakeEntry("demo.demo", "Same", "10.0", 5),
		MakeEntry("other.demo", "Same", "10.0", 5),
	};
	qmclient::rank_demo::SSortState State;
	State.m_Column = qmclient::rank_demo::ESortColumn::PLAYERS;
	State.m_Descending = true;
	qmclient::rank_demo::SortEntries(vEntries, State);
	// 主键相等时由 demo 名兜底；兜底键同样跟随方向翻转，结果与输入顺序无关。
	EXPECT_EQ(DemoOrder(vEntries), (std::vector<std::string>{"other.demo", "demo.demo"}));
}
