#include "crashdump.h"

#include "detect.h"
#include "fs.h"
#include "str.h"
#include "system.h"

#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdint>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

#include <cwchar>
#include <iterator>
#endif

static std::atomic<const char *> gs_pGraphicsBackend{nullptr};

void crashdump_set_graphics_backend(const char *pBackendName)
{
	const char *pKnownBackend = nullptr;
	if(pBackendName != nullptr)
	{
		if(str_comp_nocase(pBackendName, "OpenGL") == 0)
			pKnownBackend = "OpenGL";
		else if(str_comp_nocase(pBackendName, "OpenGL ES") == 0)
			pKnownBackend = "OpenGL ES";
		else if(str_comp_nocase(pBackendName, "Vulkan") == 0)
			pKnownBackend = "Vulkan";
	}
	gs_pGraphicsBackend.store(pKnownBackend, std::memory_order_release);
}

const char *crashdump_graphics_backend_for_report()
{
	return gs_pGraphicsBackend.load(std::memory_order_acquire);
}

const char *crashdump_signal_name(int Signal)
{
	switch(Signal)
	{
	case SIGILL: return "SIGILL";
	case SIGABRT: return "SIGABRT";
	case SIGFPE: return "SIGFPE";
	case SIGSEGV: return "SIGSEGV";
	case SIGTERM: return "SIGTERM";
#ifdef SIGBUS
	case SIGBUS: return "SIGBUS";
#endif
#ifdef SIGTRAP
	case SIGTRAP: return "SIGTRAP";
#endif
	default: return "SIG?";
	}
}

size_t crashdump_format_fatal_report(char *pBuffer, size_t BufferSize, const SCrashFatalReport &Report)
{
	if(pBuffer == nullptr || BufferSize == 0)
		return 0;

	// 可选行先单独成串，主格式只做一次拼接：handler 内调用次数越少越安全。
	char aVersionLine[192] = "";
	if(Report.m_pVersion != nullptr && Report.m_pVersion[0] != '\0')
		str_format(aVersionLine, sizeof(aVersionLine), "Version: %s\n", Report.m_pVersion);

	char aBackendLine[128] = "";
	if(Report.m_pGraphicsBackend != nullptr && Report.m_pGraphicsBackend[0] != '\0')
		str_format(aBackendLine, sizeof(aBackendLine), "Graphics backend: %s\n", Report.m_pGraphicsBackend);

	const int Length = str_format(pBuffer, (int)BufferSize,
		"QmClient fatal crash report\n"
		"===========================\n"
		"Program: %s\n"
		"%s"
		"Platform: %s\n"
		"%s"
		"Started at: %s\n"
		"Process id: %d\n"
		"\n"
		"Signal: %s (%d)\n"
		"Signal code: %d\n"
		"Fault address: 0x%016llX\n"
		"\n"
		"Registers:\n"
		"  pc 0x%016llX\n"
		"  lr 0x%016llX\n"
		"  sp 0x%016llX\n"
		"  fp 0x%016llX\n"
		"\n",
		Report.m_pProgramName != nullptr ? Report.m_pProgramName : "(unknown)",
		aVersionLine,
		Report.m_pPlatform != nullptr ? Report.m_pPlatform : "(unknown)",
		aBackendLine,
		Report.m_pStartTime != nullptr ? Report.m_pStartTime : "(unknown)",
		Report.m_Pid,
		crashdump_signal_name(Report.m_Signal), Report.m_Signal,
		Report.m_SignalCode,
		(unsigned long long)Report.m_FaultAddress,
		(unsigned long long)Report.m_Pc,
		(unsigned long long)Report.m_Lr,
		(unsigned long long)Report.m_Sp,
		(unsigned long long)Report.m_Fp);

	if(Length <= 0)
		return 0;
	return (size_t)Length < BufferSize ? (size_t)Length : BufferSize - 1;
}

