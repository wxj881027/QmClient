#include <engine/client/backend/screenshot_image.h>

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace
{
	using EOrder = screenshot_image::ERowOrder;
	using EAlpha = screenshot_image::EAlphaMode;
	class RenderTargetCaptureImage : public ::testing::Test
	{
	protected:
		CImageInfo m_Image;
		~RenderTargetCaptureImage() override { m_Image.Free(); }
		template<size_t Size>
		bool Capture(size_t Width, size_t Height, const std::array<uint8_t, Size> &aPixels, EOrder Order = EOrder::BOTTOM_UP, EAlpha Alpha = EAlpha::PRESERVE)
		{
			return screenshot_image::CaptureRgba(m_Image, Width, Height, Order, [&](uint8_t *pData, size_t Bytes) {
				if(Bytes != aPixels.size())
					return false;
				std::memcpy(pData, aPixels.data(), Bytes);
				return true; }, std::malloc, Alpha);
		}
		template<size_t Size>
		void ExpectPixels(const std::array<uint8_t, Size> &aPixels)
		{
			ASSERT_NE(m_Image.m_pData, nullptr);
			ASSERT_EQ(m_Image.DataSize(), aPixels.size());
			EXPECT_EQ(std::memcmp(m_Image.m_pData, aPixels.data(), aPixels.size()), 0);
		}
		void ExpectEmpty()
		{
			EXPECT_EQ(m_Image.m_pData, nullptr);
			EXPECT_EQ(m_Image.m_Width, 0u);
			EXPECT_EQ(m_Image.m_Height, 0u);
			EXPECT_EQ(m_Image.m_Format, CImageInfo::FORMAT_UNDEFINED);
		}
	};
}

TEST_F(RenderTargetCaptureImage, BottomUpRowsPreserveEverySourceAlpha)
{
	const std::array<uint8_t, 16> aInput{1, 2, 3, 0, 4, 5, 6, 127, 7, 8, 9, 64, 10, 11, 12, 255};
	ASSERT_TRUE(Capture(2, 2, aInput));
	ExpectPixels(std::array<uint8_t, 16>{7, 8, 9, 64, 10, 11, 12, 255, 1, 2, 3, 0, 4, 5, 6, 127});
}

TEST_F(RenderTargetCaptureImage, OddHeightKeepsTheMiddleRowsOriginalAlpha)
{
	const std::array<uint8_t, 12> aInput{1, 2, 3, 0, 4, 5, 6, 127, 7, 8, 9, 255};
	ASSERT_TRUE(Capture(1, 3, aInput));
	ExpectPixels(std::array<uint8_t, 12>{7, 8, 9, 255, 4, 5, 6, 127, 1, 2, 3, 0});
}

TEST_F(RenderTargetCaptureImage, TopDownSingleRowKeepsPixelAndAlphaOrder)
{
	const std::array<uint8_t, 8> aInput{1, 2, 3, 0, 4, 5, 6, 127};
	ASSERT_TRUE(Capture(2, 1, aInput, EOrder::TOP_DOWN));
	ExpectPixels(aInput);
}

TEST_F(RenderTargetCaptureImage, ExplicitOpaquePolicyStillReplacesAlphaWithFullOpacity)
{
	const std::array<uint8_t, 4> aInput{1, 2, 3, 0};
	ASSERT_TRUE(Capture(1, 1, aInput, EOrder::TOP_DOWN, EAlpha::OPAQUE));
	ExpectPixels(std::array<uint8_t, 4>{1, 2, 3, 255});
}

TEST_F(RenderTargetCaptureImage, AllocationFailureSkipsTheDeviceReadAndPublishesNoImage)
{
	int Reads = 0;
	EXPECT_FALSE(screenshot_image::CaptureRgba(m_Image, 2, 2, EOrder::BOTTOM_UP, [&](uint8_t *, size_t) { ++Reads; return true; }, [](size_t) -> void * { return nullptr; }, EAlpha::PRESERVE));
	EXPECT_EQ(Reads, 0);
	ExpectEmpty();
}

TEST_F(RenderTargetCaptureImage, UnaddressableScratchRowRejectsBeforeAllocationOrDeviceRead)
{
	int Allocations = 0;
	int Reads = 0;
	const size_t Width = (size_t)std::numeric_limits<ptrdiff_t>::max() / 4;
	EXPECT_FALSE(screenshot_image::CaptureRgba(m_Image, Width, 1, EOrder::BOTTOM_UP, [&](uint8_t *, size_t) { ++Reads; return true; }, [&](size_t) -> void * { ++Allocations; return nullptr; }, EAlpha::PRESERVE));
	EXPECT_EQ(Allocations, 0);
	EXPECT_EQ(Reads, 0);
	ExpectEmpty();
}

TEST_F(RenderTargetCaptureImage, ThrowingDeviceReadDoesNotPublishPartialPixels)
{
	EXPECT_FALSE(screenshot_image::CaptureRgba(m_Image, 1, 1, EOrder::BOTTOM_UP, [](uint8_t *pData, size_t) -> bool { pData[0] = 42; throw std::runtime_error("read failed"); }, std::malloc, EAlpha::PRESERVE));
	ExpectEmpty();
}

TEST_F(RenderTargetCaptureImage, FailedReplacementClearsTheOldImageAndCanRecoverWithAlpha)
{
	const std::array<uint8_t, 4> aOriginal{1, 2, 3, 0};
	ASSERT_TRUE(Capture(1, 1, aOriginal));
	EXPECT_FALSE(screenshot_image::CaptureRgba(m_Image, 1, 1, EOrder::BOTTOM_UP, [](uint8_t *pData, size_t) { pData[0] = 42; return false; }, std::malloc, EAlpha::PRESERVE));
	ExpectEmpty();
	const std::array<uint8_t, 4> aReplacement{5, 6, 7, 127};
	ASSERT_TRUE(Capture(1, 1, aReplacement));
	ExpectPixels(aReplacement);
}
