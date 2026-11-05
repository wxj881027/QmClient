#include <base/time.h>

#include <game/client/components/qmclient/axiom_scores_parse_job.h>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
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
		bool Done() const override { return m_Done; }
		bool TransportSucceeded() const override { return true; }
		int StatusCode() const override { return 200; }
		void Result(const unsigned char **ppData, size_t *pSize) const override
		{
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
