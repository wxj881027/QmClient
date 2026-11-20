#include <game/client/components/qmclient/map_preview_panel.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <memory>
#include <string>
#include <vector>

namespace
{
	struct SPreviewAllocationEntry
	{
		char m_aMapName[MAX_MAP_LENGTH] = {};
		bool m_Retiring = false;
		void CancelLoad() noexcept {}
	};
}

TEST(MapPreviewAllocation, IndexFiltersDirectoryLinksAndReplacesThePublishedList)
{
	CQmMapPreviewDownloadIndex Index;
	ASSERT_TRUE(Index.Prepare("<a href=\"old_a.map\">old</a>"));
	ASSERT_TRUE(Index.Prepare("<a href=\"A_a.map\">a</a><a href=\"folder/B_b.map\">subdir</a><a href=\"notes.txt\">text</a><a href=\"B_b.map\">b</a>"));
	EXPECT_EQ(Index.Hrefs(), (std::vector<std::string>{"A_a.map", "B_b.map"}));
}

TEST(MapPreviewAllocation, EveryIndexAllocationFailureKeepsThePublishedListUntilRetry)
{
	const std::string OldHtml = "<a href=\"old_" + std::string(80, 'o') + ".map\">old</a>";
	const std::string NextHtml = "<a href=\"A_" + std::string(96, 'a') + ".map\">a</a><a href=\"B_" + std::string(112, 'b') + ".map\">b</a><a href=\"C_" + std::string(128, 'c') + ".map\">c</a>";
	CQmMapPreviewDownloadIndex Expected;
	ASSERT_TRUE(Expected.Prepare(NextHtml));
	bool ReachedSuccess = false;
	int FailedAttempts = 0;
	for(size_t Budget = 0; Budget < 32; ++Budget)
	{
		CQmMapPreviewDownloadIndex Index;
		ASSERT_TRUE(Index.Prepare(OldHtml));
		const auto Published = Index.Hrefs();
		bool Prepared = false, Triggered = false;
		{
			CPredictionAllocationFailure Failure(Budget);
			Prepared = Index.Prepare(NextHtml);
			Triggered = Failure.Triggered();
		}
		if(Triggered)
		{
			++FailedAttempts;
			EXPECT_FALSE(Prepared);
			EXPECT_EQ(Index.Hrefs(), Published);
			ASSERT_TRUE(Index.Prepare(NextHtml));
			EXPECT_EQ(Index.Hrefs(), Expected.Hrefs());
		}
		else
		{
			EXPECT_TRUE(Prepared);
			EXPECT_EQ(Index.Hrefs(), Expected.Hrefs());
			ReachedSuccess = true;
			break;
		}
	}
	EXPECT_GT(FailedAttempts, 0);
	EXPECT_TRUE(ReachedSuccess);
}

TEST(MapPreviewAllocation, FailedEntryAllocationReturnsInitializationPlaceholderAndKeepsExistingEntry)
{
	CQmMapPreviewCacheEntries<SPreviewAllocationEntry> Entries(nullptr);
	auto pExisting = std::make_unique<SPreviewAllocationEntry>();
	str_copy(pExisting->m_aMapName, "complete");
	auto *pPublished = Entries.Insert(std::move(pExisting));
	SPreviewAllocationEntry *pResult = nullptr;
	bool Triggered = false, Initialized = false;
	{
		CPredictionAllocationFailure Failure;
		pResult = Entries.Create([&](SPreviewAllocationEntry &) { Initialized = true; });
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_EQ(pResult, nullptr);
	EXPECT_FALSE(Initialized);
	EXPECT_TRUE(CQmMapPreviewCacheEntries<SPreviewAllocationEntry>::IsInitializing(pResult));
	EXPECT_EQ(Entries.Size(), 1U);
	EXPECT_EQ(Entries.Find("complete"), pPublished);
	EXPECT_FALSE(pPublished->m_Retiring);
	auto *pRetry = Entries.Create([](SPreviewAllocationEntry &Entry) { str_copy(Entry.m_aMapName, "next"); });
	ASSERT_NE(pRetry, nullptr);
	EXPECT_EQ(Entries.Find("next"), pRetry);
}
