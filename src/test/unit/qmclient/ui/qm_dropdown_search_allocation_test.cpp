#include <game/client/QmUi/QmDropdown.h>

#include <gtest/gtest.h>
#include <test/support/tracked_allocation_resource.h>

#include <optional>

namespace
{
	using EResult = CQmDropdownSearch::EUpdateResult;
	class QmDropdownSearchAllocation : public ::testing::Test
	{
	protected:
		CTrackedAllocationResource m_Memory;
		CQmDropdownSearch m_Search{&m_Memory};
		std::vector<std::string> m_vNames{"Default", "Arial", "思源黑体", "Arial Narrow"};
		void Prime()
		{
			ASSERT_EQ(m_Search.TryUpdate(m_vNames, "Arial"), EResult::UPDATED);
			ASSERT_EQ(m_Search.Indices(), (std::vector<int>{1, 3}));
		}
		void FailNext() { m_Memory.m_FailAt = m_Memory.m_Allocations + 1; }
	};
}

TEST_F(QmDropdownSearchAllocation, FirstSnapshotAllocationFailureLeavesAnEmptyCacheAndCanRetry)
{
	FailNext();
	EXPECT_EQ(m_Search.TryUpdate(m_vNames, "Arial"), EResult::FAILED);
	EXPECT_TRUE(m_Search.Indices().empty());
	EXPECT_EQ(m_Search.SourceIndex(0), -1);
	EXPECT_EQ(m_Memory.m_Live, 0);
	m_Memory.m_FailAt = -1;
	ASSERT_NO_FATAL_FAILURE(Prime());
}

TEST_F(QmDropdownSearchAllocation, FailedQueryChangeKeepsThePreviousQueryAndMatches)
{
	ASSERT_NO_FATAL_FAILURE(Prime());
	FailNext();
	const std::string LongQuery(4096, 'x');
	EXPECT_EQ(m_Search.TryUpdate(m_vNames, LongQuery.c_str()), EResult::FAILED);
	EXPECT_EQ(m_Search.Indices(), (std::vector<int>{1, 3}));
	EXPECT_EQ(m_Search.TryUpdate(m_vNames, "Arial"), EResult::UNCHANGED);
	m_Memory.m_FailAt = -1;
	ASSERT_EQ(m_Search.TryUpdate(m_vNames, "Default"), EResult::UPDATED);
	EXPECT_EQ(m_Search.Indices(), (std::vector<int>{0}));
}

TEST_F(QmDropdownSearchAllocation, FailedSourceShrinkDoesNotPublishNewSourceWithOldIndices)
{
	ASSERT_NO_FATAL_FAILURE(Prime());
	const std::vector<std::string> Smaller{"Mono"};
	FailNext();
	EXPECT_EQ(m_Search.TryUpdate(Smaller, "Arial"), EResult::FAILED);
	EXPECT_EQ(m_Search.SourceIndex(1), 3);
	EXPECT_EQ(m_Search.TryUpdate(m_vNames, "Arial"), EResult::UNCHANGED);
	m_Memory.m_FailAt = -1;
	ASSERT_EQ(m_Search.TryUpdate(Smaller, "Arial"), EResult::UPDATED);
	EXPECT_TRUE(m_Search.Indices().empty());
	EXPECT_EQ(m_Search.SourceIndex(0), -1);
}

TEST_F(QmDropdownSearchAllocation, NullAllocationReturnsFailureWithoutPublishing)
{
	FailNext();
	m_Memory.m_ReturnNull = true;
	EXPECT_EQ(m_Search.TryUpdate(m_vNames, "Arial"), EResult::FAILED);
	EXPECT_TRUE(m_Search.Indices().empty());
	EXPECT_EQ(m_Memory.m_Live, 0);
	m_Memory.m_FailAt = -1;
	ASSERT_NO_FATAL_FAILURE(Prime());
}

TEST_F(QmDropdownSearchAllocation, UnknownAllocationExceptionReturnsFailureAndKeepsTheCache)
{
	ASSERT_NO_FATAL_FAILURE(Prime());
	FailNext();
	m_Memory.m_ThrowUnknown = true;
	EXPECT_EQ(m_Search.TryUpdate(m_vNames, "Default"), EResult::FAILED);
	EXPECT_EQ(m_Search.Indices(), (std::vector<int>{1, 3}));
	EXPECT_EQ(m_Memory.m_Live, 1);
}

