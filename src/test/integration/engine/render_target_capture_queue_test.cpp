#include <engine/client/backend/screenshot_image.h>
#include <engine/client/render_target_readback_queue.h>

#include <gtest/gtest.h>

#include <array>
#include <cstring>

namespace
{
	using EState = IGraphics::ERenderTargetReadbackState;
	using EOrder = screenshot_image::ERowOrder;
	using EAlpha = screenshot_image::EAlphaMode;
	class RenderTargetCaptureQueue : public ::testing::Test
	{
	protected:
		CRenderTargetReadbackQueue m_Queue;
		CImageInfo m_Image;
		~RenderTargetCaptureQueue() override { m_Image.Free(); }
	};
}

TEST_F(RenderTargetCaptureQueue, AllocationFailureSignalsFailedAndTheSlotCanBeReused)
{
	const auto Failed = m_Queue.Allocate();
	int Reads = 0;
	EXPECT_FALSE(screenshot_image::CaptureRgba(*Failed.m_pImage, 2, 2, EOrder::BOTTOM_UP, [&](uint8_t *, size_t) { ++Reads; return true; }, [](size_t) -> void * { return nullptr; }, EAlpha::PRESERVE));
	Failed.m_pCompletion->Signal();
	EXPECT_EQ(Reads, 0);
	EXPECT_EQ(m_Queue.Poll(Failed.m_Index, Failed.m_Generation), EState::FAILED);
	EXPECT_FALSE(m_Queue.Resolve(Failed.m_Index, Failed.m_Generation, m_Image));
	EXPECT_EQ(m_Image.m_pData, nullptr);
	const auto Next = m_Queue.Allocate();
	EXPECT_EQ(Next.m_Index, Failed.m_Index);
	EXPECT_NE(Next.m_Generation, Failed.m_Generation);
	EXPECT_EQ(m_Queue.Poll(Next.m_Index, Next.m_Generation), EState::PENDING);
}

TEST_F(RenderTargetCaptureQueue, SuccessfulReadbackResolvesTopDownPixelsWithOriginalAlpha)
{
	const auto Pending = m_Queue.Allocate();
	const std::array<uint8_t, 8> aInput{1, 2, 3, 0, 4, 5, 6, 127};
	ASSERT_TRUE(screenshot_image::CaptureRgba(*Pending.m_pImage, 1, 2, EOrder::BOTTOM_UP, [&](uint8_t *pData, size_t Bytes) {
			if(Bytes != aInput.size())
				return false;
			std::memcpy(pData, aInput.data(), Bytes);
			return true; }, std::malloc, EAlpha::PRESERVE));
	Pending.m_pCompletion->Signal();
	ASSERT_EQ(m_Queue.Poll(Pending.m_Index, Pending.m_Generation), EState::READY);
	ASSERT_TRUE(m_Queue.Resolve(Pending.m_Index, Pending.m_Generation, m_Image));
	ASSERT_NE(m_Image.m_pData, nullptr);
	const std::array<uint8_t, 8> aExpected{4, 5, 6, 127, 1, 2, 3, 0};
	ASSERT_EQ(m_Image.DataSize(), aExpected.size());
	EXPECT_EQ(std::memcmp(m_Image.m_pData, aExpected.data(), aExpected.size()), 0);
	EXPECT_EQ(m_Queue.Poll(Pending.m_Index, Pending.m_Generation), EState::INVALID);
}

TEST_F(RenderTargetCaptureQueue, PartialDeviceReadCannotBecomeAReadyImage)
{
	const auto Pending = m_Queue.Allocate();
	EXPECT_FALSE(screenshot_image::CaptureRgba(*Pending.m_pImage, 1, 1, EOrder::BOTTOM_UP, [](uint8_t *pData, size_t) { pData[0] = 42; return false; }, std::malloc, EAlpha::PRESERVE));
	Pending.m_pCompletion->Signal();
	EXPECT_EQ(m_Queue.Poll(Pending.m_Index, Pending.m_Generation), EState::FAILED);
	EXPECT_FALSE(m_Queue.Resolve(Pending.m_Index, Pending.m_Generation, m_Image));
	EXPECT_EQ(m_Image.m_pData, nullptr);
}

TEST_F(RenderTargetCaptureQueue, CanceledFailedReadbackDoesNotReviveTheOldGeneration)
{
	const auto Canceled = m_Queue.Allocate();
	m_Queue.Cancel(Canceled.m_Index, Canceled.m_Generation);
	EXPECT_FALSE(screenshot_image::CaptureRgba(*Canceled.m_pImage, 1, 1, EOrder::BOTTOM_UP, [](uint8_t *, size_t) { return true; }, [](size_t) -> void * { return nullptr; }, EAlpha::PRESERVE));
	Canceled.m_pCompletion->Signal();
	EXPECT_EQ(m_Queue.Poll(Canceled.m_Index, Canceled.m_Generation), EState::INVALID);
	const auto Next = m_Queue.Allocate();
	EXPECT_EQ(Next.m_Index, Canceled.m_Index);
	EXPECT_NE(Next.m_Generation, Canceled.m_Generation);
	EXPECT_EQ(m_Queue.Poll(Next.m_Index, Next.m_Generation), EState::PENDING);
}
