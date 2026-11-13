#include <base/mem.h>
#include <base/thread.h>

#include <engine/engine.h>
#include <engine/http.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/map_covers.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/support/job_wait.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/test.h>

#include <atomic>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace
{
	constexpr const char *CANDIDATE_URL = "https://ddnet.org/ranks/maps/decode_retry_candidate.png";
	constexpr const char *OLD_URL = "https://ddnet.org/ranks/maps/decode_retry_existing.png";

	struct CWorkerObservation
	{
		std::atomic<size_t> m_Attempts{0};
		std::atomic<bool> m_Triggered{false};
	};

	class CWorkerAllocationFailure
	{
		struct CStart
		{
			void (*m_pfnEntry)(void *);
			void *m_pUser;
			size_t m_Budget;
			std::shared_ptr<CWorkerObservation> m_pObservation;
		};
		inline static thread_local CWorkerAllocationFailure *s_pCurrent = nullptr;
		CWorkerAllocationFailure *m_pPrevious;
		const size_t m_Budget;
		std::shared_ptr<CWorkerObservation> m_pObservation;

	public:
		CWorkerAllocationFailure(size_t Budget, std::shared_ptr<CWorkerObservation> pObservation) :
			m_pPrevious(s_pCurrent), m_Budget(Budget), m_pObservation(std::move(pObservation))
		{
			s_pCurrent = this;
		}
		~CWorkerAllocationFailure() { s_pCurrent = m_pPrevious; }
		CWorkerAllocationFailure(const CWorkerAllocationFailure &) = delete;
		static void *Create(void (*pfnEntry)(void *), void *pUser, const char *pName) noexcept
		{
			if(s_pCurrent == nullptr)
				return nullptr;
			auto *pStart = new(std::nothrow) CStart{pfnEntry, pUser, s_pCurrent->m_Budget, s_pCurrent->m_pObservation};
			if(pStart == nullptr)
				return nullptr;
			void *pThread = thread_try_init([](void *pData) {
				std::unique_ptr<CStart> pOwned(static_cast<CStart *>(pData));
				// 真正的 Pool::WorkerThread 和私有 ComposeJob 在本线程的 native-new 故障域内运行。
				CPredictionAllocationFailure Failure(pOwned->m_Budget);
				pOwned->m_pfnEntry(pOwned->m_pUser);
				pOwned->m_pObservation->m_Attempts = Failure.Attempts();
				pOwned->m_pObservation->m_Triggered = Failure.Triggered();
			},
				pStart, pName);
			if(pThread == nullptr)
				delete pStart;
			return pThread;
		}
	};

	class CResponse final : public IHttpRequest
	{
	public:
		explicit CResponse(const char *pUrl) :
			IHttpRequest(pUrl) {}
		void Header(const char *) override {}
		void Abort() override { m_State = EHttpState::ABORTED; }
	};

	std::unique_ptr<IHttpRequest> CreateResponse(const char *pUrl)
	{
		return std::make_unique<CResponse>(pUrl);
	}

	class CNoNetworkHttp final : public IHttp
	{
	public:
		int m_Requests = 0;
		std::shared_ptr<IHttpRequest> m_pRequest;
		void Run(std::shared_ptr<IHttpRequest> pRequest) override
		{
			++m_Requests;
			m_pRequest = std::move(pRequest);
		}
		bool HasIpresolveBug() const override { return false; }
	};

	class CDeferredEngine final : public IEngine
	{
	public:
		std::shared_ptr<IJob> m_pPending;
		int m_Accepted = 0;
		void Init() override {}
		void AddJob(std::shared_ptr<IJob> pJob) override { (void)TryAddJob(std::move(pJob)); }
		bool TryAddJob(std::shared_ptr<IJob> pJob) override
		{
			if(m_pPending != nullptr)
				return false;
			m_pPending = std::move(pJob);
			++m_Accepted;
			return true;
		}
		void ShutdownJobs() override {}
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
		std::shared_ptr<CWorkerObservation> Complete(size_t Budget = std::numeric_limits<size_t>::max())
		{
			auto pObservation = std::make_shared<CWorkerObservation>();
			CWorkerAllocationFailure Failure(Budget, pObservation);
			CJobPool Pool;
			const bool Started = Pool.TryInit(1, CWorkerAllocationFailure::Create);
			EXPECT_TRUE(Started);
			if(!Started || m_pPending == nullptr)
				return pObservation;
			EXPECT_TRUE(Pool.TryAdd(m_pPending));
			EXPECT_TRUE(WaitForJobFinished(*m_pPending));
			EXPECT_EQ(m_pPending->State(), IJob::STATE_DONE);
			Pool.Shutdown();
			m_pPending.reset();
			return pObservation;
		}
	};

	class CGraphicsRecorder final : public CIconBenchmarkGraphics
	{
	public:
		std::vector<uint8_t> m_vLastPixels;
		bool TryLoadTextureRawMoveOwned(CImageInfo &Image, int Flags, const char *pName, SGraphicsUploadRelease &Release, CTextureHandle &Texture) override
		{
			m_vLastPixels.assign(Image.m_pData, Image.m_pData + Image.DataSize());
			return CIconBenchmarkGraphics::TryLoadTextureRawMoveOwned(Image, Flags, pName, Release, Texture);
		}
		void UnloadTexture(CTextureHandle *pHandle) override { pHandle->Invalidate(); }
	};

	class MapCoversDecodeRetry : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CByteBufferWriter m_Png;
		CNoNetworkHttp m_Http;
		CDeferredEngine m_Engine;
		CGraphicsRecorder m_Graphics;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE);
			m_pStorage->CreateFolder(QmMapCover::CACHE_FOLDER, IStorage::TYPE_SAVE);
			uint8_t aPixel[] = {80, 120, 160, 255};
			CImageInfo Image;
			Image.m_Width = Image.m_Height = 1;
			Image.m_Format = CImageInfo::FORMAT_RGBA;
			Image.m_pData = aPixel;
			ASSERT_TRUE(CImageLoader::SavePng(m_Png, Image));
		}
		std::string CachePath(const char *pUrl)
		{
			char aPath[IO_MAX_PATH_LENGTH];
			if(!QmMapCover::ThumbnailCacheFileName(pUrl, aPath, sizeof(aPath)))
				return {};
			return aPath;
		}
		void WriteCache(const char *pUrl, const CByteBufferWriter &Png)
		{
			const IOHANDLE File = m_pStorage->OpenFile(CachePath(pUrl).c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Png.Data(), Png.Size()), Png.Size());
			EXPECT_EQ(io_close(File), 0);
		}
		void Update(CQmMapCovers &Covers, float Now = 1.0f)
		{
			Covers.Update(&m_Http, &m_Engine, &m_Graphics, m_pStorage.get(), Now);
		}
		void Queue(CQmMapCovers &Covers, const char *pUrl)
		{
			CQmMapCovers::SImage Image;
			ASSERT_FALSE(Covers.Get(pUrl, CQmMapCovers::ERole::ROW, &Image));
			Update(Covers);
			ASSERT_NE(m_Engine.m_pPending, nullptr);
		}
		void MakeReady(CQmMapCovers &Covers, const char *pUrl)
		{
			ASSERT_NO_FATAL_FAILURE(Queue(Covers, pUrl));
			m_Engine.Complete();
			Update(Covers);
			CQmMapCovers::SImage Image;
			ASSERT_TRUE(Covers.Get(pUrl, CQmMapCovers::ERole::ROW, &Image));
		}
	};
}

