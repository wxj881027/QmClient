#include <engine/shared/memheap.h>

#include <gtest/gtest.h>
#include <test/support/heap_allocation_probe.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<CHeap> && !std::is_copy_assignable_v<CHeap>);

TEST(MemHeapAllocation, InitialBlockFailureReturnsNullAndCanRecover)
{
	CHeapAllocationProbe Probe;
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NULL_RESULT;
	CHeap Heap(CHeapAllocationProbe::Allocate);
	EXPECT_EQ(Heap.Allocate(16), nullptr);
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NONE;
	EXPECT_NE(Heap.Allocate(16), nullptr);
}

TEST(MemHeapAllocation, GrowthFailurePreservesEarlierBytesAndUnusedTail)
{
	CHeapAllocationProbe Probe;
	CHeap Heap(CHeapAllocationProbe::Allocate);
	auto *pFirst = static_cast<char *>(Heap.Allocate(16));
	ASSERT_NE(pFirst, nullptr);
	std::memcpy(pFirst, "kept", 5);
	const size_t Large = Probe.m_LastSize + 1;
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NULL_RESULT;
	EXPECT_EQ(Heap.Allocate(Large), nullptr);
	EXPECT_STREQ(pFirst, "kept");
	const int Before = Probe.m_Requests;
	EXPECT_NE(Heap.Allocate(16), nullptr);
	EXPECT_EQ(Probe.m_Requests, Before);
}

TEST(MemHeapAllocation, GrowthCanRetryWithoutInvalidatingEarlierPointers)
{
	CHeapAllocationProbe Probe;
	CHeap Heap(CHeapAllocationProbe::Allocate);
	const char *pFirst = Heap.StoreString("still owned");
	const size_t Large = Probe.m_LastSize + 1;
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NULL_RESULT;
	EXPECT_EQ(Heap.Allocate(Large), nullptr);
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NONE;
	ASSERT_NE(Heap.Allocate(Large), nullptr);
	EXPECT_STREQ(pFirst, "still owned");
}

TEST(MemHeapAllocation, SizeAndPaddingOverflowDoesNotReachAllocator)
{
	CHeapAllocationProbe Probe;
	CHeap Heap(CHeapAllocationProbe::Allocate);
	const int Before = Probe.m_Requests;
	EXPECT_EQ(Heap.Allocate(std::numeric_limits<size_t>::max(), 16), nullptr);
	EXPECT_EQ(Probe.m_Requests, Before);
	EXPECT_NE(Heap.Allocate(16), nullptr);
}

TEST(MemHeapAllocation, ImpossibleBlockWithHeaderIsRejectedBeforeAllocation)
{
	CHeapAllocationProbe Probe;
	CHeap Heap(CHeapAllocationProbe::Allocate);
	const int Before = Probe.m_Requests;
	EXPECT_EQ(Heap.Allocate(std::numeric_limits<size_t>::max(), 1), nullptr);
	EXPECT_EQ(Probe.m_Requests, Before);
}

TEST(MemHeapAllocation, UnrepresentablePointerDistanceIsRejectedBeforeAllocation)
{
	CHeapAllocationProbe Probe;
	CHeap Heap(CHeapAllocationProbe::Allocate);
	const int Before = Probe.m_Requests;
	const size_t TooLarge = static_cast<size_t>(std::numeric_limits<ptrdiff_t>::max()) + size_t{1};
	EXPECT_EQ(Heap.Allocate(TooLarge, 1), nullptr);
	EXPECT_EQ(Probe.m_Requests, Before);
}

TEST(MemHeapAllocation, ZeroAlignmentDoesNotConsumeExistingCapacity)
{
	CHeapAllocationProbe Probe;
	CHeap Heap(CHeapAllocationProbe::Allocate);
	auto *pFirst = static_cast<unsigned char *>(Heap.Allocate(1, 1));
	ASSERT_NE(pFirst, nullptr);
	const int Before = Probe.m_Requests;
	EXPECT_EQ(Heap.Allocate(1, 0), nullptr);
	EXPECT_EQ(Heap.Allocate(1, 1), pFirst + 1);
	EXPECT_EQ(Probe.m_Requests, Before);
}

