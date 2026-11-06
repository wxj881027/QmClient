#ifndef BASE_LOGGER_ASYNC_H
#define BASE_LOGGER_ASYNC_H

#include "logger.h"
#include "system.h"

#include <memory>
#include <mutex>
#include <utility>

struct ASYNCIO;

class CLoggerAsync : public ILogger
{
	ASYNCIO *m_pAio;
	bool m_AnsiTruecolor;
	bool m_Close;
	std::once_flag m_FinishOnce;

public:
	CLoggerAsync(IOHANDLE File, bool AnsiTruecolor, bool Close, ASYNCIO *(*Create)(IOHANDLE) = nullptr);
	~CLoggerAsync() override;
	bool IsAvailable() const noexcept override { return m_pAio != nullptr; }
	// 创建器返回非空对象即已消费文件，包括 AIO 不可用的对象；空或异常由本地 owner 回收。
	template<typename F>
	static std::unique_ptr<CLoggerAsync> CreateOwned(IOHANDLE File, F &&Create)
	{
		std::unique_ptr<void, decltype(&io_close)> Owner(File, &io_close);
		auto pLogger = std::forward<F>(Create)(File);
		if(pLogger)
			Owner.release();
		return pLogger;
	}
	void Log(const CLogMessage *pMessage) override;
	void GlobalFinish() override;
};

#endif
