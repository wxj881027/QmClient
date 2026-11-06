#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/components/qmclient/axiom_scores_parse_job.h>

#include <gtest/gtest.h>

#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	constexpr char SEARCH[] = R"({"code":200,"data":{"results":[{"user_id":7,"player_name":"player"}]}})";
	constexpr char INFO[] = R"({"code":200,"data":{"player":{"player_name":"player","points":42,"total_play_time":1,"total_maps_completed":1,"performance_points":1,"mileage":1},"difficultyData":{}}})";
	constexpr char DDSTATS[] = R"({"profile":{"name":"player"},"most_played_gametypes":[{"key":"Gores","seconds_played":10}]})";

	class CResponse final : public IQmAxiomHttpRequest
	{
		std::string m_Body;
		bool m_Done = false;

	public:
		int m_AbortCalls = 0;
		bool m_AbortThrows = false;
		bool Done() const override { return m_Done; }
		bool TransportSucceeded() const override { return true; }
		int StatusCode() const override { return 200; }
		void Result(const unsigned char **ppData, size_t *pSize) const override
		{
			*ppData = reinterpret_cast<const unsigned char *>(m_Body.data());
			*pSize = m_Body.size();
		}
		void Abort() override
		{
			++m_AbortCalls;
			if(m_AbortThrows)
				throw std::bad_alloc();
		}
		void Complete(const char *pBody)
		{
			m_Body = pBody;
			m_Done = true;
		}
	};

	class CHttp final : public IQmAxiomHttp
	{
	public:
		int m_Calls = 0;
		int m_ThrowOnCall = 0;
		std::vector<std::shared_ptr<CResponse>> m_vRequests;
		std::shared_ptr<IQmAxiomHttpRequest> Get(const char *, int, int, int64_t) override
		{
			if(++m_Calls == m_ThrowOnCall)
				throw std::bad_alloc();
			auto pRequest = std::make_shared<CResponse>();
			m_vRequests.push_back(pRequest);
			return pRequest;
		}
	};

	class CClockScores : public CQmAxiomScores
	{
		int64_t m_Now = time_freq();

	protected:
		int64_t CurrentTick() const noexcept override { return m_Now; }

	public:
		explicit CClockScores(IQmAxiomHttp *pHttp = nullptr) :
			CQmAxiomScores(pHttp) { SetDdStatsEnabled(false); }
		void AdvanceSeconds(int Seconds) { m_Now += Seconds * time_freq(); }
	};

	class CFakeScores final : public CClockScores
	{
		bool SubmitDdStatsParseJob(const std::shared_ptr<CQmAxiomDdStatsParseJob> &pJob) override
		{
			m_pParse = pJob;
			return true;
		}

	public:
		using CClockScores::CClockScores;
		std::shared_ptr<CQmAxiomDdStatsParseJob> m_pParse;
	};

	class QmAxiomRequestLifecycle : public ::testing::Test
	{
		std::string m_OldPlayerName;

	protected:
		CHttp m_Http;
		CFakeScores m_Scores{&m_Http};
		void SetUp() override
		{
			m_OldPlayerName = g_Config.m_PlayerName;
			str_copy(g_Config.m_PlayerName, "local");
		}
		void TearDown() override
		{
			m_Scores.OnShutdown();
			str_copy(g_Config.m_PlayerName, m_OldPlayerName.c_str());
		}
		void StartModes()
		{
			m_Scores.EnsureQueried("player");
			ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
			m_Http.m_vRequests[0]->Complete(SEARCH);
			m_Scores.OnUpdate();
		}
	};

	class CNativeResponse final : public IHttpRequest
	{
	public:
		bool m_HeaderThrows = false;
		bool m_AbortThrows = false;
		int m_AbortCalls = 0;
		CNativeResponse() :
			IHttpRequest("https://axiom.test/player") { LogProgress(HTTPLOG::NONE); }
		size_t Feed(std::string_view Body) { return OnData(Body.data(), Body.size()); }
		void Complete(EHttpState State = EHttpState::DONE) { OnCompletionInternal(State); }
		void Header(const char *) override
		{
			if(m_HeaderThrows)
				throw std::bad_alloc();
		}
		void Abort() override
		{
			++m_AbortCalls;
			if(m_AbortThrows)
				throw std::bad_alloc();
			IHttpRequest::Abort();
		}
	};

	class CNativeScores final : public CClockScores
	{
		std::shared_ptr<IHttpRequest> CreateNativeRequest(const char *) override
		{
			++m_FactoryCalls;
			if(m_FactoryThrows)
				throw std::bad_alloc();
			return m_pNext;
		}
		void SubmitNativeRequest(const std::shared_ptr<IHttpRequest> &pRequest) override
		{
			++m_Submissions;
			m_pAccepted = pRequest;
			if(m_SubmitThrows)
				throw std::bad_alloc();
		}

	public:
		std::shared_ptr<CNativeResponse> m_pNext;
		std::shared_ptr<IHttpRequest> m_pAccepted;
		int m_FactoryCalls = 0;
		int m_Submissions = 0;
		bool m_FactoryThrows = false;
		bool m_SubmitThrows = false;
	};

	class QmAxiomNativeRequestLifecycle : public ::testing::Test
	{
	protected:
		CNativeScores m_Scores;
		void TearDown() override { m_Scores.OnShutdown(); }
	};

	class CNativeBudgetScores final : public CClockScores
	{
		const char *StatisticsPlayerName() const override { return "local"; }
		std::shared_ptr<IHttpRequest> CreateNativeRequest(const char *) override
		{
			return std::make_shared<CNativeResponse>();
		}
		void SubmitNativeRequest(const std::shared_ptr<IHttpRequest> &pRequest) override
		{
			m_vRequests.push_back(std::static_pointer_cast<CNativeResponse>(pRequest));
		}

	public:
		std::vector<std::shared_ptr<CNativeResponse>> m_vRequests;
	};

	class QmAxiomNativeBufferBudget : public ::testing::Test
	{
	protected:
		CNativeBudgetScores m_Scores;
		void TearDown() override { m_Scores.OnShutdown(); }
		void FillBudget()
		{
			m_Scores.SetMode(EQmAxiomMode::GORES);
			const std::string Body(8 * 1024 * 1024, 'x');
			for(int Index = 0; Index < 8; ++Index)
			{
				m_Scores.OnUpdate();
				m_Scores.EnsureQueried(("remote_" + std::to_string(Index)).c_str());
				ASSERT_EQ(m_Scores.m_vRequests.size(), static_cast<size_t>(Index + 1));
				ASSERT_EQ(m_Scores.m_vRequests.back()->Feed(Body), Body.size());
			}
		}
	};
}

