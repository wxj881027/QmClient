// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/sphore.h>
#include <base/thread.h>

#include <engine/shared/host_lookup.h>
#include <engine/shared/jobs.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <chrono>
#include <functional>
#include <future>

static const int TEST_NUM_THREADS = 4;

class Jobs : public ::testing::Test
{
protected:
	CJobPool m_Pool;

	void SetUp() override
	{
		m_Pool.Init(TEST_NUM_THREADS);
	}

	void TearDown() override
	{
		m_Pool.Shutdown();
	}

	void Add(std::shared_ptr<IJob> pJob)
	{
		m_Pool.Add(std::move(pJob));
	}
};

class CJob : public IJob
{
	std::function<void()> m_JobFunction;
	void Run() override { m_JobFunction(); }

public:
	CJob(std::function<void()> &&JobFunction) :
		m_JobFunction(JobFunction) {}

	void Abortable(bool Abortable)
	{
		IJob::Abortable(Abortable);
	}
};

TEST_F(Jobs, Constructor)
{
}

TEST_F(Jobs, Simple)
{
	Add(std::make_shared<CJob>([] {}));
}

TEST_F(Jobs, Wait)
{
	SEMAPHORE sphore;
	sphore_init(&sphore);
	Add(std::make_shared<CJob>([&] { sphore_signal(&sphore); }));
	sphore_wait(&sphore);
	sphore_destroy(&sphore);
}

TEST_F(Jobs, AbortAbortable)
{
	auto pJob = std::make_shared<CJob>([&] {});
	pJob->Abortable(true);
	EXPECT_TRUE(pJob->IsAbortable());
	Add(pJob);
	EXPECT_TRUE(pJob->Abort());
	EXPECT_EQ(pJob->State(), IJob::STATE_ABORTED);
}

TEST_F(Jobs, AbortUnabortable)
{
	auto pJob = std::make_shared<CJob>([&] {});
	pJob->Abortable(false);
	EXPECT_FALSE(pJob->IsAbortable());
	Add(pJob);
	EXPECT_FALSE(pJob->Abort());
	EXPECT_NE(pJob->State(), IJob::STATE_ABORTED);
}

TEST_F(Jobs, LookupHost)
{
	static const char *HOST = "example.com";
	static const int NETTYPE = NETTYPE_ALL;
	auto pJob = std::make_shared<CHostLookup>(HOST, NETTYPE);

	EXPECT_STREQ(pJob->Hostname(), HOST);
	EXPECT_EQ(pJob->Nettype(), NETTYPE);

	Add(pJob);
	while(pJob->State() != IJob::STATE_DONE)
	{
		// yay, busy loop...
		thread_yield();
	}

	EXPECT_STREQ(pJob->Hostname(), HOST);
	EXPECT_EQ(pJob->Nettype(), NETTYPE);
	if(pJob->Result() == 0)
	{
		EXPECT_EQ(pJob->Addr().type & NETTYPE, pJob->Addr().type);
	}
}

TEST_F(Jobs, LookupHostWebsocket)
{
	static const char *HOST = "ws://example.com";
	static const int NETTYPE = NETTYPE_ALL;
	auto pJob = std::make_shared<CHostLookup>(HOST, NETTYPE);

	EXPECT_STREQ(pJob->Hostname(), HOST);
	EXPECT_EQ(pJob->Nettype(), NETTYPE);

	Add(pJob);
	while(pJob->State() != IJob::STATE_DONE)
	{
		// yay, busy loop...
		thread_yield();
	}

	EXPECT_STREQ(pJob->Hostname(), HOST);
	EXPECT_EQ(pJob->Nettype(), NETTYPE);
	if(pJob->Result() == 0)
	{
		EXPECT_EQ(pJob->Addr().type & (NETTYPE_WEBSOCKET_IPV4 | NETTYPE_WEBSOCKET_IPV6), pJob->Addr().type);
	}
}

