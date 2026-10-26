// 资源预览管道通过真实缓存、图像处理和上传队列观察状态；不启动 GPU 设备。
#include <base/borrowed_allocator.h>

#include <engine/gfx/image_loader.h>

#include <game/client/components/qmclient/settings_resource_preview.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/support/job_wait.h>
#include <test/test.h>

#include <chrono>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

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

TEST(SettingsResourcePreviewPipeline, MovedArtifactRetainsPixelsAfterSourceOwnerLeavesScope)
{
	SResourcePreviewArtifact Destination;
	{
		CPreviewImage Input(1, 1);
		ASSERT_NE(Input.m_Image.m_pData, nullptr);
		Input.m_Image.m_pData[0] = 47;
		auto Source = BuildPreviewArtifact(std::move(Input.m_Image), 4);
		ASSERT_TRUE(Source.m_Success);
		Destination = std::move(Source);
		EXPECT_EQ(Source.m_Image.m_pData, nullptr);
	}
	ASSERT_NE(Destination.m_Image.m_pData, nullptr);
	EXPECT_EQ(Destination.m_Image.m_pData[0], 47);
	EXPECT_EQ(Destination.m_Image.m_Width, 1u);
	EXPECT_TRUE(Destination.m_Success);
	CImageInfo Transferred = std::move(Destination.m_Image);
	EXPECT_EQ(Destination.m_Image.m_pData, nullptr);
	EXPECT_EQ(Transferred.m_pData[0], 47);
	Transferred.Free();
}

TEST(SettingsResourcePreviewPipeline, UploadBudgetDefersOriginalRequestAndPublishesOnNextFrame)
{
	CSettingsResourcePreviewUploadScheduler Queue;
	CSettingsResourcePreviewCache Cache;
	CIconBenchmarkGraphics Graphics;
	const auto Key = PreviewKey("deferred");
	Cache.MarkArtifactReady(Key);
	CPreviewImage Input(2, 1);
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	Queue.EnqueueUpload(Key, std::move(Input.m_Image), "deferred");
	SResourcePreviewUploadBudget Budget;
	SResourcePreviewTelemetry Telemetry;
	EXPECT_FALSE(Queue.DrainOne(Budget, Telemetry, Cache, &Graphics));
	EXPECT_EQ(Queue.QueueDepth(), 1u);
	EXPECT_EQ(Telemetry.m_UploadBudgetExhausted, 1);
	EXPECT_EQ(Graphics.m_Uploads, 0u);
	ASSERT_NE(Cache.Find(Key), nullptr);
	EXPECT_TRUE(Cache.Find(Key)->m_UploadPending);
	EXPECT_FALSE(Cache.Find(Key)->m_Failed);
	Budget.m_MaxUploads = 1;
	Telemetry = {};
	EXPECT_EQ(Queue.Drain(Budget, Telemetry, Cache, &Graphics), 1);
	EXPECT_EQ(Queue.QueueDepth(), 0u);
	EXPECT_EQ(Graphics.m_Uploads, 1u);
	EXPECT_EQ(Graphics.m_UploadBytes, 8u);
	EXPECT_EQ(Budget.m_UploadsUsed, 1);
	EXPECT_TRUE(Cache.Find(Key)->m_TextureReady);
	EXPECT_FALSE(Cache.Find(Key)->m_UploadPending);
	Cache.Clear(&Graphics);
}

