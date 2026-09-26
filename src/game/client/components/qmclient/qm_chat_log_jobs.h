#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_CHAT_LOG_JOBS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_CHAT_LOG_JOBS_H

#include <engine/shared/jobs.h>

#include <deque>
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

		void Run() override
		{
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
				Action();
			}
		}

	public:
		explicit CWriteJob(std::shared_ptr<SState> pState) :
			m_pState(std::move(pState)) {}
	};

	std::shared_ptr<SState> m_pState = std::make_shared<SState>();

public:
	std::shared_ptr<IJob> Enqueue(std::function<void()> Action)
	{
		std::lock_guard<std::mutex> Lock(m_pState->m_Mutex);
		m_pState->m_Actions.push_back(std::move(Action));
		if(m_pState->m_Running)
			return nullptr;
		m_pState->m_Running = true;
		return std::make_shared<CWriteJob>(m_pState);
	}
};

#endif
