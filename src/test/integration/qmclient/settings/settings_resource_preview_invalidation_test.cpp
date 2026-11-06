// 通过生产管线观察失效；图形替身只记录句柄接管/卸载请求，不启动真实 GPU。
#include <engine/gfx/image_loader.h>

#include <game/client/components/menus.h>
#include <game/client/components/qmclient/settings_resource_preview.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/support/job_wait.h>
#include <test/support/rejectable_allocation_resource.h>
#include <test/test.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
	constexpr const char *s_pEntityType = "assets:entity_bg";
	constexpr const char *s_pGameType = "assets:game";
	using TPreviewJobs = std::unordered_map<SResourcePreviewKey, std::shared_ptr<CSettingsResourcePreviewJob>, SResourcePreviewKeyHash>;

	SResourcePreviewKey InvalidationKey(const char *pType = s_pEntityType, const char *pId = "same", bool Workshop = false, uint64_t Generation = 0)
	{
		SResourcePreviewKey Key;
		Key.m_Type = pType;
		Key.m_Id = pId;
		Key.m_Workshop = Workshop;
		Key.m_UiScale = 100;
		Key.m_CardWidth = 128;
		Key.m_ResourceGeneration = Generation;
		return Key;
	}

	class CInvalidationInput
	{
	public:
		CImageInfo m_Image;
		CInvalidationInput()
		{
			m_Image.m_Width = 2;
			m_Image.m_Height = 1;
			m_Image.m_Format = CImageInfo::FORMAT_RGBA;
			m_Image.m_pData = static_cast<uint8_t *>(calloc(8, 1));
			if(m_Image.m_pData)
				m_Image.m_pData[0] = 47;
		}
		~CInvalidationInput() { m_Image.Free(); }
	};

	class CInvalidationGraphics : public CIconBenchmarkGraphics
	{
		struct SRecord
		{
			CTextureHandle m_Handle;
			bool m_Live = false;
			int m_Unloads = 0;
		};
		std::array<SRecord, 64> m_aRecords;
		size_t m_RecordCount = 0;

		SRecord *Find(CTextureHandle Handle)
		{
			for(size_t Index = 0; Index < m_RecordCount; ++Index)
				if(m_aRecords[Index].m_Handle.Id() == Handle.Id() && m_aRecords[Index].m_Handle.Generation() == Handle.Generation())
					return &m_aRecords[Index];
			return nullptr;
		}
		void Track(CTextureHandle Handle)
		{
			if(m_RecordCount < m_aRecords.size())
				m_aRecords[m_RecordCount++] = {Handle, true, 0};
			else
				m_Overflow = true;
		}

	public:
		bool m_Overflow = false;
		bool m_InvalidUnload = false;
		int m_UnloadCalls = 0;
		CTextureHandle LoadTextureRawMove(CImageInfo &Image, int Flags, const char *pName) override
		{
			const auto Handle = CIconBenchmarkGraphics::LoadTextureRawMove(Image, Flags, pName);
			Track(Handle);
			return Handle;
		}
		// 只构造需要区分的边界身份，不表示真实设备同时持有同一编号的两代资源。
		CTextureHandle TrackedHandle(int Id, uint32_t Generation)
		{
			const auto Handle = CreateTextureHandle(Id, Generation);
			Track(Handle);
			return Handle;
		}
		void UnloadTexture(CTextureHandle *pHandle) override
		{
			if(!pHandle->IsValid() || pHandle->IsNullTexture())
				return;
			++m_UnloadCalls;
			auto *pRecord = Find(*pHandle);
			if(pRecord == nullptr || !pRecord->m_Live)
				m_InvalidUnload = true;
			if(pRecord != nullptr)
			{
				pRecord->m_Live = false;
				++pRecord->m_Unloads;
			}
			pHandle->Invalidate();
		}
		bool IsLive(CTextureHandle Handle)
		{
			const auto *pRecord = Find(Handle);
			return pRecord != nullptr && pRecord->m_Live;
		}
		int UnloadsOf(CTextureHandle Handle)
		{
			const auto *pRecord = Find(Handle);
			return pRecord != nullptr ? pRecord->m_Unloads : 0;
		}
		size_t LiveCount() const
		{
			size_t Count = 0;
			for(size_t Index = 0; Index < m_RecordCount; ++Index)
				Count += m_aRecords[Index].m_Live ? 1 : 0;
			return Count;
		}
	};

	class CPreviewGate
	{
		mutable std::mutex m_Mutex;
		std::condition_variable m_Condition;
		bool m_Entered = false;
		bool m_Released = false;
		bool m_TimedOut = false;

	public:
		void EnterAndWait()
		{
			std::unique_lock<std::mutex> Lock(m_Mutex);
			m_Entered = true;
			m_Condition.notify_all();
			if(!m_Condition.wait_for(Lock, std::chrono::seconds(2), [&] { return m_Released; }))
				m_TimedOut = true;
		}
		bool WaitForEntry()
		{
			std::unique_lock<std::mutex> Lock(m_Mutex);
			return m_Condition.wait_for(Lock, std::chrono::seconds(2), [&] { return m_Entered; });
		}
		void Release()
		{
			const std::lock_guard<std::mutex> Lock(m_Mutex);
			m_Released = true;
			m_Condition.notify_all();
		}
		bool TimedOut() const
		{
			const std::lock_guard<std::mutex> Lock(m_Mutex);
			return m_TimedOut;
		}
	};

	// 主线程断言提前返回时也释放 worker；存储本身由 fixture 保活到关池之后。
	class CReleasePreviewGate
	{
		std::shared_ptr<CPreviewGate> m_pGate;

	public:
		explicit CReleasePreviewGate(std::shared_ptr<CPreviewGate> pGate) :
			m_pGate(std::move(pGate)) {}
		~CReleasePreviewGate() { m_pGate->Release(); }
	};

	class CBlockingPreviewJob : public IJob
	{
		std::shared_ptr<CPreviewGate> m_pGate;
		void Run() override { m_pGate->EnterAndWait(); }

	public:
		explicit CBlockingPreviewJob(std::shared_ptr<CPreviewGate> pGate) :
			m_pGate(std::move(pGate)) {}
	};

	class CBlockingPreviewStorage : public IStorage
	{
		IStorage &m_Storage;
		std::shared_ptr<CPreviewGate> m_pGate;
		[[noreturn]] static void Unexpected() { throw std::logic_error("unexpected preview invalidation storage call"); }

	public:
		std::atomic<bool> m_NameRetained{false};
		std::atomic<bool> m_ReadSucceeded{false};
		CBlockingPreviewStorage(IStorage &Storage, std::shared_ptr<CPreviewGate> pGate) :
			m_Storage(Storage), m_pGate(std::move(pGate)) {}
		bool ReadFile(const char *pName, int Type, void **ppResult, unsigned *pLength) override
		{
			const std::string OriginalName = pName;
			m_pGate->EnterAndWait();
			m_NameRetained = std::strcmp(pName, OriginalName.c_str()) == 0;
			const bool Read = m_Storage.ReadFile(pName, Type, ppResult, pLength);
			m_ReadSucceeded = Read;
			return Read;
		}
		int NumPaths() const override { Unexpected(); }
		bool GetDataPath(const char *, char *, unsigned) const override { Unexpected(); }
		bool GetExecutablePath(const char *, char *, unsigned) const override { Unexpected(); }
		void ListDirectory(int, const char *, FS_LISTDIR_CALLBACK, void *) override { Unexpected(); }
		void ListDirectoryInfo(int, const char *, FS_LISTDIR_CALLBACK_FILEINFO, void *) override { Unexpected(); }
		IOHANDLE OpenFile(const char *, int, int, char *, int) override { Unexpected(); }
		bool FileExists(const char *, int) override { Unexpected(); }
		bool FolderExists(const char *, int) override { Unexpected(); }
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

	class CPreviewPoolEngine : public IEngine
	{
		CJobPool &m_Pool;

	public:
		explicit CPreviewPoolEngine(CJobPool &Pool) :
			m_Pool(Pool) {}
		void Init() override {}
		void AddJob(std::shared_ptr<IJob> pJob) override { (void)m_Pool.TryAdd(std::move(pJob)); }
		bool TryAddJob(std::shared_ptr<IJob> pJob) override { return m_Pool.TryAdd(std::move(pJob)); }
		void ShutdownJobs() override { m_Pool.Shutdown(); }
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
	};

	class ResourcePreviewInvalidation : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		std::unique_ptr<CBlockingPreviewStorage> m_pBlockingStorage;
		CJobPool m_Pool;
		bool m_PoolStarted = false;
		TPreviewJobs m_Jobs;
		CInvalidationGraphics m_Graphics;
		CSettingsResourcePreviewCache m_Cache;
		CSettingsResourcePreviewUploadScheduler m_Uploads;
		SResourcePreviewTelemetry m_Telemetry;

		void TearDown() override
		{
			if(m_PoolStarted)
				m_Pool.Shutdown();
			InvalidateSettingsResourcePreviews(m_Cache, m_Uploads, m_Jobs, &m_Graphics);
			EXPECT_EQ(m_Graphics.LiveCount(), 0u);
			EXPECT_FALSE(m_Graphics.m_Overflow);
			EXPECT_FALSE(m_Graphics.m_InvalidUnload);
		}
		void StartPool()
		{
			if(!m_PoolStarted)
			{
				m_PoolStarted = m_Pool.TryInit(1);
				ASSERT_TRUE(m_PoolStarted);
			}
		}
		void Execute(const std::shared_ptr<IJob> &pJob)
		{
			ASSERT_NO_FATAL_FAILURE(StartPool());
			ASSERT_TRUE(m_Pool.TryAdd(pJob));
			ASSERT_TRUE(WaitForJobFinished(*pJob));
		}
		std::shared_ptr<CSettingsResourcePreviewJob> NewJob()
		{
			CInvalidationInput Input;
			if(!Input.m_Image.m_pData)
				return nullptr;
			return std::make_shared<CSettingsResourcePreviewJob>("invalidation", std::move(Input.m_Image), 0);
		}
		void PrepareBlockingStorage(const std::shared_ptr<CPreviewGate> &pGate)
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			CInvalidationInput Input;
			ASSERT_NE(Input.m_Image.m_pData, nullptr);
			ASSERT_TRUE(CImageLoader::SavePng(m_pStorage->OpenFile("late.png", IOFLAG_WRITE, IStorage::TYPE_SAVE), "late.png", Input.m_Image));
			m_pBlockingStorage = std::make_unique<CBlockingPreviewStorage>(*m_pStorage, pGate);
		}
		void QueueImage(const SResourcePreviewKey &Key)
		{
			CInvalidationInput Input;
			ASSERT_NE(Input.m_Image.m_pData, nullptr);
			ASSERT_TRUE(m_Uploads.EnqueueUpload(Key, std::move(Input.m_Image), nullptr));
			m_Cache.MarkArtifactReady(Key);
		}
		void CacheTexture(const SResourcePreviewKey &Key)
		{
			CInvalidationInput Input;
			ASSERT_NE(Input.m_Image.m_pData, nullptr);
			CSettingsResourcePreviewUploadScheduler Uploads;
			ASSERT_TRUE(Uploads.EnqueueUpload(Key, std::move(Input.m_Image), nullptr));
			SResourcePreviewUploadBudget Budget;
			Budget.m_MaxUploads = 1;
			ASSERT_TRUE(Uploads.DrainOne(Budget, m_Telemetry, m_Cache, &m_Graphics));
		}
	};

	void ExpectEntityTextureFieldsInvalid(const CMenus::SCustomEntities &Entities)
	{
		EXPECT_FALSE(Entities.m_RenderTexture.IsValid());
		for(const auto &Image : Entities.m_aImages)
			EXPECT_FALSE(Image.m_Texture.IsValid());
	}
}

