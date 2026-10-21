#include "qm_test_terminate_diagnostics.h"

#include <base/detect.h>

#if defined(CONF_PLATFORM_MACOS)
#include <execinfo.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <exception>

namespace
{
	std::atomic<std::terminate_handler> gs_pPreviousTerminateHandler{nullptr};

	[[noreturn]] void TraceTestTermination() noexcept
	{
		// 原处理器或栈采集再次终止时直接结束，避免递归遮盖原始故障。
		static thread_local bool s_Reporting = false;
		if(!s_Reporting)
		{
			s_Reporting = true;
			constexpr int MaxFrames = 64;
			void *apFrames[MaxFrames];
			const int NumFrames = backtrace(apFrames, MaxFrames);
			if(NumFrames > 0)
			{
				constexpr char aHeader[] = "QmClient test terminate stack:\n";
				(void)write(STDERR_FILENO, aHeader, sizeof(aHeader) - 1);
				backtrace_symbols_fd(apFrames, NumFrames, STDERR_FILENO);
			}
			else
			{
				constexpr char aUnavailable[] = "QmClient test terminate stack unavailable\n";
				(void)write(STDERR_FILENO, aUnavailable, sizeof(aUnavailable) - 1);
			}
			const auto pPrevious = gs_pPreviousTerminateHandler.load(std::memory_order_acquire);
			if(pPrevious != nullptr && pPrevious != TraceTestTermination)
				pPrevious();
		}
		std::abort();
	}
}
#endif

void QmInstallMacTestTerminateDiagnostics()
{
#if defined(CONF_PLATFORM_MACOS)
	const auto pPrevious = std::get_terminate();
	if(pPrevious != TraceTestTermination)
	{
		gs_pPreviousTerminateHandler.store(pPrevious, std::memory_order_release);
		std::set_terminate(TraceTestTermination);
	}
#endif
}
