#include <base/mem.h>

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

#include <array>
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
	struct SRequestLife
	{
		int m_Id = 0;
		int m_Aborts = 0;
		int m_Destroyed = 0;
	};

	struct SRequestTrace
	{
		std::array<SRequestLife, 512> m_aLives{};
		size_t m_Created = 0;
		SRequestLife *Next()
		{
			if(m_Created >= m_aLives.size())
				throw std::logic_error("community admission request capacity");
			auto &Life = m_aLives[m_Created++];
			Life.m_Id = static_cast<int>(m_Created);
			return &Life;
		}
	};
	thread_local SRequestTrace *s_pTrace = nullptr;

	class CResponse final : public IHttpRequest
	{
		SRequestLife *m_pLife;

	public:
		CResponse(const char *pUrl, SRequestLife *pLife) :
			IHttpRequest(pUrl), m_pLife(pLife) { WriteToMemory(); }
		~CResponse() override { ++m_pLife->m_Destroyed; }
		void Header(const char *) override {}
		int Id() const { return m_pLife->m_Id; }
		const SRequestLife &Life() const { return *m_pLife; }
		void Abort() override
		{
			++m_pLife->m_Aborts;
			IHttpRequest::Abort();
		}
		void Fail() { m_State = EHttpState::ERROR; }
		void Finish(std::string_view Text, int Status = 200)
		{
			free(m_pBuffer);
			m_ResponseLength = m_BufferSize = Text.size();
			m_pBuffer = static_cast<unsigned char *>(malloc(Text.size() + 1));
			ASSERT_NE(m_pBuffer, nullptr);
			if(!Text.empty())
				mem_copy(m_pBuffer, Text.data(), Text.size());
			m_pBuffer[Text.size()] = 0;
			m_StatusCode = Status;
			m_State = EHttpState::DONE;
		}
	};

	std::unique_ptr<IHttpRequest> CreateResponse(const char *pUrl)
	{
		if(s_pTrace == nullptr)
			throw std::logic_error("community admission trace missing");
		return std::make_unique<CResponse>(pUrl, s_pTrace->Next());
	}

	class CHttpRecorder final : public IHttp
	{
	public:
		enum class EMode
		{
			ASYNC,
			SYNC_ERROR,
			BAD_ALLOC,
			LENGTH_ERROR,
			UNKNOWN,
		};
		EMode m_Mode = EMode::ASYNC;
		std::array<std::shared_ptr<CResponse>, 512> m_apRequests{};
		std::array<int, 512> m_aAttemptIds{};
		std::weak_ptr<CResponse> m_LastAttempt;
		size_t m_Count = 0;
		size_t m_Attempts = 0;
		void Run(std::shared_ptr<IHttpRequest> pRequest) override
		{
			if(m_Attempts >= m_aAttemptIds.size() || m_Count >= m_apRequests.size())
				throw std::logic_error("community admission HTTP capacity");
			auto pResponse = std::static_pointer_cast<CResponse>(std::move(pRequest));
			m_LastAttempt = pResponse;
			m_aAttemptIds[m_Attempts++] = pResponse->Id();
			// 抛出模式发生在边界接纳前；不伪造真实curl没有的接纳后分配失败。
			if(m_Mode == EMode::BAD_ALLOC)
				throw std::bad_alloc();
			if(m_Mode == EMode::LENGTH_ERROR)
				throw std::length_error("community admission Run preparation");
			if(m_Mode == EMode::UNKNOWN)
				throw std::logic_error("community admission Run failure");
			if(m_Mode == EMode::SYNC_ERROR)
				pResponse->Fail();
			m_apRequests[m_Count++] = std::move(pResponse);
		}
		bool HasIpresolveBug() const override { return false; }
		std::shared_ptr<CResponse> Last() const { return m_Count == 0 ? nullptr : m_apRequests[m_Count - 1]; }
		void Clear()
		{
			for(auto &pRequest : m_apRequests)
				pRequest.reset();
			m_LastAttempt.reset();
			m_Count = m_Attempts = 0;
			m_Mode = EMode::ASYNC;
		}
	};

	class CDeferredEngine final : public IEngine
	{
		CJobPool m_ClosedPool;

	public:
		bool m_Reject = false;
		std::shared_ptr<IJob> m_pPending;
		std::shared_ptr<IJob> m_pLastRejected;
		int m_Accepted = 0;
		int m_Attempts = 0;
		void Init() override {}
		void AddJob(std::shared_ptr<IJob> pJob) override { (void)TryAddJob(std::move(pJob)); }
		bool TryAddJob(std::shared_ptr<IJob> pJob) override
		{
			++m_Attempts;
			if(m_Reject)
			{
				m_pLastRejected = pJob;
				return m_ClosedPool.TryAdd(std::move(pJob));
			}
			if(m_pPending != nullptr)
				throw std::logic_error("community admission job capacity");
			m_pPending = std::move(pJob);
			++m_Accepted;
			return true;
		}
		void ShutdownJobs() override {}
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
		void Complete()
		{
			ASSERT_NE(m_pPending, nullptr);
			CJobPool Pool;
			ASSERT_TRUE(Pool.TryInit(1));
			ASSERT_TRUE(Pool.TryAdd(m_pPending));
			const auto Finished = WaitForJobFinished(*m_pPending);
			Pool.Shutdown();
			EXPECT_TRUE(Finished);
			EXPECT_EQ(m_pPending->State(), IJob::STATE_DONE);
			m_pPending.reset();
		}
		void Clear()
		{
			m_pLastRejected.reset();
			m_Accepted = m_Attempts = 0;
			m_Reject = false;
		}
	};

	struct SObservation
	{
		size_t m_Allocations;
		bool m_Triggered;
		bool m_Escaped;
	};

	template<typename F>
	SObservation Observe(F &&Action, size_t Budget = std::numeric_limits<size_t>::max())
	{
		CPredictionAllocationFailure Failure(Budget);
		bool Escaped = false;
		try
		{
			Action();
		}
		catch(...)
		{
			Escaped = true;
		}
		return {Failure.Attempts(), Failure.Triggered(), Escaped};
	}

	class CommunityLyricsAdmission : public ::testing::Test
	{
	protected:
		SRequestTrace m_Trace;
		SRequestTrace *m_pPreviousTrace = nullptr;
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CHttpRecorder m_Http;
		CDeferredEngine m_Engine;
		SSourceSnapshot m_Source;
		const std::string m_Words = std::string(96, 'w');
		const std::string m_Text = "[1000,2000](1000,2000,0)" + m_Words;

		void SetUp() override
		{
			m_pPreviousTrace = std::exchange(s_pTrace, &m_Trace);
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			for(const char *pFolder : {"qmclient", "qmclient/lyrics", "qmclient/lyrics/netease", "qmclient/lyrics/qqmusic", "qmclient/lyrics/spotify"})
				ASSERT_TRUE(m_pStorage->CreateFolder(pFolder, IStorage::TYPE_SAVE));
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
			m_Http.Clear();
			s_pTrace = m_pPreviousTrace;
		}
		void ClearBoundaries()
		{
			ASSERT_EQ(m_Engine.m_pPending, nullptr);
			m_Http.Clear();
			m_Engine.Clear();
		}
		void Update(CCommunityLyrics &Lyrics, int SourceId, const SSourceSnapshot &Source, int64_t NowMs, IStorage *pStorage)
		{
			Lyrics.Update(SourceId, Source, true, NowMs, pStorage, &m_Engine, &m_Http);
		}
		void Update(CCommunityLyrics &Lyrics, int64_t NowMs) { Update(Lyrics, 1, m_Source, NowMs, m_pStorage.get()); }
		SSourceSnapshot Apply(const CCommunityLyrics &Lyrics) const { return Lyrics.Apply(1, m_Source); }
		std::string CachePath(int SourceId, const SSourceSnapshot &Source) const
		{
			return CommunityCachePath(CommunitySong(SourceId, Source.m_SongId, Source.m_CatalogId));
		}
		void RemoveCache(int SourceId, const SSourceSnapshot &Source)
		{
			(void)m_pStorage->RemoveFile(CachePath(SourceId, Source).c_str(), IStorage::TYPE_SAVE);
		}
		void WriteCache()
		{
			std::unique_ptr<void, decltype(&io_close)> File(m_pStorage->OpenFile(CachePath(1, m_Source).c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE), io_close);
			ASSERT_NE(File, nullptr);
			const auto Written = io_write(File.get(), m_Text.data(), m_Text.size());
			const int Closed = io_close(File.release());
			EXPECT_EQ(Written, m_Text.size());
			EXPECT_EQ(Closed, 0);
		}
		void ReadyForRequest(CCommunityLyrics &Lyrics)
		{
			RemoveCache(1, m_Source);
			Update(Lyrics, 1000);
			ASSERT_NE(m_Engine.m_pPending, nullptr);
			ASSERT_NO_FATAL_FAILURE(m_Engine.Complete());
			Update(Lyrics, 1499);
			ASSERT_EQ(m_Http.m_Count, 0u);
		}
		void ReadyResponse(CCommunityLyrics &Lyrics)
		{
			ASSERT_NO_FATAL_FAILURE(ReadyForRequest(Lyrics));
			Update(Lyrics, 1500);
			ASSERT_EQ(m_Http.m_Count, 1u);
			ASSERT_NO_FATAL_FAILURE(m_Http.Last()->Finish(m_Text));
		}
		void FinishLast(CCommunityLyrics &Lyrics, int SourceId, const SSourceSnapshot &Source, int64_t NowMs)
		{
			ASSERT_NE(m_Http.Last(), nullptr);
			ASSERT_NO_FATAL_FAILURE(m_Http.Last()->Finish(m_Text));
			Update(Lyrics, SourceId, Source, NowMs, m_pStorage.get());
			ASSERT_NE(m_Engine.m_pPending, nullptr);
			ASSERT_NO_FATAL_FAILURE(m_Engine.Complete());
			Update(Lyrics, SourceId, Source, NowMs + 1, m_pStorage.get());
			EXPECT_TRUE(Lyrics.Apply(SourceId, Source).m_HasWordTiming);
			EXPECT_EQ(Lyrics.Apply(SourceId, Source).m_Original, m_Words);
		}
		void PrepareOld(CCommunityLyrics &Lyrics, bool Loaded)
		{
			if(Loaded)
				ASSERT_NO_FATAL_FAILURE(WriteCache());
			else
				RemoveCache(1, m_Source);
			Update(Lyrics, 1000);
			ASSERT_NE(m_Engine.m_pPending, nullptr);
			ASSERT_NO_FATAL_FAILURE(m_Engine.Complete());
			Update(Lyrics, 1500);
			if(Loaded)
				ASSERT_TRUE(Apply(Lyrics).m_HasWordTiming);
			else
				ASSERT_EQ(m_Http.m_Count, 1u);
		}
		void SeedRejectedCache(CCommunityLyrics &Lyrics)
		{
			m_Engine.m_Reject = true;
			Update(Lyrics, 1000);
			ASSERT_NE(m_Engine.m_pLastRejected, nullptr);
			ASSERT_EQ(m_Engine.m_pPending, nullptr);
			m_Engine.m_Reject = false;
		}
	};
}

