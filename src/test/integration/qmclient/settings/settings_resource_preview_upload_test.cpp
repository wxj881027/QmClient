#include <game/client/components/qmclient/settings_resource_preview.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/support/job_wait.h>
#include <test/support/rejectable_allocation_resource.h>

#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
	class CUploadInput
	{
	public:
		CImageInfo m_Image;
		CUploadInput()
		{
			m_Image.m_Width = 2;
			m_Image.m_Height = 1;
			m_Image.m_Format = CImageInfo::FORMAT_RGBA;
			m_Image.m_pData = static_cast<uint8_t *>(calloc(8, 1));
			if(m_Image.m_pData)
				m_Image.m_pData[0] = 47;
		}
		~CUploadInput() { m_Image.Free(); }
	};

	SResourcePreviewKey UploadKey(const char *pId = "upload")
	{
		SResourcePreviewKey Key;
		Key.m_Type = "entity_bg";
		Key.m_Id = pId;
		return Key;
	}

	class CFailAfterPreviewPublication : public CSettingsResourcePreviewJob
	{
		void Run() override
		{
			CSettingsResourcePreviewJob::Run();
			throw std::bad_alloc();
		}

	public:
		using CSettingsResourcePreviewJob::CSettingsResourcePreviewJob;
	};

	class CFailBeforePreviewPublication : public CSettingsResourcePreviewJob
	{
		void Run() override { throw std::bad_alloc(); }

	public:
		using CSettingsResourcePreviewJob::CSettingsResourcePreviewJob;
	};

	class ResourcePreviewUpload : public ::testing::Test
	{
	protected:
		CRejectableAllocationResource m_Memory;
		CSettingsResourcePreviewUploadScheduler m_Uploads{&m_Memory};
		CSettingsResourcePreviewCache m_Cache;
		CIconBenchmarkGraphics m_Graphics;
		CJobPool m_Pool;
		bool m_Started = false;
		SResourcePreviewTelemetry m_Telemetry;
		void TearDown() override
		{
			if(m_Started)
				m_Pool.Shutdown();
			m_Cache.Clear(&m_Graphics);
		}
		void FillToNodeFailure()
		{
			m_Memory.m_Reject = true;
			for(int Attempt = 0; Attempt < 1024; ++Attempt)
			{
				CUploadInput Input;
				ASSERT_NE(Input.m_Image.m_pData, nullptr);
				auto *pPixels = Input.m_Image.m_pData;
				const size_t Before = m_Uploads.QueueDepth();
				if(!m_Uploads.EnqueueUpload(UploadKey("filler"), std::move(Input.m_Image), "filler"))
				{
					EXPECT_EQ(Input.m_Image.m_pData, pPixels);
					EXPECT_EQ(m_Uploads.QueueDepth(), Before);
					return;
				}
			}
			FAIL() << "deque never requested another node within the bounded fill";
		}
		void Execute(const std::shared_ptr<IJob> &pJob)
		{
			if(!m_Started)
			{
				m_Started = m_Pool.TryInit(1);
				ASSERT_TRUE(m_Started);
			}
			ASSERT_TRUE(m_Pool.TryAdd(pJob));
			ASSERT_TRUE(WaitForJobFinished(*pJob));
		}
	};
}

TEST_F(ResourcePreviewUpload, NodeAllocationFailureRetainsOriginalPixelsAndDimensions)
{
	ASSERT_NO_FATAL_FAILURE(FillToNodeFailure());
	CUploadInput Input;
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	auto *pPixels = Input.m_Image.m_pData;
	const auto Before = m_Uploads.QueueDepth();
	EXPECT_FALSE(m_Uploads.EnqueueUpload(UploadKey(), std::move(Input.m_Image), "new"));
	ASSERT_EQ(Input.m_Image.m_pData, pPixels);
	EXPECT_EQ(Input.m_Image.m_Width, 2u);
	EXPECT_EQ(Input.m_Image.m_Height, 1u);
	EXPECT_EQ(pPixels[0], 47);
	EXPECT_EQ(m_Uploads.QueueDepth(), Before);
}