TEST_F(QmAxiomRequestLifecycle, FactoryExceptionFailsOnceAndRetriesAfterBackoff)
{
	m_Http.m_ThrowOnCall = 1;
	EXPECT_NO_THROW(m_Scores.EnsureQueried("player"));
	const auto *pResult = m_Scores.GetResult("player");
	ASSERT_NE(pResult, nullptr);
	EXPECT_EQ(pResult->m_SearchStatus, EQmAxiomScoreStatus::HTTP_ERROR);
	m_Scores.EnsureQueried("player");
	EXPECT_EQ(m_Http.m_Calls, 1);
	m_Scores.AdvanceSeconds(30);
	m_Scores.EnsureQueried("player");
	EXPECT_EQ(m_Http.m_Calls, 2);
	EXPECT_EQ(pResult->m_SearchStatus, EQmAxiomScoreStatus::FETCHING);
}

TEST_F(QmAxiomRequestLifecycle, SearchFactoryFailureDoesNotBlockIndependentDdStatsResult)
{
	m_Scores.SetDdStatsEnabled(true);
	m_Http.m_ThrowOnCall = 1;
	EXPECT_NO_THROW(m_Scores.EnsureQueried("player"));
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	m_Http.m_vRequests[0]->Complete(DDSTATS);
	m_Scores.OnUpdate();
	ASSERT_NE(m_Scores.m_pParse, nullptr);
	m_Scores.m_pParse->Parse();
	m_Scores.OnUpdate();
	const auto *pTypes = m_Scores.GetDdStatsGameTypes("player");
	ASSERT_NE(pTypes, nullptr);
	ASSERT_EQ(pTypes->size(), 1u);
	EXPECT_EQ((*pTypes)[0].m_PlayTimeSeconds, 10);
	EXPECT_EQ(m_Scores.GetResult("player")->m_SearchStatus, EQmAxiomScoreStatus::HTTP_ERROR);
}

