#include <engine/client/render_target_readback_queue.h>

#include <gtest/gtest.h>

#include <cstdlib>

namespace
{
	using EState = IGraphics::ERenderTargetReadbackState;
	using SAllocation = CRenderTargetReadbackQueue::SAllocation;

	void Complete(const SAllocation &Allocation, uint8_t Value)
	{
		Allocation.m_pImage->m_Width = 1;
		Allocation.m_pImage->m_Height = 1;
		Allocation.m_pImage->m_Format = CImageInfo::FORMAT_RGBA;
		Allocation.m_pImage->m_pData = static_cast<uint8_t *>(malloc(4));
		ASSERT_NE(Allocation.m_pImage->m_pData, nullptr);
		Allocation.m_pImage->m_pData[0] = Value;
		Allocation.m_pCompletion->Signal();
	}
}

TEST(RenderTargetReadbackQueue, GrowingRequestsKeepsPendingRenderOutputAlive)
{
	CRenderTargetReadbackQueue Queue;
	const auto First = Queue.Allocate();
	for(int Index = 0; Index < 1024; ++Index)
		Queue.Allocate();
	Complete(First, 42);
	EXPECT_EQ(Queue.Poll(First.m_Index, First.m_Generation), EState::READY);
	CImageInfo Image;
	ASSERT_TRUE(Queue.Resolve(First.m_Index, First.m_Generation, Image));
	EXPECT_EQ(Image.m_pData[0], 42);
	Image.Free();
}

TEST(RenderTargetReadbackQueue, CanceledPendingSlotIsReusedOnlyAfterCompletion)
{
	CRenderTargetReadbackQueue Queue;
	const auto Canceled = Queue.Allocate();
	Queue.Cancel(Canceled.m_Index, Canceled.m_Generation);
	EXPECT_EQ(Queue.Poll(Canceled.m_Index, Canceled.m_Generation), EState::INVALID);
	const auto Pending = Queue.Allocate();
	EXPECT_NE(Pending.m_Index, Canceled.m_Index);
	Complete(Canceled, 10);
	const auto Reused = Queue.Allocate();
	EXPECT_EQ(Reused.m_Index, Canceled.m_Index);
	EXPECT_NE(Reused.m_Generation, Canceled.m_Generation);
	EXPECT_EQ(Reused.m_pImage->m_pData, nullptr);
	Queue.Cancel(Canceled.m_Index, Canceled.m_Generation);
	EXPECT_EQ(Queue.Poll(Reused.m_Index, Reused.m_Generation), EState::PENDING);
	EXPECT_EQ(Queue.Poll(Pending.m_Index, Pending.m_Generation), EState::PENDING);
}

TEST(RenderTargetReadbackQueue, CancelingReadyResultFreesSlotWithoutReusingOldSignal)
{
	CRenderTargetReadbackQueue Queue;
	const auto First = Queue.Allocate();
	Complete(First, 12);
	Queue.Cancel(First.m_Index, First.m_Generation);
	const auto Next = Queue.Allocate();
	EXPECT_EQ(Next.m_Index, First.m_Index);
	EXPECT_NE(Next.m_Generation, First.m_Generation);
	EXPECT_EQ(Queue.Poll(Next.m_Index, Next.m_Generation), EState::PENDING);
	EXPECT_EQ(Queue.Poll(First.m_Index, First.m_Generation), EState::INVALID);
}

TEST(RenderTargetReadbackQueue, FailedReadbackReleasesSlotAndPreservesCallerImage)
{
	CRenderTargetReadbackQueue Queue;
	const auto Failed = Queue.Allocate();
	Failed.m_pCompletion->Signal();
	EXPECT_EQ(Queue.Poll(Failed.m_Index, Failed.m_Generation), EState::FAILED);
	CImageInfo Image;
	Image.m_Width = 7;
	EXPECT_FALSE(Queue.Resolve(Failed.m_Index, Failed.m_Generation, Image));
	EXPECT_EQ(Image.m_Width, 7u);
	EXPECT_EQ(Queue.Poll(Failed.m_Index, Failed.m_Generation), EState::INVALID);
	EXPECT_EQ(Queue.Allocate().m_Index, Failed.m_Index);
}

TEST(RenderTargetReadbackQueue, PendingResolvePreservesResultAndCompletedImageTransfersOnce)
{
	CRenderTargetReadbackQueue Queue;
	const auto Pending = Queue.Allocate();
	CImageInfo Image;
	Image.m_Width = 7;
	EXPECT_FALSE(Queue.Resolve(Pending.m_Index, Pending.m_Generation, Image));
	EXPECT_EQ(Image.m_Width, 7u);
	Complete(Pending, 99);
	ASSERT_TRUE(Queue.Resolve(Pending.m_Index, Pending.m_Generation, Image));
	EXPECT_EQ(Image.m_pData[0], 99);
	EXPECT_FALSE(Queue.Resolve(Pending.m_Index, Pending.m_Generation, Image));
	EXPECT_EQ(Image.m_pData[0], 99);
	Image.Free();
}

TEST(RenderTargetReadbackQueue, ResetDoesNotReviveHandleFromPreviousDevice)
{
	CRenderTargetReadbackQueue Queue;
	const auto Old = Queue.Allocate();
	Complete(Old, 3);
	Queue.Clear();
	const auto Current = Queue.Allocate();
	EXPECT_EQ(Current.m_Index, Old.m_Index);
	EXPECT_NE(Current.m_Generation, Old.m_Generation);
	EXPECT_EQ(Queue.Poll(Old.m_Index, Old.m_Generation), EState::INVALID);
	Queue.Cancel(Old.m_Index, Old.m_Generation);
	EXPECT_EQ(Queue.Poll(Current.m_Index, Current.m_Generation), EState::PENDING);
}

TEST(RenderTargetReadbackQueue, OutOfOrderCompletionOnlyPublishesFinishedRequest)
{
	CRenderTargetReadbackQueue Queue;
	const auto First = Queue.Allocate();
	const auto Second = Queue.Allocate();
	Complete(Second, 8);
	EXPECT_EQ(Queue.Poll(First.m_Index, First.m_Generation), EState::PENDING);
	EXPECT_EQ(Queue.Poll(Second.m_Index, Second.m_Generation), EState::READY);
	EXPECT_EQ(Queue.Poll(-1, First.m_Generation), EState::INVALID);
	EXPECT_EQ(Queue.Poll(999, First.m_Generation), EState::INVALID);
	EXPECT_EQ(Queue.Poll(First.m_Index, 0), EState::INVALID);
}
