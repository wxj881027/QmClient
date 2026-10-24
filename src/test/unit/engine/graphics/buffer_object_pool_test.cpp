#include <engine/client/buffer_object_pool.h>

#include <gtest/gtest.h>

#include <new>

namespace
{
	struct SAllocation
	{
		bool m_Fail = false;
		bool m_LengthError = false;
		size_t m_Limit = 64;
		int m_Calls = 0;
	};
	template<typename T>
	struct CAllocator
	{
		using value_type = T;
		SAllocation *m_pState;
		explicit CAllocator(SAllocation *pState) :
			m_pState(pState) {}
		template<typename U>
		CAllocator(const CAllocator<U> &Other) :
			m_pState(Other.m_pState)
		{
		}
		T *allocate(size_t Count)
		{
			++m_pState->m_Calls;
			if(m_pState->m_Fail)
				throw std::bad_alloc();
			if(m_pState->m_LengthError)
				throw std::length_error("controlled BO slot limit");
			return std::allocator<T>().allocate(Count);
		}
		void deallocate(T *pData, size_t Count) { std::allocator<T>().deallocate(pData, Count); }
		size_t max_size() const { return m_pState->m_Limit; }
		template<typename U>
		bool operator==(const CAllocator<U> &Other) const
		{
			return m_pState == Other.m_pState;
		}
	};
	class BufferObjectPool : public ::testing::Test
	{
	protected:
		SAllocation m_Allocation;
		CBufferObjectPool<CAllocator<SBufferObjectSlot>> m_Pool{CAllocator<SBufferObjectSlot>(&m_Allocation)};
		int Create()
		{
			return m_Pool.Create([](auto &Creation) { Creation.Accepted(); });
		}
		void FailCommitted()
		{
			EXPECT_THROW(m_Pool.Create([](auto &Creation) {
				Creation.Accepted();
				throw std::bad_alloc();
			}),
				std::bad_alloc);
		}
	};
}

TEST_F(BufferObjectPool, AcceptedCreationPublishesDistinctLiveIndices)
{
	EXPECT_EQ(Create(), 0);
	EXPECT_EQ(Create(), 1);
	EXPECT_TRUE(m_Pool.IsLive(0));
	EXPECT_TRUE(m_Pool.IsLive(1));
	EXPECT_FALSE(m_Pool.HasPending());
}

TEST_F(BufferObjectPool, AllocationErrorBeforeAcceptanceReturnsTheIndexImmediately)
{
	EXPECT_THROW(m_Pool.Create([](auto &) { throw std::bad_alloc(); }), std::bad_alloc);
	EXPECT_FALSE(m_Pool.HasPending());
	EXPECT_FALSE(m_Pool.IsLive(0));
	EXPECT_EQ(Create(), 0);
}

TEST_F(BufferObjectPool, LengthErrorBeforeAcceptanceDoesNotConsumeTheNextIndex)
{
	ASSERT_EQ(Create(), 0);
	EXPECT_THROW(m_Pool.Create([](auto &) { throw std::length_error("preparation limit"); }), std::length_error);
	EXPECT_FALSE(m_Pool.HasPending());
	EXPECT_TRUE(m_Pool.IsLive(0));
	EXPECT_EQ(Create(), 1);
}

TEST_F(BufferObjectPool, UnrelatedErrorBeforeAcceptanceAlsoReturnsTheIndex)
{
	EXPECT_THROW(m_Pool.Create([](auto &) { throw std::runtime_error("preparation failed"); }), std::runtime_error);
	EXPECT_FALSE(m_Pool.HasPending());
	EXPECT_EQ(Create(), 0);
}

TEST_F(BufferObjectPool, ReturningWithoutAcceptanceCannotPublishAnUncreatedObject)
{
	EXPECT_THROW(m_Pool.Create([](auto &) {}), std::logic_error);
	EXPECT_FALSE(m_Pool.HasPending());
	EXPECT_EQ(Create(), 0);
}

TEST_F(BufferObjectPool, PostAcceptanceFailureKeepsTheIndexUntilDeleteIsAccepted)
{
	FailCommitted();
	EXPECT_TRUE(m_Pool.HasPending());
	EXPECT_FALSE(m_Pool.IsLive(0));
	EXPECT_EQ(Create(), 1);
	int Deleted = -1;
	m_Pool.FlushPending([&](int Index) { Deleted = Index; });
	EXPECT_EQ(Deleted, 0);
	EXPECT_EQ(Create(), 0);
	EXPECT_TRUE(m_Pool.IsLive(1));
}