TEST_F(ResourcePreviewInvalidation, CancelBeforeNativeRunClosesConsumerWithoutPretendingWorkerFinished)
{
	auto pJob = NewJob();
	ASSERT_NE(pJob, nullptr);
	pJob->CancelPublication();
	pJob->CancelPublication();
	EXPECT_TRUE(pJob->Completed());
	EXPECT_FALSE(pJob->Finished());
	int Consumers = 0;
	EXPECT_TRUE(pJob->TryPublishResult([&](auto &) { ++Consumers; return true; }));
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_TRUE(pJob->Finished());
	EXPECT_FALSE(pJob->TakeResult().m_Artifact.m_Success);
	EXPECT_TRUE(TryPublishSettingsResourcePreviewJob(*pJob, InvalidationKey(), m_Cache, m_Uploads, m_Telemetry));
	EXPECT_EQ(Consumers, 0);
	EXPECT_EQ(m_Cache.Size(), 0u);
	EXPECT_EQ(m_Uploads.QueueDepth(), 0u);
	EXPECT_EQ(m_Telemetry.m_PreviewJobsDone, 0);
}

TEST_F(ResourcePreviewInvalidation, CancelAfterNativePublicationDiscardsResultBeforeAnyConsumerRuns)
{
	auto pJob = NewJob();
	ASSERT_NE(pJob, nullptr);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	pJob->CancelPublication();
	int Consumers = 0;
	EXPECT_TRUE(pJob->TryPublishResult([&](auto &) { ++Consumers; return true; }));
	EXPECT_FALSE(pJob->TakeResult().m_Artifact.m_Success);
	EXPECT_TRUE(TryPublishSettingsResourcePreviewJob(*pJob, InvalidationKey(), m_Cache, m_Uploads, m_Telemetry));
	EXPECT_EQ(Consumers, 0);
	EXPECT_EQ(m_Cache.Size(), 0u);
	EXPECT_EQ(m_Uploads.QueueDepth(), 0u);
}

