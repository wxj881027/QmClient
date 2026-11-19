#include <engine/gfx/image_loader.h>

#include <game/client/components/qmclient/settings_resource_preview.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/support/job_wait.h>
#include <test/test.h>

#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <utility>

namespace
{
	struct CMemoryPreviewImage
	{
		CImageInfo m_Image;
		explicit CMemoryPreviewImage(size_t Width = 2, size_t Height = 1)
		{
			m_Image.m_Width = Width;
			m_Image.m_Height = Height;
			m_Image.m_Format = CImageInfo::FORMAT_RGBA;
			m_Image.m_pData = static_cast<uint8_t *>(calloc(Width * Height, 4));
		}
		~CMemoryPreviewImage() { m_Image.Free(); }
	};

	class CInlineMemoryPreviewJob : public CSettingsResourcePreviewJob
	{
	public:
		using CSettingsResourcePreviewJob::CSettingsResourcePreviewJob;
		void Execute() { Run(); }
	};

	// 仅替代设备接收边界；真实管线和预算决定何时转交与归还所有权。
	class CDeferredMemoryPreviewGraphics : public CIconBenchmarkGraphics
	{
		CImageInfo m_Source;
		SGraphicsUploadRelease m_Release;

	public:
		bool m_Accept = false;
		bool m_Throw = false;
		bool m_Unsupported = false;
		bool m_ConsumeFailure = false;
		int m_Attempts = 0;
		int m_Unloads = 0;
		~CDeferredMemoryPreviewGraphics() override { CompleteCommand(); }
		void CompleteCommand()
		{
			m_Source.Free();
			const auto Owned = std::exchange(m_Release, {});
			if(Owned.m_pfnRelease != nullptr)
				Owned.m_pfnRelease(Owned.m_pContext);
		}
		CTextureHandle ExistingTexture() { return CreateTextureHandle(m_NextId++); }
		void UnloadTexture(CTextureHandle *pTexture) override
		{
			if(pTexture->IsValid())
				++m_Unloads;
			pTexture->Invalidate();
		}
		bool TryLoadTextureRawMoveOwned(CImageInfo &Image, int Flags, const char *pName, SGraphicsUploadRelease &Release, CTextureHandle &Texture) override
		{
			++m_Attempts;
			Texture.Invalidate();
			if(m_Unsupported)
				return IGraphics::TryLoadTextureRawMoveOwned(Image, Flags, pName, Release, Texture);
			if(m_Throw)
				throw 17;
			if(!m_Accept)
				return false;
			if(m_ConsumeFailure)
			{
				Image.Free();
				const auto Owned = std::exchange(Release, {});
				if(Owned.m_pfnRelease != nullptr)
					Owned.m_pfnRelease(Owned.m_pContext);
				return true;
			}
			if(m_Source.m_pData != nullptr)
				throw std::logic_error("previous command has not completed");
			m_Source = std::move(Image);
			m_Release = std::exchange(Release, {});
			Texture = ExistingTexture();
			++m_Uploads;
			return true;
		}
	};

	SResourcePreviewKey MemoryPreviewKey(const char *pType = "skin", const char *pId = "image")
	{
		SResourcePreviewKey Key;
		Key.m_Type = pType;
		Key.m_Id = pId;
		return Key;
	}

	class ResourcePreviewMemory : public ::testing::Test
	{
	protected:
		CSettingsAssetMemoryBudget m_Budget{1024};
		CSettingsResourcePreviewCache m_Cache;
		CSettingsResourcePreviewUploadScheduler m_Uploads;
		CDeferredMemoryPreviewGraphics m_Graphics;
		SResourcePreviewTelemetry m_Telemetry;
		SResourcePreviewUploadBudget m_Frame;
		void SetUp() override { m_Frame.m_MaxUploads = 1; }
		void TearDown() override
		{
			m_Uploads.Clear();
			m_Graphics.CompleteCommand();
			m_Cache.Clear(&m_Graphics);
			EXPECT_EQ(m_Budget.Used(), 0u);
		}
	};
}

