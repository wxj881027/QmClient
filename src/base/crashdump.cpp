#include "detect.h"

#if defined(CONF_CRASHDUMP)
#if !defined(CONF_FAMILY_WINDOWS)
#error crash dumping not implemented
#else

#include "log.h"
#include "system.h"

#include <atomic>
#include <csignal>
#include <exception>

#include <windows.h>
#include <dbghelp.h>

static const char *CRASHDUMP_LIB = "exchndl.dll";
static const char *CRASHDUMP_FN = "ExcHndlSetLogFileNameW";

using MiniDumpWriteDumpFunc = BOOL(WINAPI *)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
	const MINIDUMP_EXCEPTION_INFORMATION *, const MINIDUMP_USER_STREAM_INFORMATION *, const MINIDUMP_CALLBACK_INFORMATION *);

static char gs_aCrashLogPath[IO_MAX_PATH_LENGTH] = "";
static char gs_aFallbackReportPath[IO_MAX_PATH_LENGTH] = "";
static char gs_aFallbackDumpPath[IO_MAX_PATH_LENGTH] = "";

static HMODULE gs_pDbgHelpLib = nullptr;
static MiniDumpWriteDumpFunc gs_pMiniDumpWriteDump = nullptr;

static std::atomic<bool> gs_FallbackHandlersInstalled{false};
static std::atomic_flag gs_FatalReportInProgress = ATOMIC_FLAG_INIT;

static PVOID gs_pVectoredExceptionHandler = nullptr;
static LPTOP_LEVEL_EXCEPTION_FILTER gs_pPreviousUnhandledExceptionFilter = nullptr;

static const char *ExceptionCodeToString(DWORD ExceptionCode)
{
	switch(ExceptionCode)
	{
	case EXCEPTION_ACCESS_VIOLATION:
		return "EXCEPTION_ACCESS_VIOLATION";
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
	case EXCEPTION_BREAKPOINT:
		return "EXCEPTION_BREAKPOINT";
	case EXCEPTION_DATATYPE_MISALIGNMENT:
		return "EXCEPTION_DATATYPE_MISALIGNMENT";
	case EXCEPTION_FLT_DENORMAL_OPERAND:
		return "EXCEPTION_FLT_DENORMAL_OPERAND";
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
		return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
	case EXCEPTION_FLT_INEXACT_RESULT:
		return "EXCEPTION_FLT_INEXACT_RESULT";
	case EXCEPTION_FLT_INVALID_OPERATION:
		return "EXCEPTION_FLT_INVALID_OPERATION";
	case EXCEPTION_FLT_OVERFLOW:
		return "EXCEPTION_FLT_OVERFLOW";
	case EXCEPTION_FLT_STACK_CHECK:
		return "EXCEPTION_FLT_STACK_CHECK";
	case EXCEPTION_FLT_UNDERFLOW:
		return "EXCEPTION_FLT_UNDERFLOW";
	case EXCEPTION_ILLEGAL_INSTRUCTION:
		return "EXCEPTION_ILLEGAL_INSTRUCTION";
	case EXCEPTION_IN_PAGE_ERROR:
		return "EXCEPTION_IN_PAGE_ERROR";
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
		return "EXCEPTION_INT_DIVIDE_BY_ZERO";
	case EXCEPTION_INT_OVERFLOW:
		return "EXCEPTION_INT_OVERFLOW";
	case EXCEPTION_INVALID_DISPOSITION:
		return "EXCEPTION_INVALID_DISPOSITION";
	case EXCEPTION_NONCONTINUABLE_EXCEPTION:
		return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
	case EXCEPTION_PRIV_INSTRUCTION:
		return "EXCEPTION_PRIV_INSTRUCTION";
	case EXCEPTION_SINGLE_STEP:
		return "EXCEPTION_SINGLE_STEP";
	case EXCEPTION_STACK_OVERFLOW:
		return "EXCEPTION_STACK_OVERFLOW";
	default:
		return "UNKNOWN_EXCEPTION";
	}
}

static const char *SignalToString(int SignalNumber)
{
	switch(SignalNumber)
	{
	case SIGABRT:
		return "SIGABRT";
	case SIGFPE:
		return "SIGFPE";
	case SIGILL:
		return "SIGILL";
	case SIGSEGV:
		return "SIGSEGV";
	default:
		return "UNKNOWN_SIGNAL";
	}
}

