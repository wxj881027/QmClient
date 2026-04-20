#include "detect.h"

#if defined(CONF_CRASHDUMP)
#if !defined(CONF_FAMILY_WINDOWS)

void crashdump_init_if_available(const char *log_file_path)
{
	(void)log_file_path;
}

#else

#include "log.h"
#include "system.h"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <exception>

#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>

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

static void WriteRaw(HANDLE FileHandle, const char *pText);
static const char *ExceptionCodeToString(DWORD ExceptionCode);

static constexpr MINIDUMP_TYPE gs_ExtendedDumpType = static_cast<MINIDUMP_TYPE>(
	MiniDumpWithDataSegs |
	MiniDumpWithHandleData |
	MiniDumpWithIndirectlyReferencedMemory |
	MiniDumpWithThreadInfo |
	MiniDumpWithUnloadedModules |
	MiniDumpWithProcessThreadData |
	MiniDumpWithFullMemoryInfo |
	MiniDumpScanMemory |
	MiniDumpIgnoreInaccessibleMemory);
static constexpr MINIDUMP_TYPE gs_MinimalDumpType = static_cast<MINIDUMP_TYPE>(
	MiniDumpWithDataSegs |
	MiniDumpWithHandleData |
	MiniDumpWithIndirectlyReferencedMemory);

static DWORD gs_LastMiniDumpWrittenFlags = 0;
static DWORD gs_LastMiniDumpExtendedAttemptError = ERROR_SUCCESS;
static DWORD gs_LastMiniDumpFinalError = ERROR_SUCCESS;
static bool gs_LastMiniDumpRetriedWithMinimalFlags = false;

static void WideToUtf8(const wchar_t *pWide, char *pOut, size_t OutSize)
{
	if(pOut == nullptr || OutSize == 0)
		return;
	pOut[0] = '\0';
	if(pWide == nullptr || pWide[0] == L'\0')
		return;

	const int Written = WideCharToMultiByte(CP_UTF8, 0, pWide, -1, pOut, (int)OutSize, nullptr, nullptr);
	if(Written <= 0)
		str_copy(pOut, "(utf8-conversion-failed)", OutSize);
}

static void WriteSection(HANDLE FileHandle, const char *pTitle)
{
	WriteRaw(FileHandle, "\r\n");
	WriteRaw(FileHandle, pTitle);
	WriteRaw(FileHandle, "\r\n");
}

static bool IsReadableMemoryProtection(DWORD Protection)
{
	if((Protection & PAGE_GUARD) != 0 || (Protection & PAGE_NOACCESS) != 0)
		return false;

	switch(Protection & 0xff)
	{
	case PAGE_READONLY:
	case PAGE_READWRITE:
	case PAGE_WRITECOPY:
	case PAGE_EXECUTE_READ:
	case PAGE_EXECUTE_READWRITE:
	case PAGE_EXECUTE_WRITECOPY:
		return true;
	default:
		return false;
	}
}

static size_t ReadMemoryWindow(const void *pAddress, unsigned char *pBuffer, size_t BufferSize)
{
	if(pAddress == nullptr || pBuffer == nullptr || BufferSize == 0)
		return 0;

	MEMORY_BASIC_INFORMATION MemoryInfo{};
	if(VirtualQuery(pAddress, &MemoryInfo, sizeof(MemoryInfo)) == 0)
		return 0;
	if(MemoryInfo.State != MEM_COMMIT || !IsReadableMemoryProtection(MemoryInfo.Protect))
		return 0;

	const uintptr_t Address = reinterpret_cast<uintptr_t>(pAddress);
	const uintptr_t RegionStart = reinterpret_cast<uintptr_t>(MemoryInfo.BaseAddress);
	const uintptr_t RegionEnd = RegionStart + MemoryInfo.RegionSize;
	if(Address < RegionStart || Address >= RegionEnd)
		return 0;

	const size_t RemainingRegionBytes = (size_t)(RegionEnd - Address);
	const size_t RequestedBytes = BufferSize < RemainingRegionBytes ? BufferSize : RemainingRegionBytes;
	SIZE_T BytesRead = 0;
	if(!ReadProcessMemory(GetCurrentProcess(), pAddress, pBuffer, RequestedBytes, &BytesRead))
		return 0;
	return (size_t)BytesRead;
}

