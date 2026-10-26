#ifndef TEST_SUPPORT_SKIN7_REFRESH_TEST_ENGINE_H
#define TEST_SUPPORT_SKIN7_REFRESH_TEST_ENGINE_H

#include <engine/engine.h>
#include <engine/shared/jobs.h>

#include <memory>
#include <new>
#include <utility>

// 只替换引擎接收边界，候选部件、请求回调和真实扫描作业不替换。
class CSkin7RefreshTestEngine : public IEngine
{
public:
	bool m_Accept = true;
	bool m_Throw = false;
	int m_Attempts = 0;
	std::shared_ptr<IJob> m_pAccepted;
	CJobPool *m_pPool = nullptr;
	void Init() override {}
	void AddJob(std::shared_ptr<IJob> pJob) override { (void)TryAddJob(std::move(pJob)); }
	bool TryAddJob(std::shared_ptr<IJob> pJob) override
	{
		++m_Attempts;
		if(m_Throw)
			throw std::bad_alloc();
		if(!m_Accept)
			return false;
		if(m_pPool && !m_pPool->TryAdd(pJob))
			return false;
		m_pAccepted = std::move(pJob);
		return true;
	}
	void ShutdownJobs() override {}
	void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
};

#endif
