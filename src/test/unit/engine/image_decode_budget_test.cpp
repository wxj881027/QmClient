#include <engine/gfx/image_loader.h>
#include <gtest/gtest.h>

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
#endif