TEST_F(QmAxiomRequestLifecycle, OneModeFactoryFailureDoesNotBlockOtherModeOrRetry)
{
	m_Http.m_ThrowOnCall = 2;
	ASSERT_NO_FATAL_FAILURE(StartModes());
	ASSERT_EQ(m_Http.m_vRequests.size(), 2u);
	const auto *pResult = m_Scores.GetResult("player");
	ASSERT_NE(pResult, nullptr);
	EXPECT_EQ(pResult->Mode(EQmAxiomMode::GORES).m_Status, EQmAxiomScoreStatus::HTTP_ERROR);
	EXPECT_EQ(pResult->Mode(EQmAxiomMode::AXRACE).m_Status, EQmAxiomScoreStatus::FETCHING);
	m_Http.m_vRequests[1]->Complete(INFO);
	m_Scores.OnUpdate();
	EXPECT_EQ(pResult->Mode(EQmAxiomMode::AXRACE).m_Status, EQmAxiomScoreStatus::READY);
	m_Scores.AdvanceSeconds(30);
	m_Scores.EnsureQueried("player");
	EXPECT_EQ(m_Http.m_Calls, 4);
	EXPECT_EQ(pResult->Mode(EQmAxiomMode::GORES).m_Status, EQmAxiomScoreStatus::FETCHING);
}

TEST_F(QmAxiomRequestLifecycle, ScoreboardFactoryExceptionLeavesNoFetchingSlot)
{
	m_Scores.SetMode(EQmAxiomMode::GORES);
	m_Http.m_ThrowOnCall = 1;
	EXPECT_NO_THROW(m_Scores.EnsureQueried("player"));
	EXPECT_EQ(m_Scores.GetLookup("player").m_Status, EQmAxiomScoreStatus::HTTP_ERROR);
	m_Scores.EnsureQueried("player");
	EXPECT_EQ(m_Http.m_Calls, 1);
	m_Scores.AdvanceSeconds(30);
	m_Scores.EnsureQueried("player");
	EXPECT_EQ(m_Http.m_Calls, 2);
	EXPECT_EQ(m_Scores.GetLookup("player").m_Status, EQmAxiomScoreStatus::FETCHING);
}

TEST_F(QmAxiomRequestLifecycle, ResetContinuesAfterSearchAbortExceptionAndIgnoresLateResults)
{
	m_Scores.SetDdStatsEnabled(true);
	m_Scores.EnsureQueried("player");
	ASSERT_EQ(m_Http.m_vRequests.size(), 2u);
	m_Http.m_vRequests[0]->m_AbortThrows = true;
	EXPECT_NO_THROW(m_Scores.OnReset());
	EXPECT_EQ(m_Http.m_vRequests[0]->m_AbortCalls, 1);
	EXPECT_EQ(m_Http.m_vRequests[1]->m_AbortCalls, 1);
	EXPECT_EQ(m_Scores.GetResult("player")->m_SearchStatus, EQmAxiomScoreStatus::NOT_REQUESTED);
	m_Http.m_vRequests[0]->Complete(SEARCH);
	m_Http.m_vRequests[1]->Complete(DDSTATS);
	m_Scores.OnUpdate();
	EXPECT_EQ(m_Scores.m_pParse, nullptr);
	EXPECT_EQ(m_Scores.GetDdStatsGameTypes("player"), nullptr);
	m_Scores.EnsureQueried("player");
	EXPECT_EQ(m_Http.m_vRequests.size(), 4u);
}