TEST_F(CommunityLyricsAdmission, NativeIdentityFailuresPreserveTheOldOwnerAndTheSameSourceCanRetry)
{
	struct STarget
	{
		int m_SourceId;
		const char *m_pCatalog;
		const char *m_pUrl;
	};
	const STarget aTargets[] = {
		{4, "qqmusic:id:18446744073709551615", "https://raw.githubusercontent.com/amll-dev/amll-ttml-db/main/qq-lyrics/18446744073709551615.yrc"},
		{5, "0123456789aBcDeFgHiJkL", "https://raw.githubusercontent.com/amll-dev/amll-ttml-db/main/spotify-lyrics/0123456789aBcDeFgHiJkL.yrc"},
	};
	size_t Faults = 0;
	for(const auto &Target : aTargets)
		for(const bool Loaded : {false, true})
		{
			SCOPED_TRACE(::testing::Message() << Target.m_SourceId << " loaded=" << Loaded);
			auto Source = m_Source;
			Source.m_SongId = 99;
			++Source.m_Generation;
			Source.m_CatalogId = Target.m_pCatalog;
			size_t Allocations;
			{
				ASSERT_NO_FATAL_FAILURE(ClearBoundaries());
				CCommunityLyrics Lyrics(CreateResponse);
				ASSERT_NO_FATAL_FAILURE(PrepareOld(Lyrics, Loaded));
				// 沿现有空存储早退隔离身份准备，后续cache/job阶段另有真实协作矩阵。
				const auto Result = Observe([&] { Update(Lyrics, Target.m_SourceId, Source, 1600, nullptr); });
				ASSERT_FALSE(Result.m_Escaped);
				Allocations = Result.m_Allocations;
			}
			ASSERT_LE(Allocations, 64u);
			for(size_t Budget = 0; Budget < Allocations; ++Budget)
			{
				SCOPED_TRACE(Budget);
				ASSERT_NO_FATAL_FAILURE(ClearBoundaries());
				RemoveCache(Target.m_SourceId, Source);
				CCommunityLyrics Lyrics(CreateResponse);
				ASSERT_NO_FATAL_FAILURE(PrepareOld(Lyrics, Loaded));
				const auto Before = Apply(Lyrics);
				const auto pOldRequest = m_Http.Last();
				const auto Result = Observe([&] { Update(Lyrics, Target.m_SourceId, Source, 1600, nullptr); }, Budget);
				++Faults;
				EXPECT_TRUE(Result.m_Triggered);
				EXPECT_FALSE(Result.m_Escaped);
				if(Loaded)
				{
					EXPECT_EQ(Apply(Lyrics).m_pData, Before.m_pData);
					EXPECT_EQ(Apply(Lyrics).m_Original, m_Words);
				}
				else
				{
					ASSERT_NE(pOldRequest, nullptr);
					EXPECT_EQ(pOldRequest->Life().m_Aborts, 0);
				}
				const size_t Requests = m_Http.m_Count;
				Update(Lyrics, Target.m_SourceId, Source, 1601, m_pStorage.get());
				ASSERT_NE(m_Engine.m_pPending, nullptr);
				ASSERT_NO_FATAL_FAILURE(m_Engine.Complete());
				Update(Lyrics, Target.m_SourceId, Source, 2101, m_pStorage.get());
				ASSERT_EQ(m_Http.m_Count, Requests + 1);
				EXPECT_STREQ(m_Http.Last()->Url(), Target.m_pUrl);
				ASSERT_NO_FATAL_FAILURE(FinishLast(Lyrics, Target.m_SourceId, Source, 2200));
			}
		}
	// 22字符可能落入某些实现的SSO；QQ的完整catalog保证本矩阵有真实堆准备。
	EXPECT_GT(Faults, 0u);
}

