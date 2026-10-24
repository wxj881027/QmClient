#include "color.h"
#include "dbg.h"
#include "logger.h"
#include "logger_async.h"
#include "str.h"
#include "system.h"
#include "time.h"
#include "windows.h"
#include "windows_logger.h"

#include <atomic>
#include <cstdio>
#include <memory>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#if defined(CONF_PLATFORM_ANDROID)
#include <android/log.h>
#endif

std::atomic<ILogger *> global_logger = nullptr;
thread_local ILogger *scope_logger = nullptr;
thread_local bool in_logger = false;

void log_set_global_logger(ILogger *logger)
{
	ILogger *null = nullptr;
	if(!global_logger.compare_exchange_strong(null, logger, std::memory_order_acq_rel))
	{
		dbg_assert_failed("global logger has already been set and can only be set once");
	}
#if !defined(CONF_PLATFORM_EMSCRIPTEN)
	// With Emscripten, when atexit calls log_global_logger_finish, which calls GlobalFinish, the
	// Emscripten runtime has already exited, so we cannot wait for the AIO thread to finish and
	// calling io_sync in the assertion logger is not possible. We instead finish the global logger
	// manually in src/engine/client/client.cpp before exit.
	atexit(log_global_logger_finish);
#endif
}

void log_global_logger_finish()
{
	ILogger *logger = global_logger.load(std::memory_order_acquire);
	if(logger)
		logger->GlobalFinish();
}

void log_set_global_logger_default()
{
	std::unique_ptr<ILogger> logger;
#if defined(CONF_PLATFORM_ANDROID)
	logger = log_logger_android();
#else
	logger = log_logger_stdout();
#endif
	if(logger)
	{
		log_set_global_logger(logger.release());
	}
}

ILogger *log_get_scope_logger()
{
	if(!scope_logger)
	{
		scope_logger = global_logger.load(std::memory_order_acquire);
	}
	return scope_logger;
}

void log_set_scope_logger(ILogger *logger)
{
	scope_logger = logger;
	if(!scope_logger)
	{
		scope_logger = global_logger.load(std::memory_order_acquire);
	}
}

[[gnu::format(printf, 5, 0)]] static void log_log_impl(LEVEL level, bool have_color, LOG_COLOR color, const char *sys, const char *fmt, va_list args)
{
	// Make sure we're not logging recursively.
	if(in_logger)
	{
		return;
	}
	in_logger = true;
	if(!scope_logger)
	{
		scope_logger = global_logger.load(std::memory_order_acquire);
	}
	if(!scope_logger)
	{
		in_logger = false;
		return;
	}

	CLogMessage Msg;
	Msg.m_Level = level;
	Msg.m_HaveColor = have_color;
	Msg.m_Color = color;
	str_timestamp_format(Msg.m_aTimestamp, sizeof(Msg.m_aTimestamp), FORMAT_SPACE);
	Msg.m_TimestampLength = str_length(Msg.m_aTimestamp);
	str_copy(Msg.m_aSystem, sys);
	Msg.m_SystemLength = str_length(Msg.m_aSystem);

	str_format(Msg.m_aLine, sizeof(Msg.m_aLine), "%s %c %s: ", Msg.m_aTimestamp, "EWIDT"[level], Msg.m_aSystem);
	Msg.m_LineMessageOffset = str_length(Msg.m_aLine);

	char *pMessage = Msg.m_aLine + Msg.m_LineMessageOffset;
	int MessageSize = sizeof(Msg.m_aLine) - Msg.m_LineMessageOffset;
	str_format_v(pMessage, MessageSize, fmt, args);
	Msg.m_LineLength = str_length(Msg.m_aLine);
	try
	{
		scope_logger->Log(&Msg);
	}
	catch(...)
	{
		// 后端分配或写入异常不能打断业务；这里不能再递归记录失败。
		// 仍恢复递归标记，让下一条日志可以再次访问后端。
	}
	in_logger = false;
}

void log_log_v(LEVEL level, const char *sys, const char *fmt, va_list args)
{
	log_log_impl(level, false, LOG_COLOR{0, 0, 0}, sys, fmt, args);
}

void log_log(LEVEL level, const char *sys, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	log_log_impl(level, false, LOG_COLOR{0, 0, 0}, sys, fmt, args);
	va_end(args);
}

void log_log_color_v(LEVEL level, LOG_COLOR color, const char *sys, const char *fmt, va_list args)
{
	log_log_impl(level, true, color, sys, fmt, args);
}

