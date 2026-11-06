#include <game/client/components/qmclient/settings_asset_image_job.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>
#include <test/test.h>

#include <array>
#include <cstdlib>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
	void AllocatePixel(CImageInfo &Image)
	{
		Image.m_Width = Image.m_Height = 1;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.m_pData = static_cast<uint8_t *>(malloc(4));
		if(!Image.m_pData)
			throw std::bad_alloc();
	}
	bool ThrowAllocationAfterPixels(const void *, unsigned, const char *, CImageInfo &Image)
	{
		AllocatePixel(Image);
		throw std::bad_alloc();
	}
	bool ThrowUnknownAfterPixels(const void *, unsigned, const char *, CImageInfo &Image)
	{
		AllocatePixel(Image);
		throw 7;
	}
	bool RejectAfterPixels(const void *, unsigned, const char *, CImageInfo &Image)
	{
		AllocatePixel(Image);
		return false;
	}
	bool DecodePng(const void *pData, unsigned Size, const char *pName, CImageInfo &Image)
	{
		return CImageLoader::LoadPng(pData, Size, pName, Image);
	}
	void KeepOriginalImage(CImageInfo &, int, int) {}
	void ThrowDuringResize(CImageInfo &, int, int) { throw std::runtime_error("native resize boundary failed"); }

	class CImageAdmissionEngine : public IEngine
	{
		CJobPool &m_Pool;

	public:
		bool m_Accept = true;
		bool m_Throw = false;
		std::weak_ptr<IJob> m_Attempt;
		explicit CImageAdmissionEngine(CJobPool &Pool) :
			m_Pool(Pool) {}
		void Init() override {}
		void AddJob(std::shared_ptr<IJob> pJob) override { (void)TryAddJob(std::move(pJob)); }
		bool TryAddJob(std::shared_ptr<IJob> pJob) override
		{
			m_Attempt = pJob;
			if(m_Throw)
				throw std::bad_alloc();
			return m_Accept && m_Pool.TryAdd(std::move(pJob));
		}
		void ShutdownJobs() override { m_Pool.Shutdown(); }
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
	};

	class CFailAfterPublication : public CFullAsyncImageLoadJob
	{
		void Run() override
		{
			CFullAsyncImageLoadJob::Run();
			throw std::bad_alloc();
		}

	public:
		explicit CFailAfterPublication(IStorage *pStorage) :
			CFullAsyncImageLoadJob({"image.png"}, pStorage, "image", IStorage::TYPE_SAVE) {}
	};

	class QmSettingsAssetImageJob : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CJobPool m_Pool;
		bool m_Started = false;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_Started = m_Pool.TryInit(1);
			ASSERT_TRUE(m_Started);
		}
		void TearDown() override
		{
			if(m_Started)
				m_Pool.Shutdown();
		}
		void StorePng()
		{
			std::array<uint8_t, 8> aPixels{11, 22, 33, 255, 44, 55, 66, 255};
			CImageInfo Image;
			Image.m_Width = 2;
			Image.m_Height = 1;
			Image.m_Format = CImageInfo::FORMAT_RGBA;
			Image.m_pData = aPixels.data();
			ASSERT_TRUE(CImageLoader::SavePng(m_pStorage->OpenFile("image.png", IOFLAG_WRITE, IStorage::TYPE_SAVE), "image.png", Image));
		}
		std::shared_ptr<CFullAsyncImageLoadJob> NewJob(int MaxSize = 0)
		{
			return std::make_shared<CFullAsyncImageLoadJob>(std::vector<std::string>{"missing.png", "image.png"}, m_pStorage.get(), "image", IStorage::TYPE_SAVE, MaxSize);
		}
		void Execute(const std::shared_ptr<IJob> &pJob)
		{
			ASSERT_TRUE(m_Pool.TryAdd(pJob));
			ASSERT_TRUE(WaitForJobFinished(*pJob));
		}
		void ExpectEmptyFailure(const std::shared_ptr<CFullAsyncImageLoadJob> &pJob)
		{
			EXPECT_TRUE(pJob->IsCompleted());
			auto Result = pJob->GetResult();
			EXPECT_FALSE(Result.m_Success);
			EXPECT_EQ(Result.m_Image.m_pData, nullptr);
		}
	};
}

