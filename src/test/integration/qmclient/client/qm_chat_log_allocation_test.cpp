#include <game/client/components/qmclient/qm_chat_log_jobs.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>
#include <test/support/prediction_allocation_failure.h>

#include <functional>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace
{
	class QmChatLogAllocation : public ::testing::Test
	{
	protected:
		CQmChatLogWriteQueue m_Queue;
		CJobPool m_Pool;
		bool m_Started = false;
		std::shared_ptr<IJob> m_pJob;
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
				m_pJob = std::move(pJob);
				return true;
			});
		}
		std::function<void()> Action(int Value)
		{
			return [pWritten = m_pWritten, Value] { pWritten->push_back(Value); };
		}
	};
}

TEST_F(QmChatLogAllocation, JobAllocationFailureRetainsPendingActionsAndAllowsRetry)
{
	m_Queue.Enqueue(Action(1));
	bool Submitted, Triggered;
	int Submissions = 0;
	{
		CPredictionAllocationFailure Fault;
		Submitted = m_Queue.TrySubmit([&Submissions](std::shared_ptr<IJob>) {
			++Submissions;
			return true;
		});
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Submitted);
	EXPECT_EQ(Submissions, 0);
	ASSERT_TRUE(Submit());
	ASSERT_TRUE(WaitForJobFinished(*m_pJob));
	EXPECT_EQ(*m_pWritten, (std::vector<int>{1}));
}

TEST_F(QmChatLogAllocation, ActionAllocationFailureKeepsPreviouslyQueuedActions)
{
	int Accepted = 0;
	bool Triggered = false;
	// 逐次只拒绝本次入队的首次真实分配，不依赖标准库 deque 的块大小。
	for(; Accepted < 4096; ++Accepted)
	{
		auto PreparedAction = Action(Accepted);
		bool Enqueued = true;
		{
			CPredictionAllocationFailure Fault;
			try
			{
				m_Queue.Enqueue(std::move(PreparedAction));
			}
			catch(const std::bad_alloc &)
			{
				Enqueued = false;
			}
			Triggered = Fault.Triggered();
		}
		if(Triggered)
		{
			EXPECT_FALSE(Enqueued);
			break;
		}
		ASSERT_TRUE(Enqueued);
	}
	ASSERT_TRUE(Triggered);
	m_Queue.Enqueue(Action(Accepted + 1));
	ASSERT_TRUE(Submit());
	ASSERT_TRUE(WaitForJobFinished(*m_pJob));
	std::vector<int> vExpected;
	for(int Value = 0; Value < Accepted; ++Value)
		vExpected.push_back(Value);
	vExpected.push_back(Accepted + 1);
	EXPECT_EQ(*m_pWritten, vExpected);
}