TEST_F(QmAxiomRequestLifecycle, ScoreboardModeFactoryFailureClearsCandidateAndCanRetry)
{
	m_Scores.SetMode(EQmAxiomMode::GORES);
	m_Scores.EnsureQueried("player");
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	m_Http.m_vRequests[0]->Complete(SEARCH);
	m_Scores.OnUpdate();
	m_Http.m_ThrowOnCall = 2;
	EXPECT_NO_THROW(m_Scores.EnsureQueried("player"));
	EXPECT_EQ(m_Scores.GetLookup("player").m_Status, EQmAxiomScoreStatus::HTTP_ERROR);
	m_Scores.EnsureQueried("player");
	EXPECT_EQ(m_Http.m_Calls, 2);
	m_Scores.AdvanceSeconds(30);
	m_Scores.EnsureQueried("player");
	ASSERT_EQ(m_Http.m_vRequests.size(), 2u);
	m_Http.m_vRequests[1]->Complete(INFO);
	m_Scores.OnUpdate();
	EXPECT_EQ(m_Scores.GetLookup("player").m_Status, EQmAxiomScoreStatus::READY);
	EXPECT_EQ(m_Scores.GetLookup("player").m_Points, 42);
}

TEST_F(QmAxiomRequestLifecycle, FailedRefreshKeepsReadyMatchAndScoresDuringBackoff)
{
	ASSERT_NO_FATAL_FAILURE(StartModes());
	ASSERT_EQ(m_Http.m_vRequests.size(), 3u);
	m_Http.m_vRequests[1]->Complete(INFO);
	m_Http.m_vRequests[2]->Complete(INFO);
	m_Scores.OnUpdate();
	m_Scores.ClearPersistentCacheDirty();
	m_Http.m_ThrowOnCall = 4;
	EXPECT_NO_THROW(m_Scores.Refresh("player"));
	const auto *pResult = m_Scores.GetResult("player");
	ASSERT_NE(pResult, nullptr);
	EXPECT_EQ(pResult->m_SearchStatus, EQmAxiomScoreStatus::READY);
	EXPECT_EQ(pResult->Mode(EQmAxiomMode::GORES).m_Status, EQmAxiomScoreStatus::READY);
	EXPECT_EQ(pResult->Mode(EQmAxiomMode::GORES).m_Score.m_Points, 42);
	EXPECT_FALSE(m_Scores.PersistentCacheDirty());
	m_Scores.EnsureQueried("player");
	EXPECT_EQ(m_Http.m_Calls, 4);
	m_Scores.AdvanceSeconds(30);
	m_Scores.EnsureQueried("player");
	EXPECT_EQ(m_Http.m_Calls, 5);
	EXPECT_EQ(pResult->Mode(EQmAxiomMode::GORES).m_Score.m_Points, 42);
}

TEST_F(QmAxiomRequestLifecycle, DisconnectContinuesAfterOneModeAbortThrows)
{
	ASSERT_NO_FATAL_FAILURE(StartModes());
	ASSERT_EQ(m_Http.m_vRequests.size(), 3u);
	m_Http.m_vRequests[1]->m_AbortThrows = true;
	EXPECT_NO_THROW(m_Scores.OnStateChange(IClient::STATE_OFFLINE, IClient::STATE_ONLINE));
	EXPECT_EQ(m_Http.m_vRequests[1]->m_AbortCalls, 1);
	EXPECT_EQ(m_Http.m_vRequests[2]->m_AbortCalls, 1);
	const auto *pResult = m_Scores.GetResult("player");
	ASSERT_NE(pResult, nullptr);
	EXPECT_EQ(pResult->Mode(EQmAxiomMode::GORES).m_Status, EQmAxiomScoreStatus::NOT_REQUESTED);
	EXPECT_EQ(pResult->Mode(EQmAxiomMode::AXRACE).m_Status, EQmAxiomScoreStatus::NOT_REQUESTED);
	m_Scores.OnStateChange(IClient::STATE_ONLINE, IClient::STATE_CONNECTING);
	m_Scores.EnsureQueried("player");
	EXPECT_EQ(m_Http.m_vRequests.size(), 4u);
}

TEST_F(QmAxiomRequestLifecycle, ShutdownDropsAllRequestsAndCacheEvenIfAbortThrows)
{
	m_Scores.SetDdStatsEnabled(true);
	m_Scores.EnsureQueried("player");
	ASSERT_EQ(m_Http.m_vRequests.size(), 2u);
	m_Http.m_vRequests[0]->m_AbortThrows = true;
	EXPECT_NO_THROW(m_Scores.OnShutdown());
	EXPECT_EQ(m_Http.m_vRequests[0]->m_AbortCalls, 1);
	EXPECT_EQ(m_Http.m_vRequests[1]->m_AbortCalls, 1);
	EXPECT_EQ(m_Scores.GetResult("player"), nullptr);
	EXPECT_NO_THROW(m_Scores.OnShutdown());
	EXPECT_EQ(m_Http.m_vRequests[0]->m_AbortCalls, 1);
}