TEST(SettingsResourcePreviewPipeline, TargetCallbacksKeepFifoOrderAndRunOnceAcrossDeferredFrames)
{
	CSettingsResourcePreviewUploadScheduler Queue;
	CSettingsResourcePreviewCache Cache;
	CIconBenchmarkGraphics Graphics;
	std::vector<int> Order;
	CPreviewImage First(1, 1), Second(1, 1);
	ASSERT_NE(First.m_Image.m_pData, nullptr);
	ASSERT_NE(Second.m_Image.m_pData, nullptr);
	IGraphics::CTextureHandle FirstTexture, SecondTexture;
	Queue.EnqueueUploadToTarget(PreviewKey("first"), std::move(First.m_Image), [&](bool Success, IGraphics::CTextureHandle Texture) {
		EXPECT_TRUE(Success);
		FirstTexture = Texture;
		Order.push_back(1); }, "first");
	Queue.EnqueueUploadToTarget(PreviewKey("second"), std::move(Second.m_Image), [&](bool Success, IGraphics::CTextureHandle Texture) {
		EXPECT_TRUE(Success);
		SecondTexture = Texture;
		Order.push_back(2); }, "second");
	SResourcePreviewUploadBudget Budget;
	SResourcePreviewTelemetry Telemetry;
	EXPECT_EQ(Queue.Drain(Budget, Telemetry, Cache, &Graphics), 0);
	EXPECT_TRUE(Order.empty());
	EXPECT_EQ(Queue.QueueDepth(), 2u);
	Budget.m_MaxUploads = 1;
	Telemetry = {};
	EXPECT_EQ(Queue.Drain(Budget, Telemetry, Cache, &Graphics), 1);
	EXPECT_EQ(Order, (std::vector<int>{1}));
	EXPECT_EQ(Queue.QueueDepth(), 1u);
	Budget = {};
	Budget.m_MaxUploads = 1;
	Telemetry = {};
	EXPECT_EQ(Queue.Drain(Budget, Telemetry, Cache, &Graphics), 1);
	EXPECT_EQ(Order, (std::vector<int>{1, 2}));
	EXPECT_EQ(Queue.QueueDepth(), 0u);
	EXPECT_EQ(Queue.Drain(Budget, Telemetry, Cache, &Graphics), 0);
	EXPECT_EQ(Order.size(), 2u);
	EXPECT_TRUE(FirstTexture.IsValid());
	EXPECT_TRUE(SecondTexture.IsValid());
	Graphics.UnloadTexture(&FirstTexture);
	Graphics.UnloadTexture(&SecondTexture);
}

TEST(SettingsResourcePreviewPipeline, InvalidFrontDoesNotConsumeTheFollowingValidImagesUploadBudget)
{
	CSettingsResourcePreviewUploadScheduler Queue;
	CSettingsResourcePreviewCache Cache;
	CIconBenchmarkGraphics Graphics;
	const auto InvalidKey = PreviewKey("invalid"), ValidKey = PreviewKey("valid");
	CPreviewImage Invalid(1, 1), Valid(1, 1);
	ASSERT_NE(Invalid.m_Image.m_pData, nullptr);
	ASSERT_NE(Valid.m_Image.m_pData, nullptr);
	Invalid.m_Image.m_Width = 0;
	Queue.EnqueueUpload(InvalidKey, std::move(Invalid.m_Image), "invalid");
	Queue.EnqueueUpload(ValidKey, std::move(Valid.m_Image), "valid");
	SResourcePreviewUploadBudget Budget;
	Budget.m_MaxUploads = 1;
	SResourcePreviewTelemetry Telemetry;
	EXPECT_EQ(Queue.Drain(Budget, Telemetry, Cache, &Graphics), 1);
	EXPECT_EQ(Queue.QueueDepth(), 0u);
	EXPECT_EQ(Budget.m_UploadsUsed, 1);
	EXPECT_EQ(Graphics.m_Uploads, 1u);
	EXPECT_EQ(Graphics.m_UploadBytes, 4u);
	ASSERT_NE(Cache.Find(InvalidKey), nullptr);
	EXPECT_TRUE(Cache.Find(InvalidKey)->m_Failed);
	ASSERT_NE(Cache.Find(ValidKey), nullptr);
	EXPECT_TRUE(Cache.Find(ValidKey)->m_TextureReady);
	Cache.Clear(&Graphics);
}

