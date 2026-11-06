#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MEDIA_WORKER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MEDIA_WORKER_H

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>

// 媒体创建失败按原轮询间隔重试；成功配置可重置，不让一次失败变成逐帧忙重试。
class CQmMediaRetryClock
{
	bool m_HasAttempt = false;
	int64_t m_LastAttempt = 0;

public:
	bool Begin(int64_t NowMs, int64_t IntervalMs) noexcept
	{
		const uint64_t Interval = static_cast<uint64_t>(std::max<int64_t>(1, IntervalMs));
		if(m_HasAttempt && NowMs >= m_LastAttempt && static_cast<uint64_t>(NowMs) - static_cast<uint64_t>(m_LastAttempt) < Interval)
			return false;
		m_HasAttempt = true;
		m_LastAttempt = NowMs;
		return true;
	}
	void Reset() noexcept { m_HasAttempt = false; }
};

// 启动和关闭由所属主线程串行调用；工作回调不能反向销毁自己的 owner。
class CQmMediaWorker
{
public:
	class CState
	{
	public:
		virtual ~CState() = default;
		virtual void Run(const std::atomic_bool &Stop) = 0;
		virtual void OnFailure() noexcept {}
		virtual void BeforeRestart() noexcept {}
	};
	class COperations
	{
	public:
		virtual ~COperations() = default;
		virtual std::unique_ptr<CState> CreateState() = 0;
		// 返回拥有的线程，或在没有遗留线程的情况下报告创建失败。
		virtual std::thread CreateThread(CState &State, const std::atomic_bool &Stop, std::atomic_bool &Finished)
		{
			return std::thread([&State, &Stop, &Finished] {
				try
				{
					State.Run(Stop);
				}
				catch(...)
				{
					State.OnFailure();
				}
				Finished.store(true, std::memory_order_release);
			});
		}
	};

	~CQmMediaWorker() { Stop(); }
	bool EnsureStarted(COperations &Operations, int64_t NowMs, int64_t RetryMs) noexcept
	{
		if(Running())
			return true;
		if(!m_Retry.Begin(NowMs, RetryMs))
			return false;
		try
		{
			if(m_Thread.joinable())
				m_Thread.join();
			std::unique_ptr<CState> pPrepared;
			CState *pState = m_pState.get();
			if(pState)
				pState->BeforeRestart();
			else
			{
				pPrepared = Operations.CreateState();
				pState = pPrepared.get();
			}
			if(!pState)
				return false;
			m_Stop.store(false, std::memory_order_relaxed);
			m_Finished.store(false, std::memory_order_relaxed);
			auto Thread = Operations.CreateThread(*pState, m_Stop, m_Finished);
			if(!Thread.joinable())
			{
				m_Stop.store(true, std::memory_order_relaxed);
				m_Finished.store(true, std::memory_order_release);
				return false;
			}
			// 发布前只剩不抛异常的所有权移动；线程始终借用同一地址的状态。
			if(pPrepared)
				m_pState = std::move(pPrepared);
			m_Thread = std::move(Thread);
			return true;
		}
		catch(...)
		{
			m_Stop.store(true, std::memory_order_relaxed);
			m_Finished.store(true, std::memory_order_release);
			return false;
		}
	}
	void Stop() noexcept
	{
		m_Stop.store(true, std::memory_order_relaxed);
		if(m_Thread.joinable())
			m_Thread.join();
		m_pState.reset();
		m_Finished.store(true, std::memory_order_release);
		m_Retry.Reset();
	}
	bool Running() const noexcept { return m_pState && !m_Finished.load(std::memory_order_acquire); }
	CState *State() const noexcept { return m_pState.get(); }

private:
	CQmMediaRetryClock m_Retry;
	std::unique_ptr<CState> m_pState;
	std::thread m_Thread;
	std::atomic_bool m_Stop{true};
	std::atomic_bool m_Finished{true};
};

#endif