TEST_F(MapCoversDecodeRetry, NativeWorkerRefusalsKeepTheCacheAndOldTextureUntilFreshJobSucceeds)
{
	ASSERT_NO_FATAL_FAILURE(WriteCache(CANDIDATE_URL, m_Png));
	ASSERT_NO_FATAL_FAILURE(WriteCache(OLD_URL, m_Png));
	size_t Allocations;
	std::vector<uint8_t> vExpected;
	{
		CQmMapCovers Covers(CreateResponse);
		ASSERT_NO_FATAL_FAILURE(Queue(Covers, CANDIDATE_URL));
		const auto Observation = m_Engine.Complete();
		Allocations = Observation->m_Attempts.load();
		ASSERT_FALSE(Observation->m_Triggered.load());
		Update(Covers);
		vExpected = m_Graphics.m_vLastPixels;
		ASSERT_FALSE(vExpected.empty());
		Covers.Shutdown(&m_Graphics);
	}
	ASSERT_GT(Allocations, 0u);
	ASSERT_LE(Allocations, 4u);
	for(size_t Budget = 0; Budget < Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		m_Engine.m_Accepted = 0;
		m_Graphics.ResetCounters();
		CQmMapCovers Covers(CreateResponse);
		ASSERT_NO_FATAL_FAILURE(MakeReady(Covers, OLD_URL));
		CQmMapCovers::SImage OldImage;
		ASSERT_TRUE(Covers.Get(OLD_URL, CQmMapCovers::ERole::ROW, &OldImage));
		const int OldId = OldImage.m_Texture.Id();
		ASSERT_NO_FATAL_FAILURE(Queue(Covers, CANDIDATE_URL));
		const auto pFirst = m_Engine.m_pPending;
		const auto Observation = m_Engine.Complete(Budget);
		EXPECT_TRUE(Observation->m_Triggered.load());
		EXPECT_TRUE(pFirst->Finished());
		Update(Covers);
		EXPECT_TRUE(m_pStorage->FileExists(CachePath(CANDIDATE_URL).c_str(), IStorage::TYPE_SAVE));
		EXPECT_TRUE(Covers.Loading());
		ASSERT_TRUE(Covers.Get(OLD_URL, CQmMapCovers::ERole::ROW, &OldImage));
		EXPECT_EQ(OldImage.m_Texture.Id(), OldId);
		EXPECT_EQ(m_Graphics.m_Uploads, 1u);
		ASSERT_NE(m_Engine.m_pPending, nullptr);
		EXPECT_NE(m_Engine.m_pPending, pFirst);
		m_Engine.Complete();
		Update(Covers);
		CQmMapCovers::SImage Image;
		EXPECT_TRUE(Covers.Get(CANDIDATE_URL, CQmMapCovers::ERole::ROW, &Image));
		EXPECT_FALSE(Covers.Loading());
		EXPECT_EQ(m_Graphics.m_vLastPixels, vExpected);
		EXPECT_EQ(m_Graphics.m_Uploads, 2u);
		EXPECT_EQ(m_Engine.m_Accepted, 3);
		EXPECT_EQ(m_Http.m_Requests, 0);
		Covers.Shutdown(&m_Graphics);
	}
}

