#include <engine/client/buffer_container_pool.h>

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
				throw std::length_error("controlled slot allocation limit");
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
	class BufferContainerPool : public ::testing::Test
	{
	protected:
		SAllocation m_Allocation;
		CBufferContainerPool<CAllocator<SBufferContainerSlot>> m_Pool{CAllocator<SBufferContainerSlot>(&m_Allocation)};
		int Create(int BufferObject = 42)
		{
			return m_Pool.Create(BufferObject, [](int) {});
		}
	};
}

TEST_F(BufferContainerPool, SuccessfulSubmissionPublishesDistinctIndicesAndBindings)
{
	ASSERT_EQ(Create(10), 0);
	ASSERT_EQ(Create(20), 1);
	EXPECT_TRUE(m_Pool.IsAllocated(0));
	EXPECT_TRUE(m_Pool.IsAllocated(1));
	EXPECT_EQ(m_Pool.AssociatedBufferObject(0), 10);
	EXPECT_EQ(m_Pool.AssociatedBufferObject(1), 20);
}

TEST_F(BufferContainerPool, AssociationIsPublishedOnlyAfterTheSubmissionCallbackReturns)
{
	int Submitted = -1;
	EXPECT_EQ(m_Pool.Create(17, [&](int Index) {
		Submitted = Index;
		EXPECT_TRUE(m_Pool.IsAllocated(Index));
		EXPECT_EQ(m_Pool.AssociatedBufferObject(Index), -1);
	}),
		0);
	EXPECT_EQ(Submitted, 0);
	EXPECT_EQ(m_Pool.AssociatedBufferObject(0), 17);
}

TEST_F(BufferContainerPool, RebindingOneContainerDoesNotChangeAnotherLiveBinding)
{
	ASSERT_EQ(Create(10), 0);
	ASSERT_EQ(Create(20), 1);
	m_Pool.Bind(0, 30);
	EXPECT_EQ(m_Pool.AssociatedBufferObject(0), 30);
	EXPECT_EQ(m_Pool.AssociatedBufferObject(1), 20);
	m_Pool.Release(0);
	ASSERT_EQ(Create(40), 0);
	EXPECT_EQ(m_Pool.AssociatedBufferObject(0), 40);
	EXPECT_EQ(m_Pool.AssociatedBufferObject(1), 20);
}

TEST_F(BufferContainerPool, SubmissionAllocationFailureReturnsTheSlotForRetry)
{
	EXPECT_THROW(m_Pool.Create(17, [](int) { throw std::bad_alloc(); }), std::bad_alloc);
	EXPECT_FALSE(m_Pool.IsAllocated(0));
	EXPECT_EQ(Create(23), 0);
	EXPECT_EQ(m_Pool.AssociatedBufferObject(0), 23);
}

TEST_F(BufferContainerPool, SubmissionLengthFailureDoesNotConsumeTheNextIndex)
{
	ASSERT_EQ(Create(10), 0);
	EXPECT_THROW(m_Pool.Create(17, [](int) { throw std::length_error("controlled preparation limit"); }), std::length_error);
	EXPECT_TRUE(m_Pool.IsAllocated(0));
	EXPECT_FALSE(m_Pool.IsAllocated(1));
	EXPECT_EQ(Create(23), 1);
	EXPECT_EQ(m_Pool.AssociatedBufferObject(0), 10);
}

TEST_F(BufferContainerPool, UnrelatedSubmissionErrorPropagatesAfterReturningTheSlot)
{
	EXPECT_THROW(m_Pool.Create(17, [](int) { throw std::runtime_error("unrelated submission error"); }), std::runtime_error);
	EXPECT_FALSE(m_Pool.IsAllocated(0));
	EXPECT_EQ(Create(), 0);
}

TEST_F(BufferContainerPool, FailedSubmissionOfAReusedSlotPreservesFreeListOrder)
{
	ASSERT_EQ(Create(), 0);
	ASSERT_EQ(Create(), 1);
	m_Pool.Release(0);
	m_Pool.Release(1);
	EXPECT_THROW(m_Pool.Create(17, [](int) { throw std::bad_alloc(); }), std::bad_alloc);
	EXPECT_EQ(Create(), 1);
	EXPECT_EQ(Create(), 0);
}

TEST_F(BufferContainerPool, DuplicateReleaseCannotCreateASelfReferencingFreeList)
{
	ASSERT_EQ(Create(), 0);
	ASSERT_EQ(Create(), 1);
	m_Pool.Release(0);
	m_Pool.Release(0);
	EXPECT_EQ(Create(), 0);
	EXPECT_EQ(Create(), 2);
	EXPECT_TRUE(m_Pool.IsAllocated(1));
}

