#include <game/client/components/qmclient/ghost_path.h>

#include <gtest/gtest.h>

#include <limits>
#include <new>
#include <utility>

namespace
{
	struct SSample
	{
		int m_Value;
		int m_Tick;
	};
	using CPath = qm_ghost_path::CChunkedPath<SSample>;
	class CHeap : public qm_ghost_path::CAllocation
	{
	public:
		int m_Calls = 0, m_FailAt = 0, m_ThrowAt = 0, m_Live = 0, m_Frees = 0;
		size_t m_LastCount = 0, m_LastSize = 0;
		void *Allocate(size_t Count, size_t Size) override
		{
			++m_Calls;
			m_LastCount = Count;
			m_LastSize = Size;
			if(m_Calls == m_FailAt)
				return nullptr;
			if(m_Calls == m_ThrowAt)
				throw std::bad_alloc();
			void *pMemory = CAllocation::Allocate(Count, Size);
			m_Live += pMemory != nullptr;
			return pMemory;
		}
		void Free(void *pMemory) noexcept override
		{
			--m_Live;
			++m_Frees;
			CAllocation::Free(pMemory);
		}
	};
}

TEST(QmGhostPath, EmptyConstructionDoesNotAllocateOrExposeItems)
{
	CHeap Heap;
	CPath Path(2, &Heap);
	EXPECT_EQ(Path.Size(), 0);
	EXPECT_EQ(Heap.m_Calls, 0);
	EXPECT_EQ(Path.Get(-1), nullptr);
	EXPECT_EQ(Path.Get(0), nullptr);
	EXPECT_EQ(std::as_const(Path).Get(1), nullptr);
}

TEST(QmGhostPath, AppendingAcrossChunksPreservesValuesAndPointers)
{
	CHeap Heap;
	CPath Path(2, &Heap);
	ASSERT_TRUE(Path.Add({1, 100}));
	SSample *pFirst = Path.Get(0);
	ASSERT_NE(pFirst, nullptr);
	ASSERT_TRUE(Path.Add({2, 101}));
	ASSERT_TRUE(Path.Add({3, 102}));
	EXPECT_EQ(Path.Size(), 3);
	EXPECT_EQ(Path.Get(0), pFirst);
	EXPECT_EQ(pFirst->m_Value, 1);
	ASSERT_NE(Path.Get(2), nullptr);
	EXPECT_EQ(Path.Get(2)->m_Tick, 102);
	EXPECT_EQ(Heap.m_Live, 2);
	EXPECT_EQ(Path.Get(3), nullptr);
}

TEST(QmGhostPath, PreallocatedItemsAreZeroedAndResetReleasesChunks)
{
	CHeap Heap;
	CPath Path(2, &Heap);
	ASSERT_TRUE(Path.SetSize(3));
	for(int i = 0; i < Path.Size(); ++i)
	{
		ASSERT_NE(Path.Get(i), nullptr);
		EXPECT_EQ(Path.Get(i)->m_Value, 0);
		EXPECT_EQ(Path.Get(i)->m_Tick, 0);
	}
	Path.Reset();
	EXPECT_EQ(Path.Size(), 0);
	EXPECT_EQ(Heap.m_Live, 0);
	EXPECT_EQ(Heap.m_Frees, 2);
	EXPECT_EQ(Path.Get(0), nullptr);
}

TEST(QmGhostPath, InitialAllocationFailureKeepsThePathEmptyAndCanRetry)
{
	CHeap Heap;
	Heap.m_FailAt = 1;
	CPath Path(2, &Heap);
	EXPECT_FALSE(Path.SetSize(3));
	EXPECT_EQ(Path.Size(), 0);
	EXPECT_EQ(Path.Get(0), nullptr);
	EXPECT_EQ(Heap.m_Live, 0);
	Heap.m_FailAt = 0;
	ASSERT_TRUE(Path.SetSize(3));
	EXPECT_EQ(Path.Size(), 3);
	EXPECT_EQ(Heap.m_Live, 2);
}

TEST(QmGhostPath, PartialGrowthFailureReleasesCandidatesAndPreservesOldItems)
{
	CHeap Heap;
	CPath Path(2, &Heap);
	ASSERT_TRUE(Path.Add({7, 100}));
	SSample *pFirst = Path.Get(0);
	Heap.m_FailAt = 3;
	ASSERT_NE(pFirst, nullptr);
	EXPECT_FALSE(Path.SetSize(7));
	EXPECT_EQ(Path.Size(), 1);
	EXPECT_EQ(Path.Get(0), pFirst);
	EXPECT_EQ(pFirst->m_Value, 7);
	EXPECT_EQ(Path.Get(1), nullptr);
	EXPECT_EQ(Heap.m_Live, 1);
	EXPECT_EQ(Heap.m_Frees, 1);
	Heap.m_FailAt = 0;
	ASSERT_TRUE(Path.SetSize(7));
	EXPECT_EQ(Path.Get(0), pFirst);
	ASSERT_NE(Path.Get(6), nullptr);
	EXPECT_EQ(Path.Get(6)->m_Value, 0);
	EXPECT_EQ(Heap.m_Live, 4);
}