TEST_F(ResourcePreviewUpload, TargetNodeFailureDoesNotFinalizeAndSamePixelsCanRetry)
{
	ASSERT_NO_FATAL_FAILURE(FillToNodeFailure());
	CUploadInput Input;
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	auto *pPixels = Input.m_Image.m_pData;
	int Finalizations = 0;
	IGraphics::CTextureHandle Texture;
	auto Prepare = [&] {
		return [&](bool Success, IGraphics::CTextureHandle Result) {
			++Finalizations;
			EXPECT_TRUE(Success);
			Texture = Result;
		};
	};
	EXPECT_FALSE(m_Uploads.EnqueueUploadToTarget(UploadKey(), std::move(Input.m_Image), Prepare, "target"));
	EXPECT_EQ(Input.m_Image.m_pData, pPixels);
	EXPECT_EQ(Finalizations, 0);
	m_Memory.m_Reject = false;
	ASSERT_TRUE(m_Uploads.EnqueueUploadToTarget(UploadKey(), std::move(Input.m_Image), Prepare, "target"));
	EXPECT_EQ(Input.m_Image.m_pData, nullptr);
	SResourcePreviewUploadBudget Budget;
	Budget.m_MaxUploads = 1024;
	EXPECT_GT(m_Uploads.Drain(Budget, m_Telemetry, m_Cache, &m_Graphics), 0);
	EXPECT_EQ(Finalizations, 1);
	EXPECT_EQ(m_Uploads.QueueDepth(), 0u);
	EXPECT_TRUE(Texture.IsValid());
	m_Graphics.UnloadTexture(&Texture);
}

TEST_F(ResourcePreviewUpload, FailedNewRequestDoesNotDiscardPreviouslyAcceptedImages)
{
	ASSERT_NO_FATAL_FAILURE(FillToNodeFailure());
	const auto Before = m_Uploads.QueueDepth();
	CUploadInput Input;
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	EXPECT_FALSE(m_Uploads.EnqueueUpload(UploadKey(), std::move(Input.m_Image), "rejected"));
	m_Memory.m_Reject = false;
	SResourcePreviewUploadBudget Budget;
	Budget.m_MaxUploads = 1024;
	EXPECT_EQ(m_Uploads.Drain(Budget, m_Telemetry, m_Cache, &m_Graphics), static_cast<int>(Before));
	EXPECT_EQ(m_Graphics.m_Uploads, Before);
	EXPECT_EQ(m_Uploads.QueueDepth(), 0u);
	EXPECT_NE(Input.m_Image.m_pData, nullptr);
}

TEST_F(ResourcePreviewUpload, CallbackPreparationAllocationFailureLeavesImageAndQueueUntouched)
{
	CUploadInput Input;
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	auto *pPixels = Input.m_Image.m_pData;
	EXPECT_FALSE(m_Uploads.EnqueueUploadToTarget(UploadKey(), std::move(Input.m_Image), []() -> CSettingsResourcePreviewUploadScheduler::TUploadFinalize { throw std::bad_alloc(); }, nullptr));
	EXPECT_EQ(Input.m_Image.m_pData, pPixels);
	EXPECT_EQ(m_Uploads.QueueDepth(), 0u);
}

TEST_F(ResourcePreviewUpload, CallbackPreparationLengthFailureLeavesImageForRetry)
{
	CUploadInput Input;
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	auto *pPixels = Input.m_Image.m_pData;
	EXPECT_FALSE(m_Uploads.EnqueueUploadToTarget(UploadKey(), std::move(Input.m_Image), []() -> CSettingsResourcePreviewUploadScheduler::TUploadFinalize { throw std::length_error("full"); }, nullptr));
	EXPECT_EQ(Input.m_Image.m_pData, pPixels);
	EXPECT_EQ(m_Uploads.QueueDepth(), 0u);
	ASSERT_TRUE(m_Uploads.EnqueueUpload(UploadKey(), std::move(Input.m_Image), nullptr));
	EXPECT_EQ(m_Uploads.QueueDepth(), 1u);
}

TEST_F(ResourcePreviewUpload, UnknownCallbackPreparationFailurePropagatesBeforeTakingImage)
{
	CUploadInput Input;
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	auto *pPixels = Input.m_Image.m_pData;
	EXPECT_THROW(m_Uploads.EnqueueUploadToTarget(UploadKey(), std::move(Input.m_Image), []() -> CSettingsResourcePreviewUploadScheduler::TUploadFinalize { throw 7; }, nullptr), int);
	EXPECT_EQ(Input.m_Image.m_pData, pPixels);
	EXPECT_EQ(m_Uploads.QueueDepth(), 0u);
}

TEST_F(ResourcePreviewUpload, CapturedStringAllocationFailureIsCaughtInsidePreparation)
{
	CRejectableAllocationResource CaptureMemory;
	CaptureMemory.m_Reject = true;
	CUploadInput Input;
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	auto *pPixels = Input.m_Image.m_pData;
	bool Called = false;
	EXPECT_FALSE(m_Uploads.EnqueueUploadToTarget(UploadKey(), std::move(Input.m_Image), [&] {
		using TString = std::basic_string<char, std::char_traits<char>, CBorrowedAllocator<char>>;
		TString Name(256, 'n', &CaptureMemory);
		return [Name = std::move(Name), &Called](bool, IGraphics::CTextureHandle) { Called = !Name.empty(); }; }, nullptr));
	EXPECT_FALSE(Called);
	EXPECT_EQ(Input.m_Image.m_pData, pPixels);
	EXPECT_EQ(m_Uploads.QueueDepth(), 0u);
}

