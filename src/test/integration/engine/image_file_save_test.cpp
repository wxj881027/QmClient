#include <engine/gfx/image_loader.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <cstdlib>
#include <memory>

namespace
{
	class CObservedImageFile : public CImageFileOperations
	{
	public:
		bool m_ShortWrite = false;
		bool m_FailClose = false;
		int m_Writes = 0;
		int m_Closes = 0;
		unsigned m_Written = 0;
		int m_NativeCloseResult = -1;

		unsigned Write(IOHANDLE File, const void *pData, unsigned Size) noexcept override
		{
			++m_Writes;
			m_Written = CImageFileOperations::Write(File, pData, m_ShortWrite && Size > 0 ? Size - 1 : Size);
			return m_Written;
		}
		int Close(IOHANDLE File) noexcept override
		{
			++m_Closes;
			m_NativeCloseResult = CImageFileOperations::Close(File);
			// 真实句柄始终正常回收，只在边界注入最终关闭错误，不破坏 CRT 状态。
			return m_FailClose ? 1 : m_NativeCloseResult;
		}
	};

	class ImageFileSave : public ::testing::TestWithParam<bool>
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		std::array<uint8_t, 4> m_aPixels{12, 34, 56, 255};
		CImageInfo m_Image;
		CObservedImageFile m_Operations;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_Image.m_Width = m_Image.m_Height = 1;
			m_Image.m_Format = CImageInfo::FORMAT_RGBA;
			m_Image.m_pData = m_aPixels.data();
		}
		bool Save(IOHANDLE File)
		{
			return GetParam() ? CImageLoader::SaveWebP(File, "image.webp", m_Image, m_Operations) :
					    CImageLoader::SavePng(File, "image.png", m_Image, nullptr, m_Operations);
		}
		IOHANDLE Open() { return m_pStorage->OpenFile("image", IOFLAG_WRITE, IStorage::TYPE_SAVE); }
	};
}

TEST_P(ImageFileSave, SuccessfulWriteAndCloseProduceTheOriginalPixels)
{
	IOHANDLE File = Open();
	ASSERT_NE(File, nullptr);
	ASSERT_TRUE(Save(File));
	EXPECT_EQ(m_Operations.m_Writes, 1);
	EXPECT_EQ(m_Operations.m_Closes, 1);
	EXPECT_EQ(m_Operations.m_NativeCloseResult, 0);
	void *pBytes = nullptr;
	unsigned Size = 0;
	ASSERT_TRUE(m_pStorage->ReadFile("image", IStorage::TYPE_SAVE, &pBytes, &Size));
	std::unique_ptr<void, decltype(&std::free)> Bytes(pBytes, std::free);
	CImageInfo Decoded;
	const bool Loaded = GetParam() ? CImageLoader::LoadWebP(pBytes, Size, "image", Decoded) : CImageLoader::LoadPng(pBytes, Size, "image", Decoded);
	std::unique_ptr<void, decltype(&std::free)> Pixels(Decoded.m_pData, std::free);
	ASSERT_TRUE(Loaded);
	ASSERT_NE(Decoded.m_pData, nullptr);
	ASSERT_EQ(Decoded.m_Width, 1u);
	ASSERT_EQ(Decoded.m_Height, 1u);
	ASSERT_EQ(Decoded.m_Format, CImageInfo::FORMAT_RGBA);
	EXPECT_EQ(mem_comp(Decoded.m_pData, m_aPixels.data(), m_aPixels.size()), 0);
}

TEST_P(ImageFileSave, CloseFailureIsReportedEvenAfterAFullWrite)
{
	IOHANDLE File = Open();
	ASSERT_NE(File, nullptr);
	m_Operations.m_FailClose = true;
	EXPECT_FALSE(Save(File));
	EXPECT_EQ(m_Operations.m_Writes, 1);
	EXPECT_GT(m_Operations.m_Written, 0u);
	EXPECT_EQ(m_Operations.m_NativeCloseResult, 0);
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_P(ImageFileSave, ShortWriteStillClosesTheFileAndReportsFailure)
{
	IOHANDLE File = Open();
	ASSERT_NE(File, nullptr);
	m_Operations.m_ShortWrite = true;
	EXPECT_FALSE(Save(File));
	EXPECT_EQ(m_Operations.m_Writes, 1);
	EXPECT_EQ(m_Operations.m_Closes, 1);
	EXPECT_EQ(m_Operations.m_NativeCloseResult, 0);
}

TEST_P(ImageFileSave, InvalidImageClosesTheFileWithoutWriting)
{
	IOHANDLE File = Open();
	ASSERT_NE(File, nullptr);
	m_Image.m_Width = 0;
	EXPECT_FALSE(Save(File));
	EXPECT_EQ(m_Operations.m_Writes, 0);
	EXPECT_EQ(m_Operations.m_Closes, 1);
	EXPECT_EQ(m_Operations.m_NativeCloseResult, 0);
}

TEST_P(ImageFileSave, MissingFileDoesNotReachWriteOrClose)
{
	EXPECT_FALSE(Save(nullptr));
	EXPECT_EQ(m_Operations.m_Writes, 0);
	EXPECT_EQ(m_Operations.m_Closes, 0);
}

INSTANTIATE_TEST_SUITE_P(Png, ImageFileSave, ::testing::Values(false));
#if defined(CONF_WEBP)
INSTANTIATE_TEST_SUITE_P(Webp, ImageFileSave, ::testing::Values(true));
#endif
