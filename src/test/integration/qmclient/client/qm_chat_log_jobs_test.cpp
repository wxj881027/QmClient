#include <game/client/components/qmclient/qm_chat_log_jobs.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <new>
#include <stdexcept>
#include <vector>

namespace
{
	class QmChatLogWrites : public ::testing::Test
	{
	protected:
		CQmChatLogWriteQueue m_Queue;
		CJobPool m_Pool;
		bool m_Started = false;
		std::shared_ptr<IJob> m_pLastJob;
		std::shared_ptr<std::vector<int>> m_pWritten = std::make_shared<std::vector<int>>();

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
		bool Submit()
		{
			return m_Queue.TrySubmit([this](std::shared_ptr<IJob> pJob) {
				if(!m_Pool.TryAdd(pJob))
					return false;
				m_pLastJob = std::move(pJob);
				return true;
			});
		}
		void Enqueue(int Value)
		{
			m_Queue.Enqueue([pWritten = m_pWritten, Value] { pWritten->push_back(Value); });
		}
	};

	struct SWriteGate
	{
		std::promise<void> m_Started;
		std::promise<void> m_Release;
		std::shared_future<void> m_Released = m_Release.get_future().share();
		std::atomic<bool> m_TimedOut{false};
	};
}

TEST_F(QmChatLogWrites, KeepsOrderWhileFirstDiskActionIsBlocked)
{
	auto pGate = std::make_shared<SWriteGate>();
	m_Queue.Enqueue([pGate, pWritten = m_pWritten] {
		pGate->m_Started.set_value();
		pGate->m_TimedOut = pGate->m_Released.wait_for(std::chrono::seconds(5)) != std::future_status::ready;
		pWritten->push_back(1);
	});
	ASSERT_TRUE(Submit());
	const bool WorkerStarted = pGate->m_Started.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready;
	EXPECT_TRUE(WorkerStarted);
	Enqueue(2);
	Enqueue(3);
	EXPECT_FALSE(Submit());
	pGate->m_Release.set_value();
	ASSERT_TRUE(WaitForJobFinished(*m_pLastJob));
	EXPECT_FALSE(pGate->m_TimedOut.load());
	EXPECT_EQ(*m_pWritten, (std::vector<int>{1, 2, 3}));
}

TEST_F(QmChatLogWrites, StandardActionExceptionKeepsLaterActionsInOrder)
{
	m_Queue.Enqueue([] { throw std::bad_alloc(); });
	Enqueue(2);
	Enqueue(3);
	ASSERT_TRUE(Submit());
	ASSERT_TRUE(WaitForJobFinished(*m_pLastJob));
	EXPECT_EQ(m_pLastJob->State(), IJob::STATE_DONE);
	EXPECT_EQ(*m_pWritten, (std::vector<int>{2, 3}));
}

TEST_F(QmChatLogWrites, UnknownActionExceptionDoesNotStallNextBatch)
{
	m_Queue.Enqueue([] { throw 7; });
	ASSERT_TRUE(Submit());
	ASSERT_TRUE(WaitForJobFinished(*m_pLastJob));
	Enqueue(2);
	ASSERT_TRUE(Submit());
	ASSERT_TRUE(WaitForJobFinished(*m_pLastJob));
	EXPECT_EQ(*m_pWritten, (std::vector<int>{2}));
}

TEST_F(QmChatLogWrites, FailureAfterSideEffectIsNotRetried)
{
	m_Queue.Enqueue([pWritten = m_pWritten] {
		pWritten->push_back(1);
		throw std::runtime_error("write completed before a later failure");
	});
	Enqueue(2);
	ASSERT_TRUE(Submit());
	ASSERT_TRUE(WaitForJobFinished(*m_pLastJob));
	EXPECT_FALSE(Submit());
	Enqueue(3);
	ASSERT_TRUE(Submit());
	ASSERT_TRUE(WaitForJobFinished(*m_pLastJob));
	EXPECT_EQ(*m_pWritten, (std::vector<int>{1, 2, 3}));
}