TEST_F(ResourcePreviewMemory, ImageJobAdmissionFailurePreservesUnconsumedInput)
{
	CSettingsAssetMemoryBudget Budget(15);
	CMemoryPreviewImage Input;
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	auto *pPixels = Input.m_Image.m_pData;
	EXPECT_THROW(CSettingsResourcePreviewJob("input", std::move(Input.m_Image), 0, &Budget), std::bad_alloc);
	EXPECT_EQ(Input.m_Image.m_pData, pPixels);
	EXPECT_EQ(Budget.Used(), 0u);
}

TEST_F(ResourcePreviewMemory, ArtifactQuotaDefersWithoutTakingImageAndRetryCanResize)
{
	CSettingsAssetMemoryBudget Budget(256);
	CSettingsAssetMemoryLease Blocker;
	ASSERT_TRUE(Budget.TryAcquire(1, Blocker));
	CMemoryPreviewImage Input(8, 4);
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	auto *pPixels = Input.m_Image.m_pData;
	auto Deferred = BuildPreviewArtifact(std::move(Input.m_Image), 4, nullptr, &Budget);
	EXPECT_TRUE(Deferred.m_Deferred);
	EXPECT_FALSE(Deferred.m_Success);
	EXPECT_EQ(Input.m_Image.m_pData, pPixels);
	EXPECT_EQ(Budget.Used(), 1u);
	Blocker.Reset();
	{
		auto Ready = BuildPreviewArtifact(std::move(Input.m_Image), 4, nullptr, &Budget);
		ASSERT_TRUE(Ready.m_Success);
		EXPECT_EQ(Ready.m_Image.m_Width, 4u);
		EXPECT_EQ(Ready.m_Image.m_Height, 2u);
		EXPECT_EQ(Ready.m_MemoryLease.Bytes(), 64u);
		EXPECT_EQ(Budget.Used(), 64u);
		EXPECT_EQ(Input.m_Image.m_pData, nullptr);
	}
	EXPECT_EQ(Budget.Used(), 0u);
}

TEST_F(ResourcePreviewMemory, EnqueuePreparationFailureRetainsBothOwnersForRetry)
{
	CMemoryPreviewImage Input;
	CSettingsAssetMemoryLease Lease;
	ASSERT_TRUE(SettingsAssetMemoryTryAcquireImage(Input.m_Image, Lease, &m_Budget));
	auto *pPixels = Input.m_Image.m_pData;
	EXPECT_FALSE(m_Uploads.EnqueueUploadToTarget(MemoryPreviewKey(), std::move(Input.m_Image), []() -> CSettingsResourcePreviewUploadScheduler::TUploadFinalize { throw std::bad_alloc(); }, "image", &Lease));
	EXPECT_EQ(Input.m_Image.m_pData, pPixels);
	EXPECT_EQ(Lease.Bytes(), 16u);
	EXPECT_EQ(m_Budget.Used(), 16u);
	ASSERT_TRUE(m_Uploads.EnqueueUpload(MemoryPreviewKey(), std::move(Input.m_Image), "retry", &Lease));
	EXPECT_FALSE(Lease.HasReservation());
	EXPECT_EQ(Input.m_Image.m_pData, nullptr);
	EXPECT_EQ(m_Budget.Used(), 16u);
}

