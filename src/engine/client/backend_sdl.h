#ifndef ENGINE_CLIENT_BACKEND_SDL_H
#define ENGINE_CLIENT_BACKEND_SDL_H

#include <base/detect.h>

#include <engine/client/backend/backend_base.h>
#include <engine/client/graphics_threaded.h>
#include <engine/graphics.h>

#ifndef BACKEND_NO_SDL
#include <SDL_video.h>
#else
struct SDL_Window;
typedef void *SDL_GLContext;
#endif

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#if defined(CONF_PLATFORM_MACOS) || defined(CONF_PLATFORM_IOS)
#include <objc/message.h>
#include <objc/runtime.h>

class CAutoreleasePool
{
private:
	id m_Pool;

public:
	CAutoreleasePool()
	{
		Class NSAutoreleasePoolClass = (Class)objc_getClass("NSAutoreleasePool");
		m_Pool = class_createInstance(NSAutoreleasePoolClass, 0);
		SEL selector = sel_registerName("init");
		((id (*)(id, SEL))objc_msgSend)(m_Pool, selector);
	}

	~CAutoreleasePool()
	{
		SEL selector = sel_registerName("drain");
		((id (*)(id, SEL))objc_msgSend)(m_Pool, selector);
	}
};
#endif

// basic threaded backend, abstract, missing init and shutdown functions
class CGraphicsBackend_Threaded : public IGraphicsBackend
{
private:
	TTranslateFunc m_TranslateFunc;
	std::string m_FatalError;
	SGfxWarningContainer m_Warning;
	std::atomic_bool m_SubmissionStopped = false;

public:
	// constructed on the main thread, the rest of the functions is run on the render thread
	class ICommandProcessor
	{
	public:
		virtual ~ICommandProcessor() = default;
		virtual void RunBuffer(CCommandBuffer *pBuffer) = 0;

		virtual const SGfxErrorContainer &GetError() const = 0;
		virtual void ClearError() = 0;
		virtual void ErroneousCleanup() = 0;
		// 清除已记录的致命错误。默认什么都不做，只有能安全重置错误状态的后端
		// 才需要覆写它。@see CGraphicsBackend_Threaded::TakeFatalError
		virtual void ClearFatalError() {}

		virtual const SGfxWarningContainer &GetWarning() const = 0;
	};

	CGraphicsBackend_Threaded(TTranslateFunc &&TranslateFunc);

	void RunBuffer(CCommandBuffer *pBuffer) override;
	void RunBufferSingleThreadedUnsafe(CCommandBuffer *pBuffer) override;
	bool IsIdle() const override;
	void WaitForIdle() override;

	void ProcessError(const SGfxErrorContainer &Error);

protected:
	void StartProcessor(ICommandProcessor *pProcessor);
	void StopProcessor();
	void ResetSubmissionStopForCleanup()
	{
		m_SubmissionStopped.store(false, std::memory_order_relaxed);
	}

	bool HasWarning() const
	{
		return m_Warning.m_WarningType != GFX_WARNING_TYPE_NONE;
	}

private:
	ICommandProcessor *m_pProcessor;
	std::atomic_bool m_Shutdown;
#if !defined(CONF_PLATFORM_EMSCRIPTEN)
	std::mutex m_BufferSwapMutex;
	std::condition_variable m_BufferSwapCond;
	CCommandBuffer *m_pBuffer;
	bool m_Started = false;
	std::atomic_bool m_BufferInProcess;
	void *m_pThread;
	static void ThreadFunc(void *pUser);
#endif

public:
	const char *GetFatalError() const override;
	// 非破坏性查询：图形后端是否已记录致命错误。运行时错误会停止后续提交，
	// 由主循环消费并进入恢复流程；初始化错误仍由 ProcessError 直接报告。
	bool HasFatalError() const override;
	// 「检查并清除」致命错误标记：返回 true 表示刚刚消费掉一个致命错误。
	// 收尾流程仍可能向后端提交清理命令，停止标志保证这些提交不会再次执行。
	bool TakeFatalError();
	bool GetWarning(std::vector<std::string> &WarningStrings) override;
};

