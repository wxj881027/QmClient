#include "qm_crashdump_process_probe.h"

#include <base/crashdump.h>
#include <base/system.h>

#include <cstdio>
#include <exception>

#if defined(CONF_FAMILY_WINDOWS) && defined(CONF_CRASHDUMP)
#include <windows.h>

namespace
{
	LONG WINAPI RecoverAccessViolation(EXCEPTION_POINTERS *pException)
	{
		if(pException && pException->ExceptionRecord && pException->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
			return EXCEPTION_CONTINUE_EXECUTION;
		return EXCEPTION_CONTINUE_SEARCH;
	}
}
#endif

std::optional<int> QmCrashdumpProcessProbe(int argc, const char **ppArgv)
{
	if(argc < 2 || str_comp(ppArgv[1], "--qm-test-crashdump") != 0)
		return std::nullopt;
#if defined(CONF_FAMILY_WINDOWS) && defined(CONF_CRASHDUMP)
	if(argc == 3 && str_comp(ppArgv[2], "capabilities") == 0)
	{
		std::puts("windows-crashdump=enabled");
		return 0;
	}
	if(argc != 4)
		return 2;
	const bool Recovered = str_comp(ppArgv[2], "recovered") == 0;
	const bool RecoveredThenTerminate = str_comp(ppArgv[2], "recovered-then-terminate") == 0;
	const bool Terminate = str_comp(ppArgv[2], "terminate") == 0;
	const bool Unhandled = str_comp(ppArgv[2], "unhandled") == 0;
	if(!Recovered && !RecoveredThenTerminate && !Terminate && !Unhandled)
		return 2;
	// 只作用于此独立探测进程，不启动报告窗口或系统错误弹窗。
	SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
	if(!SetEnvironmentVariableW(L"QMCLIENT_TEST_HIDE_DIALOG", L"1"))
		return 4;
	crashdump_init_if_available(ppArgv[3]);
	if(Recovered || RecoveredThenTerminate)
	{
		// 真实Windows分发：我们的恢复器排在原有VEH之后，允许异常继续执行。
		PVOID Handler = AddVectoredExceptionHandler(0, RecoverAccessViolation);
		if(!Handler)
			return 5;
		RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
		if(!RemoveVectoredExceptionHandler(Handler))
			return 6;
		if(Recovered)
			return 0;
	}
	if(Unhandled)
	{
		// 在Google Test接管SEH之前触发，实际进入生产顶层过滤器。
		RaiseException(EXCEPTION_ILLEGAL_INSTRUCTION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
		return 7;
	}
	std::terminate();
#else
	(void)argc;
	(void)ppArgv;
	return 77;
#endif
}
