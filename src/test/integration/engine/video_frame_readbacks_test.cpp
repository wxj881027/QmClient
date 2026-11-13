#include <engine/client/video_frame_readbacks.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <new>
#include <optional>

namespace
{
	class CVideoFrameReadbackFlow : public testing::Test
	{
	protected:
		using SAllocation = CRenderTargetReadbackQueue::SAllocation;
		CRenderTargetReadbackQueue m_Queue;
		CVideoFrameReadbacks m_Frames;
		CImageInfo m_aImages[2];
		int m_IdleWaits = 0;
		int m_Prepares = 0;

		void TearDown() override
		{
			for(auto &Image : m_aImages)
				Image.Free();
		}
		SAllocation Allocate()
		{
			const auto Allocation = m_Queue.Allocate();
			if(Allocation.m_Index < 0)
				throw std::bad_alloc();
			return Allocation;
		}
		// 只替代 GPU 写入和完成信号，排队、预热、结算及像素转移调用生产模块。
		void Complete(const SAllocation &Allocation, std::optional<uint8_t> Pixel, bool Signal = true)
		{
			if(Pixel)
			{
				auto &Image = *Allocation.m_pImage;
				Image.m_pData = static_cast<uint8_t *>(std::malloc(4));
				if(Image.m_pData == nullptr)
					throw std::bad_alloc();
				Image.m_Width = Image.m_Height = 1;
				Image.m_Format = CImageInfo::FORMAT_RGBA;
				Image.m_pData[0] = *Pixel;
			}
			if(Signal)
				Allocation.m_pCompletion->Signal();
		}
		bool Enqueue(const SAllocation &Allocation, bool Signal = true)
		{
			return m_Frames.Enqueue(m_Queue, Allocation.m_Index, Allocation.m_Generation, Signal,
				m_aImages[0], [this] { ++m_IdleWaits; });
		}
		int Flush(bool Signal = true, int MaxImages = 2)
		{
			return m_Frames.Flush(m_Queue, Signal, m_aImages, MaxImages, [this] { ++m_Prepares; return true; });
		}
		static int Pixel(const CImageInfo &Image)
		{
			EXPECT_NE(Image.m_pData, nullptr);
			return Image.m_pData != nullptr ? Image.m_pData[0] : -1;
		}
		void ExpectReleased(const SAllocation &Allocation)
		{
			EXPECT_EQ(m_Queue.Poll(Allocation.m_Index, Allocation.m_Generation), IGraphics::ERenderTargetReadbackState::INVALID);
		}
	};
}

TEST_F(CVideoFrameReadbackFlow, SignaledPipelineWarmsUpThenReturnsFramesInCaptureOrder)
{
	const auto First = Allocate(), Second = Allocate(), Third = Allocate();
	ASSERT_TRUE(Enqueue(First));
	EXPECT_EQ(m_aImages[0].m_pData, nullptr);
	ASSERT_TRUE(Enqueue(Second));
	EXPECT_EQ(m_aImages[0].m_pData, nullptr);
	Complete(First, 11);
	Complete(Second, 22);
	Complete(Third, 33);
	ASSERT_TRUE(Enqueue(Third));
	EXPECT_EQ(Pixel(m_aImages[0]), 11);
	ExpectReleased(First);
	m_aImages[0].Free();
	ASSERT_EQ(Flush(), 2);
	EXPECT_EQ(Pixel(m_aImages[0]), 22);
	EXPECT_EQ(Pixel(m_aImages[1]), 33);
	EXPECT_EQ(m_IdleWaits, 0);
	EXPECT_EQ(m_Prepares, 1);
	EXPECT_TRUE(m_Frames.Empty());
}

TEST_F(CVideoFrameReadbackFlow, BackendWithoutCompletionSignalsReturnsPreviousFrameAfterIdle)
{
	const auto First = Allocate(), Second = Allocate();
	Complete(First, 17, false);
	ASSERT_TRUE(Enqueue(First, false));
	EXPECT_EQ(m_aImages[0].m_pData, nullptr);
	Complete(Second, 28, false);
	ASSERT_TRUE(Enqueue(Second, false));
	EXPECT_EQ(Pixel(m_aImages[0]), 17);
	EXPECT_EQ(m_IdleWaits, 2);
	m_aImages[0].Free();
	ASSERT_EQ(Flush(false), 1);
	EXPECT_EQ(Pixel(m_aImages[0]), 28);
	ExpectReleased(First);
	ExpectReleased(Second);
}