TEST(MemHeapAllocation, ExcessiveAlignmentIsRejectedWithoutAllocating)
{
	CHeapAllocationProbe Probe;
	CHeap Heap(CHeapAllocationProbe::Allocate);
	const int Before = Probe.m_Requests;
	EXPECT_EQ(Heap.Allocate(2, std::numeric_limits<size_t>::max()), nullptr);
	EXPECT_EQ(Probe.m_Requests, Before);
}

TEST(MemHeapAllocation, AlignmentAndPayloadStaySeparateAcrossRequests)
{
	CHeap Heap;
	auto *pFirst = static_cast<unsigned char *>(Heap.Allocate(3, 1));
	ASSERT_NE(pFirst, nullptr);
	std::memset(pFirst, 0x4a, 3);
	auto *pAligned = static_cast<unsigned char *>(Heap.Allocate(32, 64));
	ASSERT_NE(pAligned, nullptr);
	EXPECT_EQ(reinterpret_cast<uintptr_t>(pAligned) % 64, 0u);
	std::memset(pAligned, 0x2b, 32);
	for(int Index = 0; Index < 3; ++Index)
		EXPECT_EQ(pFirst[Index], 0x4a);
}

TEST(MemHeapAllocation, NonPowerOfTwoAlignmentKeepsItsExistingMeaning)
{
	CHeap Heap;
	ASSERT_NE(Heap.Allocate(1, 1), nullptr);
	void *pAligned = Heap.Allocate(12, 27);
	ASSERT_NE(pAligned, nullptr);
	EXPECT_EQ(reinterpret_cast<uintptr_t>(pAligned) % 27, 0u);
}

TEST(MemHeapAllocation, ZeroSizeRequestAllowsLaterPayload)
{
	CHeap Heap;
	EXPECT_NE(Heap.Allocate(0), nullptr);
	EXPECT_NE(Heap.Allocate(16), nullptr);
}

TEST(MemHeapAllocation, ResetFailureLeavesAnEmptyHeapThatCanRetry)
{
	CHeapAllocationProbe Probe;
	CHeap Heap(CHeapAllocationProbe::Allocate);
	ASSERT_NE(Heap.Allocate(16), nullptr);
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NULL_RESULT;
	Heap.Reset();
	EXPECT_EQ(Heap.Allocate(16), nullptr);
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NONE;
	EXPECT_NE(Heap.Allocate(16), nullptr);
}

TEST(MemHeapAllocation, AllocationExceptionDuringConstructionCanRecover)
{
	CHeapAllocationProbe Probe;
	Probe.m_Failure = CHeapAllocationProbe::EFailure::BAD_ALLOC;
	CHeap Heap(CHeapAllocationProbe::Allocate);
	EXPECT_EQ(Heap.Allocate(16), nullptr);
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NONE;
	EXPECT_NE(Heap.Allocate(16), nullptr);
}

TEST(MemHeapAllocation, UnknownGrowthExceptionPreservesPreviousBytes)
{
	CHeapAllocationProbe Probe;
	CHeap Heap(CHeapAllocationProbe::Allocate);
	const char *pFirst = Heap.StoreString("kept");
	const size_t Large = Probe.m_LastSize + 1;
	Probe.m_Failure = CHeapAllocationProbe::EFailure::UNKNOWN;
	EXPECT_EQ(Heap.Allocate(Large), nullptr);
	EXPECT_STREQ(pFirst, "kept");
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NONE;
	EXPECT_NE(Heap.Allocate(Large), nullptr);
}

TEST(MemHeapAllocation, MissingAllocatorRemainsSafeAcrossResetAndRawAllocation)
{
	CHeap Heap(nullptr);
	EXPECT_EQ(Heap.Allocate(16), nullptr);
	Heap.Reset();
	EXPECT_EQ(Heap.Allocate(16), nullptr);
}

TEST(MemHeapAllocation, TypedConstructionKeepsAlignmentAndArguments)
{
	struct alignas(4096) CValue
	{
		int m_Value;
		explicit CValue(int Value) :
			m_Value(Value) {}
	};
	CHeap Heap;
	CValue *pValue = Heap.Allocate<CValue>(37);
	ASSERT_NE(pValue, nullptr);
	EXPECT_EQ(pValue->m_Value, 37);
	EXPECT_EQ(reinterpret_cast<uintptr_t>(pValue) % alignof(CValue), 0u);
}

