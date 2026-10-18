#include <base/dbg.h>
#include <base/log.h>
#include <base/math.h>
#include <base/sphore.h>
#include <base/str.h>
#include <base/thread.h>

#include <engine/client/backend_sdl.h>
#include <engine/client/graphics_threaded.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#if defined(CONF_VIDEORECORDER) && !defined(BACKEND_NO_SDL)
#include <engine/shared/video.h>
#endif

#if defined(CONF_PLATFORM_MACOS)
#include <CoreFoundation/CFRunLoop.h>

#include <chrono>
#include <cstdlib>
#endif

class IStorage;

// ------------ CGraphicsBackend_Threaded

// Run everything single threaded when compiling for Emscripten, as context binding does not work outside of the main thread with SDL2.
// TODO SDL3: Check if SDL3 supports threaded graphics and PROXY_TO_PTHREAD, OFFSCREENCANVAS_SUPPORT and OFFSCREEN_FRAMEBUFFER correctly.
#if !defined(CONF_PLATFORM_EMSCRIPTEN)
void CGraphicsBackend_Threaded::ThreadFunc(void *pUser)
{
	auto *pSelf = (CGraphicsBackend_Threaded *)pUser;
	std::unique_lock<std::mutex> Lock(pSelf->m_BufferSwapMutex);
	// notify, that the thread started
	pSelf->m_Started = true;
	pSelf->m_BufferSwapCond.notify_all();
	while(!pSelf->m_Shutdown)
	{
		pSelf->m_BufferSwapCond.wait(Lock, [&pSelf] { return pSelf->m_pBuffer != nullptr || pSelf->m_Shutdown; });
		if(pSelf->m_pBuffer)
		{
#if defined(CONF_PLATFORM_MACOS) || defined(CONF_PLATFORM_IOS)
			CAutoreleasePool AutoreleasePool;
#endif
			Lock.unlock();
			pSelf->m_pProcessor->RunBuffer(pSelf->m_pBuffer);
			Lock.lock();

			pSelf->m_pBuffer = nullptr;
			// 发布本批命令及错误状态，主线程确认空闲后才可读取处理器。
			pSelf->m_BufferInProcess.store(false, std::memory_order_release);
			pSelf->m_BufferSwapCond.notify_all();

#if defined(CONF_VIDEORECORDER) && !defined(BACKEND_NO_SDL)
			if(IVideo::Current())
				IVideo::Current()->NextVideoFrameThread();
#endif
		}
	}
}
#endif

CGraphicsBackend_Threaded::CGraphicsBackend_Threaded(TTranslateFunc &&TranslateFunc) :
	m_TranslateFunc(std::move(TranslateFunc))
{
	m_pProcessor = nullptr;
	m_Shutdown = true;
#if !defined(CONF_PLATFORM_EMSCRIPTEN)
	m_pBuffer = nullptr;
	m_BufferInProcess.store(false, std::memory_order_relaxed);
#endif
}

bool CGraphicsBackend_Threaded::StartProcessor(ICommandProcessor *pProcessor, THREAD_CREATE pfnCreate)
{
	dbg_assert(m_Shutdown, "Processor was already not shut down.");
	m_FatalError.clear();
	m_FatalErrorTextUnavailable = false;
	ResetSubmissionStopForCleanup();
	m_Shutdown = false;
	m_pProcessor = pProcessor;
#if !defined(CONF_PLATFORM_EMSCRIPTEN)
	std::unique_lock<std::mutex> Lock(m_BufferSwapMutex);
	m_Started = false;
	m_pThread = pfnCreate(ThreadFunc, this, "Graphics thread");
	if(!m_pThread)
	{
		m_pProcessor = nullptr;
		m_Shutdown = true;
		m_SubmissionStopped = true;
		return false;
	}
	// wait for the thread to start
	m_BufferSwapCond.wait(Lock, [this]() -> bool { return m_Started; });
#else
	(void)pfnCreate;
#endif
	return true;
}