size_t crashdump_walk_frame_chain(FS_CRASH_READ_MEMORY pReadMemory, void *pUser, uintptr_t Pc, uintptr_t Fp, uintptr_t Sp, uintptr_t *pFrames, size_t MaxFrames)
{
	if(pReadMemory == nullptr || pFrames == nullptr || MaxFrames == 0)
		return 0;

	size_t Count = 0;
	pFrames[Count++] = Pc;

	const uintptr_t PointerSize = (uintptr_t)sizeof(uintptr_t);
	// frame pointer 必须对齐且不低于当前栈指针，否则视为现场不可回溯。
	if(Fp == 0 || Fp % PointerSize != 0 || (Sp != 0 && Fp < Sp))
		return Count;

	uintptr_t Frame = Fp;
	while(Count < MaxFrames)
	{
		uintptr_t NextFrame = 0;
		uintptr_t ReturnAddress = 0;
		if(!pReadMemory(Frame, &NextFrame, pUser))
			break;
		if(!pReadMemory(Frame + PointerSize, &ReturnAddress, pUser))
			break;
		if(ReturnAddress == 0)
			break;
		pFrames[Count++] = ReturnAddress;
		// 栈向低地址增长：上一层 frame pointer 必须更大，否则按链已损坏处理，避免死循环。
		if(NextFrame <= Frame)
			break;
		Frame = NextFrame;
	}
	return Count;
}

const char *crashdump_find_module(const SCrashModuleRange *pModules, size_t NumModules, uintptr_t Address, uintptr_t *pOffset)
{
	if(pModules == nullptr || Address == 0)
		return nullptr;

	for(size_t i = 0; i < NumModules; i++)
	{
		if(Address >= pModules[i].m_Start && Address < pModules[i].m_End)
		{
			if(pOffset != nullptr)
				*pOffset = Address - pModules[i].m_Start;
			return pModules[i].m_aName;
		}
	}
	return nullptr;
}

bool crashdump_add_module_range(SCrashModuleRange *pModules, size_t *pNumModules, size_t MaxModules, uintptr_t Start, uintptr_t End, const char *pPath)
{
	if(pModules == nullptr || pNumModules == nullptr || pPath == nullptr || pPath[0] == '\0')
		return false;

	// 按完整路径合并同一模块的多个映射，基址取最小值即 ELF 装载基址。
	for(size_t i = 0; i < *pNumModules; i++)
	{
		if(str_comp(pModules[i].m_aPath, pPath) == 0)
		{
			if(Start < pModules[i].m_Start)
				pModules[i].m_Start = Start;
			if(End > pModules[i].m_End)
				pModules[i].m_End = End;
			return false;
		}
	}

	if(*pNumModules >= MaxModules)
		return false;

	const char *pName = str_rchr(pPath, '/');
	pName = pName != nullptr ? pName + 1 : pPath;
	if(pName[0] == '\0')
		return false;

	SCrashModuleRange &Range = pModules[(*pNumModules)++];
	Range.m_Start = Start;
	Range.m_End = End;
	str_copy(Range.m_aName, pName, sizeof(Range.m_aName));
	str_copy(Range.m_aPath, pPath, sizeof(Range.m_aPath));
	return true;
}

size_t crashdump_select_stale_reports(const time_t *pTimes, size_t NumReports, size_t MaxKeep, size_t *pIndices, size_t MaxIndices)
{
	if(pTimes == nullptr || pIndices == nullptr || MaxIndices == 0 || NumReports <= MaxKeep)
		return 0;

	const size_t StaleCount = NumReports - MaxKeep;
	const size_t Count = StaleCount < MaxIndices ? StaleCount : MaxIndices;

	// 移动端报告数量在十位数，直接逐个挑最旧的，避免在崩溃路径引入额外内存。
	for(size_t i = 0; i < Count; i++)
	{
		size_t Best = NumReports;
		for(size_t j = 0; j < NumReports; j++)
		{
			bool AlreadyPicked = false;
			for(size_t k = 0; k < i; k++)
			{
				if(pIndices[k] == j)
				{
					AlreadyPicked = true;
					break;
				}
			}
			if(AlreadyPicked)
				continue;
			if(Best == NumReports || pTimes[j] < pTimes[Best])
				Best = j;
		}
		if(Best == NumReports)
			break;
		pIndices[i] = Best;
	}
	return Count;
}

size_t crashdump_append_frame_lines(char *pBuffer, size_t BufferSize, size_t Length, const uintptr_t *pFrames, size_t NumFrames)
{
	return crashdump_append_frame_lines(pBuffer, BufferSize, Length, pFrames, NumFrames, nullptr, 0);
}