TEST_F(ResourcePreviewInvalidation, QueuedOldJobCannotPublishAfterSameFullKeyIsRequestedAgain)
{
	const auto pGate = std::make_shared<CPreviewGate>();
	const CReleasePreviewGate Release(pGate);
	ASSERT_NO_FATAL_FAILURE(StartPool());
	auto pBlocker = std::make_shared<CBlockingPreviewJob>(pGate);
	ASSERT_TRUE(m_Pool.TryAdd(pBlocker));
	ASSERT_TRUE(pGate->WaitForEntry());
	CPreviewPoolEngine Engine(m_Pool);
	const auto Key = InvalidationKey();
	auto pOld = NewJob();
	ASSERT_NE(pOld, nullptr);
	ASSERT_TRUE(TryStartSettingsResourcePreviewJob(m_Cache, m_Jobs, Key, Engine, [&] { return pOld; }));
	EXPECT_FALSE(pOld->Finished());
	InvalidateSettingsResourcePreviews(m_Cache, m_Uploads, m_Jobs, &m_Graphics, {s_pEntityType, "same", false});
	EXPECT_TRUE(m_Jobs.empty());
	EXPECT_EQ(m_Cache.Find(Key), nullptr);
	EXPECT_FALSE(pOld->Finished());
	auto pNew = NewJob();
	ASSERT_NE(pNew, nullptr);
	ASSERT_TRUE(TryStartSettingsResourcePreviewJob(m_Cache, m_Jobs, Key, Engine, [&] { return pNew; }));
	ASSERT_EQ(m_Jobs.size(), 1u);
	ASSERT_NE(m_Jobs.find(Key), m_Jobs.end());
	EXPECT_EQ(m_Jobs.find(Key)->second, pNew);
	pGate->Release();
	ASSERT_TRUE(WaitForJobFinished(*pOld));
	ASSERT_TRUE(WaitForJobFinished(*pNew));
	EXPECT_FALSE(pGate->TimedOut());
	EXPECT_TRUE(TryPublishSettingsResourcePreviewJob(*pOld, Key, m_Cache, m_Uploads, m_Telemetry));
	EXPECT_EQ(m_Uploads.QueueDepth(), 0u);
	ASSERT_NE(m_Cache.Find(Key), nullptr);
	EXPECT_TRUE(m_Cache.Find(Key)->m_PreviewJobPending);
	ASSERT_TRUE(TryPublishSettingsResourcePreviewJob(*pNew, Key, m_Cache, m_Uploads, m_Telemetry));
	EXPECT_EQ(m_Uploads.QueueDepth(), 1u);
	EXPECT_EQ(m_Telemetry.m_PreviewJobsDone, 1);
}

TEST_F(ResourcePreviewInvalidation, CancellationDuringActualReadKeepsStorageInputValidAndRejectsLateResult)
{
	const auto pGate = std::make_shared<CPreviewGate>();
	const CReleasePreviewGate Release(pGate);
	ASSERT_NO_FATAL_FAILURE(PrepareBlockingStorage(pGate));
	ASSERT_NO_FATAL_FAILURE(StartPool());
	CPreviewPoolEngine Engine(m_Pool);
	auto pOld = CSettingsResourcePreviewJob::FromPath("old", "late.png", m_pBlockingStorage.get(), IStorage::TYPE_SAVE, 0);
	uint64_t Generation = 7;
	const auto OldKey = InvalidationKey(s_pEntityType, "same", false, Generation);
	ASSERT_TRUE(TryStartSettingsResourcePreviewJob(m_Cache, m_Jobs, OldKey, Engine, [&] { return pOld; }));
	ASSERT_TRUE(pGate->WaitForEntry());
	InvalidateSettingsResourcePreviews(m_Cache, m_Uploads, m_Jobs, &m_Graphics, {s_pEntityType}, &Generation, 1);
	EXPECT_EQ(Generation, 8u);
	EXPECT_TRUE(m_Jobs.empty());
	EXPECT_EQ(m_Cache.Size(), 0u);
	EXPECT_FALSE(pOld->Finished());
	pGate->Release();
	ASSERT_TRUE(WaitForJobFinished(*pOld));
	EXPECT_FALSE(pGate->TimedOut());
	EXPECT_TRUE(m_pBlockingStorage->m_NameRetained.load());
	EXPECT_TRUE(m_pBlockingStorage->m_ReadSucceeded.load());
	EXPECT_EQ(pOld->State(), IJob::STATE_DONE);
	EXPECT_TRUE(TryPublishSettingsResourcePreviewJob(*pOld, OldKey, m_Cache, m_Uploads, m_Telemetry, Generation));
	EXPECT_EQ(m_Cache.Size(), 0u);
	EXPECT_EQ(m_Uploads.QueueDepth(), 0u);
	const auto NewKey = InvalidationKey(s_pEntityType, "same", false, Generation);
	auto pNew = NewJob();
	ASSERT_NE(pNew, nullptr);
	ASSERT_TRUE(TryStartSettingsResourcePreviewJob(m_Cache, m_Jobs, NewKey, Engine, [&] { return pNew; }));
	ASSERT_TRUE(WaitForJobFinished(*pNew));
	ASSERT_TRUE(TryPublishSettingsResourcePreviewJob(*pNew, NewKey, m_Cache, m_Uploads, m_Telemetry, Generation));
	EXPECT_EQ(m_Uploads.QueueDepth(), 1u);
	EXPECT_EQ(m_Telemetry.m_PreviewJobsDone, 1);
}

TEST_F(ResourcePreviewInvalidation, AlreadyQueuedArtifactIsRemovedBeforeAnyGpuUpload)
{
	auto pJob = NewJob();
	ASSERT_NE(pJob, nullptr);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	const auto Key = InvalidationKey();
	m_Jobs.emplace(Key, pJob);
	m_Cache.MarkPreviewJobStarted(Key);
	ASSERT_TRUE(TryPublishSettingsResourcePreviewJob(*pJob, Key, m_Cache, m_Uploads, m_Telemetry));
	ASSERT_EQ(m_Uploads.QueueDepth(), 1u);
	InvalidateSettingsResourcePreviews(m_Cache, m_Uploads, m_Jobs, &m_Graphics, {s_pEntityType});
	EXPECT_TRUE(m_Jobs.empty());
	EXPECT_EQ(m_Uploads.QueueDepth(), 0u);
	EXPECT_EQ(m_Cache.Find(Key), nullptr);
	SResourcePreviewUploadBudget Budget;
	Budget.m_MaxUploads = 1;
	EXPECT_EQ(m_Uploads.Drain(Budget, m_Telemetry, m_Cache, &m_Graphics), 0);
	EXPECT_EQ(m_Graphics.m_Uploads, 0u);
}

