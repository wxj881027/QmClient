#include <game/client/components/qmclient/monitoring/monitoring.h>

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace
{
	class CBlockedDeviceSamplerTest : public ::testing::Test
	{
		std::mutex m_Mutex;
		std::condition_variable m_Cv;
		int m_Calls = 0;
		int m_Released = 0;
		bool m_TimedOut = false;

		SQmDevicePerfSample Sample()
		{
			std::unique_lock<std::mutex> Lock(m_Mutex);
			const int Call = ++m_Calls;
			m_Cv.notify_all();
			// 工作线程的每次查询由测试显式放行，失败时也有上限。
			if(!m_Cv.wait_for(Lock, std::chrono::seconds(3), [&] { return m_Released >= Call; }))
				m_TimedOut = true;
			SQmDevicePerfSample Result;
			Result.m_CpuUsagePct = Call == 1 ? 12.0f : 24.0f;
			Result.m_MemoryUsageMb = Call == 1 ? 512.0f : 1024.0f;
			Result.m_Available = true;
			return Result;
		}

	protected:
		CQmAsyncDevicePerfSampler m_Sampler{[this] { return Sample(); }, std::chrono::milliseconds(1)};

		bool WaitForCall(int Expected)
		{
			std::unique_lock<std::mutex> Lock(m_Mutex);
			return m_Cv.wait_for(Lock, std::chrono::seconds(2), [&] { return m_Calls >= Expected; });
		}

		void Release(int Call)
		{
			{
				std::lock_guard<std::mutex> Lock(m_Mutex);
				m_Released = Call;
			}
			m_Cv.notify_all();
		}

		bool WaitForSnapshot()
		{
			const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
			while(std::chrono::steady_clock::now() < Deadline)
			{
				if(m_Sampler.Snapshot().m_Version != 0)
					return true;
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			return false;
		}

		void TearDown() override
		{
			// ASSERT 失败同样释放阻塞查询，析构不会卡在 Stop 的 join。
			Release(1000000);
			m_Sampler.Stop();
			EXPECT_FALSE(m_TimedOut);
		}
	};
}

TEST_F(CBlockedDeviceSamplerTest, SnapshotStaysReadableUntilQueryPublishesAndStopClearsIt)
{
	QmUpdateDevicePerfSamplerState(m_Sampler, true);
	ASSERT_TRUE(WaitForCall(1));
	const auto Before = m_Sampler.Snapshot();
	EXPECT_EQ(Before.m_Version, 0u);
	EXPECT_FLOAT_EQ(Before.m_Sample.m_CpuUsagePct, -1.0f);
	Release(1);
	ASSERT_TRUE(WaitForSnapshot());
	const auto After = m_Sampler.Snapshot();
	EXPECT_FLOAT_EQ(After.m_Sample.m_CpuUsagePct, 12.0f);
	EXPECT_FLOAT_EQ(After.m_Sample.m_MemoryUsageMb, 512.0f);
}

TEST_F(CBlockedDeviceSamplerTest, DisableDuringQueryRejectsOldGenerationAndRestartPublishesFreshSample)
{
	QmUpdateDevicePerfSamplerState(m_Sampler, true);
	ASSERT_TRUE(WaitForCall(1));
	QmUpdateDevicePerfSamplerState(m_Sampler, false);
	EXPECT_EQ(m_Sampler.Snapshot().m_Version, 0u);
	Release(1);
	QmUpdateDevicePerfSamplerState(m_Sampler, true);
	ASSERT_TRUE(WaitForCall(2));
	// 第二次查询已开始，说明第一次已过发布判断；关闭前的结果不可见。
	EXPECT_EQ(m_Sampler.Snapshot().m_Version, 0u);
	Release(2);
	ASSERT_TRUE(WaitForSnapshot());
	const auto After = m_Sampler.Snapshot();
	EXPECT_EQ(After.m_Version, 1u);
	EXPECT_FLOAT_EQ(After.m_Sample.m_CpuUsagePct, 24.0f);
}