static void WriteMemoryBytes(HANDLE FileHandle, const char *pLabel, const void *pAddress, size_t BytesBefore, size_t BytesAfter)
{
	if(pAddress == nullptr)
		return;

	uintptr_t StartAddress = reinterpret_cast<uintptr_t>(pAddress);
	if(StartAddress < BytesBefore)
		BytesBefore = StartAddress;
	StartAddress -= BytesBefore;

	unsigned char aBuffer[128];
	const size_t RequestedBytes = BytesBefore + BytesAfter;
	const size_t ClampedRequestedBytes = RequestedBytes < sizeof(aBuffer) ? RequestedBytes : sizeof(aBuffer);
	const size_t BytesRead = ReadMemoryWindow(reinterpret_cast<const void *>(StartAddress), aBuffer, ClampedRequestedBytes);

	char aLine[1024];
	if(BytesRead == 0)
	{
		str_format(aLine, sizeof(aLine), "%s: unavailable\r\n", pLabel);
		WriteRaw(FileHandle, aLine);
		return;
	}

	str_format(aLine, sizeof(aLine), "%s: 0x%016llX-0x%016llX\r\n",
		pLabel,
		(unsigned long long)StartAddress,
		(unsigned long long)(StartAddress + BytesRead));
	WriteRaw(FileHandle, aLine);

	for(size_t Offset = 0; Offset < BytesRead; Offset += 16)
	{
		str_format(aLine, sizeof(aLine), "  0x%016llX :",
			(unsigned long long)(StartAddress + Offset));
		for(size_t Index = 0; Index < 16 && Offset + Index < BytesRead; ++Index)
		{
			char aByte[8];
			str_format(aByte, sizeof(aByte), " %02X", aBuffer[Offset + Index]);
			str_append(aLine, aByte, sizeof(aLine));
		}
		str_append(aLine, "\r\n", sizeof(aLine));
		WriteRaw(FileHandle, aLine);
	}
}

static void WriteExecutablePath(HANDLE FileHandle)
{
	wchar_t aWidePath[IO_MAX_PATH_LENGTH];
	const DWORD WidePathSize = sizeof(aWidePath) / sizeof(aWidePath[0]);
	DWORD Length = GetModuleFileNameW(nullptr, aWidePath, WidePathSize);
	if(Length == 0 || Length >= WidePathSize)
		return;

	char aPath[IO_MAX_PATH_LENGTH * 3] = "";
	WideToUtf8(aWidePath, aPath, sizeof(aPath));

	char aLine[1024];
	str_format(aLine, sizeof(aLine), "Executable path: %s\r\n", aPath);
	WriteRaw(FileHandle, aLine);
}

static void WriteExceptionModule(HANDLE FileHandle, const void *pAddress)
{
	if(pAddress == nullptr)
		return;

	const HANDLE SnapshotHandle = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
	if(SnapshotHandle == INVALID_HANDLE_VALUE)
		return;

	MODULEENTRY32W ModuleEntry{};
	ModuleEntry.dwSize = sizeof(ModuleEntry);

	const uintptr_t Address = reinterpret_cast<uintptr_t>(pAddress);
	bool Found = false;
	if(Module32FirstW(SnapshotHandle, &ModuleEntry))
	{
		do
		{
			const uintptr_t ModuleBase = reinterpret_cast<uintptr_t>(ModuleEntry.modBaseAddr);
			const uintptr_t ModuleEnd = ModuleBase + ModuleEntry.modBaseSize;
			if(Address < ModuleBase || Address >= ModuleEnd)
				continue;

			char aModuleName[512] = "";
			char aModulePath[IO_MAX_PATH_LENGTH * 3] = "";
			WideToUtf8(ModuleEntry.szModule, aModuleName, sizeof(aModuleName));
			WideToUtf8(ModuleEntry.szExePath, aModulePath, sizeof(aModulePath));

			char aLine[1024];
			str_format(aLine, sizeof(aLine), "Exception module: %s + 0x%llX\r\n",
				aModuleName[0] != '\0' ? aModuleName : "(unknown-module)",
				(unsigned long long)(Address - ModuleBase));
			WriteRaw(FileHandle, aLine);
			str_format(aLine, sizeof(aLine), "Exception module range: 0x%016llX-0x%016llX\r\n",
				(unsigned long long)ModuleBase,
				(unsigned long long)ModuleEnd);
			WriteRaw(FileHandle, aLine);
			if(aModulePath[0] != '\0')
			{
				str_format(aLine, sizeof(aLine), "Exception module path: %s\r\n", aModulePath);
				WriteRaw(FileHandle, aLine);
			}
			Found = true;
			break;
		}
		while(Module32NextW(SnapshotHandle, &ModuleEntry));
	}

	if(!Found)
		WriteRaw(FileHandle, "Exception module: unresolved\r\n");

	CloseHandle(SnapshotHandle);
}