TEST_F(ResourcePreviewInvalidation, QueuedTargetInvalidationDoesNotInvokeFinalizeOrRecreateCache)
{
	const auto Key = InvalidationKey();
	CInvalidationInput Input;
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	int Finalizations = 0;
	ASSERT_TRUE(m_Uploads.EnqueueUploadToTarget(Key, std::move(Input.m_Image), [&] { return [&](bool, IGraphics::CTextureHandle) { ++Finalizations; }; }, nullptr));
	m_Cache.MarkArtifactReady(Key);
	InvalidateSettingsResourcePreviews(m_Cache, m_Uploads, m_Jobs, &m_Graphics, {s_pEntityType});
	EXPECT_EQ(Finalizations, 0);
	EXPECT_EQ(m_Uploads.QueueDepth(), 0u);
	EXPECT_EQ(m_Cache.Size(), 0u);
	SResourcePreviewUploadBudget Budget;
	Budget.m_MaxUploads = 1;
	EXPECT_EQ(m_Uploads.Drain(Budget, m_Telemetry, m_Cache, &m_Graphics), 0);
	EXPECT_EQ(Finalizations, 0);
	EXPECT_EQ(m_Cache.Size(), 0u);
}

TEST_F(ResourcePreviewInvalidation, RemovingInterleavedRequestsKeepsSurvivingTargetsInFifoOrder)
{
	const std::array<SResourcePreviewKey, 5> Keys = {
		InvalidationKey(s_pEntityType, "remove"), InvalidationKey(s_pEntityType, "keep-one"),
		InvalidationKey(s_pEntityType, "remove", true), InvalidationKey(s_pGameType, "remove"), InvalidationKey(s_pEntityType, "keep-two")};
	std::vector<size_t> Order;
	for(size_t Index = 0; Index < Keys.size(); ++Index)
	{
		CInvalidationInput Input;
		ASSERT_NE(Input.m_Image.m_pData, nullptr);
		ASSERT_TRUE(m_Uploads.EnqueueUploadToTarget(Keys[Index], std::move(Input.m_Image), [&] { return [&, Index](bool Success, IGraphics::CTextureHandle Texture) { EXPECT_TRUE(Success); Order.push_back(Index); m_Graphics.UnloadTexture(&Texture); }; }, nullptr));
		m_Cache.MarkArtifactReady(Keys[Index]);
	}
	InvalidateSettingsResourcePreviews(m_Cache, m_Uploads, m_Jobs, &m_Graphics, {s_pEntityType, "remove"});
	EXPECT_EQ(m_Uploads.QueueDepth(), 3u);
	SResourcePreviewUploadBudget Budget;
	Budget.m_MaxUploads = 5;
	EXPECT_EQ(m_Uploads.Drain(Budget, m_Telemetry, m_Cache, &m_Graphics), 3);
	EXPECT_EQ(Order, (std::vector<size_t>{1, 3, 4}));
	EXPECT_EQ(m_Graphics.LiveCount(), 0u);
}

TEST_F(ResourcePreviewInvalidation, CategoryInvalidationPreservesOtherCategoryJobsPayloadsAndGpuOwners)
{
	const auto JobA = InvalidationKey(s_pEntityType, "job"), JobB = InvalidationKey(s_pGameType, "job");
	auto pJobA = NewJob(), pJobB = NewJob();
	ASSERT_NE(pJobA, nullptr);
	ASSERT_NE(pJobB, nullptr);
	ASSERT_NO_FATAL_FAILURE(Execute(pJobA));
	ASSERT_NO_FATAL_FAILURE(Execute(pJobB));
	m_Jobs.emplace(JobA, pJobA);
	m_Jobs.emplace(JobB, pJobB);
	m_Cache.MarkPreviewJobStarted(JobA);
	m_Cache.MarkPreviewJobStarted(JobB);
	const auto UploadA = InvalidationKey(s_pEntityType, "upload"), UploadB = InvalidationKey(s_pGameType, "upload");
	ASSERT_NO_FATAL_FAILURE(QueueImage(UploadA));
	ASSERT_NO_FATAL_FAILURE(QueueImage(UploadB));
	const auto TextureA = InvalidationKey(s_pEntityType, "texture"), TextureB = InvalidationKey(s_pGameType, "texture");
	ASSERT_NO_FATAL_FAILURE(CacheTexture(TextureA));
	ASSERT_NO_FATAL_FAILURE(CacheTexture(TextureB));
	ASSERT_NE(m_Cache.Find(TextureA), nullptr);
	ASSERT_NE(m_Cache.Find(TextureB), nullptr);
	const auto HandleA = m_Cache.Find(TextureA)->m_Texture;
	const auto HandleB = m_Cache.Find(TextureB)->m_Texture;
	std::array<uint64_t, 2> Generations{0, 0};
	InvalidateSettingsResourcePreviews(m_Cache, m_Uploads, m_Jobs, &m_Graphics, {s_pEntityType}, Generations.data(), 1);
	EXPECT_EQ(Generations, (std::array<uint64_t, 2>{1, 0}));
	ASSERT_EQ(m_Jobs.size(), 1u);
	ASSERT_NE(m_Jobs.find(JobB), m_Jobs.end());
	EXPECT_EQ(m_Jobs.find(JobB)->second, pJobB);
	EXPECT_EQ(m_Cache.Find(JobA), nullptr);
	EXPECT_EQ(m_Cache.Find(UploadA), nullptr);
	EXPECT_EQ(m_Cache.Find(TextureA), nullptr);
	ASSERT_NE(m_Cache.Find(JobB), nullptr);
	ASSERT_NE(m_Cache.Find(UploadB), nullptr);
	EXPECT_TRUE(m_Cache.Find(JobB)->m_PreviewJobPending);
	EXPECT_TRUE(m_Cache.Find(UploadB)->m_UploadPending);
	EXPECT_EQ(m_Uploads.QueueDepth(), 1u);
	EXPECT_FALSE(m_Graphics.IsLive(HandleA));
	EXPECT_TRUE(m_Graphics.IsLive(HandleB));
	EXPECT_EQ(m_Graphics.UnloadsOf(HandleA), 1);
	EXPECT_EQ(m_Graphics.UnloadsOf(HandleB), 0);
	EXPECT_TRUE(TryPublishSettingsResourcePreviewJob(*pJobA, JobA, m_Cache, m_Uploads, m_Telemetry, Generations[0]));
	EXPECT_EQ(m_Uploads.QueueDepth(), 1u);
	EXPECT_TRUE(TryPublishSettingsResourcePreviewJob(*pJobB, JobB, m_Cache, m_Uploads, m_Telemetry, Generations[1]));
	EXPECT_EQ(m_Uploads.QueueDepth(), 2u);
}

