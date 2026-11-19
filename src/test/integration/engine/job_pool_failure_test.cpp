#include <engine/client/replay_save.h>
#include <engine/shared/jobs.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace
{
	class CFunctionJob : public IJob
	{
		std::function<void()> m_Function;
		void Run() override { m_Function(); }

	public:
		explicit CFunctionJob(std::function<void()> Function, bool CanAbort = false) :
			m_Function(std::move(Function)) { Abortable(CanAbort); }
	};

	class JobPoolFailure : public ::testing::Test
	{
	protected:
		CJobPool m_Pool;
		bool m_Started = false;
		void SetUp() override
		{
			m_Started = m_Pool.TryInit(1);
			ASSERT_TRUE(m_Started);
		}
		void TearDown() override
		{
			if(m_Started)
				m_Pool.Shutdown();
		}
	};

	struct SJobGate
	{
		std::promise<void> m_Started;
		std::promise<void> m_Release;
		std::shared_future<void> m_Released = m_Release.get_future().share();
		std::atomic<bool> m_TimedOut{false};
	};
}

TEST_F(JobPoolFailure, StandardExceptionPublishesFailedOnlyAfterStackUnwind)
{
	auto pUnwound = std::make_shared<std::atomic<bool>>(false);
	auto pJob = std::make_shared<CFunctionJob>([pUnwound] {
		struct CScope
		{
			std::atomic<bool> &m_Unwound;
			~CScope() { m_Unwound = true; }
		} Scope{*pUnwound};
		throw std::bad_alloc();
	});
	ASSERT_TRUE(m_Pool.TryAdd(pJob));
	ASSERT_TRUE(WaitForJobFinished(*pJob));
	EXPECT_EQ(pJob->State(), IJob::STATE_FAILED);
	EXPECT_TRUE(pJob->Done());
	EXPECT_TRUE(pUnwound->load());
	EXPECT_FALSE(pJob->IsAbortable());
}

TEST_F(JobPoolFailure, UnknownExceptionDoesNotStopNextQueuedJob)
{
	auto pFailed = std::make_shared<CFunctionJob>([] { throw 7; });
	auto pNextRan = std::make_shared<std::atomic<bool>>(false);
	auto pNext = std::make_shared<CFunctionJob>([pNextRan] { *pNextRan = true; });
	ASSERT_TRUE(m_Pool.TryAdd(pFailed));
	ASSERT_TRUE(m_Pool.TryAdd(pNext));
	ASSERT_TRUE(WaitForJobFinished(*pNext));
	EXPECT_EQ(pFailed->State(), IJob::STATE_FAILED);
	EXPECT_EQ(pNext->State(), IJob::STATE_DONE);
	EXPECT_TRUE(pNextRan->load());
}

TEST_F(JobPoolFailure, SuccessRemainsDoneAndConstructorIsNotFinished)
{
	auto pJob = std::make_shared<CFunctionJob>([] {});
	EXPECT_FALSE(pJob->Done());
	EXPECT_FALSE(pJob->Finished());
	ASSERT_TRUE(m_Pool.TryAdd(pJob));
	ASSERT_TRUE(WaitForJobFinished(*pJob));
	EXPECT_EQ(pJob->State(), IJob::STATE_DONE);
}

TEST_F(JobPoolFailure, CancellationDoesNotClaimWorkerExitBeforeRunUnwinds)
{
	std::promise<void> Started;
	std::promise<void> Release;
	auto Released = Release.get_future().share();
	std::atomic<bool> TimedOut{false};
	auto pJob = std::make_shared<CFunctionJob>([&] {
		Started.set_value();
		TimedOut = Released.wait_for(std::chrono::seconds(5)) != std::future_status::ready;
		throw std::runtime_error("cancelled worker unwound");
	},
		true);
	ASSERT_TRUE(m_Pool.TryAdd(pJob));
	const bool Entered = Started.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready;
	EXPECT_TRUE(Entered);
	EXPECT_TRUE(pJob->Abort());
	EXPECT_TRUE(pJob->Done());
	EXPECT_FALSE(pJob->Finished());
	Release.set_value();
	m_Pool.Shutdown();
	m_Started = false;
	EXPECT_FALSE(TimedOut);
	EXPECT_EQ(pJob->State(), IJob::STATE_ABORTED);
	EXPECT_TRUE(pJob->Finished());
}

