#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN7_LOAD_COMPLETION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN7_LOAD_COMPLETION_H

#include "skin7_resource_lifecycle.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <utility>

// 主线程通知实际的渲染信息重建；失败可重做，调用方不能放入不可重复的外部副作用。
class CQmSkin7LoadCompletion
{
	std::function<void()> m_Callback;
	uint64_t m_Generation = 0;
	bool m_Dispatching = false;

public:
	void Set(std::function<void()> &&Callback) noexcept
	{
		++m_Generation;
		m_Callback = std::move(Callback);
	}

	void Cancel() noexcept
	{
		++m_Generation;
		m_Callback = {};
	}

	bool HasPending() const { return static_cast<bool>(m_Callback); }

	std::function<void()> TakeForRetry() noexcept
	{
		++m_Generation;
		std::function<void()> Callback;
		Callback.swap(m_Callback);
		return Callback;
	}

	template<typename TDiagnose>
	bool Complete(bool &Loading, CQmSkin7ResourceLifecycle &Lifecycle, std::chrono::nanoseconds &LastRefresh,
		std::chrono::nanoseconds Now, TDiagnose &&Diagnose) noexcept
	{
		if(!Loading || !Lifecycle.CompleteLoad())
			return false;
		Loading = false;
		LastRefresh = Now;
		try
		{
			std::forward<TDiagnose>(Diagnose)();
		}
		catch(...)
		{
		}
		return true;
	}

	bool Dispatch(bool Loading) noexcept
	{
		if(Loading || m_Dispatching)
			return false;
		if(!m_Callback)
			return true;
		m_Dispatching = true;
		struct SDispatchScope
		{
			bool &m_Dispatching;
			~SDispatchScope() { m_Dispatching = false; }
		} Scope{m_Dispatching};
		const uint64_t Generation = m_Generation;
		// 回调重入刷新/取消时，正在运行的可调用对象仍独立存活。
		std::function<void()> Callback;
		Callback.swap(m_Callback);
		try
		{
			Callback();
			return true;
		}
		catch(...)
		{
			// 新请求优先；旧通知失败不能覆盖重入时登记的新回调。
			if(Generation == m_Generation)
				m_Callback = std::move(Callback);
			return false;
		}
	}
};

#endif