TEST_F(ResourcePreviewInvalidation, CategoryTypeIsMatchedExactlyRatherThanAsPrefix)
{
	const auto First = InvalidationKey("assets:entities"), Second = InvalidationKey(s_pEntityType);
	ASSERT_NO_FATAL_FAILURE(QueueImage(First));
	ASSERT_NO_FATAL_FAILURE(QueueImage(Second));
	InvalidateSettingsResourcePreviews(m_Cache, m_Uploads, m_Jobs, &m_Graphics, {"assets:entities"});
	EXPECT_EQ(m_Cache.Find(First), nullptr);
	EXPECT_NE(m_Cache.Find(Second), nullptr);
	EXPECT_EQ(m_Uploads.QueueDepth(), 1u);
}

TEST_F(ResourcePreviewInvalidation, IdentityScopePreservesOtherIdsAndOtherCategories)
{
	const auto First = InvalidationKey(s_pEntityType, "same"), OtherId = InvalidationKey(s_pEntityType, "same-extra"), OtherType = InvalidationKey(s_pGameType, "same");
	ASSERT_NO_FATAL_FAILURE(QueueImage(First));
	ASSERT_NO_FATAL_FAILURE(QueueImage(OtherId));
	ASSERT_NO_FATAL_FAILURE(QueueImage(OtherType));
	InvalidateSettingsResourcePreviews(m_Cache, m_Uploads, m_Jobs, &m_Graphics, {s_pEntityType, "same"});
	EXPECT_EQ(m_Cache.Find(First), nullptr);
	EXPECT_NE(m_Cache.Find(OtherId), nullptr);
	EXPECT_NE(m_Cache.Find(OtherType), nullptr);
	EXPECT_EQ(m_Uploads.QueueDepth(), 2u);
}

TEST_F(ResourcePreviewInvalidation, LocalIdentityScopeRemovesAllItsVariantsAndKeepsWorkshopTwin)
{
	const auto Local = InvalidationKey();
	auto LocalVariant = Local;
	LocalVariant.m_CardWidth = 256;
	LocalVariant.m_LocaleHash = 91;
	LocalVariant.m_UiScale = 150;
	LocalVariant.m_ResourceGeneration = 9;
	const auto Workshop = InvalidationKey(s_pEntityType, "same", true);
	const auto OtherId = InvalidationKey(s_pEntityType, "other");
	for(const auto &Key : {Local, LocalVariant, Workshop, OtherId})
		ASSERT_NO_FATAL_FAILURE(QueueImage(Key));
	InvalidateSettingsResourcePreviews(m_Cache, m_Uploads, m_Jobs, &m_Graphics, {s_pEntityType, "same", false});
	EXPECT_EQ(m_Cache.Find(Local), nullptr);
	EXPECT_EQ(m_Cache.Find(LocalVariant), nullptr);
	EXPECT_NE(m_Cache.Find(Workshop), nullptr);
	EXPECT_NE(m_Cache.Find(OtherId), nullptr);
	EXPECT_EQ(m_Uploads.QueueDepth(), 2u);
}

TEST_F(ResourcePreviewInvalidation, WorkshopIdentityScopeKeepsLocalTwin)
{
	const auto Local = InvalidationKey(), Workshop = InvalidationKey(s_pEntityType, "same", true);
	ASSERT_NO_FATAL_FAILURE(QueueImage(Local));
	ASSERT_NO_FATAL_FAILURE(QueueImage(Workshop));
	InvalidateSettingsResourcePreviews(m_Cache, m_Uploads, m_Jobs, &m_Graphics, {s_pEntityType, "same", true});
	EXPECT_NE(m_Cache.Find(Local), nullptr);
	EXPECT_EQ(m_Cache.Find(Workshop), nullptr);
	EXPECT_EQ(m_Uploads.QueueDepth(), 1u);
}

TEST_F(ResourcePreviewInvalidation, IdentityWithoutCategoryMatchesOnlyThatIdAcrossCategories)
{
	const auto First = InvalidationKey(s_pEntityType, "same"), Second = InvalidationKey(s_pGameType, "same"), Other = InvalidationKey(s_pGameType, "other");
	ASSERT_NO_FATAL_FAILURE(QueueImage(First));
	ASSERT_NO_FATAL_FAILURE(QueueImage(Second));
	ASSERT_NO_FATAL_FAILURE(QueueImage(Other));
	InvalidateSettingsResourcePreviews(m_Cache, m_Uploads, m_Jobs, &m_Graphics, {nullptr, "same"});
	EXPECT_EQ(m_Cache.Find(First), nullptr);
	EXPECT_EQ(m_Cache.Find(Second), nullptr);
	EXPECT_NE(m_Cache.Find(Other), nullptr);
	EXPECT_EQ(m_Uploads.QueueDepth(), 1u);
}

TEST_F(ResourcePreviewInvalidation, CategoryScopeIncludesEverySizeLocaleScaleGenerationAndOriginVariant)
{
	for(int Variant = 0; Variant < 32; ++Variant)
	{
		auto Key = InvalidationKey(s_pEntityType, "variants", (Variant & 16) != 0, (Variant & 8) != 0 ? 5 : 0);
		Key.m_CardWidth = (Variant & 1) != 0 ? 256 : 64;
		Key.m_LocaleHash = (Variant & 2) != 0 ? 83 : 0;
		Key.m_UiScale = (Variant & 4) != 0 ? 150 : 100;
		SCOPED_TRACE(Variant);
		ASSERT_NO_FATAL_FAILURE(QueueImage(Key));
	}
	const auto OtherCategory = InvalidationKey(s_pGameType, "variants");
	ASSERT_NO_FATAL_FAILURE(QueueImage(OtherCategory));
	ASSERT_EQ(m_Cache.Size(), 33u);
	ASSERT_EQ(m_Uploads.QueueDepth(), 33u);
	InvalidateSettingsResourcePreviews(m_Cache, m_Uploads, m_Jobs, &m_Graphics, {s_pEntityType});
	EXPECT_EQ(m_Cache.Size(), 1u);
	EXPECT_NE(m_Cache.Find(OtherCategory), nullptr);
	EXPECT_EQ(m_Uploads.QueueDepth(), 1u);
}

