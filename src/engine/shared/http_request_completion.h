#ifndef ENGINE_SHARED_HTTP_REQUEST_COMPLETION_H
#define ENGINE_SHARED_HTTP_REQUEST_COMPLETION_H

#include <engine/http.h>

#include <atomic>

// 请求自有的完成通知，不为回调额外申请队列节点；取消一旦发布就不再覆盖。
class CHttpRequestCompletion
{
	std::atomic<EHttpState> m_State = EHttpState::QUEUED;

public:
	void Publish(EHttpState State) noexcept
	{
		auto Previous = m_State.load();
		while(Previous != EHttpState::ABORTED && !m_State.compare_exchange_weak(Previous, State))
		{
		}
	}
	EHttpState State() const noexcept { return m_State.load(); }
};

#endif