TEST_F(QmChatLogWrites, RejectedSubmissionRetriesWithoutNewMessage)
{
	m_Pool.Shutdown();
	m_Started = false;
	Enqueue(1);
	EXPECT_FALSE(Submit());
	EXPECT_FALSE(Submit());
	m_Started = m_Pool.TryInit(1);
	ASSERT_TRUE(m_Started);
	ASSERT_TRUE(Submit());
	ASSERT_TRUE(WaitForJobFinished(*m_pLastJob));
	EXPECT_EQ(*m_pWritten, (std::vector<int>{1}));
}

TEST_F(QmChatLogWrites, ThrowingSubmissionRetriesWithoutNewMessage)
{
	Enqueue(1);
	EXPECT_FALSE(m_Queue.TrySubmit([](std::shared_ptr<IJob>) -> bool { throw std::bad_alloc(); }));
	EXPECT_FALSE(m_Queue.TrySubmit([](std::shared_ptr<IJob>) -> bool { throw 7; }));
	ASSERT_TRUE(Submit());
	ASSERT_TRUE(WaitForJobFinished(*m_pLastJob));
	EXPECT_EQ(*m_pWritten, (std::vector<int>{1}));
}

TEST_F(QmChatLogWrites, AcceptedJobReservationPreventsDuplicateSubmission)
{
	Enqueue(1);
	std::shared_ptr<IJob> pAccepted;
	ASSERT_TRUE(m_Queue.TrySubmit([&pAccepted](std::shared_ptr<IJob> pJob) {
		pAccepted = std::move(pJob);
		return true;
	}));
	Enqueue(2);
	EXPECT_FALSE(Submit());
	ASSERT_TRUE(m_Pool.TryAdd(pAccepted));
	ASSERT_TRUE(WaitForJobFinished(*pAccepted));
	EXPECT_EQ(*m_pWritten, (std::vector<int>{1, 2}));
}

TEST_F(QmChatLogWrites, AcceptedJobOwnsPendingActionsAfterQueueOwnerIsDestroyed)
{
	std::shared_ptr<IJob> pAccepted;
	{
		CQmChatLogWriteQueue Queue;
		Queue.Enqueue([pWritten = m_pWritten] { pWritten->push_back(1); });
		ASSERT_TRUE(Queue.TrySubmit([&pAccepted](std::shared_ptr<IJob> pJob) {
			pAccepted = std::move(pJob);
			return true;
		}));
	}
	ASSERT_TRUE(m_Pool.TryAdd(pAccepted));
	ASSERT_TRUE(WaitForJobFinished(*pAccepted));
	EXPECT_EQ(*m_pWritten, (std::vector<int>{1}));
}

TEST_F(QmChatLogWrites, ActionCanEnqueueNextWriteOutsideQueueLock)
{
	m_Queue.Enqueue([this] {
		m_pWritten->push_back(1);
		Enqueue(2);
	});
	ASSERT_TRUE(Submit());
	ASSERT_TRUE(WaitForJobFinished(*m_pLastJob));
	EXPECT_EQ(*m_pWritten, (std::vector<int>{1, 2}));
}

TEST_F(QmChatLogWrites, EmptyQueueDoesNotScheduleAJob)
{
	int Attempts = 0;
	EXPECT_FALSE(m_Queue.TrySubmit([&Attempts](std::shared_ptr<IJob>) {
		++Attempts;
		return true;
	}));
	EXPECT_EQ(Attempts, 0);
	Enqueue(1);
	ASSERT_TRUE(Submit());
	ASSERT_TRUE(WaitForJobFinished(*m_pLastJob));
	EXPECT_FALSE(Submit());
	EXPECT_EQ(*m_pWritten, (std::vector<int>{1}));
}