TEST_F(CommunityLyricsAdmission, NativeCachePreparationFailuresLeaveTheReadUncommittedAndRetry)
{
	ASSERT_NO_FATAL_FAILURE(WriteCache());
	size_t Allocations;
	{
		CCommunityLyrics Lyrics(CreateResponse);
		ASSERT_NO_FATAL_FAILURE(SeedRejectedCache(Lyrics));
		const auto Result = Observe([&] { Update(Lyrics, 1001); });
		ASSERT_FALSE(Result.m_Escaped);
		Allocations = Result.m_Allocations;
		ASSERT_NO_FATAL_FAILURE(m_Engine.Complete());
	}
	ASSERT_GT(Allocations, 0u);
	ASSERT_LE(Allocations, 64u);
	for(size_t Budget = 0; Budget < Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		ASSERT_NO_FATAL_FAILURE(ClearBoundaries());
		CCommunityLyrics Lyrics(CreateResponse);
		ASSERT_NO_FATAL_FAILURE(SeedRejectedCache(Lyrics));
		const auto Result = Observe([&] { Update(Lyrics, 1001); }, Budget);
		EXPECT_TRUE(Result.m_Triggered);
		EXPECT_FALSE(Result.m_Escaped);
		EXPECT_EQ(m_Engine.m_Accepted, 0);
		EXPECT_EQ(m_Http.m_Count, 0u);
		Update(Lyrics, 1002);
		ASSERT_EQ(m_Engine.m_Accepted, 1);
		ASSERT_NO_FATAL_FAILURE(m_Engine.Complete());
		Update(Lyrics, 1003);
		EXPECT_TRUE(Apply(Lyrics).m_HasWordTiming);
		EXPECT_EQ(Apply(Lyrics).m_Original, m_Words);
	}
}

