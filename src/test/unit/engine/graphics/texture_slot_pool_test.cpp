#include <engine/client/texture_slot_pool.h>

#include <gtest/gtest.h>

namespace
{
	struct SAllocationState
	{
		bool m_Fail = false;
		size_t m_Limit = 64;
		int m_Calls = 0;
	};

	// 只替换标准容器的分配边界，空闲链、代数与回滚执行生产实现。
	template<typename T>
	struct CFailingAllocator
	{
		using value_type = T;
		SAllocationState *m_pState;
		explicit CFailingAllocator(SAllocationState *pState) :
			m_pState(pState) {}
		template<typename U>
		CFailingAllocator(const CFailingAllocator<U> &Other) :
			m_pState(Other.m_pState)
		{
		}
		T *allocate(size_t Count)
		{
			++m_pState->m_Calls;
			if(m_pState->m_Fail)
				throw std::bad_alloc();
			return std::allocator<T>().allocate(Count);
		}
		void deallocate(T *pData, size_t Count) { std::allocator<T>().deallocate(pData, Count); }
		size_t max_size() const { return m_pState->m_Limit; }
		template<typename U>
		bool operator==(const CFailingAllocator<U> &Other) const
		{
			return m_pState == Other.m_pState;
		}
	};
	using CTestPool = CTextureSlotPool<CFailingAllocator<STextureSlot>>;
}

TEST(TextureSlotPool, GrowthKeepsExistingHandlesAllocated)
{
	CTextureSlotPool<> Pool;
	ASSERT_TRUE(Pool.Reset(1));
	std::array<int, 8> aIds;
	std::array<uint32_t, 8> aGenerations;
	for(size_t Index = 0; Index < aIds.size(); ++Index)
	{
		aIds[Index] = Pool.Allocate();
		ASSERT_GE(aIds[Index], 0);
		aGenerations[Index] = Pool.Generation(aIds[Index]);
		for(size_t Previous = 0; Previous < Index; ++Previous)
			EXPECT_NE(aIds[Index], aIds[Previous]);
	}
	for(size_t Index = 0; Index < aIds.size(); ++Index)
		EXPECT_TRUE(Pool.IsAllocated(aIds[Index], aGenerations[Index]));
}

TEST(TextureSlotPool, FailedGrowthPreservesLiveHandlesAndCanRetry)
{
	SAllocationState State;
	CTestPool Pool{CFailingAllocator<STextureSlot>(&State)};
	ASSERT_TRUE(Pool.Reset(1));
	const int First = Pool.Allocate();
	ASSERT_GE(First, 0);
	const uint32_t Generation = Pool.Generation(First);
	State.m_Fail = true;
	EXPECT_EQ(Pool.Allocate(), -1);
	EXPECT_TRUE(Pool.IsAllocated(First, Generation));
	State.m_Fail = false;
	const int Second = Pool.Allocate();
	ASSERT_GE(Second, 0);
	EXPECT_NE(Second, First);
	EXPECT_TRUE(Pool.IsAllocated(First, Generation));
}

TEST(TextureSlotPool, StaleReleaseCannotFreeAReusedSlot)
{
	CTextureSlotPool<> Pool;
	ASSERT_TRUE(Pool.Reset(1));
	const int First = Pool.Allocate();
	ASSERT_GE(First, 0);
	const uint32_t OldGeneration = Pool.Generation(First);
	Pool.Release(First, OldGeneration);
	const int Reused = Pool.Allocate();
	ASSERT_EQ(Reused, First);
	const uint32_t NewGeneration = Pool.Generation(Reused);
	EXPECT_NE(NewGeneration, OldGeneration);
	Pool.Release(First, OldGeneration);
	Pool.Release(-1, 0);
	Pool.Release(std::numeric_limits<int>::max(), 0);
	EXPECT_TRUE(Pool.IsAllocated(Reused, NewGeneration));
	EXPECT_NE(Pool.Allocate(), Reused);
}

