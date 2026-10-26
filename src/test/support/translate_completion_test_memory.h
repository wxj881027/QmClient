#ifndef TEST_SUPPORT_TRANSLATE_COMPLETION_TEST_MEMORY_H
#define TEST_SUPPORT_TRANSLATE_COMPLETION_TEST_MEMORY_H

#include <base/borrowed_allocator.h>

#include <limits>
#include <new>

// 仅影响调用方显式传入的队列或结果内存域，不替换全局new。
class CTranslateCompletionTestMemory final : public IAllocationResource
{
	void *Allocate(std::size_t Bytes, std::size_t Alignment) override
	{
		if(++m_Allocations == m_FailAt)
			throw std::bad_alloc();
		void *pData = IAllocationResource::AllocateDefault(Bytes, Alignment);
		++m_LiveBlocks;
		return pData;
	}
	void Deallocate(void *pData, std::size_t Bytes, std::size_t Alignment) noexcept override
	{
		--m_LiveBlocks;
		IAllocationResource::DeallocateDefault(pData, Bytes, Alignment);
	}

public:
	std::size_t m_Allocations = 0;
	std::size_t m_LiveBlocks = 0;
	std::size_t m_FailAt = std::numeric_limits<std::size_t>::max();
	void AllowAllocations() { m_FailAt = std::numeric_limits<std::size_t>::max(); }
};

#endif