// takes care of implementation independent operations
class CCommandProcessorFragment_General
{
	void Cmd_Signal(const CCommandBuffer::SCommand_Signal *pCommand);

public:
	bool RunCommand(const CCommandBuffer::SCommand *pBaseCommand);
};

struct SBackendCapabilities
{
	bool m_TileBuffering = false;
	bool m_QuadBuffering = false;
	bool m_TextBuffering = false;
	bool m_QuadContainerBuffering = false;

	bool m_MipMapping = false;
	bool m_NPOTTextures = false;
	bool m_3DTextures = false;
	bool m_2DArrayTextures = false;
	bool m_2DArrayTexturesAsExtension = false;
	bool m_ShaderSupport = false;
	bool m_MediaIslandSdf = false;
	bool m_RoundedRectSdf = false;
	std::atomic<bool> m_TexturedMsdf{false};
	bool m_RenderTargets = false;
	bool m_RenderTargetGaussianBlur = false;
	bool m_BackbufferCapture = false;
	bool m_RenderTargetExternalPassRequiresSingleSample = false;
	const char *m_pRenderTargetSupportReason = "not_initialized";

	// use quads as much as possible, even if the user config says otherwise
	bool m_TrianglesAsQuads = false;

	int m_ContextMajor = 0;
	int m_ContextMinor = 0;
	int m_ContextPatch = 0;

	// 只保存从 GL_VERSION/GLES_VERSION 解析出的真实上下文版本。
	// m_Context* 可能因兼容性降级而变化，不能用于展示实际驱动版本。
	int m_DetectedContextMajor = 0;
	int m_DetectedContextMinor = 0;
	int m_DetectedContextPatch = 0;

	void Reset()
	{
		m_TileBuffering = false;
		m_QuadBuffering = false;
		m_TextBuffering = false;
		m_QuadContainerBuffering = false;
		m_MipMapping = false;
		m_NPOTTextures = false;
		m_3DTextures = false;
		m_2DArrayTextures = false;
		m_2DArrayTexturesAsExtension = false;
		m_ShaderSupport = false;
		m_MediaIslandSdf = false;
		m_RoundedRectSdf = false;
		m_TexturedMsdf.store(false, std::memory_order_relaxed);
		m_RenderTargets = false;
		m_RenderTargetGaussianBlur = false;
		m_BackbufferCapture = false;
		m_RenderTargetExternalPassRequiresSingleSample = false;
		m_pRenderTargetSupportReason = "not_initialized";
		m_TrianglesAsQuads = false;
		m_ContextMajor = 0;
		m_ContextMinor = 0;
		m_ContextPatch = 0;
		m_DetectedContextMajor = 0;
		m_DetectedContextMinor = 0;
		m_DetectedContextPatch = 0;
	}
};

// takes care of sdl related commands
class CCommandProcessorFragment_SDL
{
	// SDL stuff
	SDL_Window *m_pWindow = nullptr;
	SDL_GLContext m_GLContext = nullptr;

public:
	enum
	{
		CMD_INIT = CCommandBuffer::CMDGROUP_PLATFORM_SDL,
		CMD_SHUTDOWN,
	};

	struct SCommand_Init : public CCommandBuffer::SCommand
	{
		SCommand_Init() :
			SCommand(CMD_INIT) {}
		SDL_Window *m_pWindow;
		SDL_GLContext m_GLContext;
	};

	struct SCommand_Shutdown : public CCommandBuffer::SCommand
	{
		SCommand_Shutdown() :
			SCommand(CMD_SHUTDOWN) {}
	};

private:
	void Cmd_Init(const SCommand_Init *pCommand);
	void Cmd_Shutdown(const SCommand_Shutdown *pCommand);
	void Cmd_Swap(const CCommandBuffer::SCommand_Swap *pCommand);
	void Cmd_VSync(const CCommandBuffer::SCommand_VSync *pCommand);
	void Cmd_WindowCreateNtf(const CCommandBuffer::SCommand_WindowCreateNtf *pCommand);
	void Cmd_WindowDestroyNtf(const CCommandBuffer::SCommand_WindowDestroyNtf *pCommand);

public:
	CCommandProcessorFragment_SDL();