TEST_F(BufferObjectPool, PostAcceptanceLengthErrorRequiresDeletionBeforeReuse)
{
	EXPECT_THROW(m_Pool.Create([](auto &Creation) {
		Creation.Accepted();
		throw std::length_error("chunk preparation limit");
	}),
		std::length_error);
	EXPECT_TRUE(m_Pool.HasPending());
	m_Pool.FlushPending([](int) {});
	EXPECT_EQ(Create(), 0);
}

TEST_F(BufferObjectPool, UnrelatedPostAcceptanceErrorStillPreservesThePendingOwner)
{
	EXPECT_THROW(m_Pool.Create([](auto &Creation) {
		Creation.Accepted();
		throw std::runtime_error("chunk preparation failed");
	}),
		std::runtime_error);
	EXPECT_TRUE(m_Pool.HasPending());
	EXPECT_FALSE(m_Pool.IsLive(0));
	m_Pool.FlushPending([](int) {});
	EXPECT_EQ(Create(), 0);
}

TEST_F(BufferObjectPool, DirectReleaseCannotRecycleAPendingIndex)
{
	FailCommitted();
	m_Pool.Release(0);
	EXPECT_TRUE(m_Pool.HasPending());
	EXPECT_EQ(Create(), 1);
	m_Pool.FlushPending([](int) {});
	EXPECT_EQ(Create(), 0);
}

TEST_F(BufferObjectPool, DeleteAllocationFailureRetainsTheIndexForRetry)
{
	FailCommitted();
	EXPECT_THROW(m_Pool.FlushPending([](int) { throw std::bad_alloc(); }), std::bad_alloc);
	EXPECT_TRUE(m_Pool.HasPending());
	EXPECT_EQ(Create(), 1);
	m_Pool.FlushPending([](int) {});
	EXPECT_FALSE(m_Pool.HasPending());
	EXPECT_EQ(Create(), 0);
}

TEST_F(BufferObjectPool, DeleteLengthFailureRestoresThePendingEntry)
{
	FailCommitted();
	EXPECT_THROW(m_Pool.FlushPending([](int) { throw std::length_error("delete limit"); }), std::length_error);
	EXPECT_TRUE(m_Pool.HasPending());
	m_Pool.FlushPending([](int) {});
	EXPECT_EQ(Create(), 0);
}

TEST_F(BufferObjectPool, UnrelatedDeleteFailurePropagatesWithoutLosingTheEntry)
{
	FailCommitted();
	EXPECT_THROW(m_Pool.FlushPending([](int) { throw std::runtime_error("delete failed"); }), std::runtime_error);
	EXPECT_TRUE(m_Pool.HasPending());
	m_Pool.FlushPending([](int) {});
	EXPECT_EQ(Create(), 0);
}

TEST_F(BufferObjectPool, MultiplePendingEntriesAreDeletedOnceAndBecomeReusable)
{
	FailCommitted();
	FailCommitted();
	std::vector<int> vDeleted;
	m_Pool.FlushPending([&](int Index) { vDeleted.push_back(Index); });
	EXPECT_EQ(vDeleted, (std::vector<int>{1, 0}));
	m_Pool.FlushPending([&](int Index) { vDeleted.push_back(Index); });
	EXPECT_EQ(vDeleted.size(), 2u);
	EXPECT_EQ(Create(), 0);
	EXPECT_EQ(Create(), 1);
}

TEST_F(BufferObjectPool, FailedOuterCreationKeepsANestedPublishedObjectLive)
{
	int Nested = -1;
	EXPECT_THROW(m_Pool.Create([&](auto &Creation) {
		Creation.Accepted();
		Nested = Create();
		throw std::bad_alloc();
	}),
		std::bad_alloc);
	ASSERT_EQ(Nested, 1);
	EXPECT_TRUE(m_Pool.IsLive(Nested));
	m_Pool.FlushPending([](int) {});
	EXPECT_TRUE(m_Pool.IsLive(Nested));
	EXPECT_EQ(Create(), 0);
}

TEST_F(BufferObjectPool, DeleteFailureRestoresItsOwnerAheadOfNewPendingCreations)
{
	FailCommitted();
	EXPECT_THROW(m_Pool.FlushPending([&](int) {
		FailCommitted();
		throw std::length_error("delete failed after nested creation");
	}),
		std::length_error);
	std::vector<int> vDeleted;
	m_Pool.FlushPending([&](int Index) { vDeleted.push_back(Index); });
	EXPECT_EQ(vDeleted, (std::vector<int>{0, 1}));
	EXPECT_FALSE(m_Pool.HasPending());
}

