#include <base/system.h>

#include <engine/client/qm_hang_diagnostics.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>

#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <new>
#include <thread>

namespace
{
	using namespace QmHangDiagnostics;

	struct CReportState
	{
		std::mutex m_Mutex;
		std::condition_variable m_Cv;
		int m_Calls = 0;
		SSnapshot m_Last;
		int m_Throw = 0;
		bool m_Block = false;
		bool m_Release = false;
		bool m_TimedOut = false;
		bool m_StopStarted = false;
		std::thread::id m_ReportThread;

		bool WaitForCalls(int Count)
		{
			std::unique_lock<std::mutex> Lock(m_Mutex);
			return m_Cv.wait_for(Lock, std::chrono::seconds(1), [&] { return m_Calls >= Count; });
		}

		static void Report(void *pUser, int64_t, const SSnapshot &Snapshot)
		{
			auto &State = *static_cast<CReportState *>(pUser);
			std::unique_lock<std::mutex> Lock(State.m_Mutex);
			State.m_Last = Snapshot;
			State.m_ReportThread = std::this_thread::get_id();
			++State.m_Calls;
			State.m_Cv.notify_all();
			if(State.m_Block)
				State.m_TimedOut = !State.m_Cv.wait_for(Lock, std::chrono::seconds(2), [&] { return State.m_Release; });
			const int Throw = State.m_Throw;
			Lock.unlock();
			if(Throw == 1)
				throw std::bad_alloc();
			if(Throw == 2)
				throw 7;
		}
	};

	struct CState
	{
		CSnapshotStore m_Store;
		CReportState m_Report;
		CWatchdog m_Watchdog{m_Store};
		bool Start(const SSnapshot &Initial, CWatchdog::FCreateThread pfnCreate = nullptr)
		{
			return m_Watchdog.Start(Initial, 1, CReportState::Report, &m_Report, std::chrono::milliseconds(1), pfnCreate);
		}
	};

	SSnapshot InitialSnapshot()
	{
		SSnapshot Initial;
		Initial.m_State = IClient::STATE_LOADING;
		str_copy(Initial.m_aCurrentMap, "initial-map");
		str_copy(Initial.m_aServerAddr, "initial-server");
		// 零时刻是合法心跳，实际单调时钟在第一次轮询时已越过超时边界。
		Initial.m_LastHeartbeat = 0;
		return Initial;
	}

	std::thread FailCreation(std::function<void()>) { throw std::bad_alloc(); }
}

TEST(QmHangWatchdogLifecycle, TimeoutReportsMatchingSnapshotFromWorkerThread)
{
	auto pState = std::make_shared<CState>();
	ASSERT_TRUE(WaitForAsyncIo([pState] {
		const auto OwnerThread = std::this_thread::get_id();
		ASSERT_TRUE(pState->Start(InitialSnapshot()));
		EXPECT_TRUE(pState->m_Report.WaitForCalls(1));
		pState->m_Watchdog.Stop();
		EXPECT_EQ(pState->m_Report.m_Calls, 1);
		EXPECT_NE(pState->m_Report.m_ReportThread, OwnerThread);
		EXPECT_EQ(pState->m_Report.m_Last.m_State, IClient::STATE_LOADING);
		EXPECT_STREQ(pState->m_Report.m_Last.m_aCurrentMap, "initial-map");
		EXPECT_STREQ(pState->m_Report.m_Last.m_aServerAddr, "initial-server");
	}));
}

TEST(QmHangWatchdogLifecycle, AllocationFailureInReportDoesNotEscapeWorkerOrRepeatReport)
{
	auto pState = std::make_shared<CState>();
	pState->m_Report.m_Throw = 1;
	ASSERT_TRUE(WaitForAsyncIo([pState] {
		ASSERT_TRUE(pState->Start(InitialSnapshot()));
		EXPECT_TRUE(pState->m_Report.WaitForCalls(1));
		pState->m_Store.Publish(InitialSnapshot());
		EXPECT_FALSE(pState->m_Store.TryClaimReport(time_get_nanoseconds().count(), 1).has_value());
		pState->m_Watchdog.Stop();
		EXPECT_EQ(pState->m_Report.m_Calls, 1);
	}));
}

TEST(QmHangWatchdogLifecycle, UnknownReportExceptionDoesNotEscapeWorker)
{
	auto pState = std::make_shared<CState>();
	pState->m_Report.m_Throw = 2;
	ASSERT_TRUE(WaitForAsyncIo([pState] {
		ASSERT_TRUE(pState->Start(InitialSnapshot()));
		EXPECT_TRUE(pState->m_Report.WaitForCalls(1));
		pState->m_Watchdog.Stop();
		EXPECT_EQ(pState->m_Report.m_Calls, 1);
	}));
}

TEST(QmHangWatchdogLifecycle, FailedStartCanRetryWithARealWorker)
{
	auto pState = std::make_shared<CState>();
	ASSERT_TRUE(WaitForAsyncIo([pState] {
		EXPECT_FALSE(pState->Start(InitialSnapshot(), FailCreation));
		EXPECT_FALSE(pState->m_Watchdog.Started());
		ASSERT_TRUE(pState->Start(InitialSnapshot()));
		EXPECT_TRUE(pState->m_Report.WaitForCalls(1));
		pState->m_Watchdog.Stop();
		EXPECT_EQ(pState->m_Report.m_Calls, 1);
	}));
}