TEST_F(ResourcePreviewInvalidation, IdentityAndOriginScopePreservesNonmatchingNativeJobs)
{
	const std::array<SResourcePreviewKey, 4> Keys = {
		InvalidationKey(), InvalidationKey(s_pEntityType, "same", true),
		InvalidationKey(s_pEntityType, "other"), InvalidationKey(s_pGameType, "same")};
	std::array<std::shared_ptr<CSettingsResourcePreviewJob>, 4> Jobs;
	for(size_t Index = 0; Index < Keys.size(); ++Index)
	{
		Jobs[Index] = NewJob();
		ASSERT_NE(Jobs[Index], nullptr);
		ASSERT_NO_FATAL_FAILURE(Execute(Jobs[Index]));
		m_Jobs.emplace(Keys[Index], Jobs[Index]);
		m_Cache.MarkPreviewJobStarted(Keys[Index]);
	}
	InvalidateSettingsResourcePreviews(m_Cache, m_Uploads, m_Jobs, &m_Graphics, {s_pEntityType, "same", false});
	EXPECT_EQ(m_Jobs.count(Keys[0]), 0u);
	EXPECT_EQ(m_Cache.Find(Keys[0]), nullptr);
	ASSERT_EQ(m_Jobs.size(), 3u);
	EXPECT_TRUE(TryPublishSettingsResourcePreviewJob(*Jobs[0], Keys[0], m_Cache, m_Uploads, m_Telemetry));
	EXPECT_EQ(m_Uploads.QueueDepth(), 0u);
	for(size_t Index = 1; Index < Keys.size(); ++Index)
	{
		ASSERT_NE(m_Jobs.find(Keys[Index]), m_Jobs.end());
		EXPECT_EQ(m_Jobs.find(Keys[Index])->second, Jobs[Index]);
		ASSERT_NE(m_Cache.Find(Keys[Index]), nullptr);
		EXPECT_TRUE(m_Cache.Find(Keys[Index])->m_PreviewJobPending);
		EXPECT_TRUE(TryPublishSettingsResourcePreviewJob(*Jobs[Index], Keys[Index], m_Cache, m_Uploads, m_Telemetry));
	}
	EXPECT_EQ(m_Uploads.QueueDepth(), 3u);
	EXPECT_EQ(m_Telemetry.m_PreviewJobsDone, 3);
}

TEST_F(ResourcePreviewInvalidation, RepeatedCategoryInvalidationDoesNotUnloadOrFinalizeTwice)
{
	const auto First = InvalidationKey(s_pEntityType, "texture"), Other = InvalidationKey(s_pGameType, "texture");
	ASSERT_NO_FATAL_FAILURE(CacheTexture(First));
	ASSERT_NO_FATAL_FAILURE(CacheTexture(Other));
	ASSERT_NE(m_Cache.Find(First), nullptr);
	ASSERT_NE(m_Cache.Find(Other), nullptr);
	const auto FirstHandle = m_Cache.Find(First)->m_Texture;
	const auto OtherHandle = m_Cache.Find(Other)->m_Texture;
	CInvalidationInput Input;
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	int Finalizations = 0;
	ASSERT_TRUE(m_Uploads.EnqueueUploadToTarget(InvalidationKey(), std::move(Input.m_Image), [&] { return [&](bool, IGraphics::CTextureHandle) { ++Finalizations; }; }, nullptr));
	for(int Attempt = 0; Attempt < 2; ++Attempt)
		InvalidateSettingsResourcePreviews(m_Cache, m_Uploads, m_Jobs, &m_Graphics, {s_pEntityType});
	EXPECT_EQ(m_Graphics.UnloadsOf(FirstHandle), 1);
	EXPECT_EQ(m_Graphics.UnloadsOf(OtherHandle), 0);
	EXPECT_FALSE(m_Graphics.IsLive(FirstHandle));
	EXPECT_TRUE(m_Graphics.IsLive(OtherHandle));
	EXPECT_EQ(Finalizations, 0);
	EXPECT_EQ(m_Uploads.QueueDepth(), 0u);
}

TEST_F(ResourcePreviewInvalidation, GlobalInvalidationClosesAllJobsPayloadsAndCacheGpuOwners)
{
	for(const char *pType : {s_pEntityType, s_pGameType})
	{
		auto pJob = NewJob();
		ASSERT_NE(pJob, nullptr);
		ASSERT_NO_FATAL_FAILURE(Execute(pJob));
		const auto JobKey = InvalidationKey(pType, "job");
		m_Jobs.emplace(JobKey, pJob);
		m_Cache.MarkPreviewJobStarted(JobKey);
		ASSERT_NO_FATAL_FAILURE(QueueImage(InvalidationKey(pType, "upload")));
		ASSERT_NO_FATAL_FAILURE(CacheTexture(InvalidationKey(pType, "texture")));
	}
	std::array<uint64_t, 3> Generations{2, 7, 10};
	ASSERT_EQ(m_Graphics.LiveCount(), 2u);
	InvalidateSettingsResourcePreviews(m_Cache, m_Uploads, m_Jobs, &m_Graphics, {}, Generations.data(), Generations.size());
	EXPECT_EQ(Generations, (std::array<uint64_t, 3>{3, 8, 11}));
	EXPECT_TRUE(m_Jobs.empty());
	EXPECT_EQ(m_Uploads.QueueDepth(), 0u);
	EXPECT_EQ(m_Cache.Size(), 0u);
	EXPECT_EQ(m_Graphics.LiveCount(), 0u);
	EXPECT_EQ(m_Graphics.m_UnloadCalls, 2);
	InvalidateSettingsResourcePreviews(m_Cache, m_Uploads, m_Jobs, &m_Graphics);
	EXPECT_EQ(m_Graphics.m_UnloadCalls, 2);
}

TEST_F(ResourcePreviewInvalidation, GenerationMismatchClosesUnstartedJobBeforeCachePreparation)
{
	auto pJob = NewJob();
	ASSERT_NE(pJob, nullptr);
	const auto Key = InvalidationKey(s_pEntityType, "stale", false, 4);
	EXPECT_TRUE(TryPublishSettingsResourcePreviewJob(*pJob, Key, m_Cache, m_Uploads, m_Telemetry, 5));
	EXPECT_FALSE(pJob->Finished());
	EXPECT_EQ(m_Cache.Size(), 0u);
	EXPECT_EQ(m_Uploads.QueueDepth(), 0u);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	// 即使持有旧快照的调用方再次传回旧代际，已关闭的发布也不能恢复。
	EXPECT_TRUE(TryPublishSettingsResourcePreviewJob(*pJob, Key, m_Cache, m_Uploads, m_Telemetry, 4));
	EXPECT_EQ(m_Cache.Size(), 0u);
	EXPECT_EQ(m_Uploads.QueueDepth(), 0u);
	EXPECT_EQ(m_Telemetry.m_PreviewJobsDone, 0);
	EXPECT_FALSE(pJob->TakeResult().m_Artifact.m_Success);
}