TEST_F(ResourcePreviewMemory, NativeRefusalRetainsQueueQuotaTextureAndFrameBudget)
{
	CMemoryPreviewImage Input;
	CSettingsAssetMemoryLease Lease;
	ASSERT_TRUE(SettingsAssetMemoryTryAcquireImage(Input.m_Image, Lease, &m_Budget));
	const auto Key = MemoryPreviewKey();
	const auto OldTexture = m_Graphics.ExistingTexture();
	m_Cache.MarkTextureReady(Key, OldTexture);
	ASSERT_TRUE(m_Uploads.EnqueueUpload(Key, std::move(Input.m_Image), "image", &Lease));
	SSettingsResourceMergeBudget Merge;
	SSettingsWarmupFrameBudget Frame;
	const auto BeforeMerge = Merge.m_MaxGpuUploads, BeforeFrame = Frame.m_MaxGpuUploads;
	m_Frame.m_pMergeBudget = &Merge;
	m_Frame.m_pFrameBudget = &Frame;
	EXPECT_EQ(m_Uploads.Drain(m_Frame, m_Telemetry, m_Cache, &m_Graphics), 0);
	EXPECT_EQ(m_Uploads.QueueDepth(), 1u);
	EXPECT_EQ(m_Graphics.m_Attempts, 1);
	EXPECT_EQ(m_Frame.m_UploadsUsed, 0);
	EXPECT_EQ(Merge.m_MaxGpuUploads, BeforeMerge);
	EXPECT_EQ(Frame.m_MaxGpuUploads, BeforeFrame);
	EXPECT_EQ(m_Budget.Used(), 16u);
	ASSERT_NE(m_Cache.Find(Key), nullptr);
	EXPECT_EQ(m_Cache.Find(Key)->m_Texture, OldTexture);
	EXPECT_TRUE(m_Cache.Find(Key)->m_TextureReady);
	EXPECT_EQ(m_Graphics.m_Unloads, 0);
	m_Graphics.m_Accept = true;
	ASSERT_TRUE(m_Uploads.DrainOne(m_Frame, m_Telemetry, m_Cache, &m_Graphics));
	EXPECT_EQ(Merge.m_MaxGpuUploads, BeforeMerge - 1);
	EXPECT_EQ(Frame.m_MaxGpuUploads, BeforeFrame - 1);
	EXPECT_EQ(m_Uploads.QueueDepth(), 0u);
	EXPECT_EQ(m_Budget.Used(), 16u);
	EXPECT_EQ(m_Graphics.m_Unloads, 1);
	m_Cache.Invalidate({}, &m_Graphics);
	EXPECT_EQ(m_Budget.Used(), 16u);
	m_Graphics.CompleteCommand();
	EXPECT_EQ(m_Budget.Used(), 0u);
}

TEST_F(ResourcePreviewMemory, UnknownNativePreparationExceptionRestoresQuotaForRetry)
{
	CMemoryPreviewImage Input;
	CSettingsAssetMemoryLease Lease;
	ASSERT_TRUE(SettingsAssetMemoryTryAcquireImage(Input.m_Image, Lease, &m_Budget));
	ASSERT_TRUE(m_Uploads.EnqueueUpload(MemoryPreviewKey(), std::move(Input.m_Image), "image", &Lease));
	m_Graphics.m_Throw = true;
	EXPECT_THROW(m_Uploads.DrainOne(m_Frame, m_Telemetry, m_Cache, &m_Graphics), int);
	EXPECT_EQ(m_Uploads.QueueDepth(), 1u);
	EXPECT_EQ(m_Budget.Used(), 16u);
	EXPECT_EQ(m_Frame.m_UploadsUsed, 0);
	m_Graphics.m_Throw = false;
	m_Graphics.m_Accept = true;
	ASSERT_TRUE(m_Uploads.DrainOne(m_Frame, m_Telemetry, m_Cache, &m_Graphics));
	EXPECT_EQ(m_Budget.Used(), 16u);
	m_Graphics.CompleteCommand();
	EXPECT_EQ(m_Budget.Used(), 0u);
}

TEST_F(ResourcePreviewMemory, UnsupportedOwnedUploadDoesNotCallLegacyOrLoseQueuedPixels)
{
	CMemoryPreviewImage Input;
	CSettingsAssetMemoryLease Lease;
	ASSERT_TRUE(SettingsAssetMemoryTryAcquireImage(Input.m_Image, Lease, &m_Budget));
	ASSERT_TRUE(m_Uploads.EnqueueUpload(MemoryPreviewKey(), std::move(Input.m_Image), "image", &Lease));
	m_Graphics.m_Unsupported = true;
	EXPECT_FALSE(m_Uploads.DrainOne(m_Frame, m_Telemetry, m_Cache, &m_Graphics));
	EXPECT_EQ(m_Uploads.QueueDepth(), 1u);
	EXPECT_EQ(m_Graphics.m_Uploads, 0u);
	EXPECT_EQ(m_Budget.Used(), 16u);
	m_Uploads.Clear();
	EXPECT_EQ(m_Budget.Used(), 0u);
}