TEST_F(CVideoFrameReadbackFlow, RejectedRequestReportsFailureWithoutReplacingPendingFrames)
{
	const auto First = Allocate(), Second = Allocate();
	ASSERT_TRUE(Enqueue(First));
	ASSERT_TRUE(Enqueue(Second));
	Complete(First, 10);
	Complete(Second, 20);
	// 失败不能推进既有顺序；预先完成请求，回归时也不会留下无界等待。
	EXPECT_FALSE(Enqueue({}));
	EXPECT_EQ(m_IdleWaits, 0);
	ASSERT_EQ(Flush(), 2);
	EXPECT_EQ(Pixel(m_aImages[0]), 10);
	EXPECT_EQ(Pixel(m_aImages[1]), 20);
}

TEST_F(CVideoFrameReadbackFlow, UnsignaledPixelsAreReadOnlyAfterTheBackendIdleBoundary)
{
	const auto First = Allocate(), Second = Allocate();
	ASSERT_TRUE(m_Frames.Enqueue(m_Queue, First.m_Index, First.m_Generation, false, m_aImages[0], [&] {
		Complete(First, 41, false);
	}));
	ASSERT_TRUE(m_Frames.Enqueue(m_Queue, Second.m_Index, Second.m_Generation, false, m_aImages[0], [] {}));
	EXPECT_EQ(Pixel(m_aImages[0]), 41);
	m_aImages[0].Free();
	ASSERT_EQ(m_Frames.Flush(m_Queue, false, m_aImages, 2, [&] {
		Complete(Second, 42, false);
		return true;
	}),
		1);
	EXPECT_EQ(Pixel(m_aImages[0]), 42);
}

TEST_F(CVideoFrameReadbackFlow, FailedMatureFrameIsReportedInsteadOfBeingTreatedAsWarmup)
{
	const auto First = Allocate(), Second = Allocate(), Third = Allocate();
	ASSERT_TRUE(Enqueue(First));
	ASSERT_TRUE(Enqueue(Second));
	Complete(First, std::nullopt);
	Complete(Second, 22);
	Complete(Third, 33);
	EXPECT_FALSE(Enqueue(Third));
	EXPECT_EQ(m_aImages[0].m_pData, nullptr);
	ExpectReleased(First);
	ASSERT_EQ(Flush(), 2);
	EXPECT_EQ(Pixel(m_aImages[0]), 22);
	EXPECT_EQ(Pixel(m_aImages[1]), 33);
}

TEST_F(CVideoFrameReadbackFlow, FailedFinalFrameCannotTurnEarlierSuccessfulOutputIntoACompleteExport)
{
	const auto First = Allocate(), Second = Allocate();
	ASSERT_TRUE(Enqueue(First));
	ASSERT_TRUE(Enqueue(Second));
	Complete(First, 91);
	Complete(Second, std::nullopt);
	EXPECT_EQ(Flush(), -1);
	EXPECT_EQ(Pixel(m_aImages[0]), 91);
	EXPECT_EQ(m_aImages[1].m_pData, nullptr);
	ExpectReleased(First);
	ExpectReleased(Second);
	EXPECT_TRUE(m_Frames.Empty());
}

TEST_F(CVideoFrameReadbackFlow, FailedFirstFinalFrameDoesNotHideItsErrorBehindALaterSuccessfulFrame)
{
	const auto First = Allocate(), Second = Allocate();
	ASSERT_TRUE(Enqueue(First));
	ASSERT_TRUE(Enqueue(Second));
	Complete(First, std::nullopt);
	Complete(Second, 82);
	EXPECT_EQ(Flush(), -1);
	EXPECT_EQ(Pixel(m_aImages[0]), 82);
	EXPECT_EQ(m_aImages[1].m_pData, nullptr);
	ExpectReleased(First);
	ExpectReleased(Second);
}

