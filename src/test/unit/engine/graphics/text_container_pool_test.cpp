#include <engine/client/text_container_pool.h>

#include <gtest/gtest.h>

namespace
{
	enum class EFailure
	{
		NONE,
		ALLOCATION,
		LENGTH,
		OTHER,
	};
	struct SState
	{
		EFailure m_ConstructionFailure = EFailure::NONE;
		EFailure m_ArrayFailure = EFailure::NONE;
		size_t m_Limit = 64;
		int m_ConstructionCalls = 0;
		int m_Destroyed = 0;
		int m_Alive = 0;
		int m_ArrayCalls = 0;
	};
	thread_local SState *s_pConstructionState = nullptr;
	struct SConstructionScope
	{
		SState *m_pPrevious;
		explicit SConstructionScope(SState &State) :
			m_pPrevious(s_pConstructionState) { s_pConstructionState = &State; }
		~SConstructionScope() { s_pConstructionState = m_pPrevious; }
	};
	void FailIfRequested(EFailure Failure)
	{
		if(Failure == EFailure::ALLOCATION)
			throw std::bad_alloc();
		if(Failure == EFailure::LENGTH)
			throw std::length_error("controlled text container limit");
		if(Failure == EFailure::OTHER)
			throw std::runtime_error("unrelated constructor failure");
	}
	struct SContainer
	{
		SState *m_pState = s_pConstructionState;
		int m_Value = 0;
		SContainer()
		{
			++m_pState->m_ConstructionCalls;
			FailIfRequested(m_pState->m_ConstructionFailure);
			++m_pState->m_Alive;
		}
		~SContainer()
		{
			--m_pState->m_Alive;
			++m_pState->m_Destroyed;
		}
	};
	// 只控制容器数组的分配；对象接管、索引和空闲链均使用生产实现。
	template<typename T>
	struct CAllocator
	{
		using value_type = T;
		SState *m_pState;
		explicit CAllocator(SState *pState) :
			m_pState(pState) {}
		template<typename U>
		CAllocator(const CAllocator<U> &Other) :
			m_pState(Other.m_pState)
		{
		}
		T *allocate(size_t Count)
		{
			++m_pState->m_ArrayCalls;
			FailIfRequested(m_pState->m_ArrayFailure);
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
	using CPool = CTextContainerPool<SContainer, CAllocator<STextContainerSlot<SContainer>>>;
	class TextContainerPool : public ::testing::Test
	{
	protected:
		SState m_State;
		SConstructionScope m_Scope{m_State};
		CPool m_Pool{CAllocator<STextContainerSlot<SContainer>>(&m_State)};
	};
}

TEST_F(TextContainerPool, NewSlotsOwnDistinctObjectsAndSequentialIndices)
{
	EXPECT_EQ(m_Pool.Size(), 0u);
	EXPECT_FALSE(m_Pool.IsAllocated(0));
	ASSERT_EQ(m_Pool.Allocate(), 0);
	ASSERT_EQ(m_Pool.Allocate(), 1);
	EXPECT_NE(&m_Pool.Get(0), &m_Pool.Get(1));
	EXPECT_TRUE(m_Pool.IsAllocated(0));
	EXPECT_TRUE(m_Pool.IsAllocated(1));
	EXPECT_EQ(m_State.m_Alive, 2);
}

TEST_F(TextContainerPool, ArrayGrowthDoesNotMoveTheBorrowedContainer)
{
	ASSERT_EQ(m_Pool.Allocate(), 0);
	SContainer *pFirst = &m_Pool.Get(0);
	pFirst->m_Value = 42;
	for(int Index = 1; Index < 32; ++Index)
		ASSERT_EQ(m_Pool.Allocate(), Index);
	EXPECT_EQ(&m_Pool.Get(0), pFirst);
	EXPECT_EQ(pFirst->m_Value, 42);
	EXPECT_TRUE(m_Pool.IsAllocated(0));
}

TEST_F(TextContainerPool, FailedInitialArrayAllocationReclaimsTheUnpublishedObject)
{
	m_State.m_ArrayFailure = EFailure::ALLOCATION;
	EXPECT_EQ(m_Pool.Allocate(), -1);
	EXPECT_EQ(m_Pool.Size(), 0u);
	EXPECT_EQ(m_State.m_ConstructionCalls, 1);
	EXPECT_EQ(m_State.m_Destroyed, 1);
	EXPECT_EQ(m_State.m_Alive, 0);
	m_State.m_ArrayFailure = EFailure::NONE;
	EXPECT_EQ(m_Pool.Allocate(), 0);
	EXPECT_EQ(m_State.m_Alive, 1);
}

TEST_F(TextContainerPool, FailedGrowthKeepsTheOldSlotAndDoesNotSkipTheNextIndex)
{
	m_State.m_Limit = 1;
	ASSERT_EQ(m_Pool.Allocate(), 0);
	SContainer *pFirst = &m_Pool.Get(0);
	pFirst->m_Value = 42;
	m_State.m_Limit = 64;
	m_State.m_ArrayFailure = EFailure::ALLOCATION;
	EXPECT_EQ(m_Pool.Allocate(), -1);
	EXPECT_EQ(m_Pool.Size(), 1u);
	EXPECT_EQ(m_State.m_Alive, 1);
	EXPECT_EQ(m_State.m_Destroyed, 1);
	EXPECT_EQ(&m_Pool.Get(0), pFirst);
	EXPECT_EQ(pFirst->m_Value, 42);
	m_State.m_ArrayFailure = EFailure::NONE;
	EXPECT_EQ(m_Pool.Allocate(), 1);
}

TEST_F(TextContainerPool, ArrayLengthFailureReclaimsItsCandidateAndCanRetry)
{
	m_State.m_ArrayFailure = EFailure::LENGTH;
	EXPECT_EQ(m_Pool.Allocate(), -1);
	EXPECT_EQ(m_State.m_Destroyed, 1);
	EXPECT_EQ(m_State.m_Alive, 0);
	EXPECT_EQ(m_Pool.Size(), 0u);
	m_State.m_ArrayFailure = EFailure::NONE;
	EXPECT_EQ(m_Pool.Allocate(), 0);
}

TEST_F(TextContainerPool, ConstructorAllocationFailureDoesNotPublishOrTouchTheArray)
{
	m_State.m_ConstructionFailure = EFailure::ALLOCATION;
	EXPECT_EQ(m_Pool.Allocate(), -1);
	EXPECT_EQ(m_Pool.Size(), 0u);
	EXPECT_EQ(m_State.m_ArrayCalls, 0);
	EXPECT_EQ(m_State.m_Alive, 0);
	m_State.m_ConstructionFailure = EFailure::NONE;
	EXPECT_EQ(m_Pool.Allocate(), 0);
}

TEST_F(TextContainerPool, ConstructorLengthFailureLeavesTheNextIndexAvailable)
{
	m_State.m_ConstructionFailure = EFailure::LENGTH;
	EXPECT_EQ(m_Pool.Allocate(), -1);
	EXPECT_EQ(m_Pool.Size(), 0u);
	EXPECT_EQ(m_State.m_ArrayCalls, 0);
	m_State.m_ConstructionFailure = EFailure::NONE;
	EXPECT_EQ(m_Pool.Allocate(), 0);
}

TEST_F(TextContainerPool, ReleasedSlotsReuseTheSameObjectsInLastReleasedOrder)
{
	ASSERT_EQ(m_Pool.Allocate(), 0);
	ASSERT_EQ(m_Pool.Allocate(), 1);
	SContainer *pFirst = &m_Pool.Get(0);
	SContainer *pSecond = &m_Pool.Get(1);
	m_Pool.Release(0);
	m_Pool.Release(1);
	const int ArrayCalls = m_State.m_ArrayCalls;
	m_State.m_ArrayFailure = m_State.m_ConstructionFailure = EFailure::ALLOCATION;
	EXPECT_EQ(m_Pool.Allocate(), 1);
	EXPECT_EQ(m_Pool.Allocate(), 0);
	EXPECT_EQ(&m_Pool.Get(0), pFirst);
	EXPECT_EQ(&m_Pool.Get(1), pSecond);
	EXPECT_EQ(m_State.m_ArrayCalls, ArrayCalls);
	EXPECT_EQ(m_State.m_ConstructionCalls, 2);
	EXPECT_EQ(m_State.m_Destroyed, 0);
}

TEST_F(TextContainerPool, DuplicateAndInvalidReleaseCannotPublishOneSlotTwice)
{
	ASSERT_EQ(m_Pool.Allocate(), 0);
	ASSERT_EQ(m_Pool.Allocate(), 1);
	m_Pool.Release(0);
	m_Pool.Release(0);
	m_Pool.Release(-1);
	m_Pool.Release(99);
	EXPECT_FALSE(m_Pool.IsAllocated(0));
	EXPECT_TRUE(m_Pool.IsAllocated(1));
	EXPECT_EQ(m_Pool.Allocate(), 0);
	EXPECT_EQ(m_Pool.Allocate(), 2);
}

TEST_F(TextContainerPool, ClearReclaimsBothLiveAndRetainedFreeObjectsAndRestartsIndices)
{
	ASSERT_EQ(m_Pool.Allocate(), 0);
	ASSERT_EQ(m_Pool.Allocate(), 1);
	m_Pool.Release(0);
	m_Pool.Clear();
	EXPECT_EQ(m_State.m_Alive, 0);
	EXPECT_EQ(m_State.m_Destroyed, 2);
	EXPECT_EQ(m_Pool.Size(), 0u);
	EXPECT_FALSE(m_Pool.IsAllocated(1));
	m_Pool.Clear();
	EXPECT_EQ(m_State.m_Destroyed, 2);
	EXPECT_EQ(m_Pool.Allocate(), 0);
}

TEST_F(TextContainerPool, DestructionReclaimsRetainedObjectsWithoutExplicitClear)
{
	{
		CTextContainerPool<SContainer> Pool;
		ASSERT_EQ(Pool.Allocate(), 0);
		ASSERT_EQ(Pool.Allocate(), 1);
		Pool.Release(0);
		EXPECT_EQ(m_State.m_Alive, 2);
	}
	EXPECT_EQ(m_State.m_Alive, 0);
	EXPECT_EQ(m_State.m_Destroyed, 2);
}

TEST_F(TextContainerPool, CapacityLimitRejectsBeforeConstructingAnotherObject)
{
	m_State.m_Limit = 1;
	ASSERT_EQ(m_Pool.Allocate(), 0);
	EXPECT_EQ(m_Pool.Allocate(), -1);
	EXPECT_EQ(m_State.m_ConstructionCalls, 1);
	EXPECT_EQ(m_State.m_Alive, 1);
	m_Pool.Release(0);
	EXPECT_EQ(m_Pool.Allocate(), 0);
}

TEST_F(TextContainerPool, UnrelatedConstructorFailurePropagatesWithoutConsumingAnIndex)
{
	m_State.m_ConstructionFailure = EFailure::OTHER;
	EXPECT_THROW(m_Pool.Allocate(), std::runtime_error);
	EXPECT_EQ(m_Pool.Size(), 0u);
	EXPECT_EQ(m_State.m_ArrayCalls, 0);
	m_State.m_ConstructionFailure = EFailure::NONE;
	EXPECT_EQ(m_Pool.Allocate(), 0);
}
