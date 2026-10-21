#include <engine/shared/ringbuffer.h>

#include <gtest/gtest.h>
#include <test/support/ringbuffer_allocation_probe.h>

#include <limits>
#include <numeric>
#include <vector>

namespace
{
	struct SRecord
	{
		int m_Value;
	};

	using CBuffer = CDynamicRingBuffer<SRecord>;
}

TEST(RingBufferAllocation, InitialFailureLeavesSafeEmptyBuffer)
{
	CRingBufferAllocationProbe Probe;
	Probe.m_Fail = true;
	CBuffer Buffer(1024, CRingBufferBase::FLAG_RECYCLE, Probe.Allocate);
	EXPECT_EQ(Probe.m_Requests, 1);
	EXPECT_EQ(Probe.m_LastSize, 1024U);
	EXPECT_FALSE(Buffer.IsInitialized());
	EXPECT_EQ(Buffer.First(), nullptr);
	EXPECT_EQ(Buffer.Last(), nullptr);
	EXPECT_EQ(Buffer.Next(nullptr), nullptr);
	EXPECT_EQ(Buffer.Prev(nullptr), nullptr);
	EXPECT_EQ(Buffer.Allocate(sizeof(SRecord)), nullptr);
	EXPECT_EQ(Buffer.PopFirst(), 0);
}

TEST(RingBufferAllocation, InvalidCapacityDoesNotAllocateOrDiscardOldRecords)
{
	CRingBufferAllocationProbe Probe;
	CBuffer Buffer(1024, 0, Probe.Allocate);
	SRecord *pRecord = Buffer.Allocate(sizeof(SRecord));
	ASSERT_NE(pRecord, nullptr);
	pRecord->m_Value = 17;
	for(const int Size : {-1, 0, CRingBufferBase::ITEM_SIZE - 1})
	{
		SCOPED_TRACE(Size);
		EXPECT_FALSE(Buffer.Init(Size, 0, Probe.Allocate));
		EXPECT_EQ(Buffer.First(), pRecord);
		EXPECT_EQ(pRecord->m_Value, 17);
	}
	EXPECT_EQ(Probe.m_Requests, 1);
}

TEST(RingBufferAllocation, FailedReinitializationKeepsRecordsAndBorrowedPointers)
{
	CRingBufferAllocationProbe Probe;
	CBuffer Buffer(1024, 0, Probe.Allocate);
	SRecord *pFirst = Buffer.Allocate(sizeof(SRecord));
	SRecord *pSecond = Buffer.Allocate(sizeof(SRecord));
	ASSERT_NE(pFirst, nullptr);
	ASSERT_NE(pSecond, nullptr);
	pFirst->m_Value = 11;
	pSecond->m_Value = 22;
	Probe.m_Fail = true;
	EXPECT_FALSE(Buffer.Init(2048, CRingBufferBase::FLAG_RECYCLE, Probe.Allocate));
	EXPECT_EQ(Buffer.First(), pFirst);
	EXPECT_EQ(Buffer.Last(), pSecond);
	EXPECT_EQ(Buffer.Next(pFirst), pSecond);
	EXPECT_EQ(pFirst->m_Value, 11);
	EXPECT_EQ(pSecond->m_Value, 22);
	pFirst->m_Value = 33;
	EXPECT_EQ(Buffer.First()->m_Value, 33);
}

TEST(RingBufferAllocation, FailedInitialAllocationCanRetry)
{
	CRingBufferAllocationProbe Probe;
	Probe.m_Fail = true;
	CBuffer Buffer(1024, 0, Probe.Allocate);
	Probe.m_Fail = false;
	ASSERT_TRUE(Buffer.Init(1024, 0, Probe.Allocate));
	EXPECT_TRUE(Buffer.IsInitialized());
	SRecord *pRecord = Buffer.Allocate(sizeof(SRecord));
	ASSERT_NE(pRecord, nullptr);
	pRecord->m_Value = 42;
	EXPECT_EQ(Buffer.First()->m_Value, 42);
	EXPECT_EQ(Buffer.Last(), pRecord);
	EXPECT_EQ(Probe.m_Requests, 2);
}

