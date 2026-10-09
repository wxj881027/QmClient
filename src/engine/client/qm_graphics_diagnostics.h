#ifndef ENGINE_CLIENT_QM_GRAPHICS_DIAGNOSTICS_H
#define ENGINE_CLIENT_QM_GRAPHICS_DIAGNOSTICS_H

#include <base/str.h>
#include <base/system.h>

#include <initializer_list>

namespace QmGraphicsDiagnostics
{
	// 分段写入，避免较长的后端诊断把报告尾部的 GPU 信息挤出固定缓冲。
	inline bool WriteReport(const char *pPath, const char *pHeader, const char *pError, const char *pGpuInfo)
	{
		IOHANDLE File = io_open(pPath, IOFLAG_WRITE);
		if(!File)
			return false;
		bool Success = true;
		for(const char *pPart : {pHeader, "\nGraphics error:\n", pError[0] != '\0' ? pError : "(not reported by the backend)", "\n\n", pGpuInfo, "\n"})
		{
			const unsigned Length = str_length(pPart);
			if(io_write(File, pPart, Length) != Length)
			{
				Success = false;
				break;
			}
		}
		if(io_sync(File) != 0)
			Success = false;
		if(io_close(File) != 0)
			Success = false;
		return Success;
	}
} // namespace QmGraphicsDiagnostics

#endif
