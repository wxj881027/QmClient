#ifndef ENGINE_CLIENT_BACKEND_VULKAN_VULKAN_DIAGNOSTICS_H
#define ENGINE_CLIENT_BACKEND_VULKAN_VULKAN_DIAGNOSTICS_H

#include <engine/client/backend/backend_base.h>

#include <atomic>

namespace vulkan_diagnostics
{
	// 调用方持有诊断锁；先发布无需分配的状态，文本失败不能取消停止或初始化回退。
	template<typename TWriteDetails>
	void PublishError(SGfxErrorContainer &Error, SGfxWarningContainer &Warning, std::atomic<bool> &HasError, bool Runtime, EGfxErrorType Type, TWriteDetails &&WriteDetails) noexcept
	{
		const bool FirstError = Runtime && !HasError;
		if(Runtime)
		{
			if(FirstError)
				Error.m_ErrorType = Type;
			HasError = true;
		}
		else
			Warning.m_WarningType = GFX_WARNING_TYPE_INIT_FAILED;
		try
		{
			WriteDetails(FirstError);
		}
		catch(...)
		{
			// 外层诊断消费支持没有文本的错误类型，并保留已经收集到的明细。
		}
	}

	template<typename TWriteDetails>
	void PublishWarning(SGfxWarningContainer &Warning, EGfxWarningType Type, TWriteDetails &&WriteDetails) noexcept
	{
		Warning.m_WarningType = Type;
		try
		{
			WriteDetails();
		}
		catch(...)
		{
			// 文本仅用于解释警告；分配失败仍保留原有明细和本次警告类型。
		}
	}
}

#endif