TEST_F(QmAxiomRequestLifecycle, ModeSwitchCancelsEveryPrefetchAfterFirstAbortThrows)
{
	m_Scores.SetMode(EQmAxiomMode::GORES);
	m_Scores.EnsureQueried("first");
	m_Scores.EnsureQueried("second");
	ASSERT_EQ(m_Http.m_vRequests.size(), 2u);
	m_Http.m_vRequests[0]->m_AbortThrows = true;
	EXPECT_NO_THROW(m_Scores.SetMode(EQmAxiomMode::AXRACE));
	EXPECT_EQ(m_Http.m_vRequests[0]->m_AbortCalls, 1);
	EXPECT_EQ(m_Http.m_vRequests[1]->m_AbortCalls, 1);
	EXPECT_EQ(m_Scores.GetLookup("first").m_Status, EQmAxiomScoreStatus::NOT_REQUESTED);
	EXPECT_EQ(m_Scores.GetLookup("second").m_Status, EQmAxiomScoreStatus::NOT_REQUESTED);
	m_Scores.OnUpdate();
	m_Scores.EnsureQueried("first");
	EXPECT_EQ(m_Http.m_vRequests.size(), 3u);
}

TEST_F(QmAxiomNativeRequestLifecycle, FactoryExceptionUsesFailureBackoffAndCanRetry)
{
	m_Scores.m_FactoryThrows = true;
	EXPECT_NO_THROW(m_Scores.EnsureQueried("player"));
	EXPECT_EQ(m_Scores.GetResult("player")->m_SearchStatus, EQmAxiomScoreStatus::HTTP_ERROR);
	EXPECT_EQ(m_Scores.m_Submissions, 0);
	m_Scores.EnsureQueried("player");
	EXPECT_EQ(m_Scores.m_FactoryCalls, 1);
	m_Scores.m_FactoryThrows = false;
	m_Scores.m_pNext = std::make_shared<CNativeResponse>();
	m_Scores.AdvanceSeconds(30);
	m_Scores.EnsureQueried("player");
	EXPECT_EQ(m_Scores.m_Submissions, 1);
}

TEST_F(QmAxiomNativeRequestLifecycle, NullNativeFactoryDoesNotSubmitOrLeaveFetching)
{
	m_Scores.EnsureQueried("player");
	EXPECT_EQ(m_Scores.m_Submissions, 0);
	EXPECT_EQ(m_Scores.GetResult("player")->m_SearchStatus, EQmAxiomScoreStatus::HTTP_ERROR);
}

TEST_F(QmAxiomNativeRequestLifecycle, HeaderPreparationExceptionAbortsWithoutSubmitting)
{
	m_Scores.m_pNext = std::make_shared<CNativeResponse>();
	m_Scores.m_pNext->m_HeaderThrows = true;
	EXPECT_NO_THROW(m_Scores.EnsureQueried("player"));
	EXPECT_EQ(m_Scores.m_Submissions, 0);
	EXPECT_TRUE(m_Scores.m_pNext->IsAbortRequested());
	EXPECT_EQ(m_Scores.GetResult("player")->m_SearchStatus, EQmAxiomScoreStatus::HTTP_ERROR);
}

TEST_F(QmAxiomNativeRequestLifecycle, AcceptedThenThrowingSubmissionIsAbortedAndNeverReplayed)
{
	m_Scores.m_pNext = std::make_shared<CNativeResponse>();
	m_Scores.m_SubmitThrows = true;
	EXPECT_NO_THROW(m_Scores.EnsureQueried("player"));
	EXPECT_EQ(m_Scores.m_pAccepted, m_Scores.m_pNext);
	EXPECT_TRUE(m_Scores.m_pNext->IsAbortRequested());
	EXPECT_EQ(m_Scores.m_pNext->m_AbortCalls, 1);
	EXPECT_EQ(m_Scores.GetResult("player")->m_SearchStatus, EQmAxiomScoreStatus::HTTP_ERROR);
	m_Scores.OnUpdate();
	m_Scores.EnsureQueried("player");
	EXPECT_EQ(m_Scores.m_Submissions, 1);
	m_Scores.m_SubmitThrows = false;
	m_Scores.m_pNext = std::make_shared<CNativeResponse>();
	m_Scores.AdvanceSeconds(30);
	m_Scores.EnsureQueried("player");
	EXPECT_EQ(m_Scores.m_Submissions, 2);
}