static void WriteLoadedModules(HANDLE FileHandle)
{
	WriteSection(FileHandle, "Loaded modules:");

	const HANDLE SnapshotHandle = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
	if(SnapshotHandle == INVALID_HANDLE_VALUE)
	{
		WriteRaw(FileHandle, "  <module snapshot unavailable>\r\n");
		return;
	}

	MODULEENTRY32W ModuleEntry{};
	ModuleEntry.dwSize = sizeof(ModuleEntry);
	int ModuleCount = 0;
	bool Truncated = false;
	if(Module32FirstW(SnapshotHandle, &ModuleEntry))
	{
		do
		{
			if(ModuleCount >= 128)
			{
				Truncated = true;
				break;
			}

			char aModuleName[512] = "";
			char aModulePath[IO_MAX_PATH_LENGTH * 3] = "";
			WideToUtf8(ModuleEntry.szModule, aModuleName, sizeof(aModuleName));
			WideToUtf8(ModuleEntry.szExePath, aModulePath, sizeof(aModulePath));

			const uintptr_t ModuleBase = reinterpret_cast<uintptr_t>(ModuleEntry.modBaseAddr);
			const uintptr_t ModuleEnd = ModuleBase + ModuleEntry.modBaseSize;

			char aLine[2048];
			str_format(aLine, sizeof(aLine), "  0x%016llX-0x%016llX %8lu %s | %s\r\n",
				(unsigned long long)ModuleBase,
				(unsigned long long)ModuleEnd,
				(unsigned long)ModuleEntry.modBaseSize,
				aModuleName[0] != '\0' ? aModuleName : "(unknown-module)",
				aModulePath[0] != '\0' ? aModulePath : "(unknown-path)");
			WriteRaw(FileHandle, aLine);
			++ModuleCount;
		}
		while(Module32NextW(SnapshotHandle, &ModuleEntry));
	}
	else
	{
		WriteRaw(FileHandle, "  <no modules enumerated>\r\n");
	}

	if(Truncated)
		WriteRaw(FileHandle, "  <module list truncated after 128 entries>\r\n");

	CloseHandle(SnapshotHandle);
}