TEST(QmGhostPath, ThrowingAllocationRollsBackWithoutEscaping)
{
	CHeap Heap;
	CPath Path(2, &Heap);
	ASSERT_TRUE(Path.Add({7, 100}));
	Heap.m_ThrowAt = 3;
	EXPECT_FALSE(Path.SetSize(7));
	EXPECT_EQ(Path.Size(), 1);
	ASSERT_NE(Path.Get(0), nullptr);
	EXPECT_EQ(Path.Get(0)->m_Tick, 100);
	EXPECT_EQ(Heap.m_Live, 1);
	EXPECT_EQ(Heap.m_Frees, 1);
}

TEST(QmGhostPath, FailedAppendKeepsThePreviousSampleAndLaterAppendCanSucceed)
{
	CHeap Heap;
	CPath Path(1, &Heap);
	ASSERT_TRUE(Path.Add({7, 100}));
	Heap.m_FailAt = 2;
	EXPECT_FALSE(Path.Add({8, 101}));
	EXPECT_EQ(Path.Size(), 1);
	EXPECT_EQ(Path.Get(1), nullptr);
	Heap.m_FailAt = 0;
	ASSERT_TRUE(Path.Add({9, 102}));
	ASSERT_NE(Path.Get(1), nullptr);
	EXPECT_EQ(Path.Get(1)->m_Value, 9);
	EXPECT_EQ(Path.Get(1)->m_Tick, 102);
}

TEST(QmGhostPath, NegativeSizeDoesNotChangeExistingItems)
{
	CHeap Heap;
	CPath Path(2, &Heap);
	ASSERT_TRUE(Path.Add({7, 100}));
	EXPECT_FALSE(Path.SetSize(-1));
	EXPECT_EQ(Path.Size(), 1);
	ASSERT_NE(Path.Get(0), nullptr);
	EXPECT_EQ(Path.Get(0)->m_Value, 7);
	EXPECT_EQ(Heap.m_Calls, 1);
}

TEST(QmGhostPath, InvalidChunkSizeRejectsGrowthUntilResetWithValidSize)
{
	CHeap Heap;
	CPath Path(0, &Heap);
	EXPECT_FALSE(Path.SetSize(1));
	EXPECT_FALSE(Path.Add({1, 100}));
	Path.Reset(-1);
	EXPECT_FALSE(Path.SetSize(1));
	EXPECT_EQ(Path.Get(0), nullptr);
	EXPECT_EQ(Heap.m_Calls, 0);
	Path.Reset(1);
	ASSERT_TRUE(Path.Add({1, 100}));
	EXPECT_EQ(Path.Size(), 1);
}

TEST(QmGhostPath, MaximumCountUsesCheckedChunkAndByteArithmetic)
{
	CHeap Heap;
	Heap.m_FailAt = 1;
	CPath Path(std::numeric_limits<int>::max(), &Heap);
	EXPECT_FALSE(Path.SetSize(std::numeric_limits<int>::max()));
	EXPECT_EQ(Path.Size(), 0);
	EXPECT_EQ(Heap.m_Live, 0);
	if(static_cast<size_t>(std::numeric_limits<int>::max()) > std::numeric_limits<size_t>::max() / sizeof(SSample))
		EXPECT_EQ(Heap.m_Calls, 0);
	else
	{
		EXPECT_EQ(Heap.m_Calls, 1);
		EXPECT_EQ(Heap.m_LastCount, static_cast<size_t>(std::numeric_limits<int>::max()));
		EXPECT_EQ(Heap.m_LastSize, sizeof(SSample));
	}
}

TEST(QmGhostPath, ShrinkAndRegrowReuseExistingChunksAndData)
{
	CHeap Heap;
	CPath Path(1, &Heap);
	ASSERT_TRUE(Path.Add({1, 100}));
	ASSERT_TRUE(Path.Add({2, 101}));
	ASSERT_TRUE(Path.SetSize(1));
	EXPECT_EQ(Path.Get(1), nullptr);
	ASSERT_TRUE(Path.SetSize(2));
	ASSERT_NE(Path.Get(1), nullptr);
	EXPECT_EQ(Path.Get(1)->m_Value, 2);
	EXPECT_EQ(Heap.m_Calls, 2);
}

