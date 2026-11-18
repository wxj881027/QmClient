#include "crashdump.h"

#include "crash_output.h"
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

namespace
{
	// Android 在信号处理器里使用这份格式化逻辑。只写调用方给的缓冲区，不进入 printf。
	class CCrashReportText
	{
		char *m_pBuffer;
		size_t m_MaxLength;
		size_t m_Length;

	public:
		CCrashReportText(char *pBuffer, size_t BufferSize, size_t Length = 0) :
			m_pBuffer(pBuffer),
			m_MaxLength(BufferSize - 1),
			m_Length(Length)
		{
		}

		bool Full() const { return m_Length == m_MaxLength; }

		void Append(const char *pText)
		{
			while(!Full() && *pText != '\0')
				m_pBuffer[m_Length++] = *pText++;
		}

		void AppendUnsigned(uint64_t Value, unsigned Base = 10, size_t MinWidth = 0)
		{
			static constexpr char s_aDigits[] = "0123456789ABCDEF";
			char aNumber[20];
			size_t Begin = sizeof(aNumber);
			do
			{
				aNumber[--Begin] = s_aDigits[Value % Base];
				Value /= Base;
			} while(Value != 0 || sizeof(aNumber) - Begin < MinWidth);

			while(!Full() && Begin < sizeof(aNumber))
				m_pBuffer[m_Length++] = aNumber[Begin++];
		}

		void AppendSigned(int Value)
		{
			unsigned Magnitude = static_cast<unsigned>(Value);
			if(Value < 0)
			{
				Append("-");
				Magnitude = 0u - Magnitude;
			}
			AppendUnsigned(Magnitude);
		}

		size_t Finish()
		{
			m_pBuffer[m_Length] = '\0';
			return Full() ? static_cast<size_t>(str_utf8_fix_truncation(m_pBuffer)) : m_Length;
		}
	};
}

size_t crashdump_format_fatal_report(char *pBuffer, size_t BufferSize, const SCrashFatalReport &Report)
{
	if(pBuffer == nullptr || BufferSize == 0)
		return 0;

	CCrashReportText Text(pBuffer, BufferSize);
	Text.Append(
		"QmClient fatal crash report\n"
		"===========================\n"
		"Program: ");
	Text.Append(Report.m_pProgramName ? Report.m_pProgramName : "(unknown)");
	if(Report.m_pVersion && Report.m_pVersion[0] != '\0')
	{
		Text.Append("\nVersion: ");
		Text.Append(Report.m_pVersion);
	}
	Text.Append("\nPlatform: ");
	Text.Append(Report.m_pPlatform ? Report.m_pPlatform : "(unknown)");
	if(Report.m_pGraphicsBackend && Report.m_pGraphicsBackend[0] != '\0')
	{
		Text.Append("\nGraphics backend: ");
		Text.Append(Report.m_pGraphicsBackend);
	}
	Text.Append("\nStarted at: ");
	Text.Append(Report.m_pStartTime ? Report.m_pStartTime : "(unknown)");
	Text.Append("\nProcess id: ");
	Text.AppendSigned(Report.m_Pid);
	Text.Append("\n\nSignal: ");
	Text.Append(crashdump_signal_name(Report.m_Signal));
	Text.Append(" (");
	Text.AppendSigned(Report.m_Signal);
	Text.Append(")\nSignal code: ");
	Text.AppendSigned(Report.m_SignalCode);
	Text.Append("\nFault address: 0x");
	Text.AppendUnsigned(Report.m_FaultAddress, 16, 16);
	Text.Append("\n\nRegisters:\n  pc 0x");
	Text.AppendUnsigned(Report.m_Pc, 16, 16);
	Text.Append("\n  lr 0x");
	Text.AppendUnsigned(Report.m_Lr, 16, 16);
	Text.Append("\n  sp 0x");
	Text.AppendUnsigned(Report.m_Sp, 16, 16);
	Text.Append("\n  fp 0x");
	Text.AppendUnsigned(Report.m_Fp, 16, 16);
	Text.Append("\n\n");
	return Text.Finish();
}

size_t crashdump_walk_frame_chain(FS_CRASH_READ_MEMORY pReadMemory, void *pUser, uintptr_t Pc, uintptr_t Fp, uintptr_t Sp, uintptr_t *pFrames, size_t MaxFrames)
{
	if(pReadMemory == nullptr || pFrames == nullptr || MaxFrames == 0)
		return 0;

	size_t Count = 0;
	pFrames[Count++] = Pc;

	const uintptr_t PointerSize = (uintptr_t)sizeof(uintptr_t);
	if(Fp == 0 || Fp < Sp)
		return Count;

	uintptr_t Frame = Fp;
	while(Count < MaxFrames)
	{
		// 损坏的链可以在任一层给出错位地址，或让 Frame + PointerSize 回绕。
		if(Frame % PointerSize != 0 || Frame > UINTPTR_MAX - PointerSize)
			break;
		uintptr_t NextFrame = 0;
		uintptr_t ReturnAddress = 0;
		if(!pReadMemory(Frame, &NextFrame, pUser))
			break;
		if(!pReadMemory(Frame + PointerSize, &ReturnAddress, pUser))
			break;
		if(ReturnAddress == 0)
			break;
		pFrames[Count++] = ReturnAddress;
		// 上一层必须在当前两字长的栈帧之后。
		if(NextFrame <= Frame + PointerSize)
			break;
		Frame = NextFrame;
	}
	return Count;
}

static const char *FindCrashModule(const SCrashModuleRange *pModules, size_t NumModules, uintptr_t Address, uintptr_t *pOffset, const uintptr_t *pLoadBiases)
{
	if(pModules == nullptr || Address == 0)
		return nullptr;

	for(size_t i = 0; i < NumModules; i++)
	{
		if(Address >= pModules[i].m_Start && Address < pModules[i].m_End)
		{
			if(pOffset != nullptr)
				*pOffset = Address - (pLoadBiases ? pLoadBiases[i] : pModules[i].m_Start);
			return pModules[i].m_aName;
		}
	}
	return nullptr;
}

const char *crashdump_find_module(const SCrashModuleRange *pModules, size_t NumModules, uintptr_t Address, uintptr_t *pOffset)
{
	return FindCrashModule(pModules, NumModules, Address, pOffset, nullptr);
}

