#include <engine/gfx/image_loader.h>

#include <gtest/gtest.h>
#include <test/support/image_writer_test_resource.h>

#if defined(CONF_WEBP)
#include <webp/encode.h>
#endif

#include <array>
#include <memory>
#include <string>

namespace
{
	struct SDecodedImage
	{
		CImageInfo m_Image;
		~SDecodedImage() { m_Image.Free(); }
	};
	class CImageEncodeFailureTest : public ::testing::Test
	{
	protected:
		CImageWriterTestResource m_Resource;
		std::array<uint8_t, 16> m_aPixels{13, 17, 19, 255, 23, 29, 31, 255, 37, 41, 43, 255, 47, 53, 59, 255};
		CImageInfo m_Image;
		std::unique_ptr<CByteBufferWriter> m_pWriter;
		const std::string m_Prefix = "old:";
		void SetUp() override
		{
			m_Image.m_Width = 2;
			m_Image.m_Height = 2;
			m_Image.m_Format = CImageInfo::FORMAT_RGBA;
			m_Image.m_pData = m_aPixels.data();
			m_pWriter = std::make_unique<CByteBufferWriter>(&m_Resource.m_Allocation);
			ASSERT_TRUE(m_pWriter->Write(m_Prefix.data(), m_Prefix.size()));
		}
		void TearDown() override
		{
			m_pWriter.reset();
			EXPECT_EQ(m_Resource.m_Live, 0);
		}
		std::string Bytes() const { return m_pWriter->Size() == 0 ? std::string{} : std::string(reinterpret_cast<const char *>(m_pWriter->Data()), m_pWriter->Size()); }
		void ExpectDecoded(const CImageInfo &Image)
		{
			ASSERT_NE(Image.m_pData, nullptr);
			EXPECT_EQ(Image.m_Width, 2u);
			EXPECT_EQ(Image.m_Height, 2u);
			EXPECT_EQ(Image.m_Format, CImageInfo::FORMAT_RGBA);
			EXPECT_EQ(std::string(reinterpret_cast<const char *>(Image.m_pData), Image.DataSize()),
				std::string(reinterpret_cast<const char *>(m_aPixels.data()), m_aPixels.size()));
		}
		void ExpectPng()
		{
			ASSERT_GT(m_pWriter->Size(), m_Prefix.size());
			SDecodedImage Image;
			ASSERT_TRUE(CImageLoader::LoadPng(m_pWriter->Data() + m_Prefix.size(), m_pWriter->Size() - m_Prefix.size(), "writer-recovery", Image.m_Image, m_aPixels.size()));
			ExpectDecoded(Image.m_Image);
		}
#if defined(CONF_WEBP)
		void ExpectWebp()
		{
			ASSERT_GT(m_pWriter->Size(), m_Prefix.size());
			SDecodedImage Image;
			ASSERT_TRUE(CImageLoader::LoadWebP(m_pWriter->Data() + m_Prefix.size(), m_pWriter->Size() - m_Prefix.size(), "writer-recovery", Image.m_Image, m_aPixels.size()));
			ExpectDecoded(Image.m_Image);
		}
#endif
	};
}

TEST_F(CImageEncodeFailureTest, PngSuccessKeepsPrefixAndRoundTripsNativePixels)
{
	ASSERT_TRUE(CImageLoader::SavePng(*m_pWriter, m_Image));
	EXPECT_EQ(Bytes().substr(0, m_Prefix.size()), m_Prefix);
	ExpectPng();
}

TEST_F(CImageEncodeFailureTest, PngCallbackAllocationFailureReturnsFalseAndKeepsOldOutput)
{
	m_Resource.m_Reject = true;
	EXPECT_FALSE(CImageLoader::SavePng(*m_pWriter, m_Image));
	EXPECT_GT(m_Resource.m_Rejections, 0);
	EXPECT_EQ(Bytes(), m_Prefix);
}

TEST_F(CImageEncodeFailureTest, PngFailureAfterPartialNativeWriteRollsBackAndCanEncodeAgain)
{
	bool ObservedPartialWrite = false;
	m_Resource.m_When = [&] {
		ObservedPartialWrite = m_pWriter->Size() > m_Prefix.size();
		return ObservedPartialWrite;
	};
	m_Resource.m_Reject = true;
	EXPECT_FALSE(CImageLoader::SavePng(*m_pWriter, m_Image));
	EXPECT_TRUE(ObservedPartialWrite);
	EXPECT_GT(m_Resource.m_Rejections, 0);
	EXPECT_EQ(Bytes(), m_Prefix);
	m_Resource.m_Reject = false;
	ASSERT_TRUE(CImageLoader::SavePng(*m_pWriter, m_Image));
	ExpectPng();
}