static bool IsLikelyFatalException(DWORD ExceptionCode)
{
	switch(ExceptionCode)
	{
	case EXCEPTION_ACCESS_VIOLATION:
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
	case EXCEPTION_DATATYPE_MISALIGNMENT:
	case EXCEPTION_FLT_DENORMAL_OPERAND:
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
	case EXCEPTION_FLT_INVALID_OPERATION:
	case EXCEPTION_FLT_OVERFLOW:
	case EXCEPTION_FLT_UNDERFLOW:
	case EXCEPTION_ILLEGAL_INSTRUCTION:
	case EXCEPTION_IN_PAGE_ERROR:
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
	case EXCEPTION_INT_OVERFLOW:
	case EXCEPTION_INVALID_DISPOSITION:
	case EXCEPTION_NONCONTINUABLE_EXCEPTION:
	case EXCEPTION_PRIV_INSTRUCTION:
	case EXCEPTION_STACK_OVERFLOW:
		return true;
	default:
		return false;
	}
}

static void BuildSidecarPath(const char *pBasePath, const char *pSuffix, char *pOutPath, size_t OutPathSize)
{
	str_copy(pOutPath, pBasePath, OutPathSize);
	char *pLastSlash = strrchr(pOutPath, '/');
	char *pLastBackslash = strrchr(pOutPath, '\\');
	char *pLastSeparator = pLastSlash;
	if(pLastBackslash != nullptr && (pLastSeparator == nullptr || pLastBackslash > pLastSeparator))
		pLastSeparator = pLastBackslash;

	char *pExtension = strrchr(pOutPath, '.');
	if(pExtension != nullptr && (pLastSeparator == nullptr || pExtension > pLastSeparator))
		*pExtension = '\0';

	str_append(pOutPath, pSuffix, OutPathSize);
}

static void WriteRaw(HANDLE FileHandle, const char *pText)
{
	if(pText == nullptr || pText[0] == '\0')
		return;
	const DWORD TotalBytes = (DWORD)str_length(pText);
	DWORD TotalWritten = 0;
	while(TotalWritten < TotalBytes)
	{
		DWORD Written = 0;
		if(!WriteFile(FileHandle, pText + TotalWritten, TotalBytes - TotalWritten, &Written, nullptr) || Written == 0)
			break;
		TotalWritten += Written;
	}
}

static void WriteMinimalCrashReport(const char *pReason, EXCEPTION_POINTERS *pExceptionPointers, int SignalNumber, bool DumpWritten)
{
	if(gs_aFallbackReportPath[0] == '\0')
		return;

	HANDLE FileHandle = CreateFileA(gs_aFallbackReportPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(FileHandle == INVALID_HANDLE_VALUE)
		return;

	char aLine[512];
	WriteRaw(FileHandle, "QmClient fatal error report\r\n");
	WriteRaw(FileHandle, "Report type: fatal-crash\r\n");
	str_format(aLine, sizeof(aLine), "Reason: %s\r\n", pReason != nullptr ? pReason : "unknown");
	WriteRaw(FileHandle, aLine);
	str_format(aLine, sizeof(aLine), "Process ID: %lu\r\n", (unsigned long)GetCurrentProcessId());
	WriteRaw(FileHandle, aLine);
	str_format(aLine, sizeof(aLine), "Thread ID: %lu\r\n", (unsigned long)GetCurrentThreadId());
	WriteRaw(FileHandle, aLine);
	str_format(aLine, sizeof(aLine), "Crash log path (.RTP): %s\r\n", gs_aCrashLogPath);
	WriteRaw(FileHandle, aLine);
	str_format(aLine, sizeof(aLine), "Fallback minidump path: %s\r\n", gs_aFallbackDumpPath);
	WriteRaw(FileHandle, aLine);
	str_format(aLine, sizeof(aLine), "Fallback minidump written: %s\r\n", DumpWritten ? "yes" : "no");
	WriteRaw(FileHandle, aLine);

	if(pExceptionPointers != nullptr && pExceptionPointers->ExceptionRecord != nullptr)
	{
		const DWORD ExceptionCode = pExceptionPointers->ExceptionRecord->ExceptionCode;
		str_format(aLine, sizeof(aLine), "Exception code: 0x%08lX (%s)\r\n", (unsigned long)ExceptionCode, ExceptionCodeToString(ExceptionCode));
		WriteRaw(FileHandle, aLine);
		str_format(aLine, sizeof(aLine), "Exception address: 0x%p\r\n", pExceptionPointers->ExceptionRecord->ExceptionAddress);
		WriteRaw(FileHandle, aLine);
	}

	if(SignalNumber != 0)
	{
		str_format(aLine, sizeof(aLine), "Signal: %d (%s)\r\n", SignalNumber, SignalToString(SignalNumber));
		WriteRaw(FileHandle, aLine);
	}

	FlushFileBuffers(FileHandle);
	CloseHandle(FileHandle);
}

static bool WriteMiniDumpFile(const char *pFilename, EXCEPTION_POINTERS *pExceptionPointers)
{
	if(gs_pMiniDumpWriteDump == nullptr || pFilename == nullptr || pFilename[0] == '\0')
		return false;

	HANDLE FileHandle = CreateFileA(pFilename, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(FileHandle == INVALID_HANDLE_VALUE)
		return false;

	const MINIDUMP_TYPE DumpType = static_cast<MINIDUMP_TYPE>(
		MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithIndirectlyReferencedMemory);

	MINIDUMP_EXCEPTION_INFORMATION ExceptionInfo{};
	PMINIDUMP_EXCEPTION_INFORMATION pExceptionInfo = nullptr;
	if(pExceptionPointers != nullptr)
	{
		ExceptionInfo.ThreadId = GetCurrentThreadId();
		ExceptionInfo.ExceptionPointers = pExceptionPointers;
		ExceptionInfo.ClientPointers = FALSE;
		pExceptionInfo = &ExceptionInfo;
	}

	const BOOL Result = gs_pMiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), FileHandle, DumpType, pExceptionInfo, nullptr, nullptr);
	CloseHandle(FileHandle);
	return Result != FALSE;
}