void log_log_color(LEVEL level, LOG_COLOR color, const char *sys, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	log_log_impl(level, true, color, sys, fmt, args);
	va_end(args);
}

bool CLogFilter::Filters(const CLogMessage *pMessage)
{
	return pMessage->m_Level > m_MaxLevel.load(std::memory_order_relaxed);
}

#if defined(CONF_PLATFORM_ANDROID)
class CLoggerAndroid : public ILogger
{
public:
	void Log(const CLogMessage *pMessage) override
	{
		if(m_Filter.Filters(pMessage))
		{
			return;
		}
		int AndroidLevel;
		switch(pMessage->m_Level)
		{
		case LEVEL_TRACE: AndroidLevel = ANDROID_LOG_VERBOSE; break;
		case LEVEL_DEBUG: AndroidLevel = ANDROID_LOG_DEBUG; break;
		case LEVEL_INFO: AndroidLevel = ANDROID_LOG_INFO; break;
		case LEVEL_WARN: AndroidLevel = ANDROID_LOG_WARN; break;
		case LEVEL_ERROR: AndroidLevel = ANDROID_LOG_ERROR; break;
		}
		char aTag[64];
		str_copy(aTag, ANDROID_PACKAGE_NAME "/");
		str_append(aTag, pMessage->m_aSystem);
		__android_log_write(AndroidLevel, aTag, pMessage->Message());
	}
};
std::unique_ptr<ILogger> log_logger_android()
{
	return std::make_unique<CLoggerAndroid>();
}
#else
std::unique_ptr<ILogger> log_logger_android()
{
	dbg_assert(0, "Android logger on non-Android");
	return nullptr;
}
#endif

class CLoggerCollection : public ILogger
{
	std::vector<std::shared_ptr<ILogger>> m_vpLoggers;

public:
	CLoggerCollection(std::vector<std::shared_ptr<ILogger>> &&vpLoggers) :
		m_vpLoggers(std::move(vpLoggers))
	{
		m_Filter.m_MaxLevel.store(LEVEL_TRACE, std::memory_order_relaxed);
	}
	void Log(const CLogMessage *pMessage) override
	{
		if(m_Filter.Filters(pMessage))
		{
			return;
		}
		for(auto &pLogger : m_vpLoggers)
		{
			pLogger->Log(pMessage);
		}
	}
	void GlobalFinish() override
	{
		for(auto &pLogger : m_vpLoggers)
		{
			pLogger->GlobalFinish();
		}
	}
};

std::unique_ptr<ILogger> log_logger_collection(std::vector<std::shared_ptr<ILogger>> &&vpLoggers)
{
	return std::make_unique<CLoggerCollection>(std::move(vpLoggers));
}

class CPrefixRouterLogger : public ILogger
{
	std::shared_ptr<ILogger> m_pPrefixLogger;
	std::shared_ptr<ILogger> m_pFallbackLogger;
	char m_aSystemPrefix[32];

public:
	CPrefixRouterLogger(std::shared_ptr<ILogger> pPrefixLogger, std::shared_ptr<ILogger> pFallbackLogger, const char *pSystemPrefix) :
		m_pPrefixLogger(std::move(pPrefixLogger)),
		m_pFallbackLogger(std::move(pFallbackLogger))
	{
		m_Filter.m_MaxLevel.store(LEVEL_TRACE, std::memory_order_relaxed);
		str_copy(m_aSystemPrefix, pSystemPrefix);
	}
	void Log(const CLogMessage *pMessage) override
	{
		if(m_Filter.Filters(pMessage))
			return;
		ILogger *pLogger = str_startswith(pMessage->m_aSystem, m_aSystemPrefix) ? m_pPrefixLogger.get() : m_pFallbackLogger.get();
		if(pLogger)
			pLogger->Log(pMessage);
	}
	void GlobalFinish() override
	{
		if(m_pPrefixLogger)
			m_pPrefixLogger->GlobalFinish();
		if(m_pFallbackLogger)
			m_pFallbackLogger->GlobalFinish();
	}
};

std::unique_ptr<ILogger> log_logger_prefix_router(std::shared_ptr<ILogger> pPrefixLogger, std::shared_ptr<ILogger> pFallbackLogger, const char *pSystemPrefix)
{
	return std::make_unique<CPrefixRouterLogger>(std::move(pPrefixLogger), std::move(pFallbackLogger), pSystemPrefix);
}

