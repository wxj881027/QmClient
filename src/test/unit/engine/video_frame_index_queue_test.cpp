#include <engine/client/video_frame_index_queue.h>

#include <gtest/gtest.h>
#include <test/support/tracked_allocation_resource.h>

#include <limits>

TEST(VideoFrameIndexQueue, UninitializedQueueRejectsOperationsWithoutChangingOutput)
{
	CVideoFrameIndexQueue<> Queue;
	size_t Slot = 71;
	EXPECT_TRUE(Queue.Empty());
	EXPECT_FALSE(Queue.Push(4));
	EXPECT_FALSE(Queue.Pop(Slot));
	EXPECT_EQ(Slot, 71u);
}

TEST(VideoFrameIndexQueue, PreservesCaptureOrderAcrossRepeatedWraparound)
{
	CVideoFrameIndexQueue<> Queue;
	ASSERT_TRUE(Queue.TryInit(3));
	ASSERT_TRUE(Queue.Push(7));
	ASSERT_TRUE(Queue.Push(2));
	size_t Slot;
	ASSERT_TRUE(Queue.Pop(Slot));
	EXPECT_EQ(Slot, 7u);
	ASSERT_TRUE(Queue.Push(9));
	ASSERT_TRUE(Queue.Push(1));
	ASSERT_TRUE(Queue.Pop(Slot));
	EXPECT_EQ(Slot, 2u);
	ASSERT_TRUE(Queue.Pop(Slot));
	EXPECT_EQ(Slot, 9u);
	ASSERT_TRUE(Queue.Push(8));
	ASSERT_TRUE(Queue.Pop(Slot));
	EXPECT_EQ(Slot, 1u);
	ASSERT_TRUE(Queue.Pop(Slot));
	EXPECT_EQ(Slot, 8u);
	EXPECT_TRUE(Queue.Empty());
}

TEST(VideoFrameIndexQueue, FullQueueRejectsNewSlotWithoutReplacingPendingFrames)
{
	CVideoFrameIndexQueue<> Queue;
	ASSERT_TRUE(Queue.TryInit(2));
	ASSERT_TRUE(Queue.Push(4));
	ASSERT_TRUE(Queue.Push(6));
	EXPECT_FALSE(Queue.Push(8));
	size_t Slot;
	ASSERT_TRUE(Queue.Pop(Slot));
	EXPECT_EQ(Slot, 4u);
	ASSERT_TRUE(Queue.Pop(Slot));
	EXPECT_EQ(Slot, 6u);
	EXPECT_TRUE(Queue.Empty());
}

TEST(VideoFrameIndexQueue, AllocationFailureKeepsPendingFramesAndAllowsInitializationRetry)
{
	CTrackedAllocationResource Resource;
	{
		CVideoFrameIndexQueue<CBorrowedAllocator<size_t>> Queue{&Resource};
		ASSERT_TRUE(Queue.TryInit(2));
		ASSERT_TRUE(Queue.Push(5));
		Resource.m_FailAt = Resource.m_Allocations + 1;
		EXPECT_FALSE(Queue.TryInit(4));
		size_t Slot;
		ASSERT_TRUE(Queue.Pop(Slot));
		EXPECT_EQ(Slot, 5u);
		EXPECT_TRUE(Queue.TryInit(4));
		EXPECT_EQ(Resource.m_Live, 1);
	}
	EXPECT_EQ(Resource.m_Live, 0);
}

TEST(VideoFrameIndexQueue, ZeroCapacityDoesNotDiscardPendingFrames)
{
	CVideoFrameIndexQueue<> Queue;
	ASSERT_TRUE(Queue.TryInit(1));
	ASSERT_TRUE(Queue.Push(5));
	EXPECT_FALSE(Queue.TryInit(0));
	size_t Slot;
	ASSERT_TRUE(Queue.Pop(Slot));
	EXPECT_EQ(Slot, 5u);
}

TEST(VideoFrameIndexQueue, SuccessfulInitializationReplacesPendingFrames)
{
	CVideoFrameIndexQueue<> Queue;
	ASSERT_TRUE(Queue.TryInit(1));
	ASSERT_TRUE(Queue.Push(5));
	ASSERT_TRUE(Queue.TryInit(2));
	EXPECT_TRUE(Queue.Empty());
	ASSERT_TRUE(Queue.Push(3));
	size_t Slot;
	ASSERT_TRUE(Queue.Pop(Slot));
	EXPECT_EQ(Slot, 3u);
}

TEST(VideoFrameIndexQueue, UnrepresentableCapacityKeepsPendingFrames)
{
	CVideoFrameIndexQueue<> Queue;
	ASSERT_TRUE(Queue.TryInit(1));
	ASSERT_TRUE(Queue.Push(5));
	EXPECT_FALSE(Queue.TryInit(std::numeric_limits<size_t>::max()));
	size_t Slot;
	ASSERT_TRUE(Queue.Pop(Slot));
	EXPECT_EQ(Slot, 5u);
}

TEST(VideoFrameIndexQueue, UnknownAllocatorExceptionPropagatesWithoutDiscardingPendingFrames)
{
	CTrackedAllocationResource Resource;
	CVideoFrameIndexQueue<CBorrowedAllocator<size_t>> Queue{&Resource};
	ASSERT_TRUE(Queue.TryInit(2));
	ASSERT_TRUE(Queue.Push(5));
	Resource.m_FailAt = Resource.m_Allocations + 1;
	Resource.m_ThrowUnknown = true;
	EXPECT_THROW(Queue.TryInit(4), int);
	size_t Slot;
	ASSERT_TRUE(Queue.Pop(Slot));
	EXPECT_EQ(Slot, 5u);
}

TEST(VideoFrameIndexQueue, CaptureAndConsumptionDoNotAllocateAfterInitialization)
{
	CTrackedAllocationResource Resource;
	CVideoFrameIndexQueue<CBorrowedAllocator<size_t>> Queue{&Resource};
	ASSERT_TRUE(Queue.TryInit(2));
	const int Allocations = Resource.m_Allocations;
	Resource.m_FailAt = Allocations + 1;
	for(size_t Iteration = 0; Iteration < 20; ++Iteration)
	{
		ASSERT_TRUE(Queue.Push(4));
		ASSERT_TRUE(Queue.Push(8));
		size_t Slot;
		ASSERT_TRUE(Queue.Pop(Slot));
		EXPECT_EQ(Slot, 4u);
		ASSERT_TRUE(Queue.Pop(Slot));
		EXPECT_EQ(Slot, 8u);
	}
	EXPECT_EQ(Resource.m_Allocations, Allocations);
}
