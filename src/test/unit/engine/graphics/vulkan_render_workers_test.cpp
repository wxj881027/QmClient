#include <engine/client/backend/vulkan/vulkan_render_workers.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>

#include <atomic>
#include <cstdlib>
#include <future>
#include <limits>
#include <new>
#include <string>
#include <system_error>

#if !defined(CONF_PLATFORM_EMSCRIPTEN)
namespace
{
	class VulkanRenderWorkersDeathTest : public ::testing::Test
	{
		std::string m_PreviousDeathTestStyle;
		void SetUp() override
		{
			m_PreviousDeathTestStyle = GTEST_FLAG_GET(death_test_style);
			GTEST_FLAG_SET(death_test_style, "threadsafe");
		}
		void TearDown() override { GTEST_FLAG_SET(death_test_style, m_PreviousDeathTestStyle); }
	};

	// 启动握手或 join 回归也必须有界；所有资源均由隔离进程的场景持有。
	template<typename F>
	void RunWorkerScenario(F &&Scenario)
	{
		const bool Completed = WaitForAsyncIo(std::forward<F>(Scenario));
		std::_Exit(Completed && !::testing::Test::HasFailure() ? 0 : 1);
	}

	void WaitForStop(CVulkanRenderWorkers &Workers, size_t Index, std::atomic<int> &Entered, std::atomic<int> &Exited)
	{
		auto &Worker = Workers[Index];
		std::unique_lock<std::mutex> Lock(Worker.m_Mutex);
		++Entered;
		Worker.m_Cond.wait(Lock, [&] { return Worker.m_Finished; });
		++Exited;
	}
}

TEST_F(VulkanRenderWorkersDeathTest, StartAndStopJoinEveryNativeWorker)
{
	ASSERT_EXIT(RunWorkerScenario([] {
		std::atomic<int> Entered{0}, Exited{0};
		CVulkanRenderWorkers Workers;
		ASSERT_TRUE(Workers.Start(2, [&](size_t Index) { WaitForStop(Workers, Index, Entered, Exited); }));
		EXPECT_EQ(Workers.Size(), 2U);
		Workers.Stop();
		EXPECT_EQ(Workers.Size(), 0U);
		EXPECT_EQ(Entered, 2);
		EXPECT_EQ(Exited, 2);
	}),
		::testing::ExitedWithCode(0), "");
}

TEST_F(VulkanRenderWorkersDeathTest, DestructorStopsWorkersBeforeTheirOwnerCanDisappear)
{
	ASSERT_EXIT(RunWorkerScenario([] {
		std::atomic<int> Entered{0}, Exited{0};
		{
			CVulkanRenderWorkers Workers;
			ASSERT_TRUE(Workers.Start(2, [&](size_t Index) { WaitForStop(Workers, Index, Entered, Exited); }));
		}
		EXPECT_EQ(Entered, 2);
		EXPECT_EQ(Exited, 2);
	}),
		::testing::ExitedWithCode(0), "");
}

TEST_F(VulkanRenderWorkersDeathTest, FirstNativeCreationFailureDoesNotWaitForAnUnstartedWorker)
{
	ASSERT_EXIT(RunWorkerScenario([] {
		int Attempts = 0;
		std::atomic<int> Entered{0};
		CVulkanRenderWorkers Workers;
		EXPECT_FALSE(Workers.Start(2, [&](size_t) { ++Entered; }, [&](auto) -> std::thread {
			++Attempts;
			throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again)); }));
		EXPECT_EQ(Attempts, 1);
		EXPECT_EQ(Entered, 0);
		EXPECT_EQ(Workers.Size(), 0U);
		Workers.Stop();
	}),
		::testing::ExitedWithCode(0), "");
}

TEST_F(VulkanRenderWorkersDeathTest, LaterNativeCreationFailureJoinsTheStartedPrefix)
{
	ASSERT_EXIT(RunWorkerScenario([] {
		int Attempts = 0;
		std::atomic<int> Entered{0}, Exited{0};
		CVulkanRenderWorkers Workers;
		EXPECT_FALSE(Workers.Start(3, [&](size_t Index) { WaitForStop(Workers, Index, Entered, Exited); }, [&](auto Entry) {
			if(++Attempts == 3)
				throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
			return std::thread(std::move(Entry)); }));
		EXPECT_EQ(Attempts, 3);
		EXPECT_EQ(Entered, 2);
		EXPECT_EQ(Exited, 2);
		EXPECT_EQ(Workers.Size(), 0U);
	}),
		::testing::ExitedWithCode(0), "");
}