TEST_F(ResourcePreviewInvalidation, ResourceGenerationIsPartOfCacheIdentity)
{
	const auto Old = InvalidationKey(s_pEntityType, "same", false, 8), Current = InvalidationKey(s_pEntityType, "same", false, 9);
	m_Cache.MarkPreviewJobStarted(Old);
	m_Cache.MarkMetadataReady(Current);
	ASSERT_EQ(m_Cache.Size(), 2u);
	ASSERT_NE(m_Cache.Find(Old), nullptr);
	ASSERT_NE(m_Cache.Find(Current), nullptr);
	EXPECT_TRUE(m_Cache.Find(Old)->m_PreviewJobPending);
	EXPECT_FALSE(m_Cache.Find(Current)->m_PreviewJobPending);
	EXPECT_TRUE(m_Cache.Find(Current)->m_MetadataReady);
}

TEST_F(ResourcePreviewInvalidation, MatchingGenerationPublishesNativeResultAndDoesNotDuplicateIt)
{
	auto pJob = NewJob();
	ASSERT_NE(pJob, nullptr);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	const auto Key = InvalidationKey(s_pEntityType, "current", false, 42);
	m_Cache.MarkPreviewJobStarted(Key);
	ASSERT_TRUE(TryPublishSettingsResourcePreviewJob(*pJob, Key, m_Cache, m_Uploads, m_Telemetry, 42));
	ASSERT_EQ(m_Uploads.QueueDepth(), 1u);
	EXPECT_TRUE(TryPublishSettingsResourcePreviewJob(*pJob, Key, m_Cache, m_Uploads, m_Telemetry, 42));
	EXPECT_EQ(m_Uploads.QueueDepth(), 1u);
	EXPECT_EQ(m_Telemetry.m_PreviewJobsDone, 1);
	SResourcePreviewUploadBudget Budget;
	Budget.m_MaxUploads = 1;
	EXPECT_EQ(m_Uploads.Drain(Budget, m_Telemetry, m_Cache, &m_Graphics), 1);
	ASSERT_NE(m_Cache.Find(Key), nullptr);
	EXPECT_TRUE(m_Cache.Find(Key)->m_TextureReady);
	EXPECT_TRUE(m_Graphics.IsLive(m_Cache.Find(Key)->m_Texture));
}

TEST_F(ResourcePreviewInvalidation, CacheCleanupDoesNotTakeTargetTextureOwnedByCallback)
{
	const auto OwnedKey = InvalidationKey(s_pEntityType, "cache-owner"), TargetKey = InvalidationKey(s_pEntityType, "target-owner");
	ASSERT_NO_FATAL_FAILURE(CacheTexture(OwnedKey));
	ASSERT_NE(m_Cache.Find(OwnedKey), nullptr);
	const auto CacheHandle = m_Cache.Find(OwnedKey)->m_Texture;
	CInvalidationInput Input;
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	IGraphics::CTextureHandle TargetHandle;
	ASSERT_TRUE(m_Uploads.EnqueueUploadToTarget(TargetKey, std::move(Input.m_Image), [&] { return [&](bool Success, IGraphics::CTextureHandle Texture) { EXPECT_TRUE(Success); TargetHandle = Texture; }; }, nullptr));
	SResourcePreviewUploadBudget Budget;
	Budget.m_MaxUploads = 1;
	ASSERT_TRUE(m_Uploads.DrainOne(Budget, m_Telemetry, m_Cache, &m_Graphics));
	ASSERT_TRUE(TargetHandle.IsValid());
	ASSERT_TRUE(m_Graphics.IsLive(TargetHandle));
	ASSERT_NE(m_Cache.Find(TargetKey), nullptr);
	EXPECT_FALSE(m_Cache.Find(TargetKey)->m_Texture.IsValid());
	InvalidateSettingsResourcePreviews(m_Cache, m_Uploads, m_Jobs, &m_Graphics);
	EXPECT_EQ(m_Graphics.UnloadsOf(CacheHandle), 1);
	EXPECT_EQ(m_Graphics.UnloadsOf(TargetHandle), 0);
	EXPECT_FALSE(m_Graphics.IsLive(CacheHandle));
	EXPECT_TRUE(m_Graphics.IsLive(TargetHandle));
	m_Graphics.UnloadTexture(&TargetHandle);
	EXPECT_EQ(m_Graphics.LiveCount(), 0u);
}

TEST_F(ResourcePreviewInvalidation, BorrowedJobsAndUploadAllocatorsCanRejectAllCleanupAllocations)
{
	CRejectableAllocationResource JobsMemory, UploadMemory;
	using TJobEntry = std::pair<const SResourcePreviewKey, std::shared_ptr<CSettingsResourcePreviewJob>>;
	using TJobs = std::unordered_map<SResourcePreviewKey, std::shared_ptr<CSettingsResourcePreviewJob>, SResourcePreviewKeyHash, std::equal_to<SResourcePreviewKey>, CBorrowedAllocator<TJobEntry>>;
	TJobs Jobs(&JobsMemory);
	CSettingsResourcePreviewUploadScheduler Uploads(&UploadMemory);
	const auto First = InvalidationKey(s_pEntityType, "job"), Other = InvalidationKey(s_pGameType, "job");
	auto pFirst = NewJob(), pOther = NewJob();
	ASSERT_NE(pFirst, nullptr);
	ASSERT_NE(pOther, nullptr);
	ASSERT_NO_FATAL_FAILURE(Execute(pFirst));
	ASSERT_NO_FATAL_FAILURE(Execute(pOther));
	Jobs.emplace(First, pFirst);
	Jobs.emplace(Other, pOther);
	m_Cache.MarkPreviewJobStarted(First);
	m_Cache.MarkPreviewJobStarted(Other);
	int Finalizations = 0;
	for(const char *pType : {s_pEntityType, s_pGameType})
	{
		CInvalidationInput Input;
		ASSERT_NE(Input.m_Image.m_pData, nullptr);
		ASSERT_TRUE(Uploads.EnqueueUploadToTarget(InvalidationKey(pType, "upload"), std::move(Input.m_Image), [&] { return [&](bool, IGraphics::CTextureHandle) { ++Finalizations; }; }, nullptr));
		m_Cache.MarkArtifactReady(InvalidationKey(pType, "upload"));
		ASSERT_NO_FATAL_FAILURE(CacheTexture(InvalidationKey(pType, "texture")));
	}
	JobsMemory.m_Reject = true;
	UploadMemory.m_Reject = true;
	EXPECT_NO_THROW(InvalidateSettingsResourcePreviews(m_Cache, Uploads, Jobs, &m_Graphics, {s_pEntityType}));
	EXPECT_EQ(Jobs.count(First), 0u);
	EXPECT_EQ(Jobs.count(Other), 1u);
	EXPECT_EQ(Uploads.QueueDepth(), 1u);
	EXPECT_EQ(m_Cache.Find(First), nullptr);
	ASSERT_NE(m_Cache.Find(Other), nullptr);
	EXPECT_TRUE(m_Cache.Find(Other)->m_PreviewJobPending);
	EXPECT_EQ(m_Graphics.m_UnloadCalls, 1);
	EXPECT_EQ(Finalizations, 0);
	EXPECT_NO_THROW(InvalidateSettingsResourcePreviews(m_Cache, Uploads, Jobs, &m_Graphics));
	EXPECT_TRUE(Jobs.empty());
	EXPECT_EQ(Uploads.QueueDepth(), 0u);
	EXPECT_EQ(m_Cache.Size(), 0u);
	EXPECT_EQ(m_Graphics.m_UnloadCalls, 2);
	EXPECT_EQ(Finalizations, 0);
}

