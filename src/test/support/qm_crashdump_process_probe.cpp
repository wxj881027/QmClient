#include "qm_crashdump_process_probe.h"

#include <base/crashdump.h>
#include <base/dbg.h>
#include <base/logger.h>
#include <base/system.h>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <new>

#if defined(CONF_FAMILY_WINDOWS) && defined(CONF_CRASHDUMP)
#include <windows.h>

namespace
{
	void AssertMarker(const char *pText)
	{
		std::fputs(pText, stderr);
		std::fputc('\n', stderr);
		std::fflush(stderr);
	}

	struct SRejectAssertCopy
	{
		std::shared_ptr<bool> m_pReject;
		explicit SRejectAssertCopy(std::shared_ptr<bool> pReject) :
			m_pReject(std::move(pReject)) {}
		SRejectAssertCopy(const SRejectAssertCopy &Other) :
			m_pReject(Other.m_pReject)
		{
			if(*m_pReject)
				throw std::bad_alloc();
		}
		void operator()(const char *) const
		{
			if(dbg_assert_has_failed())
				AssertMarker("assert-copy-target-invoked");
		}
	};

	class CAssertFinishFailureLogger : public ILogger
	{
		void Log(const CLogMessage *pMessage) override
		{
			std::puts(pMessage->m_aLine);
			std::fflush(stdout);
		}
		void GlobalFinish() override
		{
			AssertMarker("assert-finish-entered");
			throw std::bad_alloc();
		}
	};

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
	const bool AssertCopy = str_comp(ppArgv[2], "assert-copy-failure") == 0;
	const bool AssertThrow = str_comp(ppArgv[2], "assert-handler-throws") == 0;
	const bool AssertReenter = str_comp(ppArgv[2], "assert-reenter") == 0;
	const bool AssertFinish = str_comp(ppArgv[2], "assert-finish-throws") == 0;
	const bool AssertAtExit = str_comp(ppArgv[2], "assert-at-exit") == 0;
	if(!Recovered && !RecoveredThenTerminate && !Terminate && !Unhandled && !AssertCopy && !AssertThrow && !AssertReenter && !AssertFinish && !AssertAtExit)
		return 2;
	// 只作用于此独立探测进程，不启动报告窗口或系统错误弹窗。
	SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
	if(!SetEnvironmentVariableW(L"QMCLIENT_TEST_HIDE_DIALOG", L"1"))
		return 4;
	if(AssertFinish)
		log_set_global_logger(new CAssertFinishFailureLogger);
	else
		log_set_global_logger_default();
	crashdump_init_if_available(ppArgv[3]);
	if(AssertAtExit)
	{
		// 先登记退出断言再首次创建槽位：槽位不能先于本回调析构。
		if(std::atexit([] { dbg_assert_failed("probe assertion during static cleanup"); }) != 0)
			return 8;
		dbg_assert_set_handler([](const char *) { AssertMarker("assert-exit-handler-invoked"); });
		return 0;
	}
	if(AssertCopy)
	{
		auto pReject = std::make_shared<bool>(false);
		dbg_assert_set_handler(SRejectAssertCopy(pReject));
		*pReject = true;
		dbg_assert_failed("probe assertion with allocation-failing callback copy");
	}
	if(AssertThrow)
	{
		dbg_assert_set_handler([](const char *) {
			AssertMarker("assert-throwing-handler-invoked");
			throw 7;
		});
		dbg_assert_failed("probe assertion with throwing callback");
	}
	if(AssertReenter)
	{
		dbg_assert_set_handler([](const char *) {
			dbg_assert_set_handler(nullptr);
			AssertMarker("assert-reentrant-handler-cleared");
			dbg_assert_failed("recursive-diagnostic-must-not-run");
		});
		dbg_assert_failed("probe assertion with recursive callback");
	}
	if(AssertFinish)
		dbg_assert_failed("probe assertion with throwing logger finish");
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
