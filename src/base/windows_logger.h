#ifndef BASE_WINDOWS_LOGGER_H
#define BASE_WINDOWS_LOGGER_H

#include "windows_io.h"

#if defined(CONF_FAMILY_WINDOWS)
#include <memory>

class ILogger;
class CLoggerAsync;

// 仅在工厂调用中借用；返回的日志对象不保存操作对象引用。
class CWindowsStdoutOperations : public CWindowsFileOperations
{
public:
	virtual intptr_t OutputHandle() noexcept;
	virtual unsigned long OutputType(intptr_t Handle) noexcept;
	virtual void AttachParentConsole() noexcept;
	virtual bool ReadConsoleMode(intptr_t Handle, unsigned long *pMode) noexcept;
	virtual bool WriteConsoleMode(intptr_t Handle, unsigned long Mode) noexcept;
	virtual unsigned int OutputCodePage() noexcept;
	virtual bool SetOutputCodePage(unsigned int CodePage) noexcept;
	virtual bool ColorsEnabled() noexcept;
	// 成功返回当前调用拥有的副本；失败为 0，原句柄始终由进程持有。
	virtual intptr_t DuplicateOutput(intptr_t Handle) noexcept;
	// 返回非空对象即已接管文件，包括 AIO 不可用的对象；返回空或抛异常时仍由工厂持有。
	virtual std::unique_ptr<CLoggerAsync> CreateAsyncLogger(IOHANDLE File, bool Colors);
	virtual std::unique_ptr<ILogger> CreateConsoleLogger(intptr_t Handle, bool Colors);
};

std::unique_ptr<ILogger> log_logger_windows_stdout(CWindowsStdoutOperations *pOperations = nullptr) noexcept;
#endif

#endif // BASE_WINDOWS_LOGGER_H
