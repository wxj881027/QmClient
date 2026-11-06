#include <game/client/components/qmclient/console_search_results.h>

#include <gtest/gtest.h>

namespace
{
	struct SHeap
	{
		bool m_Fail = false;
		bool m_LengthError = false;
		int m_Live = 0;
	};
	template<typename T>
	struct CAllocator
	{
		using value_type = T;
		SHeap *m_pHeap = nullptr;
		CAllocator() = default;
		explicit CAllocator(SHeap *pHeap) noexcept :
			m_pHeap(pHeap) {}
		template<typename U>
		CAllocator(const CAllocator<U> &Other) noexcept :
			m_pHeap(Other.m_pHeap)
		{
		}
		T *allocate(size_t Count)
		{
			if(m_pHeap->m_Fail)
				throw std::bad_alloc();
			if(m_pHeap->m_LengthError)
				throw std::length_error("controlled search limit");
			T *pResult = std::allocator<T>{}.allocate(Count);
			++m_pHeap->m_Live;
			return pResult;
		}
		void deallocate(T *pMemory, size_t Count) noexcept
		{
			--m_pHeap->m_Live;
			std::allocator<T>{}.deallocate(pMemory, Count);
		}
		template<typename U>
		bool operator==(const CAllocator<U> &Other) const noexcept
		{
			return m_pHeap == Other.m_pHeap;
		}
		template<typename U>
		bool operator!=(const CAllocator<U> &Other) const noexcept
		{
			return !(*this == Other);
		}
	};
	using CResults = QmConsoleSearch::CResults<64, CAllocator<QmConsoleSearch::SMatch>>;
	using EUpdate = CResults::EUpdate;
	class QmConsoleSearchResults : public ::testing::Test
	{
	protected:
		SHeap m_Heap;
		CResults m_Results{CAllocator<QmConsoleSearch::SMatch>(&m_Heap)};
		EUpdate Fill(const char *pQuery = "query", int Count = 3)
		{
			return m_Results.Prepare(pQuery, [&](auto &Candidate) {
				for(int i = 0; i < Count; ++i)
					if(!Candidate.Append(i, 1, i, i, i))
						return false;
				return true;
			});
		}
	};
}

TEST_F(QmConsoleSearchResults, NewResultsStartWithAnEmptyReadyQuery)
{
	EXPECT_TRUE(m_Results.Ready());
	EXPECT_FALSE(m_Results.Pending());
	EXPECT_TRUE(m_Results.Matches().empty());
	EXPECT_EQ(m_Results.CurrentMatch(), nullptr);
}

TEST_F(QmConsoleSearchResults, PublicationSortsTopToBottomThenByBytePosition)
{
	ASSERT_EQ(m_Results.Prepare("q", [](auto &Candidate) {
		return Candidate.Append(8, 2, 3, 3, 3) && Candidate.Append(4, 2, 9, 9, 9) && Candidate.Append(1, 2, 9, 9, 9);
	}),
		EUpdate::CHANGED_QUERY);
	ASSERT_EQ(m_Results.Matches().size(), 3u);
	EXPECT_EQ(m_Results.Matches()[0].m_Pos, 1);
	EXPECT_EQ(m_Results.Matches()[1].m_Pos, 4);
	EXPECT_EQ(m_Results.Matches()[2].m_StartLine, 3);
	EXPECT_EQ(m_Results.CurrentIndex(), 0);
}

TEST_F(QmConsoleSearchResults, ChangedQuerySelectsTheFirstNewMatch)
{
	ASSERT_EQ(Fill(), EUpdate::CHANGED_QUERY);
	ASSERT_TRUE(m_Results.SelectNext(1));
	ASSERT_EQ(Fill("different"), EUpdate::CHANGED_QUERY);
	EXPECT_EQ(m_Results.CurrentIndex(), 0);
	EXPECT_STREQ(m_Results.RequestedQuery(), "different");
}

TEST_F(QmConsoleSearchResults, SameQueryRetainsAndClampsTheSelection)
{
	ASSERT_EQ(Fill(), EUpdate::CHANGED_QUERY);
	ASSERT_TRUE(m_Results.SelectNext(1));
	ASSERT_TRUE(m_Results.SelectNext(1));
	ASSERT_EQ(Fill("query", 2), EUpdate::SAME_QUERY);
	EXPECT_EQ(m_Results.CurrentIndex(), 1);
}

