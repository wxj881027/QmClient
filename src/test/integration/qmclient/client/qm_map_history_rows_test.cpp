#include <game/client/components/qmclient/map_history_rows.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{
	class CQmMapHistoryRowsTest : public ::testing::Test
	{
	protected:
		QmMapHistory::CMapHistory m_History;
		std::vector<size_t> m_vIndices;
		std::vector<int> m_vIds;
		std::vector<CButtonContainer> m_vButtons;
		void SetUp() override
		{
			m_History.RecordVisit("Zulu", "z", 200, "2026-11-01");
			m_History.RecordVisit("Old", "old", 100, "2026-11-01");
			m_History.RecordVisit("Alpha", "a", 200, "2026-11-01");
			m_History.MarkFinished("z", 1000, 1000);
		}
		bool Prepare(QmMapHistory::EMapHistoryFilter Filter = QmMapHistory::EMapHistoryFilter::RECENT)
		{
			return QmMapHistoryUi::PrepareRows(m_History, Filter, m_vIndices, m_vIds, m_vButtons);
		}
	};
}

TEST_F(CQmMapHistoryRowsTest, FiltersAndTieOrderProduceMatchingInteractiveRowCounts)
{
	ASSERT_TRUE(Prepare());
	EXPECT_EQ(m_vIndices, (std::vector<size_t>{2, 0, 1}));
	EXPECT_EQ(m_vIds.size(), 3u);
	EXPECT_EQ(m_vButtons.size(), 3u);
	ASSERT_TRUE(Prepare(QmMapHistory::EMapHistoryFilter::UNFINISHED));
	EXPECT_EQ(m_vIndices, (std::vector<size_t>{2, 1}));
	EXPECT_EQ(m_vIds.size(), 2u);
	EXPECT_EQ(m_vButtons.size(), 2u);
	ASSERT_TRUE(Prepare(QmMapHistory::EMapHistoryFilter::FINISHED));
	EXPECT_EQ(m_vIndices, (std::vector<size_t>{0}));
	EXPECT_EQ(m_vIds.size(), 1u);
	EXPECT_EQ(m_vButtons.size(), 1u);
}

TEST_F(CQmMapHistoryRowsTest, RepeatedFramesKeepInteractiveAddressesWithinCachedCapacity)
{
	ASSERT_TRUE(Prepare());
	const int *pIds = m_vIds.data();
	const CButtonContainer *pButtons = m_vButtons.data();
	ASSERT_TRUE(Prepare());
	EXPECT_EQ(m_vIds.data(), pIds);
	EXPECT_EQ(m_vButtons.data(), pButtons);
	ASSERT_TRUE(Prepare(QmMapHistory::EMapHistoryFilter::UNFINISHED));
	ASSERT_TRUE(Prepare());
	EXPECT_EQ(m_vIds.data(), pIds);
	EXPECT_EQ(m_vButtons.data(), pButtons);
}

TEST_F(CQmMapHistoryRowsTest, RemovalRebuildsIndicesAgainstTheCurrentCollection)
{
	ASSERT_TRUE(Prepare());
	ASSERT_TRUE(m_History.Remove("z"));
	ASSERT_TRUE(Prepare());
	ASSERT_EQ(m_vIndices.size(), 2u);
	EXPECT_EQ(m_History.Entries()[m_vIndices[0]].m_MapId, "a");
	EXPECT_EQ(m_History.Entries()[m_vIndices[1]].m_MapId, "old");
	EXPECT_EQ(m_vIds.size(), 2u);
	EXPECT_EQ(m_vButtons.size(), 2u);
}

TEST_F(CQmMapHistoryRowsTest, EmptyModelOrFilterKeepsTheExistingUnusedIdStorage)
{
	ASSERT_TRUE(Prepare());
	const int *pIds = m_vIds.data();
	const CButtonContainer *pButtons = m_vButtons.data();
	m_History.Clear();
	ASSERT_TRUE(Prepare());
	EXPECT_TRUE(m_vIndices.empty());
	EXPECT_EQ(m_vIds.size(), 3u);
	EXPECT_EQ(m_vButtons.size(), 3u);
	m_History.RecordVisit("New unfinished", "new", 1, "2026-11-01");
	ASSERT_TRUE(Prepare(QmMapHistory::EMapHistoryFilter::FINISHED));
	EXPECT_TRUE(m_vIndices.empty());
	EXPECT_EQ(m_vIds.data(), pIds);
	EXPECT_EQ(m_vButtons.data(), pButtons);
	EXPECT_EQ(m_vIds.size(), 3u);
	EXPECT_EQ(m_vButtons.size(), 3u);
}
