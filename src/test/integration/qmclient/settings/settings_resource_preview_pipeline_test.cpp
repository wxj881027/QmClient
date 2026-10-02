// 资源预览管道通过真实缓存、图像处理和上传队列观察状态；不启动 GPU 设备。
#include <game/client/components/qmclient/settings_resource_preview.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <thread>
#include <utility>

namespace
{
	class CPreviewImage
	{
	public:
		CImageInfo m_Image;
		CPreviewImage(size_t Width, size_t Height)
		{
			m_Image.m_Width = Width;
			m_Image.m_Height = Height;
			m_Image.m_Format = CImageInfo::FORMAT_RGBA;
			m_Image.m_pData = static_cast<uint8_t *>(calloc(Width * Height, 4));
		}
		~CPreviewImage() { m_Image.Free(); }
	};

	SResourcePreviewKey PreviewKey(const char *pId = "preview")
	{
		SResourcePreviewKey Key;
		Key.m_Type = "skin";
		Key.m_Id = pId;
		Key.m_UiScale = 100;
		Key.m_CardWidth = 320;
		return Key;
	}
}

TEST(SettingsResourcePreviewPipeline, FailedJobCanRetryAndPublishArtifactWithoutExposingTexture)
{
	CSettingsResourcePreviewCache Cache;
	const auto Key = PreviewKey();
	Cache.MarkMetadataReady(Key);
	Cache.MarkPreviewJobStarted(Key);
	Cache.MarkPreviewJobDone(Key, false);
	ASSERT_NE(Cache.Find(Key), nullptr);
	EXPECT_FALSE(Cache.Find(Key)->m_PreviewJobPending);
	EXPECT_EQ(SettingsResourcePreviewDrawResult(*Cache.Find(Key)), ESettingsResourcePreviewDrawResult::FAILED_PLACEHOLDER);

	Cache.MarkPreviewJobStarted(Key);
	EXPECT_TRUE(Cache.Find(Key)->m_PreviewJobPending);
	EXPECT_FALSE(Cache.Find(Key)->m_Failed);
	EXPECT_EQ(SettingsResourcePreviewDrawResult(*Cache.Find(Key)), ESettingsResourcePreviewDrawResult::PLACEHOLDER);
	Cache.MarkPreviewJobDone(Key, true);
	EXPECT_FALSE(Cache.Find(Key)->m_PreviewJobPending);
	EXPECT_TRUE(Cache.Find(Key)->m_MetadataReady);
	EXPECT_TRUE(Cache.Find(Key)->m_ArtifactReady);
	EXPECT_TRUE(Cache.Find(Key)->m_UploadPending);
	EXPECT_FALSE(Cache.Find(Key)->m_TextureReady);
	EXPECT_EQ(SettingsResourcePreviewDrawResult(*Cache.Find(Key)), ESettingsResourcePreviewDrawResult::PLACEHOLDER);
}

TEST(SettingsResourcePreviewPipeline, ScaleLocaleAndWorkshopVariantsHaveIndependentFailureState)
{
	CSettingsResourcePreviewCache Cache;
	const auto Original = PreviewKey();
	Cache.MarkPreviewJobDone(Original, false);
	for(int Variant = 0; Variant < 4; ++Variant)
	{
		SCOPED_TRACE(Variant);
		auto Key = Original;
		switch(Variant)
		{
		case 0: Key.m_UiScale = 150; break;
		case 1: Key.m_LocaleHash = 42; break;
		case 2: Key.m_Workshop = true; break;
		case 3: Key.m_CardWidth = 640; break;
		}
		EXPECT_EQ(Cache.Find(Key), nullptr);
		Cache.MarkArtifactReady(Key);
		ASSERT_NE(Cache.Find(Key), nullptr);
		EXPECT_FALSE(Cache.Find(Key)->m_Failed);
		EXPECT_TRUE(Cache.Find(Key)->m_UploadPending);
		EXPECT_TRUE(Cache.Find(Original)->m_Failed);
	}
	EXPECT_EQ(Cache.Size(), 5u);
	Cache.Clear();
	EXPECT_EQ(Cache.Size(), 0u);
	EXPECT_EQ(Cache.Find(Original), nullptr);
	Cache.MarkPreviewJobStarted(Original);
	ASSERT_NE(Cache.Find(Original), nullptr);
	EXPECT_FALSE(Cache.Find(Original)->m_Failed);
}

TEST(SettingsResourcePreviewPipeline, ArtifactResizePreservesAspectAndMovesPixelOwnership)
{
	CPreviewImage Input(8, 4);
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	auto Artifact = BuildPreviewArtifact(std::move(Input.m_Image), 4);
	ASSERT_TRUE(Artifact.m_Success);
	EXPECT_EQ(Input.m_Image.m_pData, nullptr);
	EXPECT_EQ(Artifact.m_Image.m_Width, 4u);
	EXPECT_EQ(Artifact.m_Image.m_Height, 2u);
	EXPECT_TRUE(SettingsResourcePreviewImageValidForUpload(Artifact.m_Image));
	Artifact.m_Image.Free();
}

