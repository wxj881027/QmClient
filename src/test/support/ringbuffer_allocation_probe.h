#ifndef TEST_SUPPORT_RINGBUFFER_ALLOCATION_PROBE_H
#define TEST_SUPPORT_RINGBUFFER_ALLOCATION_PROBE_H

#include <cstdlib>
#include <utility>

// 仅替换显式传入的分配器，不拦截全局分配；成功缓冲仍交给生产 free 回收。
class CRingBufferAllocationProbe
{
	static inline thread_local CRingBufferAllocationProbe *s_pActive = nullptr;
	CRingBufferAllocationProbe *m_pPrevious;

public:
	bool m_Fail = false;
	int m_Requests = 0;
	size_t m_LastSize = 0;

	CRingBufferAllocationProbe() :
		m_pPrevious(std::exchange(s_pActive, this)) {}
	~CRingBufferAllocationProbe() { s_pActive = m_pPrevious; }
	CRingBufferAllocationProbe(const CRingBufferAllocationProbe &) = delete;
	CRingBufferAllocationProbe &operator=(const CRingBufferAllocationProbe &) = delete;

	static void *Allocate(size_t Size) noexcept
	{
		if(s_pActive == nullptr)
			return nullptr;
		++s_pActive->m_Requests;
		s_pActive->m_LastSize = Size;
		return s_pActive->m_Fail ? nullptr : std::malloc(Size);
	}
};

#endif