bool crashdump_add_module_range(SCrashModuleRange *pModules, size_t *pNumModules, size_t MaxModules, uintptr_t Start, uintptr_t End, const char *pPath)
{
	if(pModules == nullptr || pNumModules == nullptr || pPath == nullptr || pPath[0] == '\0')
		return false;
	if(*pNumModules > MaxModules || str_length(pPath) >= static_cast<int>(sizeof(pModules[0].m_aPath)))
		return false;

	// 只用能完整保存的路径作合并键。
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

static size_t AppendCrashFrameLines(char *pBuffer, size_t BufferSize, size_t Length, const uintptr_t *pFrames, size_t NumFrames,
	const SCrashModuleRange *pModules, size_t NumModules, const uintptr_t *pLoadBiases)
{
	if(pBuffer == nullptr || BufferSize == 0)
		return 0;
	if(Length >= BufferSize - 1)
		return BufferSize - 1;
	if(pFrames == nullptr || NumFrames == 0)
		return Length;

	CCrashReportText Text(pBuffer, BufferSize, Length);
	Text.Append("Backtrace (");
	Text.AppendUnsigned(NumFrames);
	Text.Append(" frames, raw addresses; resolve them with addr2line against the unstripped library):\n");
	for(size_t i = 0; i < NumFrames && !Text.Full(); i++)
	{
		Text.Append("  #");
		Text.AppendUnsigned(i, 10, 2);
		Text.Append(" 0x");
		Text.AppendUnsigned(pFrames[i], 16, 16);

		uintptr_t Offset = 0;
		if(const char *pModule = FindCrashModule(pModules, NumModules, pFrames[i], &Offset, pLoadBiases))
		{
			Text.Append(" ");
			Text.Append(pModule);
			Text.Append("+0x");
			Text.AppendUnsigned(Offset, 16);
		}
		Text.Append("\n");
	}
	return Text.Finish();
}

size_t crashdump_append_frame_lines(char *pBuffer, size_t BufferSize, size_t Length, const uintptr_t *pFrames, size_t NumFrames,
	const SCrashModuleRange *pModules, size_t NumModules)
{
	return AppendCrashFrameLines(pBuffer, BufferSize, Length, pFrames, NumFrames, pModules, NumModules, nullptr);
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
#include <link.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <ucontext.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <iterator>

static char gs_aAndroidReportPath[IO_MAX_PATH_LENGTH] = "";
static char gs_aAndroidCandidatePath[IO_MAX_PATH_LENGTH + 16] = "";
static char gs_aAndroidPendingPath[IO_MAX_PATH_LENGTH + 16] = "";
static char gs_aAndroidStartTime[64] = "";
static bool gs_AndroidHandlersInstalled = false;
static std::atomic_flag gs_AndroidReportInProgress = ATOMIC_FLAG_INIT;
static_assert(std::atomic<const char *>::is_always_lock_free);

static constexpr int gs_aAndroidFatalSignals[] = {
	SIGSEGV,
	SIGABRT,
#ifdef SIGBUS
	SIGBUS,
#endif
	SIGFPE,
	SIGILL,
};
static struct sigaction gs_aAndroidPreviousActions[std::size(gs_aAndroidFatalSignals)];
static char gs_aAndroidReportBuffer[8192];

// 启动时记录 ELF 可执行段和真正的装载偏移。崩溃后只查这张固定表。
static SCrashModuleRange gs_aAndroidModules[64];
static uintptr_t gs_aAndroidLoadBiases[std::size(gs_aAndroidModules)];
static size_t gs_NumAndroidModules = 0;

static int AndroidAddModule(struct dl_phdr_info *pInfo, size_t, void *)
{
	if(!pInfo->dlpi_name || pInfo->dlpi_name[0] == '\0')
		return 0;
	const char *pName = fs_filename(pInfo->dlpi_name);
	for(size_t i = 0; i < pInfo->dlpi_phnum; ++i)
	{
		const auto &Segment = pInfo->dlpi_phdr[i];
		if(Segment.p_type != PT_LOAD || !(Segment.p_flags & PF_X) || Segment.p_memsz == 0)
			continue;
		const uintptr_t Start = pInfo->dlpi_addr + Segment.p_vaddr;
		if(Segment.p_memsz > UINTPTR_MAX - Start)
			continue;
		if(gs_NumAndroidModules == std::size(gs_aAndroidModules))
			return 1;

		const size_t Index = gs_NumAndroidModules++;
		SCrashModuleRange &Range = gs_aAndroidModules[Index];
		Range.m_Start = Start;
		Range.m_End = Start + Segment.p_memsz;
		str_copy(Range.m_aName, pName, sizeof(Range.m_aName));
		gs_aAndroidLoadBiases[Index] = pInfo->dlpi_addr;
	}
	return 0;
}

static bool AndroidWriteAll(int File, const char *pData, size_t Size)
{
	while(Size > 0)
	{
		const ssize_t Written = write(File, pData, Size);
		if(Written < 0 && errno == EINTR)
			continue;
		if(Written <= 0)
			return false;
		pData += Written;
		Size -= static_cast<size_t>(Written);
	}
	return true;
}

static bool AndroidPublishReport(const char *pData, size_t Size)
{
	const int File = open(gs_aAndroidCandidatePath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if(File < 0)
		return false;
	const bool Complete = AndroidWriteAll(File, pData, Size) && fsync(File) == 0;
	const bool Closed = close(File) == 0;
	if(Complete && Closed && rename(gs_aAndroidCandidatePath, gs_aAndroidReportPath) == 0)
		return true;
	unlink(gs_aAndroidCandidatePath);
	return false;
}

#if !defined(__arm__)
static bool AndroidReadMemory(uintptr_t Address, uintptr_t *pValue, void *)
{
	// 内核校验地址和读取长度，坏栈帧只会结束回溯。
	const iovec Local{pValue, sizeof(*pValue)};
	const iovec Remote{reinterpret_cast<void *>(Address), sizeof(*pValue)};
	return process_vm_readv(getpid(), &Local, 1, &Remote, 1, 0) == static_cast<ssize_t>(sizeof(*pValue));
}
#endif

static void AndroidForwardFatalSignal(int Signal, siginfo_t *pInfo)
{
	for(size_t i = 0; i < std::size(gs_aAndroidFatalSignals); ++i)
	{
		if(gs_aAndroidFatalSignals[i] != Signal)
			continue;

		struct sigaction Previous = gs_aAndroidPreviousActions[i];
		if(Previous.sa_handler == SIG_IGN)
			Previous.sa_handler = SIG_DFL;
		if(sigaction(Signal, &Previous, nullptr) == 0)
		{
			// 当前信号在 handler 内被阻塞。返回后由系统向原线程递送，并保留故障地址。
			if(pInfo && syscall(SYS_rt_tgsigqueueinfo, getpid(), gettid(), Signal, pInfo) == 0)
				return;
			if(raise(Signal) == 0)
				return;
		}
		break;
	}
	_exit(128 + Signal);
}

static void AndroidFatalSignalHandler(int Signal, siginfo_t *pInfo, void *pContext)
{
	if(gs_AndroidReportInProgress.test_and_set(std::memory_order_acquire))
	{
		AndroidForwardFatalSignal(Signal, pInfo);
		return;
	}

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
	Report.m_Pid = getpid();
	Report.m_Signal = Signal;
	Report.m_SignalCode = pInfo != nullptr ? pInfo->si_code : 0;
	if(pInfo && pInfo->si_code > 0 && Signal != SIGABRT)
		Report.m_FaultAddress = reinterpret_cast<uintptr_t>(pInfo->si_addr);
	Report.m_Pc = Pc;
	Report.m_Sp = Sp;
	Report.m_Fp = Fp;
	Report.m_Lr = Lr;

	char *pBuffer = gs_aAndroidReportBuffer;
	const size_t Capacity = sizeof(gs_aAndroidReportBuffer);
	const size_t Length = crashdump_format_fatal_report(pBuffer, Capacity, Report);
	if(Length > 0 && AndroidPublishReport(pBuffer, Length))
	{
		// 正文已完整发布。先标记供下次启动导出，回溯失败仍能拿到正文。
		const int PendingFile = open(gs_aAndroidPendingPath, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
		if(PendingFile >= 0)
			close(PendingFile);

		size_t FullLength = Length;
#if defined(__arm__)
		CCrashReportText Text(pBuffer, Capacity, Length);
		Text.Append("Backtrace not collected: 32-bit ARM frame pointer layout is not reliable.\n");
		FullLength = Text.Finish();
#else
		uintptr_t aFrames[32];
		const size_t NumFrames = crashdump_walk_frame_chain(AndroidReadMemory, nullptr, Pc, Fp, Sp, aFrames, std::size(aFrames));
		FullLength = AppendCrashFrameLines(pBuffer, Capacity, Length, aFrames, NumFrames,
			gs_aAndroidModules, gs_NumAndroidModules, gs_aAndroidLoadBiases);
#endif
		if(FullLength > Length)
			AndroidPublishReport(pBuffer, FullLength);
	}

	AndroidForwardFatalSignal(Signal, pInfo);
}

void crashdump_init_if_available(const char *log_file_path)
{
	if(gs_AndroidHandlersInstalled || !log_file_path || !log_file_path[0])
		return;

	if(!crash_output::MakeSidecarPath(log_file_path, "_fatal_report.txt", gs_aAndroidReportPath, sizeof(gs_aAndroidReportPath)) ||
		!crash_output::AppendPathSuffix(gs_aAndroidReportPath, ".tmp", gs_aAndroidCandidatePath, sizeof(gs_aAndroidCandidatePath)) ||
		!crash_output::AppendPathSuffix(gs_aAndroidReportPath, ".pending", gs_aAndroidPendingPath, sizeof(gs_aAndroidPendingPath)))
	{
		log_warn("crashdump", "crash output paths are invalid or exceed the path budget; file output disabled");
		return;
	}

	str_timestamp(gs_aAndroidStartTime, sizeof(gs_aAndroidStartTime));
	gs_NumAndroidModules = 0;
	dl_iterate_phdr(AndroidAddModule, nullptr);

	stack_t AltStack = {};
	if(sigaltstack(nullptr, &AltStack) != 0)
		log_warn("crashdump", "failed to query the alternate signal stack");
	else if(AltStack.ss_flags & SS_DISABLE)
	{
		static char s_aAltStack[64 * 1024];
		AltStack = {};
		AltStack.ss_sp = s_aAltStack;
		AltStack.ss_size = sizeof(s_aAltStack);
		if(sigaltstack(&AltStack, nullptr) != 0)
			log_warn("crashdump", "failed to install the alternate signal stack");
	}

	bool aPreviousRead[std::size(gs_aAndroidFatalSignals)];
	for(size_t i = 0; i < std::size(gs_aAndroidFatalSignals); ++i)
		aPreviousRead[i] = sigaction(gs_aAndroidFatalSignals[i], nullptr, &gs_aAndroidPreviousActions[i]) == 0;
	gs_AndroidReportInProgress.clear(std::memory_order_release);

	struct sigaction Action = {};
	Action.sa_sigaction = AndroidFatalSignalHandler;
	sigemptyset(&Action.sa_mask);
	Action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
	for(size_t i = 0; i < std::size(gs_aAndroidFatalSignals); ++i)
	{
		const int Signal = gs_aAndroidFatalSignals[i];
		if(!aPreviousRead[i] || sigaction(Signal, &Action, nullptr) != 0)
			log_warn("crashdump", "failed to install handler for signal %d", Signal);
		else
			gs_AndroidHandlersInstalled = true;
	}
	if(gs_AndroidHandlersInstalled)
		log_info("crashdump", "fatal signal handlers installed, report path '%s'", gs_aAndroidReportPath);
}

#else

void crashdump_init_if_available(const char *log_file_path)
{
	(void)log_file_path;
}

#endif

#else

#include "crash_output_windows.h"
#include "crashdump_windows.h"
#include "log.h"
#include "windows.h"

#include <dbghelp.h>
#include <process.h>
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
// UTF-8路径在初始化期转换，致命处理器不再按系统ANSI代码页打开文件。
static wchar_t gs_aWideFallbackReportPath[IO_MAX_PATH_LENGTH] = L"";
static wchar_t gs_aWideFallbackDumpPath[IO_MAX_PATH_LENGTH] = L"";
static wchar_t gs_aWideConfirmedPath[IO_MAX_PATH_LENGTH + 16] = L"";
static wchar_t gs_aWideCrashLogPath[IO_MAX_PATH_LENGTH] = L"";
static wchar_t gs_aWideReportCandidatePath[IO_MAX_PATH_LENGTH + 16] = L"";
static wchar_t gs_aWideDumpCandidatePath[IO_MAX_PATH_LENGTH + 16] = L"";
static wchar_t gs_aWideConfirmedCandidatePath[IO_MAX_PATH_LENGTH + 32] = L"";

static HMODULE gs_pDbgHelpLib = nullptr;
static MiniDumpWriteDumpFunc gs_pMiniDumpWriteDump = nullptr;

static std::atomic_flag gs_CrashdumpInitialized = ATOMIC_FLAG_INIT;
static std::atomic<DWORD> gs_FatalOwnerThreadId{0};
static HANDLE gs_pFatalRequestEvent = nullptr;
static HANDLE gs_pFatalFinishedEvent = nullptr;
static unsigned gs_FatalWorkerThreadId = 0;
static constexpr DWORD FATAL_DIAGNOSTICS_TIMEOUT_MS = 10000;

struct SFatalCrashRequest
{
	const char *m_pReason;
	DWORD m_ThreadId;
	int m_SignalNumber;
};
static SFatalCrashRequest gs_FatalRequest;
// 首个致命入口独占固定快照，提交工作前保存现场，不借用故障线程的内存。
static crashdump_windows::CExceptionSnapshot gs_ExceptionSnapshot;

using CNativeCrashFile = crash_output::CWindowsFile;
using CCrashWriter = crash_output::CWriter<CNativeCrashFile>;

static void WriteRaw(CCrashWriter &Output, const char *pText);
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

struct SMiniDumpResult
{
	DWORD m_WrittenFlags = 0;
	DWORD m_ExtendedAttemptError = ERROR_SUCCESS;
	DWORD m_FinalError = ERROR_SUCCESS;
	bool m_RetriedWithMinimalFlags = false;
};

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

static void WriteSection(CCrashWriter &Output, const char *pTitle)
{
	WriteRaw(Output, "\r\n");
	WriteRaw(Output, pTitle);
	WriteRaw(Output, "\r\n");
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

static void WriteMemoryBytes(CCrashWriter &Output, const char *pLabel, const void *pAddress, size_t BytesBefore, size_t BytesAfter)
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
		WriteRaw(Output, aLine);
		return;
	}

	str_format(aLine, sizeof(aLine), "%s: 0x%016llX-0x%016llX\r\n",
		pLabel,
		(unsigned long long)StartAddress,
		(unsigned long long)(StartAddress + BytesRead));
	WriteRaw(Output, aLine);

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
		WriteRaw(Output, aLine);
	}
}

static void WriteExecutablePath(CCrashWriter &Output)
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
	WriteRaw(Output, aLine);
}

static void WriteExceptionModule(CCrashWriter &Output, const void *pAddress)
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
			WriteRaw(Output, aLine);
			str_format(aLine, sizeof(aLine), "Exception module range: 0x%016llX-0x%016llX\r\n",
				(unsigned long long)ModuleBase,
				(unsigned long long)ModuleEnd);
			WriteRaw(Output, aLine);
			if(aModulePath[0] != '\0')
			{
				str_format(aLine, sizeof(aLine), "Exception module path: %s\r\n", aModulePath);
				WriteRaw(Output, aLine);
			}
			Found = true;
			break;
		} while(Module32NextW(SnapshotHandle, &ModuleEntry));
	}

	if(!Found)
		WriteRaw(Output, "Exception module: unresolved\r\n");

	CloseHandle(SnapshotHandle);
}

