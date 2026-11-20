#include <engine/client/backend/screenshot_image.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>

namespace
{
	class CScreenshotImageTest : public ::testing::Test
	{
	protected:
		CImageInfo m_Image;

		~CScreenshotImageTest() override { m_Image.Free(); }

		void ExpectEmpty() const
		{
			EXPECT_EQ(m_Image.m_pData, nullptr);
			EXPECT_EQ(m_Image.m_Width, 0U);
			EXPECT_EQ(m_Image.m_Height, 0U);
			EXPECT_EQ(m_Image.m_Format, CImageInfo::FORMAT_UNDEFINED);
		}

		template<size_t Size>
		bool Capture(size_t Width, size_t Height, const std::array<uint8_t, Size> &aPixels, screenshot_image::ERowOrder Order = screenshot_image::ERowOrder::TOP_DOWN)
		{
			return screenshot_image::CaptureRgba(m_Image, Width, Height, Order, [&](uint8_t *pData, size_t Bytes) {
				if(Bytes != aPixels.size())
					return false;
				std::memcpy(pData, aPixels.data(), Bytes);
				return true;
			});
		}

		template<size_t Size>
		void ExpectPixels(const std::array<uint8_t, Size> &aPixels) const
		{
			ASSERT_NE(m_Image.m_pData, nullptr);
			ASSERT_EQ(m_Image.DataSize(), aPixels.size());
			EXPECT_EQ(std::memcmp(m_Image.m_pData, aPixels.data(), aPixels.size()), 0);
		}
	};
}

TEST_F(CScreenshotImageTest, TopDownReadbackPreservesRowOrderAndMakesPixelsOpaque)
{
	const std::array<uint8_t, 8> aInput{10, 20, 30, 0, 40, 50, 60, 127};
	ASSERT_TRUE(Capture(1, 2, aInput));
	EXPECT_EQ(m_Image.m_Width, 1U);
	EXPECT_EQ(m_Image.m_Height, 2U);
	EXPECT_EQ(m_Image.m_Format, CImageInfo::FORMAT_RGBA);
	ExpectPixels(std::array<uint8_t, 8>{10, 20, 30, 255, 40, 50, 60, 255});
}

TEST_F(CScreenshotImageTest, BottomUpReadbackReversesRowsWithoutReversingPixels)
{
	const std::array<uint8_t, 16> aInput{1, 2, 3, 0, 4, 5, 6, 0, 7, 8, 9, 0, 10, 11, 12, 0};
	ASSERT_TRUE(Capture(2, 2, aInput, screenshot_image::ERowOrder::BOTTOM_UP));
	ExpectPixels(std::array<uint8_t, 16>{7, 8, 9, 255, 10, 11, 12, 255, 1, 2, 3, 255, 4, 5, 6, 255});
}

TEST_F(CScreenshotImageTest, OddHeightReadbackAlsoMakesTheMiddleRowOpaque)
{
	const std::array<uint8_t, 12> aInput{1, 2, 3, 0, 4, 5, 6, 127, 7, 8, 9, 0};
	ASSERT_TRUE(Capture(1, 3, aInput, screenshot_image::ERowOrder::BOTTOM_UP));
	ExpectPixels(std::array<uint8_t, 12>{7, 8, 9, 255, 4, 5, 6, 255, 1, 2, 3, 255});
}

TEST_F(CScreenshotImageTest, SingleRowReadbackKeepsPixelsAndResetsAlpha)
{
	const std::array<uint8_t, 8> aInput{1, 2, 3, 0, 4, 5, 6, 127};
	ASSERT_TRUE(Capture(2, 1, aInput, screenshot_image::ERowOrder::BOTTOM_UP));
	ExpectPixels(std::array<uint8_t, 8>{1, 2, 3, 255, 4, 5, 6, 255});
}

TEST_F(CScreenshotImageTest, EmptyDimensionsSkipAllocationAndReadback)
{
	int Allocations = 0;
	int Reads = 0;
	const auto Read = [&](uint8_t *, size_t) { ++Reads; return true; };
	const auto Allocate = [&](size_t) -> void * { ++Allocations; return nullptr; };
	EXPECT_FALSE(screenshot_image::CaptureRgba(m_Image, 0, 1, screenshot_image::ERowOrder::TOP_DOWN, Read, Allocate));
	EXPECT_FALSE(screenshot_image::CaptureRgba(m_Image, 1, 0, screenshot_image::ERowOrder::TOP_DOWN, Read, Allocate));
	EXPECT_EQ(Allocations, 0);
	EXPECT_EQ(Reads, 0);
	ExpectEmpty();
}