static void HandleFatalCrash(const char *pReason, EXCEPTION_POINTERS *pExceptionPointers, int SignalNumber)
{
	if(gs_FatalReportInProgress.test_and_set(std::memory_order_acq_rel))
		return;

	const bool DumpWritten = WriteMiniDumpFile(gs_aFallbackDumpPath, pExceptionPointers);
	WriteMinimalCrashReport(pReason, pExceptionPointers, SignalNumber, DumpWritten);
}

static LONG WINAPI FallbackVectoredExceptionHandler(PEXCEPTION_POINTERS pExceptionPointers)
{
	if(pExceptionPointers == nullptr || pExceptionPointers->ExceptionRecord == nullptr)
		return EXCEPTION_CONTINUE_SEARCH;

	// Keep debugger behavior predictable and avoid reports for normal unwind flow.
	if(IsDebuggerPresent())
		return EXCEPTION_CONTINUE_SEARCH;
	if((pExceptionPointers->ExceptionRecord->ExceptionFlags & EXCEPTION_UNWINDING) != 0)
		return EXCEPTION_CONTINUE_SEARCH;
	if(!IsLikelyFatalException(pExceptionPointers->ExceptionRecord->ExceptionCode))
		return EXCEPTION_CONTINUE_SEARCH;

	HandleFatalCrash("Vectored exception fallback", pExceptionPointers, 0);
	return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI FallbackUnhandledExceptionFilter(EXCEPTION_POINTERS *pExceptionPointers)
{
	HandleFatalCrash("Unhandled structured exception", pExceptionPointers, 0);
	if(gs_pPreviousUnhandledExceptionFilter != nullptr && gs_pPreviousUnhandledExceptionFilter != FallbackUnhandledExceptionFilter)
	{
		const LONG PreviousFilterResult = gs_pPreviousUnhandledExceptionFilter(pExceptionPointers);
		if(PreviousFilterResult != EXCEPTION_CONTINUE_SEARCH)
			return PreviousFilterResult;
	}
	return EXCEPTION_EXECUTE_HANDLER;
}

static void TerminateFromFatalHandler()
{
	TerminateProcess(GetCurrentProcess(), 3);
	ExitProcess(3);
}

static void FallbackTerminateHandler()
{
	HandleFatalCrash("Unhandled C++ exception (std::terminate)", nullptr, 0);
	TerminateFromFatalHandler();
}

static void FallbackSignalHandler(int SignalNumber)
{
	// Signal handlers must stay minimal and avoid complex I/O or allocations.
	// Re-raise with the default handler to preserve normal crash semantics.
	std::signal(SignalNumber, SIG_DFL);
	std::raise(SignalNumber);
	TerminateFromFatalHandler();
}

static void InitMiniDumpWriterIfNeeded()
{
	if(gs_pMiniDumpWriteDump != nullptr || gs_pDbgHelpLib != nullptr)
		return;

	gs_pDbgHelpLib = LoadLibraryA("dbghelp.dll");
	if(gs_pDbgHelpLib == nullptr)
	{
		const DWORD LastError = GetLastError();
		log_warn("crashdump", "failed to load dbghelp.dll (error %ld %s), fallback report will be generated without fallback minidump", LastError, windows_format_system_message(LastError).c_str());
		return;
	}

	// Intentional
#ifdef __MINGW32__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
	gs_pMiniDumpWriteDump = (MiniDumpWriteDumpFunc)GetProcAddress(gs_pDbgHelpLib, "MiniDumpWriteDump");
#ifdef __MINGW32__
#pragma GCC diagnostic pop
#endif
	if(gs_pMiniDumpWriteDump == nullptr)
	{
		const DWORD LastError = GetLastError();
		log_warn("crashdump", "could not find MiniDumpWriteDump in dbghelp.dll (error %ld %s), fallback report will be generated without fallback minidump", LastError, windows_format_system_message(LastError).c_str());
	}
}

static void InstallFallbackHandlersIfNeeded()
{
	bool Expected = false;
	if(!gs_FallbackHandlersInstalled.compare_exchange_strong(Expected, true, std::memory_order_acq_rel))
		return;

	gs_pVectoredExceptionHandler = AddVectoredExceptionHandler(1, FallbackVectoredExceptionHandler);
	if(gs_pVectoredExceptionHandler == nullptr)
	{
		const DWORD LastError = GetLastError();
		log_warn("crashdump", "failed to install vectored exception handler (error %ld %s)", LastError, windows_format_system_message(LastError).c_str());
	}

	std::set_terminate(FallbackTerminateHandler);
	std::signal(SIGABRT, FallbackSignalHandler);
}

static void InstallUnhandledExceptionFilter()
{
	const LPTOP_LEVEL_EXCEPTION_FILTER pPrevious = SetUnhandledExceptionFilter(FallbackUnhandledExceptionFilter);
	if(pPrevious != FallbackUnhandledExceptionFilter)
		gs_pPreviousUnhandledExceptionFilter = pPrevious;
}

void crashdump_init_if_available(const char *log_file_path)
{
	str_copy(gs_aCrashLogPath, log_file_path, sizeof(gs_aCrashLogPath));
	BuildSidecarPath(gs_aCrashLogPath, "_fatal_report.txt", gs_aFallbackReportPath, sizeof(gs_aFallbackReportPath));
	BuildSidecarPath(gs_aCrashLogPath, "_fatal_dump.dmp", gs_aFallbackDumpPath, sizeof(gs_aFallbackDumpPath));

	if(gs_aCrashLogPath[0] != '\0')
		fs_makedir_rec_for(gs_aCrashLogPath);
	if(gs_aFallbackReportPath[0] != '\0')
		fs_makedir_rec_for(gs_aFallbackReportPath);
	if(gs_aFallbackDumpPath[0] != '\0')
		fs_makedir_rec_for(gs_aFallbackDumpPath);

	InitMiniDumpWriterIfNeeded();
	InstallFallbackHandlersIfNeeded();

	HMODULE pCrashdumpLib = LoadLibraryA(CRASHDUMP_LIB);
	if(pCrashdumpLib == nullptr)
	{
		const DWORD LastError = GetLastError();
		const std::string ErrorMsg = windows_format_system_message(LastError);
		log_warn("crashdump", "failed to load crashdump library '%s' (error %ld %s), using fallback crash handlers only", CRASHDUMP_LIB, LastError, ErrorMsg.c_str());
		InstallUnhandledExceptionFilter();
		return;
	}

	const std::wstring WideLogFilePath = windows_utf8_to_wide(log_file_path);
	// Intentional
#ifdef __MINGW32__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
	auto ExceptionLogFilePathFunc = (BOOL(APIENTRY *)(const WCHAR *))(GetProcAddress(pCrashdumpLib, CRASHDUMP_FN));
#ifdef __MINGW32__
#pragma GCC diagnostic pop
#endif
	if(ExceptionLogFilePathFunc == nullptr)
	{
		const DWORD LastError = GetLastError();
		const std::string ErrorMsg = windows_format_system_message(LastError);
		log_warn("exception_handling", "could not find function '%s' in exception handling library (error %ld %s), using fallback crash handlers only", CRASHDUMP_FN, LastError, ErrorMsg.c_str());
		InstallUnhandledExceptionFilter();
		return;
	}

	ExceptionLogFilePathFunc(WideLogFilePath.c_str());

	// Install our unhandled filter after external crashdump setup so fallback
	// remains active even if that library changes the top-level filter.
	InstallUnhandledExceptionFilter();
}
#endif
#else
void crashdump_init_if_available(const char *log_file_path)
{
	(void)log_file_path;
}
#endif