namespace
{
	using TPreviewJobs = std::unordered_map<SResourcePreviewKey, std::shared_ptr<CSettingsResourcePreviewJob>, SResourcePreviewKeyHash>;
	class CPreviewAdmissionEngine : public IEngine
	{
	public:
		bool m_Accept = true, m_Throw = false;
		int m_Attempts = 0;
		CJobPool *m_pPool = nullptr;
		IJob::EJobState m_LastNativeState = IJob::STATE_DONE;
		std::vector<std::shared_ptr<IJob>> m_vAccepted;
		void Init() override {}
		void AddJob(std::shared_ptr<IJob> pJob) override { (void)TryAddJob(std::move(pJob)); }
		bool TryAddJob(std::shared_ptr<IJob> pJob) override
		{
			++m_Attempts;
			if(m_Throw)
				throw std::bad_alloc();
			if(!m_Accept)
				return false;
			if(m_pPool != nullptr)
			{
				const bool Accepted = m_pPool->TryAdd(pJob);
				m_LastNativeState = pJob->State();
				return Accepted;
			}
			m_vAccepted.push_back(std::move(pJob));
			return true;
		}
		void ShutdownJobs() override {}
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
	};
	class CPreviewTrackingMemory : public IAllocationResource
	{
		void *Allocate(size_t Bytes, size_t Alignment) override
		{
			if(m_Fail)
				throw std::bad_alloc();
			return IAllocationResource::AllocateDefault(Bytes, Alignment);
		}
		void Deallocate(void *pData, size_t Bytes, size_t Alignment) noexcept override { IAllocationResource::DeallocateDefault(pData, Bytes, Alignment); }

	public:
		bool m_Fail = false;
	};
	class ResourcePreviewAdmission : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CSettingsResourcePreviewCache m_Cache;
		TPreviewJobs m_Jobs;
		CPreviewAdmissionEngine m_Engine;
		SResourcePreviewKey m_Key = PreviewKey("admission");
		std::weak_ptr<CSettingsResourcePreviewJob> m_LastCandidate;
		int m_FactoryCalls = 0;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			CPreviewImage Image(2, 1);
			ASSERT_NE(Image.m_Image.m_pData, nullptr);
			IOHANDLE File = m_pStorage->OpenFile("preview.png", IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			ASSERT_TRUE(CImageLoader::SavePng(File, "preview.png", Image.m_Image));
		}
		std::shared_ptr<CSettingsResourcePreviewJob> CreateJob()
		{
			++m_FactoryCalls;
			auto pJob = CSettingsResourcePreviewJob::FromPath("admission", "preview.png", m_pStorage.get(), IStorage::TYPE_ALL, 4);
			m_LastCandidate = pJob;
			return pJob;
		}
		bool Start()
		{
			return TryStartSettingsResourcePreviewJob(m_Cache, m_Jobs, m_Key, m_Engine, [this] { return CreateJob(); });
		}
		void ExpectUntracked()
		{
			EXPECT_TRUE(m_Jobs.empty());
			ASSERT_NE(m_Cache.Find(m_Key), nullptr);
			EXPECT_FALSE(m_Cache.Find(m_Key)->m_PreviewJobPending);
		}
	};
}

TEST_F(ResourcePreviewAdmission, RejectionCanRetryAndAcceptedIdentityIsNotDuplicated)
{
	m_Engine.m_Accept = false;
	EXPECT_FALSE(Start());
	ExpectUntracked();
	EXPECT_TRUE(m_LastCandidate.expired());
	m_Engine.m_Accept = true;
	ASSERT_TRUE(Start());
	ASSERT_EQ(m_Jobs.size(), 1u);
	const auto pAccepted = m_Jobs.at(m_Key);
	EXPECT_TRUE(m_Cache.Find(m_Key)->m_PreviewJobPending);
	EXPECT_FALSE(Start());
	EXPECT_EQ(m_Jobs.at(m_Key), pAccepted);
	EXPECT_EQ(m_Engine.m_Attempts, 2);
	EXPECT_EQ(m_FactoryCalls, 2);
}

TEST_F(ResourcePreviewAdmission, EngineExceptionWithdrawsTrackingAndRetainsPriorFailureUntilAccepted)
{
	m_Cache.MarkPreviewJobDone(m_Key, false);
	m_Engine.m_Throw = true;
	EXPECT_FALSE(Start());
	ExpectUntracked();
	EXPECT_TRUE(m_LastCandidate.expired());
	EXPECT_TRUE(m_Cache.Find(m_Key)->m_Failed);
	m_Engine.m_Throw = false;
	ASSERT_TRUE(Start());
	EXPECT_TRUE(m_Cache.Find(m_Key)->m_PreviewJobPending);
	EXPECT_FALSE(m_Cache.Find(m_Key)->m_Failed);
}