TEST(RingBufferAllocation, SuccessfulReinitializationStartsWithEmptyStorage)
{
	CRingBufferAllocationProbe Probe;
	CBuffer Buffer(1024, 0, Probe.Allocate);
	ASSERT_NE(Buffer.Allocate(sizeof(SRecord)), nullptr);
	ASSERT_TRUE(Buffer.Init(2048, 0, Probe.Allocate));
	EXPECT_EQ(Buffer.First(), nullptr);
	EXPECT_EQ(Buffer.Last(), nullptr);
	EXPECT_EQ(Buffer.PopFirst(), 0);
	SRecord *pRecord = Buffer.Allocate(sizeof(SRecord));
	ASSERT_NE(pRecord, nullptr);
	pRecord->m_Value = 51;
	EXPECT_EQ(Buffer.First()->m_Value, 51);
}

TEST(RingBufferAllocation, EmptyClearIsRepeatableAfterAllocationFailure)
{
	CRingBufferAllocationProbe Probe;
	Probe.m_Fail = true;
	CBuffer Buffer(1024, 0, Probe.Allocate);
	Buffer.Clear();
	Buffer.Clear();
	EXPECT_EQ(Buffer.First(), nullptr);
	EXPECT_EQ(Buffer.Last(), nullptr);
	EXPECT_EQ(Buffer.Allocate(0), nullptr);
	EXPECT_EQ(Buffer.PopFirst(), 0);
}

TEST(RingBufferAllocation, ClearPreservesExistingNoCallbackAndReuseBehavior)
{
	CBuffer Buffer(1024, CRingBufferBase::FLAG_RECYCLE);
	int PopCount = 0;
	Buffer.SetPopCallback([&](SRecord *) { ++PopCount; });
	ASSERT_NE(Buffer.Allocate(sizeof(SRecord)), nullptr);
	Buffer.Clear();
	EXPECT_EQ(PopCount, 0);
	EXPECT_EQ(Buffer.First(), nullptr);
	EXPECT_EQ(Buffer.Last(), nullptr);
	SRecord *pRecord = Buffer.Allocate(sizeof(SRecord));
	ASSERT_NE(pRecord, nullptr);
	pRecord->m_Value = 61;
	EXPECT_EQ(Buffer.First()->m_Value, 61);
}

TEST(RingBufferAllocation, InvalidRecordSizesDoNotRecycleExistingRecords)
{
	CBuffer Buffer(1024, CRingBufferBase::FLAG_RECYCLE);
	int PopCount = 0;
	Buffer.SetPopCallback([&](SRecord *) { ++PopCount; });
	SRecord *pRecord = Buffer.Allocate(sizeof(SRecord));
	ASSERT_NE(pRecord, nullptr);
	pRecord->m_Value = 71;
	for(const int Size : {-1, std::numeric_limits<int>::max()})
	{
		SCOPED_TRACE(Size);
		EXPECT_EQ(Buffer.Allocate(Size), nullptr);
		EXPECT_EQ(Buffer.First(), pRecord);
		EXPECT_EQ(pRecord->m_Value, 71);
	}
	EXPECT_EQ(PopCount, 0);
}

TEST(RingBufferAllocation, ZeroPayloadKeepsExistingAllocationAndPopBehavior)
{
	CBuffer Buffer(1024);
	SRecord *pRecord = Buffer.Allocate(0);
	ASSERT_NE(pRecord, nullptr);
	EXPECT_EQ(Buffer.First(), pRecord);
	EXPECT_EQ(Buffer.Last(), pRecord);
	EXPECT_EQ(Buffer.PopFirst(), 1);
	EXPECT_EQ(Buffer.First(), nullptr);
}

TEST(RingBufferAllocation, FullNonRecyclingBufferKeepsExistingRecord)
{
	CBuffer Buffer(1024);
	SRecord *pRecord = Buffer.Allocate(900);
	ASSERT_NE(pRecord, nullptr);
	pRecord->m_Value = 81;
	EXPECT_EQ(Buffer.Allocate(900), nullptr);
	EXPECT_EQ(Buffer.First(), pRecord);
	EXPECT_EQ(Buffer.Last(), pRecord);
	EXPECT_EQ(pRecord->m_Value, 81);
}

