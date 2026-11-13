// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/system.h>

#include <engine/gfx/image_manipulation.h>
#include <engine/image.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

TEST(Image, MoveAndReleaseHaveNoThrowContracts)
{
	EXPECT_TRUE(std::is_nothrow_move_constructible_v<CImageInfo>);
	EXPECT_TRUE(std::is_nothrow_move_assignable_v<CImageInfo>);
	EXPECT_TRUE(noexcept(std::declval<CImageInfo &>().Free()));
}

TEST(Image, MoveConstructionTransfersPixelsAndClearsSource)
{
	std::unique_ptr<uint8_t, decltype(&std::free)> pPixels(static_cast<uint8_t *>(std::malloc(8)), &std::free);
	ASSERT_NE(pPixels, nullptr);
	pPixels.get()[0] = 73;
	CImageInfo Source;
	Source.m_Width = 2;
	Source.m_Height = 1;
	Source.m_Format = CImageInfo::FORMAT_RGBA;
	Source.m_pData = pPixels.get();
	CImageInfo Moved(std::move(Source));
	ASSERT_EQ(Moved.m_pData, pPixels.get());
	EXPECT_EQ(Moved.m_pData[0], 73);
	EXPECT_EQ(Moved.m_Width, 2u);
	EXPECT_EQ(Moved.m_Height, 1u);
	EXPECT_EQ(Moved.m_Format, CImageInfo::FORMAT_RGBA);
	EXPECT_EQ(Source.m_pData, nullptr);
	EXPECT_EQ(Source.m_Width, 0u);
	EXPECT_EQ(Source.m_Height, 0u);
	EXPECT_EQ(Source.m_Format, CImageInfo::FORMAT_UNDEFINED);
}

TEST(Image, MoveAssignmentTransfersPixelsIntoAnOccupiedDestination)
{
	std::unique_ptr<uint8_t, decltype(&std::free)> pPixels(static_cast<uint8_t *>(std::malloc(8)), &std::free);
	std::unique_ptr<uint8_t, decltype(&std::free)> pPrevious(static_cast<uint8_t *>(std::malloc(4)), &std::free);
	ASSERT_NE(pPixels, nullptr);
	ASSERT_NE(pPrevious, nullptr);
	CImageInfo Source;
	Source.m_Width = 2;
	Source.m_Height = 1;
	Source.m_Format = CImageInfo::FORMAT_RGBA;
	Source.m_pData = pPixels.get();
	CImageInfo Target;
	Target.m_Width = Target.m_Height = 1;
	Target.m_Format = CImageInfo::FORMAT_RGBA;
	Target.m_pData = pPrevious.release();
	CImageInfo &Assigned = Target = std::move(Source);
	EXPECT_EQ(&Assigned, &Target);
	EXPECT_EQ(Target.m_pData, pPixels.get());
	EXPECT_EQ(Target.m_Width, 2u);
	EXPECT_EQ(Target.m_Height, 1u);
	EXPECT_EQ(Target.m_Format, CImageInfo::FORMAT_RGBA);
	EXPECT_EQ(Source.m_pData, nullptr);
	EXPECT_EQ(Source.m_Format, CImageInfo::FORMAT_UNDEFINED);
}

TEST(Image, SelfMoveKeepsPixelsAndDimensions)
{
	std::unique_ptr<uint8_t, decltype(&std::free)> pPixels(static_cast<uint8_t *>(std::malloc(4)), &std::free);
	ASSERT_NE(pPixels, nullptr);
	pPixels.get()[0] = 19;
	CImageInfo Image;
	Image.m_Width = Image.m_Height = 1;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.m_pData = pPixels.get();
	CImageInfo &Alias = Image;
	Image = std::move(Alias);
	ASSERT_EQ(Image.m_pData, pPixels.get());
	EXPECT_EQ(Image.m_pData[0], 19);
	EXPECT_EQ(Image.m_Width, 1u);
	EXPECT_EQ(Image.m_Height, 1u);
	EXPECT_EQ(Image.m_Format, CImageInfo::FORMAT_RGBA);
}

TEST(Image, FormatNamesMatchFormatValues)
{
	EXPECT_STREQ(CImageInfo::FormatName(CImageInfo::FORMAT_UNDEFINED), "UNDEFINED");
	EXPECT_STREQ(CImageInfo::FormatName(CImageInfo::FORMAT_RGB), "RGB");
	EXPECT_STREQ(CImageInfo::FormatName(CImageInfo::FORMAT_RGBA), "RGBA");
	EXPECT_STREQ(CImageInfo::FormatName(CImageInfo::FORMAT_R), "R");
	EXPECT_STREQ(CImageInfo::FormatName(CImageInfo::FORMAT_RA), "RA");
}

