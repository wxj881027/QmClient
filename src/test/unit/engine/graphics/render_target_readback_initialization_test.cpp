#include <engine/client/render_target_readback_queue.h>

#include <gtest/gtest.h>
#include <test/support/tracked_allocation_resource.h>

#include <memory>

namespace
{
	using EState = IGraphics::ERenderTargetReadbackState;

	class RenderTargetReadbackInitialization : public ::testing::Test
	{
	protected:
		CTrackedAllocationResource m_Memory;

		void ExpectInitialAllocationFailureThenRetry(bool ClearAfterFailure = false)
		{
			{
				m_Memory.m_FailAt = 1;
				CRenderTargetReadbackQueue Queue(&m_Memory);
				EXPECT_EQ(m_Memory.m_Allocations, 0);
				const auto Failed = Queue.Allocate();
				EXPECT_EQ(Failed.m_Index, -1);
				EXPECT_EQ(Failed.m_pImage, nullptr);
				EXPECT_EQ(Failed.m_pCompletion, nullptr);
				EXPECT_EQ(Queue.Poll(0, 1), EState::INVALID);
				ASSERT_EQ(m_Memory.m_Allocations, 1);
				if(ClearAfterFailure)
					Queue.Clear();
				m_Memory.m_FailAt = -1;
				const auto Retry = Queue.Allocate();
				ASSERT_EQ(Retry.m_Index, 0);
				EXPECT_EQ(Retry.m_Generation, 1u);
				EXPECT_EQ(Queue.Poll(Retry.m_Index, Retry.m_Generation), EState::PENDING);
			}
			EXPECT_EQ(m_Memory.m_Live, 0);
		}
	};
}

TEST_F(RenderTargetReadbackInitialization, ConstructingPollingAndClearingUnusedQueueNeverAllocatesStorage)
{
	{
		m_Memory.m_FailAt = 1;
		CRenderTargetReadbackQueue Queue(&m_Memory);
		EXPECT_EQ(Queue.Poll(0, 1), EState::INVALID);
		Queue.Cancel(0, 1);
		CImageInfo Image;
		Image.m_Width = 9;
		EXPECT_FALSE(Queue.Resolve(0, 1, Image));
		EXPECT_EQ(Image.m_Width, 9u);
		Queue.Clear();
		Queue.Clear();
		EXPECT_EQ(m_Memory.m_Allocations, 0);
	}
	EXPECT_EQ(m_Memory.m_Live, 0);
}

TEST_F(RenderTargetReadbackInitialization, MissingCompletionDoesNotInitializeQueueStorage)
{
	{
		m_Memory.m_FailAt = 1;
		CRenderTargetReadbackQueue Queue(&m_Memory);
		const auto Failed = Queue.Allocate([] { return std::unique_ptr<CSemaphore>{}; });
		EXPECT_EQ(Failed.m_Index, -1);
		EXPECT_EQ(Queue.Poll(0, 1), EState::INVALID);
		EXPECT_EQ(m_Memory.m_Allocations, 0);
	}
	EXPECT_EQ(m_Memory.m_Live, 0);
}

TEST_F(RenderTargetReadbackInitialization, InitialContainerAllocationFailureReturnsInvalidAndCanRetry)
{
	ExpectInitialAllocationFailureThenRetry();
}

TEST_F(RenderTargetReadbackInitialization, NullInitialContainerAllocationReturnsInvalidAndCanRetry)
{
	m_Memory.m_ReturnNull = true;
	ExpectInitialAllocationFailureThenRetry();
}

TEST_F(RenderTargetReadbackInitialization, UnknownInitialContainerFailureReturnsInvalidAndCanRetry)
{
	m_Memory.m_ThrowUnknown = true;
	ExpectInitialAllocationFailureThenRetry();
}

TEST_F(RenderTargetReadbackInitialization, ClearingFailedInitializationKeepsRetryAtFirstGeneration)
{
	ExpectInitialAllocationFailureThenRetry(true);
}
