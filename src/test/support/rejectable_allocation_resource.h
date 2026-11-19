#ifndef TEST_SUPPORT_REJECTABLE_ALLOCATION_RESOURCE_H
#define TEST_SUPPORT_REJECTABLE_ALLOCATION_RESOURCE_H

#include <base/borrowed_allocator.h>

// 只拒绝显式传给被测标准容器的真实分配，不替换全局堆。
class CRejectableAllocationResource : public IAllocationResource
{
	void *Allocate(size_t Bytes, size_t Alignment) override
	{
		if(m_Reject)
			throw std::bad_alloc();
		return AllocateDefault(Bytes, Alignment);
	}
	void Deallocate(void *pData, size_t Bytes, size_t Alignment) noexcept override
	{
		DeallocateDefault(pData, Bytes, Alignment);
	}

public:
	bool m_Reject = false;
};

#endif
