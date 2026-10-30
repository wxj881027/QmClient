#ifndef TEST_SUPPORT_HEAP_ALLOCATION_PROBE_H
#define TEST_SUPPORT_HEAP_ALLOCATION_PROBE_H

#include <cstdlib>
#include <new>
#include <utility>

// 只替换显式借用的块分配器，成功内存仍由生产堆调用free释放。
class CHeapAllocationProbe
{
	static inline thread_local CHeapAllocationProbe *s_pActive = nullptr;
	CHeapAllocationProbe *m_pPrevious;

public:
	enum class EFailure
	{
		NONE,
		NULL_RESULT,
		BAD_ALLOC,
		UNKNOWN
	};
	EFailure m_Failure = EFailure::NONE;
	int m_Requests = 0;
	size_t m_LastSize = 0;
	CHeapAllocationProbe() :
		m_pPrevious(std::exchange(s_pActive, this)) {}
	~CHeapAllocationProbe() { s_pActive = m_pPrevious; }
	CHeapAllocationProbe(const CHeapAllocationProbe &) = delete;
	CHeapAllocationProbe &operator=(const CHeapAllocationProbe &) = delete;
	static void *Allocate(size_t Size)
	{
		if(!s_pActive)
			return nullptr;
		auto &Self = *s_pActive;
		++Self.m_Requests;
		Self.m_LastSize = Size;
		switch(Self.m_Failure)
		{
		case EFailure::NULL_RESULT: return nullptr;
		case EFailure::BAD_ALLOC: throw std::bad_alloc();
		case EFailure::UNKNOWN: throw 7;
		case EFailure::NONE: return std::malloc(Size);
		}
		return nullptr;
	}
};

#endif