TEST_F(QmAxiomNativeRequestLifecycle, AbortExceptionDuringSubmissionRecoveryStillFinishesFailure)
{
	m_Scores.m_pNext = std::make_shared<CNativeResponse>();
	m_Scores.m_pNext->m_AbortThrows = true;
	m_Scores.m_SubmitThrows = true;
	EXPECT_NO_THROW(m_Scores.EnsureQueried("player"));
	EXPECT_EQ(m_Scores.m_pNext->m_AbortCalls, 1);
	EXPECT_EQ(m_Scores.GetResult("player")->m_SearchStatus, EQmAxiomScoreStatus::HTTP_ERROR);
	EXPECT_NO_THROW(m_Scores.OnReset());
	EXPECT_EQ(m_Scores.m_pNext->m_AbortCalls, 1);
}

TEST_F(QmAxiomNativeRequestLifecycle, SuccessfulNativeRequestIsOwnedUntilResetCancellation)
{
	m_Scores.m_pNext = std::make_shared<CNativeResponse>();
	m_Scores.EnsureQueried("player");
	EXPECT_EQ(m_Scores.GetResult("player")->m_SearchStatus, EQmAxiomScoreStatus::FETCHING);
	EXPECT_EQ(m_Scores.m_Submissions, 1);
	EXPECT_FALSE(m_Scores.m_pNext->IsAbortRequested());
	m_Scores.OnReset();
	EXPECT_TRUE(m_Scores.m_pNext->IsAbortRequested());
	EXPECT_EQ(m_Scores.m_pNext->m_AbortCalls, 1);
	EXPECT_EQ(m_Scores.GetResult("player")->m_SearchStatus, EQmAxiomScoreStatus::NOT_REQUESTED);
}

TEST_F(QmAxiomNativeBufferBudget, NativeRequestsShareOneCapacityLimit)
{
	ASSERT_NO_FATAL_FAILURE(FillBudget());
	m_Scores.OnUpdate();
	m_Scores.EnsureQueried("over_budget");
	ASSERT_EQ(m_Scores.m_vRequests.size(), 9u);
	EXPECT_EQ(m_Scores.m_vRequests.back()->Feed("x"), 0u);
	m_Scores.m_vRequests.back()->Complete();
	EXPECT_EQ(m_Scores.m_vRequests.back()->State(), EHttpState::ERROR);
	m_Scores.OnUpdate();
	const auto *pResult = m_Scores.GetResult("over_budget");
	ASSERT_NE(pResult, nullptr);
	EXPECT_EQ(pResult->m_SearchStatus, EQmAxiomScoreStatus::HTTP_ERROR);
}

TEST_F(QmAxiomNativeBufferBudget, ResetRetainsChargesUntilBackendOwnersActuallyReleaseBuffers)
{
	ASSERT_NO_FATAL_FAILURE(FillBudget());
	m_Scores.OnReset();
	EXPECT_TRUE(m_Scores.m_vRequests[0]->IsAbortRequested());
	m_Scores.OnUpdate();
	m_Scores.EnsureQueried("during_reset");
	ASSERT_EQ(m_Scores.m_vRequests.size(), 9u);
	EXPECT_EQ(m_Scores.m_vRequests.back()->Feed("x"), 0u);
	m_Scores.m_vRequests.back()->Complete();
	m_Scores.OnUpdate();
	m_Scores.m_vRequests.clear();
	m_Scores.EnsureQueried("after_release");
	ASSERT_EQ(m_Scores.m_vRequests.size(), 1u);
	EXPECT_EQ(m_Scores.m_vRequests.back()->Feed("x"), 1u);
}