TEST_F(BufferContainerPool, InvalidIndicesDoNotAlterAnExistingAllocation)
{
	ASSERT_EQ(Create(10), 0);
	m_Pool.Release(-1);
	m_Pool.Release(99);
	EXPECT_FALSE(m_Pool.IsAllocated(-1));
	EXPECT_FALSE(m_Pool.IsAllocated(99));
	EXPECT_TRUE(m_Pool.IsAllocated(0));
	EXPECT_EQ(Create(), 1);
	EXPECT_EQ(m_Pool.AssociatedBufferObject(0), 10);
}

TEST_F(BufferContainerPool, ClearDiscardsOldBindingsAndRestartsTheIndexSpace)
{
	ASSERT_EQ(Create(10), 0);
	ASSERT_EQ(Create(20), 1);
	m_Pool.Release(0);
	m_Pool.Clear();
	EXPECT_EQ(m_Pool.Size(), 0u);
	EXPECT_FALSE(m_Pool.IsAllocated(1));
	ASSERT_EQ(Create(30), 0);
	EXPECT_EQ(m_Pool.AssociatedBufferObject(0), 30);
}

TEST_F(BufferContainerPool, FailedOuterSubmissionDoesNotUndoANestedSuccessfulSubmission)
{
	int Nested = -1;
	EXPECT_THROW(m_Pool.Create(10, [&](int) {
		Nested = Create(20);
		throw std::bad_alloc();
	}),
		std::bad_alloc);
	ASSERT_EQ(Nested, 1);
	EXPECT_FALSE(m_Pool.IsAllocated(0));
	EXPECT_TRUE(m_Pool.IsAllocated(Nested));
	EXPECT_EQ(m_Pool.AssociatedBufferObject(Nested), 20);
	EXPECT_EQ(Create(30), 0);
}

TEST_F(BufferContainerPool, NestedGrowthDoesNotInvalidateTheReservedIndex)
{
	ASSERT_EQ(m_Pool.Create(10, [&](int Index) {
		EXPECT_EQ(Index, 0);
		for(int Other = 1; Other < 32; ++Other)
			ASSERT_EQ(Create(Other + 10), Other);
	}),
		0);
	ASSERT_EQ(m_Pool.Size(), 32u);
	EXPECT_EQ(m_Pool.AssociatedBufferObject(0), 10);
	EXPECT_EQ(m_Pool.AssociatedBufferObject(31), 41);
}

TEST_F(BufferContainerPool, InitialArrayFailureDoesNotInvokeSubmission)
{
	m_Allocation.m_Fail = true;
	int Submissions = 0;
	EXPECT_THROW(m_Pool.Create(10, [&](int) { ++Submissions; }), std::bad_alloc);
	EXPECT_EQ(Submissions, 0);
	EXPECT_EQ(m_Pool.Size(), 0u);
	m_Allocation.m_Fail = false;
	EXPECT_EQ(Create(), 0);
}

TEST_F(BufferContainerPool, ArrayLengthFailureLeavesThePoolEmptyAndRetryable)
{
	m_Allocation.m_LengthError = true;
	EXPECT_THROW(Create(), std::length_error);
	EXPECT_EQ(m_Pool.Size(), 0u);
	m_Allocation.m_LengthError = false;
	EXPECT_EQ(Create(), 0);
}

TEST_F(BufferContainerPool, CapacityLimitRejectsBeforeSubmissionAndPreservesTheLiveBinding)
{
	m_Allocation.m_Limit = 1;
	ASSERT_EQ(Create(10), 0);
	int Submissions = 0;
	EXPECT_THROW(m_Pool.Create(20, [&](int) { ++Submissions; }), std::length_error);
	EXPECT_EQ(Submissions, 0);
	EXPECT_EQ(m_Pool.AssociatedBufferObject(0), 10);
}

TEST_F(BufferContainerPool, ReusingAReleasedSlotDoesNotAllocate)
{
	ASSERT_EQ(Create(), 0);
	m_Pool.Release(0);
	const int Calls = m_Allocation.m_Calls;
	m_Allocation.m_Fail = true;
	EXPECT_EQ(Create(), 0);
	EXPECT_EQ(m_Allocation.m_Calls, Calls);
}

TEST_F(BufferContainerPool, FailedGrowthPreservesTheOldBindingAndRetryDoesNotSkipAnIndex)
{
	m_Allocation.m_Limit = 1;
	ASSERT_EQ(Create(10), 0);
	m_Allocation.m_Limit = 64;
	m_Allocation.m_Fail = true;
	EXPECT_THROW(Create(20), std::bad_alloc);
	EXPECT_EQ(m_Pool.Size(), 1u);
	EXPECT_TRUE(m_Pool.IsAllocated(0));
	EXPECT_EQ(m_Pool.AssociatedBufferObject(0), 10);
	m_Allocation.m_Fail = false;
	EXPECT_EQ(Create(20), 1);
}
