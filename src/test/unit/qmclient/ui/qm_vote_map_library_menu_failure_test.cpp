#include <game/client/QmUi/QmVoteMenu.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

TEST(QmVoteMapLibraryMenu, FailedCatalogMergeLeavesTheOutputAvailableForRetry)
{
	std::vector<QmVoteMaps::SMap> vCatalog(1);
	vCatalog.front().m_Name = "New map";
	std::vector<QmVoteMaps::SMap> vCandidate(1);
	vCandidate.front().m_Name = "Old map";
	const auto *pOldMap = &vCandidate.front();
	bool Prepared;
	bool FailedAllocation;
	{
		CPredictionAllocationFailure Failure;
		Prepared = QmTryPrepareDdnetVoteMaps(vCatalog, nullptr, vCandidate);
		FailedAllocation = Failure.Triggered();
	}
	ASSERT_TRUE(FailedAllocation);
	EXPECT_FALSE(Prepared);
	ASSERT_EQ(vCandidate.size(), 1u);
	EXPECT_EQ(&vCandidate.front(), pOldMap);
	EXPECT_EQ(vCandidate.front().m_Name, "Old map");

	ASSERT_TRUE(QmTryPrepareDdnetVoteMaps(vCatalog, nullptr, vCandidate));
	ASSERT_EQ(vCandidate.size(), 1u);
	EXPECT_EQ(vCandidate.front().m_Name, "New map");
}

TEST(QmVoteMapLibraryMenu, FailedFilterDropsItsPrefixAndRetrySortsAllRows)
{
	std::vector<QmVoteMaps::SMap> vMaps(2);
	vMaps[0].m_Name = "Beta";
	vMaps[1].m_Name = "Alpha";
	std::vector<int> vRows;
	EXPECT_FALSE(QmTryRebuildVoteMapRows(vMaps, {}, QmVoteMaps::ESort::NAME, false, 0, vRows,
		[](const QmVoteMaps::SMap &Map, const QmVoteMaps::SFilter &) {
			if(Map.m_Name == "Alpha")
				throw std::bad_alloc();
			return true;
		}));
	EXPECT_TRUE(vRows.empty());

	ASSERT_TRUE(QmTryRebuildVoteMapRows(vMaps, {}, QmVoteMaps::ESort::NAME, false, 0, vRows,
		[](const QmVoteMaps::SMap &, const QmVoteMaps::SFilter &) { return true; }));
	EXPECT_EQ(vRows, (std::vector<int>{1, 0}));
}
