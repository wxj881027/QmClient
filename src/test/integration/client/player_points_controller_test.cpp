#include <base/time.h>

#include <engine/client.h>

#include <game/client/components/player_points.h>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <new>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
	class CResponse : public IHttpRequest
	{
	public:
		bool m_AbortThrows = false;
		CResponse() :
			IHttpRequest("https://points.test/player") { LogProgress(HTTPLOG::NONE); }
		void Header(const char *) override {}
		void Abort() override
		{
			if(m_AbortThrows)
				throw std::bad_alloc();
			IHttpRequest::Abort();
		}
		void Finish(std::string_view Body, EHttpState State = EHttpState::DONE)
		{
			m_StatusCode = 200;
			ASSERT_EQ(OnData(Body.data(), Body.size()), Body.size());
			OnCompletionInternal(State);
		}
	};

	class CPoints : public CPlayerPoints
	{
		CJobPool m_Pool;

	public:
		std::shared_ptr<CResponse> m_pNext;
		std::vector<std::shared_ptr<IJob>> m_vJobs;
		bool m_FactoryThrows = false;
		bool m_FactoryReturnsNull = false;
		bool m_SubmitThrows = false;
		bool m_JobSubmitThrows = false;
		bool m_StoreThrows = false;
		int m_FactoryCalls = 0;
		int m_SubmitCalls = 0;
		int m_JobSubmitCalls = 0;
		int m_StoreCalls = 0;
		int64_t m_Now = time_freq();
		bool StartJobs() { return m_Pool.TryInit(1); }
		void StopJobs() { m_Pool.Shutdown(); }
		void AdvanceSeconds(int Seconds) { m_Now += time_freq() * Seconds; }

	protected:
		// 只替换HTTP传输、时钟及磁盘边界；解析仍交给真实CJobPool和生产作业。
		std::shared_ptr<IHttpRequest> CreateRequest(const char *) override
		{
			++m_FactoryCalls;
			if(m_FactoryThrows)
				throw std::bad_alloc();
			return m_FactoryReturnsNull ? nullptr : m_pNext;
		}
		void SubmitRequest(const std::shared_ptr<IHttpRequest> &) override
		{
			++m_SubmitCalls;
			if(m_SubmitThrows)
				throw std::bad_alloc();
		}
		bool SubmitParseJob(const std::shared_ptr<IJob> &pJob) override
		{
			++m_JobSubmitCalls;
			if(m_JobSubmitThrows)
				throw std::bad_alloc();
			m_vJobs.push_back(pJob);
			return m_Pool.TryAdd(pJob);
		}
		int64_t Now() const noexcept override { return m_Now; }
		void StoreToDb(const char *, int) override
		{
			++m_StoreCalls;
			if(m_StoreThrows)
				throw std::bad_alloc();
		}
	};

	::testing::AssertionResult WaitForJob(const std::shared_ptr<IJob> &pJob)
	{
		const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while(!pJob->Done() && std::chrono::steady_clock::now() < Deadline)
			std::this_thread::yield();
		return pJob->State() == IJob::STATE_DONE ? ::testing::AssertionSuccess() :
							   ::testing::AssertionFailure() << "parse job did not finish within two seconds";
	}

	class PlayerPointsController : public ::testing::Test
	{
	protected:
		CPoints m_Points;
		void SetUp() override { ASSERT_TRUE(m_Points.StartJobs()); }
		void TearDown() override { m_Points.OnShutdown(); }
		std::shared_ptr<CResponse> Query(const char *pName = "player")
		{
			m_Points.m_pNext = std::make_shared<CResponse>();
			m_Points.EnsureQueried(pName);
			return m_Points.m_pNext;
		}
		void Publish(std::string_view Body)
		{
			auto pRequest = Query();
			ASSERT_NO_FATAL_FAILURE(pRequest->Finish(Body));
			m_Points.OnRender();
			ASSERT_FALSE(m_Points.m_vJobs.empty());
			ASSERT_TRUE(WaitForJob(m_Points.m_vJobs.back()));
			m_Points.OnRender();
		}
	};
}

TEST_F(PlayerPointsController, FactoryExceptionFailsAndAllowsRetryAfterBackoff)
{
	m_Points.m_FactoryThrows = true;
	EXPECT_NO_THROW(Query());
	EXPECT_EQ(m_Points.GetPoints("player").m_Status, EPointsStatus::FAILED);
	m_Points.EnsureQueried("player");
	EXPECT_EQ(m_Points.m_FactoryCalls, 1);
	m_Points.m_FactoryThrows = false;
	m_Points.AdvanceSeconds(30);
	Query();
	EXPECT_EQ(m_Points.GetPoints("player").m_Status, EPointsStatus::FETCHING);
	EXPECT_EQ(m_Points.m_SubmitCalls, 1);
}