TEST(QmGhostPath, MovingTransfersOwnershipAndTheSourceCanBeReused)
{
	CHeap Heap;
	CPath Source(1, &Heap);
	ASSERT_TRUE(Source.Add({7, 100}));
	SSample *pFirst = Source.Get(0);
	CPath Destination(std::move(Source));
	EXPECT_EQ(Destination.Get(0), pFirst);
	EXPECT_EQ(Source.Size(), 0);
	EXPECT_EQ(Source.Get(0), nullptr);
	ASSERT_TRUE(Source.Add({9, 200}));
	EXPECT_EQ(Heap.m_Live, 2);
	Destination.Reset();
	EXPECT_EQ(Heap.m_Live, 1);
}

TEST(QmGhostPath, MoveAssignmentReleasesThePreviousHeap)
{
	CHeap OldHeap, NewHeap;
	CPath Destination(1, &OldHeap), Source(2, &NewHeap);
	ASSERT_TRUE(Destination.Add({1, 100}));
	ASSERT_TRUE(Source.Add({2, 200}));
	SSample *pFirst = Source.Get(0);
	Destination = std::move(Source);
	EXPECT_EQ(OldHeap.m_Live, 0);
	EXPECT_EQ(NewHeap.m_Live, 1);
	EXPECT_EQ(Destination.Get(0), pFirst);
	Destination.Reset();
	EXPECT_EQ(NewHeap.m_Live, 0);
}

TEST(QmGhostPath, SelfMoveKeepsTheItemsAndOwnership)
{
	CHeap Heap;
	CPath Destination(1, &Heap);
	ASSERT_TRUE(Destination.Add({7, 100}));
	SSample *pFirst = Destination.Get(0);
	auto &Same = Destination;
	Destination = std::move(Same);
	EXPECT_EQ(Destination.Size(), 1);
	EXPECT_EQ(Destination.Get(0), pFirst);
	Destination.Reset();
	EXPECT_EQ(Heap.m_Live, 0);
}

TEST(QmGhostPath, AppendingAnExistingItemKeepsItsBorrowValid)
{
	CHeap Heap;
	CPath Path(1, &Heap);
	ASSERT_TRUE(Path.Add({7, 100}));
	ASSERT_NE(Path.Get(0), nullptr);
	const SSample &Value = *Path.Get(0);
	ASSERT_TRUE(Path.Add(Value));
	ASSERT_NE(Path.Get(1), nullptr);
	EXPECT_EQ(Path.Get(1)->m_Value, 7);
}

TEST(QmGhostPath, DestructionReleasesCapacityRetainedAfterShrinkingToZero)
{
	CHeap Heap;
	{
		CPath Path(1, &Heap);
		ASSERT_TRUE(Path.Add({7, 100}));
		ASSERT_TRUE(Path.Add({8, 101}));
		ASSERT_TRUE(Path.SetSize(0));
		EXPECT_EQ(Heap.m_Live, 2);
	}
	EXPECT_EQ(Heap.m_Live, 0);
	EXPECT_EQ(Heap.m_Frees, 2);
}

TEST(QmGhostPath, ReadingAPrefixZerosFieldsMissingFromTheRecord)
{
	CPath Path;
	ASSERT_TRUE(Path.ReadAndAdd([](SSample &Value) { Value.m_Value = 7; return true; }));
	ASSERT_NE(Path.Get(0), nullptr);
	EXPECT_EQ(Path.Get(0)->m_Value, 7);
	EXPECT_EQ(Path.Get(0)->m_Tick, 0);
}

TEST(QmGhostPath, FailedReadDoesNotAppendItsPartiallyFilledCandidate)
{
	CPath Path;
	ASSERT_TRUE(Path.Add({7, 100}));
	EXPECT_FALSE(Path.ReadAndAdd([](SSample &Value) { Value.m_Value = 9; return false; }));
	EXPECT_EQ(Path.Size(), 1);
	ASSERT_NE(Path.Get(0), nullptr);
	EXPECT_EQ(Path.Get(0)->m_Value, 7);
}

TEST(QmGhostPath, ThrowingReadKeepsExistingItemsAndAllowsRetry)
{
	CPath Path;
	ASSERT_TRUE(Path.Add({7, 100}));
	EXPECT_FALSE(Path.ReadAndAdd([](SSample &Value) -> bool { Value.m_Value = 9; throw std::bad_alloc(); }));
	EXPECT_EQ(Path.Size(), 1);
	ASSERT_TRUE(Path.ReadAndAdd([](SSample &Value) { Value = {10, 200}; return true; }));
	ASSERT_NE(Path.Get(1), nullptr);
	EXPECT_EQ(Path.Get(1)->m_Value, 10);
}
