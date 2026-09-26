#include <game/client/components/qmclient/qm_chat_log_jobs.h>

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

TEST(QmChatLogWrites, KeepsOrderWhileFirstDiskActionIsBlocked)
{
	CQmChatLogWriteQueue Queue;
	std::mutex Mutex;
	std::condition_variable Cv;
	bool Started = false;
	bool Released = false;
	std::vector<int> vWritten;

	CJobPool Pool;
	Pool.Init(1);
	auto pJob = Queue.Enqueue([&] {
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			Started = true;
		}
		Cv.notify_one();
		{
			std::unique_lock<std::mutex> Lock(Mutex);
			Cv.wait_for(Lock, std::chrono::seconds(2), [&] { return Released; });
		}
		vWritten.push_back(1);
	});
	EXPECT_NE(pJob, nullptr);
	Pool.Add(pJob);

	bool WorkerStarted;
	{
		std::unique_lock<std::mutex> Lock(Mutex);
		WorkerStarted = Cv.wait_for(Lock, std::chrono::seconds(2), [&] { return Started; });
	}
	if(WorkerStarted)
	{
		EXPECT_EQ(Queue.Enqueue([&] { vWritten.push_back(2); }), nullptr);
		EXPECT_EQ(Queue.Enqueue([&] { vWritten.push_back(3); }), nullptr);
	}
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		Released = true;
	}
	Cv.notify_one();
	Pool.Shutdown();

	ASSERT_TRUE(WorkerStarted);
	EXPECT_EQ(vWritten, (std::vector<int>{1, 2, 3}));
	EXPECT_NE(Queue.Enqueue([] {}), nullptr);
}