void CGraphicsBackend_Threaded::StopProcessor()
{
	dbg_assert(!m_Shutdown, "Processor was already shut down.");
	m_SubmissionStopped = true;
#if !defined(CONF_PLATFORM_EMSCRIPTEN)
	{
		std::unique_lock<std::mutex> Lock(m_BufferSwapMutex);
		m_Shutdown = true;
		m_BufferSwapCond.notify_all();
	}
	thread_wait(m_pThread);
	m_pThread = nullptr;
	m_Started = false;
#else
	m_Shutdown = true;
#endif
	ICommandProcessor *pProcessor = m_pProcessor;
	m_pProcessor = nullptr;
	try
	{
		// 先完成线程回收；诊断复制失败不能阻止调用方释放处理器和窗口。
		// 候选警告完整复制后再替换，避免枚举已更新但字符串仍是旧内容。
		SGfxWarningContainer Warning = pProcessor->GetWarning();
		m_Warning.m_vWarnings.swap(Warning.m_vWarnings);
		m_Warning.m_WarningType = Warning.m_WarningType;
	}
	catch(...)
	{
		log_error("gfx", "Failed to preserve graphics warnings during shutdown.");
	}
}

void CGraphicsBackend_Threaded::RunBuffer(CCommandBuffer *pBuffer)
{
	if(m_SubmissionStopped.load(std::memory_order_acquire))
		return;

	const SGfxErrorContainer *pError = nullptr;
#if defined(CONF_PLATFORM_EMSCRIPTEN)
	const SGfxErrorContainer &Error = m_pProcessor->GetError();
	if(Error.m_ErrorType == GFX_ERROR_TYPE_NONE)
	{
		RunBufferSingleThreadedUnsafe(pBuffer);
#if defined(CONF_VIDEORECORDER) && !defined(BACKEND_NO_SDL)
		if(IVideo::Current())
			IVideo::Current()->NextVideoFrameThread();
#endif
	}
	else
		pError = &Error;
#else
	WaitForIdle();
	{
		std::unique_lock<std::mutex> Lock(m_BufferSwapMutex);
		const SGfxErrorContainer &Error = m_pProcessor->GetError();
		if(Error.m_ErrorType == GFX_ERROR_TYPE_NONE)
		{
			m_pBuffer = pBuffer;
			m_BufferInProcess.store(true, std::memory_order_relaxed);
			m_BufferSwapCond.notify_all();
		}
		else
			pError = &Error;
	}
#endif

	// Process error after lock is released to prevent deadlock
	if(pError)
	{
		// 失败时未提交新缓冲，处理器诊断在主线程消费期间保持稳定。
		ProcessError(*pError);
	}
}

void CGraphicsBackend_Threaded::RunBufferSingleThreadedUnsafe(CCommandBuffer *pBuffer)
{
	m_pProcessor->RunBuffer(pBuffer);
}

bool CGraphicsBackend_Threaded::IsIdle() const
{
#if defined(CONF_PLATFORM_EMSCRIPTEN)
	return true;
#else
	return !m_BufferInProcess.load(std::memory_order_acquire);
#endif
}

void CGraphicsBackend_Threaded::WaitForIdle()
{
#if !defined(CONF_PLATFORM_EMSCRIPTEN)
	std::unique_lock<std::mutex> Lock(m_BufferSwapMutex);
#if defined(CONF_PLATFORM_MACOS)
	while(!m_BufferSwapCond.wait_for(Lock, std::chrono::milliseconds(1), [this]() { return m_pBuffer == nullptr; }))
	{
		Lock.unlock();
		CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true);
		Lock.lock();
	}
#else
	m_BufferSwapCond.wait(Lock, [this]() { return m_pBuffer == nullptr; });
#endif
#endif
}

