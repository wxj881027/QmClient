#include <game/client/components/qmclient/rank_list_controller.h>

#include <gtest/gtest.h>

#include <string>
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

	qmclient::rank_demo::SListSelection SelectDemo(const std::string &Demo, int Index)
	{
		qmclient::rank_demo::SListSelection Selection;
		Selection.m_Demo = Demo;
		Selection.m_Index = Index;
		return Selection;
	}

	std::vector<std::string> DemoOrder(const std::vector<qmclient::rank_demo::SEntry> &Entries)
	{
		std::vector<std::string> Demos;
		for(const auto &Entry : Entries)
			Demos.push_back(Entry.m_Demo);
		return Demos;
	}
} // namespace

TEST(QmRankListController, SelectionFollowsDemoWhenSortFlipsDirection)
{
	std::vector<qmclient::rank_demo::SEntry> vEntries = {
		MakeEntry("a.demo", "Alpha", "60.0", 1),
		MakeEntry("b.demo", "Bravo", "50.0", 2),
		MakeEntry("c.demo", "Charlie", "40.0", 3),
		MakeEntry("d.demo", "Delta", "70.0", 4),
	};
	qmclient::rank_demo::SSortState Sort;
	Sort.m_Column = qmclient::rank_demo::ESortColumn::TIME;

	// 升序：c40, b50, a60, d70 —— b 在第 1 行；降序后 b 应挪到第 2 行。
	const auto First = qmclient::rank_demo::BuildRankListSnapshot(vEntries, Sort, 0, SelectDemo("b.demo", 1));
	ASSERT_EQ(First.m_vEntries[First.m_SelectedIndex].m_Demo, "b.demo");
	const int FirstIndex = First.m_SelectedIndex;

	Sort.m_Descending = true;
	// 用上一帧的选中身份构造下一帧：行号可能变化，但选中的回放必须还是同一条。
	qmclient::rank_demo::SListSelection Selection;
	Selection.m_Demo = First.m_vEntries[First.m_SelectedIndex].m_Demo;
	Selection.m_Index = First.m_SelectedIndex;
	const auto Second = qmclient::rank_demo::BuildRankListSnapshot(vEntries, Sort, 0, Selection);

	EXPECT_EQ(Second.m_vEntries[Second.m_SelectedIndex].m_Demo, "b.demo");
	EXPECT_NE(Second.m_SelectedIndex, FirstIndex);
	EXPECT_EQ(Second.m_vEntries[Second.m_SelectedIndex].m_Names, "Bravo");
}

TEST(QmRankListController, SelectionFollowsDemoWhenSearchNarrowsResults)
{
	std::vector<qmclient::rank_demo::SEntry> vEntries = {
		MakeEntry("a.demo", "Alpha", "60.0", 1),
		MakeEntry("b.demo", "Bravo", "50.0", 2),
		MakeEntry("c.demo", "Charlie", "40.0", 3),
	};
	qmclient::rank_demo::SSortState Sort;
	const auto Wide = qmclient::rank_demo::BuildRankListSnapshot(vEntries, Sort, 0, SelectDemo("c.demo", 2));
	ASSERT_EQ(Wide.m_SelectedIndex, 2);

	// 搜索后只剩 b、c：选中项仍是 c，但行号变为 1。
	const std::vector<qmclient::rank_demo::SEntry> vFiltered = {
		MakeEntry("b.demo", "Bravo", "50.0", 2),
		MakeEntry("c.demo", "Charlie", "40.0", 3),
	};
	qmclient::rank_demo::SListSelection Selection;
	Selection.m_Demo = Wide.m_vEntries[Wide.m_SelectedIndex].m_Demo;
	Selection.m_Index = Wide.m_SelectedIndex;
	const auto Narrow = qmclient::rank_demo::BuildRankListSnapshot(vFiltered, Sort, 0, Selection);

	ASSERT_EQ(Narrow.m_SelectedIndex, 1);
	EXPECT_EQ(Narrow.m_vEntries[Narrow.m_SelectedIndex].m_Demo, "c.demo");
}

TEST(QmRankListController, SelectionClampsWhenSelectedDemoDisappears)
{
	std::vector<qmclient::rank_demo::SEntry> vEntries = {
		MakeEntry("a.demo", "Alpha", "60.0", 1),
		MakeEntry("b.demo", "Bravo", "50.0", 2),
	};
	const qmclient::rank_demo::SSortState Sort;
	const auto Snapshot = qmclient::rank_demo::BuildRankListSnapshot(vEntries, Sort, 0, SelectDemo("missing.demo", 5));

	EXPECT_EQ(Snapshot.m_SelectedIndex, 1);
	EXPECT_EQ(Snapshot.m_vEntries[Snapshot.m_SelectedIndex].m_Demo, "b.demo");
}

TEST(QmRankListController, EmptyListKeepsIndexInRangeAndHasNoSelection)
{
	const qmclient::rank_demo::SSortState Sort;
	const auto Snapshot = qmclient::rank_demo::BuildRankListSnapshot({}, Sort, 100, SelectDemo("a.demo", 3));

	EXPECT_EQ(Snapshot.m_SelectedIndex, 0);
	EXPECT_TRUE(Snapshot.m_vEntries.empty());
	EXPECT_EQ(Snapshot.Selected(), nullptr);
}

