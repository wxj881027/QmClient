#ifndef BASE_LOGGER_ASYNC_H
#define BASE_LOGGER_ASYNC_H

#include "logger.h"

#include <mutex>

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
	void Log(const CLogMessage *pMessage) override;
	void GlobalFinish() override;
};

#endif