TEST_F(CommunityLyricsAdmission, ClosedPoolRejectionKeepsCacheAndResponseStagesRetryable)
{
	for(const bool Downloaded : {false, true})
	{
		SCOPED_TRACE(Downloaded);
		ASSERT_NO_FATAL_FAILURE(ClearBoundaries());
		CCommunityLyrics Lyrics(CreateResponse);
		if(Downloaded)
			ASSERT_NO_FATAL_FAILURE(ReadyResponse(Lyrics));
		else
			ASSERT_NO_FATAL_FAILURE(WriteCache());
		m_Engine.m_Reject = true;
		Update(Lyrics, Downloaded ? 1600 : 1000);
		ASSERT_NE(m_Engine.m_pLastRejected, nullptr);
		EXPECT_FALSE(m_Engine.m_pLastRejected->IsAbortable());
		EXPECT_EQ(m_Engine.m_pLastRejected->State(), IJob::STATE_QUEUED);
		EXPECT_FALSE(m_Engine.m_pLastRejected->Finished());
		EXPECT_EQ(m_Engine.m_pPending, nullptr);
		EXPECT_FALSE(Apply(Lyrics).m_HasWordTiming);
		Update(Lyrics, 1700);
		EXPECT_EQ(m_Engine.m_pPending, nullptr);
		EXPECT_EQ(m_Http.m_Count, Downloaded ? 1u : 0u);
		m_Engine.m_Reject = false;
		Update(Lyrics, 1800);
		ASSERT_NE(m_Engine.m_pPending, nullptr);
		ASSERT_NO_FATAL_FAILURE(m_Engine.Complete());
		Update(Lyrics, 1801);
		EXPECT_TRUE(Apply(Lyrics).m_HasWordTiming);
		EXPECT_EQ(Apply(Lyrics).m_Original, m_Words);
		EXPECT_EQ(m_Http.m_Count, Downloaded ? 1u : 0u);
	}
}

