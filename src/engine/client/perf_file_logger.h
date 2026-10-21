#ifndef ENGINE_CLIENT_PERF_FILE_LOGGER_H
#define ENGINE_CLIENT_PERF_FILE_LOGGER_H

#include <base/lock.h>
#include <base/logger.h>

#include <engine/shared/jobs.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

// 关闭任务只持有日志对象，不借用客户端或 Storage；退出时也可同步接管未派发任务。
class CQmPerfLoggerCloseJob : public IJob
{
	friend class CQmPerfFileSwitchLogger;
	void Take(std::shared_ptr<ILogger> &pLogger) noexcept { m_pLogger.swap(pLogger); }
	std::mutex m_Mutex;
	std::shared_ptr<ILogger> m_pLogger;

protected:
	void Run() override { Close(); }

public:
	explicit CQmPerfLoggerCloseJob(std::shared_ptr<ILogger> pLogger) :
		m_pLogger(std::move(pLogger)) {}

	void Close()
	{
		const std::lock_guard<std::mutex> Lock(m_Mutex);
		m_pLogger.reset();
	}
};

class CQmPerfFileSwitchLogger : public ILogger
{
public:
	// 只替换真实关闭任务与队列容量申请，操作对象须覆盖 Switch 的使用期。
	class CPreparationOperations
	{
	public:
		virtual ~CPreparationOperations() = default;
		virtual std::shared_ptr<CQmPerfLoggerCloseJob> CreateCloseJob()
		{
			return std::make_shared<CQmPerfLoggerCloseJob>(nullptr);
		}
		virtual void Reserve(std::vector<std::shared_ptr<CQmPerfLoggerCloseJob>> &vpJobs, size_t Count)
		{
			vpJobs.reserve(Count);
		}
	};

private:
	CLock m_SwitchLock;
	std::mutex m_FinishMutex;
	std::shared_ptr<ILogger> m_pLogger;
	std::shared_ptr<CQmPerfLoggerCloseJob> m_pPreparedClose;
	std::vector<std::shared_ptr<CQmPerfLoggerCloseJob>> m_vpClosing;
	CPreparationOperations *m_pPreparationOperations;

	std::shared_ptr<CQmPerfLoggerCloseJob> PrepareClose(size_t Extra)
	{
		CPreparationOperations DefaultOperations;
		CPreparationOperations &Operations = m_pPreparationOperations ? *m_pPreparationOperations : DefaultOperations;
		auto pClose = Operations.CreateCloseJob();
		if(!pClose)
			throw std::bad_alloc();
		if(Extra > m_vpClosing.max_size() - m_vpClosing.size())
			throw std::length_error("performance logger close queue");
		const size_t Required = m_vpClosing.size() + Extra;
		if(Required > m_vpClosing.capacity())
			Operations.Reserve(m_vpClosing, Required);
		return pClose;
	}

public:
	explicit CQmPerfFileSwitchLogger(CPreparationOperations *pOperations = nullptr) :
		m_pPreparationOperations(pOperations) {}
	~CQmPerfFileSwitchLogger() override { FinishPending(); }

	void Set(std::shared_ptr<ILogger> pLogger)
	{
		std::shared_ptr<CQmPerfLoggerCloseJob> pPrepared;
		{
			const CLockScope LockScope(m_SwitchLock);
			if(pLogger)
				pPrepared = PrepareClose(1);
			m_pLogger.swap(pLogger);
			m_pPreparedClose.swap(pPrepared);
		}
		// 同步关闭仅用于初始化/退出，仍须在切换锁外析构。
	}

	std::shared_ptr<IJob> SetAsync(std::shared_ptr<ILogger> pLogger)
	{
		const CLockScope LockScope(m_SwitchLock);
		m_vpClosing.erase(std::remove_if(m_vpClosing.begin(), m_vpClosing.end(), [](const auto &pJob) { return pJob->Done(); }), m_vpClosing.end());
		std::shared_ptr<CQmPerfLoggerCloseJob> pNextClose;
		if(pLogger)
			pNextClose = PrepareClose(m_pLogger ? 2 : 1);
		auto pClose = m_pPreparedClose;
		if(m_pLogger)
		{
			dbg_assert(pClose != nullptr && m_vpClosing.size() < m_vpClosing.capacity(), "active logger must have a reserved close slot");
			m_vpClosing.push_back(pClose);
			pClose->Take(m_pLogger);
		}
		m_pLogger = std::move(pLogger);
		m_pPreparedClose = std::move(pNextClose);
		return pClose;
	}

	void FinishPending()
	{
		// 并发收尾等待同一次关闭；逐项移出保留容量，不复制队列或在切换锁内关闭。
		const std::lock_guard<std::mutex> FinishLock(m_FinishMutex);
		for(;;)
		{
			std::shared_ptr<CQmPerfLoggerCloseJob> pClose;
			{
				const CLockScope LockScope(m_SwitchLock);
				if(m_vpClosing.empty())
					break;
				pClose = std::move(m_vpClosing.back());
				m_vpClosing.pop_back();
			}
			pClose->Close();
		}
	}

	void Log(const CLogMessage *pMessage) override
	{
		const CLockScope LockScope(m_SwitchLock);
		if(m_pLogger)
			m_pLogger->Log(pMessage);
	}

	void GlobalFinish() override
	{
		FinishPending();
		const CLockScope LockScope(m_SwitchLock);
		if(m_pLogger)
			m_pLogger->GlobalFinish();
	}

	void OnFilterChange() override
	{
		const CLockScope LockScope(m_SwitchLock);
		if(m_pLogger)
			m_pLogger->SetFilter(m_Filter);
	}
};

#endif
