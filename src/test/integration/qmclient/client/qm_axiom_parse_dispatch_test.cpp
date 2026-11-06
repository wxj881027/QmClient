#include <base/time.h>

#include <game/client/components/qmclient/axiom_scores_parse_job.h>

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace
{
	constexpr char RESPONSE[] = R"({"profile":{"name":"player"},"most_played_gametypes":[{"key":"Gores","seconds_played":10}]})";
	constexpr char SEARCH[] = R"({"code":200,"data":{"results":[]}})";

	class CResponse final : public IQmAxiomHttpRequest
	{
		std::string m_Body;
		bool m_Done = false;

	public:
		bool m_ResultThrows = false;
		mutable int m_ResultCalls = 0;
		bool Done() const override { return m_Done; }
		bool TransportSucceeded() const override { return true; }
		int StatusCode() const override { return 200; }
		void Result(const unsigned char **ppData, size_t *pSize) const override
		{
			++m_ResultCalls;
			if(m_ResultThrows)
				throw std::bad_alloc();
			*ppData = reinterpret_cast<const unsigned char *>(m_Body.data());
			*pSize = m_Body.size();
		}
		void Abort() override {}
		void Complete(const char *pBody)
		{
			m_Body = pBody;
			m_Done = true;
		}
	};

	struct SBlockedRead
	{
		std::mutex m_Mutex;
		std::condition_variable m_Condition;
		bool m_Entered = false;
		bool m_Continue = false;
	};

	class CBlockingResponse final : public IQmAxiomHttpRequest
	{
		std::shared_ptr<SBlockedRead> m_pRead;

	public:
		explicit CBlockingResponse(std::shared_ptr<SBlockedRead> pRead) :
			m_pRead(std::move(pRead)) {}
		bool Done() const override { return true; }
		bool TransportSucceeded() const override { return true; }
		int StatusCode() const override { return 200; }
		void Abort() override {}
		void Result(const unsigned char **ppData, size_t *pSize) const override
		{
			std::unique_lock<std::mutex> Lock(m_pRead->m_Mutex);
			m_pRead->m_Entered = true;
			m_pRead->m_Condition.notify_all();
			if(!m_pRead->m_Condition.wait_for(Lock, std::chrono::seconds(5), [&] { return m_pRead->m_Continue; }))
				throw std::bad_alloc();
			*ppData = reinterpret_cast<const unsigned char *>(RESPONSE);
			*pSize = sizeof(RESPONSE) - 1;
		}
	};

	class CHttp final : public IQmAxiomHttp
	{
	public:
		std::vector<std::shared_ptr<CResponse>> m_vRequests;
		std::shared_ptr<IQmAxiomHttpRequest> Get(const char *, int, int, int64_t) override
		{
			auto pRequest = std::make_shared<CResponse>();
			m_vRequests.push_back(pRequest);
			return pRequest;
		}
	};

	class CScores final : public CQmAxiomScores
	{
		CJobPool m_Pool;
		int64_t m_Now = time_freq();

	protected:
		int64_t CurrentTick() const noexcept override { return m_Now; }
		bool SubmitDdStatsParseJob(const std::shared_ptr<CQmAxiomDdStatsParseJob> &pJob) override
		{
			++m_Submissions;
			m_pLastJob = pJob;
			if(m_SubmitThrows)
				throw std::bad_alloc();
			return m_UsePool ? m_Pool.TryAdd(pJob) : !m_Reject;
		}

	public:
		explicit CScores(IQmAxiomHttp *pHttp) :
			CQmAxiomScores(pHttp) {}
		bool m_Reject = false;
		bool m_SubmitThrows = false;
		bool m_UsePool = false;
		int m_Submissions = 0;
		std::shared_ptr<CQmAxiomDdStatsParseJob> m_pLastJob;
		bool StartPool() { return m_Pool.TryInit(1); }
		void StopPool() { m_Pool.Shutdown(); }
		void AdvanceSeconds(int Seconds) { m_Now += Seconds * time_freq(); }
	};

	class QmAxiomParseDispatch : public ::testing::Test
	{
	protected:
		CHttp m_Http;
		CScores m_Scores{&m_Http};
		void TearDown() override { m_Scores.OnShutdown(); }
		void CompleteLatest(const char *pBody = RESPONSE)
		{
			const size_t Count = m_Http.m_vRequests.size();
			ASSERT_GE(Count, 2u);
			m_Http.m_vRequests[Count - 2]->Complete(SEARCH);
			m_Http.m_vRequests[Count - 1]->Complete(pBody);
		}
	};
}