TEST_F(CommunityLyricsAdmission, NativeCompletedResponsePreparationFailuresKeepTheSameResult)
{
	size_t Allocations;
	{
		CCommunityLyrics Lyrics(CreateResponse);
		ASSERT_NO_FATAL_FAILURE(ReadyResponse(Lyrics));
		const auto Result = Observe([&] { Update(Lyrics, 1600); });
		ASSERT_FALSE(Result.m_Escaped);
		Allocations = Result.m_Allocations;
		ASSERT_NO_FATAL_FAILURE(m_Engine.Complete());
	}
	ASSERT_GT(Allocations, 0u);
	ASSERT_LE(Allocations, 64u);
	for(size_t Budget = 0; Budget < Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		ASSERT_NO_FATAL_FAILURE(ClearBoundaries());
		CCommunityLyrics Lyrics(CreateResponse);
		ASSERT_NO_FATAL_FAILURE(ReadyResponse(Lyrics));
		const auto pRequest = m_Http.Last();
		const auto Result = Observe([&] { Update(Lyrics, 1600); }, Budget);
		EXPECT_TRUE(Result.m_Triggered);
		EXPECT_FALSE(Result.m_Escaped);
		EXPECT_EQ(pRequest->State(), EHttpState::DONE);
		EXPECT_EQ(pRequest->Life().m_Aborts, 0);
		EXPECT_EQ(m_Engine.m_pPending, nullptr);
		EXPECT_EQ(m_Engine.m_Accepted, 1);
		EXPECT_EQ(m_Http.m_Count, 1u);
		Update(Lyrics, 1601);
		ASSERT_EQ(m_Engine.m_Accepted, 2);
		ASSERT_NO_FATAL_FAILURE(m_Engine.Complete());
		Update(Lyrics, 1602);
		EXPECT_TRUE(Apply(Lyrics).m_HasWordTiming);
		EXPECT_EQ(Apply(Lyrics).m_Original, m_Words);
		EXPECT_EQ(m_Http.m_Count, 1u);
	}
}

