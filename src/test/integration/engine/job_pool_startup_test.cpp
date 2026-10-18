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
