#include <engine/client/render_target_readback_queue.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>

#include <cstdlib>
#include <memory>

namespace
{
	using EState = IGraphics::ERenderTargetReadbackState;

	std::unique_ptr<CSemaphore> FailNativeCreation()
	{
		return CSemaphore::TryCreate([](SEMAPHORE *) { return false; });
	}
}

TEST(RenderTargetCompletionPrepareIntegration, NativeInitializationFailureLeavesNoRequestAndRetryCanComplete)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		CRenderTargetReadbackQueue Queue;
		const auto Failed = Queue.Allocate(FailNativeCreation);
		EXPECT_EQ(Failed.m_Index, -1);
		EXPECT_EQ(Failed.m_pCompletion, nullptr);
		EXPECT_EQ(Queue.Poll(0, 1), EState::INVALID);
		const auto Retry = Queue.Allocate();
		ASSERT_EQ(Retry.m_Index, 0);
		EXPECT_EQ(Retry.m_Generation, 1u);
		Retry.m_pCompletion->Signal();
		EXPECT_EQ(Queue.Poll(Retry.m_Index, Retry.m_Generation), EState::FAILED);
		CImageInfo Image;
		EXPECT_FALSE(Queue.Resolve(Retry.m_Index, Retry.m_Generation, Image));
		EXPECT_EQ(Queue.Allocate().m_Index, 0);
	}));
}

TEST(RenderTargetCompletionPrepareIntegration, NativeFailurePreservesAnEarlierPendingCompletion)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		CRenderTargetReadbackQueue Queue;
		const auto First = Queue.Allocate();
		ASSERT_EQ(First.m_Index, 0);
		EXPECT_EQ(Queue.Allocate(FailNativeCreation).m_Index, -1);
		EXPECT_EQ(Queue.Poll(First.m_Index, First.m_Generation), EState::PENDING);
		First.m_pCompletion->Signal();
		EXPECT_EQ(Queue.Poll(First.m_Index, First.m_Generation), EState::FAILED);
		const auto Next = Queue.Allocate();
		EXPECT_EQ(Next.m_Index, 1);
		EXPECT_EQ(Next.m_Generation, First.m_Generation + 1);
	}));
}

TEST(RenderTargetCompletionPrepareIntegration, NativeFailureDoesNotRecycleACanceledRequestBeforeItsSignal)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		CRenderTargetReadbackQueue Queue;
		const auto Old = Queue.Allocate();
		ASSERT_EQ(Old.m_Index, 0);
		Queue.Cancel(Old.m_Index, Old.m_Generation);
		EXPECT_EQ(Queue.Allocate(FailNativeCreation).m_Index, -1);
		const auto Separate = Queue.Allocate();
		ASSERT_EQ(Separate.m_Index, 1);
		EXPECT_NE(Separate.m_pImage, Old.m_pImage);
		Old.m_pCompletion->Signal();
		const auto Reused = Queue.Allocate();
		EXPECT_EQ(Reused.m_Index, Old.m_Index);
		EXPECT_NE(Reused.m_Generation, Old.m_Generation);
		EXPECT_EQ(Queue.Poll(Reused.m_Index, Reused.m_Generation), EState::PENDING);
		EXPECT_EQ(Queue.Poll(Separate.m_Index, Separate.m_Generation), EState::PENDING);
	}));
}

TEST(RenderTargetCompletionPrepareIntegration, FactoryCreatedNativeSemaphorePublishesTheQueueImageOnce)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		CRenderTargetReadbackQueue Queue;
		int Calls = 0;
		const auto Request = Queue.Allocate([&] {
			return CSemaphore::TryCreate([&](SEMAPHORE *pNative) {
				++Calls;
				return sphore_try_init(pNative);
			});
		});
		ASSERT_EQ(Request.m_Index, 0);
		EXPECT_EQ(Calls, 1);
		auto *pImage = Request.m_pImage;
		pImage->m_pData = static_cast<uint8_t *>(malloc(4));
		ASSERT_NE(pImage->m_pData, nullptr);
		pImage->m_Width = 1;
		pImage->m_Height = 1;
		pImage->m_Format = CImageInfo::FORMAT_RGBA;
		pImage->m_pData[0] = 42;
		pImage->m_pData[3] = 73;
		Request.m_pCompletion->Signal();
		EXPECT_EQ(Queue.Poll(Request.m_Index, Request.m_Generation), EState::READY);
		CImageInfo Result;
		ASSERT_TRUE(Queue.Resolve(Request.m_Index, Request.m_Generation, Result));
		EXPECT_EQ(Result.m_pData[0], 42);
		EXPECT_EQ(Result.m_pData[3], 73);
		EXPECT_FALSE(Queue.Resolve(Request.m_Index, Request.m_Generation, Result));
		Result.Free();
	}));
}