TEST_F(ResourcePreviewAdmission, TrackingAllocationFailureDoesNotSubmitCandidateAndCanRecover)
{
	CPreviewTrackingMemory Memory;
	std::unordered_map<SResourcePreviewKey, std::shared_ptr<CSettingsResourcePreviewJob>, SResourcePreviewKeyHash, std::equal_to<SResourcePreviewKey>, CBorrowedAllocator<std::pair<const SResourcePreviewKey, std::shared_ptr<CSettingsResourcePreviewJob>>>> Jobs(&Memory);
	Memory.m_Fail = true;
	EXPECT_FALSE(TryStartSettingsResourcePreviewJob(m_Cache, Jobs, m_Key, m_Engine, [this] { return CreateJob(); }));
	EXPECT_TRUE(Jobs.empty());
	EXPECT_EQ(m_Engine.m_Attempts, 0);
	EXPECT_TRUE(m_LastCandidate.expired());
	ASSERT_NE(m_Cache.Find(m_Key), nullptr);
	EXPECT_FALSE(m_Cache.Find(m_Key)->m_PreviewJobPending);
	Memory.m_Fail = false;
	ASSERT_TRUE(TryStartSettingsResourcePreviewJob(m_Cache, Jobs, m_Key, m_Engine, [this] { return CreateJob(); }));
	EXPECT_EQ(Jobs.size(), 1u);
	EXPECT_TRUE(m_Cache.Find(m_Key)->m_PreviewJobPending);
}

TEST_F(ResourcePreviewAdmission, SourcePreparationExceptionDoesNotSubmitOrPublishWaitingState)
{
	EXPECT_FALSE(TryStartSettingsResourcePreviewJob(m_Cache, m_Jobs, m_Key, m_Engine,
		[]() -> std::shared_ptr<CSettingsResourcePreviewJob> { throw std::bad_alloc(); }));
	ExpectUntracked();
	EXPECT_EQ(m_Engine.m_Attempts, 0);
	ASSERT_TRUE(Start());
	EXPECT_TRUE(m_Cache.Find(m_Key)->m_PreviewJobPending);
}

TEST_F(ResourcePreviewAdmission, MissingSourceCandidateDoesNotSubmitOrPublishWaitingState)
{
	EXPECT_FALSE(TryStartSettingsResourcePreviewJob(m_Cache, m_Jobs, m_Key, m_Engine,
		[] { return std::shared_ptr<CSettingsResourcePreviewJob>(); }));
	ExpectUntracked();
	EXPECT_EQ(m_Engine.m_Attempts, 0);
	ASSERT_TRUE(Start());
}

TEST_F(ResourcePreviewAdmission, ClosedNativePoolLeavesNoWaitingIdentityAndReopenedPoolDecodes)
{
	CJobPool Pool;
	ASSERT_TRUE(Pool.TryInit(1));
	Pool.Shutdown();
	m_Engine.m_pPool = &Pool;
	EXPECT_FALSE(Start());
	ExpectUntracked();
	EXPECT_EQ(m_Engine.m_LastNativeState, IJob::STATE_QUEUED);
	EXPECT_TRUE(m_LastCandidate.expired());
	ASSERT_TRUE(Pool.TryInit(1));
	ASSERT_TRUE(Start());
	ASSERT_EQ(m_Jobs.size(), 1u);
	const auto pJob = m_Jobs.at(m_Key);
	ASSERT_TRUE(WaitForJobDone(*pJob));
	Pool.Shutdown();
	ASSERT_TRUE(pJob->Completed());
	auto Result = pJob->TakeResult();
	EXPECT_TRUE(Result.m_Artifact.m_Success);
	EXPECT_EQ(Result.m_Artifact.m_Image.m_Width, 2u);
	EXPECT_EQ(Result.m_Artifact.m_Image.m_Height, 1u);
	EXPECT_NE(Result.m_Artifact.m_Image.m_pData, nullptr);
}