static void WriteExceptionContext(HANDLE FileHandle, const CONTEXT *pContext)
{
	if(pContext == nullptr)
		return;

	WriteSection(FileHandle, "CPU context:");

	char aLine[1024];
	str_format(aLine, sizeof(aLine), "Context flags: 0x%08lX\r\n", (unsigned long)pContext->ContextFlags);
	WriteRaw(FileHandle, aLine);

#if defined(CONF_ARCH_AMD64)
	str_format(aLine, sizeof(aLine), "RIP=0x%016llX RSP=0x%016llX RBP=0x%016llX EFLAGS=0x%08lX\r\n",
		(unsigned long long)pContext->Rip,
		(unsigned long long)pContext->Rsp,
		(unsigned long long)pContext->Rbp,
		(unsigned long)pContext->EFlags);
	WriteRaw(FileHandle, aLine);
	str_format(aLine, sizeof(aLine), "RAX=0x%016llX RBX=0x%016llX RCX=0x%016llX RDX=0x%016llX\r\n",
		(unsigned long long)pContext->Rax,
		(unsigned long long)pContext->Rbx,
		(unsigned long long)pContext->Rcx,
		(unsigned long long)pContext->Rdx);
	WriteRaw(FileHandle, aLine);
	str_format(aLine, sizeof(aLine), "RSI=0x%016llX RDI=0x%016llX R8 =0x%016llX R9 =0x%016llX\r\n",
		(unsigned long long)pContext->Rsi,
		(unsigned long long)pContext->Rdi,
		(unsigned long long)pContext->R8,
		(unsigned long long)pContext->R9);
	WriteRaw(FileHandle, aLine);
	str_format(aLine, sizeof(aLine), "R10=0x%016llX R11=0x%016llX R12=0x%016llX R13=0x%016llX\r\n",
		(unsigned long long)pContext->R10,
		(unsigned long long)pContext->R11,
		(unsigned long long)pContext->R12,
		(unsigned long long)pContext->R13);
	WriteRaw(FileHandle, aLine);
	str_format(aLine, sizeof(aLine), "R14=0x%016llX R15=0x%016llX\r\n",
		(unsigned long long)pContext->R14,
		(unsigned long long)pContext->R15);
	WriteRaw(FileHandle, aLine);

	WriteMemoryBytes(FileHandle, "Instruction bytes near RIP", reinterpret_cast<const void *>(pContext->Rip), 32, 64);
	WriteMemoryBytes(FileHandle, "Stack bytes near RSP", reinterpret_cast<const void *>(pContext->Rsp), 0, 128);
#elif defined(CONF_ARCH_IA32)
	str_format(aLine, sizeof(aLine), "EIP=0x%08lX ESP=0x%08lX EBP=0x%08lX EFLAGS=0x%08lX\r\n",
		(unsigned long)pContext->Eip,
		(unsigned long)pContext->Esp,
		(unsigned long)pContext->Ebp,
		(unsigned long)pContext->EFlags);
	WriteRaw(FileHandle, aLine);
	str_format(aLine, sizeof(aLine), "EAX=0x%08lX EBX=0x%08lX ECX=0x%08lX EDX=0x%08lX\r\n",
		(unsigned long)pContext->Eax,
		(unsigned long)pContext->Ebx,
		(unsigned long)pContext->Ecx,
		(unsigned long)pContext->Edx);
	WriteRaw(FileHandle, aLine);
	str_format(aLine, sizeof(aLine), "ESI=0x%08lX EDI=0x%08lX\r\n",
		(unsigned long)pContext->Esi,
		(unsigned long)pContext->Edi);
	WriteRaw(FileHandle, aLine);

	WriteMemoryBytes(FileHandle, "Instruction bytes near EIP", reinterpret_cast<const void *>(pContext->Eip), 32, 64);
	WriteMemoryBytes(FileHandle, "Stack bytes near ESP", reinterpret_cast<const void *>(pContext->Esp), 0, 128);
#else
	WriteRaw(FileHandle, "Register dump: unsupported architecture\r\n");
#endif
}