size_t crashdump_append_frame_lines(char *pBuffer, size_t BufferSize, size_t Length, const uintptr_t *pFrames, size_t NumFrames, const SCrashModuleRange *pModules, size_t NumModules)
{
	if(pBuffer == nullptr || BufferSize == 0)
		return 0;
	if(Length >= BufferSize - 1)
		return Length;
	if(pFrames == nullptr || NumFrames == 0)
		return Length;

	int Written = str_format(pBuffer + Length, (int)(BufferSize - Length),
		"Backtrace (%zu frames, raw addresses; resolve them with addr2line against the unstripped library):\n", NumFrames);
	if(Written <= 0)
		return Length;
	Length += (size_t)Written;

	for(size_t i = 0; i < NumFrames && Length < BufferSize - 1; i++)
	{
		uintptr_t Offset = 0;
		const char *pModule = crashdump_find_module(pModules, NumModules, pFrames[i], &Offset);
		if(pModule != nullptr)
			Written = str_format(pBuffer + Length, (int)(BufferSize - Length), "  #%02zu 0x%016llX %s+0x%llX\n", i, (unsigned long long)pFrames[i], pModule, (unsigned long long)Offset);
		else
			Written = str_format(pBuffer + Length, (int)(BufferSize - Length), "  #%02zu 0x%016llX\n", i, (unsigned long long)pFrames[i]);
		if(Written <= 0)
			break;
		Length += (size_t)Written;
	}
	return Length < BufferSize ? Length : BufferSize - 1;
}

static std::atomic<bool> gs_SuppressNextFatalReporter{false};

// QmClient: 退出阶段标记。客户端在销毁图形后端之前置位，用于识别
// “进程已在退出、异常又落在图形驱动模块内”的已知无害故障（见 crashdump.h）。
static std::atomic<bool> gs_ShutdownInProgress{false};
static std::atomic<bool> gs_ShutdownGraphicsFault{false};

// 退出清理期间允许被忽略的图形驱动模块。这些 DLL 内的崩溃发生在驱动释放
// GPU 对象的过程中，进程已进入退出，重新弹窗既无意义也打扰用户。
static constexpr const char *gs_apShutdownIgnorableDriverModules[] = {
	"nvoglv64.dll",
	"nvoglv32.dll",
	"nvd3dumx.dll",
	"nvwgf2umx.dll",
	"amdvlk64.dll",
	"atio6axx.dll",
	"ig9icd64.dll",
	"igvk64.dll",
	"opengl32.dll",
	"vulkan-1.dll",
	"D3D12Core.dll",
	"d3d12.dll",
	"dxgi.dll",
};

bool crashdump_is_ignorable_shutdown_driver_module(const char *p_module_name)
{
	if(p_module_name == nullptr || p_module_name[0] == '\0')
		return false;
	for(const char *pCandidate : gs_apShutdownIgnorableDriverModules)
	{
		if(str_comp_nocase(p_module_name, pCandidate) == 0)
			return true;
	}
	return false;
}

void crashdump_suppress_reporter_once()
{
	gs_SuppressNextFatalReporter.store(true, std::memory_order_release);
}

void crashdump_mark_shutdown_begin(const char *p_driver_module)
{
	(void)p_driver_module;
	// 驱动模块清单固定在本文件内；参数保留给将来需要按实际渲染器细分的场景。
	gs_ShutdownInProgress.store(true, std::memory_order_release);
}

bool crashdump_is_shutdown_graphics_fault()
{
	return gs_ShutdownGraphicsFault.load(std::memory_order_acquire);
}

void crashdump_mark_shutdown_end()
{
	gs_ShutdownInProgress.store(false, std::memory_order_release);
}