	bool RunCommand(const CCommandBuffer::SCommand *pBaseCommand);
};

// command processor implementation, uses the fragments to combine into one processor
class CCommandProcessor_SDL_GL : public CGraphicsBackend_Threaded::ICommandProcessor
{
	CCommandProcessorFragment_GLBase *m_pGLBackend;
	CCommandProcessorFragment_SDL m_SDL;
	CCommandProcessorFragment_General m_General;

	EBackendType m_BackendType;

	SGfxErrorContainer m_Error;
	SGfxWarningContainer m_Warning;

public:
	CCommandProcessor_SDL_GL(EBackendType BackendType, int GLMajor, int GLMinor, int GLPatch);
	~CCommandProcessor_SDL_GL() override;
	void RunBuffer(CCommandBuffer *pBuffer) override;

	const SGfxErrorContainer &GetError() const override;
	void ClearError() override { m_Error = {}; }
	void ErroneousCleanup() override;
	// 清除已记录的致命错误。只允许在「已经决定不再信任本帧图形输出」时调用，
	// 目的是让随后的收尾流程（Shutdown 仍会提交清理命令）不再重复触发断言。
	void ClearFatalError() override;

	const SGfxWarningContainer &GetWarning() const override;

	void HandleError();
	void HandleWarning();
};

static constexpr size_t gs_GpuInfoStringSize = 256;

// graphics backend implemented with SDL and the graphics library @see EBackendType
class CGraphicsBackend_SDL_GL : public CGraphicsBackend_Threaded
{
	SDL_Window *m_pWindow = nullptr;
	SDL_GLContext m_GLContext = nullptr;
	ICommandProcessor *m_pProcessor = nullptr;
	std::atomic<uint64_t> m_TextureMemoryUsage{0};
	std::atomic<uint64_t> m_BufferMemoryUsage{0};
	std::atomic<uint64_t> m_StreamMemoryUsage{0};
	std::atomic<uint64_t> m_StagingMemoryUsage{0};

	TTwGraphicsGpuList m_GpuList;

	TGLBackendReadPresentedImageData m_ReadPresentedImageDataFunc;

	int m_NumScreens;

	SBackendCapabilities m_Capabilities;

	char m_aVendorString[gs_GpuInfoStringSize] = {};
	char m_aVersionString[gs_GpuInfoStringSize] = {};
	char m_aRendererString[gs_GpuInfoStringSize] = {};

	EBackendType m_BackendType = BACKEND_TYPE_AUTO;
	EBackendType m_BackendOverride = BACKEND_TYPE_AUTO;

	char m_aErrorString[256];

	EBackendType DetectBackend() const;
	static void ClampDriverVersion(EBackendType BackendType);

public:
	CGraphicsBackend_SDL_GL(TTranslateFunc &&TranslateFunc);
	void SetBackendOverride(EBackendType BackendType) override { m_BackendOverride = BackendType; }
	int Init(const char *pName, int *pScreen, int *pWidth, int *pHeight, int *pRefreshRate, int *pFsaaSamples, int Flags, int *pDesktopWidth, int *pDesktopHeight, int *pCurrentWidth, int *pCurrentHeight, class IStorage *pStorage) override;
	int Shutdown() override;

	uint64_t TextureMemoryUsage() const override;
	uint64_t BufferMemoryUsage() const override;
	uint64_t StreamedMemoryUsage() const override;
	uint64_t StagingMemoryUsage() const override;

	const TTwGraphicsGpuList &GetGpus() const override;

	int GetNumScreens() const override { return m_NumScreens; }
	const char *GetScreenName(int Screen) const override;

	void GetVideoModes(CVideoMode *pModes, int MaxModes, int *pNumModes, float HiDPIScale, int MaxWindowWidth, int MaxWindowHeight, int ScreenId) override;
	void GetCurrentVideoMode(CVideoMode &CurMode, float HiDPIScale, int MaxWindowWidth, int MaxWindowHeight, int ScreenId) override;