TEST_F(QmDropdownSearchAllocation, UnchangedQueryAvoidsPreparingAnotherSourceSnapshot)
{
	ASSERT_NO_FATAL_FAILURE(Prime());
	const int Allocations = m_Memory.m_Allocations;
	FailNext();
	EXPECT_EQ(m_Search.TryUpdate(m_vNames, "Arial"), EResult::UNCHANGED);
	EXPECT_EQ(m_Memory.m_Allocations, Allocations);
}

TEST_F(QmDropdownSearchAllocation, InputChangesAreDetectedAgainstAnOwnedSourceSnapshot)
{
	ASSERT_NO_FATAL_FAILURE(Prime());
	m_vNames[1] = "Mono";
	ASSERT_EQ(m_Search.TryUpdate(m_vNames, "Arial"), EResult::UPDATED);
	EXPECT_EQ(m_Search.Indices(), (std::vector<int>{3}));
	EXPECT_EQ(m_Search.TryUpdate(m_vNames, "Arial"), EResult::UNCHANGED);
}

TEST_F(QmDropdownSearchAllocation, UnicodeFilteringAndOriginalIndicesKeepTheirExistingMeaning)
{
	ASSERT_EQ(m_Search.TryUpdate(m_vNames, "黑体"), EResult::UPDATED);
	EXPECT_EQ(m_Search.Indices(), (std::vector<int>{2}));
	ASSERT_EQ(m_Search.TryUpdate(m_vNames, "aRiAl"), EResult::UPDATED);
	EXPECT_EQ(m_Search.Indices(), (std::vector<int>{1, 3}));
	EXPECT_EQ(m_Search.SourceIndex(-1), -1);
	EXPECT_EQ(m_Search.SourceIndex(2), -1);
}

TEST_F(QmDropdownSearchAllocation, ResetInvalidatesTheCacheAndARejectedRebuildCanRetry)
{
	ASSERT_NO_FATAL_FAILURE(Prime());
	m_Search.Reset();
	EXPECT_TRUE(m_Search.Indices().empty());
	FailNext();
	EXPECT_EQ(m_Search.TryUpdate(m_vNames, "Arial"), EResult::FAILED);
	EXPECT_TRUE(m_Search.Indices().empty());
	m_Memory.m_FailAt = -1;
	ASSERT_NO_FATAL_FAILURE(Prime());
}

TEST_F(QmDropdownSearchAllocation, CopyConstructionDoesNotExtendTheSourceMemoryLoan)
{
	std::optional<CQmDropdownSearch> Copy;
	{
		CTrackedAllocationResource Memory;
		CQmDropdownSearch Source(&Memory);
		ASSERT_EQ(Source.TryUpdate(m_vNames, "Arial"), EResult::UPDATED);
		Copy.emplace(Source);
	}
	ASSERT_EQ(Copy->TryUpdate(m_vNames, "Arial"), EResult::UNCHANGED);
	ASSERT_EQ(Copy->TryUpdate(m_vNames, nullptr), EResult::UPDATED);
	EXPECT_EQ(Copy->Indices(), (std::vector<int>{0, 1, 2, 3}));
	EXPECT_EQ(Copy->TryUpdate(m_vNames, ""), EResult::UNCHANGED);
}

TEST_F(QmDropdownSearchAllocation, LegacyBooleanEntryReturnsFalseOnFailureAndCanRetry)
{
	FailNext();
	EXPECT_FALSE(m_Search.Update(m_vNames, "Arial"));
	m_Memory.m_FailAt = -1;
	EXPECT_TRUE(m_Search.Update(m_vNames, "Arial"));
	EXPECT_FALSE(m_Search.Update(m_vNames, "Arial"));
}

TEST_F(QmDropdownSearchAllocation, SourceMemoryIsReleasedWhenTheSearchStorageIsDestroyed)
{
	CTrackedAllocationResource Memory;
	{
		CQmDropdownSearch Search(&Memory);
		ASSERT_EQ(Search.TryUpdate(m_vNames, "Arial"), EResult::UPDATED);
		EXPECT_EQ(Memory.m_Live, 1);
		Search.Reset();
		// Reset 清内容但保留容量，内存域须继续有效到存储销毁。
		EXPECT_EQ(Memory.m_Live, 1);
	}
	EXPECT_EQ(Memory.m_Live, 0);
}
