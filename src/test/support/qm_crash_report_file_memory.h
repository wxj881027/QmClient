#ifndef TEST_SUPPORT_QM_CRASH_REPORT_FILE_MEMORY_H
#define TEST_SUPPORT_QM_CRASH_REPORT_FILE_MEMORY_H

#include <base/borrowed_allocator.h>

// 只控制显式内存域的真实分配；首次分配可用于在长度预检之后修改真实文件。
class CCrashReportFileMemory : public IAllocationResource
{
	void *Allocate(size_t Bytes, size_t Alignment) override
	{
		++m_Calls;
		if(m_Calls == m_FailAt)
			throw std::bad_alloc();
		if(m_Calls == 1 && m_pfnBeforeFirstAllocation)
			m_pfnBeforeFirstAllocation(m_pUser);
		void *pData = AllocateDefault(Bytes, Alignment);
		++m_Live;
		return pData;
	}
	void Deallocate(void *pData, size_t Bytes, size_t Alignment) noexcept override
	{
		--m_Live;
		DeallocateDefault(pData, Bytes, Alignment);
	}

public:
	size_t m_Calls = 0;
	size_t m_FailAt = 0;
	size_t m_Live = 0;
	void (*m_pfnBeforeFirstAllocation)(void *) = nullptr;
	void *m_pUser = nullptr;
};

#endif