CLoggerAsync::CLoggerAsync(IOHANDLE File, bool AnsiTruecolor, bool Close, ASYNCIO *(*Create)(IOHANDLE)) :
	m_pAio((Create ? Create : aio_new)(File)),
	m_AnsiTruecolor(AnsiTruecolor),
	m_Close(Close)
{
	// AIO 创建失败时尚未接管文件；标准输出等借用句柄仍由调用方持有。
	if(!m_pAio && m_Close)
		io_close(File);
}

void CLoggerAsync::Log(const CLogMessage *pMessage)
{
	if(!m_pAio || m_Filter.Filters(pMessage))
		return;
	aio_lock(m_pAio);
	if(m_AnsiTruecolor && pMessage->m_HaveColor)
	{
		// https://en.wikipedia.org/w/index.php?title=ANSI_escape_code&oldid=1077146479#24-bit
		char aAnsi[32];
		str_format(aAnsi, sizeof(aAnsi),
			"\x1b[38;2;%d;%d;%dm",
			pMessage->m_Color.r,
			pMessage->m_Color.g,
			pMessage->m_Color.b);
		aio_write_unlocked(m_pAio, aAnsi, str_length(aAnsi));
	}
	aio_write_unlocked(m_pAio, pMessage->m_aLine, pMessage->m_LineLength);
	if(m_AnsiTruecolor && pMessage->m_HaveColor)
	{
		const char aResetColor[] = "\x1b[0m";
		aio_write_unlocked(m_pAio, aResetColor, str_length(aResetColor)); // reset
	}
	aio_write_newline_unlocked(m_pAio);
	aio_unlock(m_pAio);
}

CLoggerAsync::~CLoggerAsync()
{
	GlobalFinish();
	if(m_pAio)
		aio_free(m_pAio);
}

void CLoggerAsync::GlobalFinish()
{
	if(!m_pAio)
		return;
	// 共享日志路由可能多次结束同一个后端；并发调用也等待同一次刷盘完成。
	std::call_once(m_FinishOnce, [this] {
		if(m_Close)
			aio_close(m_pAio);
		aio_wait(m_pAio);
	});
}

std::unique_ptr<ILogger> log_logger_file(IOHANDLE logfile)
{
	return CLoggerAsync::CreateOwned(logfile, [](IOHANDLE File) { return std::make_unique<CLoggerAsync>(File, false, true); });
}

class CPrefixLogger : public ILogger
{
	std::unique_ptr<ILogger> m_pLogger;
	char m_aSystemPrefix[32];

public:
	CPrefixLogger(std::unique_ptr<ILogger> pLogger, const char *pSystemPrefix) :
		m_pLogger(std::move(pLogger))
	{
		str_copy(m_aSystemPrefix, pSystemPrefix);
	}
	bool IsAvailable() const noexcept override { return m_pLogger->IsAvailable(); }
	void Log(const CLogMessage *pMessage) override
	{
		if(m_Filter.Filters(pMessage) || !str_startswith(pMessage->m_aSystem, m_aSystemPrefix))
			return;
		m_pLogger->Log(pMessage);
	}
	void GlobalFinish() override
	{
		m_pLogger->GlobalFinish();
	}
	void OnFilterChange() override
	{
		m_pLogger->SetFilter(m_Filter);
	}
};

std::unique_ptr<ILogger> log_logger_prefix_file(IOHANDLE logfile, const char *pSystemPrefix)
{
	return log_logger_prefix(log_logger_file(logfile), pSystemPrefix);
}

std::unique_ptr<ILogger> log_logger_prefix(std::unique_ptr<ILogger> pLogger, const char *pSystemPrefix)
{
	if(!pLogger)
		return nullptr;
	return std::make_unique<CPrefixLogger>(std::move(pLogger), pSystemPrefix);
}

#if defined(CONF_FAMILY_WINDOWS)
static int color_hsv_to_windows_console_color(const ColorHSVA &Hsv)
{
	int h = Hsv.h * 255.0f;
	int s = Hsv.s * 255.0f;
	int v = Hsv.v * 255.0f;
	if(s >= 0 && s <= 10)
	{
		if(v <= 150)
			return FOREGROUND_INTENSITY;
		return FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY;
	}
	if(h < 0)
		return FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY;
	else if(h < 15)
		return FOREGROUND_RED | FOREGROUND_INTENSITY;
	else if(h < 30)
		return FOREGROUND_GREEN | FOREGROUND_RED;
	else if(h < 60)
		return FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY;
	else if(h < 110)
		return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
	else if(h < 140)
		return FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
	else if(h < 170)
		return FOREGROUND_BLUE | FOREGROUND_INTENSITY;
	else if(h < 195)
		return FOREGROUND_BLUE | FOREGROUND_RED;
	else if(h < 240)
		return FOREGROUND_BLUE | FOREGROUND_RED | FOREGROUND_INTENSITY;
	else
		return FOREGROUND_RED | FOREGROUND_INTENSITY;
}

