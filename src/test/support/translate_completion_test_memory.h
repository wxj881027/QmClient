#ifndef TEST_SUPPORT_TRANSLATE_COMPLETION_TEST_MEMORY_H
#define TEST_SUPPORT_TRANSLATE_COMPLETION_TEST_MEMORY_H

#include <limits>
#include <memory_resource>
#include <new>

// 仅影响调用方显式传入的队列或结果内存域，不替换全局new。
class CTranslateCompletionTestMemory final : public std::pmr::memory_resource
{
	void *do_allocate(std::size_t Bytes, std::size_t Alignment) override
	{
		if(++m_Allocations == m_FailAt)
			throw std::bad_alloc();
		void *pData = std::pmr::new_delete_resource()->allocate(Bytes, Alignment);
		++m_LiveBlocks;
		return pData;
	}
	void do_deallocate(void *pData, std::size_t Bytes, std::size_t Alignment) override
	{
		--m_LiveBlocks;
		std::pmr::new_delete_resource()->deallocate(pData, Bytes, Alignment);
	}
	bool do_is_equal(const std::pmr::memory_resource &Other) const noexcept override { return this == &Other; }

public:
	std::size_t m_Allocations = 0;
	std::size_t m_LiveBlocks = 0;
	std::size_t m_FailAt = std::numeric_limits<std::size_t>::max();
	void AllowAllocations() { m_FailAt = std::numeric_limits<std::size_t>::max(); }
};

#endif