TEST_F(CommunityLyricsAdmission, NativeRequestPreparationFailuresDoNotRunUntilTheCandidateIsComplete)
{
	size_t Allocations;
	{
		CCommunityLyrics Lyrics(CreateResponse);
		ASSERT_NO_FATAL_FAILURE(ReadyForRequest(Lyrics));
		const auto Result = Observe([&] { Update(Lyrics, 1500); });
		ASSERT_FALSE(Result.m_Escaped);
		Allocations = Result.m_Allocations;
	}
	ASSERT_GT(Allocations, 0u);
	ASSERT_LE(Allocations, 64u);
	for(size_t Budget = 0; Budget < Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		ASSERT_NO_FATAL_FAILURE(ClearBoundaries());
		CCommunityLyrics Lyrics(CreateResponse);
		ASSERT_NO_FATAL_FAILURE(ReadyForRequest(Lyrics));
		const auto Result = Observe([&] { Update(Lyrics, 1500); }, Budget);
		EXPECT_TRUE(Result.m_Triggered);
		EXPECT_FALSE(Result.m_Escaped);
		EXPECT_EQ(m_Http.m_Attempts, 0u);
		EXPECT_EQ(m_Http.m_Count, 0u);
		Update(Lyrics, 1501);
		ASSERT_EQ(m_Http.m_Attempts, 1u);
		ASSERT_EQ(m_Http.m_Count, 1u);
		ASSERT_NO_FATAL_FAILURE(FinishLast(Lyrics, 1, m_Source, 1600));
	}
}

TEST_F(CommunityLyricsAdmission, SynchronousHttpErrorUsesTheExistingSixtySecondRetry)
{
	CCommunityLyrics Lyrics(CreateResponse);
	ASSERT_NO_FATAL_FAILURE(ReadyForRequest(Lyrics));
	m_Http.m_Mode = CHttpRecorder::EMode::SYNC_ERROR;
	Update(Lyrics, 1500);
	ASSERT_EQ(m_Http.m_Count, 1u);
	EXPECT_EQ(m_Http.Last()->State(), EHttpState::ERROR);
	Update(Lyrics, 1501);
	EXPECT_FALSE(Apply(Lyrics).m_HasWordTiming);
	Update(Lyrics, 61500);
	EXPECT_EQ(m_Http.m_Count, 1u);
	m_Http.m_Mode = CHttpRecorder::EMode::ASYNC;
	Update(Lyrics, 61501);
	ASSERT_EQ(m_Http.m_Count, 2u);
	ASSERT_NO_FATAL_FAILURE(FinishLast(Lyrics, 1, m_Source, 61600));
}