	void Minimize() override;
	void HideWindow() override;
	void ShowWindow() override;
	void SetWindowParams(int FullscreenMode, bool IsBorderless) override;
	bool SetWindowScreen(int Index, bool MoveToCenter, ivec2 *pDesktopSize) override;
	bool UpdateDisplayMode(int Index, ivec2 *pDesktopSize) override;
	int GetWindowScreen() override;
	int WindowActive() override;
	int WindowOpen() override;
	void SetWindowGrab(bool Grab) override;
	bool ResizeWindow(int w, int h, int RefreshRate) override;
	void GetViewportSize(int &w, int &h) override;
	void GetDisplayCutoutInsets(int &Left, int &Right) override;
	void NotifyWindow() override;
	bool IsScreenKeyboardShown() override;

	void WindowDestroyNtf(uint32_t WindowId) override;
	void WindowCreateNtf(uint32_t WindowId) override;

	bool GetDriverVersion(EGraphicsDriverAgeType DriverAgeType, int &Major, int &Minor, int &Patch, const char *&pName, EBackendType BackendType) override;
	bool GetDetectedContextVersion(int &Major, int &Minor, int &Patch, const char *&pName) override;
	bool IsConfigModernAPI() override
	{
		if(g_Config.m_GfxGLMajor == 0 && m_Capabilities.m_DetectedContextMajor > 0)
		{
			if(m_BackendType == BACKEND_TYPE_OPENGL)
				return (m_Capabilities.m_DetectedContextMajor == 3 && m_Capabilities.m_DetectedContextMinor >= 3) || m_Capabilities.m_DetectedContextMajor >= 4;
			if(m_BackendType == BACKEND_TYPE_OPENGL_ES)
				return m_Capabilities.m_DetectedContextMajor >= 3;
		}
		return IsModernAPI(m_BackendType);
	}
	bool HasMediaIslandSdf() override { return m_Capabilities.m_MediaIslandSdf; }
	bool HasRoundedRectSdf() override { return m_Capabilities.m_RoundedRectSdf; }
	bool HasTexturedMsdf() override { return m_Capabilities.m_TexturedMsdf.load(std::memory_order_acquire); }
	bool UseTrianglesAsQuad() override { return m_Capabilities.m_TrianglesAsQuads; }
	bool HasTileBuffering() override { return m_Capabilities.m_TileBuffering; }
	bool HasQuadBuffering() override { return m_Capabilities.m_QuadBuffering; }
	bool HasTextBuffering() override { return m_Capabilities.m_TextBuffering; }
	bool HasQuadContainerBuffering() override { return m_Capabilities.m_QuadContainerBuffering; }
	bool HasRenderTargets() override { return m_Capabilities.m_RenderTargets; }
	bool HasRenderTargetGaussianBlur() override { return m_Capabilities.m_RenderTargetGaussianBlur; }
	bool HasBackbufferCapture() override { return m_Capabilities.m_BackbufferCapture; }
	bool RenderTargetExternalPassRequiresSingleSample() override { return m_Capabilities.m_RenderTargetExternalPassRequiresSingleSample; }
	const char *RenderTargetSupportReason() override { return m_Capabilities.m_RenderTargets ? "supported" : m_Capabilities.m_pRenderTargetSupportReason; }
	bool Uses2DTextureArrays() override { return m_Capabilities.m_2DArrayTextures; }
	bool HasTextureArraysSupport() const override { return m_Capabilities.m_2DArrayTextures || m_Capabilities.m_3DTextures; }

	const char *GetErrorString() override
	{
		if(m_aErrorString[0] != '\0')
			return m_aErrorString;

		return nullptr;
	}

	const char *GetVendorString() override
	{
		return m_aVendorString;
	}

	const char *GetVersionString() override
	{
		return m_aVersionString;
	}

	const char *GetRendererString() override
	{
		return m_aRendererString;
	}

	EBackendType GetBackendType() const override { return m_BackendType; }

	TGLBackendReadPresentedImageData &GetReadPresentedImageDataFuncUnsafe() override;

	std::optional<int> ShowMessageBox(const IGraphics::CMessageBox &MessageBox) override;

	static bool IsModernAPI(EBackendType BackendType);
};

#endif // ENGINE_CLIENT_BACKEND_SDL_H