TEST(MemHeapAllocation, ConstructorExceptionDoesNotPreventLaterRawAllocation)
{
	struct CThrows
	{
		CThrows() { throw 7; }
	};
	CHeap Heap;
	EXPECT_THROW(Heap.Allocate<CThrows>(), int);
	EXPECT_NE(Heap.Allocate(16), nullptr);
}

TEST(MemHeapAllocation, StoredStringsRemainOwnedAcrossGrowth)
{
	CHeapAllocationProbe Probe;
	CHeap Heap(CHeapAllocationProbe::Allocate);
	char aInput[] = "original";
	const char *pFirst = Heap.StoreString(aInput);
	aInput[0] = 'X';
	ASSERT_NE(Heap.Allocate(Probe.m_LastSize + 1), nullptr);
	EXPECT_STREQ(pFirst, "original");
	EXPECT_STREQ(Heap.StoreString(""), "");
}

TEST(MemHeapAllocation, TypedFailureThrowsBeforeInvokingConstructor)
{
	struct CValue
	{
		explicit CValue(int &Calls) { ++Calls; }
	};
	CHeapAllocationProbe Probe;
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NULL_RESULT;
	CHeap Heap(CHeapAllocationProbe::Allocate);
	int Calls = 0;
	EXPECT_THROW(Heap.Allocate<CValue>(Calls), std::bad_alloc);
	EXPECT_EQ(Calls, 0);
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NONE;
	EXPECT_NE(Heap.Allocate<CValue>(Calls), nullptr);
	EXPECT_EQ(Calls, 1);
}

TEST(MemHeapAllocation, StringFailureThrowsAndPreservesPreviousString)
{
	CHeapAllocationProbe Probe;
	CHeap Heap(CHeapAllocationProbe::Allocate);
	const char *pOld = Heap.StoreString("old");
	const std::string Large(Probe.m_LastSize + 1, 'x');
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NULL_RESULT;
	EXPECT_THROW(Heap.StoreString(Large.c_str()), std::bad_alloc);
	EXPECT_STREQ(pOld, "old");
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NONE;
	EXPECT_STREQ(Heap.StoreString("retry"), "retry");
}

TEST(MemHeapAllocation, EmptyStringFailureDoesNotWriteToNull)
{
	CHeap Heap(nullptr);
	EXPECT_THROW(Heap.StoreString(""), std::bad_alloc);
}

TEST(MemHeapAllocation, AbsorbKeepsSourcePointersAfterSourceDestruction)
{
	CHeap Owner;
	const char *pOld = Owner.StoreString("owner");
	const char *pNew;
	{
		CHeap Candidate;
		pNew = Candidate.StoreString("candidate");
		Owner.Absorb(Candidate);
	}
	EXPECT_STREQ(pOld, "owner");
	EXPECT_STREQ(pNew, "candidate");
	EXPECT_STREQ(Owner.StoreString("later"), "later");
}

TEST(MemHeapAllocation, AbsorbedSourceCanResetWithoutInvalidatingTransferredPointers)
{
	CHeap Owner, Candidate;
	const char *pNew = Candidate.StoreString("transferred");
	Owner.Absorb(Candidate);
	Candidate.Reset();
	EXPECT_STREQ(Candidate.StoreString("separate"), "separate");
	EXPECT_STREQ(pNew, "transferred");
}

TEST(MemHeapAllocation, AbsorbingEmptyAndSelfDoesNotLoseExistingData)
{
	CHeap Owner;
	CHeap Empty(nullptr);
	const char *pOld = Owner.StoreString("kept");
	Owner.Absorb(Empty);
	Owner.Absorb(Owner);
	EXPECT_STREQ(pOld, "kept");
	Owner.Reset();
	EXPECT_NE(Owner.Allocate(16), nullptr);
}

TEST(MemHeapAllocation, StorageSwapKeepsAllocatorPoliciesWithTheirInstances)
{
	CHeapAllocationProbe Probe;
	CHeap Enabled(CHeapAllocationProbe::Allocate), Disabled(nullptr);
	const char *pMoved = Enabled.StoreString("moved");
	const size_t Large = Probe.m_LastSize + 1;
	Enabled.SwapStorage(Disabled);
	EXPECT_STREQ(pMoved, "moved");
	EXPECT_EQ(Disabled.Allocate(Large), nullptr);
	EXPECT_NE(Enabled.Allocate(16), nullptr);
	Enabled.SwapStorage(Enabled);
	EXPECT_NE(Enabled.Allocate(16), nullptr);
}
