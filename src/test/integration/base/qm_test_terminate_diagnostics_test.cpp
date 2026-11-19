#include <base/detect.h>

#if defined(CONF_PLATFORM_MACOS)
#include <gtest/gtest.h>
#include <test/support/qm_test_terminate_diagnostics.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#include <exception>
#include <new>
#include <thread>

namespace
{
	void ThrowAllocationFailure()
	{
		throw std::bad_alloc();
	}

	void FailInsideNoexceptBoundary() noexcept
	{
		ThrowAllocationFailure();
	}
}

TEST(MacTestTerminateDeathTest, NativeStackKeepsDefaultAbortStatus)
{
	ASSERT_EXIT(
		{
			alarm(5);
			QmInstallMacTestTerminateDiagnostics();
			std::terminate();
		},
		::testing::KilledBySignal(SIGABRT), "QmClient test terminate stack:(.|\n)*0x");
}

TEST(MacTestTerminateDeathTest, PreviousHandlerRunsAfterStackOutput)
{
	ASSERT_EXIT(
		{
			alarm(5);
			std::set_terminate([] { std::_Exit(73); });
			QmInstallMacTestTerminateDiagnostics();
			std::terminate();
		},
		::testing::ExitedWithCode(73), "QmClient test terminate stack:(.|\n)*0x");
}

TEST(MacTestTerminateDeathTest, RepeatedInstallationKeepsThePreviousHandler)
{
	ASSERT_EXIT(
		{
			alarm(5);
			std::set_terminate([] { std::_Exit(73); });
			QmInstallMacTestTerminateDiagnostics();
			QmInstallMacTestTerminateDiagnostics();
			std::terminate();
		},
		::testing::ExitedWithCode(73), "QmClient test terminate stack:(.|\n)*0x");
}

TEST(MacTestTerminateDeathTest, ReentrantPreviousHandlerAbortsWithoutLooping)
{
	ASSERT_EXIT(
		{
			alarm(5);
			std::set_terminate([] { std::terminate(); });
			QmInstallMacTestTerminateDiagnostics();
			std::terminate();
		},
		::testing::KilledBySignal(SIGABRT), "QmClient test terminate stack:(.|\n)*0x");
}

TEST(MacTestTerminateDeathTest, ActiveExceptionRetainsTheOriginalDiagnostic)
{
	ASSERT_EXIT(
		{
			alarm(5);
			FailInsideNoexceptBoundary();
		},
		::testing::KilledBySignal(SIGABRT), "QmClient test terminate stack:(.|\n)*std::bad_alloc");
}

TEST(MacTestTerminateDeathTest, WorkerExceptionProducesTheTerminatingThreadStack)
{
	ASSERT_EXIT(
		{
			alarm(5);
			std::thread Worker([] { FailInsideNoexceptBoundary(); });
			Worker.join();
		},
		::testing::KilledBySignal(SIGABRT), "QmClient test terminate stack:(.|\n)*std::bad_alloc");
}

TEST(MacTestTerminateDeathTest, LoadedRuntimeMetadataStillDelegatesToThePreviousHandler)
{
	ASSERT_EXIT(
		{
			alarm(5);
			std::set_terminate([] { std::_Exit(73); });
			QmInstallMacTestTerminateDiagnostics();
			std::terminate();
		},
		::testing::ExitedWithCode(73), "QmClient test unwind provider: __cxa_throw(.|\n)*QmClient test unwind frame: 0(.|\n)*encoding=0x[0-9a-f]+ lsda=0x[0-9a-f]+");
}

TEST(MacTestTerminateDeathTest, ThrowingThreadReportsUnwindMetadataBeforeTheOriginalException)
{
	ASSERT_EXIT(
		{
			alarm(5);
			FailInsideNoexceptBoundary();
		},
		::testing::KilledBySignal(SIGABRT), "QmClient test unwind frame:(.|\n)*handler=0x[0-9a-f]+(.|\n)*std::bad_alloc");
}
#endif