TEST_F(ResourcePreviewInvalidation, InvalidatedQueueReleasesCapturedOwnerWithoutCallingFinalize)
{
	auto pLifetime = std::make_shared<int>(3);
	std::weak_ptr<int> Lifetime = pLifetime;
	CInvalidationInput Input;
	ASSERT_NE(Input.m_Image.m_pData, nullptr);
	int Calls = 0;
	ASSERT_TRUE(m_Uploads.EnqueueUploadToTarget(InvalidationKey(), std::move(Input.m_Image), [&] { return [Lease = pLifetime, &Calls](bool, IGraphics::CTextureHandle) { Calls += *Lease; }; }, nullptr));
	pLifetime.reset();
	EXPECT_FALSE(Lifetime.expired());
	InvalidateSettingsResourcePreviews(m_Cache, m_Uploads, m_Jobs, &m_Graphics);
	EXPECT_TRUE(Lifetime.expired());
	EXPECT_EQ(Calls, 0);
	EXPECT_EQ(m_Uploads.QueueDepth(), 0u);
}

TEST_F(ResourcePreviewInvalidation, IndependentEntityRenderTextureIsUnloadedWithoutArrayOwner)
{
	CMenus::SCustomEntities Entities;
	const auto Render = m_Graphics.TrackedHandle(501, 3);
	Entities.m_RenderTexture = Render;
	Entities.UnloadTextures(&m_Graphics);
	EXPECT_EQ(m_Graphics.UnloadsOf(Render), 1);
	EXPECT_EQ(m_Graphics.m_UnloadCalls, 1);
	EXPECT_FALSE(m_Graphics.IsLive(Render));
	ExpectEntityTextureFieldsInvalid(Entities);
}

TEST_F(ResourcePreviewInvalidation, IndependentEntityRenderAndArrayTexturesAreBothUnloaded)
{
	CMenus::SCustomEntities Entities;
	const auto Render = m_Graphics.TrackedHandle(501, 3);
	const auto Array = m_Graphics.TrackedHandle(502, 4);
	Entities.m_RenderTexture = Render;
	Entities.m_aImages[0].m_Texture = Array;
	Entities.UnloadTextures(&m_Graphics);
	EXPECT_EQ(m_Graphics.UnloadsOf(Render), 1);
	EXPECT_EQ(m_Graphics.UnloadsOf(Array), 1);
	EXPECT_EQ(m_Graphics.m_UnloadCalls, 2);
	ExpectEntityTextureFieldsInvalid(Entities);
}

TEST_F(ResourcePreviewInvalidation, EntityRenderAliasDoesNotRepeatItsArrayTexturesUnload)
{
	CMenus::SCustomEntities Entities;
	const auto Shared = m_Graphics.TrackedHandle(501, 3);
	Entities.m_RenderTexture = Shared;
	Entities.m_aImages[0].m_Texture = Shared;
	Entities.UnloadTextures(&m_Graphics);
	EXPECT_EQ(m_Graphics.UnloadsOf(Shared), 1);
	EXPECT_EQ(m_Graphics.m_UnloadCalls, 1);
	EXPECT_EQ(m_Graphics.LiveCount(), 0u);
	ExpectEntityTextureFieldsInvalid(Entities);
}

TEST_F(ResourcePreviewInvalidation, EntityTextureIdentityIncludesGenerationWhenIdIsReused)
{
	CMenus::SCustomEntities Entities;
	const auto Array = m_Graphics.TrackedHandle(501, 3);
	const auto Render = m_Graphics.TrackedHandle(501, 4);
	Entities.m_aImages[0].m_Texture = Array;
	Entities.m_RenderTexture = Render;
	Entities.UnloadTextures(&m_Graphics);
	EXPECT_EQ(m_Graphics.UnloadsOf(Array), 1);
	EXPECT_EQ(m_Graphics.UnloadsOf(Render), 1);
	EXPECT_EQ(m_Graphics.m_UnloadCalls, 2);
	ExpectEntityTextureFieldsInvalid(Entities);
}

TEST_F(ResourcePreviewInvalidation, DuplicateEntityArrayAliasesCauseOnlyOneUnloadRequest)
{
	CMenus::SCustomEntities Entities;
	const auto Shared = m_Graphics.TrackedHandle(501, 3);
	for(auto &Image : Entities.m_aImages)
		Image.m_Texture = Shared;
	Entities.UnloadTextures(&m_Graphics);
	EXPECT_EQ(m_Graphics.UnloadsOf(Shared), 1);
	EXPECT_EQ(m_Graphics.m_UnloadCalls, 1);
	EXPECT_EQ(m_Graphics.LiveCount(), 0u);
	ExpectEntityTextureFieldsInvalid(Entities);
}

TEST_F(ResourcePreviewInvalidation, EmptyAndPreviouslyOwnedEntityTexturesCanBeClearedRepeatedly)
{
	for(const bool Owned : {false, true})
	{
		SCOPED_TRACE(Owned);
		CMenus::SCustomEntities Entities;
		if(Owned)
			Entities.m_RenderTexture = m_Graphics.TrackedHandle(501, 3);
		const int Before = m_Graphics.m_UnloadCalls;
		Entities.UnloadTextures(&m_Graphics);
		Entities.UnloadTextures(&m_Graphics);
		EXPECT_EQ(m_Graphics.m_UnloadCalls - Before, Owned ? 1 : 0);
		ExpectEntityTextureFieldsInvalid(Entities);
	}
}