TEST_F(ResourcePreviewUpload, EmptyUntargetedImageDoesNotCreateRequest)
{
	CImageInfo Image;
	EXPECT_FALSE(m_Uploads.EnqueueUpload(UploadKey(), std::move(Image), nullptr));
	EXPECT_EQ(m_Uploads.QueueDepth(), 0u);
}

TEST_F(ResourcePreviewUpload, QueueOwnsPixelsAndCallbackAfterFactoryScopeEnds)
{
	std::string Published;
	IGraphics::CTextureHandle Texture;
	{
		CUploadInput Input;
		ASSERT_NE(Input.m_Image.m_pData, nullptr);
		std::string Name(256, 'n');
		ASSERT_TRUE(m_Uploads.EnqueueUploadToTarget(UploadKey(), std::move(Input.m_Image), [&] { return [Name, &Published, &Texture](bool Success, IGraphics::CTextureHandle Result) { EXPECT_TRUE(Success); Published = Name; Texture = Result; }; }, nullptr));
	}
	SResourcePreviewUploadBudget Budget;
	Budget.m_MaxUploads = 1;
	EXPECT_EQ(m_Uploads.Drain(Budget, m_Telemetry, m_Cache, &m_Graphics), 1);
	EXPECT_EQ(Published, std::string(256, 'n'));
	EXPECT_EQ(m_Graphics.m_UploadBytes, 8u);
	m_Graphics.UnloadTexture(&Texture);
}

TEST_F(ResourcePreviewUpload, UnfinishedJobPublicationLeavesCacheAndTelemetryUntouched)
{
	CUploadInput Input;
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	auto pJob = std::make_shared<CSettingsResourcePreviewJob>("unfinished", std::move(Input.m_Image), 0);
	EXPECT_FALSE(TryPublishSettingsResourcePreviewJob(*pJob, UploadKey(), m_Cache, m_Uploads, m_Telemetry));
	EXPECT_EQ(m_Cache.Size(), 0u);
	EXPECT_EQ(m_Telemetry.m_PreviewJobsDone, 0);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_TRUE(TryPublishSettingsResourcePreviewJob(*pJob, UploadKey(), m_Cache, m_Uploads, m_Telemetry));
	EXPECT_EQ(m_Telemetry.m_PreviewJobsDone, 1);
}

TEST_F(ResourcePreviewUpload, EarlyEmptyTakeDoesNotConsumeFutureCompletedPublication)
{
	CUploadInput Input;
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	auto pJob = std::make_shared<CSettingsResourcePreviewJob>("early", std::move(Input.m_Image), 0);
	EXPECT_FALSE(pJob->TakeResult().m_Artifact.m_Success);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	auto Result = pJob->TakeResult();
	ASSERT_TRUE(Result.m_Artifact.m_Success);
	ASSERT_NE(Result.m_Artifact.m_Image.m_pData, nullptr);
	EXPECT_EQ(Result.m_Artifact.m_Image.m_pData[0], 47);
	EXPECT_FALSE(pJob->TakeResult().m_Artifact.m_Success);
}

TEST_F(ResourcePreviewUpload, NativeCompletedResultSurvivesRejectedUploadAndPublishesOnceOnRetry)
{
	ASSERT_NO_FATAL_FAILURE(FillToNodeFailure());
	CUploadInput Input;
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	auto *pPixels = Input.m_Image.m_pData;
	auto pJob = std::make_shared<CSettingsResourcePreviewJob>("retry", std::move(Input.m_Image), 0);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	const auto Key = UploadKey("retry");
	m_Cache.MarkPreviewJobStarted(Key);
	const auto Before = m_Uploads.QueueDepth();
	EXPECT_FALSE(TryPublishSettingsResourcePreviewJob(*pJob, Key, m_Cache, m_Uploads, m_Telemetry));
	EXPECT_EQ(m_Uploads.QueueDepth(), Before);
	EXPECT_EQ(m_Telemetry.m_PreviewJobsDone, 0);
	ASSERT_NE(m_Cache.Find(Key), nullptr);
	EXPECT_TRUE(m_Cache.Find(Key)->m_PreviewJobPending);
	EXPECT_FALSE(m_Cache.Find(Key)->m_UploadPending);
	EXPECT_FALSE(pJob->TryPublishResult([&](auto &Result) { EXPECT_EQ(Result.m_Artifact.m_Image.m_pData, pPixels); return false; }));
	m_Memory.m_Reject = false;
	ASSERT_TRUE(TryPublishSettingsResourcePreviewJob(*pJob, Key, m_Cache, m_Uploads, m_Telemetry));
	EXPECT_EQ(m_Uploads.QueueDepth(), Before + 1);
	EXPECT_EQ(m_Telemetry.m_PreviewJobsDone, 1);
	EXPECT_FALSE(m_Cache.Find(Key)->m_PreviewJobPending);
	EXPECT_TRUE(m_Cache.Find(Key)->m_UploadPending);
	ASSERT_TRUE(TryPublishSettingsResourcePreviewJob(*pJob, Key, m_Cache, m_Uploads, m_Telemetry));
	EXPECT_EQ(m_Uploads.QueueDepth(), Before + 1);
	EXPECT_EQ(m_Telemetry.m_PreviewJobsDone, 1);
}

