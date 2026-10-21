#include <engine/engine.h>
#include <engine/shared/jobs.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>
#include <test/support/thread_creation_probe.h>

#include <atomic>
#include <memory>

namespace
{
	class CCountJob : public IJob
	{
		std::atomic<int> &m_Count;
		void Run() override { ++m_Count; }

	public:
		explicit CCountJob(std::atomic<int> &Count) :
			m_Count(Count) {}
	};

	struct CStartupResult
	{
		std::shared_ptr<CThreadCreationProbe::CCounts> m_pCounts = std::make_shared<CThreadCreationProbe::CCounts>();
		bool m_First = true;
		bool m_Retry = false;
		std::atomic<int> m_Jobs{0};
		int m_CompletedAtFailure = -1;
	};
}

TEST(JobPoolStartup, FirstThreadFailureLeavesPoolSafeToDestroy)
{
	auto pResult = std::make_shared<CStartupResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		CThreadCreationProbe Probe(pResult->m_pCounts, 1);
		CJobPool Pool;
		pResult->m_First = Pool.TryInit(3, CThreadCreationProbe::Create);
	}));
	EXPECT_FALSE(pResult->m_First);
	EXPECT_EQ(pResult->m_pCounts->m_Attempts.load(), 1);
	EXPECT_EQ(pResult->m_pCounts->m_Completed.load(), 0);
}

TEST(JobPoolStartup, PartialStartJoinsWorkersBeforeReturningFailure)
{
	auto pResult = std::make_shared<CStartupResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		CThreadCreationProbe Probe(pResult->m_pCounts, 3);
		CJobPool Pool;
		pResult->m_First = Pool.TryInit(4, CThreadCreationProbe::Create);
		pResult->m_CompletedAtFailure = pResult->m_pCounts->m_Completed.load();
	}));
	EXPECT_FALSE(pResult->m_First);
	EXPECT_EQ(pResult->m_pCounts->m_Attempts.load(), 3);
	EXPECT_EQ(pResult->m_CompletedAtFailure, 2);
}

TEST(JobPoolStartup, FailedPoolCanRetryAndCompleteJobs)
{
	auto pResult = std::make_shared<CStartupResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		CThreadCreationProbe Probe(pResult->m_pCounts, 2);
		CJobPool Pool;
		pResult->m_First = Pool.TryInit(3, CThreadCreationProbe::Create);
		pResult->m_Retry = Pool.TryInit(2, CThreadCreationProbe::Create);
		if(pResult->m_Retry)
		{
			Pool.Add(std::make_shared<CCountJob>(pResult->m_Jobs));
			Pool.Shutdown();
		}
	}));
	EXPECT_FALSE(pResult->m_First);
	EXPECT_TRUE(pResult->m_Retry);
	EXPECT_EQ(pResult->m_Jobs.load(), 1);
	EXPECT_EQ(pResult->m_pCounts->m_Completed.load(), 3);
}

TEST(JobPoolStartup, InvalidWorkerCountDoesNotCreateThreadsAndAllowsRetry)
{
	auto pResult = std::make_shared<CStartupResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		CThreadCreationProbe Probe(pResult->m_pCounts, 0);
		CJobPool Pool;
		pResult->m_First = Pool.TryInit(0, CThreadCreationProbe::Create) || Pool.TryInit(-1, CThreadCreationProbe::Create);
		pResult->m_Retry = Pool.TryInit(1, CThreadCreationProbe::Create);
	}));
	EXPECT_FALSE(pResult->m_First);
	EXPECT_TRUE(pResult->m_Retry);
	EXPECT_EQ(pResult->m_pCounts->m_Attempts.load(), 1);
	EXPECT_EQ(pResult->m_pCounts->m_Completed.load(), 1);
}

TEST(JobPoolStartup, EngineFactoryDoesNotPublishAnUnstartedPool)
{
	auto pResult = std::make_shared<CStartupResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		CThreadCreationProbe Probe(pResult->m_pCounts, 2);
		std::unique_ptr<IEngine> pEngine(CreateTestEngine("thread-failure", CThreadCreationProbe::Create));
		pResult->m_First = pEngine != nullptr;
		pResult->m_CompletedAtFailure = pResult->m_pCounts->m_Completed.load();
	}));
	EXPECT_FALSE(pResult->m_First);
	EXPECT_EQ(pResult->m_CompletedAtFailure, 1);
}

TEST(JobPoolAcceptance, UninitializedPoolReportsRejection)
{
	CJobPool Pool;
	std::atomic<int> Count{0};
	auto pJob = std::make_shared<CCountJob>(Count);
	EXPECT_FALSE(Pool.TryAdd(pJob));
	EXPECT_EQ(Count.load(), 0);
}

TEST(JobPoolAcceptance, LivePoolReportsAcceptanceAndCompletesBeforeShutdown)
{
	struct CResult
	{
		std::atomic<int> m_Count{0};
		bool m_Started = false;
		bool m_Accepted = false;
	};
	auto pResult = std::make_shared<CResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		CJobPool Pool;
		pResult->m_Started = Pool.TryInit(1);
		if(!pResult->m_Started)
			return;
		pResult->m_Accepted = Pool.TryAdd(std::make_shared<CCountJob>(pResult->m_Count));
		Pool.Shutdown();
	}));
	EXPECT_TRUE(pResult->m_Started);
	EXPECT_TRUE(pResult->m_Accepted);
	EXPECT_EQ(pResult->m_Count.load(), 1);
}

TEST(JobPoolAcceptance, ShutDownPoolReportsRejection)
{
	struct CResult
	{
		std::atomic<int> m_Count{0};
		bool m_Started = false;
		bool m_Accepted = true;
	};
	auto pResult = std::make_shared<CResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		CJobPool Pool;
		pResult->m_Started = Pool.TryInit(1);
		if(!pResult->m_Started)
			return;
		Pool.Shutdown();
		pResult->m_Accepted = Pool.TryAdd(std::make_shared<CCountJob>(pResult->m_Count));
	}));
	EXPECT_TRUE(pResult->m_Started);
	EXPECT_FALSE(pResult->m_Accepted);
	EXPECT_EQ(pResult->m_Count.load(), 0);
}

TEST(JobPoolAcceptance, EngineFacadeReportsItsPoolAcceptanceAndRejection)
{
	struct CResult
	{
		std::atomic<int> m_Count{0};
		bool m_Started = false;
		bool m_Accepted = false;
		bool m_Rejected = false;
	};
	auto pResult = std::make_shared<CResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		std::unique_ptr<IEngine> pEngine(CreateTestEngine("replay-acceptance"));
		if(!pEngine)
			return;
		pResult->m_Started = true;
		pResult->m_Accepted = pEngine->TryAddJob(std::make_shared<CCountJob>(pResult->m_Count));
		pEngine->ShutdownJobs();
		pResult->m_Rejected = !pEngine->TryAddJob(std::make_shared<CCountJob>(pResult->m_Count));
	}));
	EXPECT_TRUE(pResult->m_Started);
	EXPECT_TRUE(pResult->m_Accepted);
	EXPECT_TRUE(pResult->m_Rejected);
	EXPECT_EQ(pResult->m_Count.load(), 1);
}