TEST_F(QmAxiomParseDispatch, ClosedPoolReleasesRequestAndAllowsRetryAfterBackoff)
{
	ASSERT_TRUE(m_Scores.StartPool());
	m_Scores.StopPool();
	m_Scores.m_UsePool = true;
	m_Scores.EnsureQueried("player");
	ASSERT_EQ(m_Http.m_vRequests.size(), 2u);
	ASSERT_NO_FATAL_FAILURE(CompleteLatest());
	m_Scores.OnUpdate();
	ASSERT_NE(m_Scores.m_pLastJob, nullptr);
	EXPECT_EQ(m_Scores.m_pLastJob->State(), IJob::STATE_QUEUED);
	EXPECT_EQ(m_Scores.GetDdStatsGameTypes("player"), nullptr);
	m_Scores.OnUpdate();
	m_Scores.EnsureQueried("player");
	EXPECT_EQ(m_Scores.m_Submissions, 1);
	EXPECT_EQ(m_Http.m_vRequests.size(), 2u);
	m_Scores.AdvanceSeconds(30);
	m_Scores.m_UsePool = false;
	m_Scores.EnsureQueried("player");
	ASSERT_EQ(m_Http.m_vRequests.size(), 4u);
	ASSERT_NO_FATAL_FAILURE(CompleteLatest());
	m_Scores.OnUpdate();
	ASSERT_EQ(m_Scores.m_Submissions, 2);
	m_Scores.m_pLastJob->Parse();
	m_Scores.OnUpdate();
	const auto *pTypes = m_Scores.GetDdStatsGameTypes("player");
	ASSERT_NE(pTypes, nullptr);
	ASSERT_EQ(pTypes->size(), 1u);
	EXPECT_EQ((*pTypes)[0].m_PlayTimeSeconds, 10);
}

TEST_F(QmAxiomParseDispatch, SubmissionExceptionDiscardsLateResultWithoutReplayingResponse)
{
	m_Scores.m_SubmitThrows = true;
	m_Scores.EnsureQueried("player");
	ASSERT_NO_FATAL_FAILURE(CompleteLatest());
	EXPECT_NO_THROW(m_Scores.OnUpdate());
	ASSERT_NE(m_Scores.m_pLastJob, nullptr);
	const auto pDiscardedJob = m_Scores.m_pLastJob;
	m_Scores.OnUpdate();
	EXPECT_EQ(m_Scores.m_Submissions, 1);
	pDiscardedJob->Parse();
	EXPECT_FALSE(pDiscardedJob->Ready());
	m_Scores.OnUpdate();
	EXPECT_EQ(m_Scores.GetDdStatsGameTypes("player"), nullptr);
	EXPECT_FALSE(m_Scores.PersistentCacheDirty());
	m_Scores.m_SubmitThrows = false;
	m_Scores.Refresh("player");
	ASSERT_EQ(m_Http.m_vRequests.size(), 4u);
}

TEST_F(QmAxiomParseDispatch, FailedWorkerReleasesRequestWithoutReadingPartialResults)
{
	ASSERT_TRUE(m_Scores.StartPool());
	m_Scores.m_UsePool = true;
	m_Scores.EnsureQueried("player");
	ASSERT_NO_FATAL_FAILURE(CompleteLatest());
	m_Http.m_vRequests.back()->m_ResultThrows = true;
	m_Scores.OnUpdate();
	ASSERT_NE(m_Scores.m_pLastJob, nullptr);
	const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while(!m_Scores.m_pLastJob->Done() && std::chrono::steady_clock::now() < Deadline)
		std::this_thread::yield();
	ASSERT_EQ(m_Scores.m_pLastJob->State(), IJob::STATE_ABORTED);
	EXPECT_FALSE(m_Scores.m_pLastJob->Ready());
	m_Scores.OnUpdate();
	EXPECT_EQ(m_Scores.GetDdStatsGameTypes("player"), nullptr);
	EXPECT_FALSE(m_Scores.PersistentCacheDirty());
	m_Scores.EnsureQueried("another");
	EXPECT_EQ(m_Http.m_vRequests.size(), 4u);
}

