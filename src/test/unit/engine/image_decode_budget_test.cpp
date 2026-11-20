#include <base/io.h>

#include <engine/gfx/image_loader.h>

#include <gtest/gtest.h>
#include <test/test.h>

namespace
{
	struct SDecodedImage
	{
		CImageInfo m_Image;
		~SDecodedImage() { m_Image.Free(); }
	};
	CImageInfo SmallImage(uint8_t *pPixels, CImageInfo::EImageFormat Format = CImageInfo::FORMAT_RGBA)
	{
		CImageInfo Image;
		Image.m_Width = 2;
		Image.m_Height = 4;
		Image.m_Format = Format;
		Image.m_pData = pPixels;
		return Image;
	}
}

TEST(ImageDecodeBudget, PngRejectsOneByteBelowRgbaBudgetAndAcceptsExactBudget)
{
	uint8_t aPixels[32] = {};
	CByteBufferWriter Writer;
	ASSERT_TRUE(CImageLoader::SavePng(Writer, SmallImage(aPixels)));
	SDecodedImage Decoded;
	EXPECT_FALSE(CImageLoader::LoadPng(Writer.Data(), Writer.Size(), "budget", Decoded.m_Image, 31));
	EXPECT_EQ(Decoded.m_Image.m_pData, nullptr);
	ASSERT_TRUE(CImageLoader::LoadPng(Writer.Data(), Writer.Size(), "budget", Decoded.m_Image, 32));
	EXPECT_EQ(Decoded.m_Image.m_Width, 2u);
	EXPECT_EQ(Decoded.m_Image.m_Height, 4u);
}

TEST(ImageDecodeBudget, PngAccountsForLaterRgbaConversionOfRgbPixels)
{
	uint8_t aPixels[24] = {};
	CByteBufferWriter Writer;
	ASSERT_TRUE(CImageLoader::SavePng(Writer, SmallImage(aPixels, CImageInfo::FORMAT_RGB)));
	SDecodedImage Decoded;
	EXPECT_FALSE(CImageLoader::LoadPng(Writer.Data(), Writer.Size(), "rgb-budget", Decoded.m_Image, 24));
	ASSERT_TRUE(CImageLoader::LoadPng(Writer.Data(), Writer.Size(), "rgb-budget", Decoded.m_Image, 32));
	EXPECT_EQ(Decoded.m_Image.m_Format, CImageInfo::FORMAT_RGB);
}

TEST(ImageDecodeBudget, TruncatedPngFailsAndFollowingValidDecodeRecovers)
{
	uint8_t aPixels[32] = {};
	CByteBufferWriter Writer;
	ASSERT_TRUE(CImageLoader::SavePng(Writer, SmallImage(aPixels)));
	ASSERT_GT(Writer.Size(), 40u);
	SDecodedImage Decoded;
	EXPECT_FALSE(CImageLoader::LoadPng(Writer.Data(), Writer.Size() / 2, "truncated", Decoded.m_Image, 32));
	EXPECT_EQ(Decoded.m_Image.m_pData, nullptr);
	ASSERT_TRUE(CImageLoader::LoadPng(Writer.Data(), Writer.Size(), "recovered", Decoded.m_Image, 32));
}

TEST(ImageDecodeBudget, InvalidPngDimensionsReturnFailure)
{
	uint8_t aPixel = 0;
	CImageInfo Image = SmallImage(&aPixel);
	Image.m_Width = 0x80000000u;
	CByteBufferWriter Writer;
	EXPECT_FALSE(CImageLoader::SavePng(Writer, Image));
}

#if defined(CONF_WEBP)
TEST(ImageDecodeBudget, WebpRejectsOneByteBelowPixelBudgetAndAcceptsExactBudget)
{
	uint8_t aPixels[32] = {};
	CByteBufferWriter Writer;
	ASSERT_TRUE(CImageLoader::SaveWebP(Writer, SmallImage(aPixels)));
	SDecodedImage Decoded;
	EXPECT_FALSE(CImageLoader::LoadWebP(Writer.Data(), Writer.Size(), "budget", Decoded.m_Image, 31));
	EXPECT_EQ(Decoded.m_Image.m_pData, nullptr);
	ASSERT_TRUE(CImageLoader::LoadWebP(Writer.Data(), Writer.Size(), "budget", Decoded.m_Image, 32));
	EXPECT_EQ(Decoded.m_Image.m_Width, 2u);
	EXPECT_EQ(Decoded.m_Image.m_Height, 4u);
}

TEST(ImageDecodeBudget, WebpFileLoadRecoversAfterEmptyInput)
{
	CTestInfo TestInfo;
	const char *pFilename = TestInfo.m_aFilename;
	IOHANDLE EmptyFile = io_open(pFilename, IOFLAG_WRITE);
	ASSERT_NE(EmptyFile, nullptr);
	ASSERT_EQ(io_close(EmptyFile), 0);

	SDecodedImage Decoded;
	IOHANDLE EmptyInput = io_open(pFilename, IOFLAG_READ);
	ASSERT_NE(EmptyInput, nullptr);
	EXPECT_FALSE(CImageLoader::LoadWebP(EmptyInput, pFilename, Decoded.m_Image));
	EXPECT_EQ(Decoded.m_Image.m_pData, nullptr);

	uint8_t aPixels[32] = {17, 23, 42, 255};
	ASSERT_TRUE(CImageLoader::SaveWebP(io_open(pFilename, IOFLAG_WRITE), pFilename, SmallImage(aPixels)));
	ASSERT_TRUE(CImageLoader::LoadWebP(io_open(pFilename, IOFLAG_READ), pFilename, Decoded.m_Image));
	ASSERT_EQ(Decoded.m_Image.m_Width, 2u);
	ASSERT_EQ(Decoded.m_Image.m_Height, 4u);
	EXPECT_EQ(Decoded.m_Image.m_Format, CImageInfo::FORMAT_RGBA);
	ASSERT_NE(Decoded.m_Image.m_pData, nullptr);
	EXPECT_EQ(Decoded.m_Image.m_pData[0], 17);
	EXPECT_EQ(Decoded.m_Image.m_pData[3], 255);
}
#endif
