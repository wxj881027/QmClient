#include <engine/client/graphics_readback_completion.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <cstdlib>

namespace
{
	class CGraphicsReadbackCompletionTest : public testing::Test
	{
	protected:
		CSemaphore m_FirstSignal, m_SecondSignal;
		CImageInfo m_FirstImage, m_SecondImage;
		void SetUp() override
		{
			m_FirstImage.m_pData = static_cast<uint8_t *>(std::malloc(4));
			m_SecondImage.m_pData = static_cast<uint8_t *>(std::malloc(4));
			ASSERT_NE(m_FirstImage.m_pData, nullptr);
			ASSERT_NE(m_SecondImage.m_pData, nullptr);
			m_FirstImage.m_pData[0] = 31;
			m_SecondImage.m_pData[0] = 42;
		}
		void TearDown() override
		{
			m_FirstImage.Free();
			m_SecondImage.Free();
		}
	};
}

TEST_F(CGraphicsReadbackCompletionTest, ScopeCompletionSignalsOnceAndKeepsPixelsWithTheCaller)
{
	{
		CGraphicsReadbackCompletion Completion(&m_FirstImage, &m_FirstSignal);
		EXPECT_TRUE(Completion);
		EXPECT_EQ(m_FirstSignal.GetApproximateValue(), 0);
	}
	EXPECT_EQ(m_FirstSignal.GetApproximateValue(), 1);
	ASSERT_NE(m_FirstImage.m_pData, nullptr);
	EXPECT_EQ(m_FirstImage.m_pData[0], 31);
}

TEST_F(CGraphicsReadbackCompletionTest, MovingTheOwnerDoesNotPublishBeforeTheNewOwnerCompletes)
{
	{
		CGraphicsReadbackCompletion First(&m_FirstImage, &m_FirstSignal);
		CGraphicsReadbackCompletion Second(std::move(First));
		EXPECT_FALSE(First);
		First.Complete();
		EXPECT_EQ(m_FirstSignal.GetApproximateValue(), 0);
		EXPECT_EQ(Second.Image(), &m_FirstImage);
	}
	EXPECT_EQ(m_FirstSignal.GetApproximateValue(), 1);
}

TEST_F(CGraphicsReadbackCompletionTest, ReplacingAnOwnerAbortsItsOldOutputBeforeTakingTheNewOne)
{
	{
		CGraphicsReadbackCompletion First(&m_FirstImage, &m_FirstSignal);
		CGraphicsReadbackCompletion Second(&m_SecondImage, &m_SecondSignal);
		First = std::move(Second);
		EXPECT_FALSE(Second);
		EXPECT_EQ(m_FirstSignal.GetApproximateValue(), 1);
		EXPECT_EQ(m_FirstImage.m_pData, nullptr);
		EXPECT_EQ(m_SecondSignal.GetApproximateValue(), 0);
	}
	EXPECT_EQ(m_FirstSignal.GetApproximateValue(), 1);
	EXPECT_EQ(m_SecondSignal.GetApproximateValue(), 1);
	EXPECT_NE(m_SecondImage.m_pData, nullptr);
}

TEST_F(CGraphicsReadbackCompletionTest, RepeatedAbortPublishesOneFailureAndReleasesPartialPixels)
{
	{
		CGraphicsReadbackCompletion Completion(&m_FirstImage, &m_FirstSignal);
		Completion.Abort();
		Completion.Abort();
		Completion.Complete();
		EXPECT_FALSE(Completion);
	}
	EXPECT_EQ(m_FirstSignal.GetApproximateValue(), 1);
	EXPECT_EQ(m_FirstImage.m_pData, nullptr);
}

TEST_F(CGraphicsReadbackCompletionTest, CompletionWithoutASemaphorePreservesTheBorrowedImage)
{
	{
		CGraphicsReadbackCompletion Completion(&m_FirstImage, nullptr);
		EXPECT_TRUE(Completion);
		Completion.Complete();
		Completion.Abort();
		EXPECT_FALSE(Completion);
	}
	EXPECT_NE(m_FirstImage.m_pData, nullptr);
}

TEST_F(CGraphicsReadbackCompletionTest, ShutdownOwnershipTransferAndAbortDoNotNeedCppAllocation)
{
	bool Triggered;
	{
		CPredictionAllocationFailure Fault(0);
		CGraphicsReadbackCompletion First(&m_FirstImage, &m_FirstSignal);
		CGraphicsReadbackCompletion Second(std::move(First));
		Second.Abort();
		Triggered = Fault.Triggered();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(m_FirstSignal.GetApproximateValue(), 1);
	EXPECT_EQ(m_FirstImage.m_pData, nullptr);
}