TEST(SettingsResourcePreviewPipeline, InvalidArtifactIsRejectedAndInputPixelsReleased)
{
	CPreviewImage Input(1, 1);
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	Input.m_Image.m_Width = 0;
	auto Artifact = BuildPreviewArtifact(std::move(Input.m_Image), 4);
	EXPECT_FALSE(Artifact.m_Success);
	EXPECT_EQ(Input.m_Image.m_pData, nullptr);
	EXPECT_EQ(Artifact.m_Image.m_pData, nullptr);
}

TEST(SettingsResourcePreviewPipeline, MissingGraphicsDefersQueuedWorkAndClearDropsIt)
{
	CSettingsResourcePreviewUploadScheduler Queue;
	CSettingsResourcePreviewCache Cache;
	SResourcePreviewTelemetry Telemetry;
	SResourcePreviewUploadBudget Budget;
	Budget.m_MaxUploads = 1;
	CPreviewImage Input(1, 1);
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	int Finalizations = 0;
	Queue.EnqueueUploadToTarget(PreviewKey(), std::move(Input.m_Image), [&](bool, IGraphics::CTextureHandle) { ++Finalizations; }, nullptr);
	EXPECT_EQ(Input.m_Image.m_pData, nullptr);
	ASSERT_EQ(Queue.QueueDepth(), 1u);
	EXPECT_EQ(Queue.Drain(Budget, Telemetry, Cache, nullptr), 0);
	EXPECT_EQ(Queue.QueueDepth(), 1u);
	EXPECT_EQ(Telemetry.m_UploadQueueDepth, 1);
	EXPECT_EQ(Telemetry.m_UploadBudgetExhausted, 1);
	EXPECT_EQ(Budget.m_UploadsUsed, 0);
	EXPECT_EQ(Finalizations, 0);
	EXPECT_EQ(Cache.Size(), 0u);
	Queue.Clear();
	EXPECT_EQ(Queue.QueueDepth(), 0u);
	EXPECT_EQ(Finalizations, 0);
	Queue.Clear();
	Telemetry = {};
	EXPECT_EQ(Queue.Drain(Budget, Telemetry, Cache, nullptr), 0);
	EXPECT_EQ(Telemetry.m_UploadQueueDepth, 0);
	EXPECT_EQ(Telemetry.m_UploadBudgetExhausted, 0);
}

TEST(SettingsResourcePreviewPipeline, MissingPixelsFinalizeFailureOnceWithoutEnteringQueue)
{
	CSettingsResourcePreviewUploadScheduler Queue;
	CImageInfo Image;
	int Finalizations = 0;
	Queue.EnqueueUploadToTarget(PreviewKey(), std::move(Image), [&](bool Success, IGraphics::CTextureHandle Texture) {
		++Finalizations;
		EXPECT_FALSE(Success);
		EXPECT_FALSE(Texture.IsValid()); }, nullptr);
	EXPECT_EQ(Finalizations, 1);
	EXPECT_EQ(Queue.QueueDepth(), 0u);
	Queue.Clear();
	EXPECT_EQ(Finalizations, 1);
}

TEST(SettingsResourcePreviewPipeline, WorkerPublishesResizedArtifactAndResultCanOnlyBeTakenOnce)
{
	CPreviewImage Input(8, 4);
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	auto pJob = std::make_shared<CSettingsResourcePreviewJob>("preview-test", std::move(Input.m_Image), 4);
	EXPECT_EQ(Input.m_Image.m_pData, nullptr);
	EXPECT_FALSE(pJob->Completed());
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pJob);
	const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while(!pJob->Completed() && std::chrono::steady_clock::now() < Deadline)
		std::this_thread::yield();
	const bool Completed = pJob->Completed();
	Pool.Shutdown();
	ASSERT_TRUE(Completed);
	EXPECT_EQ(pJob->State(), IJob::STATE_DONE);
	auto Result = pJob->TakeResult();
	EXPECT_TRUE(Result.m_Artifact.m_Success);
	EXPECT_EQ(Result.m_Artifact.m_Image.m_Width, 4u);
	EXPECT_EQ(Result.m_Artifact.m_Image.m_Height, 2u);
	EXPECT_NE(Result.m_Artifact.m_Image.m_pData, nullptr);
	Result.m_Artifact.m_Image.Free();
	auto Repeated = pJob->TakeResult();
	EXPECT_FALSE(Repeated.m_Artifact.m_Success);
	EXPECT_EQ(Repeated.m_Artifact.m_Image.m_pData, nullptr);
}

TEST(SettingsResourcePreviewPipeline, WorkerPublishesInvalidInputAsCompletedFailure)
{
	CImageInfo Empty;
	auto pJob = std::make_shared<CSettingsResourcePreviewJob>("preview-test", std::move(Empty), 4);
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pJob);
	const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while(!pJob->Completed() && std::chrono::steady_clock::now() < Deadline)
		std::this_thread::yield();
	const bool Completed = pJob->Completed();
	Pool.Shutdown();
	ASSERT_TRUE(Completed);
	EXPECT_EQ(pJob->State(), IJob::STATE_DONE);
	auto Result = pJob->TakeResult();
	EXPECT_FALSE(Result.m_Artifact.m_Success);
	EXPECT_EQ(Result.m_Artifact.m_Image.m_pData, nullptr);
}