TEST_F(QmAxiomParseDispatch, RejectedRefreshPreservesVisibleDataAndLaterRefreshPublishesOnce)
{
	m_Scores.EnsureQueried("player");
	ASSERT_NO_FATAL_FAILURE(CompleteLatest());
	m_Scores.OnUpdate();
	ASSERT_NE(m_Scores.m_pLastJob, nullptr);
	m_Scores.m_pLastJob->Parse();
	m_Scores.OnUpdate();
	m_Scores.ClearPersistentCacheDirty();
	m_Scores.m_Reject = true;
	m_Scores.Refresh("player");
	ASSERT_NO_FATAL_FAILURE(CompleteLatest());
	m_Scores.OnUpdate();
	const auto *pTypes = m_Scores.GetDdStatsGameTypes("player");
	ASSERT_NE(pTypes, nullptr);
	ASSERT_EQ(pTypes->size(), 1u);
	EXPECT_EQ((*pTypes)[0].m_PlayTimeSeconds, 10);
	EXPECT_FALSE(m_Scores.PersistentCacheDirty());
	m_Scores.m_Reject = false;
	m_Scores.Refresh("player");
	ASSERT_NO_FATAL_FAILURE(CompleteLatest(R"({"profile":{"name":"player"},"most_played_gametypes":[{"key":"Gores","seconds_played":20}]})"));
	m_Scores.OnUpdate();
	ASSERT_EQ(m_Scores.m_Submissions, 3);
	m_Scores.m_pLastJob->Parse();
	m_Scores.OnUpdate();
	pTypes = m_Scores.GetDdStatsGameTypes("player");
	ASSERT_NE(pTypes, nullptr);
	EXPECT_EQ((*pTypes)[0].m_PlayTimeSeconds, 20);
	EXPECT_TRUE(m_Scores.PersistentCacheDirty());
	m_Scores.ClearPersistentCacheDirty();
	m_Scores.OnUpdate();
	EXPECT_FALSE(m_Scores.PersistentCacheDirty());
}

TEST(QmAxiomParseInputLifetime, SuccessfulParseReleasesResponseBeforeJobResultIsConsumed)
{
	auto pResponse = std::make_shared<CResponse>();
	pResponse->Complete(RESPONSE);
	const std::weak_ptr<CResponse> Weak = pResponse;
	CQmAxiomDdStatsParseJob Job(pResponse, "player");
	pResponse.reset();
	ASSERT_FALSE(Weak.expired());
	Job.Parse();
	EXPECT_TRUE(Weak.expired());
	ASSERT_TRUE(Job.Ready());
	ASSERT_EQ(Job.Result(), EQmAxiomParseResult::SUCCESS);
	const auto Types = Job.TakeGameTypes();
	ASSERT_EQ(Types.size(), 1u);
	EXPECT_EQ(Types[0].m_PlayTimeSeconds, 10);
}

TEST(QmAxiomParseInputLifetime, RepeatedParseDoesNotReadResponseOrOverwritePublishedResult)
{
	auto pResponse = std::make_shared<CResponse>();
	pResponse->Complete(RESPONSE);
	CQmAxiomDdStatsParseJob Job(pResponse, "player");
	Job.Parse();
	ASSERT_TRUE(Job.Ready());
	ASSERT_EQ(Job.Result(), EQmAxiomParseResult::SUCCESS);
	Job.TakeGameTypes();
	pResponse->m_ResultThrows = true;
	EXPECT_NO_THROW(Job.Parse());
	EXPECT_EQ(pResponse->m_ResultCalls, 1);
	EXPECT_EQ(Job.Result(), EQmAxiomParseResult::SUCCESS);
}

TEST(QmAxiomParseInputLifetime, ResponseExceptionReleasesInputAndCannotReplayIt)
{
	auto pResponse = std::make_shared<CResponse>();
	pResponse->Complete(RESPONSE);
	pResponse->m_ResultThrows = true;
	const std::weak_ptr<CResponse> Weak = pResponse;
	CQmAxiomDdStatsParseJob Job(pResponse, "player");
	pResponse.reset();
	EXPECT_THROW(Job.Parse(), std::bad_alloc);
	EXPECT_TRUE(Weak.expired());
	EXPECT_FALSE(Job.Ready());
	EXPECT_NO_THROW(Job.Parse());
	EXPECT_FALSE(Job.Ready());
}

