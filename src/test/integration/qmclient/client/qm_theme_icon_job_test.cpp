#include <engine/gfx/image_loader.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/theme_icon_load_job.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>
#include <test/test.h>

#include <cstdlib>
#include <memory>
#include <new>
#include <stdexcept>
#include <vector>

namespace
{
	bool DecodePng(const void *pData, unsigned Size, const char *pName, CImageInfo &Image)
	{
		return CImageLoader::LoadPng(pData, Size, pName, Image);
	}
	bool ThrowBeforePixels(const void *, unsigned, const char *, CImageInfo &)
	{
		throw std::bad_alloc();
	}
	void AllocatePixel(CImageInfo &Image)
	{
		Image.m_Width = Image.m_Height = 1;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.m_pData = static_cast<uint8_t *>(malloc(4));
		if(Image.m_pData == nullptr)
			throw std::bad_alloc();
	}
	bool ThrowAfterPixels(const void *, unsigned, const char *, CImageInfo &Image)
	{
		AllocatePixel(Image);
		throw std::runtime_error("decoder boundary failure");
	}
	bool RejectAfterPixels(const void *, unsigned, const char *, CImageInfo &Image)
	{
		AllocatePixel(Image);
		return false;
	}

	class QmThemeIconJob : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CJobPool m_Pool;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_Pool.TryInit(1));
		}
		void TearDown() override { m_Pool.Shutdown(); }
		void Store(int Width = 2, int Height = 1, CImageInfo::EImageFormat Format = CImageInfo::FORMAT_RGBA)
		{
			std::vector<uint8_t> vPixels((size_t)Width * Height * CImageInfo::PixelSize(Format), 127);
			CImageInfo Image;
			Image.m_Width = Width;
			Image.m_Height = Height;
			Image.m_Format = Format;
			Image.m_pData = vPixels.data();
			ASSERT_TRUE(CImageLoader::SavePng(m_pStorage->OpenFile("icon.png", IOFLAG_WRITE, IStorage::TYPE_SAVE), "icon.png", Image));
		}
		std::shared_ptr<CQmThemeIconLoadJob> Run(CQmThemeIconLoadJob::FDecode pDecode = DecodePng)
		{
			auto pJob = std::make_shared<CQmThemeIconLoadJob>("theme", "icon.png", m_pStorage.get(), pDecode);
			if(!m_Pool.TryAdd(pJob))
			{
				ADD_FAILURE() << "native pool rejected theme icon job";
				return nullptr;
			}
			if(!WaitForJobDone(*pJob))
			{
				ADD_FAILURE() << "theme icon job did not complete";
				return nullptr;
			}
			return pJob;
		}
		void ExpectFailed(const std::shared_ptr<CQmThemeIconLoadJob> &pJob)
		{
			ASSERT_NE(pJob, nullptr);
			EXPECT_TRUE(pJob->IsCompleted());
			auto Result = pJob->TakeResult();
			EXPECT_EQ(Result.m_ThemeName, "theme");
			EXPECT_FALSE(Result.m_Success);
			EXPECT_EQ(Result.m_Image.m_pData, nullptr);
		}
	};
}

TEST_F(QmThemeIconJob, PngResultTransfersImageOnceAndSurvivesJobRelease)
{
	ASSERT_NO_FATAL_FAILURE(Store());
	auto pJob = Run();
	ASSERT_NE(pJob, nullptr);
	ASSERT_TRUE(pJob->IsCompleted());
	auto Result = pJob->TakeResult();
	ASSERT_TRUE(Result.m_Success);
	ASSERT_NE(Result.m_Image.m_pData, nullptr);
	EXPECT_EQ(Result.m_ThemeName, "theme");
	EXPECT_EQ(Result.m_Image.m_Width, 2u);
	EXPECT_EQ(Result.m_Image.m_Height, 1u);
	auto Empty = pJob->TakeResult();
	EXPECT_FALSE(Empty.m_Success);
	EXPECT_EQ(Empty.m_Image.m_pData, nullptr);
	pJob.reset();
	EXPECT_EQ(Result.m_Image.m_pData[0], 127);
	EXPECT_EQ(Result.m_Image.m_pData[7], 127);
}

TEST_F(QmThemeIconJob, MissingFilePublishesFailedCompletion)
{
	ExpectFailed(Run());
}

TEST_F(QmThemeIconJob, InvalidPngPublishesFailedCompletion)
{
	IOHANDLE File = m_pStorage->OpenFile("icon.png", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	io_write(File, "not png", 7);
	io_close(File);
	ExpectFailed(Run());
}

TEST_F(QmThemeIconJob, DecoderAllocationExceptionPublishesFailedCompletion)
{
	ASSERT_NO_FATAL_FAILURE(Store());
	ExpectFailed(Run(ThrowBeforePixels));
}

TEST_F(QmThemeIconJob, ExceptionAfterPixelsDiscardsCandidateAndDoesNotStopNextNativeJob)
{
	ASSERT_NO_FATAL_FAILURE(Store());
	ExpectFailed(Run(ThrowAfterPixels));
	auto pNext = Run();
	ASSERT_NE(pNext, nullptr);
	EXPECT_TRUE(pNext->IsCompleted());
	auto Result = pNext->TakeResult();
	EXPECT_TRUE(Result.m_Success);
	ASSERT_NE(Result.m_Image.m_pData, nullptr);
	EXPECT_EQ(Result.m_Image.m_pData[0], 127);
}

TEST_F(QmThemeIconJob, DecoderRejectionDiscardsPartiallyWrittenPixels)
{
	ASSERT_NO_FATAL_FAILURE(Store());
	ExpectFailed(Run(RejectAfterPixels));
}

TEST_F(QmThemeIconJob, RgbPngIsConvertedToRgba)
{
	ASSERT_NO_FATAL_FAILURE(Store(2, 1, CImageInfo::FORMAT_RGB));
	auto pJob = Run();
	ASSERT_NE(pJob, nullptr);
	auto Result = pJob->TakeResult();
	ASSERT_TRUE(Result.m_Success);
	EXPECT_EQ(Result.m_Image.m_Format, CImageInfo::FORMAT_RGBA);
	ASSERT_NE(Result.m_Image.m_pData, nullptr);
	EXPECT_EQ(Result.m_Image.m_pData[0], 127);
	EXPECT_EQ(Result.m_Image.m_pData[3], 255);
	EXPECT_EQ(Result.m_Image.m_pData[7], 255);
}

TEST_F(QmThemeIconJob, LargePngKeepsExistingMaximumEdgeAndAspectRatio)
{
	ASSERT_NO_FATAL_FAILURE(Store(1024, 2));
	auto pJob = Run();
	ASSERT_NE(pJob, nullptr);
	auto Result = pJob->TakeResult();
	ASSERT_TRUE(Result.m_Success);
	EXPECT_EQ(Result.m_Image.m_Width, 512u);
	EXPECT_EQ(Result.m_Image.m_Height, 1u);
	ASSERT_NE(Result.m_Image.m_pData, nullptr);
	EXPECT_EQ(Result.m_Image.m_pData[0], 127);
}
