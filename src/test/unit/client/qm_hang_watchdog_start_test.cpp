#include <engine/client/qm_hang_diagnostics.h>

#include <gtest/gtest.h>

#include <new>
#include <system_error>

namespace
{
	void IgnoreReport(void *, int64_t, const QmHangDiagnostics::SSnapshot &) {}

	std::thread FailAllocation(std::function<void()>) { throw std::bad_alloc(); }
	std::thread FailCreation(std::function<void()>) { throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again)); }
	std::thread FailUnknown(std::function<void()>) { throw 1; }
	std::thread EmptyThread(std::function<void()>) { return {}; }

	void ExpectInactive(QmHangDiagnostics::CWatchdog::FCreateThread pfnCreate)
	{
		QmHangDiagnostics::CSnapshotStore Store;
		QmHangDiagnostics::CWatchdog Watchdog(Store);
		QmHangDiagnostics::SSnapshot Initial;
		Initial.m_LastHeartbeat = 10;
		EXPECT_FALSE(Watchdog.Start(Initial, 10, IgnoreReport, nullptr, std::chrono::milliseconds(1), pfnCreate));
		EXPECT_FALSE(Watchdog.Started());
		EXPECT_FALSE(Store.TryClaimReport(100, 10).has_value());
		Watchdog.Stop();
		Watchdog.Stop();
	}
}

TEST(QmHangWatchdogStart, AllocationFailureRollsBackActiveReportState)
{
	ExpectInactive(FailAllocation);
}

TEST(QmHangWatchdogStart, NativeThreadCreationFailureRollsBackActiveReportState)
{
	ExpectInactive(FailCreation);
}

TEST(QmHangWatchdogStart, UnknownCreationFailureIsContained)
{
	ExpectInactive(FailUnknown);
}

TEST(QmHangWatchdogStart, EmptyCreatedThreadIsNotReportedAsStarted)
{
	ExpectInactive(EmptyThread);
}