TEST_F(ResourcePreviewMemory, TargetFinalizesOnceOnAdmissionWhileNativeRetainsQuota)
{
	CMemoryPreviewImage Input;
	CSettingsAssetMemoryLease Lease;
	ASSERT_TRUE(SettingsAssetMemoryTryAcquireImage(Input.m_Image, Lease, &m_Budget));
	int Calls = 0;
	ASSERT_TRUE(m_Uploads.EnqueueUploadToTarget(MemoryPreviewKey(), std::move(Input.m_Image), [&] { return [&](bool Success, IGraphics::CTextureHandle Texture) {
														++Calls;
														EXPECT_TRUE(Success);
														EXPECT_TRUE(Texture.IsValid());
														EXPECT_EQ(m_Budget.Used(), 16u);
														m_Graphics.UnloadTexture(&Texture);
													}; }, "image", &Lease));
	EXPECT_FALSE(m_Uploads.DrainOne(m_Frame, m_Telemetry, m_Cache, &m_Graphics));
	EXPECT_EQ(Calls, 0);
	m_Graphics.m_Accept = true;
	ASSERT_TRUE(m_Uploads.DrainOne(m_Frame, m_Telemetry, m_Cache, &m_Graphics));
	EXPECT_EQ(Calls, 1);
	m_Uploads.Clear();
	EXPECT_EQ(m_Budget.Used(), 16u);
	m_Graphics.CompleteCommand();
	EXPECT_EQ(m_Budget.Used(), 0u);
	EXPECT_EQ(Calls, 1);
}

TEST_F(ResourcePreviewMemory, ConsumedNativeFailureReleasesQuotaAndFinalizesFailureOnce)
{
	CMemoryPreviewImage Input;
	CSettingsAssetMemoryLease Lease;
	ASSERT_TRUE(SettingsAssetMemoryTryAcquireImage(Input.m_Image, Lease, &m_Budget));
	int Calls = 0;
	const auto Key = MemoryPreviewKey();
	ASSERT_TRUE(m_Uploads.EnqueueUploadToTarget(Key, std::move(Input.m_Image), [&] { return [&](bool Success, IGraphics::CTextureHandle Texture) {
												 ++Calls;
												 EXPECT_FALSE(Success);
												 EXPECT_FALSE(Texture.IsValid());
												 EXPECT_EQ(m_Budget.Used(), 0u);
											 }; }, "image", &Lease));
	m_Graphics.m_Accept = m_Graphics.m_ConsumeFailure = true;
	EXPECT_FALSE(m_Uploads.DrainOne(m_Frame, m_Telemetry, m_Cache, &m_Graphics));
	EXPECT_EQ(m_Uploads.QueueDepth(), 0u);
	EXPECT_EQ(m_Budget.Used(), 0u);
	EXPECT_EQ(Calls, 1);
	ASSERT_NE(m_Cache.Find(Key), nullptr);
	EXPECT_TRUE(m_Cache.Find(Key)->m_Failed);
	EXPECT_EQ(m_Telemetry.m_PreviewUploads, 0);
	EXPECT_FALSE(m_Uploads.DrainOne(m_Frame, m_Telemetry, m_Cache, &m_Graphics));
	EXPECT_EQ(Calls, 1);
}

TEST_F(ResourcePreviewMemory, ScopedQueueInvalidationReleasesMatchingQuotaWithoutFinalizing)
{
	int Calls = 0;
	for(const char *pType : {"skin", "entity"})
	{
		CMemoryPreviewImage Input;
		CSettingsAssetMemoryLease Lease;
		ASSERT_TRUE(SettingsAssetMemoryTryAcquireImage(Input.m_Image, Lease, &m_Budget));
		ASSERT_TRUE(m_Uploads.EnqueueUploadToTarget(MemoryPreviewKey(pType), std::move(Input.m_Image), [&] { return [&](bool, IGraphics::CTextureHandle) { ++Calls; }; }, "image", &Lease));
	}
	EXPECT_EQ(m_Budget.Used(), 32u);
	m_Uploads.Invalidate({"skin"});
	EXPECT_EQ(m_Uploads.QueueDepth(), 1u);
	EXPECT_EQ(m_Budget.Used(), 16u);
	EXPECT_EQ(Calls, 0);
	m_Uploads.Clear();
	EXPECT_EQ(m_Budget.Used(), 0u);
	EXPECT_EQ(Calls, 0);
}