static void WriteLoadedModules(CCrashWriter &Output)
{
	WriteSection(Output, "Loaded modules:");

	const HANDLE SnapshotHandle = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
	if(SnapshotHandle == INVALID_HANDLE_VALUE)
	{
		WriteRaw(Output, "  <module snapshot unavailable>\r\n");
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
			WriteRaw(Output, aLine);
			++ModuleCount;
		} while(Module32NextW(SnapshotHandle, &ModuleEntry));
	}
	else
	{
		WriteRaw(Output, "  <no modules enumerated>\r\n");
	}

	if(Truncated)
		WriteRaw(Output, "  <module list truncated after 128 entries>\r\n");

	CloseHandle(SnapshotHandle);
}

static void WriteExceptionContext(CCrashWriter &Output, const CONTEXT *pContext, bool IncludeMemory)
{
	if(pContext == nullptr)
		return;

	WriteSection(Output, "CPU context:");

	char aLine[1024];
	str_format(aLine, sizeof(aLine), "Context flags: 0x%08lX\r\n", (unsigned long)pContext->ContextFlags);
	WriteRaw(Output, aLine);

#if defined(CONF_ARCH_AMD64)
	str_format(aLine, sizeof(aLine), "RIP=0x%016llX RSP=0x%016llX RBP=0x%016llX EFLAGS=0x%08lX\r\n",
		(unsigned long long)pContext->Rip,
		(unsigned long long)pContext->Rsp,
		(unsigned long long)pContext->Rbp,
		(unsigned long)pContext->EFlags);
	WriteRaw(Output, aLine);
	str_format(aLine, sizeof(aLine), "RAX=0x%016llX RBX=0x%016llX RCX=0x%016llX RDX=0x%016llX\r\n",
		(unsigned long long)pContext->Rax,
		(unsigned long long)pContext->Rbx,
		(unsigned long long)pContext->Rcx,
		(unsigned long long)pContext->Rdx);
	WriteRaw(Output, aLine);
	str_format(aLine, sizeof(aLine), "RSI=0x%016llX RDI=0x%016llX R8 =0x%016llX R9 =0x%016llX\r\n",
		(unsigned long long)pContext->Rsi,
		(unsigned long long)pContext->Rdi,
		(unsigned long long)pContext->R8,
		(unsigned long long)pContext->R9);
	WriteRaw(Output, aLine);
	str_format(aLine, sizeof(aLine), "R10=0x%016llX R11=0x%016llX R12=0x%016llX R13=0x%016llX\r\n",
		(unsigned long long)pContext->R10,
		(unsigned long long)pContext->R11,
		(unsigned long long)pContext->R12,
		(unsigned long long)pContext->R13);
	WriteRaw(Output, aLine);
	str_format(aLine, sizeof(aLine), "R14=0x%016llX R15=0x%016llX\r\n",
		(unsigned long long)pContext->R14,
		(unsigned long long)pContext->R15);
	WriteRaw(Output, aLine);

	if(IncludeMemory)
	{
		WriteMemoryBytes(Output, "Instruction bytes near RIP", reinterpret_cast<const void *>(pContext->Rip), 32, 64);
		WriteMemoryBytes(Output, "Stack bytes near RSP", reinterpret_cast<const void *>(pContext->Rsp), 0, 128);
	}
#elif defined(CONF_ARCH_IA32)
	str_format(aLine, sizeof(aLine), "EIP=0x%08lX ESP=0x%08lX EBP=0x%08lX EFLAGS=0x%08lX\r\n",
		(unsigned long)pContext->Eip,
		(unsigned long)pContext->Esp,
		(unsigned long)pContext->Ebp,
		(unsigned long)pContext->EFlags);
	WriteRaw(Output, aLine);
	str_format(aLine, sizeof(aLine), "EAX=0x%08lX EBX=0x%08lX ECX=0x%08lX EDX=0x%08lX\r\n",
		(unsigned long)pContext->Eax,
		(unsigned long)pContext->Ebx,
		(unsigned long)pContext->Ecx,
		(unsigned long)pContext->Edx);
	WriteRaw(Output, aLine);
	str_format(aLine, sizeof(aLine), "ESI=0x%08lX EDI=0x%08lX\r\n",
		(unsigned long)pContext->Esi,
		(unsigned long)pContext->Edi);
	WriteRaw(Output, aLine);

	if(IncludeMemory)
	{
		WriteMemoryBytes(Output, "Instruction bytes near EIP", reinterpret_cast<const void *>(pContext->Eip), 32, 64);
		WriteMemoryBytes(Output, "Stack bytes near ESP", reinterpret_cast<const void *>(pContext->Esp), 0, 128);
	}
#else
	WriteRaw(Output, "Register dump: unsupported architecture\r\n");
#endif
}

