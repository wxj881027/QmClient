#ifndef ENGINE_CLIENT_BACKEND_GRAPHICS_COMMAND_EXECUTION_H
#define ENGINE_CLIENT_BACKEND_GRAPHICS_COMMAND_EXECUTION_H

#include "backend_base.h"

#include <base/dbg.h>

// 平台和通用命令仍由调用方分发；异常收尾与后端诊断在同一处完成。
template<typename TRunPlatformCommand>
ERunCommandReturnTypes RunGraphicsCommandBuffer(CCommandProcessorFragment_GLBase &Backend, CCommandBuffer &Buffer, TRunPlatformCommand &&RunPlatformCommand) noexcept
{
	const CCommandBuffer::SCommand *pExecutingCommand = Buffer.Head();
	try
	{
		Backend.StartCommands(Buffer.m_CommandCount, Buffer.m_RenderCallCount);
		ERunCommandReturnTypes Result = RUN_COMMAND_COMMAND_HANDLED;
		for(const CCommandBuffer::SCommand *pCommand = Buffer.Head(); pCommand; pCommand = pCommand->m_pNext)
		{
			pExecutingCommand = pCommand;
			Result = Backend.RunCommand(pCommand);
			if(Result == RUN_COMMAND_COMMAND_ERROR || Result == RUN_COMMAND_COMMAND_WARNING)
				break;
			if(Result == RUN_COMMAND_COMMAND_HANDLED || RunPlatformCommand(pCommand))
				continue;
			dbg_assert_failed("Unknown graphics command %d", pCommand->m_Cmd);
		}
		Backend.EndCommands();
		// 工作线程可能在最后一次 RunCommand 之后才失败。
		if(Backend.GetError().m_ErrorType != GFX_ERROR_TYPE_NONE)
			return RUN_COMMAND_COMMAND_ERROR;
		return Result == RUN_COMMAND_COMMAND_WARNING || Result == RUN_COMMAND_COMMAND_ERROR ? Result : RUN_COMMAND_COMMAND_HANDLED;
	}
	catch(...)
	{
		// Abort 必须先结束所有 CPU 借用；返回后才允许回收上传并复用命令区。
		const auto Result = Backend.HandleCommandException(pExecutingCommand);
		Buffer.DiscardUploads();
		return Result;
	}
}

#endif