TEST_F(ResourcePreviewMemory, CpuPressureDiscardPreservesTextureMetadataAndOtherCategory)
{
	using TJobs = std::unordered_map<SResourcePreviewKey, std::shared_ptr<CSettingsResourcePreviewJob>, SResourcePreviewKeyHash>;
	TJobs Jobs;
	const auto Key = MemoryPreviewKey();
	const auto OldTexture = m_Graphics.ExistingTexture();
	auto &State = m_Cache.GetOrCreate(Key);
	State.m_Texture = OldTexture;
	State.m_TextureReady = State.m_MetadataReady = State.m_PreviewJobPending = State.m_ArtifactReady = State.m_UploadPending = State.m_Failed = true;
	State.m_AppearStart = 41;
	for(const char *pType : {"skin", "entity"})
	{
		CMemoryPreviewImage Input;
		auto pJob = std::make_shared<CInlineMemoryPreviewJob>("job", std::move(Input.m_Image), 0, &m_Budget);
		pJob->Execute();
		Jobs.emplace(MemoryPreviewKey(pType), std::move(pJob));
		CMemoryPreviewImage Pending;
		CSettingsAssetMemoryLease Lease;
		ASSERT_TRUE(SettingsAssetMemoryTryAcquireImage(Pending.m_Image, Lease, &m_Budget));
		ASSERT_TRUE(m_Uploads.EnqueueUpload(MemoryPreviewKey(pType), std::move(Pending.m_Image), "image", &Lease));
	}
	EXPECT_EQ(m_Budget.Used(), 64u);
	DiscardSettingsResourcePreviewInputs(m_Cache, m_Uploads, Jobs, {"skin"});
	EXPECT_EQ(Jobs.size(), 1u);
	EXPECT_EQ(m_Uploads.QueueDepth(), 1u);
	EXPECT_EQ(m_Budget.Used(), 32u);
	ASSERT_NE(m_Cache.Find(Key), nullptr);
	EXPECT_TRUE(State.m_TextureReady);
	EXPECT_EQ(State.m_Texture, OldTexture);
	EXPECT_TRUE(State.m_MetadataReady);
	EXPECT_EQ(State.m_AppearStart, 41);
	EXPECT_FALSE(State.m_PreviewJobPending || State.m_ArtifactReady || State.m_UploadPending || State.m_Failed);
	EXPECT_EQ(m_Graphics.m_Unloads, 0);
	Jobs.clear();
	m_Uploads.Clear();
	EXPECT_EQ(m_Budget.Used(), 0u);
}

TEST_F(ResourcePreviewMemory, CancelledQueuedJobReleasesInputWhenWorkerObservesCancellation)
{
	CMemoryPreviewImage Input;
	auto pJob = std::make_shared<CInlineMemoryPreviewJob>("job", std::move(Input.m_Image), 0, &m_Budget);
	EXPECT_EQ(m_Budget.Used(), 16u);
	pJob->CancelPublication();
	EXPECT_EQ(m_Budget.Used(), 16u);
	pJob->Execute();
	EXPECT_EQ(m_Budget.Used(), 0u);
	EXPECT_FALSE(pJob->TakeResult().m_Artifact.m_Success);
}

namespace
{
	class ResourcePreviewMemoryFile : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CJobPool m_Pool;
		bool m_Started = false;
		size_t m_FileBytes = 0;
		uint64_t m_WorkingBytes = 0;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			CMemoryPreviewImage Input;
			ASSERT_NE(Input.m_Image.m_pData, nullptr);
			ASSERT_TRUE(CImageLoader::SavePng(m_pStorage->OpenFile("image.png", IOFLAG_WRITE, IStorage::TYPE_SAVE), "image", Input.m_Image));
			const auto File = m_pStorage->OpenFile("image.png", IOFLAG_READ, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			const auto Length = io_length(File);
			const auto Closed = io_close(File);
			ASSERT_GT(Length, 0);
			ASSERT_EQ(Closed, 0);
			m_FileBytes = (size_t)Length;
			ASSERT_TRUE(SettingsAssetMemoryComputeWorkingBytes(m_FileBytes, 8, 0, m_WorkingBytes));
		}
		void TearDown() override
		{
			if(m_Started)
				m_Pool.Shutdown();
		}
		std::shared_ptr<CSettingsResourcePreviewJob> Create(CSettingsAssetMemoryBudget &Budget, ESettingsAssetImageAdmission &Admission)
		{
			return CSettingsResourcePreviewJob::FromPath("memory", "image.png", m_pStorage.get(), IStorage::TYPE_SAVE, 0, m_FileBytes, 8, &Budget, &Admission);
		}
		void Execute(const std::shared_ptr<IJob> &pJob)
		{
			if(!m_Started)
			{
				m_Started = m_Pool.TryInit(1);
				ASSERT_TRUE(m_Started);
			}
			ASSERT_NE(pJob, nullptr);
			ASSERT_TRUE(m_Pool.TryAdd(pJob));
			ASSERT_TRUE(WaitForJobFinished(*pJob));
		}
	};
}