namespace
{
	// 只替换文件读取边界，任务、池终态和结果消费均使用生产实现。
	class CPreviewReadFailureStorage : public IStorage
	{
		[[noreturn]] static void Unexpected()
		{
			ADD_FAILURE() << "unexpected preview storage boundary";
			throw std::logic_error("unexpected preview storage boundary");
		}

	public:
		enum class EFailure
		{
			ALLOCATION,
			RUNTIME,
			NON_STANDARD,
		};
		struct SReadFailure
		{
		};
		EFailure m_Failure = EFailure::ALLOCATION;
		int m_Reads = 0;
		int NumPaths() const override { Unexpected(); }
		bool GetDataPath(const char *, char *, unsigned) const override { Unexpected(); }
		bool GetExecutablePath(const char *, char *, unsigned) const override { Unexpected(); }
		void ListDirectory(int, const char *, FS_LISTDIR_CALLBACK, void *) override { Unexpected(); }
		void ListDirectoryInfo(int, const char *, FS_LISTDIR_CALLBACK_FILEINFO, void *) override { Unexpected(); }
		IOHANDLE OpenFile(const char *, int, int, char *, int) override { Unexpected(); }
		bool FileExists(const char *, int) override { Unexpected(); }
		bool FolderExists(const char *, int) override { Unexpected(); }
		bool ReadFile(const char *, int, void **ppResult, unsigned *pResultLen) override
		{
			*ppResult = nullptr;
			*pResultLen = 0;
			++m_Reads;
			if(m_Failure == EFailure::ALLOCATION)
				throw std::bad_alloc();
			if(m_Failure == EFailure::RUNTIME)
				throw std::runtime_error("preview read failed");
			throw SReadFailure{};
		}
		char *ReadFileStr(const char *, int) override { Unexpected(); }
		bool RetrieveTimes(const char *, int, time_t *, time_t *) override { Unexpected(); }
		bool CalculateHashes(const char *, int, SHA256_DIGEST *, unsigned *) override { Unexpected(); }
		bool FindFile(const char *, const char *, int, char *, int) override { Unexpected(); }
		size_t FindFiles(const char *, const char *, int, std::set<std::string> *) override { Unexpected(); }
		bool RemoveFile(const char *, int) override { Unexpected(); }
		bool RemoveFolder(const char *, int) override { Unexpected(); }
		bool RenameFile(const char *, const char *, int, bool) override { Unexpected(); }
		bool CreateFolder(const char *, int) override { Unexpected(); }
		void GetCompletePath(int, const char *, char *, unsigned) override { Unexpected(); }
		bool RemoveBinaryFile(const char *) override { Unexpected(); }
		bool RenameBinaryFile(const char *, const char *) override { Unexpected(); }
		const char *GetBinaryPath(const char *, char *, unsigned) override { Unexpected(); }
		const char *GetBinaryPathAbsolute(const char *, char *, unsigned) override { Unexpected(); }
	};
}

TEST(SettingsResourcePreviewWorkerFailure, AllocationExceptionCanBeConsumedAsCompletedFailure)
{
	CPreviewReadFailureStorage Storage;
	CSettingsResourcePreviewCache Cache;
	const auto Key = PreviewKey("allocation-failure");
	Cache.MarkPreviewJobStarted(Key);
	auto pJob = CSettingsResourcePreviewJob::FromPath("failure", "preview.png", &Storage, IStorage::TYPE_ALL, 4);
	CJobPool Pool;
	ASSERT_TRUE(Pool.TryInit(1));
	ASSERT_TRUE(Pool.TryAdd(pJob));
	ASSERT_TRUE(WaitForJobDone(*pJob));
	Pool.Shutdown();
	ASSERT_TRUE(pJob->Completed());
	EXPECT_EQ(Storage.m_Reads, 1);
	auto Result = pJob->TakeResult();
	EXPECT_FALSE(Result.m_Artifact.m_Success);
	EXPECT_EQ(Result.m_Artifact.m_Image.m_pData, nullptr);
	Cache.MarkPreviewJobDone(Key, Result.m_Artifact.m_Success);
	ASSERT_NE(Cache.Find(Key), nullptr);
	EXPECT_FALSE(Cache.Find(Key)->m_PreviewJobPending);
	EXPECT_EQ(SettingsResourcePreviewDrawResult(*Cache.Find(Key)), ESettingsResourcePreviewDrawResult::FAILED_PLACEHOLDER);
}