TEST_F(CVideoFrameReadbackFlow, UnsignaledReadbackWithNoPixelsIsAFailureAfterIdle)
{
	const auto First = Allocate(), Second = Allocate();
	ASSERT_TRUE(Enqueue(First, false));
	Complete(Second, 64, false);
	EXPECT_FALSE(Enqueue(Second, false));
	EXPECT_EQ(m_aImages[0].m_pData, nullptr);
	ExpectReleased(First);
	ASSERT_EQ(Flush(false), 1);
	EXPECT_EQ(Pixel(m_aImages[0]), 64);
}

TEST_F(CVideoFrameReadbackFlow, UnsignaledFinalReadbackFailureIsNotAnEmptySuccessfulFlush)
{
	const auto Request = Allocate();
	ASSERT_TRUE(Enqueue(Request, false));
	EXPECT_EQ(Flush(false), -1);
	EXPECT_EQ(m_aImages[0].m_pData, nullptr);
	ExpectReleased(Request);
	EXPECT_TRUE(m_Frames.Empty());
}

TEST_F(CVideoFrameReadbackFlow, InsufficientOutputCapacityStillReleasesAllCompletedRequests)
{
	const auto First = Allocate(), Second = Allocate();
	ASSERT_TRUE(Enqueue(First));
	ASSERT_TRUE(Enqueue(Second));
	Complete(First, 31);
	Complete(Second, 32);
	EXPECT_EQ(Flush(true, 1), -1);
	EXPECT_EQ(Pixel(m_aImages[0]), 31);
	EXPECT_EQ(m_aImages[1].m_pData, nullptr);
	ExpectReleased(First);
	ExpectReleased(Second);
	EXPECT_TRUE(m_Frames.Empty());
}

TEST_F(CVideoFrameReadbackFlow, DeviceResetAndRepeatedEmptyFlushDoNotReviveOldRequests)
{
	const auto Old = Allocate();
	ASSERT_TRUE(Enqueue(Old));
	// 替代后端已空闲且设备完成清理的边界，不在复位后继续写旧指针。
	m_Queue.Clear();
	m_Frames.Reset();
	EXPECT_EQ(Flush(), 0);
	EXPECT_EQ(Flush(), 0);
	EXPECT_EQ(m_Prepares, 0);
	const auto Current = Allocate();
	EXPECT_EQ(Current.m_Index, Old.m_Index);
	EXPECT_NE(Current.m_Generation, Old.m_Generation);
	ASSERT_TRUE(Enqueue(Current));
	Complete(Current, 77);
	ASSERT_EQ(Flush(), 1);
	EXPECT_EQ(Pixel(m_aImages[0]), 77);
}

TEST_F(CVideoFrameReadbackFlow, RejectedFlushCancelsCompletedRequestsWithoutTransferringPartialOutput)
{
	const auto First = Allocate(), Second = Allocate();
	ASSERT_TRUE(Enqueue(First));
	ASSERT_TRUE(Enqueue(Second));
	Complete(First, 61);
	Complete(Second, 62);
	EXPECT_EQ(m_Frames.Flush(m_Queue, true, m_aImages, 2, [] { return false; }), -1);
	EXPECT_EQ(m_aImages[0].m_pData, nullptr);
	EXPECT_EQ(m_aImages[1].m_pData, nullptr);
	ExpectReleased(First);
	ExpectReleased(Second);
	EXPECT_TRUE(m_Frames.Empty());
}

TEST_F(CVideoFrameReadbackFlow, RejectedFlushKeepsPendingStorageAliveUntilTheBackendStopsBorrowingIt)
{
	const auto Request = Allocate();
	ASSERT_TRUE(Enqueue(Request, false));
	EXPECT_EQ(m_Frames.Flush(m_Queue, false, m_aImages, 2, [] { return false; }), -1);
	EXPECT_TRUE(m_Frames.Empty());
	const auto Other = Allocate();
	EXPECT_NE(Other.m_Index, Request.m_Index);
	// 取消后后端仍可写入先前借用的地址，只有完成信号到达后才能复用该槽位。
	Complete(Request, 72);
	const auto Reused = Allocate();
	EXPECT_EQ(Reused.m_Index, Request.m_Index);
	EXPECT_NE(Reused.m_Generation, Request.m_Generation);
	EXPECT_EQ(m_Queue.Poll(Reused.m_Index, Reused.m_Generation), IGraphics::ERenderTargetReadbackState::PENDING);
	EXPECT_EQ(m_aImages[0].m_pData, nullptr);
}
