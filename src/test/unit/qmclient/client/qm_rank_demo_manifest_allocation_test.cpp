#include <game/client/components/qmclient/rank_demo_manifest.h>

#include <test/support/prediction_allocation_failure.h>

#include <gtest/gtest.h>

TEST(QmRankDemoManifestAllocation, ParseFailureDiscardsPreviouslyParsedRowsAndAllowsRetry)
{
	using namespace qmclient::rank_demo;
	constexpr char FIRST_ROW[] = R"({"status":"ok","map":"Kobra","rank":1,"demo":"first.demo"})" "\n";
	constexpr char SECOND_ROW[] = R"({"status":"ok","map":"Kobra","rank":1,"demo":"second.demo","finishers":[1,2]})";
	const std::string Manifest = std::string(FIRST_ROW) + SECOND_ROW;
	std::vector<SEntry> vFirstRow;
	size_t FirstRowAllocations;
	bool Parsed;
	{
		CPredictionAllocationFailure Probe(std::numeric_limits<size_t>::max());
		Parsed = ParseManifest(reinterpret_cast<const unsigned char *>(FIRST_ROW), sizeof(FIRST_ROW) - 1, vFirstRow);
		FirstRowAllocations = Probe.Attempts();
	}
	ASSERT_TRUE(Parsed);
	ASSERT_EQ(vFirstRow.size(), 1u);

	const auto *pData = reinterpret_cast<const unsigned char *>(Manifest.data());
	std::vector<SEntry> vEntries;
	bool AllocationFailed;
	{
		CPredictionAllocationFailure Failure(FirstRowAllocations);
		Parsed = ParseManifest(pData, Manifest.size(), vEntries);
		AllocationFailed = Failure.Triggered();
	}
	EXPECT_TRUE(AllocationFailed);
	EXPECT_FALSE(Parsed);
	EXPECT_TRUE(vEntries.empty());
	ASSERT_TRUE(ParseManifest(pData, Manifest.size(), vEntries));
	ASSERT_EQ(vEntries.size(), 2u);
	EXPECT_EQ(vEntries.back().m_Demo, "second.demo");
}

TEST(QmRankDemoManifestAllocation, CollectionFailureClearsOldResultsAndAllowsRetry)
{
	using namespace qmclient::rank_demo;
	SEntry Entry;
	Entry.m_Map = "Kobra";
	Entry.m_Rank = 1;
	Entry.m_Demo = "completed-kobra-run.demo";
	const std::vector<SEntry> vEntries{Entry};
	std::vector<SEntry> vCollected{SEntry{}};
	bool AllocationFailed;
	{
		CPredictionAllocationFailure Failure;
		CollectRankEntries(vEntries, "Kobra", 1, vCollected);
		AllocationFailed = Failure.Triggered();
	}
	EXPECT_TRUE(AllocationFailed);
	EXPECT_TRUE(vCollected.empty());
	CollectRankEntries(vEntries, "Kobra", 1, vCollected);
	ASSERT_EQ(vCollected.size(), 1u);
	EXPECT_EQ(vCollected.front().m_Demo, Entry.m_Demo);
}