TEST_F(CommunityLyricsAdmission, RunExceptionsAbortTheAttemptAndOnlyANewRequestCanRetry)
{
	using EMode = CHttpRecorder::EMode;
	for(const auto Mode : {EMode::BAD_ALLOC, EMode::LENGTH_ERROR, EMode::UNKNOWN})
	{
		SCOPED_TRACE(static_cast<int>(Mode));
		ASSERT_NO_FATAL_FAILURE(ClearBoundaries());
		CCommunityLyrics Lyrics(CreateResponse);
		ASSERT_NO_FATAL_FAILURE(ReadyForRequest(Lyrics));
		const size_t FirstLife = m_Trace.m_Created;
		m_Http.m_Mode = Mode;
		if(Mode == EMode::UNKNOWN)
			EXPECT_THROW(Update(Lyrics, 1500), std::logic_error);
		else
			EXPECT_NO_THROW(Update(Lyrics, 1500));
		EXPECT_EQ(m_Http.m_Count, 0u);
		EXPECT_EQ(m_Http.m_Attempts, 1u);
		EXPECT_TRUE(m_Http.m_LastAttempt.expired());
		EXPECT_EQ(m_Trace.m_aLives[FirstLife].m_Aborts, 1);
		EXPECT_EQ(m_Trace.m_aLives[FirstLife].m_Destroyed, 1);
		m_Http.m_Mode = EMode::ASYNC;
		Update(Lyrics, 1501);
		ASSERT_EQ(m_Http.m_Count, 1u);
		ASSERT_EQ(m_Http.m_Attempts, 2u);
		EXPECT_NE(m_Http.m_aAttemptIds[0], m_Http.m_aAttemptIds[1]);
		ASSERT_NO_FATAL_FAILURE(FinishLast(Lyrics, 1, m_Source, 1600));
	}
}

TEST_F(CommunityLyricsAdmission, GenerationChangeWaitsForTheOldJobAndRejectsItsDirectPublication)
{
	CCommunityLyrics Lyrics(CreateResponse);
	ASSERT_NO_FATAL_FAILURE(ReadyResponse(Lyrics));
	Update(Lyrics, 1600);
	ASSERT_NE(m_Engine.m_pPending, nullptr);
	const auto pOldJob = m_Engine.m_pPending;
	const int Accepted = m_Engine.m_Accepted;
	auto Source = m_Source;
	++Source.m_Generation;
	Update(Lyrics, 1, Source, 1700, m_pStorage.get());
	Update(Lyrics, 1, Source, 1750, m_pStorage.get());
	EXPECT_EQ(m_Engine.m_Accepted, Accepted);
	EXPECT_EQ(m_Engine.m_pPending, pOldJob);
	EXPECT_FALSE(Lyrics.Apply(1, Source).m_HasWordTiming);
	ASSERT_NO_FATAL_FAILURE(m_Engine.Complete());
	Update(Lyrics, 1, Source, 1800, m_pStorage.get());
	ASSERT_EQ(m_Engine.m_Accepted, Accepted + 1);
	ASSERT_NE(m_Engine.m_pPending, nullptr);
	EXPECT_FALSE(Lyrics.Apply(1, Source).m_HasWordTiming);
	ASSERT_NO_FATAL_FAILURE(m_Engine.Complete());
	Update(Lyrics, 1, Source, 1801, m_pStorage.get());
	EXPECT_TRUE(Lyrics.Apply(1, Source).m_HasWordTiming);
	EXPECT_EQ(Lyrics.Apply(1, Source).m_Original, m_Words);
	EXPECT_FALSE(Apply(Lyrics).m_HasWordTiming);
	EXPECT_EQ(m_Http.m_Count, 1u);
}
