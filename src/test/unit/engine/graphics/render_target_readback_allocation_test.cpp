#include <engine/client/render_target_readback_queue.h>

#include <gtest/gtest.h>
#include <test/support/tracked_allocation_resource.h>

#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace
{
	using EState = IGraphics::ERenderTargetReadbackState;
	using SAllocation = CRenderTargetReadbackQueue::SAllocation;

	void ExpectInvalid(const SAllocation &Allocation)
	{
		EXPECT_EQ(Allocation.m_Index, -1);
		EXPECT_EQ(Allocation.m_Generation, 0u);
		EXPECT_EQ(Allocation.m_pImage, nullptr);
		EXPECT_EQ(Allocation.m_pCompletion, nullptr);
	}

	class RenderTargetReadbackAllocation : public ::testing::Test
	{
	protected:
		CRenderTargetReadbackQueue m_Queue;

		template<typename TFactory>
		void ExpectFactoryFailureThenRetry(TFactory &&Factory)
		{
			ExpectInvalid(m_Queue.Allocate(std::forward<TFactory>(Factory)));
			const auto Retry = m_Queue.Allocate();
			ASSERT_EQ(Retry.m_Index, 0);
			EXPECT_EQ(Retry.m_Generation, 1u);
			EXPECT_EQ(m_Queue.Poll(Retry.m_Index, Retry.m_Generation), EState::PENDING);
		}
	};

	class RenderTargetReadbackPoolAllocation : public ::testing::Test
	{
	protected:
		CTrackedAllocationResource m_Memory;

		void ExpectGrowthFailureThenRetry()
		{
			{
				CRenderTargetReadbackQueue Queue(&m_Memory);
				const auto First = Queue.Allocate();
				ASSERT_EQ(First.m_Index, 0);
				m_Memory.m_FailAt = m_Memory.m_Allocations + 1;
				int ExpectedIndex = 1;
				bool Failed = false;
				// 以实际分配观察点找扩容，不假设各标准库的 deque 页大小。
				for(int Attempt = 0; Attempt < 4096; ++Attempt)
				{
					const auto Current = Queue.Allocate();
					if(Current.m_Index < 0)
					{
						ExpectInvalid(Current);
						Failed = true;
						break;
					}
					ASSERT_EQ(Current.m_Index, ExpectedIndex);
					++ExpectedIndex;
				}
				ASSERT_TRUE(Failed);
				EXPECT_EQ(Queue.Poll(First.m_Index, First.m_Generation), EState::PENDING);
				EXPECT_EQ(First.m_pImage->m_pData, nullptr);
				m_Memory.m_FailAt = -1;
				const auto Retry = Queue.Allocate();
				ASSERT_EQ(Retry.m_Index, ExpectedIndex);
				EXPECT_EQ(Retry.m_Generation, static_cast<uint32_t>(ExpectedIndex + 1));
				EXPECT_EQ(Queue.Poll(Retry.m_Index, Retry.m_Generation), EState::PENDING);
				First.m_pCompletion->Signal();
				EXPECT_EQ(Queue.Poll(First.m_Index, First.m_Generation), EState::FAILED);
				CImageInfo Image;
				EXPECT_FALSE(Queue.Resolve(First.m_Index, First.m_Generation, Image));
				EXPECT_EQ(Queue.Allocate().m_Index, First.m_Index);
			}
			EXPECT_EQ(m_Memory.m_Live, 0);
		}
	};
}

TEST_F(RenderTargetReadbackAllocation, CompletionAllocationFailureReturnsInvalidAndRetryUsesFirstSlot)
{
	ExpectFactoryFailureThenRetry([]() -> std::unique_ptr<CSemaphore> { throw std::bad_alloc(); });
}

TEST_F(RenderTargetReadbackAllocation, CompletionLengthFailureDoesNotConsumeASlotOrGeneration)
{
	ExpectFactoryFailureThenRetry([]() -> std::unique_ptr<CSemaphore> { throw std::length_error("completion preparation"); });
}

TEST_F(RenderTargetReadbackAllocation, UnknownCompletionFailureReturnsInvalidAndCanRetry)
{
	ExpectFactoryFailureThenRetry([]() -> std::unique_ptr<CSemaphore> { throw 7; });
}

TEST_F(RenderTargetReadbackAllocation, MissingCompletionDoesNotPublishAPendingRequest)
{
	ExpectFactoryFailureThenRetry([] { return std::unique_ptr<CSemaphore>{}; });
}

TEST_F(RenderTargetReadbackAllocation, CompletionFailurePreservesAnEarlierRequestsPointersAndCompletion)
{
	const auto First = m_Queue.Allocate();
	ASSERT_EQ(First.m_Index, 0);
	ExpectInvalid(m_Queue.Allocate([]() -> std::unique_ptr<CSemaphore> { throw std::bad_alloc(); }));
	EXPECT_EQ(m_Queue.Poll(First.m_Index, First.m_Generation), EState::PENDING);
	First.m_pCompletion->Signal();
	EXPECT_EQ(m_Queue.Poll(First.m_Index, First.m_Generation), EState::FAILED);
	CImageInfo Image;
	Image.m_Width = 7;
	EXPECT_FALSE(m_Queue.Resolve(First.m_Index, First.m_Generation, Image));
	EXPECT_EQ(Image.m_Width, 7u);
	const auto Retry = m_Queue.Allocate();
	EXPECT_EQ(Retry.m_Index, First.m_Index);
	EXPECT_NE(Retry.m_Generation, First.m_Generation);
	EXPECT_EQ(m_Queue.Poll(Retry.m_Index, Retry.m_Generation), EState::PENDING);
}

