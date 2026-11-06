#ifndef BASE_LOGGER_H
#define BASE_LOGGER_H

#include "lock.h"
#include "log.h"

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

typedef void *IOHANDLE;

/**
 * @ingroup Log
 *
 * Metadata and actual content of a log message.
 */
class CLogMessage
{
public:
	/**
	 * Severity
	 */
	LEVEL m_Level;
	bool m_HaveColor;
	/**
	 * The requested color of the log message. Only useful if `m_HaveColor`
	 * is set.
	 */
	LOG_COLOR m_Color;
	char m_aTimestamp[80];
	char m_aSystem[32];
	/**
	 * The actual log message including the timestamp and the system.
	 */
	char m_aLine[4096];
	int m_TimestampLength;
	int m_SystemLength;
	/**
	 * Length of the log message including timestamp and the system.
	 */
	int m_LineLength;
	int m_LineMessageOffset;

	/**
	 * The actual log message excluding timestamp and the system.
	 */
	const char *Message() const
	{
		return m_aLine + m_LineMessageOffset;
	}
};

class CLogFilter
{
public:
	/**
	 * The highest `LEVEL` that is still logged, -1 corresponds to no
	 * printing at all.
	 */
	std::atomic_int m_MaxLevel{LEVEL_INFO};

	bool Filters(const CLogMessage *pMessage);
};

class ILogger
{
protected:
	CLogFilter m_Filter;

public:
	virtual ~ILogger() = default;
	// 查询后端初始化是否完成，不代表后续 I/O 必然成功。
	virtual bool IsAvailable() const noexcept { return true; }

	/**
	 * Set a new filter. It's up to the logger implementation to actually
	 * use the filter.
	 */
	void SetFilter(const CLogFilter &Filter)
	{
		m_Filter.m_MaxLevel.store(Filter.m_MaxLevel.load(std::memory_order_relaxed), std::memory_order_relaxed);
		OnFilterChange();
	}

	/**
	 * Send the specified message to the logging backend.
	 *
	 * @param pMessage Struct describing the log message.
	 */
	virtual void Log(const CLogMessage *pMessage) = 0;
	/**
	 * Flushes output buffers and shuts down.
	 * Global loggers cannot be destroyed because they might be accessed
	 * from multiple threads concurrently.
	 *
	 * This function is called on the global logger by
	 * `log_global_logger_finish` when the program is about to shut down
	 * and loggers are supposed to finish writing the log messages they
	 * have received so far.
	 *
	 * The destructor of this `ILogger` instance will not be called if this
	 * function is called.
	 *
	 * @see log_global_logger_finish
	 */
	virtual void GlobalFinish() {}
	/**
	 * Notifies the logger of a changed `m_Filter`.
	 */
	virtual void OnFilterChange() {}
};

// 完整准备后端及共享所有权；失败保留原目标，发布由调用方负责。
template<typename F>
bool log_prepare_backend(std::shared_ptr<ILogger> &pTarget, F &&Create) noexcept
{
	try
	{
		std::unique_ptr<ILogger> pCandidate = std::forward<F>(Create)();
		if(!pCandidate || !pCandidate->IsAvailable())
			return false;
		std::shared_ptr<ILogger> pPrepared(std::move(pCandidate));
		pTarget.swap(pPrepared);
		return true;
	}
	catch(...)
	{
		return false;
	}
}

/**
 * @ingroup Log
 *
 * Registers a logger instance as the default logger for all current and future
 * threads. It will only be used if no thread-local logger is set via
 * `log_set_scope_logger`.
 *
 * This function can only be called once. The passed logger instance will never
 * be freed.
 *
 * @param logger The global logger default.
 */
void log_set_global_logger(ILogger *logger);

/**
 * @ingroup Log
 *
 * Registers a sane default as the default logger for all current and future
 * threads.
 *
 * This is logging to stdout on most platforms and to the system log on
 * Android.
 *
 * @see log_set_global_logger
 */
void log_set_global_logger_default();

/**
 * @ingroup Log
 *
 * Notify global loggers of impending abnormal exit.
 *
 * This function is automatically called on normal exit. It notifies the global
 * logger of the impending shutdown via `GlobalFinish`, the logger is supposed
 * to flush its buffers and shut down.
 *
 * Don't call this except right before an abnormal exit.
 */
void log_global_logger_finish();

/**
 * @ingroup Log
 *
 * Get the logger active in the current scope. This might be the global default
 * logger or some other logger set via `log_set_scope_logger`.
 */
ILogger *log_get_scope_logger();

/**
 * @ingroup Log
 *
 * Set the logger for the current thread. The logger isn't managed by the
 * logging system, it still needs to be kept alive or freed by the caller.
 *
 * Consider using `CLogScope` if you only want to set the logger temporarily.
 *
 * @see CLogScope
 */
void log_set_scope_logger(ILogger *logger);