TEST_F(BufferObjectPool, DuplicateAndInvalidReleaseCannotCorruptTheFreeList)
{
	ASSERT_EQ(Create(), 0);
	ASSERT_EQ(Create(), 1);
	m_Pool.Release(0);
	m_Pool.Release(0);
	m_Pool.Release(-1);
	m_Pool.Release(99);
	EXPECT_FALSE(m_Pool.IsLive(-1));
	EXPECT_FALSE(m_Pool.IsLive(99));
	EXPECT_EQ(Create(), 0);
	EXPECT_EQ(Create(), 2);
	EXPECT_TRUE(m_Pool.IsLive(1));
}

TEST_F(BufferObjectPool, CapacityLimitRejectsBeforeCallingTheBuilder)
{
	m_Allocation.m_Limit = 1;
	ASSERT_EQ(Create(), 0);
	int Calls = 0;
	EXPECT_THROW(m_Pool.Create([&](auto &) { ++Calls; }), std::length_error);
	EXPECT_EQ(Calls, 0);
	EXPECT_TRUE(m_Pool.IsLive(0));
}

TEST_F(BufferObjectPool, ArrayAllocationFailureDoesNotInvokeTheBuilder)
{
	m_Allocation.m_Fail = true;
	int Calls = 0;
	EXPECT_THROW(m_Pool.Create([&](auto &) { ++Calls; }), std::bad_alloc);
	EXPECT_EQ(Calls, 0);
	EXPECT_EQ(m_Pool.Size(), 0u);
	m_Allocation.m_Fail = false;
	EXPECT_EQ(Create(), 0);
}

TEST_F(BufferObjectPool, AReleasedIndexCanBeReusedWithoutAnotherArrayAllocation)
{
	ASSERT_EQ(Create(), 0);
	m_Pool.Release(0);
	const int Calls = m_Allocation.m_Calls;
	m_Allocation.m_Fail = true;
	EXPECT_EQ(Create(), 0);
	EXPECT_EQ(m_Allocation.m_Calls, Calls);
}

TEST_F(BufferObjectPool, DeviceClearDropsBothLiveAndPendingOldIndices)
{
	FailCommitted();
	ASSERT_EQ(Create(), 1);
	m_Pool.Clear();
	EXPECT_FALSE(m_Pool.HasPending());
	EXPECT_FALSE(m_Pool.IsLive(1));
	EXPECT_EQ(m_Pool.Size(), 0u);
	EXPECT_EQ(Create(), 0);
	int Deletes = 0;
	m_Pool.FlushPending([&](int) { ++Deletes; });
	EXPECT_EQ(Deletes, 0);
	EXPECT_TRUE(m_Pool.IsLive(0));
}

TEST_F(BufferObjectPool, NestedGrowthDoesNotMoveTheReservedIdentity)
{
	ASSERT_EQ(m_Pool.Create([&](auto &Creation) {
		EXPECT_EQ(Creation.Index(), 0);
		for(int Index = 1; Index < 32; ++Index)
			EXPECT_EQ(Create(), Index);
		Creation.Accepted();
	}),
		0);
	ASSERT_EQ(m_Pool.Size(), 32u);
	EXPECT_TRUE(m_Pool.IsLive(0));
	EXPECT_TRUE(m_Pool.IsLive(31));
}

TEST_F(BufferObjectPool, ArrayLengthFailureCanRetryWithoutSkippingAnIndex)
{
	m_Allocation.m_LengthError = true;
	EXPECT_THROW(Create(), std::length_error);
	EXPECT_EQ(m_Pool.Size(), 0u);
	m_Allocation.m_LengthError = false;
	EXPECT_EQ(Create(), 0);
}

TEST_F(BufferObjectPool, GrowthFailurePreservesTheEarlierLiveObject)
{
	m_Allocation.m_Limit = 1;
	ASSERT_EQ(Create(), 0);
	m_Allocation.m_Limit = 64;
	m_Allocation.m_Fail = true;
	EXPECT_THROW(Create(), std::bad_alloc);
	EXPECT_TRUE(m_Pool.IsLive(0));
	EXPECT_FALSE(m_Pool.HasPending());
	m_Allocation.m_Fail = false;
	EXPECT_EQ(Create(), 1);
}

TEST_F(BufferObjectPool, ADeleteBeingSubmittedCannotBeReleasedBeforeAcceptance)
{
	FailCommitted();
	int Other = -1;
	m_Pool.FlushPending([&](int Index) {
		m_Pool.Release(Index);
		EXPECT_FALSE(m_Pool.IsLive(Index));
		Other = Create();
		EXPECT_NE(Other, Index);
	});
	EXPECT_TRUE(m_Pool.IsLive(Other));
	EXPECT_EQ(Create(), 0);
}