TEST_F(RenderTargetReadbackAllocation, ReusingReleasedSlotNeedsNoNewCompletion)
{
	const auto First = m_Queue.Allocate();
	ASSERT_EQ(First.m_Index, 0);
	First.m_pCompletion->Signal();
	m_Queue.Cancel(First.m_Index, First.m_Generation);
	int Calls = 0;
	const auto Reused = m_Queue.Allocate([&]() -> std::unique_ptr<CSemaphore> {
		++Calls;
		throw std::bad_alloc();
	});
	EXPECT_EQ(Calls, 0);
	EXPECT_EQ(Reused.m_Index, First.m_Index);
	EXPECT_EQ(Reused.m_pCompletion, First.m_pCompletion);
	EXPECT_EQ(m_Queue.Poll(Reused.m_Index, Reused.m_Generation), EState::PENDING);
	EXPECT_EQ(m_Queue.Poll(First.m_Index, First.m_Generation), EState::INVALID);
}

TEST_F(RenderTargetReadbackAllocation, CanceledPendingSlotSurvivesFailedAllocationUntilItsSignal)
{
	const auto Canceled = m_Queue.Allocate();
	ASSERT_EQ(Canceled.m_Index, 0);
	m_Queue.Cancel(Canceled.m_Index, Canceled.m_Generation);
	ExpectInvalid(m_Queue.Allocate([] { return std::unique_ptr<CSemaphore>{}; }));
	const auto Separate = m_Queue.Allocate();
	ASSERT_EQ(Separate.m_Index, 1);
	EXPECT_NE(Separate.m_pImage, Canceled.m_pImage);
	EXPECT_NE(Separate.m_pCompletion, Canceled.m_pCompletion);
	Canceled.m_pCompletion->Signal();
	int Calls = 0;
	const auto Reused = m_Queue.Allocate([&]() -> std::unique_ptr<CSemaphore> {
		++Calls;
		return {};
	});
	EXPECT_EQ(Calls, 0);
	EXPECT_EQ(Reused.m_Index, Canceled.m_Index);
	EXPECT_EQ(m_Queue.Poll(Reused.m_Index, Reused.m_Generation), EState::PENDING);
	EXPECT_EQ(m_Queue.Poll(Separate.m_Index, Separate.m_Generation), EState::PENDING);
}

TEST_F(RenderTargetReadbackAllocation, ClearAfterFailedPreparationKeepsOldHandlesInvalid)
{
	const auto Old = m_Queue.Allocate();
	ASSERT_EQ(Old.m_Index, 0);
	ExpectInvalid(m_Queue.Allocate([]() -> std::unique_ptr<CSemaphore> { throw std::bad_alloc(); }));
	m_Queue.Clear();
	const auto Current = m_Queue.Allocate();
	EXPECT_EQ(Current.m_Index, Old.m_Index);
	EXPECT_NE(Current.m_Generation, Old.m_Generation);
	m_Queue.Cancel(Old.m_Index, Old.m_Generation);
	EXPECT_EQ(m_Queue.Poll(Current.m_Index, Current.m_Generation), EState::PENDING);
}

TEST_F(RenderTargetReadbackPoolAllocation, PoolAllocationFailurePreservesPendingRequestAndAllowsRetry)
{
	ExpectGrowthFailureThenRetry();
}

TEST_F(RenderTargetReadbackPoolAllocation, NullPoolAllocationReturnsInvalidAndAllowsRetry)
{
	m_Memory.m_ReturnNull = true;
	ExpectGrowthFailureThenRetry();
}

TEST_F(RenderTargetReadbackPoolAllocation, UnknownPoolAllocationFailureReturnsInvalidAndAllowsRetry)
{
	m_Memory.m_ThrowUnknown = true;
	ExpectGrowthFailureThenRetry();
}

TEST_F(RenderTargetReadbackPoolAllocation, ReusingReleasedSlotDoesNotAllocatePoolStorage)
{
	{
		CRenderTargetReadbackQueue Queue(&m_Memory);
		const auto First = Queue.Allocate();
		ASSERT_EQ(First.m_Index, 0);
		First.m_pCompletion->Signal();
		Queue.Cancel(First.m_Index, First.m_Generation);
		const int Before = m_Memory.m_Allocations;
		m_Memory.m_FailAt = Before + 1;
		const auto Reused = Queue.Allocate();
		EXPECT_EQ(Reused.m_Index, First.m_Index);
		EXPECT_EQ(m_Memory.m_Allocations, Before);
		EXPECT_EQ(Queue.Poll(Reused.m_Index, Reused.m_Generation), EState::PENDING);
	}
	EXPECT_EQ(m_Memory.m_Live, 0);
}