class CWindowsConsoleLogger : public ILogger
{
	HANDLE m_pConsole;
	bool m_EnableColor;
	int m_BackgroundColor;
	int m_ForegroundColor;
	CLock m_OutputLock;
	bool m_Finished = false;

public:
	CWindowsConsoleLogger(HANDLE pConsole, bool EnableColor) :
		m_pConsole(pConsole),
		m_EnableColor(EnableColor)
	{
		CONSOLE_SCREEN_BUFFER_INFO ConsoleInfo;
		if(GetConsoleScreenBufferInfo(pConsole, &ConsoleInfo))
		{
			m_BackgroundColor = ConsoleInfo.wAttributes & (BACKGROUND_BLUE | BACKGROUND_GREEN | BACKGROUND_RED | BACKGROUND_INTENSITY);
			m_ForegroundColor = ConsoleInfo.wAttributes & (FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY);
		}
		else
		{
			m_BackgroundColor = 0;
			m_ForegroundColor = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY;
		}
	}
	void Log(const CLogMessage *pMessage) override REQUIRES(!m_OutputLock)
	{
		if(m_Filter.Filters(pMessage))
		{
			return;
		}
		const std::wstring WideMessage = windows_utf8_to_wide(pMessage->m_aLine);

		int Color = m_BackgroundColor;
		if(m_EnableColor && pMessage->m_HaveColor)
		{
			const ColorRGBA Rgba(pMessage->m_Color.r / 255.0f, pMessage->m_Color.g / 255.0f, pMessage->m_Color.b / 255.0f);
			Color |= color_hsv_to_windows_console_color(color_cast<ColorHSVA>(Rgba));
		}
		else
			Color |= FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY;

		const CLockScope LockScope(m_OutputLock);
		if(!m_Finished)
		{
			SetConsoleTextAttribute(m_pConsole, Color);
			WriteConsoleW(m_pConsole, WideMessage.c_str(), WideMessage.length(), nullptr, nullptr);
			WriteConsoleW(m_pConsole, L"\r\n", 2, nullptr, nullptr);
		}
	}
	void GlobalFinish() override REQUIRES(!m_OutputLock)
	{
		// Restore original color
		const CLockScope LockScope(m_OutputLock);
		SetConsoleTextAttribute(m_pConsole, m_BackgroundColor | m_ForegroundColor);
		m_Finished = true;
	}
};

intptr_t CWindowsStdoutOperations::OutputHandle() noexcept { return reinterpret_cast<intptr_t>(GetStdHandle(STD_OUTPUT_HANDLE)); }
unsigned long CWindowsStdoutOperations::OutputType(intptr_t Handle) noexcept { return GetFileType(reinterpret_cast<HANDLE>(Handle)); }
void CWindowsStdoutOperations::AttachParentConsole() noexcept { AttachConsole(ATTACH_PARENT_PROCESS); }
bool CWindowsStdoutOperations::ReadConsoleMode(intptr_t Handle, unsigned long *pMode) noexcept { return GetConsoleMode(reinterpret_cast<HANDLE>(Handle), pMode) != 0; }
bool CWindowsStdoutOperations::WriteConsoleMode(intptr_t Handle, unsigned long Mode) noexcept { return SetConsoleMode(reinterpret_cast<HANDLE>(Handle), Mode) != 0; }
unsigned int CWindowsStdoutOperations::OutputCodePage() noexcept { return GetConsoleOutputCP(); }
bool CWindowsStdoutOperations::SetOutputCodePage(unsigned int CodePage) noexcept { return SetConsoleOutputCP(CodePage) != 0; }
bool CWindowsStdoutOperations::ColorsEnabled() noexcept { return _wgetenv(L"NO_COLOR") == nullptr; }

intptr_t CWindowsStdoutOperations::DuplicateOutput(intptr_t Handle) noexcept
{
	HANDLE Copy = nullptr;
	if(!DuplicateHandle(GetCurrentProcess(), reinterpret_cast<HANDLE>(Handle), GetCurrentProcess(), &Copy, 0, FALSE, DUPLICATE_SAME_ACCESS))
		return 0;
	return reinterpret_cast<intptr_t>(Copy);
}