TEST_F(VulkanRenderWorkersDeathTest, LaunchAllocationFailureAlsoJoinsTheStartedPrefix)
{
	ASSERT_EXIT(RunWorkerScenario([] {
		int Attempts = 0;
		std::atomic<int> Entered{0}, Exited{0};
		CVulkanRenderWorkers Workers;
		EXPECT_FALSE(Workers.Start(2, [&](size_t Index) { WaitForStop(Workers, Index, Entered, Exited); }, [&](auto Entry) {
			if(++Attempts == 2)
				throw std::bad_alloc();
			return std::thread(std::move(Entry)); }));
		EXPECT_EQ(Entered, 1);
		EXPECT_EQ(Exited, 1);
		EXPECT_EQ(Workers.Size(), 0U);
	}),
		::testing::ExitedWithCode(0), "");
}

TEST_F(VulkanRenderWorkersDeathTest, FailedStartCanBeRetriedAndStoppedMoreThanOnce)
{
	ASSERT_EXIT(RunWorkerScenario([] {
		std::atomic<int> Entered{0}, Exited{0};
		CVulkanRenderWorkers Workers;
		auto Run = [&](size_t Index) { WaitForStop(Workers, Index, Entered, Exited); };
		EXPECT_FALSE(Workers.Start(2, Run, [](auto) -> std::thread { throw std::bad_alloc(); }));
		ASSERT_TRUE(Workers.Start(2, Run));
		Workers.Stop();
		Workers.Stop();
		EXPECT_EQ(Entered, 2);
		EXPECT_EQ(Exited, 2);
		ASSERT_TRUE(Workers.Start(1, Run));
		Workers.Stop();
		EXPECT_EQ(Entered, 3);
		EXPECT_EQ(Exited, 3);
	}),
		::testing::ExitedWithCode(0), "");
}

TEST_F(VulkanRenderWorkersDeathTest, RepeatedStartDoesNotReplaceLiveWorkers)
{
	ASSERT_EXIT(RunWorkerScenario([] {
		std::atomic<int> Entered{0}, Exited{0}, Replacement{0};
		CVulkanRenderWorkers Workers;
		ASSERT_TRUE(Workers.Start(2, [&](size_t Index) { WaitForStop(Workers, Index, Entered, Exited); }));
		EXPECT_FALSE(Workers.Start(3, [&](size_t) { ++Replacement; }));
		EXPECT_EQ(Workers.Size(), 2U);
		Workers.Stop();
		EXPECT_EQ(Entered, 2);
		EXPECT_EQ(Exited, 2);
		EXPECT_EQ(Replacement, 0);
	}),
		::testing::ExitedWithCode(0), "");
}

TEST_F(VulkanRenderWorkersDeathTest, ZeroWorkersDoesNotInvokeTheLauncher)
{
	ASSERT_EXIT(RunWorkerScenario([] {
		CVulkanRenderWorkers Workers;
		int Attempts = 0;
		EXPECT_TRUE(Workers.Start(0, [](size_t) {}, [&](auto Entry) {
			++Attempts;
			return std::thread(std::move(Entry)); }));
		EXPECT_EQ(Attempts, 0);
		EXPECT_EQ(Workers.Size(), 0U);
		Workers.Stop();
	}),
		::testing::ExitedWithCode(0), "");
}

TEST_F(VulkanRenderWorkersDeathTest, UnrepresentableWorkerListFailsBeforeLaunching)
{
	ASSERT_EXIT(RunWorkerScenario([] {
		CVulkanRenderWorkers Workers;
		int Attempts = 0;
		EXPECT_FALSE(Workers.Start(std::numeric_limits<size_t>::max(), [](size_t) {}, [&](auto Entry) {
			++Attempts;
			return std::thread(std::move(Entry)); }));
		EXPECT_EQ(Attempts, 0);
		EXPECT_EQ(Workers.Size(), 0U);
	}),
		::testing::ExitedWithCode(0), "");
}

TEST_F(VulkanRenderWorkersDeathTest, EmptyNativeHandleDoesNotEnterTheStartupWait)
{
	ASSERT_EXIT(RunWorkerScenario([] {
		CVulkanRenderWorkers Workers;
		EXPECT_FALSE(Workers.Start(2, [](size_t) {}, [](auto) { return std::thread(); }));
		EXPECT_EQ(Workers.Size(), 0U);
	}),
		::testing::ExitedWithCode(0), "");
}

TEST_F(VulkanRenderWorkersDeathTest, StopSignalsEveryWorkerBeforeWaitingForAnyOne)
{
	ASSERT_EXIT(RunWorkerScenario([] {
		std::promise<void> SecondStopped;
		auto Second = SecondStopped.get_future();
		std::atomic<bool> AllSignalled{false};
		CVulkanRenderWorkers Workers;
		ASSERT_TRUE(Workers.Start(2, [&](size_t Index) {
			auto &Worker = Workers[Index];
			std::unique_lock<std::mutex> Lock(Worker.m_Mutex);
			Worker.m_Cond.wait(Lock, [&] { return Worker.m_Finished; });
			Lock.unlock();
			if(Index == 1)
				SecondStopped.set_value();
			else
				AllSignalled = Second.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
		}));
		Workers.Stop();
		EXPECT_TRUE(AllSignalled);
	}),
		::testing::ExitedWithCode(0), "");
}
#endif