TEST(RingBufferAllocation, RecyclingRetiresOldestRecordsInOrder)
{
	CBuffer Buffer(1024, CRingBufferBase::FLAG_RECYCLE);
	std::vector<int> vValues;
	Buffer.SetPopCallback([&](SRecord *pRecord) { vValues.push_back(pRecord->m_Value); });
	for(int Value = 1; Value <= 128; ++Value)
	{
		SRecord *pRecord = Buffer.Allocate(sizeof(SRecord));
		ASSERT_NE(pRecord, nullptr);
		pRecord->m_Value = Value;
	}
	ASSERT_FALSE(vValues.empty());
	for(SRecord *pRecord = Buffer.First(); pRecord != nullptr; pRecord = Buffer.Next(pRecord))
	{
		ASSERT_LT(vValues.size(), 128U);
		vValues.push_back(pRecord->m_Value);
	}
	std::vector<int> vExpected(128);
	std::iota(vExpected.begin(), vExpected.end(), 1);
	EXPECT_EQ(vValues, vExpected);
	ASSERT_NE(Buffer.Last(), nullptr);
	EXPECT_EQ(Buffer.Last()->m_Value, 128);
}

TEST(RingBufferAllocation, FailedReinitializationPreservesRecyclePolicyAndCallback)
{
	CRingBufferAllocationProbe Probe;
	CBuffer Buffer(1024, CRingBufferBase::FLAG_RECYCLE, Probe.Allocate);
	int PopCount = 0;
	Buffer.SetPopCallback([&](SRecord *) { ++PopCount; });
	Probe.m_Fail = true;
	EXPECT_FALSE(Buffer.Init(2048, 0, Probe.Allocate));
	for(int Value = 1; Value <= 128; ++Value)
	{
		SRecord *pRecord = Buffer.Allocate(sizeof(SRecord));
		ASSERT_NE(pRecord, nullptr);
		pRecord->m_Value = Value;
	}
	EXPECT_GT(PopCount, 0);
	ASSERT_NE(Buffer.Last(), nullptr);
	EXPECT_EQ(Buffer.Last()->m_Value, 128);
}

TEST(RingBufferAllocation, PopCallbackCanReadRecordBeforeRelease)
{
	CBuffer Buffer(1024);
	SRecord *pRecord = Buffer.Allocate(sizeof(SRecord));
	ASSERT_NE(pRecord, nullptr);
	pRecord->m_Value = 91;
	int RetiredValue = -1;
	Buffer.SetPopCallback([&](SRecord *pRetired) {
		EXPECT_EQ(Buffer.First(), pRetired);
		RetiredValue = pRetired->m_Value;
	});
	EXPECT_EQ(Buffer.PopFirst(), 1);
	EXPECT_EQ(RetiredValue, 91);
	EXPECT_EQ(Buffer.First(), nullptr);
}

TEST(RingBufferAllocation, ReverseIterationMatchesForwardRecords)
{
	CBuffer Buffer(1024);
	for(const int Value : {1, 2, 3})
	{
		SRecord *pRecord = Buffer.Allocate(sizeof(SRecord));
		ASSERT_NE(pRecord, nullptr);
		pRecord->m_Value = Value;
	}
	std::vector<int> vValues;
	for(SRecord *pRecord = Buffer.Last(); pRecord != nullptr; pRecord = Buffer.Prev(pRecord))
	{
		ASSERT_LT(vValues.size(), 3U);
		vValues.push_back(pRecord->m_Value);
	}
	EXPECT_EQ(vValues, (std::vector<int>{3, 2, 1}));
}

TEST(RingBufferAllocation, StaticBufferRetainsResetAndReuseBehavior)
{
	CStaticRingBuffer<SRecord, 1024> Buffer;
	ASSERT_NE(Buffer.Allocate(sizeof(SRecord)), nullptr);
	Buffer.Init();
	EXPECT_EQ(Buffer.First(), nullptr);
	EXPECT_EQ(Buffer.Last(), nullptr);
	SRecord *pRecord = Buffer.Allocate(sizeof(SRecord));
	ASSERT_NE(pRecord, nullptr);
	pRecord->m_Value = 101;
	EXPECT_EQ(Buffer.First()->m_Value, 101);
}

TEST(RingBufferIteration, SingleFullBlockIsBothFirstAndLast)
{
	CBuffer Buffer(2 * CRingBufferBase::ITEM_SIZE);
	SRecord *pRecord = Buffer.Allocate(sizeof(SRecord));
	ASSERT_NE(pRecord, nullptr);
	pRecord->m_Value = 17;
	EXPECT_EQ(Buffer.First(), pRecord);
	EXPECT_EQ(Buffer.Last(), pRecord);
	EXPECT_EQ(Buffer.Next(pRecord), nullptr);
	EXPECT_EQ(Buffer.Prev(pRecord), nullptr);
	EXPECT_EQ(Buffer.PopFirst(), 1);
	EXPECT_EQ(Buffer.First(), nullptr);
	EXPECT_EQ(Buffer.Last(), nullptr);
}