static void WriteExceptionDetails(HANDLE FileHandle, EXCEPTION_POINTERS *pExceptionPointers)
{
	if(pExceptionPointers == nullptr || pExceptionPointers->ExceptionRecord == nullptr)
		return;

	const EXCEPTION_RECORD *pRecord = pExceptionPointers->ExceptionRecord;

	WriteSection(FileHandle, "Exception details:");

	char aLine[1024];
	const DWORD ExceptionCode = pRecord->ExceptionCode;
	str_format(aLine, sizeof(aLine), "Exception code: 0x%08lX (%s)\r\n",
		(unsigned long)ExceptionCode,
		ExceptionCodeToString(ExceptionCode));
	WriteRaw(FileHandle, aLine);
	str_format(aLine, sizeof(aLine), "Exception flags: 0x%08lX\r\n", (unsigned long)pRecord->ExceptionFlags);
	WriteRaw(FileHandle, aLine);
	str_format(aLine, sizeof(aLine), "Exception address: 0x%p\r\n", pRecord->ExceptionAddress);
	WriteRaw(FileHandle, aLine);
	WriteExceptionModule(FileHandle, pRecord->ExceptionAddress);
	str_format(aLine, sizeof(aLine), "Exception parameters: %lu\r\n", (unsigned long)pRecord->NumberParameters);
	WriteRaw(FileHandle, aLine);

	for(DWORD Index = 0; Index < pRecord->NumberParameters && Index < EXCEPTION_MAXIMUM_PARAMETERS; ++Index)
	{
		str_format(aLine, sizeof(aLine), "  Parameter[%lu]: 0x%016llX\r\n",
			(unsigned long)Index,
			(unsigned long long)pRecord->ExceptionInformation[Index]);
		WriteRaw(FileHandle, aLine);
	}

	if((ExceptionCode == EXCEPTION_ACCESS_VIOLATION || ExceptionCode == EXCEPTION_IN_PAGE_ERROR) && pRecord->NumberParameters >= 2)
	{
		const char *pAccessKind = "unknown";
		switch(pRecord->ExceptionInformation[0])
		{
		case 0:
			pAccessKind = "read";
			break;
		case 1:
			pAccessKind = "write";
			break;
		case 8:
			pAccessKind = "execute";
			break;
		}

		str_format(aLine, sizeof(aLine), "Access violation operation: %s\r\n", pAccessKind);
		WriteRaw(FileHandle, aLine);
		str_format(aLine, sizeof(aLine), "Access violation target address: 0x%016llX\r\n",
			(unsigned long long)pRecord->ExceptionInformation[1]);
		WriteRaw(FileHandle, aLine);
		WriteMemoryBytes(FileHandle, "Bytes at fault address", reinterpret_cast<const void *>(pRecord->ExceptionInformation[1]), 0, 64);
	}

	if(ExceptionCode == EXCEPTION_IN_PAGE_ERROR && pRecord->NumberParameters >= 3)
	{
		str_format(aLine, sizeof(aLine), "In-page NTSTATUS: 0x%08lX\r\n", (unsigned long)pRecord->ExceptionInformation[2]);
		WriteRaw(FileHandle, aLine);
	}

	if(pRecord->ExceptionRecord != nullptr)
	{
		str_format(aLine, sizeof(aLine), "Nested exception code: 0x%08lX\r\n", (unsigned long)pRecord->ExceptionRecord->ExceptionCode);
		WriteRaw(FileHandle, aLine);
		str_format(aLine, sizeof(aLine), "Nested exception address: 0x%p\r\n", pRecord->ExceptionRecord->ExceptionAddress);
		WriteRaw(FileHandle, aLine);
	}
}

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

	char aLine[2048];
	SYSTEMTIME LocalTime{};
	GetLocalTime(&LocalTime);

	WriteRaw(FileHandle, "QmClient fatal error report\r\n");
	WriteRaw(FileHandle, "Report type: fatal-crash\r\n");
	str_format(aLine, sizeof(aLine), "Timestamp: %04u-%02u-%02u %02u:%02u:%02u.%03u\r\n",
		(unsigned)LocalTime.wYear,
		(unsigned)LocalTime.wMonth,
		(unsigned)LocalTime.wDay,
		(unsigned)LocalTime.wHour,
		(unsigned)LocalTime.wMinute,
		(unsigned)LocalTime.wSecond,
		(unsigned)LocalTime.wMilliseconds);
	WriteRaw(FileHandle, aLine);
	str_format(aLine, sizeof(aLine), "Reason: %s\r\n", pReason != nullptr ? pReason : "unknown");
	WriteRaw(FileHandle, aLine);
	str_format(aLine, sizeof(aLine), "Process ID: %lu\r\n", (unsigned long)GetCurrentProcessId());
	WriteRaw(FileHandle, aLine);
	str_format(aLine, sizeof(aLine), "Thread ID: %lu\r\n", (unsigned long)GetCurrentThreadId());
	WriteRaw(FileHandle, aLine);
	WriteExecutablePath(FileHandle);
	str_format(aLine, sizeof(aLine), "Crash log path (.RTP): %s\r\n", gs_aCrashLogPath);
	WriteRaw(FileHandle, aLine);
	str_format(aLine, sizeof(aLine), "Fallback minidump path: %s\r\n", gs_aFallbackDumpPath);
	WriteRaw(FileHandle, aLine);
	str_format(aLine, sizeof(aLine), "Fallback minidump written: %s\r\n", DumpWritten ? "yes" : "no");
	WriteRaw(FileHandle, aLine);
	str_format(aLine, sizeof(aLine), "Requested extended minidump flags: 0x%08lX\r\n", (unsigned long)gs_ExtendedDumpType);
	WriteRaw(FileHandle, aLine);
	str_format(aLine, sizeof(aLine), "Fallback to minimal minidump flags: %s\r\n", gs_LastMiniDumpRetriedWithMinimalFlags ? "yes" : "no");
	WriteRaw(FileHandle, aLine);
	str_format(aLine, sizeof(aLine), "Written minidump flags: 0x%08lX\r\n", (unsigned long)gs_LastMiniDumpWrittenFlags);
	WriteRaw(FileHandle, aLine);
	if(gs_LastMiniDumpRetriedWithMinimalFlags)
	{
		str_format(aLine, sizeof(aLine), "Extended minidump attempt error: 0x%08lX\r\n", (unsigned long)gs_LastMiniDumpExtendedAttemptError);
		WriteRaw(FileHandle, aLine);
	}
	if(!DumpWritten)
	{
		str_format(aLine, sizeof(aLine), "Final minidump error: 0x%08lX\r\n", (unsigned long)gs_LastMiniDumpFinalError);
		WriteRaw(FileHandle, aLine);
	}

	if(SignalNumber != 0)
	{
		str_format(aLine, sizeof(aLine), "Signal: %d (%s)\r\n", SignalNumber, SignalToString(SignalNumber));
		WriteRaw(FileHandle, aLine);
	}

	WriteExceptionDetails(FileHandle, pExceptionPointers);
	if(pExceptionPointers != nullptr)
		WriteExceptionContext(FileHandle, pExceptionPointers->ContextRecord);
	WriteLoadedModules(FileHandle);

	FlushFileBuffers(FileHandle);
	CloseHandle(FileHandle);
}