TEST_F(QmConsoleSearchResults, CaseOnlyChangesKeepTheExistingQueryIdentity)
{
	ASSERT_EQ(Fill("Query"), EUpdate::CHANGED_QUERY);
	ASSERT_TRUE(m_Results.SelectNext(1));
	ASSERT_EQ(Fill("QUERY"), EUpdate::SAME_QUERY);
	EXPECT_EQ(m_Results.CurrentIndex(), 1);
	EXPECT_STREQ(m_Results.RequestedQuery(), "QUERY");
}

TEST_F(QmConsoleSearchResults, AllocationFailurePreservesPublishedStorageAndDisablesNavigation)
{
	ASSERT_EQ(Fill(), EUpdate::CHANGED_QUERY);
	const auto *pOld = m_Results.Matches().data();
	m_Heap.m_Fail = true;
	EXPECT_EQ(Fill("new"), EUpdate::FAILED);
	EXPECT_EQ(m_Results.Matches().data(), pOld);
	EXPECT_FALSE(m_Results.Ready());
	EXPECT_TRUE(m_Results.Pending());
	EXPECT_FALSE(m_Results.SelectNext(1));
	EXPECT_EQ(m_Results.CurrentMatch(), nullptr);
	EXPECT_STREQ(m_Results.RequestedQuery(), "new");
	EXPECT_EQ(m_Heap.m_Live, 1);
}

TEST_F(QmConsoleSearchResults, LengthFailureKeepsTheResultUnavailable)
{
	m_Heap.m_LengthError = true;
	EXPECT_EQ(Fill(), EUpdate::FAILED);
	EXPECT_TRUE(m_Results.Pending());
	EXPECT_TRUE(m_Results.Matches().empty());
}

TEST_F(QmConsoleSearchResults, AnIgnoredAppendFailureCannotPublishAPartialCandidate)
{
	m_Heap.m_Fail = true;
	EXPECT_EQ(m_Results.Prepare("q", [&](auto &Candidate) {
		EXPECT_FALSE(Candidate.Append(0, 1, 0, 0, 0));
		m_Heap.m_Fail = false;
		EXPECT_FALSE(Candidate.Append(1, 1, 0, 0, 0));
		return true;
	}),
		EUpdate::FAILED);
	EXPECT_FALSE(m_Results.Ready());
	EXPECT_TRUE(m_Results.Matches().empty());
}

TEST_F(QmConsoleSearchResults, ARejectedPartiallyBuiltCandidateReleasesItsStorage)
{
	ASSERT_EQ(Fill(), EUpdate::CHANGED_QUERY);
	EXPECT_EQ(m_Results.Prepare("new", [](auto &Candidate) {
		EXPECT_TRUE(Candidate.Append(7, 1, 0, 0, 0));
		return false;
	}),
		EUpdate::FAILED);
	EXPECT_EQ(m_Heap.m_Live, 1);
	EXPECT_EQ(m_Results.Matches().size(), 3u);
}

TEST_F(QmConsoleSearchResults, RetryPublishesTheRequestedQueryOnlyAfterCompletePreparation)
{
	ASSERT_EQ(Fill(), EUpdate::CHANGED_QUERY);
	m_Heap.m_Fail = true;
	EXPECT_EQ(Fill("new"), EUpdate::FAILED);
	m_Heap.m_Fail = false;
	ASSERT_EQ(Fill("new", 2), EUpdate::CHANGED_QUERY);
	EXPECT_TRUE(m_Results.Ready());
	EXPECT_FALSE(m_Results.Pending());
	EXPECT_EQ(m_Results.Matches().size(), 2u);
	EXPECT_EQ(m_Heap.m_Live, 1);
}

TEST_F(QmConsoleSearchResults, ReturningToThePublishedQueryAfterFailureKeepsItsSelection)
{
	ASSERT_EQ(Fill(), EUpdate::CHANGED_QUERY);
	ASSERT_TRUE(m_Results.SelectNext(1));
	m_Heap.m_Fail = true;
	EXPECT_EQ(Fill("other"), EUpdate::FAILED);
	m_Heap.m_Fail = false;
	ASSERT_EQ(Fill("query"), EUpdate::SAME_QUERY);
	EXPECT_EQ(m_Results.CurrentIndex(), 1);
}

