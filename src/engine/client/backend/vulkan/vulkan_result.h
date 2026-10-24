#ifndef ENGINE_CLIENT_BACKEND_VULKAN_VULKAN_RESULT_H
#define ENGINE_CLIENT_BACKEND_VULKAN_VULKAN_RESULT_H

#include <base/dbg.h>
#include <base/str.h>

#include <vulkan/vulkan_core.h>

namespace vulkan_diagnostics
{
	template<typename TWarning, typename TDeviceFault>
	const char *DescribeResult(VkResult Result, bool Verbose, bool &RecreateSwapChain, TWarning &&Warning, TDeviceFault &&DeviceFault) noexcept
	{
		const char *pCriticalError = nullptr;
		const char *pLog = nullptr;
		const char *pWarning = nullptr;
		switch(Result)
		{
		case VK_ERROR_OUT_OF_HOST_MEMORY:
			pLog = pCriticalError = "host ran out of memory";
			break;
		case VK_ERROR_OUT_OF_DEVICE_MEMORY:
			pLog = pCriticalError = "device ran out of memory";
			break;
		case VK_ERROR_DEVICE_LOST:
			pLog = pCriticalError = "device lost";
			break;
		case VK_ERROR_OUT_OF_DATE_KHR:
			RecreateSwapChain = true;
			if(Verbose)
				pLog = "queueing swap chain recreation because the current is out of date";
			break;
		case VK_SUBOPTIMAL_KHR:
			RecreateSwapChain = true;
			if(Verbose)
				pLog = "queueing swap chain recreation because the current is sub optimal";
			break;
		case VK_ERROR_SURFACE_LOST_KHR:
			pLog = "surface lost";
			break;
		case VK_ERROR_INCOMPATIBLE_DRIVER:
			pLog = pCriticalError = "no compatible driver found. Vulkan 1.1 is required.";
			break;
		case VK_ERROR_INITIALIZATION_FAILED:
			pLog = pCriticalError = "initialization failed for unknown reason.";
			break;
		case VK_ERROR_LAYER_NOT_PRESENT:
			pWarning = "One Vulkan layer was not present. (try to disable them)";
			break;
		case VK_ERROR_EXTENSION_NOT_PRESENT:
			pWarning = "One Vulkan extension was not present. (try to disable them)";
			break;
		case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
			pLog = "native window in use";
			break;
		case VK_SUCCESS:
			break;
		default:
			pCriticalError = "unknown error";
			break;
		}

		try
		{
			if(pLog != nullptr)
				dbg_msg("vulkan", "%s", pLog);
			if(pWarning != nullptr)
				Warning(pWarning);
			if(Result == VK_ERROR_DEVICE_LOST)
				DeviceFault();
		}
		catch(...)
		{
			// 附加诊断可能再次分配失败，仍将原始结果交给调用方停止或回退。
		}

		if(pCriticalError == nullptr)
			return nullptr;
		// 错误描述仅来自上面的固定文本；保留结果码，不再扩容字符串。
		// 每个线程独立存储，返回值有效至同线程的下一次致命结果描述。
		static thread_local char s_aError[128];
		str_format(s_aError, sizeof(s_aError), "%s (VkResult %d)", pCriticalError, (int)Result);
		return s_aError;
	}
}

#endif
