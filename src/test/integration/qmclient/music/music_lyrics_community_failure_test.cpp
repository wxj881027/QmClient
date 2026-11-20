#include <base/mem.h>
#include <base/thread.h>

#include <engine/engine.h>
#include <engine/http.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/music_lyrics/music_lyrics_community.h>
#include <game/client/components/qmclient/music_lyrics/music_lyrics_community_format.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/test.h>

#include <atomic>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using namespace QmMusicLyrics;

namespace
{
	struct CWorkerObservation
	{
		std::atomic<size_t> m_Attempts{0};
		std::atomic<bool> m_Triggered{false};
		IJob::EJobState m_State = IJob::STATE_QUEUED;
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
			m_pPrevious(s_pCurrent), m_Budget(Budget), m_pObservation(std::move(pObservation)) { s_pCurrent = this; }
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
				// 真实池入口与私有歌词作业在同一工作线程的故障域内执行。
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
			IHttpRequest(pUrl) { WriteToMemory(); }
		void Header(const char *) override {}
		void Finish(std::string_view Text)
		{
			free(m_pBuffer);
			m_ResponseLength = m_BufferSize = Text.size();
			m_pBuffer = static_cast<unsigned char *>(malloc(Text.size() + 1));
			ASSERT_NE(m_pBuffer, nullptr);
			mem_copy(m_pBuffer, Text.data(), Text.size());
			m_pBuffer[Text.size()] = 0;
			m_StatusCode = 200;
			m_State = EHttpState::DONE;
		}
	};

	std::unique_ptr<IHttpRequest> CreateResponse(const char *pUrl) { return std::make_unique<CResponse>(pUrl); }

	class CHttpRecorder final : public IHttp
	{
	public:
		std::shared_ptr<CResponse> m_pLast;
		int m_Count = 0;
		void Run(std::shared_ptr<IHttpRequest> pRequest) override
		{
			m_pLast = std::static_pointer_cast<CResponse>(std::move(pRequest));
			++m_Count;
		}
		bool HasIpresolveBug() const override { return false; }
		void Reset()
		{
			m_pLast.reset();
			m_Count = 0;
		}
	};

	class CDeferredEngine final : public IEngine
	{
	public:
		std::shared_ptr<IJob> m_pPending;
		void Init() override {}
		void AddJob(std::shared_ptr<IJob> pJob) override
		{
			if(m_pPending != nullptr)
				throw std::logic_error("community lyric test job capacity");
			m_pPending = std::move(pJob);
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
			Pool.Shutdown();
			pObservation->m_State = m_pPending->State();
			m_pPending.reset();
			return pObservation;
		}
	};

	class CommunityLyricsFailure : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		const std::string m_Words = std::string(96, 'w');
		const std::string m_Text = "[1000,2000](1000,2000,0)" + m_Words;
		CHttpRecorder m_Http;
		CDeferredEngine m_Engine;
		SSourceSnapshot m_Source;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
			ASSERT_TRUE(m_pStorage->CreateFolder("qmclient/lyrics", IStorage::TYPE_SAVE));
			ASSERT_TRUE(m_pStorage->CreateFolder("qmclient/lyrics/netease", IStorage::TYPE_SAVE));
			m_Source.m_SongId = 42;
			m_Source.m_Generation = 1;
			m_Source.m_ProducerPid = 100;
			m_Source.m_Active = true;
			m_Source.m_Original = "player line";
			m_Source.m_PositionMs = 1500;
		}
		void TearDown() override
		{
			if(m_Engine.m_pPending != nullptr)
				m_Engine.Complete();
		}
		void Update(CCommunityLyrics &Lyrics, int64_t NowMs)
		{
			Lyrics.Update(1, m_Source, true, NowMs, m_pStorage.get(), &m_Engine, &m_Http);
		}
		SSourceSnapshot Apply(const CCommunityLyrics &Lyrics) const { return Lyrics.Apply(1, m_Source); }
		std::string CachePath() const { return CommunityCachePath(CommunitySong(1, m_Source.m_SongId, {})); }
		void WriteCache()
		{
			const IOHANDLE File = m_pStorage->OpenFile(CachePath().c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, m_Text.data(), m_Text.size()), m_Text.size());
			EXPECT_EQ(io_close(File), 0);
		}
		void BeginDownload(CCommunityLyrics &Lyrics)
		{
			(void)m_pStorage->RemoveFile(CachePath().c_str(), IStorage::TYPE_SAVE);
			m_Http.Reset();
			Update(Lyrics, 1000);
			ASSERT_NE(m_Engine.m_pPending, nullptr);
			EXPECT_EQ(m_Engine.Complete()->m_State, IJob::STATE_DONE);
			Update(Lyrics, 1500);
			ASSERT_EQ(m_Http.m_Count, 1);
			ASSERT_NE(m_Http.m_pLast, nullptr);
			ASSERT_NO_FATAL_FAILURE(m_Http.m_pLast->Finish(m_Text));
			Update(Lyrics, 1600);
			ASSERT_NE(m_Engine.m_pPending, nullptr);
		}
		void FinishDownload(CCommunityLyrics &Lyrics, int64_t NowMs)
		{
			ASSERT_NE(m_Http.m_pLast, nullptr);
			ASSERT_NO_FATAL_FAILURE(m_Http.m_pLast->Finish(m_Text));
			Update(Lyrics, NowMs);
			ASSERT_NE(m_Engine.m_pPending, nullptr);
			EXPECT_EQ(m_Engine.Complete()->m_State, IJob::STATE_DONE);
			Update(Lyrics, NowMs + 1);
			EXPECT_TRUE(Apply(Lyrics).m_HasWordTiming);
			EXPECT_EQ(Apply(Lyrics).m_Original, m_Words);
		}
	};
}

