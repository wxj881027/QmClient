#ifndef TEST_SUPPORT_TRACKED_ALLOCATION_RESOURCE_H
#define TEST_SUPPORT_TRACKED_ALLOCATION_RESOURCE_H

#include <base/borrowed_allocator.h>

#include <gtest/gtest.h>

#include <array>
#include <new>

// 只追踪显式借用内存域的分配，按实际请求次数拒绝；不替换全局堆。
class CTrackedAllocationResource : public IAllocationResource
{
	struct CBlock
	{
		void *m_pData = nullptr;
		size_t m_Bytes = 0;
		size_t m_Alignment = 0;
	};
	std::array<CBlock, 64> m_aBlocks{};

public:
	int m_Allocations = 0;
	int m_Live = 0;
	int m_FailAt = -1;
	bool m_ReturnNull = false;
	bool m_ThrowUnknown = false;
	void *Allocate(size_t Bytes, size_t Alignment) override
	{
		++m_Allocations;
		if(m_Allocations == m_FailAt)
		{
			if(m_ReturnNull)
				return nullptr;
			if(m_ThrowUnknown)
				throw 7;
			throw std::bad_alloc();
		}
		for(auto &Block : m_aBlocks)
		{
			if(Block.m_pData)
				continue;
			Block = {AllocateDefault(Bytes, Alignment), Bytes, Alignment};
			++m_Live;
			return Block.m_pData;
		}
		throw std::bad_alloc();
	}
	void Deallocate(void *pData, size_t Bytes, size_t Alignment) noexcept override
	{
		for(auto &Block : m_aBlocks)
		{
			if(Block.m_pData != pData)
				continue;
			EXPECT_EQ(Bytes, Block.m_Bytes);
			EXPECT_EQ(Alignment, Block.m_Alignment);
			DeallocateDefault(pData, Block.m_Bytes, Block.m_Alignment);
			Block = {};
			--m_Live;
			return;
		}
		ADD_FAILURE() << "allocation resource released an unowned block";
	}
};

#endif