void CGraphicsBackend_Threaded::ProcessError(const SGfxErrorContainer &Error) noexcept
{
	if(Error.m_ErrorType == GFX_ERROR_TYPE_NONE)
		return;
	// 第一份故障已保存后，不让后续收尾错误覆盖报告。
	if(m_SubmissionStopped.exchange(true, std::memory_order_acq_rel))
		return;
	m_FatalErrorTextUnavailable = true;
	try
	{
		std::string Report;
		bool Complete = true;
		for(const auto &ErrStr : Error.m_vErrors)
		{
			if(!Report.empty())
				Report.append("\n");
			if(ErrStr.m_RequiresTranslation)
			{
				const char *pText = m_TranslateFunc(ErrStr.m_Err.c_str(), "");
				if(!pText)
				{
					Complete = false;
					break;
				}
				Report.append(pText);
			}
			else
				Report.append(ErrStr.m_Err);
		}
		if(Complete && !Report.empty())
		{
			m_FatalError.swap(Report);
			m_FatalErrorTextUnavailable = false;
		}
	}
	catch(...)
	{
		// 翻译或字符串分配失败仍保留停止标记，不发布半份诊断。
	}
	if(Error.m_ErrorType == GFX_ERROR_TYPE_INIT)
	{
		dbg_assert_failed("Graphics Error:\n%s", GetFatalError());
	}
	log_error("gfx", "Graphics Error:\n%s", GetFatalError());
}

const char *CGraphicsBackend_Threaded::GetFatalError() const
{
	// 沿用已有未知错误建议；低内存路径不再分配或调用翻译器。
	if(m_FatalErrorTextUnavailable)
		return "Unknown error. Try to change gfx_backend to OpenGL or Vulkan in qmclient/settings.cfg in the config directory and try again.";
	return m_FatalError.c_str();
}

bool CGraphicsBackend_Threaded::HasFatalError() const
{
	if(m_pProcessor == nullptr)
		return false;
	const_cast<CGraphicsBackend_Threaded *>(this)->WaitForIdle();
	return m_pProcessor->GetError().m_ErrorType != GFX_ERROR_TYPE_NONE;
}

bool CGraphicsBackend_Threaded::TakeFatalError()
{
	// 主线程是唯一提交者；空闲标记的 acquire 与命令完成时的 release 配对。
	// 渲染仍在执行时延后消费，避免每帧轮询错误都等待上一批命令完成。
	if(m_pProcessor == nullptr || !IsIdle())
		return false;
	if(m_pProcessor->GetError().m_ErrorType == GFX_ERROR_TYPE_NONE)
		return false;
	// 主循环可能在下一次 RunBuffer 之前消费错误，必须先保存详情并停止提交。
	ProcessError(m_pProcessor->GetError());
	m_pProcessor->ClearFatalError();
	return true;
}

bool CGraphicsBackend_Threaded::GetWarning(std::vector<std::string> &WarningStrings)
{
	if(m_Warning.m_WarningType != GFX_WARNING_TYPE_NONE)
	{
		// 转交已有字符串，不在消费警告时再次分配。
		WarningStrings.swap(m_Warning.m_vWarnings);
		m_Warning.m_vWarnings.clear();
		m_Warning.m_WarningType = GFX_WARNING_TYPE_NONE;
		return true;
	}
	return false;
}

// ------------ CCommandProcessorFragment_General

void CCommandProcessorFragment_General::Cmd_Signal(const CCommandBuffer::SCommand_Signal *pCommand)
{
	pCommand->m_pSemaphore->Signal();
}

bool CCommandProcessorFragment_General::RunCommand(const CCommandBuffer::SCommand *pBaseCommand)
{
	switch(pBaseCommand->m_Cmd)
	{
	case CCommandBuffer::CMD_SIGNAL: Cmd_Signal(static_cast<const CCommandBuffer::SCommand_Signal *>(pBaseCommand)); break;
	default: return false;
	}

	return true;
}