TEST(TextureSlotPool, FailedDeviceResetStillInvalidatesOldHandles)
{
	SAllocationState State;
	CTestPool Pool{CFailingAllocator<STextureSlot>(&State)};
	ASSERT_TRUE(Pool.Reset(1));
	const int Old = Pool.Allocate();
	ASSERT_GE(Old, 0);
	const uint32_t Generation = Pool.Generation(Old);
	State.m_Fail = true;
	EXPECT_FALSE(Pool.Reset(32));
	EXPECT_FALSE(Pool.IsAllocated(Old, Generation));
	State.m_Fail = false;
	ASSERT_TRUE(Pool.Reset(32));
	const int Current = Pool.Allocate();
	ASSERT_EQ(Current, Old);
	EXPECT_FALSE(Pool.IsAllocated(Current, Generation));
	EXPECT_TRUE(Pool.IsAllocated(Current, Pool.Generation(Current)));
}

TEST(TextureSlotPool, FailedSecondReservationReturnsTheFirstSlot)
{
	SAllocationState State;
	CTestPool Pool{CFailingAllocator<STextureSlot>(&State)};
	ASSERT_TRUE(Pool.Reset(1));
	State.m_Fail = true;
	{
		auto Slots = Pool.Reserve<2>();
		EXPECT_FALSE(Slots.IsValid());
	}
	// 仍禁止扩容；成功说明刚才申请的第一个槽位已归还。
	EXPECT_EQ(Pool.Allocate(), 0);
}

TEST(TextureSlotPool, ExceptionBeforeCommitReturnsBothReservedSlots)
{
	CTextureSlotPool<> Pool;
	ASSERT_TRUE(Pool.Reset(2));
	std::array<int, 2> aIds;
	std::array<uint32_t, 2> aGenerations;
	try
	{
		auto Slots = Pool.Reserve<2>();
		ASSERT_TRUE(Slots.IsValid());
		for(size_t Index = 0; Index < 2; ++Index)
		{
			aIds[Index] = Slots.Id(Index);
			aGenerations[Index] = Slots.Generation(Index);
		}
		throw std::bad_alloc();
	}
	catch(const std::bad_alloc &)
	{
	}
	for(size_t Index = 0; Index < 2; ++Index)
		EXPECT_FALSE(Pool.IsAllocated(aIds[Index], aGenerations[Index]));
	const int First = Pool.Allocate(), Second = Pool.Allocate();
	EXPECT_NE(First, Second);
	EXPECT_TRUE(First == aIds[0] || First == aIds[1]);
	EXPECT_TRUE(Second == aIds[0] || Second == aIds[1]);
}

TEST(TextureSlotPool, CommittedReservationKeepsSlotsAllocated)
{
	CTextureSlotPool<> Pool;
	ASSERT_TRUE(Pool.Reset(2));
	std::array<int, 2> aIds;
	std::array<uint32_t, 2> aGenerations;
	{
		auto Slots = Pool.Reserve<2>();
		ASSERT_TRUE(Slots.IsValid());
		for(size_t Index = 0; Index < 2; ++Index)
		{
			aIds[Index] = Slots.Id(Index);
			aGenerations[Index] = Slots.Generation(Index);
		}
		Slots.Commit();
	}
	for(size_t Index = 0; Index < 2; ++Index)
		EXPECT_TRUE(Pool.IsAllocated(aIds[Index], aGenerations[Index]));
}

TEST(TextureSlotPool, OldReservationCannotReleaseSlotsFromANewDevice)
{
	CTextureSlotPool<> Pool;
	ASSERT_TRUE(Pool.Reset(1));
	int Current;
	uint32_t Generation;
	{
		auto Old = Pool.Reserve<1>();
		ASSERT_TRUE(Old.IsValid());
		ASSERT_TRUE(Pool.Reset(1));
		Current = Pool.Allocate();
		ASSERT_GE(Current, 0);
		Generation = Pool.Generation(Current);
	}
	EXPECT_TRUE(Pool.IsAllocated(Current, Generation));
}

TEST(TextureSlotPool, CapacityLimitRejectsGrowthWithoutCallingAllocator)
{
	SAllocationState State;
	State.m_Limit = 2;
	CTestPool Pool{CFailingAllocator<STextureSlot>(&State)};
	ASSERT_TRUE(Pool.Reset(2));
	ASSERT_GE(Pool.Allocate(), 0);
	ASSERT_GE(Pool.Allocate(), 0);
	const int Calls = State.m_Calls;
	EXPECT_EQ(Pool.Allocate(), -1);
	EXPECT_FALSE(Pool.Reset(std::numeric_limits<size_t>::max()));
	EXPECT_EQ(State.m_Calls, Calls);
}
