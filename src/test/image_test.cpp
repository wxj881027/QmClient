// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "test.h"

#include <base/system.h>

#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/image.h>

#include <gtest/gtest.h>

#include <limits>

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

static CImageInfo MakePngTestImage(size_t Width, size_t Height, CImageInfo::EImageFormat Format)
{
	CImageInfo Image;
	Image.m_Width = Width;
	Image.m_Height = Height;
	Image.m_Format = Format;
	Image.m_pData = static_cast<uint8_t *>(calloc(Image.DataSize(), 1));
	return Image;
}

static void SetPngTestPixel(CImageInfo &Image, size_t x, size_t y, uint8_t Red, uint8_t Green, uint8_t Blue, uint8_t Alpha)
{
	const size_t Offset = (y * Image.m_Width + x) * Image.PixelSize();
	Image.m_pData[Offset] = Red;
	Image.m_pData[Offset + 1] = Green;
	Image.m_pData[Offset + 2] = Blue;
	if(Image.m_Format == CImageInfo::FORMAT_RGBA)
		Image.m_pData[Offset + 3] = Alpha;
}

// PNG 往返：SavePng -> LoadPng 应保持尺寸、格式与像素完全一致（含奇数高度）
TEST(Image, PngSaveLoadRoundTripPreservesPixels)
{
	constexpr size_t Width = 5;
	constexpr size_t Height = 7;
	CImageInfo Image = MakePngTestImage(Width, Height, CImageInfo::FORMAT_RGBA);
	ASSERT_NE(Image.m_pData, nullptr);

	for(size_t y = 0; y < Height; y++)
	{
		for(size_t x = 0; x < Width; x++)
		{
			SetPngTestPixel(Image, x, y, (uint8_t)(x * 40 + 1), (uint8_t)(y * 30 + 2), (uint8_t)(x * y * 7 + 3), (uint8_t)(x + y + 4));
		}
	}

	CByteBufferWriter Writer;
	ASSERT_TRUE(CImageLoader::SavePng(Writer, Image));
	ASSERT_GT(Writer.Size(), 0u);

	CImageInfo Reloaded;
	ASSERT_TRUE(CImageLoader::LoadPng(Writer.Data(), Writer.Size(), "image-test-png", Reloaded));
	ASSERT_NE(Reloaded.m_pData, nullptr);
	EXPECT_EQ(Reloaded.m_Width, Width);
	EXPECT_EQ(Reloaded.m_Height, Height);
	EXPECT_EQ(Reloaded.m_Format, CImageInfo::FORMAT_RGBA);
	EXPECT_EQ(mem_comp(Reloaded.m_pData, Image.m_pData, (size_t)(Width * Height * 4)), 0);

	Reloaded.Free();
	Image.Free();
}

// PNG 往返（RGB，无 alpha 通道）
TEST(Image, PngSaveLoadRoundTripPreservesRgbPixels)
{
	constexpr size_t Width = 3;
	constexpr size_t Height = 2;
	CImageInfo Image = MakePngTestImage(Width, Height, CImageInfo::FORMAT_RGB);
	ASSERT_NE(Image.m_pData, nullptr);

	for(size_t y = 0; y < Height; y++)
	{
		for(size_t x = 0; x < Width; x++)
		{
			SetPngTestPixel(Image, x, y, (uint8_t)(x * 11 + 5), (uint8_t)(y * 13 + 6), (uint8_t)(x + y + 7), 255);
		}
	}

	CByteBufferWriter Writer;
	ASSERT_TRUE(CImageLoader::SavePng(Writer, Image));

	CImageInfo Reloaded;
	ASSERT_TRUE(CImageLoader::LoadPng(Writer.Data(), Writer.Size(), "image-test-png-rgb", Reloaded));
	ASSERT_NE(Reloaded.m_pData, nullptr);
	EXPECT_EQ(Reloaded.m_Width, Width);
	EXPECT_EQ(Reloaded.m_Height, Height);
	EXPECT_EQ(Reloaded.m_Format, CImageInfo::FORMAT_RGB);
	EXPECT_EQ(mem_comp(Reloaded.m_pData, Image.m_pData, (size_t)(Width * Height * 3)), 0);

	Reloaded.Free();
	Image.Free();
}

// 截断/损坏的 PNG 不应崩溃，只返回 false（覆盖 longjmp -> Cleanup 路径）
TEST(Image, PngLoadRejectsTruncatedAndCorruptData)
{
	CImageInfo Image = MakePngTestImage(4, 4, CImageInfo::FORMAT_RGBA);
	ASSERT_NE(Image.m_pData, nullptr);
	SetPngTestPixel(Image, 0, 0, 1, 2, 3, 4);

	CByteBufferWriter Writer;
	ASSERT_TRUE(CImageLoader::SavePng(Writer, Image));

	CImageInfo Truncated;
	EXPECT_FALSE(CImageLoader::LoadPng(Writer.Data(), Writer.Size() / 3, "image-test-png-truncated", Truncated));
	Truncated.Free();

	const uint8_t aNotPng[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
	CImageInfo Corrupt;
	EXPECT_FALSE(CImageLoader::LoadPng(aNotPng, sizeof(aNotPng), "image-test-png-corrupt", Corrupt));
	Corrupt.Free();

	Image.Free();
}