TEST(QmHangWatchdogLifecycle, RepeatedStartDoesNotResetClaimOrReplaceSnapshot)
{
	auto pState = std::make_shared<CState>();
	ASSERT_TRUE(WaitForAsyncIo([pState] {
		ASSERT_TRUE(pState->Start(InitialSnapshot()));
		EXPECT_TRUE(pState->m_Report.WaitForCalls(1));
		auto Changed = InitialSnapshot();
		str_copy(Changed.m_aCurrentMap, "later-map");
		EXPECT_TRUE(pState->Start(Changed, FailCreation));
		EXPECT_STREQ(pState->m_Store.Read().m_aCurrentMap, "initial-map");
		pState->m_Watchdog.Stop();
		EXPECT_EQ(pState->m_Report.m_Calls, 1);
	}));
}

TEST(QmHangWatchdogLifecycle, StopBeforeTimeoutPreventsLateReport)
{
	auto pState = std::make_shared<CState>();
	ASSERT_TRUE(WaitForAsyncIo([pState] {
		auto Initial = InitialSnapshot();
		Initial.m_LastHeartbeat = time_get_nanoseconds().count();
		ASSERT_TRUE(pState->m_Watchdog.Start(Initial, std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::hours(1)).count(), CReportState::Report, &pState->m_Report, std::chrono::milliseconds(1)));
		pState->m_Watchdog.Stop();
		EXPECT_FALSE(pState->m_Watchdog.Started());
		EXPECT_FALSE(pState->m_Store.TryClaimReport(Initial.m_LastHeartbeat + 10, 1).has_value());
		EXPECT_EQ(pState->m_Report.m_Calls, 0);
	}));
}

TEST(QmHangWatchdogLifecycle, RestartAfterStopCreatesAnIndependentReportClaim)
{
	auto pState = std::make_shared<CState>();
	ASSERT_TRUE(WaitForAsyncIo([pState] {
		ASSERT_TRUE(pState->Start(InitialSnapshot()));
		EXPECT_TRUE(pState->m_Report.WaitForCalls(1));
		pState->m_Watchdog.Stop();
		auto Next = InitialSnapshot();
		str_copy(Next.m_aCurrentMap, "next-map");
		ASSERT_TRUE(pState->Start(Next));
		EXPECT_TRUE(pState->m_Report.WaitForCalls(2));
		pState->m_Watchdog.Stop();
		EXPECT_EQ(pState->m_Report.m_Calls, 2);
		EXPECT_STREQ(pState->m_Report.m_Last.m_aCurrentMap, "next-map");
	}));
}

TEST(QmHangWatchdogLifecycle, StopWaitsForInflightReportBeforeReturning)
{
	auto pState = std::make_shared<CState>();
	pState->m_Report.m_Block = true;
	ASSERT_TRUE(WaitForAsyncIo([pState] {
		ASSERT_TRUE(pState->Start(InitialSnapshot()));
		EXPECT_TRUE(pState->m_Report.WaitForCalls(1));
		auto Stop = std::async(std::launch::async, [pState] {
			{
				const std::lock_guard<std::mutex> Lock(pState->m_Report.m_Mutex);
				pState->m_Report.m_StopStarted = true;
				pState->m_Report.m_Cv.notify_all();
			}
			pState->m_Watchdog.Stop();
		});
		{
			std::unique_lock<std::mutex> Lock(pState->m_Report.m_Mutex);
			EXPECT_TRUE(pState->m_Report.m_Cv.wait_for(Lock, std::chrono::seconds(1), [&] { return pState->m_Report.m_StopStarted; }));
		}
		EXPECT_EQ(Stop.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
		{
			const std::lock_guard<std::mutex> Lock(pState->m_Report.m_Mutex);
			pState->m_Report.m_Release = true;
		}
		pState->m_Report.m_Cv.notify_all();
		EXPECT_EQ(Stop.wait_for(std::chrono::seconds(1)), std::future_status::ready);
		Stop.get();
		EXPECT_FALSE(pState->m_Report.m_TimedOut);
		EXPECT_FALSE(pState->m_Watchdog.Started());
	}));
}

TEST(QmHangWatchdogLifecycle, DestructionStopsWorkerBeforeItsSnapshotStoreIsDestroyed)
{
	auto pReport = std::make_shared<CReportState>();
	ASSERT_TRUE(WaitForAsyncIo([pReport] {
		CSnapshotStore Store;
		{
			CWatchdog Watchdog(Store);
			ASSERT_TRUE(Watchdog.Start(InitialSnapshot(), 1, CReportState::Report, pReport.get(), std::chrono::milliseconds(1)));
			EXPECT_TRUE(pReport->WaitForCalls(1));
		}
		EXPECT_FALSE(Store.TryClaimReport(time_get_nanoseconds().count(), 1).has_value());
		EXPECT_EQ(pReport->m_Calls, 1);
	}));
}