bool crashdump_launch_reporter_if_available(const char *report_path)
{
	// 退出阶段的图形驱动故障不再打扰用户，也不启动报告进程。
	if(crashdump_is_shutdown_graphics_fault())
		return false;
#if defined(CONF_FAMILY_WINDOWS)
	// QmClient 测试专用：设置了 QMCLIENT_TEST_HIDE_DIALOG 的进程级回归测试不拉起
	// 报告进程——测试直接校验报告文件，避免遗留后台报告进程占用可执行文件。
	char aSuppressTestReporter[8] = "";
	if(GetEnvironmentVariableA("QMCLIENT_TEST_HIDE_DIALOG", aSuppressTestReporter, sizeof(aSuppressTestReporter)) > 0)
		return false;

	if(report_path == nullptr || report_path[0] == '\0')
		return false;

	wchar_t aExecutablePath[IO_MAX_PATH_LENGTH];
	const DWORD ExecutablePathLength = GetModuleFileNameW(nullptr, aExecutablePath, std::size(aExecutablePath));
	if(ExecutablePathLength == 0 || ExecutablePathLength >= std::size(aExecutablePath))
		return false;

	wchar_t aReportPath[IO_MAX_PATH_LENGTH];
	if(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, report_path, -1, aReportPath, std::size(aReportPath)) <= 0)
		return false;

	// Windows 路径不能包含双引号，因此可以安全地用引号包住自生成的报告路径。
	wchar_t aCommandLine[IO_MAX_PATH_LENGTH * 2 + 96];
	const int CommandLength = _snwprintf_s(aCommandLine, std::size(aCommandLine), _TRUNCATE,
		L"\"%ls\" --qm-crash-reporter \"%ls\"", aExecutablePath, aReportPath);
	if(CommandLength < 0)
		return false;

	STARTUPINFOW StartupInfo{};
	StartupInfo.cb = sizeof(StartupInfo);
	StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
	StartupInfo.wShowWindow = SW_SHOWNORMAL;
	AllowSetForegroundWindow(ASFW_ANY);
	PROCESS_INFORMATION ProcessInfo{};
	if(!CreateProcessW(aExecutablePath, aCommandLine, nullptr, nullptr, FALSE, CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &StartupInfo, &ProcessInfo))
		return false;

	CloseHandle(ProcessInfo.hThread);
	CloseHandle(ProcessInfo.hProcess);
	return true;
#else
	(void)report_path;
	return false;
#endif
}

#if defined(CONF_CRASHDUMP)
#if !defined(CONF_FAMILY_WINDOWS)

#if defined(CONF_PLATFORM_ANDROID)

#include "log.h"

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <ucontext.h>

#include <cstdio>
#include <iterator>

static char gs_aAndroidReportPath[IO_MAX_PATH_LENGTH] = "";
static char gs_aAndroidStartTime[64] = "";
static int gs_AndroidPid = 0;
static bool gs_AndroidHandlersInstalled = false;
// handler 重入即意味着回溯读到了不可访问的内存：核心现场已落盘，直接结束进程。
static volatile sig_atomic_t gs_AndroidHandlerDepth = 0;

static const int gs_aAndroidFatalSignals[] = {
	SIGSEGV,
	SIGABRT,
#ifdef SIGBUS
	SIGBUS,
#endif
	SIGFPE,
	SIGILL,
};

// 报告写成独立文件，沿用桌面的 _fatal_report.txt 约定：拷到手机「下载」后
// 文件管理器可以按 text/plain 直接打开。主日志路径本身是 .RTP，不能当报告用。
static void AndroidBuildReportPath(const char *pLogPath, char *pBuffer, size_t BufferSize)
{
	str_copy(pBuffer, pLogPath, (int)BufferSize);
	const char *pExtension = str_rchr(pBuffer, '.');
	if(pExtension != nullptr)
		pBuffer[pExtension - pBuffer] = '\0';
	str_append(pBuffer, "_fatal_report.txt", (int)BufferSize);
}

// 模块表在初始化阶段（进程健康）填充，信号处理器只做纯内存查表。
static SCrashModuleRange gs_aAndroidModules[64];
static size_t gs_NumAndroidModules = 0;

static void AndroidAddModule(uintptr_t Start, uintptr_t End, const char *pPath)
{
	crashdump_add_module_range(gs_aAndroidModules, &gs_NumAndroidModules, std::size(gs_aAndroidModules), Start, End, pPath);
}

static void AndroidLoadModuleRanges()
{
	gs_NumAndroidModules = 0;
	FILE *pFile = fopen("/proc/self/maps", "r");
	if(pFile == nullptr)
		return;

	char aLine[512];
	while(fgets(aLine, sizeof(aLine), pFile) != nullptr)
	{
		unsigned long long Start = 0;
		unsigned long long End = 0;
		char aPermissions[8] = "";
		char aPath[384] = "";
		// 行格式："start-end perms offset dev inode pathname"，路径是最后一个字段。
		if(sscanf(aLine, "%llx-%llx %7s %*s %*s %*s %383s", &Start, &End, aPermissions, aPath) != 4)
			continue;
		// 只有可执行映射才可能出现在栈帧里。
		if(aPermissions[2] != 'x')
			continue;
		if(str_find(aPath, ".so") == nullptr)
			continue;
		AndroidAddModule((uintptr_t)Start, (uintptr_t)End, aPath);
	}
	fclose(pFile);
}