TEST_F(MapCoversDecodeRetry, DecodedByteLimitStillDeletesTheCacheAndKeepsTheOriginalCooldown)
{
	CImageInfo Source;
	struct SFreeImage
	{
		CImageInfo &m_Image;
		~SFreeImage() { m_Image.Free(); }
	} FreeImage{Source};
	Source.m_Width = 2048;
	Source.m_Height = 1025;
	Source.m_Format = CImageInfo::FORMAT_RGB;
	Source.m_pData = static_cast<uint8_t *>(std::malloc(Source.DataSize()));
	ASSERT_NE(Source.m_pData, nullptr);
	mem_zero(Source.m_pData, Source.DataSize());
	CByteBufferWriter Png;
	ASSERT_TRUE(CImageLoader::SavePng(Png, Source));
	ASSERT_NO_FATAL_FAILURE(WriteCache(CANDIDATE_URL, Png));
	CQmMapCovers Covers(CreateResponse);
	ASSERT_NO_FATAL_FAILURE(Queue(Covers, CANDIDATE_URL));
	m_Engine.Complete();
	Update(Covers);
	EXPECT_FALSE(m_pStorage->FileExists(CachePath(CANDIDATE_URL).c_str(), IStorage::TYPE_SAVE));
	EXPECT_FALSE(Covers.Loading());
	EXPECT_EQ(m_Graphics.m_Uploads, 0u);
	CQmMapCovers::SImage Image;
	Update(Covers, 120.0f);
	EXPECT_FALSE(Covers.Get(CANDIDATE_URL, CQmMapCovers::ERole::ROW, &Image));
	EXPECT_FALSE(Covers.Loading());
	Update(Covers, 122.0f);
	EXPECT_FALSE(Covers.Get(CANDIDATE_URL, CQmMapCovers::ERole::ROW, &Image));
	EXPECT_TRUE(Covers.Loading());
	Update(Covers, 122.0f);
	ASSERT_EQ(m_Http.m_Requests, 1);
	ASSERT_NE(m_Http.m_pRequest, nullptr);
	EXPECT_STREQ(m_Http.m_pRequest->Url(), CANDIDATE_URL);
	Covers.Shutdown(&m_Graphics);
}