TEST_F(CScreenshotImageTest, OverflowingPixelCountSkipsAllocationAndReadback)
{
	int Allocations = 0;
	int Reads = 0;
	EXPECT_FALSE(screenshot_image::CaptureRgba(m_Image, std::numeric_limits<size_t>::max(), 2, screenshot_image::ERowOrder::TOP_DOWN, [&](uint8_t *, size_t) { ++Reads; return true; }, [&](size_t) -> void * { ++Allocations; return nullptr; }));
	EXPECT_EQ(Allocations, 0);
	EXPECT_EQ(Reads, 0);
	ExpectEmpty();
}

TEST_F(CScreenshotImageTest, OverflowingPixelBytesSkipAllocation)
{
	int Allocations = 0;
	EXPECT_FALSE(screenshot_image::CaptureRgba(m_Image, std::numeric_limits<size_t>::max() / 4 + 1, 1, screenshot_image::ERowOrder::TOP_DOWN, [](uint8_t *, size_t) { return true; }, [&](size_t) -> void * { ++Allocations; return nullptr; }));
	EXPECT_EQ(Allocations, 0);
	ExpectEmpty();
}

TEST_F(CScreenshotImageTest, UnaddressableFlipScratchRowSkipsAllocation)
{
	const size_t Width = static_cast<size_t>(std::numeric_limits<ptrdiff_t>::max()) / 4;
	int Allocations = 0;
	EXPECT_FALSE(screenshot_image::CaptureRgba(m_Image, Width, 1, screenshot_image::ERowOrder::BOTTOM_UP, [](uint8_t *, size_t) { return true; }, [&](size_t) -> void * { ++Allocations; return nullptr; }));
	EXPECT_EQ(Allocations, 0);
	ExpectEmpty();
}

TEST_F(CScreenshotImageTest, NullAllocationSkipsReadbackAndLeavesNoImage)
{
	int Reads = 0;
	EXPECT_FALSE(screenshot_image::CaptureRgba(m_Image, 2, 2, screenshot_image::ERowOrder::BOTTOM_UP, [&](uint8_t *, size_t) { ++Reads; return true; }, [](size_t) -> void * { return nullptr; }));
	EXPECT_EQ(Reads, 0);
	ExpectEmpty();
}

TEST_F(CScreenshotImageTest, ThrowingAllocatorSkipsReadbackAndLeavesNoImage)
{
	int Reads = 0;
	EXPECT_FALSE(screenshot_image::CaptureRgba(m_Image, 2, 2, screenshot_image::ERowOrder::BOTTOM_UP, [&](uint8_t *, size_t) { ++Reads; return true; }, [](size_t) -> void * { throw std::bad_alloc(); }));
	EXPECT_EQ(Reads, 0);
	ExpectEmpty();
}

TEST_F(CScreenshotImageTest, FailedReadbackDoesNotPublishPartialPixels)
{
	EXPECT_FALSE(screenshot_image::CaptureRgba(m_Image, 2, 2, screenshot_image::ERowOrder::BOTTOM_UP,
		[](uint8_t *pData, size_t) { pData[0] = 42; return false; }));
	ExpectEmpty();
}

TEST_F(CScreenshotImageTest, ThrowingReadbackDoesNotPublishPartialPixels)
{
	EXPECT_FALSE(screenshot_image::CaptureRgba(m_Image, 2, 2, screenshot_image::ERowOrder::BOTTOM_UP,
		[](uint8_t *pData, size_t) -> bool { pData[0] = 42; throw std::runtime_error("readback failed"); }));
	ExpectEmpty();
}

TEST_F(CScreenshotImageTest, PublicationWaitsForCompletedReadback)
{
	const std::array<uint8_t, 4> aOriginal{1, 2, 3, 4};
	ASSERT_TRUE(Capture(1, 1, aOriginal));
	ASSERT_TRUE(screenshot_image::CaptureRgba(m_Image, 1, 1, screenshot_image::ERowOrder::TOP_DOWN,
		[&](uint8_t *pData, size_t Bytes) {
			ExpectEmpty();
			EXPECT_EQ(Bytes, 4U);
			std::memset(pData, 7, Bytes);
			return true;
		}));
	ExpectPixels(std::array<uint8_t, 4>{7, 7, 7, 255});
}

TEST_F(CScreenshotImageTest, FailedReplacementClearsStaleImageAndNextCaptureRecovers)
{
	const std::array<uint8_t, 4> aOriginal{1, 2, 3, 4};
	ASSERT_TRUE(Capture(1, 1, aOriginal));
	EXPECT_FALSE(screenshot_image::CaptureRgba(m_Image, 1, 1, screenshot_image::ERowOrder::TOP_DOWN, [](uint8_t *, size_t) { return true; }, [](size_t) -> void * { return nullptr; }));
	ExpectEmpty();
	const std::array<uint8_t, 4> aReplacement{9, 8, 7, 6};
	ASSERT_TRUE(Capture(1, 1, aReplacement));
	ExpectPixels(std::array<uint8_t, 4>{9, 8, 7, 255});
}