static void AndroidWriteAll(int File, const char *pData, size_t Size)
{
	while(Size > 0)
	{
		const ssize_t Written = write(File, pData, Size);
		if(Written <= 0)
			return;
		pData += Written;
		Size -= (size_t)Written;
	}
}

#if !defined(__arm__)
static bool AndroidReadMemory(uintptr_t Address, uintptr_t *pValue, void *pUser)
{
	(void)pUser;
	// 地址 0 附近必然是无效页；其余交给重入保护兜底。
	if(Address < 4096)
		return false;
	*pValue = *reinterpret_cast<const uintptr_t *>(Address);
	return true;
}
#endif

static void AndroidFatalSignalHandler(int Signal, siginfo_t *pInfo, void *pContext)
{
	if(gs_AndroidHandlerDepth != 0)
		_exit(128 + Signal);
	gs_AndroidHandlerDepth = 1;

	uintptr_t Pc = 0;
	uintptr_t Sp = 0;
	uintptr_t Fp = 0;
	uintptr_t Lr = 0;
	const ucontext_t *pUContext = static_cast<const ucontext_t *>(pContext);
#if defined(__aarch64__)
	if(pUContext != nullptr)
	{
		Pc = (uintptr_t)pUContext->uc_mcontext.pc;
		Sp = (uintptr_t)pUContext->uc_mcontext.sp;
		Fp = (uintptr_t)pUContext->uc_mcontext.regs[29];
		Lr = (uintptr_t)pUContext->uc_mcontext.regs[30];
	}
#elif defined(__x86_64__)
	if(pUContext != nullptr)
	{
		Pc = (uintptr_t)pUContext->uc_mcontext.gregs[REG_RIP];
		Sp = (uintptr_t)pUContext->uc_mcontext.gregs[REG_RSP];
		Fp = (uintptr_t)pUContext->uc_mcontext.gregs[REG_RBP];
	}
#elif defined(__arm__)
	// 32 位 ARM 的 frame pointer 链布局随编译选项变化，这里只记录寄存器现场，
	// 不沿 fp 回溯，避免把错误的栈帧当成真实调用栈。
	if(pUContext != nullptr)
	{
		Pc = (uintptr_t)pUContext->uc_mcontext.arm_pc;
		Sp = (uintptr_t)pUContext->uc_mcontext.arm_sp;
		Lr = (uintptr_t)pUContext->uc_mcontext.arm_lr;
	}
#else
	(void)pUContext;
#endif

	SCrashFatalReport Report = {};
	Report.m_pProgramName = "QmClient";
	Report.m_pPlatform = CONF_PLATFORM_STRING;
	Report.m_pStartTime = gs_aAndroidStartTime;
	Report.m_pGraphicsBackend = crashdump_graphics_backend_for_report();
	Report.m_Pid = gs_AndroidPid;
	Report.m_Signal = Signal;
	Report.m_SignalCode = pInfo != nullptr ? pInfo->si_code : 0;
	Report.m_FaultAddress = pInfo != nullptr ? (uintptr_t)pInfo->si_addr : 0;
	Report.m_Pc = Pc;
	Report.m_Sp = Sp;
	Report.m_Fp = Fp;
	Report.m_Lr = Lr;

	char aBuffer[8192];
	const size_t Length = crashdump_format_fatal_report(aBuffer, sizeof(aBuffer), Report);
	if(Length > 0)
	{
		// 核心现场先落盘：后面的栈回溯可能踩到坏内存触发二次崩溃。
		const int File = open(gs_aAndroidReportPath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if(File >= 0)
		{
			AndroidWriteAll(File, aBuffer, Length);
			fsync(File);
			close(File);
		}
	}

	size_t AppendedLength = 0;
#if defined(__arm__)
	// 32 位 ARM 的 frame pointer 布局随编译选项变化，宁可明确说明也不给错栈。
	str_copy(aBuffer, "Backtrace not collected: 32-bit ARM frame pointer layout is not reliable.\n");
	AppendedLength = (size_t)str_length(aBuffer);
#else
	uintptr_t aFrames[32];
	const size_t NumFrames = crashdump_walk_frame_chain(AndroidReadMemory, nullptr, Pc, Fp, Sp, aFrames, std::size(aFrames));
	if(NumFrames > 0)
		AppendedLength = crashdump_append_frame_lines(aBuffer, sizeof(aBuffer), 0, aFrames, NumFrames, gs_aAndroidModules, gs_NumAndroidModules);
#endif
	if(AppendedLength > 0)
	{
		const int File = open(gs_aAndroidReportPath, O_WRONLY | O_APPEND);
		if(File >= 0)
		{
			AndroidWriteAll(File, aBuffer, AppendedLength);
			fsync(File);
			close(File);
		}
	}

	// 空标记文件：下次启动只搬运本次崩溃新产生、尚未导出的报告。
	char aPendingPath[IO_MAX_PATH_LENGTH + 16];
	str_format(aPendingPath, sizeof(aPendingPath), "%s.pending", gs_aAndroidReportPath);
	const int PendingFile = open(aPendingPath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if(PendingFile >= 0)
		close(PendingFile);

	// 交回系统默认处理，让 Android 侧仍能生成 tombstone 与崩溃统计。
	struct sigaction Default = {};
	Default.sa_handler = SIG_DFL;
	sigemptyset(&Default.sa_mask);
	sigaction(Signal, &Default, nullptr);
	raise(Signal);
	_exit(128 + Signal);
}

void crashdump_init_if_available(const char *log_file_path)
{
	if(log_file_path == nullptr || log_file_path[0] == '\0')
		return;

	AndroidBuildReportPath(log_file_path, gs_aAndroidReportPath, sizeof(gs_aAndroidReportPath));
	str_timestamp(gs_aAndroidStartTime, sizeof(gs_aAndroidStartTime));
	gs_AndroidPid = pid();
	AndroidLoadModuleRanges();
	if(gs_AndroidHandlersInstalled)
		return;

	// 栈溢出时处理函数仍要能运行，因此需要独立信号栈。
	static char s_aAltStack[64 * 1024];
	stack_t AltStack = {};
	AltStack.ss_sp = s_aAltStack;
	AltStack.ss_size = sizeof(s_aAltStack);
	if(sigaltstack(&AltStack, nullptr) != 0)
		log_warn("crashdump", "failed to install the alternate signal stack");

	struct sigaction Action = {};
	Action.sa_sigaction = AndroidFatalSignalHandler;
	sigemptyset(&Action.sa_mask);
	Action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
	for(const int Signal : gs_aAndroidFatalSignals)
	{
		if(sigaction(Signal, &Action, nullptr) != 0)
			log_warn("crashdump", "failed to install handler for signal %d", Signal);
	}
	gs_AndroidHandlersInstalled = true;
	log_info("crashdump", "fatal signal handlers installed, report path '%s'", gs_aAndroidReportPath);
}

#else

void crashdump_init_if_available(const char *log_file_path)
{
	(void)log_file_path;
}

#endif

#else

#include "log.h"
#include "windows.h"

#include <dbghelp.h>
#include <tlhelp32.h>

#include <csignal>
#include <cstdint>
#include <exception>

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
static std::atomic_flag gs_FatalReporterLaunched = ATOMIC_FLAG_INIT;
static std::atomic<bool> gs_FallbackReportWritten{false};

static PVOID gs_pVectoredExceptionHandler = nullptr;

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
		} while(Module32NextW(SnapshotHandle, &ModuleEntry));
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
		} while(Module32NextW(SnapshotHandle, &ModuleEntry));
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