TEST_F(ResourcePreviewUpload, RejectedResultConsumerLeavesCompletePixelsAvailable)
{
	CUploadInput Input;
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	auto *pPixels = Input.m_Image.m_pData;
	auto pJob = std::make_shared<CSettingsResourcePreviewJob>("keep", std::move(Input.m_Image), 0);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_FALSE(pJob->TryPublishResult([](auto &) { return false; }));
	auto Result = pJob->TakeResult();
	EXPECT_TRUE(Result.m_Artifact.m_Success);
	EXPECT_EQ(Result.m_Artifact.m_Image.m_pData, pPixels);
}

TEST_F(ResourcePreviewUpload, ThrowingResultPreparationLeavesCompletePixelsForLaterPublish)
{
	CUploadInput Input;
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	auto pJob = std::make_shared<CSettingsResourcePreviewJob>("keep", std::move(Input.m_Image), 0);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_THROW(pJob->TryPublishResult([](auto &) -> bool { throw std::bad_alloc(); }), std::bad_alloc);
	EXPECT_TRUE(TryPublishSettingsResourcePreviewJob(*pJob, UploadKey(), m_Cache, m_Uploads, m_Telemetry));
	EXPECT_EQ(m_Uploads.QueueDepth(), 1u);
	EXPECT_EQ(m_Telemetry.m_PreviewJobsDone, 1);
}

TEST_F(ResourcePreviewUpload, FailedPoolStateAfterPublicationStillAllowsTakingCompletePixels)
{
	CUploadInput Input;
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	auto pJob = std::make_shared<CFailAfterPreviewPublication>("published", std::move(Input.m_Image), 0);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_EQ(pJob->State(), IJob::STATE_FAILED);
	auto Result = pJob->TakeResult();
	ASSERT_TRUE(Result.m_Artifact.m_Success);
	ASSERT_NE(Result.m_Artifact.m_Image.m_pData, nullptr);
	EXPECT_EQ(Result.m_Artifact.m_Image.m_pData[0], 47);
}

TEST_F(ResourcePreviewUpload, FailedPoolStateAfterPublicationCanStillQueueCompleteArtifact)
{
	CUploadInput Input;
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	auto pJob = std::make_shared<CFailAfterPreviewPublication>("published", std::move(Input.m_Image), 0);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_EQ(pJob->State(), IJob::STATE_FAILED);
	const auto Key = UploadKey();
	m_Cache.MarkPreviewJobStarted(Key);
	ASSERT_TRUE(TryPublishSettingsResourcePreviewJob(*pJob, Key, m_Cache, m_Uploads, m_Telemetry));
	EXPECT_EQ(m_Uploads.QueueDepth(), 1u);
	ASSERT_NE(m_Cache.Find(Key), nullptr);
	EXPECT_FALSE(m_Cache.Find(Key)->m_Failed);
	EXPECT_TRUE(m_Cache.Find(Key)->m_UploadPending);
}

TEST_F(ResourcePreviewUpload, UnpublishedWorkerFailurePublishesFailureOnceAndClearsWaitingState)
{
	CUploadInput Input;
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	auto pJob = std::make_shared<CFailBeforePreviewPublication>("failed", std::move(Input.m_Image), 0);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_EQ(pJob->State(), IJob::STATE_FAILED);
	const auto Key = UploadKey();
	m_Cache.MarkPreviewJobStarted(Key);
	ASSERT_TRUE(TryPublishSettingsResourcePreviewJob(*pJob, Key, m_Cache, m_Uploads, m_Telemetry));
	EXPECT_EQ(m_Uploads.QueueDepth(), 0u);
	ASSERT_NE(m_Cache.Find(Key), nullptr);
	EXPECT_FALSE(m_Cache.Find(Key)->m_PreviewJobPending);
	EXPECT_TRUE(m_Cache.Find(Key)->m_Failed);
	EXPECT_EQ(m_Telemetry.m_PreviewJobsDone, 1);
	ASSERT_TRUE(TryPublishSettingsResourcePreviewJob(*pJob, Key, m_Cache, m_Uploads, m_Telemetry));
	EXPECT_EQ(m_Telemetry.m_PreviewJobsDone, 1);
}
