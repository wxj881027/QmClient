#include <game/client/components/qmclient/ghost_manual.h>

#include <gtest/gtest.h>

#include <array>
#include <initializer_list>
#include <limits>

namespace
{
	struct SFrame
	{
		int m_Tick;
	};
	// 只提供借用输入的索引访问，不实现待测的选择、时间映射或事务。
	struct STrack
	{
		struct SPath
		{
			std::vector<SFrame> m_vFrames;
			int Size() const { return static_cast<int>(m_vFrames.size()); }
			const SFrame *Get(int Index) const { return &m_vFrames[Index]; }
		} m_Path;
		int m_StartTick = 0;
	};
	struct SHeap
	{
		bool m_Fail = false;
		bool m_LengthError = false;
		int m_Calls = 0;
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
			++m_pHeap->m_Calls;
			if(m_pHeap->m_Fail)
				throw std::bad_alloc();
			if(m_pHeap->m_LengthError)
				throw std::length_error("controlled vector limit");
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
	class QmGhostManual : public ::testing::Test
	{
	protected:
		SHeap m_Heap;
		qm_ghost_manual::CPlan<3, CAllocator<int>> m_Plan{CAllocator<int>(&m_Heap)};
		std::array<STrack, 3> m_aTracks{};
		void Fill(int Slot, int Start, std::initializer_list<int> Ticks)
		{
			m_aTracks[Slot].m_StartTick = Start;
			for(int Tick : Ticks)
				m_aTracks[Slot].m_Path.m_vFrames.push_back({Tick});
		}
		bool Prepare(const std::vector<int> &vSlots, int First = 0)
		{
			return m_Plan.Prepare(vSlots, [this](int Slot) { return &m_aTracks[Slot]; }, First);
		}
	};
}

TEST_F(QmGhostManual, LongestSelectedTrackSetsDurationWhilePrimaryProvidesSamples)
{
	Fill(0, 100, {100, 110});
	Fill(1, 200, {200, 230});
	ASSERT_TRUE(Prepare({0, 1}, 1000));
	EXPECT_EQ(m_Plan.m_PrimarySlot, 0);
	EXPECT_EQ(m_Plan.m_Range.Last(), 1030);
	ASSERT_EQ(m_Plan.m_vSampleTicks.size(), 2u);
	EXPECT_EQ(m_Plan.m_vSampleTicks[0], 0);
	EXPECT_EQ(m_Plan.m_vSampleTicks[1], 10);
	EXPECT_TRUE(m_Plan.m_aSlots[0]);
	EXPECT_TRUE(m_Plan.m_aSlots[1]);
	EXPECT_FALSE(m_Plan.m_aSlots[2]);
}

TEST_F(QmGhostManual, InvalidFirstSlotKeepsExistingPrimarySelectionSemantics)
{
	Fill(1, 0, {0, 10});
	ASSERT_TRUE(Prepare({-1, 1, 3}));
	EXPECT_EQ(m_Plan.m_PrimarySlot, -1);
	EXPECT_TRUE(m_Plan.m_vSampleTicks.empty());
	EXPECT_TRUE(m_Plan.m_aSlots[1]);
}

TEST_F(QmGhostManual, EmptySelectionDoesNotReplaceAnExistingPlan)
{
	Fill(0, 0, {0, 10});
	ASSERT_TRUE(Prepare({0}, 20));
	EXPECT_FALSE(Prepare({}));
	EXPECT_EQ(m_Plan.m_Range.Last(), 30);
	EXPECT_TRUE(m_Plan.m_aSlots[0]);
}

TEST_F(QmGhostManual, EmptyTracksDoNotPublishAPlan)
{
	EXPECT_FALSE(Prepare({0, 1}));
	EXPECT_EQ(m_Plan.m_PrimarySlot, -1);
	EXPECT_FALSE(m_Plan.m_aSlots[0]);
	EXPECT_EQ(m_Heap.m_Calls, 0);
}

TEST_F(QmGhostManual, ZeroDurationDoesNotPublishSamples)
{
	Fill(0, 10, {10, 10});
	EXPECT_FALSE(Prepare({0}));
	EXPECT_EQ(m_Heap.m_Calls, 0);
	EXPECT_TRUE(m_Plan.m_vSampleTicks.empty());
}

TEST_F(QmGhostManual, NegativeEndingMemberDoesNotShortenTheOtherMember)
{
	Fill(0, 100, {80, 90});
	Fill(1, 0, {0, 20});
	ASSERT_TRUE(Prepare({0, 1}));
	EXPECT_EQ(m_Plan.m_Range.Duration(), 20);
	ASSERT_EQ(m_Plan.m_vSampleTicks.size(), 2u);
	EXPECT_EQ(m_Plan.m_vSampleTicks[0], -20);
}

TEST_F(QmGhostManual, AllocationFailureKeepsOldStorageAndCanRetry)
{
	Fill(0, 0, {0, 10});
	Fill(1, 0, {0, 20, 30});
	ASSERT_TRUE(Prepare({0}, 100));
	const int *pOld = m_Plan.m_vSampleTicks.data();
	m_Heap.m_Fail = true;
	EXPECT_FALSE(Prepare({1}, 200));
	EXPECT_EQ(m_Plan.m_vSampleTicks.data(), pOld);
	EXPECT_EQ(m_Plan.m_PrimarySlot, 0);
	EXPECT_EQ(m_Plan.m_Range.First(), 100);
	EXPECT_TRUE(m_Plan.m_aSlots[0]);
	EXPECT_FALSE(m_Plan.m_aSlots[1]);
	EXPECT_EQ(m_Heap.m_Live, 1);
	m_Heap.m_Fail = false;
	ASSERT_TRUE(Prepare({1}, 200));
	EXPECT_EQ(m_Plan.m_PrimarySlot, 1);
	EXPECT_EQ(m_Plan.m_Range.Last(), 230);
	EXPECT_EQ(m_Heap.m_Live, 1);
}

TEST_F(QmGhostManual, VectorLengthFailureDoesNotPublishCandidateSelection)
{
	Fill(0, 0, {0, 10});
	m_Heap.m_LengthError = true;
	EXPECT_FALSE(Prepare({0}));
	EXPECT_EQ(m_Plan.m_PrimarySlot, -1);
	EXPECT_FALSE(m_Plan.m_aSlots[0]);
	EXPECT_EQ(m_Heap.m_Live, 0);
}

TEST_F(QmGhostManual, NewPlanReplacesSelectionAndReleasesOldSampleStorage)
{
	Fill(0, 0, {0, 10});
	Fill(2, 0, {0, 20});
	ASSERT_TRUE(Prepare({0}));
	ASSERT_TRUE(Prepare({2}));
	EXPECT_FALSE(m_Plan.m_aSlots[0]);
	EXPECT_TRUE(m_Plan.m_aSlots[2]);
	EXPECT_EQ(m_Plan.m_PrimarySlot, 2);
	EXPECT_EQ(m_Heap.m_Live, 1);
}

TEST_F(QmGhostManual, DuplicateTicksAndRepresentablePrerollAreKept)
{
	Fill(0, 100, {90, 100, 100, 110});
	ASSERT_TRUE(Prepare({0}));
	ASSERT_EQ(m_Plan.m_vSampleTicks.size(), 4u);
	EXPECT_EQ(m_Plan.m_vSampleTicks[0], -10);
	EXPECT_EQ(m_Plan.m_vSampleTicks[1], 0);
	EXPECT_EQ(m_Plan.m_vSampleTicks[2], 0);
	EXPECT_EQ(m_Plan.m_vSampleTicks[3], 10);
}

TEST_F(QmGhostManual, RelativeEndOverflowIsRejectedBeforeAllocating)
{
	Fill(0, std::numeric_limits<int>::min(), {std::numeric_limits<int>::max()});
	EXPECT_FALSE(Prepare({0}));
	EXPECT_EQ(m_Heap.m_Calls, 0);
}

TEST_F(QmGhostManual, AbsoluteMetadataOverflowIsRejectedBeforeAllocating)
{
	Fill(0, 0, {0, std::numeric_limits<int>::max()});
	EXPECT_FALSE(Prepare({0}, 1));
	EXPECT_EQ(m_Heap.m_Calls, 0);
	EXPECT_EQ(m_Plan.m_Range.Last(), 0);
}

TEST_F(QmGhostManual, SparseMaximumDurationDoesNotImposeASampleCountLimit)
{
	Fill(0, 0, {0, std::numeric_limits<int>::max()});
	ASSERT_TRUE(Prepare({0}));
	EXPECT_EQ(m_Plan.m_Range.Duration(), std::numeric_limits<int>::max());
	EXPECT_EQ(m_Plan.m_vSampleTicks.size(), 2u);
}

TEST_F(QmGhostManual, EmptyPrimaryDoesNotBorrowAnotherMembersSamples)
{
	Fill(1, 0, {0, 20});
	ASSERT_TRUE(Prepare({0, 1}));
	EXPECT_EQ(m_Plan.m_PrimarySlot, 0);
	EXPECT_TRUE(m_Plan.m_vSampleTicks.empty());
}

TEST_F(QmGhostManual, InvalidSampleAfterReserveReleasesTheCandidateStorage)
{
	Fill(0, 0, {0, 10});
	Fill(1, 1, {std::numeric_limits<int>::min(), 20});
	ASSERT_TRUE(Prepare({0}));
	const int *pOld = m_Plan.m_vSampleTicks.data();
	EXPECT_FALSE(Prepare({1}));
	EXPECT_EQ(m_Plan.m_vSampleTicks.data(), pOld);
	EXPECT_EQ(m_Heap.m_Live, 1);
}

TEST_F(QmGhostManual, OutOfRangeSlotsAreNeverPassedToTheTrackGetter)
{
	Fill(0, 0, {0, 10});
	int Calls = 0;
	ASSERT_TRUE(m_Plan.Prepare({0, -1, 3, std::numeric_limits<int>::max()}, [&](int Slot) {
		++Calls;
		EXPECT_EQ(Slot, 0);
		return &m_aTracks[Slot];
	}));
	EXPECT_EQ(Calls, 2);
}

TEST(QmGhostManualLifetime, DestroyingThePlanReleasesPublishedSamples)
{
	SHeap Heap;
	{
		STrack Track;
		Track.m_Path.m_vFrames = {{0}, {10}};
		qm_ghost_manual::CPlan<1, CAllocator<int>> Plan{CAllocator<int>(&Heap)};
		ASSERT_TRUE(Plan.Prepare({0}, [&](int) { return &Track; }));
		EXPECT_EQ(Heap.m_Live, 1);
	}
	EXPECT_EQ(Heap.m_Live, 0);
}