static bool WriteMinimalCrashReport(const char *pReason, EXCEPTION_POINTERS *pExceptionPointers, int SignalNumber, bool DumpWritten)
{
	gs_FallbackReportWritten.store(false, std::memory_order_release);
	if(gs_aFallbackReportPath[0] == '\0')
		return false;

	HANDLE FileHandle = CreateFileA(gs_aFallbackReportPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(FileHandle == INVALID_HANDLE_VALUE)
		return false;

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
	if(const char *pGraphicsBackend = crashdump_graphics_backend_for_report())
	{
		str_format(aLine, sizeof(aLine), "Graphics backend: %s\r\n", pGraphicsBackend);
		WriteRaw(FileHandle, aLine);
	}
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
	gs_FallbackReportWritten.store(true, std::memory_order_release);
	return true;
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

// QmClient: 判断异常地址是否落在已知图形驱动模块内。
// 只在退出阶段使用：退出清理销毁图形后端时，个别驱动会在这类模块内部崩溃，
// 此时进程本就即将结束，写完整转储与弹窗只会打扰用户。
static bool IsShutdownGraphicsFault(EXCEPTION_POINTERS *pExceptionPointers)
{
	if(!gs_ShutdownInProgress.load(std::memory_order_acquire))
		return false;
	if(pExceptionPointers == nullptr || pExceptionPointers->ExceptionRecord == nullptr)
		return false;

	const void *pFaultAddress = pExceptionPointers->ExceptionRecord->ExceptionAddress;
	if(pFaultAddress == nullptr)
		return false;

	HMODULE hModule = nullptr;
	if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		   reinterpret_cast<LPCWSTR>(pFaultAddress), &hModule) ||
		hModule == nullptr)
	{
		return false;
	}

	char aModulePath[IO_MAX_PATH_LENGTH];
	const DWORD Length = GetModuleFileNameA(hModule, aModulePath, sizeof(aModulePath));
	if(Length == 0 || Length >= sizeof(aModulePath))
		return false;

	return crashdump_is_ignorable_shutdown_driver_module(fs_filename(aModulePath));
}

