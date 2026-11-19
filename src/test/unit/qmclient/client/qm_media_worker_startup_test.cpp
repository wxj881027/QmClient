#include <game/client/components/qmclient/media_worker.h>

#include <gtest/gtest.h>

#include <new>
#include <system_error>

namespace
{
	struct COperations : CQmMediaWorker::COperations
	{
		int m_CreateCalls = 0;
		int m_ThreadCalls = 0;
		int m_Destroyed = 0;
		int m_Fault = 0;
		bool m_EmptyState = false;
		bool m_FailThread = false;
		struct CState : CQmMediaWorker::CState
		{
			int &m_Destroyed;
			explicit CState(int &Destroyed) :
				m_Destroyed(Destroyed) {}
			~CState() override { ++m_Destroyed; }
			void Run(const std::atomic_bool &) override { ADD_FAILURE(); }
		};
		void Fail()
		{
			if(m_Fault == 1)
				throw std::bad_alloc();
			if(m_Fault == 2)
				throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
			if(m_Fault == 3)
				throw 7;
		}
		std::unique_ptr<CQmMediaWorker::CState> CreateState() override
		{
			++m_CreateCalls;
			if(!m_FailThread)
				Fail();
			if(m_EmptyState)
				return nullptr;
			return std::make_unique<CState>(m_Destroyed);
		}
		std::thread CreateThread(CQmMediaWorker::CState &, const std::atomic_bool &, std::atomic_bool &) override
		{
			++m_ThreadCalls;
			if(m_FailThread)
				Fail();
			return {};
		}
	};
}

TEST(QmMediaRetryClock, FirstAttemptIsImmediateAndFailedAttemptsUseTheInterval)
{
	CQmMediaRetryClock Clock;
	EXPECT_TRUE(Clock.Begin(1000, 200));
	EXPECT_FALSE(Clock.Begin(1000, 200));
	EXPECT_FALSE(Clock.Begin(1199, 200));
	EXPECT_TRUE(Clock.Begin(1200, 200));
}

TEST(QmMediaRetryClock, ResetAllowsAnExplicitRestartBeforeThePreviousDeadline)
{
	CQmMediaRetryClock Clock;
	ASSERT_TRUE(Clock.Begin(1000, 500));
	Clock.Reset();
	EXPECT_TRUE(Clock.Begin(1001, 500));
}

TEST(QmMediaRetryClock, NewClockEpochDoesNotLockOutRecovery)
{
	CQmMediaRetryClock Clock;
	ASSERT_TRUE(Clock.Begin(1000, 500));
	EXPECT_TRUE(Clock.Begin(1, 500));
	EXPECT_FALSE(Clock.Begin(2, 500));
	EXPECT_TRUE(Clock.Begin(501, 500));
}

TEST(QmMediaRetryClock, ZeroIntervalStillSuppressesRepeatedAttemptsAtTheSameTime)
{
	CQmMediaRetryClock Clock;
	EXPECT_TRUE(Clock.Begin(1, 0));
	EXPECT_FALSE(Clock.Begin(1, 0));
	EXPECT_TRUE(Clock.Begin(2, 0));
}

TEST(QmMediaWorkerStartup, StateCreationExceptionsDoNotPublishOrStartAThread)
{
	for(int Fault : {1, 2, 3})
	{
		SCOPED_TRACE(Fault);
		COperations Operations;
		Operations.m_Fault = Fault;
		CQmMediaWorker Worker;
		EXPECT_FALSE(Worker.EnsureStarted(Operations, 0, 200));
		EXPECT_FALSE(Worker.Running());
		EXPECT_EQ(Worker.State(), nullptr);
		EXPECT_EQ(Operations.m_CreateCalls, 1);
		EXPECT_EQ(Operations.m_ThreadCalls, 0);
	}
}

TEST(QmMediaWorkerStartup, EmptyPreparedStateDoesNotEnterThreadCreation)
{
	COperations Operations;
	Operations.m_EmptyState = true;
	CQmMediaWorker Worker;
	EXPECT_FALSE(Worker.EnsureStarted(Operations, 0, 200));
	EXPECT_EQ(Worker.State(), nullptr);
	EXPECT_EQ(Operations.m_ThreadCalls, 0);
}

TEST(QmMediaWorkerStartup, ThreadCreationExceptionsReleasePreparedResourcesWithoutPublishing)
{
	for(int Fault : {1, 2, 3})
	{
		SCOPED_TRACE(Fault);
		COperations Operations;
		Operations.m_Fault = Fault;
		Operations.m_FailThread = true;
		CQmMediaWorker Worker;
		EXPECT_FALSE(Worker.EnsureStarted(Operations, 0, 200));
		EXPECT_FALSE(Worker.Running());
		EXPECT_EQ(Worker.State(), nullptr);
		EXPECT_EQ(Operations.m_Destroyed, 1);
	}
}

TEST(QmMediaWorkerStartup, NonJoinableThreadIsNotPublishedAndRetryWaitsForTheInterval)
{
	COperations Operations;
	CQmMediaWorker Worker;
	EXPECT_FALSE(Worker.EnsureStarted(Operations, 1000, 200));
	EXPECT_EQ(Worker.State(), nullptr);
	EXPECT_EQ(Operations.m_Destroyed, 1);
	EXPECT_FALSE(Worker.EnsureStarted(Operations, 1199, 200));
	EXPECT_EQ(Operations.m_CreateCalls, 1);
	EXPECT_FALSE(Worker.EnsureStarted(Operations, 1200, 200));
	EXPECT_EQ(Operations.m_CreateCalls, 2);
	EXPECT_EQ(Operations.m_Destroyed, 2);
}

TEST(QmMediaWorkerStartup, RepeatedStopResetsFailureBackoffWithoutCreatingState)
{
	COperations Operations;
	Operations.m_EmptyState = true;
	CQmMediaWorker Worker;
	EXPECT_FALSE(Worker.EnsureStarted(Operations, 1000, 500));
	Worker.Stop();
	Worker.Stop();
	EXPECT_FALSE(Worker.EnsureStarted(Operations, 1001, 500));
	EXPECT_EQ(Operations.m_CreateCalls, 2);
	EXPECT_EQ(Worker.State(), nullptr);
}
