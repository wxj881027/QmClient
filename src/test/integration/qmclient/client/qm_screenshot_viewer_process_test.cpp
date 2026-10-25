#include <game/client/components/qmclient/screenshot_viewer.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <new>
#include <thread>

namespace
{
	PROCESS TestProcess()
	{
#if defined(CONF_FAMILY_WINDOWS)
		return reinterpret_cast<PROCESS>(1);
#else
		return 1;
#endif
	}

	struct SObservation
	{
		std::atomic<int> m_Waits{0};
		std::promise<PROCESS> m_Observed;
	};

	bool WaitForRelease(const std::weak_ptr<int> &Marker)
	{
		const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while(!Marker.expired() && std::chrono::steady_clock::now() < Deadline)
			std::this_thread::yield();
		return Marker.expired();
	}
}

TEST(QmScreenshotViewerProcess, RejectedWaiterDoesNotLaunchChildOrRetainContext)
{
	int Launches = 0;
	auto pMarker = std::make_shared<int>(0);
	std::weak_ptr<int> Marker = pMarker;
	EXPECT_FALSE(QmLaunchScreenshotViewerProcess([&]() {
		++Launches;
		return TestProcess(); }, [pMarker](PROCESS) { (void)pMarker; }, [](void (*)(void *), void *, const char *) noexcept -> void * { return nullptr; }));
	pMarker.reset();
	EXPECT_EQ(Launches, 0);
	EXPECT_TRUE(Marker.expired());
}

TEST(QmScreenshotViewerProcess, SuccessfulLaunchPublishesOneTokenToPreparedNativeWaiter)
{
	auto pObservation = std::make_shared<SObservation>();
	auto Observed = pObservation->m_Observed.get_future();
	auto pMarker = std::make_shared<int>(0);
	std::weak_ptr<int> Marker = pMarker;
	ASSERT_TRUE(QmLaunchScreenshotViewerProcess([]() { return TestProcess(); }, [pObservation, pMarker](PROCESS Process) {
		(void)pMarker;
		++pObservation->m_Waits;
		pObservation->m_Observed.set_value(Process); }));
	pMarker.reset();
	ASSERT_TRUE(WaitForRelease(Marker));
	ASSERT_EQ(Observed.wait_for(std::chrono::seconds(0)), std::future_status::ready);
	EXPECT_EQ(Observed.get(), TestProcess());
	EXPECT_EQ(pObservation->m_Waits.load(), 1);
}

TEST(QmScreenshotViewerProcess, InvalidLaunchReleasesPreparedWaiterWithoutWaitingForChild)
{
	auto pObservation = std::make_shared<SObservation>();
	auto pMarker = std::make_shared<int>(0);
	std::weak_ptr<int> Marker = pMarker;
	EXPECT_FALSE(QmLaunchScreenshotViewerProcess([]() { return INVALID_PROCESS; }, [pObservation, pMarker](PROCESS) { (void)pMarker; ++pObservation->m_Waits; }));
	pMarker.reset();
	ASSERT_TRUE(WaitForRelease(Marker));
	EXPECT_EQ(pObservation->m_Waits.load(), 0);
}

TEST(QmScreenshotViewerProcess, ThrowingLaunchReleasesPreparedWaiterAndLaterLaunchCanSucceed)
{
	auto pObservation = std::make_shared<SObservation>();
	auto pMarker = std::make_shared<int>(0);
	std::weak_ptr<int> Marker = pMarker;
	bool Launched = false;
	EXPECT_FALSE(QmLaunchScreenshotViewerProcess([&]() -> PROCESS {
		Launched = true;
		throw std::bad_alloc(); }, [pObservation, pMarker](PROCESS) { (void)pMarker; ++pObservation->m_Waits; }));
	EXPECT_TRUE(Launched);
	pMarker.reset();
	ASSERT_TRUE(WaitForRelease(Marker));
	EXPECT_EQ(pObservation->m_Waits.load(), 0);

	auto Observed = pObservation->m_Observed.get_future();
	auto pRetryMarker = std::make_shared<int>(0);
	std::weak_ptr<int> RetryMarker = pRetryMarker;
	ASSERT_TRUE(QmLaunchScreenshotViewerProcess([]() { return TestProcess(); }, [pObservation, pRetryMarker](PROCESS Process) {
		(void)pRetryMarker;
		++pObservation->m_Waits;
		pObservation->m_Observed.set_value(Process); }));
	pRetryMarker.reset();
	ASSERT_TRUE(WaitForRelease(RetryMarker));
	ASSERT_EQ(Observed.wait_for(std::chrono::seconds(0)), std::future_status::ready);
	EXPECT_EQ(Observed.get(), TestProcess());
	EXPECT_EQ(pObservation->m_Waits.load(), 1);
}