static void HandleFatalCrash(const char *pReason, EXCEPTION_POINTERS *pExceptionPointers, int SignalNumber)
{
	if(gs_FatalReportInProgress.test_and_set(std::memory_order_acq_rel))
		return;

	// 退出阶段的已知图形驱动故障：只留日志，不写转储、不生成报告。
	if(IsShutdownGraphicsFault(pExceptionPointers))
	{
		gs_ShutdownGraphicsFault.store(true, std::memory_order_release);
		log_warn("crashdump", "ignoring graphics driver fault '%s' during shutdown; process is already exiting", pReason);
		return;
	}

	const bool DumpWritten = WriteMiniDumpFile(gs_aFallbackDumpPath, pExceptionPointers);
	WriteMinimalCrashReport(pReason, pExceptionPointers, SignalNumber, DumpWritten);
}

static void LaunchFatalCrashReporter()
{
	if(gs_SuppressNextFatalReporter.exchange(false, std::memory_order_acq_rel))
		return;
	if(!gs_FallbackReportWritten.load(std::memory_order_acquire) || gs_aFallbackReportPath[0] == '\0')
		return;
	if(gs_FatalReporterLaunched.test_and_set(std::memory_order_acq_rel))
		return;

	char aConfirmedPath[IO_MAX_PATH_LENGTH + 16];
	str_format(aConfirmedPath, sizeof(aConfirmedPath), "%s.confirmed", gs_aFallbackReportPath);
	HANDLE ConfirmedFile = CreateFileA(aConfirmedPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(ConfirmedFile != INVALID_HANDLE_VALUE)
	{
		WriteRaw(ConfirmedFile, "confirmed fatal crash\r\n");
		FlushFileBuffers(ConfirmedFile);
		CloseHandle(ConfirmedFile);
	}
	crashdump_launch_reporter_if_available(gs_aFallbackReportPath);
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
	LaunchFatalCrashReporter();
	// 本地报告完成后结束异常处理。继续调用旧处理器会进入 exchndl 的在线符号
	// 下载，使已经崩溃的主线程再次阻塞并触发误导性的 hang 报告。
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
	LaunchFatalCrashReporter();
	TerminateFromFatalHandler();
}

static void FallbackSignalHandler(int SignalNumber)
{
	// 先恢复默认处理，防止报告失败时递归进入信号处理器。
	std::signal(SignalNumber, SIG_DFL);
	HandleFatalCrash("Fatal signal", nullptr, SignalNumber);
	// C 信号处理器中不要创建子进程。报告文件已在上面落盘，下一次启动会
	// 通过 pending-report 流程展示，避免 CreateProcess/窗口初始化再次触发未定义行为。
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
	SetUnhandledExceptionFilter(FallbackUnhandledExceptionFilter);
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