TEST_F(QmSettingsAssetImageJob, PngPixelsTransferOnceAndSurviveJobRelease)
{
	ASSERT_NO_FATAL_FAILURE(StorePng());
	auto pJob = NewJob();
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	auto Result = pJob->GetResult();
	ASSERT_TRUE(Result.m_Success);
	ASSERT_NE(Result.m_Image.m_pData, nullptr);
	EXPECT_EQ(Result.m_Image.m_Width, 2u);
	EXPECT_EQ(pJob->GetResult().m_Image.m_pData, nullptr);
	m_Pool.Shutdown();
	m_Started = false;
	pJob.reset();
	EXPECT_EQ(Result.m_Image.m_pData[0], 11);
	EXPECT_EQ(Result.m_Image.m_pData[4], 44);
}

TEST_F(QmSettingsAssetImageJob, MissingFilesPublishCompletedEmptyFailure)
{
	auto pJob = NewJob();
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	ExpectEmptyFailure(pJob);
	EXPECT_EQ(pJob->State(), IJob::STATE_DONE);
}

TEST_F(QmSettingsAssetImageJob, FirstReadableCandidateKeepsExistingPriorityEvenWhenDecodeFails)
{
	ASSERT_NO_FATAL_FAILURE(StorePng());
	IOHANDLE File = m_pStorage->OpenFile("first.png", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	const unsigned Written = io_write(File, "invalid", 7);
	const int Closed = io_close(File);
	ASSERT_EQ(Written, 7u);
	ASSERT_EQ(Closed, 0);
	auto pJob = std::make_shared<CFullAsyncImageLoadJob>(std::vector<std::string>{"first.png", "image.png"}, m_pStorage.get(), "image", IStorage::TYPE_SAVE);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	ExpectEmptyFailure(pJob);
}

TEST_F(QmSettingsAssetImageJob, AllocationExceptionAfterPixelsCompletesFailureAndNextJobRuns)
{
	ASSERT_NO_FATAL_FAILURE(StorePng());
	auto pJob = std::make_shared<CFullAsyncImageLoadJob>(std::vector<std::string>{"image.png"}, m_pStorage.get(), "image", IStorage::TYPE_SAVE, 0, ThrowAllocationAfterPixels);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_EQ(pJob->State(), IJob::STATE_FAILED);
	ExpectEmptyFailure(pJob);
	auto pNext = NewJob();
	ASSERT_NO_FATAL_FAILURE(Execute(pNext));
	EXPECT_TRUE(pNext->GetResult().m_Success);
}

TEST_F(QmSettingsAssetImageJob, UnknownExceptionAfterPixelsDoesNotLeaveConsumerWaiting)
{
	ASSERT_NO_FATAL_FAILURE(StorePng());
	auto pJob = std::make_shared<CFullAsyncImageLoadJob>(std::vector<std::string>{"image.png"}, m_pStorage.get(), "image", IStorage::TYPE_SAVE, 0, ThrowUnknownAfterPixels);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_EQ(pJob->State(), IJob::STATE_FAILED);
	ExpectEmptyFailure(pJob);
}

TEST_F(QmSettingsAssetImageJob, DecoderFailureDoesNotPublishAllocatedPixels)
{
	ASSERT_NO_FATAL_FAILURE(StorePng());
	auto pJob = std::make_shared<CFullAsyncImageLoadJob>(std::vector<std::string>{"image.png"}, m_pStorage.get(), "image", IStorage::TYPE_SAVE, 0, RejectAfterPixels);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_EQ(pJob->State(), IJob::STATE_DONE);
	ExpectEmptyFailure(pJob);
}

TEST_F(QmSettingsAssetImageJob, ResizeThatRetainsOversizeOriginalPublishesFailure)
{
	ASSERT_NO_FATAL_FAILURE(StorePng());
	auto pJob = std::make_shared<CFullAsyncImageLoadJob>(std::vector<std::string>{"image.png"}, m_pStorage.get(), "image", IStorage::TYPE_SAVE, 1, DecodePng, KeepOriginalImage);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	auto Result = pJob->GetResult();
	EXPECT_FALSE(Result.m_Success);
	EXPECT_FALSE(Result.m_Resized);
	EXPECT_EQ(Result.m_Image.m_pData, nullptr);
	EXPECT_EQ(Result.m_SourceWidth, 2);
}

TEST_F(QmSettingsAssetImageJob, NativeResizePublishesTargetAndSourceDimensions)
{
	ASSERT_NO_FATAL_FAILURE(StorePng());
	auto pJob = NewJob(1);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	auto Result = pJob->GetResult();
	ASSERT_TRUE(Result.m_Success);
	EXPECT_TRUE(Result.m_Resized);
	EXPECT_EQ(Result.m_SourceWidth, 2);
	EXPECT_EQ(Result.m_SourceHeight, 1);
	EXPECT_EQ(Result.m_Image.m_Width, 1u);
	EXPECT_EQ(Result.m_Image.m_Height, 1u);
}

TEST_F(QmSettingsAssetImageJob, ResizeExceptionDoesNotLeaveConsumerWaiting)
{
	ASSERT_NO_FATAL_FAILURE(StorePng());
	auto pJob = std::make_shared<CFullAsyncImageLoadJob>(std::vector<std::string>{"image.png"}, m_pStorage.get(), "image", IStorage::TYPE_SAVE, 1, DecodePng, ThrowDuringResize);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_EQ(pJob->State(), IJob::STATE_FAILED);
	ExpectEmptyFailure(pJob);
}

TEST_F(QmSettingsAssetImageJob, EarlyEmptyResultDoesNotConsumeFuturePublication)
{
	ASSERT_NO_FATAL_FAILURE(StorePng());
	auto pJob = NewJob();
	EXPECT_FALSE(pJob->IsCompleted());
	EXPECT_FALSE(pJob->GetResult().m_Success);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_TRUE(pJob->GetResult().m_Success);
}

TEST_F(QmSettingsAssetImageJob, LaterExecutionFailureKeepsAlreadyPublishedCompleteImage)
{
	ASSERT_NO_FATAL_FAILURE(StorePng());
	auto pJob = std::make_shared<CFailAfterPublication>(m_pStorage.get());
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_EQ(pJob->State(), IJob::STATE_FAILED);
	EXPECT_TRUE(pJob->IsCompleted());
	auto Result = pJob->GetResult();
	ASSERT_TRUE(Result.m_Success);
	ASSERT_NE(Result.m_Image.m_pData, nullptr);
	EXPECT_EQ(Result.m_Image.m_pData[0], 11);
}

TEST_F(QmSettingsAssetImageJob, RejectedSubmissionReleasesCandidateWithoutPublishingWaiter)
{
	CImageAdmissionEngine Engine(m_Pool);
	Engine.m_Accept = false;
	EXPECT_EQ(QmTryQueueSettingsAssetImage({"image.png"}, m_pStorage.get(), Engine, "image", IStorage::TYPE_SAVE, 0), nullptr);
	EXPECT_TRUE(Engine.m_Attempt.expired());
}

TEST_F(QmSettingsAssetImageJob, SubmissionAllocationExceptionDoesNotLatchCandidateAndLaterCallRecovers)
{
	ASSERT_NO_FATAL_FAILURE(StorePng());
	CImageAdmissionEngine Engine(m_Pool);
	Engine.m_Throw = true;
	EXPECT_EQ(QmTryQueueSettingsAssetImage({"image.png"}, m_pStorage.get(), Engine, "image", IStorage::TYPE_SAVE, 0), nullptr);
	EXPECT_TRUE(Engine.m_Attempt.expired());
	Engine.m_Throw = false;
	auto pJob = QmTryQueueSettingsAssetImage({"image.png"}, m_pStorage.get(), Engine, "image", IStorage::TYPE_SAVE, 0);
	ASSERT_NE(pJob, nullptr);
	ASSERT_TRUE(WaitForJobFinished(*pJob));
	EXPECT_TRUE(pJob->GetResult().m_Success);
}

TEST_F(QmSettingsAssetImageJob, AcceptedNativeJobReturnsTrackedOwnerAndCompleteResult)
{
	ASSERT_NO_FATAL_FAILURE(StorePng());
	CImageAdmissionEngine Engine(m_Pool);
	auto pJob = QmTryQueueSettingsAssetImage({"image.png"}, m_pStorage.get(), Engine, "image", IStorage::TYPE_SAVE, 0);
	ASSERT_NE(pJob, nullptr);
	ASSERT_TRUE(WaitForJobFinished(*pJob));
	ASSERT_TRUE(pJob->IsCompleted());
	auto Result = pJob->GetResult();
	EXPECT_TRUE(Result.m_Success);
	EXPECT_EQ(Result.m_Image.m_Width, 2u);
}