TEST(QmAxiomParseInputLifetime, CancelQueuedJobReleasesInputAndSkipsLaterWorkerParse)
{
	auto pResponse = std::make_shared<CResponse>();
	pResponse->Complete(RESPONSE);
	const std::weak_ptr<CResponse> Weak = pResponse;
	CQmAxiomDdStatsParseJob Job(pResponse, "player");
	pResponse.reset();
	Job.Cancel();
	EXPECT_TRUE(Weak.expired());
	Job.Cancel();
	Job.Parse();
	EXPECT_FALSE(Job.Ready());
}

TEST(QmAxiomParseInputLifetime, CancelRunningJobKeepsBorrowedInputUntilWorkerFinishes)
{
	auto pRead = std::make_shared<SBlockedRead>();
	auto pResponse = std::make_shared<CBlockingResponse>(pRead);
	const std::weak_ptr<CBlockingResponse> Weak = pResponse;
	auto pJob = std::make_shared<CQmAxiomDdStatsParseJob>(pResponse, "player");
	pResponse.reset();
	CJobPool Pool;
	ASSERT_TRUE(Pool.TryInit(1));
	ASSERT_TRUE(Pool.TryAdd(pJob));
	{
		std::unique_lock<std::mutex> Lock(pRead->m_Mutex);
		EXPECT_TRUE(pRead->m_Condition.wait_for(Lock, std::chrono::seconds(2), [&] { return pRead->m_Entered; }));
	}
	pJob->Cancel();
	EXPECT_FALSE(Weak.expired());
	{
		const std::lock_guard<std::mutex> Lock(pRead->m_Mutex);
		pRead->m_Continue = true;
	}
	pRead->m_Condition.notify_all();
	Pool.Shutdown();
	EXPECT_TRUE(Weak.expired());
	ASSERT_TRUE(pJob->Ready());
	EXPECT_EQ(pJob->Result(), EQmAxiomParseResult::SUCCESS);
}

TEST_F(QmAxiomParseDispatch, ParsedResponseIsReleasedBeforeMainThreadConsumesCompletedJob)
{
	m_Scores.EnsureQueried("player");
	ASSERT_NO_FATAL_FAILURE(CompleteLatest());
	const std::weak_ptr<CResponse> Weak = m_Http.m_vRequests.back();
	m_Scores.OnUpdate();
	ASSERT_NE(m_Scores.m_pLastJob, nullptr);
	m_Http.m_vRequests.clear();
	ASSERT_FALSE(Weak.expired());
	m_Scores.m_pLastJob->Parse();
	EXPECT_TRUE(Weak.expired());
	EXPECT_EQ(m_Scores.GetDdStatsGameTypes("player"), nullptr);
	m_Scores.OnUpdate();
	const auto *pTypes = m_Scores.GetDdStatsGameTypes("player");
	ASSERT_NE(pTypes, nullptr);
	ASSERT_EQ(pTypes->size(), 1u);
	EXPECT_EQ((*pTypes)[0].m_PlayTimeSeconds, 10);
}

TEST_F(QmAxiomParseDispatch, PendingParseKeepsPlayerActiveUntilResultIsConsumed)
{
	m_Scores.EnsureQueried("player");
	ASSERT_NO_FATAL_FAILURE(CompleteLatest());
	m_Scores.OnUpdate();
	ASSERT_NE(m_Scores.m_pLastJob, nullptr);
	m_Scores.EnsureQueried("another");
	EXPECT_EQ(m_Http.m_vRequests.size(), 2u);
	m_Scores.m_pLastJob->Parse();
	m_Scores.OnUpdate();
	m_Scores.EnsureQueried("another");
	EXPECT_EQ(m_Http.m_vRequests.size(), 4u);
}

TEST_F(QmAxiomParseDispatch, RefreshReleasesQueuedInputBeforeObsoleteJobRuns)
{
	m_Scores.EnsureQueried("player");
	ASSERT_NO_FATAL_FAILURE(CompleteLatest());
	const std::weak_ptr<CResponse> Weak = m_Http.m_vRequests.back();
	m_Scores.OnUpdate();
	ASSERT_NE(m_Scores.m_pLastJob, nullptr);
	const auto pOldJob = m_Scores.m_pLastJob;
	m_Http.m_vRequests.clear();
	ASSERT_FALSE(Weak.expired());
	m_Scores.Refresh("player");
	EXPECT_TRUE(Weak.expired());
	pOldJob->Parse();
	EXPECT_FALSE(pOldJob->Ready());
	m_Scores.OnUpdate();
	EXPECT_EQ(m_Scores.GetDdStatsGameTypes("player"), nullptr);
	EXPECT_FALSE(m_Scores.PersistentCacheDirty());
}