static bool TryWriteMiniDumpFile(const char *pFilename, EXCEPTION_POINTERS *pExceptionPointers, MINIDUMP_TYPE DumpType, DWORD *pOutError)
{
	if(gs_pMiniDumpWriteDump == nullptr || pFilename == nullptr || pFilename[0] == '\0')
	{
		if(pOutError != nullptr)
			*pOutError = gs_pMiniDumpWriteDump == nullptr ? ERROR_PROC_NOT_FOUND : ERROR_INVALID_PARAMETER;
		return false;
	}

	HANDLE FileHandle = CreateFileA(pFilename, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(FileHandle == INVALID_HANDLE_VALUE)
	{
		if(pOutError != nullptr)
			*pOutError = GetLastError();
		return false;
	}

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
	const DWORD LastError = Result ? ERROR_SUCCESS : GetLastError();
	CloseHandle(FileHandle);
	if(pOutError != nullptr)
		*pOutError = LastError;
	return Result != FALSE;
}

static bool WriteMiniDumpFile(const char *pFilename, EXCEPTION_POINTERS *pExceptionPointers)
{
	gs_LastMiniDumpWrittenFlags = 0;
	gs_LastMiniDumpExtendedAttemptError = ERROR_SUCCESS;
	gs_LastMiniDumpFinalError = ERROR_SUCCESS;
	gs_LastMiniDumpRetriedWithMinimalFlags = false;

	if(TryWriteMiniDumpFile(pFilename, pExceptionPointers, gs_ExtendedDumpType, &gs_LastMiniDumpExtendedAttemptError))
	{
		gs_LastMiniDumpWrittenFlags = (DWORD)gs_ExtendedDumpType;
		return true;
	}

	gs_LastMiniDumpRetriedWithMinimalFlags = true;
	if(TryWriteMiniDumpFile(pFilename, pExceptionPointers, gs_MinimalDumpType, &gs_LastMiniDumpFinalError))
	{
		gs_LastMiniDumpWrittenFlags = (DWORD)gs_MinimalDumpType;
		return true;
	}

	return false;
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
