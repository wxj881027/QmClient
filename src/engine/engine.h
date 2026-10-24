/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_ENGINE_H
#define ENGINE_ENGINE_H

#include "kernel.h"

#include <base/thread.h>

#include <memory>
#include <utility>

class CFutureLogger;
class IJob;
class ILogger;

class IEngine : public IInterface
{
	MACRO_INTERFACE("engine")

public:
	virtual void Init() = 0;
	virtual void AddJob(std::shared_ptr<IJob> pJob) = 0;
	// 需要保留失败请求的调用方可区分关池拒绝；旧调用入口保持不变。
	virtual bool TryAddJob(std::shared_ptr<IJob> pJob)
	{
		AddJob(std::move(pJob));
		return true;
	}
	virtual void ShutdownJobs() = 0;
	virtual void SetAdditionalLogger(std::shared_ptr<ILogger> &&pLogger) = 0;
};

// 工作线程初始化失败返回空，不发布尚未就绪的引擎。
extern IEngine *CreateEngine(const char *pAppname, std::shared_ptr<CFutureLogger> pFutureLogger);
extern IEngine *CreateTestEngine(const char *pAppname, THREAD_CREATE pfnCreate = thread_try_init);

#endif