TEST_F(PlayerPointsController, NullFactoryResultDoesNotLeaveFetchingSlot)
{
	m_Points.m_FactoryReturnsNull = true;
	Query();
	EXPECT_EQ(m_Points.GetPoints("player").m_Status, EPointsStatus::FAILED);
	EXPECT_EQ(m_Points.m_SubmitCalls, 0);
}

TEST_F(PlayerPointsController, SubmissionExceptionCancelsCandidateAndClearsPendingToken)
{
	m_Points.m_SubmitThrows = true;
	auto pRequest = Query();
	EXPECT_TRUE(pRequest->IsAbortRequested());
	EXPECT_EQ(m_Points.GetPoints("player").m_Status, EPointsStatus::FAILED);
	m_Points.EnsureQueried("player");
	EXPECT_EQ(m_Points.m_SubmitCalls, 1);
}

TEST_F(PlayerPointsController, AbortExceptionDuringSubmissionRecoveryDoesNotEscape)
{
	m_Points.m_SubmitThrows = true;
	m_Points.m_pNext = std::make_shared<CResponse>();
	m_Points.m_pNext->m_AbortThrows = true;
	EXPECT_NO_THROW(m_Points.EnsureQueried("player"));
	EXPECT_EQ(m_Points.GetPoints("player").m_Status, EPointsStatus::FAILED);
}

TEST_F(PlayerPointsController, ClosedPoolRejectsNonAbortableParseWithoutHoldingSlot)
{
	m_Points.StopJobs();
	auto pRequest = Query();
	ASSERT_NO_FATAL_FAILURE(pRequest->Finish(R"({"points":{"points":123}})"));
	m_Points.OnRender();
	ASSERT_EQ(m_Points.m_vJobs.size(), 1u);
	EXPECT_EQ(m_Points.m_vJobs[0]->State(), IJob::STATE_QUEUED);
	EXPECT_EQ(m_Points.GetPoints("player").m_Status, EPointsStatus::FAILED);
	m_Points.OnRender();
	EXPECT_EQ(m_Points.m_JobSubmitCalls, 1);
}

TEST_F(PlayerPointsController, ParseSubmissionExceptionFailsOnceWithoutReplayingResponse)
{
	m_Points.m_JobSubmitThrows = true;
	auto pRequest = Query();
	ASSERT_NO_FATAL_FAILURE(pRequest->Finish(R"({"points":{"points":123}})"));
	EXPECT_NO_THROW(m_Points.OnRender());
	EXPECT_EQ(m_Points.GetPoints("player").m_Status, EPointsStatus::FAILED);
	m_Points.OnRender();
	EXPECT_EQ(m_Points.m_JobSubmitCalls, 1);
}

TEST_F(PlayerPointsController, CompletedProductionParsePublishesAndStoresOnlyOnce)
{
	ASSERT_NO_FATAL_FAILURE(Publish(R"({"points":{"points":123}})"));
	EXPECT_EQ(m_Points.GetPoints("player").m_Status, EPointsStatus::READY);
	EXPECT_EQ(m_Points.GetPoints("player").m_Points, 123);
	m_Points.OnRender();
	m_Points.EnsureQueried("player");
	EXPECT_EQ(m_Points.m_StoreCalls, 1);
	EXPECT_EQ(m_Points.m_JobSubmitCalls, 1);
	EXPECT_EQ(m_Points.m_FactoryCalls, 1);
}

TEST_F(PlayerPointsController, MalformedResponseFailsWithoutReplacingCache)
{
	ASSERT_NO_FATAL_FAILURE(Publish("{"));
	EXPECT_EQ(m_Points.GetPoints("player").m_Status, EPointsStatus::FAILED);
	EXPECT_EQ(m_Points.m_StoreCalls, 0);
}