TEST_F(ResourcePreviewMemoryFile, FromPathReservesBeforeWorkerAndMovesShrunkQuotaToResult)
{
	CSettingsAssetMemoryBudget Budget(m_WorkingBytes);
	ESettingsAssetImageAdmission Admission = ESettingsAssetImageAdmission::FAILED;
	auto pJob = Create(Budget, Admission);
	ASSERT_NE(pJob, nullptr);
	EXPECT_EQ(Admission, ESettingsAssetImageAdmission::ACCEPTED);
	EXPECT_EQ(Budget.Used(), m_WorkingBytes);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_EQ(Budget.Used(), 16u);
	{
		auto Result = pJob->TakeResult();
		ASSERT_TRUE(Result.m_Artifact.m_Success);
		EXPECT_EQ(Result.m_Artifact.m_MemoryLease.Bytes(), 16u);
		pJob.reset();
		EXPECT_EQ(Budget.Used(), 16u);
	}
	EXPECT_EQ(Budget.Used(), 0u);
}

TEST_F(ResourcePreviewMemoryFile, QuotaRejectionKeepsNoJobAndCanRetryAfterPriorOwnerDies)
{
	CSettingsAssetMemoryBudget Budget(m_WorkingBytes);
	ESettingsAssetImageAdmission Admission = ESettingsAssetImageAdmission::FAILED;
	auto pOwner = Create(Budget, Admission);
	ASSERT_NE(pOwner, nullptr);
	EXPECT_EQ(Create(Budget, Admission), nullptr);
	EXPECT_EQ(Admission, ESettingsAssetImageAdmission::DEFERRED);
	EXPECT_EQ(Budget.Used(), m_WorkingBytes);
	pOwner.reset();
	EXPECT_EQ(Budget.Used(), 0u);
	auto pNext = Create(Budget, Admission);
	ASSERT_NE(pNext, nullptr);
	EXPECT_EQ(Admission, ESettingsAssetImageAdmission::ACCEPTED);
	ASSERT_NO_FATAL_FAILURE(Execute(pNext));
	EXPECT_TRUE(pNext->TakeResult().m_Artifact.m_Success);
	EXPECT_EQ(Budget.Used(), 0u);
}

TEST_F(ResourcePreviewMemoryFile, ResultPublicationAndNativeHandoffNeverReturnQuotaEarly)
{
	CSettingsAssetMemoryBudget Budget(m_WorkingBytes);
	ESettingsAssetImageAdmission Admission = ESettingsAssetImageAdmission::FAILED;
	auto pJob = Create(Budget, Admission);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	CSettingsResourcePreviewCache Cache;
	CSettingsResourcePreviewUploadScheduler Uploads;
	CDeferredMemoryPreviewGraphics Graphics;
	SResourcePreviewTelemetry Telemetry;
	const auto Key = MemoryPreviewKey();
	ASSERT_TRUE(TryPublishSettingsResourcePreviewJob(*pJob, Key, Cache, Uploads, Telemetry));
	pJob.reset();
	EXPECT_EQ(Budget.Used(), 16u);
	SResourcePreviewUploadBudget Frame;
	Frame.m_MaxUploads = 1;
	EXPECT_FALSE(Uploads.DrainOne(Frame, Telemetry, Cache, &Graphics));
	EXPECT_EQ(Budget.Used(), 16u);
	Graphics.m_Accept = true;
	ASSERT_TRUE(Uploads.DrainOne(Frame, Telemetry, Cache, &Graphics));
	Uploads.Clear();
	Cache.Clear(&Graphics);
	EXPECT_EQ(Budget.Used(), 16u);
	Graphics.CompleteCommand();
	EXPECT_EQ(Budget.Used(), 0u);
}
