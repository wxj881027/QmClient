#include <engine/client/backend/vulkan/vulkan_recording_batch.h>

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <mutex>
#include <new>
#include <thread>

namespace
{
	class VulkanRecordingBatch : public ::testing::Test
	{
	protected:
		std::vector<int> m_vCommands{1, 2, 3};
		std::vector<int> m_vRecorded;
		std::atomic<bool> m_HasError{false};
		bool m_IsRendering = true;
		std::condition_variable m_Cond;
		int m_FinishCount = 0;
		int m_AbortCount = 0;
		bool m_Exception = false;

		template<typename TRecord, typename TFinish>
		void Run(TRecord &&Record, TFinish &&Finish)
		{
			RunVulkanRecordingBatch(m_vCommands, m_HasError, m_IsRendering, m_Cond, Record, Finish, [&](bool Exception) noexcept {
				++m_AbortCount;
				m_Exception = Exception;
				m_HasError = true;
			});
			EXPECT_FALSE(m_IsRendering);
			EXPECT_TRUE(m_vCommands.empty());
		}

		template<typename TRecord>
		void Run(TRecord &&Record)
		{
			Run(Record, [&] {
				++m_FinishCount;
				return true;
			});
		}
	};
}

TEST_F(VulkanRecordingBatch, SuccessfulCommandsAreRecordedInOrderAndFinishedOnce)
{
	Run([&](int Command) {
		m_vRecorded.push_back(Command);
		return true;
	});
	EXPECT_EQ(m_vRecorded, (std::vector<int>{1, 2, 3}));
	EXPECT_EQ(m_FinishCount, 1);
	EXPECT_EQ(m_AbortCount, 0);
	EXPECT_FALSE(m_HasError);
}

TEST_F(VulkanRecordingBatch, FailedCommandDiscardsTheTailWithoutFinishing)
{
	Run([&](int Command) {
		m_vRecorded.push_back(Command);
		return Command != 2;
	});
	EXPECT_EQ(m_vRecorded, (std::vector<int>{1, 2}));
	EXPECT_EQ(m_FinishCount, 0);
	EXPECT_EQ(m_AbortCount, 1);
	EXPECT_FALSE(m_Exception);
}

TEST_F(VulkanRecordingBatch, AllocationExceptionStopsTheBatchAndReleasesItsBorrowedCommands)
{
	Run([&](int Command) {
		m_vRecorded.push_back(Command);
		if(Command == 2)
			throw std::bad_alloc();
		return true;
	});
	EXPECT_EQ(m_vRecorded, (std::vector<int>{1, 2}));
	EXPECT_EQ(m_FinishCount, 0);
	EXPECT_EQ(m_AbortCount, 1);
	EXPECT_TRUE(m_Exception);
	EXPECT_TRUE(m_HasError);
}

TEST_F(VulkanRecordingBatch, UnexpectedExceptionAlsoCompletesTheBatch)
{
	Run([](int) -> bool { throw 7; });
	EXPECT_EQ(m_FinishCount, 0);
	EXPECT_EQ(m_AbortCount, 1);
	EXPECT_TRUE(m_Exception);
}

TEST_F(VulkanRecordingBatch, ExistingFrameErrorSkipsRecordingAndFinishing)
{
	m_HasError = true;
	Run([&](int Command) {
		m_vRecorded.push_back(Command);
		return true;
	});
	EXPECT_TRUE(m_vRecorded.empty());
	EXPECT_EQ(m_FinishCount, 0);
	EXPECT_EQ(m_AbortCount, 1);
	EXPECT_FALSE(m_Exception);
}

TEST_F(VulkanRecordingBatch, FrameFailureDuringTheLastCommandPreventsFinishing)
{
	Run([&](int Command) {
		m_vRecorded.push_back(Command);
		if(Command == 3)
			m_HasError = true;
		return true;
	});
	EXPECT_EQ(m_vRecorded.size(), 3U);
	EXPECT_EQ(m_FinishCount, 0);
	EXPECT_EQ(m_AbortCount, 1);
}

TEST_F(VulkanRecordingBatch, FinishFailureDiscardsTheRecordedBatch)
{
	Run([](int) { return true; }, [&] {
		++m_FinishCount;
		return false; });
	EXPECT_EQ(m_FinishCount, 1);
	EXPECT_EQ(m_AbortCount, 1);
	EXPECT_FALSE(m_Exception);
}

TEST_F(VulkanRecordingBatch, FinishExceptionStillCompletesTheBatch)
{
	Run([](int) { return true; }, []() -> bool { throw std::bad_alloc(); });
	EXPECT_EQ(m_AbortCount, 1);
	EXPECT_TRUE(m_Exception);
}

TEST_F(VulkanRecordingBatch, DiscardedCommandsCannotBeReplayedOnAnotherBatch)
{
	Run([](int) -> bool { throw std::bad_alloc(); });
	m_IsRendering = true;
	m_HasError = false;
	Run([&](int Command) {
		m_vRecorded.push_back(Command);
		return true;
	});
	EXPECT_TRUE(m_vRecorded.empty());
	EXPECT_EQ(m_FinishCount, 1);
	EXPECT_EQ(m_AbortCount, 1);
}

TEST_F(VulkanRecordingBatch, ExceptionCompletionWakesAWaitingOwner)
{
	std::mutex Mutex;
	std::promise<void> Waiting;
	auto Ready = Waiting.get_future();
	std::promise<void> Finished;
	auto Done = Finished.get_future();
	bool Completed = false;
	std::thread Owner([&] {
		std::unique_lock<std::mutex> Lock(Mutex);
		Waiting.set_value();
		Completed = m_Cond.wait_for(Lock, std::chrono::seconds(5), [&] { return !m_IsRendering; });
		Finished.set_value();
	});
	const bool OwnerReady = Ready.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
	{
		std::unique_lock<std::mutex> Lock(Mutex);
		Run([](int) -> bool { throw std::bad_alloc(); });
	}
	// 等待方自身超时后也会重查谓词；提前观察返回，才能发现遗漏通知。
	const bool Notified = Done.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
	Owner.join();
	EXPECT_TRUE(OwnerReady);
	EXPECT_TRUE(Notified);
	EXPECT_TRUE(Completed);
	EXPECT_TRUE(m_HasError);
}