TEST_F(QmConsoleSearchResults, EmptyQueryCommitsWithoutCallingTheBuilder)
{
	ASSERT_EQ(Fill(), EUpdate::CHANGED_QUERY);
	int Calls = 0;
	EXPECT_EQ(m_Results.Prepare("", [&](auto &) { ++Calls; return false; }), EUpdate::CHANGED_QUERY);
	EXPECT_EQ(Calls, 0);
	EXPECT_TRUE(m_Results.Ready());
	EXPECT_TRUE(m_Results.Matches().empty());
	EXPECT_EQ(m_Results.CurrentIndex(), -1);
}

TEST_F(QmConsoleSearchResults, NoMatchesPublishAnEmptySuccessfulResult)
{
	ASSERT_EQ(Fill(), EUpdate::CHANGED_QUERY);
	ASSERT_EQ(Fill("none", 0), EUpdate::CHANGED_QUERY);
	EXPECT_TRUE(m_Results.Ready());
	EXPECT_EQ(m_Results.CurrentMatch(), nullptr);
}

TEST_F(QmConsoleSearchResults, NavigationWrapsAtBothEnds)
{
	ASSERT_EQ(Fill(), EUpdate::CHANGED_QUERY);
	ASSERT_TRUE(m_Results.SelectNext(-1));
	EXPECT_EQ(m_Results.CurrentIndex(), 2);
	ASSERT_TRUE(m_Results.SelectNext(1));
	EXPECT_EQ(m_Results.CurrentIndex(), 0);
}

TEST_F(QmConsoleSearchResults, SourceInvalidationRequiresPreparationEvenWithTheSameQuery)
{
	ASSERT_EQ(Fill(), EUpdate::CHANGED_QUERY);
	m_Results.Invalidate();
	EXPECT_FALSE(m_Results.Ready());
	EXPECT_TRUE(m_Results.Pending());
	ASSERT_EQ(Fill("query", 1), EUpdate::SAME_QUERY);
	EXPECT_TRUE(m_Results.Ready());
	EXPECT_EQ(m_Results.Matches().size(), 1u);
}

TEST_F(QmConsoleSearchResults, LeavingSearchStopsRetryWithoutMakingOldResultsReady)
{
	ASSERT_EQ(Fill(), EUpdate::CHANGED_QUERY);
	m_Results.Invalidate();
	m_Results.CancelPending();
	EXPECT_FALSE(m_Results.Pending());
	EXPECT_FALSE(m_Results.Ready());
	EXPECT_EQ(m_Results.CurrentMatch(), nullptr);
}

TEST_F(QmConsoleSearchResults, ResetClearsQueriesSelectionAndPendingState)
{
	ASSERT_EQ(Fill(), EUpdate::CHANGED_QUERY);
	m_Results.Invalidate();
	m_Results.Reset();
	EXPECT_STREQ(m_Results.RequestedQuery(), "");
	EXPECT_TRUE(m_Results.Ready());
	EXPECT_FALSE(m_Results.Pending());
	EXPECT_EQ(m_Results.CurrentIndex(), -1);
	EXPECT_TRUE(m_Results.Matches().empty());
}

TEST_F(QmConsoleSearchResults, ExternalResourceExceptionsAreNotSilentlyConvertedToSearchFailure)
{
	ASSERT_EQ(Fill(), EUpdate::CHANGED_QUERY);
	EXPECT_THROW(m_Results.Prepare("new", [](auto &) -> bool { throw std::runtime_error("external resource failure"); }), std::runtime_error);
	EXPECT_FALSE(m_Results.Ready());
	EXPECT_TRUE(m_Results.Pending());
	EXPECT_EQ(m_Results.Matches().size(), 3u);
}

TEST(QmConsoleSearchResultsLifetime, DestructionReleasesThePublishedCandidateStorage)
{
	SHeap Heap;
	{
		CResults Results{CAllocator<QmConsoleSearch::SMatch>(&Heap)};
		ASSERT_EQ(Results.Prepare("q", [](auto &Candidate) { return Candidate.Append(0, 1, 0, 0, 0); }), EUpdate::CHANGED_QUERY);
		EXPECT_EQ(Heap.m_Live, 1);
	}
	EXPECT_EQ(Heap.m_Live, 0);
}