TEST_F(CImageEncodeFailureTest, UnknownWriterExceptionUsesNativePngErrorExit)
{
	m_Resource.m_Reject = true;
	m_Resource.m_Unknown = true;
	EXPECT_FALSE(CImageLoader::SavePng(*m_pWriter, m_Image));
	EXPECT_GT(m_Resource.m_Rejections, 0);
	EXPECT_EQ(Bytes(), m_Prefix);
}

#if defined(CONF_WEBP)
TEST_F(CImageEncodeFailureTest, WebpSuccessKeepsPrefixAndRoundTripsOpaqueNativePixels)
{
	ASSERT_TRUE(CImageLoader::SaveWebP(*m_pWriter, m_Image));
	EXPECT_EQ(Bytes().substr(0, m_Prefix.size()), m_Prefix);
	ExpectWebp();
}

TEST_F(CImageEncodeFailureTest, WebpNonRgbaConversionLeavesSourceUntouchedAndEncodesRgba)
{
	struct SCase
	{
		CImageInfo::EImageFormat m_Format;
		std::array<uint8_t, 12> m_aSource;
		std::array<uint8_t, 16> m_aExpected;
	};
	const SCase aCases[] = {
		{CImageInfo::FORMAT_RGB, {13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59}, {13, 17, 19, 255, 23, 29, 31, 255, 37, 41, 43, 255, 47, 53, 59, 255}},
		{CImageInfo::FORMAT_R, {255, 255, 255, 255}, {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255}},
		{CImageInfo::FORMAT_RA, {13, 255, 23, 255, 37, 255, 47, 255}, {13, 13, 13, 255, 23, 23, 23, 255, 37, 37, 37, 255, 47, 47, 47, 255}},
	};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_Format);
		auto aSource = Case.m_aSource;
		m_aPixels = Case.m_aExpected;
		m_Image.m_Format = Case.m_Format;
		m_Image.m_pData = aSource.data();
		m_pWriter = std::make_unique<CByteBufferWriter>(&m_Resource.m_Allocation);
		ASSERT_TRUE(m_pWriter->Write(m_Prefix.data(), m_Prefix.size()));
		ASSERT_TRUE(CImageLoader::SaveWebP(*m_pWriter, m_Image));
		EXPECT_EQ(aSource, Case.m_aSource);
		EXPECT_EQ(m_Image.m_pData, aSource.data());
		EXPECT_EQ(m_Image.m_Format, Case.m_Format);
		ExpectWebp();
	}
}

TEST_F(CImageEncodeFailureTest, WebpWriterFailureAfterNativeEncodingKeepsPrefixAndCanRetry)
{
	m_Resource.m_Reject = true;
	EXPECT_FALSE(CImageLoader::SaveWebP(*m_pWriter, m_Image));
	EXPECT_GT(m_Resource.m_Rejections, 0);
	EXPECT_EQ(Bytes(), m_Prefix);
	m_Resource.m_Reject = false;
	ASSERT_TRUE(CImageLoader::SaveWebP(*m_pWriter, m_Image));
	ExpectWebp();
}

TEST_F(CImageEncodeFailureTest, WebpInvalidImageIsRejectedWithoutChangingOutput)
{
	for(int Case = 0; Case < 6; ++Case)
	{
		SCOPED_TRACE(Case);
		CImageInfo Invalid;
		Invalid.m_Width = 2;
		Invalid.m_Height = 2;
		Invalid.m_Format = CImageInfo::FORMAT_RGBA;
		Invalid.m_pData = m_aPixels.data();
		if(Case == 0)
			Invalid.m_pData = nullptr;
		else if(Case == 1)
			Invalid.m_Width = 0;
		else if(Case == 2)
			Invalid.m_Height = 0;
		else if(Case == 3)
			Invalid.m_Format = CImageInfo::FORMAT_UNDEFINED;
		else if(Case == 4)
			Invalid.m_Width = WEBP_MAX_DIMENSION + 1u;
		else
			Invalid.m_Height = WEBP_MAX_DIMENSION + 1u;
		const int Calls = m_Resource.m_Calls;
		EXPECT_FALSE(CImageLoader::SaveWebP(*m_pWriter, Invalid));
		EXPECT_EQ(Bytes(), m_Prefix);
		EXPECT_EQ(m_Resource.m_Calls, Calls);
	}
}
#endif
