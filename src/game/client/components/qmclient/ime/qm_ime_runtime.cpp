#include "qm_ime_runtime.h"

#include <base/dbg.h>

#include <condition_variable>
#include <cstring>
#include <mutex>
#include <utility>

namespace
{
	std::atomic<bool> s_RuntimeReserved{false};

	std::thread StartRuntimeWorker(std::function<void()> Task)
	{
		return std::thread(std::move(Task));
	}
}

void CQmImeRuntime::OnNotification(const char *pType, const char *pValue)
{
	if(pType != nullptr && pValue != nullptr && std::strcmp(pType, "deploy") == 0 && std::strcmp(pValue, "failure") == 0)
		m_DeployFailed.store(true);
}

struct CQmImeRuntimeShutdown::SState
{
	std::mutex m_Mutex;
	std::condition_variable m_Condition;
	bool m_CloseRequested = false;
	bool m_HoldsReservation = true;
	std::promise<EResult> m_ClosedPromise;
	std::shared_future<EResult> m_Completion = m_ClosedPromise.get_future().share();
	std::unique_ptr<CQmImeRuntime> m_pRuntime;
	// 外部 API 若异常退出，无法证明回调已结束；保留完整 owner 并阻止重入。
	std::shared_ptr<SState> m_pFailedOwner;

	~SState() { ReleaseReservation(); }

	void ReleaseReservation()
	{
		if(m_HoldsReservation)
		{
			m_HoldsReservation = false;
			s_RuntimeReserved.store(false);
		}
	}
};

CQmImeRuntimeShutdown::CQmImeRuntimeShutdown(std::shared_ptr<SState> pState) :
	m_pState(std::move(pState))
{
}

std::unique_ptr<CQmImeRuntimeShutdown> CQmImeRuntimeShutdown::Prepare(FStartWorker StartWorker)
{
	bool Expected = false;
	if(!s_RuntimeReserved.compare_exchange_strong(Expected, true))
		return nullptr;

	std::shared_ptr<SState> pState;
	std::thread Worker;
	try
	{
		pState = std::make_shared<SState>();
		auto pOwner = std::unique_ptr<CQmImeRuntimeShutdown>(new CQmImeRuntimeShutdown(pState));
		Worker = (StartWorker != nullptr ? StartWorker : StartRuntimeWorker)([pState] { Run(pState); });
		if(!Worker.joinable())
			return nullptr;
		// 线程捕获共享状态；Close 后该状态独占 runtime，不借用前台 owner。
		Worker.detach();
		return pOwner;
	}
	catch(...)
	{
		if(pState != nullptr)
		{
			{
				std::lock_guard Lock(pState->m_Mutex);
				pState->m_CloseRequested = true;
			}
			pState->m_Condition.notify_one();
		}
		else
			s_RuntimeReserved.store(false);
		// detach 失败时线程还没有接收 runtime，只需唤醒空任务并回收线程句柄。
		if(Worker.joinable())
			Worker.join();
		return nullptr;
	}
}

CQmImeRuntimeShutdown::~CQmImeRuntimeShutdown()
{
	Close();
}

void CQmImeRuntimeShutdown::Close(std::unique_ptr<CQmImeRuntime> pRuntime)
{
	{
		std::lock_guard Lock(m_pState->m_Mutex);
		if(m_pState->m_CloseRequested)
		{
			dbg_assert(pRuntime == nullptr, "Rime runtime may only be transferred once");
			return;
		}
		m_pState->m_pRuntime = std::move(pRuntime);
		m_pState->m_CloseRequested = true;
	}
	m_pState->m_Condition.notify_one();
}

void CQmImeRuntimeShutdown::Run(const std::shared_ptr<SState> &pState)
{
	{
		std::unique_lock Lock(pState->m_Mutex);
		pState->m_Condition.wait(Lock, [&] { return pState->m_CloseRequested; });
	}

	EResult Result = EResult::CLOSED;
	try
	{
		if(pState->m_pRuntime != nullptr)
		{
			auto &Runtime = *pState->m_pRuntime;
			if(Runtime.Initialized())
			{
				// join 覆盖在途和稍后到达的维护通知；它完成前不更换 handler。
				Runtime.JoinMaintenance();
				Runtime.DestroySession();
				Runtime.ClearNotificationHandler();
				Runtime.Finalize();
			}
			Runtime.UnloadLibrary();
			pState->m_pRuntime.reset();
		}
		{
			std::lock_guard Lock(pState->m_Mutex);
			pState->ReleaseReservation();
		}
	}
	catch(...)
	{
		std::lock_guard Lock(pState->m_Mutex);
		pState->m_pFailedOwner = pState;
		Result = EResult::FAILED;
	}
	pState->m_ClosedPromise.set_value(Result);
}

std::shared_future<CQmImeRuntimeShutdown::EResult> CQmImeRuntimeShutdown::Completion() const
{
	return m_pState->m_Completion;
}