static void WriteExceptionDetails(CCrashWriter &Output, const EXCEPTION_RECORD *pRecord, bool IncludeDiagnostics)
{
	if(pRecord == nullptr)
		return;

	WriteSection(Output, "Exception details:");

	char aLine[1024];
	const DWORD ExceptionCode = pRecord->ExceptionCode;
	str_format(aLine, sizeof(aLine), "Exception code: 0x%08lX (%s)\r\n",
		(unsigned long)ExceptionCode,
		ExceptionCodeToString(ExceptionCode));
	WriteRaw(Output, aLine);
	str_format(aLine, sizeof(aLine), "Exception flags: 0x%08lX\r\n", (unsigned long)pRecord->ExceptionFlags);
	WriteRaw(Output, aLine);
	str_format(aLine, sizeof(aLine), "Exception address: 0x%p\r\n", pRecord->ExceptionAddress);
	WriteRaw(Output, aLine);
	if(IncludeDiagnostics)
		WriteExceptionModule(Output, pRecord->ExceptionAddress);
	str_format(aLine, sizeof(aLine), "Exception parameters: %lu\r\n", (unsigned long)pRecord->NumberParameters);
	WriteRaw(Output, aLine);

	for(DWORD Index = 0; Index < pRecord->NumberParameters && Index < EXCEPTION_MAXIMUM_PARAMETERS; ++Index)
	{
		str_format(aLine, sizeof(aLine), "  Parameter[%lu]: 0x%016llX\r\n",
			(unsigned long)Index,
			(unsigned long long)pRecord->ExceptionInformation[Index]);
		WriteRaw(Output, aLine);
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
		WriteRaw(Output, aLine);
		str_format(aLine, sizeof(aLine), "Access violation target address: 0x%016llX\r\n",
			(unsigned long long)pRecord->ExceptionInformation[1]);
		WriteRaw(Output, aLine);
		if(IncludeDiagnostics)
			WriteMemoryBytes(Output, "Bytes at fault address", reinterpret_cast<const void *>(pRecord->ExceptionInformation[1]), 0, 64);
	}

	if(ExceptionCode == EXCEPTION_IN_PAGE_ERROR && pRecord->NumberParameters >= 3)
	{
		str_format(aLine, sizeof(aLine), "In-page NTSTATUS: 0x%08lX\r\n", (unsigned long)pRecord->ExceptionInformation[2]);
		WriteRaw(Output, aLine);
	}

	if(pRecord->ExceptionRecord != nullptr)
	{
		str_format(aLine, sizeof(aLine), "Nested exception code: 0x%08lX\r\n", (unsigned long)pRecord->ExceptionRecord->ExceptionCode);
		WriteRaw(Output, aLine);
		str_format(aLine, sizeof(aLine), "Nested exception address: 0x%p\r\n", pRecord->ExceptionRecord->ExceptionAddress);
		WriteRaw(Output, aLine);
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

template<size_t N>
static bool PrepareWideOutputPath(const char *pUtf8, wchar_t (&aWide)[N])
{
	if(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, pUtf8, -1, aWide, static_cast<int>(N)) > 0)
		return true;
	aWide[0] = L'\0';
	return false;
}

template<size_t N>
static bool PrepareCandidateOutputPath(const char *pFinal, wchar_t (&aWide)[N])
{
	char aCandidate[N];
	aWide[0] = L'\0';
	return crash_output::MakeSidecarPath(pFinal, ".tmp", aCandidate, sizeof(aCandidate)) && PrepareWideOutputPath(aCandidate, aWide);
}

static void WriteRaw(CCrashWriter &Output, const char *pText)
{
	Output.WriteText(pText);
}

static bool WriteCrashReport(const SFatalCrashRequest &Request, const SMiniDumpResult *pDumpResult)
{
	if(gs_aWideReportCandidatePath[0] == L'\0')
		return false;

	HANDLE FileHandle = CreateFileW(gs_aWideReportCandidatePath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(FileHandle == INVALID_HANDLE_VALUE)
		return false;

	const bool DumpWritten = pDumpResult && pDumpResult->m_WrittenFlags != 0;
	CNativeCrashFile File(FileHandle);
	CCrashWriter Output(File);
	char aLine[2048];
	SYSTEMTIME LocalTime{};
	GetLocalTime(&LocalTime);

	WriteRaw(Output, "QmClient fatal error report\r\n");
	WriteRaw(Output, "Report type: fatal-crash\r\n");
	str_format(aLine, sizeof(aLine), "Timestamp: %04u-%02u-%02u %02u:%02u:%02u.%03u\r\n",
		(unsigned)LocalTime.wYear,
		(unsigned)LocalTime.wMonth,
		(unsigned)LocalTime.wDay,
		(unsigned)LocalTime.wHour,
		(unsigned)LocalTime.wMinute,
		(unsigned)LocalTime.wSecond,
		(unsigned)LocalTime.wMilliseconds);
	WriteRaw(Output, aLine);
	str_format(aLine, sizeof(aLine), "Reason: %s\r\n", Request.m_pReason);
	WriteRaw(Output, aLine);
	str_format(aLine, sizeof(aLine), "Process ID: %lu\r\n", (unsigned long)GetCurrentProcessId());
	WriteRaw(Output, aLine);
	str_format(aLine, sizeof(aLine), "Thread ID: %lu\r\n", (unsigned long)Request.m_ThreadId);
	WriteRaw(Output, aLine);
	if(pDumpResult)
		WriteExecutablePath(Output);
	str_format(aLine, sizeof(aLine), "Crash log path (.RTP): %s\r\n", gs_aCrashLogPath);
	WriteRaw(Output, aLine);
	str_format(aLine, sizeof(aLine), "Fallback minidump path: %s\r\n", gs_aFallbackDumpPath);
	WriteRaw(Output, aLine);
	if(pDumpResult)
	{
		str_format(aLine, sizeof(aLine), "Fallback minidump written: %s\r\n", DumpWritten ? "yes" : "no");
		WriteRaw(Output, aLine);
		str_format(aLine, sizeof(aLine), "Requested extended minidump flags: 0x%08lX\r\n", (unsigned long)gs_ExtendedDumpType);
		WriteRaw(Output, aLine);
		str_format(aLine, sizeof(aLine), "Fallback to minimal minidump flags: %s\r\n", pDumpResult->m_RetriedWithMinimalFlags ? "yes" : "no");
		WriteRaw(Output, aLine);
		str_format(aLine, sizeof(aLine), "Written minidump flags: 0x%08lX\r\n", (unsigned long)pDumpResult->m_WrittenFlags);
		WriteRaw(Output, aLine);
	}
	else
	{
		WriteRaw(Output, "Minidump and extended diagnostics had not completed when this report was saved.\r\n");
	}
	if(const char *pGraphicsBackend = crashdump_graphics_backend_for_report())
	{
		str_format(aLine, sizeof(aLine), "Graphics backend: %s\r\n", pGraphicsBackend);
		WriteRaw(Output, aLine);
	}
	if(pDumpResult)
	{
		if(pDumpResult->m_RetriedWithMinimalFlags)
		{
			str_format(aLine, sizeof(aLine), "Extended minidump attempt error: 0x%08lX\r\n", (unsigned long)pDumpResult->m_ExtendedAttemptError);
			WriteRaw(Output, aLine);
		}
		if(!DumpWritten)
		{
			str_format(aLine, sizeof(aLine), "Final minidump error: 0x%08lX\r\n", (unsigned long)pDumpResult->m_FinalError);
			WriteRaw(Output, aLine);
		}
	}

	if(Request.m_SignalNumber != 0)
	{
		str_format(aLine, sizeof(aLine), "Signal: %d (%s)\r\n", Request.m_SignalNumber, SignalToString(Request.m_SignalNumber));
		WriteRaw(Output, aLine);
	}

	WriteExceptionDetails(Output, gs_ExceptionSnapshot.Record(), pDumpResult != nullptr);
	WriteExceptionContext(Output, gs_ExceptionSnapshot.Context(), pDumpResult != nullptr);
	if(pDumpResult)
		WriteLoadedModules(Output);

	return Output.Finish() && crash_output::PublishWindowsOutput(gs_aWideReportCandidatePath, gs_aWideFallbackReportPath);
}

enum class EMiniDumpAttempt
{
	WRITTEN,
	DUMP_FAILED,
	OUTPUT_FAILED
};

static EMiniDumpAttempt TryWriteMiniDumpFile(const wchar_t *pFilename, DWORD ThreadId, EXCEPTION_POINTERS *pExceptionPointers, MINIDUMP_TYPE DumpType, DWORD &Error)
{
	if(gs_pMiniDumpWriteDump == nullptr || pFilename[0] == L'\0')
	{
		Error = gs_pMiniDumpWriteDump == nullptr ? ERROR_PROC_NOT_FOUND : ERROR_INVALID_PARAMETER;
		return EMiniDumpAttempt::OUTPUT_FAILED;
	}

	HANDLE FileHandle = CreateFileW(gs_aWideDumpCandidatePath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(FileHandle == INVALID_HANDLE_VALUE)
	{
		Error = GetLastError();
		return EMiniDumpAttempt::OUTPUT_FAILED;
	}

	CNativeCrashFile File(FileHandle);
	CCrashWriter Output(File);
	MINIDUMP_EXCEPTION_INFORMATION ExceptionInfo{};
	PMINIDUMP_EXCEPTION_INFORMATION pExceptionInfo = nullptr;
	if(pExceptionPointers != nullptr)
	{
		ExceptionInfo.ThreadId = ThreadId;
		ExceptionInfo.ExceptionPointers = pExceptionPointers;
		ExceptionInfo.ClientPointers = FALSE;
		pExceptionInfo = &ExceptionInfo;
	}

	const BOOL Result = gs_pMiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), FileHandle, DumpType, pExceptionInfo, nullptr, nullptr);
	if(!Result)
		File.Fail(GetLastError());
	const bool Completed = Output.Finish(Result != FALSE);
	if(Completed && !crash_output::PublishWindowsOutput(gs_aWideDumpCandidatePath, pFilename))
		File.Fail(GetLastError(), CNativeCrashFile::EOperation::PUBLISH);
	Error = File.Error();
	if(!Result)
		return EMiniDumpAttempt::DUMP_FAILED;
	return Error == ERROR_SUCCESS ? EMiniDumpAttempt::WRITTEN : EMiniDumpAttempt::OUTPUT_FAILED;
}

static SMiniDumpResult WriteMiniDumpFile(DWORD ThreadId, EXCEPTION_POINTERS *pExceptionPointers)
{
	SMiniDumpResult Result;
	const crashdump_windows::CMiniDumpWriteGuard DumpGuard;
	if(!DumpGuard.Acquired())
	{
		Result.m_FinalError = ERROR_BUSY;
		return Result;
	}

	const auto Attempt = TryWriteMiniDumpFile(gs_aWideFallbackDumpPath, ThreadId, pExceptionPointers, gs_ExtendedDumpType, Result.m_ExtendedAttemptError);
	if(Attempt == EMiniDumpAttempt::WRITTEN)
	{
		Result.m_WrittenFlags = static_cast<DWORD>(gs_ExtendedDumpType);
		return Result;
	}
	if(Attempt == EMiniDumpAttempt::OUTPUT_FAILED)
	{
		// 更换转储选项无法修复文件输出错误。保留可能已经完整写好的候选。
		Result.m_FinalError = Result.m_ExtendedAttemptError;
		return Result;
	}

	Result.m_RetriedWithMinimalFlags = true;
	if(TryWriteMiniDumpFile(gs_aWideFallbackDumpPath, ThreadId, pExceptionPointers, gs_MinimalDumpType, Result.m_FinalError) == EMiniDumpAttempt::WRITTEN)
		Result.m_WrittenFlags = static_cast<DWORD>(gs_MinimalDumpType);
	return Result;
}

// QmClient: 判断异常地址是否落在已知图形驱动模块内。
// 只在退出阶段使用：退出清理销毁图形后端时，个别驱动会在这类模块内部崩溃，
// 此时进程本就即将结束，写完整转储与弹窗只会打扰用户。
static bool IsShutdownGraphicsFault(const EXCEPTION_RECORD *pRecord)
{
	if(!gs_ShutdownInProgress.load(std::memory_order_acquire))
		return false;
	if(pRecord == nullptr)
		return false;

	const void *pFaultAddress = pRecord->ExceptionAddress;
	if(pFaultAddress == nullptr)
		return false;

	HMODULE hModule = nullptr;
	if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
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

static void LaunchFatalCrashReporter()
{
	if(gs_SuppressNextFatalReporter.exchange(false, std::memory_order_acq_rel))
		return;

	if(gs_aWideConfirmedPath[0] == L'\0' || gs_aWideConfirmedCandidatePath[0] == L'\0')
		return;
	HANDLE ConfirmedFile = CreateFileW(gs_aWideConfirmedCandidatePath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(ConfirmedFile == INVALID_HANDLE_VALUE)
		return;
	CNativeCrashFile File(ConfirmedFile);
	CCrashWriter Output(File);
	WriteRaw(Output, "confirmed fatal crash\r\n");
	if(!Output.Finish() || !crash_output::PublishWindowsOutput(gs_aWideConfirmedCandidatePath, gs_aWideConfirmedPath))
		return;
	crashdump_launch_reporter_if_available(gs_aFallbackReportPath);
}

static unsigned __stdcall FatalCrashWorker(void *)
{
	if(WaitForSingleObject(gs_pFatalRequestEvent, INFINITE) != WAIT_OBJECT_0)
		return 0;

	const SFatalCrashRequest &Request = gs_FatalRequest;
	if(IsShutdownGraphicsFault(gs_ExceptionSnapshot.Record()))
	{
		gs_ShutdownGraphicsFault.store(true, std::memory_order_release);
		log_warn("crashdump", "ignoring graphics driver fault '%s' during shutdown; process is already exiting", Request.m_pReason);
	}
	else
	{
		// DbgHelp 和模块枚举可能在故障进程内阻塞。先提交能供下次启动读取的现场。
		const bool BasicReportWritten = WriteCrashReport(Request, nullptr);
		const SMiniDumpResult DumpResult = WriteMiniDumpFile(Request.m_ThreadId, gs_ExceptionSnapshot.PointersForDump());
		const bool FullReportWritten = WriteCrashReport(Request, &DumpResult);
		if((FullReportWritten || BasicReportWritten) && Request.m_SignalNumber == 0)
			LaunchFatalCrashReporter();
	}

	SetEvent(gs_pFatalFinishedEvent);
	return 0;
}

static bool InitFatalCrashWorker()
{
	gs_pFatalRequestEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if(!gs_pFatalRequestEvent)
		return false;

	gs_pFatalFinishedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if(gs_pFatalFinishedEvent)
	{
		const uintptr_t Thread = _beginthreadex(nullptr, 0, FatalCrashWorker, nullptr, 0, &gs_FatalWorkerThreadId);
		if(Thread != 0)
		{
			// 处理器、事件和等待请求的线程都随进程存活。这里只释放创建线程时的句柄。
			CloseHandle(reinterpret_cast<HANDLE>(Thread));
			return true;
		}
		CloseHandle(gs_pFatalFinishedEvent);
		gs_pFatalFinishedEvent = nullptr;
	}
	CloseHandle(gs_pFatalRequestEvent);
	gs_pFatalRequestEvent = nullptr;
	return false;
}

static bool HandleFatalCrash(const char *pReason, EXCEPTION_POINTERS *pExceptionPointers, int SignalNumber)
{
	const DWORD ThreadId = GetCurrentThreadId();
	DWORD Owner = 0;
	if(!gs_FatalOwnerThreadId.compare_exchange_strong(Owner, ThreadId, std::memory_order_relaxed))
	{
		// 其他故障线程给首个报告留出完成时间。故障线程或采集线程重入时不能等待自己。
		return gs_pFatalFinishedEvent && ThreadId != Owner && ThreadId != gs_FatalWorkerThreadId &&
		       WaitForSingleObject(gs_pFatalFinishedEvent, FATAL_DIAGNOSTICS_TIMEOUT_MS) == WAIT_OBJECT_0;
	}

	gs_ExceptionSnapshot.Capture(pExceptionPointers);
	gs_FatalRequest = {pReason, ThreadId, SignalNumber};
	if(!gs_pFatalRequestEvent)
	{
		if(!gs_ShutdownInProgress.load(std::memory_order_acquire))
			WriteCrashReport(gs_FatalRequest, nullptr);
		return true;
	}

	return SetEvent(gs_pFatalRequestEvent) &&
	       WaitForSingleObject(gs_pFatalFinishedEvent, FATAL_DIAGNOSTICS_TIMEOUT_MS) == WAIT_OBJECT_0;
}

[[noreturn]] static void TerminateFromFatalHandler()
{
	TerminateProcess(GetCurrentProcess(), 3);
	ExitProcess(3);
}

static LONG WINAPI FallbackUnhandledExceptionFilter(EXCEPTION_POINTERS *pExceptionPointers)
{
	if(!HandleFatalCrash("Unhandled structured exception", pExceptionPointers, 0))
		TerminateFromFatalHandler();
	// 旧过滤器可能进入 exchndl 的在线符号下载，延长已崩溃进程的退出。
	return EXCEPTION_EXECUTE_HANDLER;
}

static void FallbackTerminateHandler()
{
	HandleFatalCrash("Unhandled C++ exception (std::terminate)", nullptr, 0);
	TerminateFromFatalHandler();
}

static void FallbackSignalHandler(int SignalNumber)
{
	// 报告器不在 SIGABRT 入口启动，已写入的文本留给下次启动。
	std::signal(SignalNumber, SIG_DFL);
	if(HandleFatalCrash("Fatal signal", nullptr, SignalNumber))
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

static void InstallUnhandledExceptionFilter()
{
	SetUnhandledExceptionFilter(FallbackUnhandledExceptionFilter);
}

void crashdump_init_if_available(const char *log_file_path)
{
	// 路径和工作线程在启动时发布一次，安装过滤器后保持可供任何线程读取。
	if(gs_CrashdumpInitialized.test_and_set(std::memory_order_relaxed))
		return;

	char aConfirmedPath[IO_MAX_PATH_LENGTH + 16];
	const bool PathsReady = log_file_path &&
				crash_output::AppendPathSuffix(log_file_path, {}, gs_aCrashLogPath, sizeof(gs_aCrashLogPath)) &&
				crash_output::MakeSidecarPath(gs_aCrashLogPath, "_fatal_report.txt", gs_aFallbackReportPath, sizeof(gs_aFallbackReportPath)) &&
				crash_output::MakeSidecarPath(gs_aCrashLogPath, "_fatal_dump.dmp", gs_aFallbackDumpPath, sizeof(gs_aFallbackDumpPath)) &&
				crash_output::AppendPathSuffix(gs_aFallbackReportPath, ".confirmed", aConfirmedPath, sizeof(aConfirmedPath)) &&
				PrepareWideOutputPath(gs_aCrashLogPath, gs_aWideCrashLogPath) &&
				PrepareWideOutputPath(gs_aFallbackReportPath, gs_aWideFallbackReportPath) &&
				PrepareWideOutputPath(gs_aFallbackDumpPath, gs_aWideFallbackDumpPath) &&
				PrepareWideOutputPath(aConfirmedPath, gs_aWideConfirmedPath) &&
				PrepareCandidateOutputPath(gs_aFallbackReportPath, gs_aWideReportCandidatePath) &&
				PrepareCandidateOutputPath(gs_aFallbackDumpPath, gs_aWideDumpCandidatePath) &&
				PrepareCandidateOutputPath(aConfirmedPath, gs_aWideConfirmedCandidatePath);
	if(!PathsReady)
	{
		// 全部路径共同准备，失败不保留任何部分结果或截断别名。
		gs_aCrashLogPath[0] = gs_aFallbackReportPath[0] = gs_aFallbackDumpPath[0] = '\0';
		wchar_t *apPaths[] = {gs_aWideCrashLogPath, gs_aWideFallbackReportPath, gs_aWideFallbackDumpPath, gs_aWideConfirmedPath,
			gs_aWideReportCandidatePath, gs_aWideDumpCandidatePath, gs_aWideConfirmedCandidatePath};
		for(wchar_t *pPath : apPaths)
			pPath[0] = L'\0';
		log_warn("crashdump", "crash output paths are invalid or exceed the path budget; file output disabled");
	}
	if(gs_aCrashLogPath[0] != '\0')
		fs_makedir_rec_for(gs_aCrashLogPath);
	if(gs_aFallbackReportPath[0] != '\0')
		fs_makedir_rec_for(gs_aFallbackReportPath);
	if(gs_aFallbackDumpPath[0] != '\0')
		fs_makedir_rec_for(gs_aFallbackDumpPath);

	if(PathsReady)
	{
		InitMiniDumpWriterIfNeeded();
		ULONG StackGuarantee = 32 * 1024;
		if(!SetThreadStackGuarantee(&StackGuarantee))
			log_warn("crashdump", "failed to reserve exception stack space for the startup thread");
		if(!InitFatalCrashWorker())
			log_warn("crashdump", "failed to start fatal diagnostics worker; only a basic fatal report will be attempted");
	}
	// 恢复成功的 SEH 不占用报告名额。这里只安装 C++ 终止与致命信号入口。
	std::set_terminate(FallbackTerminateHandler);
	if(std::signal(SIGABRT, FallbackSignalHandler) == SIG_ERR)
		log_warn("crashdump", "failed to install the fatal signal handler");
	if(!PathsReady)
	{
		InstallUnhandledExceptionFilter();
		return;
	}

	HMODULE pCrashdumpLib = LoadLibraryA(CRASHDUMP_LIB);
	if(pCrashdumpLib == nullptr)
	{
		const DWORD LastError = GetLastError();
		const std::string ErrorMsg = windows_format_system_message(LastError);
		log_warn("crashdump", "failed to load crashdump library '%s' (error %ld %s), using fallback crash handlers only", CRASHDUMP_LIB, LastError, ErrorMsg.c_str());
		InstallUnhandledExceptionFilter();
		return;
	}

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

	ExceptionLogFilePathFunc(gs_aWideCrashLogPath);

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
