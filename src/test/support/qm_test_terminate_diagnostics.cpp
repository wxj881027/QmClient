#include "qm_test_terminate_diagnostics.h"

#include <base/detect.h>

#if defined(CONF_PLATFORM_MACOS)
#include <dlfcn.h>
#include <execinfo.h>
#include <libunwind.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

namespace
{
	std::atomic<std::terminate_handler> gs_pPreviousTerminateHandler{nullptr};

	template<size_t N>
	void WriteDiagnosticLine(const char (&aLine)[N], int Length) noexcept
	{
		if(Length > 0)
			(void)write(STDERR_FILENO, aLine, std::min(static_cast<size_t>(Length), N - 1));
	}

	const char *ImageName(const Dl_info &Info) noexcept
	{
		if(Info.dli_fname == nullptr)
			return "unknown";
		const char *pSlash = std::strrchr(Info.dli_fname, '/');
		return pSlash != nullptr ? pSlash + 1 : Info.dli_fname;
	}

	void TraceRuntimeSymbol(const char *pName) noexcept
	{
		void *pSymbol = dlsym(RTLD_DEFAULT, pName);
		Dl_info Image{};
		if(pSymbol != nullptr)
			(void)dladdr(pSymbol, &Image);
		char aLine[512];
		WriteDiagnosticLine(aLine, std::snprintf(aLine, sizeof(aLine), "QmClient test unwind provider: %s image=%.80s path=%.240s address=%p base=%p\n",
						   pName, ImageName(Image), Image.dli_fname != nullptr ? Image.dli_fname : "unknown", pSymbol, Image.dli_fbase));
	}

	void TraceTestUnwindInfo() noexcept
	{
		// 只查询当前进程已加载的展开接口，不加载或替换新的实现。
		const auto pGetContext = reinterpret_cast<decltype(&unw_getcontext)>(dlsym(RTLD_DEFAULT, "unw_getcontext"));
		const auto pInitLocal = reinterpret_cast<decltype(&unw_init_local)>(dlsym(RTLD_DEFAULT, "unw_init_local"));
		const auto pGetReg = reinterpret_cast<decltype(&unw_get_reg)>(dlsym(RTLD_DEFAULT, "unw_get_reg"));
		const auto pGetInfo = reinterpret_cast<decltype(&unw_get_proc_info)>(dlsym(RTLD_DEFAULT, "unw_get_proc_info"));
		const auto pStep = reinterpret_cast<decltype(&unw_step)>(dlsym(RTLD_DEFAULT, "unw_step"));
		char aLine[512];
		if(pGetContext == nullptr || pInitLocal == nullptr || pGetReg == nullptr || pGetInfo == nullptr || pStep == nullptr)
		{
			constexpr char aUnavailable[] = "QmClient test unwind info unavailable: loaded API missing\n";
			(void)write(STDERR_FILENO, aUnavailable, sizeof(aUnavailable) - 1);
			return;
		}

		TraceRuntimeSymbol("_Unwind_RaiseException");
		TraceRuntimeSymbol("__gxx_personality_v0");
		TraceRuntimeSymbol("__cxa_throw");
		// 同名库可能来自不同目录；同时记录取上下文和步进等接口的实际提供者。
		constexpr const char *apNativeSymbols[] = {"unw_getcontext", "unw_init_local", "unw_get_reg", "unw_get_proc_info", "unw_step"};
		for(const char *pName : apNativeSymbols)
			TraceRuntimeSymbol(pName);
		unw_context_t Context;
		unw_cursor_t Cursor;
		const int ContextResult = pGetContext(&Context);
		const int InitResult = ContextResult == 0 ? pInitLocal(&Cursor, &Context) : ContextResult;
		if(InitResult != 0)
		{
			WriteDiagnosticLine(aLine, std::snprintf(aLine, sizeof(aLine), "QmClient test unwind info unavailable: init=%d\n", InitResult));
			return;
		}
		constexpr int MaxFrames = 16;
		for(int Frame = 0; Frame < MaxFrames; ++Frame)
		{
			unw_word_t Ip = 0;
			const int RegisterResult = pGetReg(&Cursor, UNW_REG_IP, &Ip);
			if(RegisterResult != 0 || Ip == 0)
			{
				WriteDiagnosticLine(aLine, std::snprintf(aLine, sizeof(aLine), "QmClient test unwind end: register=%d ip=0x%llx frames=%d\n", RegisterResult, static_cast<unsigned long long>(Ip), Frame));
				return;
			}
			unw_proc_info_t Info{};
			const int Result = pGetInfo(&Cursor, &Info);
			Dl_info Image{};
			(void)dladdr(reinterpret_cast<const void *>(Ip), &Image);
			const auto ImageBase = reinterpret_cast<uintptr_t>(Image.dli_fbase);
			const auto ImageOffset = ImageBase != 0 && Ip >= ImageBase ? Ip - ImageBase : 0;
			if(Result == 0)
			{
				WriteDiagnosticLine(aLine, std::snprintf(aLine, sizeof(aLine),
								   "QmClient test unwind frame: %d image=%.80s ip=0x%llx image_offset=0x%llx status=%d start=0x%llx end=0x%llx encoding=0x%08x lsda=0x%llx handler=0x%llx dwarf=0x%llx\n",
								   Frame, ImageName(Image), static_cast<unsigned long long>(Ip), static_cast<unsigned long long>(ImageOffset), Result,
								   static_cast<unsigned long long>(Info.start_ip), static_cast<unsigned long long>(Info.end_ip), static_cast<unsigned>(Info.format),
								   static_cast<unsigned long long>(Info.lsda), static_cast<unsigned long long>(Info.handler), static_cast<unsigned long long>(Info.unwind_info)));
			}
			else
			{
				// 查询失败的清零字段不能被误读成真实的零编码展开项。
				WriteDiagnosticLine(aLine, std::snprintf(aLine, sizeof(aLine),
								   "QmClient test unwind frame: %d image=%.80s ip=0x%llx image_offset=0x%llx status=%d info=unavailable\n",
								   Frame, ImageName(Image), static_cast<unsigned long long>(Ip), static_cast<unsigned long long>(ImageOffset), Result));
			}
			const int StepResult = pStep(&Cursor);
			if(StepResult <= 0)
			{
				WriteDiagnosticLine(aLine, std::snprintf(aLine, sizeof(aLine), "QmClient test unwind end: step=%d frames=%d\n", StepResult, Frame + 1));
				return;
			}
		}
		constexpr char aTruncated[] = "QmClient test unwind truncated: frames=16\n";
		(void)write(STDERR_FILENO, aTruncated, sizeof(aTruncated) - 1);
	}

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
			TraceTestUnwindInfo();
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