TEST_F(JobPoolFailure, FailedReplayQueueHeadIsConsumedOnceAndDoesNotBlockNextResult)
{
	auto pFailed = std::make_shared<CFunctionJob>([] { throw std::bad_alloc(); });
	auto pNext = std::make_shared<CFunctionJob>([] {});
	ASSERT_TRUE(m_Pool.TryAdd(pFailed));
	ASSERT_TRUE(m_Pool.TryAdd(pNext));
	ASSERT_TRUE(WaitForJobFinished(*pNext));
	std::deque<std::shared_ptr<CFunctionJob>> Jobs{pFailed, pNext};
	int Failed = 0, Succeeded = 0;
	const auto Notify = [&](const auto &pJob) {
		if(pJob->State() == IJob::STATE_DONE)
			++Succeeded;
		else
			++Failed;
	};
	EXPECT_TRUE(replay_save::ConsumeCompleted(Jobs, Notify));
	EXPECT_TRUE(replay_save::ConsumeCompleted(Jobs, Notify));
	EXPECT_FALSE(replay_save::ConsumeCompleted(Jobs, Notify));
	EXPECT_EQ(Failed, 1);
	EXPECT_EQ(Succeeded, 1);
	EXPECT_TRUE(Jobs.empty());
}

TEST_F(JobPoolFailure, ShutdownDrainsNonAbortableFailureAndPoolCanBeReinitialized)
{
	auto pFailed = std::make_shared<CFunctionJob>([] { throw 7; });
	ASSERT_TRUE(m_Pool.TryAdd(pFailed));
	m_Pool.Shutdown();
	m_Started = false;
	EXPECT_EQ(pFailed->State(), IJob::STATE_FAILED);
	m_Started = m_Pool.TryInit(1);
	ASSERT_TRUE(m_Started);
	auto pNext = std::make_shared<CFunctionJob>([] {});
	ASSERT_TRUE(m_Pool.TryAdd(pNext));
	ASSERT_TRUE(WaitForJobFinished(*pNext));
	EXPECT_EQ(pNext->State(), IJob::STATE_DONE);
}

TEST_F(JobPoolFailure, QueuedCancellationNeverRunsAndFinishesBookkeeping)
{
	auto pGate = std::make_shared<SJobGate>();
	auto pBlocked = std::make_shared<CFunctionJob>([pGate] {
		pGate->m_Started.set_value();
		pGate->m_TimedOut = pGate->m_Released.wait_for(std::chrono::seconds(5)) != std::future_status::ready;
	});
	ASSERT_TRUE(m_Pool.TryAdd(pBlocked));
	const bool Entered = pGate->m_Started.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready;
	EXPECT_TRUE(Entered);
	auto pRan = std::make_shared<std::atomic<bool>>(false);
	auto pCancelled = std::make_shared<CFunctionJob>([pRan] { *pRan = true; }, true);
	ASSERT_TRUE(m_Pool.TryAdd(pCancelled));
	EXPECT_TRUE(pCancelled->Abort());
	EXPECT_FALSE(pCancelled->Finished());
	pGate->m_Release.set_value();
	m_Pool.Shutdown();
	m_Started = false;
	EXPECT_FALSE(pGate->m_TimedOut.load());
	EXPECT_FALSE(pRan->load());
	EXPECT_EQ(pCancelled->State(), IJob::STATE_ABORTED);
	EXPECT_TRUE(pCancelled->Finished());
}

TEST_F(JobPoolFailure, RejectedNonAbortablePreparationCanBeRetriedOnReopenedPool)
{
	m_Pool.Shutdown();
	m_Started = false;
	auto pRuns = std::make_shared<std::atomic<int>>(0);
	auto pJob = std::make_shared<CFunctionJob>([pRuns] { ++*pRuns; });
	EXPECT_FALSE(m_Pool.TryAdd(pJob));
	EXPECT_EQ(pJob->State(), IJob::STATE_QUEUED);
	EXPECT_FALSE(pJob->Finished());
	m_Started = m_Pool.TryInit(1);
	ASSERT_TRUE(m_Started);
	ASSERT_TRUE(m_Pool.TryAdd(pJob));
	ASSERT_TRUE(WaitForJobFinished(*pJob));
	EXPECT_EQ(pJob->State(), IJob::STATE_DONE);
	EXPECT_EQ(pRuns->load(), 1);
}
