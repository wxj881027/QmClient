#ifndef ENGINE_SHARED_HTTP_RESPONSE_BUFFER_BUDGET_H
#define ENGINE_SHARED_HTTP_RESPONSE_BUFFER_BUDGET_H

#include <base/dbg.h>

#include <atomic>
#include <cstddef>

// 统计仍由请求持有的缓冲容量；只做资源准入，不承担响应数据的线程发布。
class CHttpResponseBufferBudget
{
	const size_t m_Limit;
	std::atomic<size_t> m_Used{0};

public:
	explicit CHttpResponseBufferBudget(size_t Limit) noexcept :
		m_Limit(Limit) {}
	bool TryAcquire(size_t Bytes) noexcept
	{
		size_t Used = m_Used.load(std::memory_order_relaxed);
		do
		{
			if(Bytes > m_Limit - Used)
				return false;
		} while(!m_Used.compare_exchange_weak(Used, Used + Bytes, std::memory_order_relaxed));
		return true;
	}
	void Release(size_t Bytes) noexcept
	{
		const size_t Previous = m_Used.fetch_sub(Bytes, std::memory_order_relaxed);
		dbg_assert(Bytes <= Previous, "HTTP buffer budget released more than acquired");
	}
	size_t Used() const noexcept { return m_Used.load(std::memory_order_relaxed); }
};

#endif
