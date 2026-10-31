#include "prediction_allocation_failure.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <new>

// 覆盖只链接进客户端预测测试程序；故障开关仅在测试目标操作期间启用。
void *operator new(std::size_t Size)
{
	if(CPredictionAllocationFailure::FailNextAllocation())
		throw std::bad_alloc();
	if(void *pMemory = std::malloc(Size == 0 ? 1 : Size))
		return pMemory;
	throw std::bad_alloc();
}

void *operator new[](std::size_t Size) { return ::operator new(Size); }
// nothrow 入口也使用同一 malloc/free 配对，不能混用运行库的默认分配入口。
void *operator new(std::size_t Size, const std::nothrow_t &) noexcept
{
	try
	{
		return ::operator new(Size);
	}
	catch(...)
	{
		return nullptr;
	}
}
void *operator new[](std::size_t Size, const std::nothrow_t &Tag) noexcept { return ::operator new(Size, Tag); }
void operator delete(void *pMemory) noexcept { std::free(pMemory); }
void operator delete[](void *pMemory) noexcept { std::free(pMemory); }
void operator delete(void *pMemory, std::size_t) noexcept { std::free(pMemory); }
void operator delete[](void *pMemory, std::size_t) noexcept { std::free(pMemory); }
void operator delete(void *pMemory, const std::nothrow_t &) noexcept { std::free(pMemory); }
void operator delete[](void *pMemory, const std::nothrow_t &) noexcept { std::free(pMemory); }

int main(int argc, char **argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
