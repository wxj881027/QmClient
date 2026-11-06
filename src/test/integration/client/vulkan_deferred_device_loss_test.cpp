#include <engine/client/backend/vulkan/backend_vulkan_qm_ext.h>
#include <engine/client/backend/vulkan/vulkan_render_workers.h>
#include <engine/client/backend_sdl.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

#if !defined(CONF_PLATFORM_EMSCRIPTEN)
TEST(VulkanDeferredDeviceLossIntegration, WorkerReportsStayDeferredUntilRecordersStop)
{
	struct SResult
	{
		bool m_Started = false;
		bool m_AllReported = false;
		bool m_UnchangedDuringRecording = false;
		bool m_Stopped = false;
		bool m_Applied = false;
		bool m_EmptyAfter = false;
		bool m_CapabilitiesDisabled = false;
		bool m_WorkerTimedOut = true;
		int m_ModeReadsBefore = -1;
		int m_ModeReads = 0;
		int m_Disables = 0;
		qm_vulkan_ext::EDisableReason m_Reason = qm_vulkan_ext::EDisableReason::NONE;
	};
	auto pResult = std::make_shared<SResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		qm_vulkan_ext::CDeferredDeviceLoss Pending;
		SBackendCapabilities Capabilities;
		Capabilities.m_MediaIslandSdf = true;
		Capabilities.m_RoundedRectSdf = true;
		std::mutex Mutex;
		std::condition_variable Condition;
		int Reports = 0;
		bool Release = false;
		std::atomic<bool> WorkerTimedOut{false};
		CVulkanRenderWorkers Workers;
		pResult->m_Started = Workers.Start(2, [&](size_t) {
			Pending.Report();
			std::unique_lock<std::mutex> Lock(Mutex);
			++Reports;
			Condition.notify_all();
			if(!Condition.wait_for(Lock, std::chrono::seconds(3), [&] { return Release; }))
				WorkerTimedOut.store(true, std::memory_order_relaxed);
		});
		if(!pResult->m_Started)
			return;
		{
			std::unique_lock<std::mutex> Lock(Mutex);
			pResult->m_AllReported = Condition.wait_for(Lock, std::chrono::seconds(2), [&] { return Reports == 2; });
			pResult->m_UnchangedDuringRecording = Capabilities.m_MediaIslandSdf.load(std::memory_order_acquire) && Capabilities.m_RoundedRectSdf.load(std::memory_order_acquire);
			Release = true;
			Condition.notify_all();
		}
		Workers.Stop();
		pResult->m_WorkerTimedOut = WorkerTimedOut.load(std::memory_order_relaxed);
		pResult->m_Stopped = Workers.Size() == 0;
		pResult->m_ModeReadsBefore = pResult->m_ModeReads;
		auto ReadMode = [&]() noexcept {
			++pResult->m_ModeReads;
			return qm_vulkan_ext::EEnhancedMode::AUTO;
		};
		auto Disable = [&](qm_vulkan_ext::EDisableReason Reason) noexcept {
			++pResult->m_Disables;
			pResult->m_Reason = Reason;
			Capabilities.m_MediaIslandSdf.store(false, std::memory_order_release);
			Capabilities.m_RoundedRectSdf.store(false, std::memory_order_release);
		};
		pResult->m_Applied = Pending.ConsumeAfterRecording(false, ReadMode, Disable);
		pResult->m_EmptyAfter = !Pending.ConsumeAfterRecording(false, ReadMode, Disable);
		pResult->m_CapabilitiesDisabled = !Capabilities.m_MediaIslandSdf.load(std::memory_order_acquire) && !Capabilities.m_RoundedRectSdf.load(std::memory_order_acquire);
	}));
	EXPECT_TRUE(pResult->m_Started);
	EXPECT_TRUE(pResult->m_AllReported);
	EXPECT_TRUE(pResult->m_UnchangedDuringRecording);
	EXPECT_TRUE(pResult->m_Stopped);
	EXPECT_FALSE(pResult->m_WorkerTimedOut);
	EXPECT_EQ(pResult->m_ModeReadsBefore, 0);
	EXPECT_TRUE(pResult->m_Applied);
	EXPECT_TRUE(pResult->m_EmptyAfter);
	EXPECT_TRUE(pResult->m_CapabilitiesDisabled);
	EXPECT_EQ(pResult->m_ModeReads, 1);
	EXPECT_EQ(pResult->m_Disables, 1);
	EXPECT_EQ(pResult->m_Reason, qm_vulkan_ext::EDisableReason::DEVICE_LOST);
}

TEST(VulkanDeferredDeviceLossIntegration, ReportsAcrossWorkerRestartApplyOncePerQuiescentBatch)
{
	struct SResult
	{
		int m_StartedBatches = 0;
		int m_Reports = 0;
		int m_ModeReads = 0;
		int m_Disables = 0;
		bool m_AppliedEach = true;
		bool m_EmptyBetween = true;
	};
	auto pResult = std::make_shared<SResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		qm_vulkan_ext::CDeferredDeviceLoss Pending;
		std::atomic<int> Reports{0};
		CVulkanRenderWorkers Workers;
		auto ReadMode = [&]() noexcept {
			++pResult->m_ModeReads;
			return qm_vulkan_ext::EEnhancedMode::AUTO;
		};
		auto Disable = [&](qm_vulkan_ext::EDisableReason) noexcept { ++pResult->m_Disables; };
		for(int Batch = 0; Batch < 2; ++Batch)
		{
			if(!Workers.Start(2, [&](size_t) {
				   Pending.Report();
				   Reports.fetch_add(1, std::memory_order_relaxed);
			   }))
				return;
			++pResult->m_StartedBatches;
			Workers.Stop();
			pResult->m_AppliedEach &= Pending.ConsumeAfterRecording(false, ReadMode, Disable);
			pResult->m_EmptyBetween &= !Pending.ConsumeAfterRecording(false, ReadMode, Disable);
		}
		pResult->m_Reports = Reports.load(std::memory_order_relaxed);
	}));
	EXPECT_EQ(pResult->m_StartedBatches, 2);
	EXPECT_EQ(pResult->m_Reports, 4);
	EXPECT_TRUE(pResult->m_AppliedEach);
	EXPECT_TRUE(pResult->m_EmptyBetween);
	EXPECT_EQ(pResult->m_ModeReads, 2);
	EXPECT_EQ(pResult->m_Disables, 2);
}
#endif