/**
 * @ingroup Log
 *
 * Logger for sending logs to the Android system log.
 *
 * Should only be called when targeting the Android platform.
 */
std::unique_ptr<ILogger> log_logger_android();

/**
 * @ingroup Log
 *
 * Logger combining a vector of other loggers.
 */
std::unique_ptr<ILogger> log_logger_collection(std::vector<std::shared_ptr<ILogger>> &&vpLoggers);

/**
 * @ingroup Log
 *
 * Logger routing logs with a matching system prefix to `prefix_logger` and all
 * other logs to `fallback_logger`.
 */
std::unique_ptr<ILogger> log_logger_prefix_router(std::shared_ptr<ILogger> prefix_logger, std::shared_ptr<ILogger> fallback_logger, const char *system_prefix);

/**
 * @ingroup Log
 *
 * Logger for writing logs to the given file.
 *
 * @param file File to write to, must be opened for writing.
 */
// 从入口接管文件；创建异常沿用抛出约定，文件先由工厂回收。
std::unique_ptr<ILogger> log_logger_file(IOHANDLE file);

/**
 * @ingroup Log
 *
 * Logger for writing logs with a matching system prefix to the given file.
 *
 * @param file File to write to, must be opened for writing.
 * @param system_prefix System name prefix to include.
 */
std::unique_ptr<ILogger> log_logger_prefix_file(IOHANDLE file, const char *system_prefix);
// 包装已拥有的后端；构造失败仍由参数所有者回收，空后端保持为空。
std::unique_ptr<ILogger> log_logger_prefix(std::unique_ptr<ILogger> pLogger, const char *pSystemPrefix);

/**
 * @ingroup Log
 *
 * Logger for writing logs to the standard output (stdout).
 *
 * @remark This function can return `nullptr` if the standard output is not available.
 */
std::unique_ptr<ILogger> log_logger_stdout();

/**
 * @ingroup Log
 *
 * Logger for sending logs to the debugger on Windows via `OutputDebugStringW`.
 *
 * Should only be called when targeting the Windows platform.
 */
std::unique_ptr<ILogger> log_logger_windows_debugger();

/**
 * @ingroup Log
 *
 * Logger which discards all logs.
 */
std::unique_ptr<ILogger> log_logger_noop();

/**
 * @ingroup Log
 *
 * Logger that collects log messages in memory until it is replaced by another
 * logger.
 *
 * Useful when you want to set a global logger without all logging targets
 * being configured.
 *
 * This logger forwards `SetFilter` calls, `SetFilter` calls before a logger is
 * set have no effect.
 */
class CFutureLogger : public ILogger
{
private:
	std::shared_ptr<ILogger> m_pLogger;
	std::vector<CLogMessage> m_vPending;
	CLock m_PendingLock;

public:
	/**
	 * Replace the `CFutureLogger` instance with the given logger. It'll
	 * receive all log messages sent to the `CFutureLogger` so far.
	 */
	void Set(std::shared_ptr<ILogger> pLogger) REQUIRES(!m_PendingLock);
	void Log(const CLogMessage *pMessage) override REQUIRES(!m_PendingLock);
	void GlobalFinish() override;
	void OnFilterChange() override;
};

/**
 * @ingroup Log
 *
 * Logger that collects messages in memory. This is useful to collect the log
 * messages for a particular operation and show them in a user interface when
 * the operation failed. Use only temporarily with @link CLogScope @endlink
 * or it will result in excessive memory usage.
 *
 * Messages are also forwarded to the parent logger if it's set, regardless
 * of this logger's filter.
 */
class CMemoryLogger : public ILogger
{
	ILogger *m_pParentLogger = nullptr;
	std::vector<CLogMessage> m_vMessages GUARDED_BY(m_MessagesMutex);
	CLock m_MessagesMutex;

public:
	void SetParent(ILogger *pParentLogger) { m_pParentLogger = pParentLogger; }
	void Log(const CLogMessage *pMessage) override REQUIRES(!m_MessagesMutex);
	std::vector<CLogMessage> Lines() REQUIRES(!m_MessagesMutex);
	std::string ConcatenatedLines() REQUIRES(!m_MessagesMutex);
};

/**
 * @ingroup Log
 *
 * RAII guard for temporarily changing the logger via `log_set_scope_logger`.
 *
 * @see log_set_scope_logger
 */
class CLogScope
{
	ILogger *old_scope_logger;
	ILogger *new_scope_logger;

public:
	CLogScope(ILogger *logger) :
		old_scope_logger(log_get_scope_logger()),
		new_scope_logger(logger)
	{
		log_set_scope_logger(new_scope_logger);
	}
	~CLogScope()
	{
		log_set_scope_logger(old_scope_logger);
	}
	CLogScope(const CLogScope &) = delete;
};
#endif // BASE_LOGGER_H