TEST_F(CommunityLyricsFailure, NativeCacheWorkerFailuresKeepTheExistingDelayAndCanRecover)
{
	ASSERT_NO_FATAL_FAILURE(WriteCache());
	for(int Warmup = 0; Warmup < 2; ++Warmup)
	{
		CCommunityLyrics Lyrics(CreateResponse);
		Update(Lyrics, 1000);
		EXPECT_EQ(m_Engine.Complete()->m_State, IJob::STATE_DONE);
		Update(Lyrics, 1001);
		ASSERT_TRUE(Apply(Lyrics).m_HasWordTiming);
	}
	size_t Allocations;
	{
		CCommunityLyrics Lyrics(CreateResponse);
		Update(Lyrics, 1000);
		const auto Observation = m_Engine.Complete();
		ASSERT_EQ(Observation->m_State, IJob::STATE_DONE);
		Allocations = Observation->m_Attempts;
	}
	ASSERT_GT(Allocations, 0u);
	ASSERT_LE(Allocations, 256u);
	size_t FailedJobs = 0;
	for(size_t Budget = 0; Budget < Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		m_Http.Reset();
		CCommunityLyrics Lyrics(CreateResponse);
		Update(Lyrics, 1000);
		ASSERT_NE(m_Engine.m_pPending, nullptr);
		const auto Observation = m_Engine.Complete(Budget);
		ASSERT_TRUE(Observation->m_Triggered);
		FailedJobs += Observation->m_State == IJob::STATE_FAILED;
		Update(Lyrics, 1499);
		EXPECT_EQ(m_Http.m_Count, 0);
		if(Apply(Lyrics).m_HasWordTiming)
		{
			// 底层nothrow分配拒收可能有正常回退，不能强迫每次故障都变成FAILED。
			EXPECT_EQ(Observation->m_State, IJob::STATE_DONE);
			EXPECT_EQ(Apply(Lyrics).m_Original, m_Words);
			continue;
		}
		EXPECT_EQ(Apply(Lyrics).m_Original, m_Source.m_Original);
		Update(Lyrics, 1500);
		ASSERT_EQ(m_Http.m_Count, 1);
		ASSERT_NO_FATAL_FAILURE(FinishDownload(Lyrics, 1600));
	}
	// 此矩阵运行真实文件读取/resize；关闭由生产owner静态证明，不冒充句柄计数。
	EXPECT_GT(FailedJobs, 0u);
}

TEST_F(CommunityLyricsFailure, NativeDownloadedWorkerFailuresRejectPartialDataAndKeepTheRetryDeadline)
{
	for(int Warmup = 0; Warmup < 2; ++Warmup)
	{
		CCommunityLyrics Lyrics(CreateResponse);
		ASSERT_NO_FATAL_FAILURE(BeginDownload(Lyrics));
		EXPECT_EQ(m_Engine.Complete()->m_State, IJob::STATE_DONE);
		Update(Lyrics, 1601);
		ASSERT_TRUE(Apply(Lyrics).m_HasWordTiming);
	}
	size_t Allocations;
	{
		CCommunityLyrics Lyrics(CreateResponse);
		ASSERT_NO_FATAL_FAILURE(BeginDownload(Lyrics));
		const auto Observation = m_Engine.Complete();
		ASSERT_EQ(Observation->m_State, IJob::STATE_DONE);
		Allocations = Observation->m_Attempts;
	}
	ASSERT_GT(Allocations, 0u);
	ASSERT_LE(Allocations, 256u);
	size_t FailedJobs = 0;
	for(size_t Budget = 0; Budget < Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		CCommunityLyrics Lyrics(CreateResponse);
		ASSERT_NO_FATAL_FAILURE(BeginDownload(Lyrics));
		const auto Observation = m_Engine.Complete(Budget);
		ASSERT_TRUE(Observation->m_Triggered);
		Update(Lyrics, 1601);
		if(Observation->m_State == IJob::STATE_DONE)
		{
			EXPECT_TRUE(Apply(Lyrics).m_HasWordTiming);
			EXPECT_EQ(Apply(Lyrics).m_Original, m_Words);
			continue;
		}
		ASSERT_EQ(Observation->m_State, IJob::STATE_FAILED);
		++FailedJobs;
		EXPECT_FALSE(Apply(Lyrics).m_HasWordTiming);
		EXPECT_EQ(Apply(Lyrics).m_Original, m_Source.m_Original);
		Update(Lyrics, 61599);
		EXPECT_EQ(m_Http.m_Count, 1);
		Update(Lyrics, 61600);
		ASSERT_EQ(m_Http.m_Count, 2);
		ASSERT_NO_FATAL_FAILURE(FinishDownload(Lyrics, 61700));
	}
	EXPECT_GT(FailedJobs, 0u);
}