TEST_F(Jobs, Many)
{
	std::atomic<int> ThreadsRunning(0);
	std::vector<std::shared_ptr<IJob>> vpJobs;
	SEMAPHORE sphore;
	sphore_init(&sphore);
	for(int i = 0; i < TEST_NUM_THREADS; i++)
	{
		std::shared_ptr<IJob> pJob = std::make_shared<CJob>([&] {
			int Prev = ThreadsRunning.fetch_add(1);
			if(Prev == TEST_NUM_THREADS - 1)
			{
				sphore_signal(&sphore);
			}
		});
		EXPECT_EQ(pJob->State(), IJob::STATE_QUEUED);
		vpJobs.push_back(pJob);
	}
	for(auto &pJob : vpJobs)
	{
		Add(pJob);
	}
	sphore_wait(&sphore);
	sphore_destroy(&sphore);
	TearDown();
	for(auto &pJob : vpJobs)
	{
		EXPECT_EQ(pJob->State(), IJob::STATE_DONE);
	}
	SetUp();
}

TEST(JobsShutdown, WaitsPastFormerTimeoutUntilRunningJobReturns)
{
	using namespace std::chrono_literals;
	CJobPool Pool;
	Pool.Init(1);
	std::promise<void> Started;
	std::promise<void> Release;
	auto ReleaseFuture = Release.get_future().share();
	auto pJob = std::make_shared<CJob>([&] {
		Started.set_value();
		ReleaseFuture.wait();
	});
	Pool.Add(pJob);
	const auto StartStatus = Started.get_future().wait_for(2s);
	EXPECT_EQ(StartStatus, std::future_status::ready);
	if(StartStatus != std::future_status::ready)
	{
		Release.set_value();
		Pool.Shutdown();
		return;
	}
	auto Shutdown = std::async(std::launch::async, [&] { Pool.Shutdown(); });
	// 超过原先 detach 的五秒边界，仍须等待持有引擎资源的作业返回。
	EXPECT_EQ(Shutdown.wait_for(5200ms), std::future_status::timeout);
	Release.set_value();
	EXPECT_EQ(Shutdown.wait_for(2s), std::future_status::ready);
	Shutdown.get();
	EXPECT_EQ(pJob->State(), IJob::STATE_DONE);
	// 关闭完整结束后可重新初始化，不能遗留访问上一轮信号量的线程。
	Pool.Init(1);
	auto pNextJob = std::make_shared<CJob>([] {});
	Pool.Add(pNextJob);
	Pool.Shutdown();
	EXPECT_EQ(pNextJob->State(), IJob::STATE_DONE);
}

TEST(JobsShutdown, CancelsRunningAndQueuedJobsBeforeReleasingResources)
{
	using namespace std::chrono_literals;
	CJobPool Pool;
	Pool.Init(1);
	std::promise<void> Started;
	std::promise<void> Release;
	auto ReleaseFuture = Release.get_future().share();
	auto pRunning = std::make_shared<CJob>([&] { Started.set_value(); ReleaseFuture.wait(); });
	pRunning->Abortable(true);
	Pool.Add(pRunning);
	const auto StartStatus = Started.get_future().wait_for(2s);
	EXPECT_EQ(StartStatus, std::future_status::ready);
	if(StartStatus != std::future_status::ready)
	{
		Release.set_value();
		Pool.Shutdown();
		return;
	}
	std::atomic<bool> QueuedRan{false};
	auto pQueued = std::make_shared<CJob>([&] { QueuedRan = true; });
	pQueued->Abortable(true);
	Pool.Add(pQueued);
	auto Shutdown = std::async(std::launch::async, [&] { Pool.Shutdown(); });
	const auto Deadline = std::chrono::steady_clock::now() + 2s;
	while(pRunning->State() != IJob::STATE_ABORTED && std::chrono::steady_clock::now() < Deadline)
		thread_yield();
	EXPECT_EQ(pRunning->State(), IJob::STATE_ABORTED);
	EXPECT_EQ(pQueued->State(), IJob::STATE_ABORTED);
	EXPECT_EQ(Shutdown.wait_for(0s), std::future_status::timeout);
	Release.set_value();
	EXPECT_EQ(Shutdown.wait_for(2s), std::future_status::ready);
	Shutdown.get();
	EXPECT_FALSE(QueuedRan);
	auto pRejected = std::make_shared<CJob>([&] { QueuedRan = true; });
	pRejected->Abortable(true);
	Pool.Add(pRejected);
	EXPECT_EQ(pRejected->State(), IJob::STATE_ABORTED);
	EXPECT_FALSE(QueuedRan);
}