TEST(RingBufferIteration, FullBufferReverseIterationIncludesOldestRecord)
{
	CBuffer Buffer(6 * CRingBufferBase::ITEM_SIZE);
	for(const int Value : {1, 2, 3})
	{
		SRecord *pRecord = Buffer.Allocate(sizeof(SRecord));
		ASSERT_NE(pRecord, nullptr);
		pRecord->m_Value = Value;
	}
	EXPECT_EQ(Buffer.Allocate(sizeof(SRecord)), nullptr);
	std::vector<int> vValues;
	for(SRecord *pRecord = Buffer.Last(); pRecord != nullptr; pRecord = Buffer.Prev(pRecord))
	{
		ASSERT_LT(vValues.size(), 3U);
		vValues.push_back(pRecord->m_Value);
	}
	EXPECT_EQ(vValues, (std::vector<int>{3, 2, 1}));
}

TEST(RingBufferIteration, SingleBlockRecyclingKeepsTheLatestRecordVisible)
{
	CBuffer Buffer(2 * CRingBufferBase::ITEM_SIZE, CRingBufferBase::FLAG_RECYCLE);
	std::vector<int> vRetired;
	Buffer.SetPopCallback([&](SRecord *pRecord) { vRetired.push_back(pRecord->m_Value); });
	for(int Value = 1; Value <= 8; ++Value)
	{
		SCOPED_TRACE(Value);
		SRecord *pRecord = Buffer.Allocate(sizeof(SRecord));
		ASSERT_NE(pRecord, nullptr);
		pRecord->m_Value = Value;
		EXPECT_EQ(Buffer.First(), pRecord);
		EXPECT_EQ(Buffer.Last(), pRecord);
		EXPECT_EQ(Buffer.Prev(pRecord), nullptr);
		EXPECT_EQ(Buffer.Next(pRecord), nullptr);
	}
	EXPECT_EQ(vRetired, (std::vector<int>{1, 2, 3, 4, 5, 6, 7}));
}

TEST(RingBufferIteration, ReverseIterationAfterPopKeepsRemainingRecords)
{
	CBuffer Buffer(6 * CRingBufferBase::ITEM_SIZE);
	for(const int Value : {1, 2, 3})
	{
		SRecord *pRecord = Buffer.Allocate(sizeof(SRecord));
		ASSERT_NE(pRecord, nullptr);
		pRecord->m_Value = Value;
	}
	ASSERT_EQ(Buffer.PopFirst(), 1);
	std::vector<int> vValues;
	for(SRecord *pRecord = Buffer.Last(); pRecord != nullptr; pRecord = Buffer.Prev(pRecord))
	{
		ASSERT_LT(vValues.size(), 2U);
		vValues.push_back(pRecord->m_Value);
	}
	EXPECT_EQ(vValues, (std::vector<int>{3, 2}));
	ASSERT_NE(Buffer.First(), nullptr);
	EXPECT_EQ(Buffer.First()->m_Value, 2);
}

TEST(RingBufferIteration, RecycledFullBufferTraversesBothDirectionsAfterWrap)
{
	CBuffer Buffer(6 * CRingBufferBase::ITEM_SIZE, CRingBufferBase::FLAG_RECYCLE);
	for(const int Value : {1, 2, 3, 4, 5})
	{
		SRecord *pRecord = Buffer.Allocate(sizeof(SRecord));
		ASSERT_NE(pRecord, nullptr);
		pRecord->m_Value = Value;
	}
	std::vector<int> vForward;
	for(SRecord *pRecord = Buffer.First(); pRecord != nullptr; pRecord = Buffer.Next(pRecord))
	{
		ASSERT_LT(vForward.size(), 3U);
		vForward.push_back(pRecord->m_Value);
	}
	EXPECT_EQ(vForward, (std::vector<int>{3, 4, 5}));
	std::vector<int> vReverse;
	for(SRecord *pRecord = Buffer.Last(); pRecord != nullptr; pRecord = Buffer.Prev(pRecord))
	{
		ASSERT_LT(vReverse.size(), 3U);
		vReverse.push_back(pRecord->m_Value);
	}
	EXPECT_EQ(vReverse, (std::vector<int>{5, 4, 3}));
}