std::unique_ptr<CLoggerAsync> CWindowsStdoutOperations::CreateAsyncLogger(IOHANDLE File, bool Colors)
{
	return std::make_unique<CLoggerAsync>(File, Colors, true);
}

std::unique_ptr<ILogger> CWindowsStdoutOperations::CreateConsoleLogger(intptr_t Handle, bool Colors)
{
	return std::make_unique<CWindowsConsoleLogger>(reinterpret_cast<HANDLE>(Handle), Colors);
}

static std::unique_ptr<ILogger> WindowsAsyncLogger(intptr_t Handle, int OpenFlags, bool Colors, CWindowsStdoutOperations &Operations) noexcept
{
	try
	{
		const intptr_t Copy = Operations.DuplicateOutput(Handle);
		if(Copy == 0 || Copy == -1)
			return nullptr;
		const IOHANDLE File = windows_io_from_handle(Copy, "w", &Operations, OpenFlags);
		if(!File)
			return nullptr;
		auto pLogger = CLoggerAsync::CreateOwned(File, [&](IOHANDLE Owned) { return Operations.CreateAsyncLogger(Owned, Colors); });
		if(!pLogger || !pLogger->IsAvailable())
			return nullptr;
		return pLogger;
	}
	catch(...)
	{
		return nullptr;
	}
}

class CWindowsConsoleSetup
{
	CWindowsStdoutOperations &m_Operations;
	intptr_t m_Handle;
	unsigned long m_OldMode;
	unsigned int m_OldCodePage = 0;
	bool m_ModeChanged = false;
	bool m_Committed = false;

public:
	CWindowsConsoleSetup(CWindowsStdoutOperations &Operations, intptr_t Handle, unsigned long OldMode) :
		m_Operations(Operations), m_Handle(Handle), m_OldMode(OldMode) {}
	CWindowsConsoleSetup(const CWindowsConsoleSetup &) = delete;
	CWindowsConsoleSetup &operator=(const CWindowsConsoleSetup &) = delete;
	~CWindowsConsoleSetup()
	{
		if(!m_Committed)
		{
			if(m_OldCodePage)
				m_Operations.SetOutputCodePage(m_OldCodePage);
			if(m_ModeChanged)
				m_Operations.WriteConsoleMode(m_Handle, m_OldMode);
		}
	}
	void ModeChanged() { m_ModeChanged = true; }
	void CodePageChanged(unsigned int OldCodePage) { m_OldCodePage = OldCodePage; }
	void Commit() { m_Committed = true; }
};