TEST(QmRankListController, DisplayLimitAppliesAfterGlobalSort)
{
	// 输入顺序与时间顺序相反：先截断再排序会留下最慢的一批，必须先排序再截断。
	std::vector<qmclient::rank_demo::SEntry> vEntries;
	for(int Index = 0; Index < 150; Index++)
	{
		char aDemo[32];
		char aTime[32];
		str_format(aDemo, sizeof(aDemo), "demo%03d.demo", Index);
		str_format(aTime, sizeof(aTime), "%d.0", 1000 - Index);
		vEntries.push_back(MakeEntry(aDemo, "Player", aTime, Index));
	}
	qmclient::rank_demo::SSortState Sort;
	Sort.m_Column = qmclient::rank_demo::ESortColumn::TIME;
	const auto Snapshot = qmclient::rank_demo::BuildRankListSnapshot(vEntries, Sort, 100, {});

	ASSERT_EQ(Snapshot.m_vEntries.size(), 100U);
	EXPECT_EQ(Snapshot.m_vEntries.front().m_Demo, "demo149.demo");
	EXPECT_EQ(Snapshot.m_vEntries.back().m_Demo, "demo050.demo");
}

TEST(QmRankListController, NonPositiveLimitKeepsEveryEntry)
{
	std::vector<qmclient::rank_demo::SEntry> vEntries;
	for(int Index = 0; Index < 150; Index++)
	{
		char aDemo[32];
		str_format(aDemo, sizeof(aDemo), "demo%03d.demo", Index);
		vEntries.push_back(MakeEntry(aDemo, "Player", "60.0", Index));
	}
	const qmclient::rank_demo::SSortState Sort;
	EXPECT_EQ(qmclient::rank_demo::BuildRankListSnapshot(vEntries, Sort, 0, {}).m_vEntries.size(), 150U);
	EXPECT_EQ(qmclient::rank_demo::BuildRankListSnapshot(vEntries, Sort, -1, {}).m_vEntries.size(), 150U);
}

TEST(QmRankListController, CommitStoresRowReturnedByListBox)
{
	qmclient::rank_demo::SListSelection Selection = SelectDemo("a.demo", 0);
	const std::vector<qmclient::rank_demo::SEntry> vEntries = {
		MakeEntry("a.demo", "Alpha", "60.0", 1),
		MakeEntry("b.demo", "Bravo", "50.0", 2),
	};
	qmclient::rank_demo::CommitListSelection(Selection, vEntries, 1);

	EXPECT_EQ(Selection.m_Demo, "b.demo");
	EXPECT_EQ(Selection.m_Index, 1);
}

TEST(QmRankListController, CommitClearsSelectionWhenListBecomesEmpty)
{
	qmclient::rank_demo::SListSelection Selection = SelectDemo("a.demo", 0);
	qmclient::rank_demo::CommitListSelection(Selection, {}, -1);

	EXPECT_TRUE(Selection.m_Demo.empty());
	EXPECT_EQ(Selection.m_Index, 0);

	// 清空后恢复出条目的第一帧不应把旧 demo 又当成选中项。
	const std::vector<qmclient::rank_demo::SEntry> vEntries = {
		MakeEntry("b.demo", "Bravo", "50.0", 2),
	};
	const auto Snapshot = qmclient::rank_demo::BuildRankListSnapshot(vEntries, {}, 0, Selection);
	EXPECT_EQ(Snapshot.m_SelectedIndex, 0);
	EXPECT_EQ(Snapshot.m_vEntries[Snapshot.m_SelectedIndex].m_Demo, "b.demo");
}

TEST(QmRankListController, RowActivationDownloadsWhenReplayIsNotCached)
{
	EXPECT_EQ(qmclient::rank_demo::ResolveRowActivation(false, false, false), qmclient::rank_demo::ERowActivation::DOWNLOAD_REPLAY);
	// 影子已激活但回放未缓存时，双击仍然只下载回放。
	EXPECT_EQ(qmclient::rank_demo::ResolveRowActivation(false, true, false), qmclient::rank_demo::ERowActivation::DOWNLOAD_REPLAY);
}

TEST(QmRankListController, RowActivationLoadsGhostWhenReplayIsCached)
{
	EXPECT_EQ(qmclient::rank_demo::ResolveRowActivation(false, false, true), qmclient::rank_demo::ERowActivation::LOAD_GHOST);
	// 已在激活态：双击不重复加载，也不误触下载。
	EXPECT_EQ(qmclient::rank_demo::ResolveRowActivation(false, true, true), qmclient::rank_demo::ERowActivation::NONE);
}

TEST(QmRankListController, RowActivationQueuesUncachedDownloadWhileTaskIsRunning)
{
	EXPECT_EQ(qmclient::rank_demo::ResolveRowActivation(true, false, false), qmclient::rank_demo::ERowActivation::DOWNLOAD_REPLAY);
	EXPECT_EQ(qmclient::rank_demo::ResolveRowActivation(true, true, false), qmclient::rank_demo::ERowActivation::DOWNLOAD_REPLAY);
	EXPECT_EQ(qmclient::rank_demo::ResolveRowActivation(true, false, true), qmclient::rank_demo::ERowActivation::NONE);
	EXPECT_EQ(qmclient::rank_demo::ResolveRowActivation(true, true, true), qmclient::rank_demo::ERowActivation::NONE);
}
