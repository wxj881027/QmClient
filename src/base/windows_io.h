#ifndef BASE_WINDOWS_IO_H
#define BASE_WINDOWS_IO_H

#include "detect.h"
#include "types.h"

#if defined(CONF_FAMILY_WINDOWS)
#include <cstdint>
#include <cstdio>

// 操作对象只在当前调用中借用，不保存全局状态；Open 方法失败时不得接管输入资源。
class CWindowsFileOperations
{
public:
	virtual ~CWindowsFileOperations() = default;
	virtual int OpenDescriptor(intptr_t Handle) noexcept;
	virtual FILE *OpenStream(int Descriptor, const char *pMode) noexcept;
	virtual void CloseNativeHandle(intptr_t Handle) noexcept;
	virtual void CloseDescriptor(int Descriptor) noexcept;
};

// 接管有效的 Win32 文件句柄。成功由返回的流负责关闭，失败按已完成的转交阶段释放。
// pMode 沿用 io_open 的 rb/wb/ab；返回空值时保留引发失败的 errno。
IOHANDLE windows_io_from_handle(intptr_t Handle, const char *pMode, CWindowsFileOperations *pOperations = nullptr) noexcept;
#endif

#endif // BASE_WINDOWS_IO_H