std::unique_ptr<ILogger> log_logger_windows_stdout(CWindowsStdoutOperations *pOperations) noexcept
{
	CWindowsStdoutOperations DefaultOperations;
	CWindowsStdoutOperations &Operations = pOperations ? *pOperations : DefaultOperations;
	try
	{
		const intptr_t Initial = Operations.OutputHandle();
		if(Initial == 0 || Initial == -1 || Operations.OutputType(Initial) == FILE_TYPE_UNKNOWN)
			Operations.AttachParentConsole();
		const intptr_t Output = Operations.OutputHandle();
		if(Output == 0 || Output == -1)
			return nullptr;
		const unsigned long Type = Operations.OutputType(Output);
		if(Type == FILE_TYPE_CHAR)
		{
			unsigned long OldMode = 0;
			if(!Operations.ReadConsoleMode(Output, &OldMode))
				return nullptr;
			const bool Colors = Operations.ColorsEnabled();
			CWindowsConsoleSetup Setup(Operations, Output, OldMode);
			if(!Operations.WriteConsoleMode(Output, OldMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN) &&
				!Operations.WriteConsoleMode(Output, OldMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
				return Operations.CreateConsoleLogger(Output, Colors);
			Setup.ModeChanged();
			const unsigned int OldCodePage = Operations.OutputCodePage();
			if(!OldCodePage || !Operations.SetOutputCodePage(CP_UTF8))
				return Operations.CreateConsoleLogger(Output, Colors);
			Setup.CodePageChanged(OldCodePage);
			auto pLogger = WindowsAsyncLogger(Output, _O_TEXT, Colors, Operations);
			if(!pLogger)
				return Operations.CreateConsoleLogger(Output, Colors);
			Setup.Commit();
			return pLogger;
		}
		if(Type == FILE_TYPE_DISK || Type == FILE_TYPE_PIPE)
			return WindowsAsyncLogger(Output, _O_APPEND, false, Operations);
		return nullptr;
	}
	catch(...)
	{
		return nullptr;
	}
}
#endif

std::unique_ptr<ILogger> log_logger_stdout()
{
#if !defined(CONF_FAMILY_WINDOWS)
	// TODO: Only enable true color when COLORTERM contains "truecolor".
	// https://github.com/termstandard/colors/tree/65bf0cd1ece7c15fa33a17c17528b02c99f1ae0b#checking-for-colorterm
	const bool Colors = getenv("NO_COLOR") == nullptr && isatty(STDOUT_FILENO);
	return std::make_unique<CLoggerAsync>(io_stdout(), Colors, false);
#else
	return log_logger_windows_stdout();
#endif
}

#if defined(CONF_FAMILY_WINDOWS)
class CLoggerWindowsDebugger : public ILogger
{
public:
	void Log(const CLogMessage *pMessage) override
	{
		if(m_Filter.Filters(pMessage))
		{
			return;
		}
		const std::wstring WideMessage = windows_utf8_to_wide(pMessage->m_aLine);
		OutputDebugStringW(WideMessage.c_str());
	}
};
std::unique_ptr<ILogger> log_logger_windows_debugger()
{
	return std::make_unique<CLoggerWindowsDebugger>();
}
#else
std::unique_ptr<ILogger> log_logger_windows_debugger()
{
	dbg_assert(0, "Windows Debug logger on non-Windows");
	return nullptr;
}
#endif

class CLoggerNoOp : public ILogger
{
public:
	void Log(const CLogMessage *pMessage) override
	{
		// no-op
	}
};
std::unique_ptr<ILogger> log_logger_noop()
{
	return std::make_unique<CLoggerNoOp>();
}

#ifdef __GNUC__
// atomic_compare_exchange_strong_explicit is deprecated
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
void CFutureLogger::Set(std::shared_ptr<ILogger> pLogger)
{
	const CLockScope LockScope(m_PendingLock);
	std::shared_ptr<ILogger> pNullLogger;
	if(!std::atomic_compare_exchange_strong_explicit(&m_pLogger, &pNullLogger, pLogger, std::memory_order_acq_rel, std::memory_order_acquire))
	{
		dbg_assert_failed("future logger has already been set and can only be set once");
	}
	// 发布后的共享指针只能原子访问；积压转交使用本次调用持有的引用。
	for(const auto &Pending : m_vPending)
	{
		pLogger->Log(&Pending);
	}
	m_vPending.clear();
	m_vPending.shrink_to_fit();
}

void CFutureLogger::Log(const CLogMessage *pMessage)
{
	auto pLogger = std::atomic_load_explicit(&m_pLogger, std::memory_order_acquire);
	if(pLogger)
	{
		pLogger->Log(pMessage);
		return;
	}
	const CLockScope LockScope(m_PendingLock);
	pLogger = std::atomic_load_explicit(&m_pLogger, std::memory_order_relaxed);
	if(pLogger)
	{
		pLogger->Log(pMessage);
		return;
	}
	m_vPending.push_back(*pMessage);
}

void CFutureLogger::GlobalFinish()
{
	auto pLogger = std::atomic_load_explicit(&m_pLogger, std::memory_order_acquire);
	if(pLogger)
	{
		pLogger->GlobalFinish();
	}
}

void CFutureLogger::OnFilterChange()
{
	auto pLogger = std::atomic_load_explicit(&m_pLogger, std::memory_order_acquire);
	if(pLogger)
	{
		pLogger->SetFilter(m_Filter);
	}
}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

void CMemoryLogger::Log(const CLogMessage *pMessage)
{
	if(m_pParentLogger)
	{
		m_pParentLogger->Log(pMessage);
	}
	if(m_Filter.Filters(pMessage))
	{
		return;
	}
	const CLockScope LockScope(m_MessagesMutex);
	m_vMessages.push_back(*pMessage);
}

std::vector<CLogMessage> CMemoryLogger::Lines()
{
	const CLockScope LockScope(m_MessagesMutex);
	return m_vMessages;
}

std::string CMemoryLogger::ConcatenatedLines()
{
	const CLockScope LockScope(m_MessagesMutex);
	std::string Result;
	for(const CLogMessage &Message : m_vMessages)
	{
		if(!Result.empty())
		{
			Result += '\n';
		}
		Result += Message.m_aLine;
	}
	return Result;
}