TEST(Image, DataSizeRejectsOverflow)
{
	CImageInfo Image;
	Image.m_Width = std::numeric_limits<size_t>::max();
	Image.m_Height = 2;
	Image.m_Format = CImageInfo::FORMAT_RGBA;

	size_t DataSize = 123;
	EXPECT_FALSE(Image.DataSize(DataSize));
	EXPECT_EQ(DataSize, 0u);
}

TEST(Image, DataSizeRejectsUndefinedFormat)
{
	CImageInfo Image;
	Image.m_Width = 1;
	Image.m_Height = 1;

	size_t DataSize = 123;
	EXPECT_FALSE(Image.DataSize(DataSize));
	EXPECT_EQ(DataSize, 0u);
}

TEST(Image, DeepCopyCopiesData)
{
	uint8_t aPixels[] = {1, 2, 3, 4, 5, 6, 7, 8};

	CImageInfo Image;
	Image.m_Width = 2;
	Image.m_Height = 1;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.m_pData = aPixels;

	CImageInfo Copy = Image.DeepCopy();
	ASSERT_NE(Copy.m_pData, nullptr);
	EXPECT_NE(Copy.m_pData, Image.m_pData);
	EXPECT_TRUE(Copy.DataEquals(Image));
	Copy.Free();
}

TEST(Image, ConvertRgbToRgbaAlloc)
{
	uint8_t aRgb[] = {10, 20, 30, 40, 50, 60};

	CImageInfo Image;
	Image.m_Width = 2;
	Image.m_Height = 1;
	Image.m_Format = CImageInfo::FORMAT_RGB;
	Image.m_pData = aRgb;

	uint8_t *pRgba = nullptr;
	EXPECT_FALSE(ConvertToRgbaAlloc(pRgba, Image));
	ASSERT_NE(pRgba, nullptr);
	const uint8_t aExpected[] = {10, 20, 30, 255, 40, 50, 60, 255};
	EXPECT_EQ(mem_comp(pRgba, aExpected, sizeof(aExpected)), 0);
	free(pRgba);
}

TEST(Image, ConvertRgbaAllocReportsAlreadyRgba)
{
	uint8_t aRgba[] = {1, 2, 3, 4};

	CImageInfo Image;
	Image.m_Width = 1;
	Image.m_Height = 1;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.m_pData = aRgba;

	uint8_t *pRgba = nullptr;
	EXPECT_TRUE(ConvertToRgbaAlloc(pRgba, Image));
	ASSERT_NE(pRgba, nullptr);
	EXPECT_EQ(mem_comp(pRgba, aRgba, sizeof(aRgba)), 0);
	free(pRgba);
}

TEST(Image, ConvertToRgbaRejectsMissingSourceData)
{
	CImageInfo Image;
	Image.m_Width = 1;
	Image.m_Height = 1;
	Image.m_Format = CImageInfo::FORMAT_RGB;

	uint8_t *pRgba = nullptr;
	EXPECT_FALSE(ConvertToRgbaAlloc(pRgba, Image));
	EXPECT_EQ(pRgba, nullptr);
}

TEST(Image, ResizeRejectsInvalidDimensions)
{
	uint8_t aRgba[] = {1, 2, 3, 4};

	uint8_t *pResized = ResizeImage(aRgba, 1, 1, 0, 1, 4);
	EXPECT_EQ(pResized, nullptr);
}

TEST(Image, ConvertToGrayscaleRectOnlyChangesSelectedPixels)
{
	uint8_t aRgba[] = {100, 150, 200, 17, 10, 20, 30, 40};

	CImageInfo Image;
	Image.m_Width = 2;
	Image.m_Height = 1;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.m_pData = aRgba;

	ConvertToGrayscaleRect(Image, 1, 0, 1, 1);

	const uint8_t aExpected[] = {100, 150, 200, 17, 18, 18, 18, 40};
	EXPECT_EQ(mem_comp(aRgba, aExpected, sizeof(aExpected)), 0);
}

TEST(Image, ColorizeWithHueRectPreservesAlphaAndAppliesHue)
{
	uint8_t aRgba[] = {128, 128, 128, 77};

	CImageInfo Image;
	Image.m_Width = 1;
	Image.m_Height = 1;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.m_pData = aRgba;

	ColorizeWithHueRect(Image, 0.0f, 0.75f, 0, 0, 1, 1);

	EXPECT_GT(aRgba[0], aRgba[1]);
	EXPECT_EQ(aRgba[1], aRgba[2]);
	EXPECT_EQ(aRgba[3], 77);
}