TEST_F(PlayerPointsController, RejectedRefreshKeepsReadyPointsAndRetriesAfterBackoff)
{
	ASSERT_NO_FATAL_FAILURE(Publish(R"({"points":{"points":42}})"));
	m_Points.AdvanceSeconds(2 * 60 * 60);
	m_Points.StopJobs();
	auto pRequest = Query();
	ASSERT_NO_FATAL_FAILURE(pRequest->Finish(R"({"points":{"points":99}})"));
	m_Points.OnRender();
	EXPECT_EQ(m_Points.GetPoints("player").m_Status, EPointsStatus::READY);
	EXPECT_EQ(m_Points.GetPoints("player").m_Points, 42);
	m_Points.EnsureQueried("player");
	EXPECT_EQ(m_Points.m_FactoryCalls, 2);
	m_Points.AdvanceSeconds(30);
	Query();
	EXPECT_EQ(m_Points.m_FactoryCalls, 3);
}

TEST_F(PlayerPointsController, ResetDiscardsLateParseAndAllowsFreshRequest)
{
	auto pOld = Query();
	ASSERT_NO_FATAL_FAILURE(pOld->Finish(R"({"points":{"points":99}})"));
	m_Points.OnRender();
	ASSERT_EQ(m_Points.m_vJobs.size(), 1u);
	m_Points.OnReset();
	ASSERT_TRUE(WaitForJob(m_Points.m_vJobs[0]));
	m_Points.OnRender();
	EXPECT_EQ(m_Points.GetPoints("player").m_Status, EPointsStatus::NOT_REQUESTED);
	ASSERT_NO_FATAL_FAILURE(Publish(R"({"points":{"points":53}})"));
	EXPECT_EQ(m_Points.GetPoints("player").m_Points, 53);
}

TEST_F(PlayerPointsController, PersistenceExceptionKeepsCommittedPointsWithoutReparsing)
{
	m_Points.m_StoreThrows = true;
	ASSERT_NO_FATAL_FAILURE(Publish(R"({"points":{"points":123}})"));
	EXPECT_EQ(m_Points.GetPoints("player").m_Status, EPointsStatus::READY);
	EXPECT_EQ(m_Points.GetPoints("player").m_Points, 123);
	m_Points.OnRender();
	m_Points.EnsureQueried("player");
	EXPECT_EQ(m_Points.m_StoreCalls, 1);
	EXPECT_EQ(m_Points.m_JobSubmitCalls, 1);
	EXPECT_EQ(m_Points.m_FactoryCalls, 1);
}

TEST_F(PlayerPointsController, FailedRequestReleasesOneOfTwoConcurrentSlots)
{
	auto pFirst = Query("first");
	Query("second");
	Query("third");
	EXPECT_EQ(m_Points.m_FactoryCalls, 2);
	EXPECT_EQ(m_Points.GetPoints("third").m_Status, EPointsStatus::NOT_REQUESTED);
	ASSERT_NO_FATAL_FAILURE(pFirst->Finish("", EHttpState::ERROR));
	m_Points.OnRender();
	Query("third");
	EXPECT_EQ(m_Points.m_FactoryCalls, 3);
	EXPECT_EQ(m_Points.GetPoints("third").m_Status, EPointsStatus::FETCHING);
}

TEST_F(PlayerPointsController, NewServerSessionClearsFailureBackoff)
{
	m_Points.m_FactoryThrows = true;
	Query();
	m_Points.m_FactoryThrows = false;
	m_Points.OnStateChange(IClient::STATE_ONLINE, IClient::STATE_CONNECTING);
	Query();
	EXPECT_EQ(m_Points.GetPoints("player").m_Status, EPointsStatus::FETCHING);
	EXPECT_EQ(m_Points.m_FactoryCalls, 2);
}

TEST_F(PlayerPointsController, ResetInvalidatesAllSlotsWhenOneAbortThrows)
{
	auto pFirst = Query("first");
	auto pSecond = Query("second");
	pFirst->m_AbortThrows = true;
	EXPECT_NO_THROW(m_Points.OnReset());
	EXPECT_TRUE(pSecond->IsAbortRequested());
	EXPECT_EQ(m_Points.GetPoints("first").m_Status, EPointsStatus::NOT_REQUESTED);
	EXPECT_EQ(m_Points.GetPoints("second").m_Status, EPointsStatus::NOT_REQUESTED);
}

TEST_F(PlayerPointsController, EmptyNamesDoNotStartRequests)
{
	m_Points.EnsureQueried(nullptr);
	m_Points.EnsureQueried("");
	EXPECT_EQ(m_Points.GetPoints(nullptr).m_Status, EPointsStatus::NOT_REQUESTED);
	EXPECT_EQ(m_Points.GetPoints("").m_Status, EPointsStatus::NOT_REQUESTED);
	EXPECT_EQ(m_Points.m_FactoryCalls, 0);
}
