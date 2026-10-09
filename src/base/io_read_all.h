#ifndef BASE_IO_READ_ALL_H
#define BASE_IO_READ_ALL_H

#include "system.h"

// 整文件读取的分配边界；调用者负责用同一分配器释放成功返回的缓冲。
// 每次调用独立传入上下文，避免为故障注入修改全局分配器。
struct SIOReadAllAllocator
{
	void *(*m_pfnReallocate)(void *pBuffer, size_t Size, void *pUser);
	void (*m_pfnFree)(void *pBuffer, void *pUser);
	void *m_pUser;
};

bool io_read_all_with_allocator(IOHANDLE File, void **ppResult, unsigned *pResultLength, const SIOReadAllAllocator &Allocator);

#endif // BASE_IO_READ_ALL_H