TEST(SettingsResourcePreviewWorkerFailure, NonStandardExceptionCanBeConsumedAsCompletedFailure)
{
	CPreviewReadFailureStorage Storage;
	Storage.m_Failure = CPreviewReadFailureStorage::EFailure::NON_STANDARD;
	auto pJob = CSettingsResourcePreviewJob::FromPath("failure", "preview.png", &Storage, IStorage::TYPE_ALL, 4);
	CJobPool Pool;
	ASSERT_TRUE(Pool.TryInit(1));
	ASSERT_TRUE(Pool.TryAdd(pJob));
	ASSERT_TRUE(WaitForJobDone(*pJob));
	Pool.Shutdown();
	ASSERT_TRUE(pJob->Completed());
	EXPECT_EQ(Storage.m_Reads, 1);
	auto Result = pJob->TakeResult();
	EXPECT_FALSE(Result.m_Artifact.m_Success);
	EXPECT_EQ(Result.m_Artifact.m_Image.m_pData, nullptr);
	auto Repeated = pJob->TakeResult();
	EXPECT_FALSE(Repeated.m_Artifact.m_Success);
	EXPECT_EQ(Repeated.m_Artifact.m_Image.m_pData, nullptr);
}

TEST(SettingsResourcePreviewWorkerFailure, RuntimeFailureAllowsRetryOnTheSameNativePool)
{
	CPreviewReadFailureStorage Storage;
	Storage.m_Failure = CPreviewReadFailureStorage::EFailure::RUNTIME;
	CSettingsResourcePreviewCache Cache;
	const auto Key = PreviewKey("retry");
	Cache.MarkPreviewJobStarted(Key);
	CJobPool Pool;
	ASSERT_TRUE(Pool.TryInit(1));
	auto pFailed = CSettingsResourcePreviewJob::FromPath("failure", "preview.png", &Storage, IStorage::TYPE_ALL, 4);
	ASSERT_TRUE(Pool.TryAdd(pFailed));
	ASSERT_TRUE(WaitForJobDone(*pFailed));
	ASSERT_TRUE(pFailed->Completed());
	auto Failed = pFailed->TakeResult();
	ASSERT_FALSE(Failed.m_Artifact.m_Success);
	Cache.MarkPreviewJobDone(Key, Failed.m_Artifact.m_Success);
	EXPECT_FALSE(Cache.Find(Key)->m_PreviewJobPending);
	EXPECT_TRUE(Cache.Find(Key)->m_Failed);

	CPreviewImage Input(2, 1);
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	auto pRetry = std::make_shared<CSettingsResourcePreviewJob>("retry", std::move(Input.m_Image), 4);
	Cache.MarkPreviewJobStarted(Key);
	ASSERT_TRUE(Pool.TryAdd(pRetry));
	ASSERT_TRUE(WaitForJobDone(*pRetry));
	Pool.Shutdown();
	ASSERT_TRUE(pRetry->Completed());
	auto Success = pRetry->TakeResult();
	ASSERT_TRUE(Success.m_Artifact.m_Success);
	Cache.MarkPreviewJobDone(Key, Success.m_Artifact.m_Success);
	EXPECT_FALSE(Cache.Find(Key)->m_PreviewJobPending);
	EXPECT_FALSE(Cache.Find(Key)->m_Failed);
	EXPECT_TRUE(Cache.Find(Key)->m_UploadPending);
	EXPECT_EQ(Success.m_Artifact.m_Image.m_Width, 2u);
}
