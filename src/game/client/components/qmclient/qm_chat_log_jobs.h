#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_CHAT_LOG_JOBS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_CHAT_LOG_JOBS_H

#include <base/log.h>

#include <engine/shared/jobs.h>

#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

// 日志写入按入队顺序执行，磁盘操作时不占用主线程的入队锁。
class CQmChatLogWriteQueue
{
	struct SState
	{
		std::mutex m_Mutex;
		std::deque<std::function<void()>> m_Actions;
		bool m_Running = false;
	};

	class CWriteJob : public IJob
	{
		std::shared_ptr<SState> m_pState;
		bool m_Started = false; // 由队列锁保护；未运行的候选销毁时归还调度权。

		static void ReportFailure(const char *pError) noexcept
		try
		{
			log_error("chat", "Chat log action failed: %s", pError);
		}
		catch(...)
		{
			// 诊断失败也不能阻止后续日志动作。
		}

		void Run() override
		{
			{
				std::lock_guard<std::mutex> Lock(m_pState->m_Mutex);
				m_Started = true;
			}
			while(true)
			{
				std::function<void()> Action;
				{
					std::lock_guard<std::mutex> Lock(m_pState->m_Mutex);
					if(m_pState->m_Actions.empty())
					{
						m_pState->m_Running = false;
						return;
					}
					Action = std::move(m_pState->m_Actions.front());
					m_pState->m_Actions.pop_front();
				}
				try
				{
					Action();
				}
				catch(const std::exception &Error)
				{
					ReportFailure(Error.what());
				}
				catch(...)
				{
					ReportFailure("unknown exception");
				}
				// 动作可能已完成部分写入，失败项不重放；其余项仍按入队顺序执行。
			}
		}

	public:
		explicit CWriteJob(std::shared_ptr<SState> pState) :
			m_pState(std::move(pState)) {}

		~CWriteJob() override
		{
			std::lock_guard<std::mutex> Lock(m_pState->m_Mutex);
			if(!m_Started)
				m_pState->m_Running = false;
		}
	};

	std::shared_ptr<SState> m_pState = std::make_shared<SState>();

public:
	void Enqueue(std::function<void()> Action)
	{
		std::lock_guard<std::mutex> Lock(m_pState->m_Mutex);
		m_pState->m_Actions.push_back(std::move(Action));
	}

	// Submit 遵守 TryAddJob 合同：仅在接收后返回 true，拒绝或抛出前不保留任务。
	// 候选准备失败或未接收时，动作仍在队列；后续帧无需新消息即可重试。
	template<typename FSubmit>
	bool TrySubmit(FSubmit Submit) noexcept
	try
	{
		std::shared_ptr<IJob> pJob;
		{
			std::lock_guard<std::mutex> Lock(m_pState->m_Mutex);
			if(m_pState->m_Running || m_pState->m_Actions.empty())
				return false;
			pJob = std::make_shared<CWriteJob>(m_pState);
			m_pState->m_Running = true;
		}
		return Submit(std::move(pJob));
	}
	catch(...)
	{
		return false;
	}
};

#endif
