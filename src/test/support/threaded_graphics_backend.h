#ifndef TEST_SUPPORT_THREADED_GRAPHICS_BACKEND_H
#define TEST_SUPPORT_THREADED_GRAPHICS_BACKEND_H

#include <engine/client/backend_sdl.h>

#include <stdexcept>
#include <utility>

// 只替代窗口与 GPU 边界，提交、等待、错误消费均调用生产线程后端。
class CTestGraphicsBackend : public CGraphicsBackend_Threaded
{
	TTwGraphicsGpuList m_Gpus;
	TGLBackendReadPresentedImageData m_ReadImage;
	bool m_Running = false;

public:
	CTestGraphicsBackend() :
		CGraphicsBackend_Threaded([](const char *, const char *) { return "translated driver advice"; }) {}
	explicit CTestGraphicsBackend(TTranslateFunc Translate) :
		CGraphicsBackend_Threaded(std::move(Translate)) {}
	explicit CTestGraphicsBackend(ICommandProcessor &Processor) :
		CTestGraphicsBackend()
	{
		if(!Start(Processor))
			throw std::runtime_error("failed to start graphics test backend");
	}
	~CTestGraphicsBackend() override { Stop(); }
	bool Start(ICommandProcessor &Processor, THREAD_CREATE pfnCreate = thread_try_init)
	{
		m_Running = StartProcessor(&Processor, pfnCreate);
		return m_Running;
	}
	void Stop()
	{
		if(m_Running)
		{
			StopProcessor();
			m_Running = false;
		}
	}
	void Restart(ICommandProcessor &Processor)
	{
		Stop();
		if(!Start(Processor))
			throw std::runtime_error("failed to restart graphics test backend");
	}

	int Init(const char *, int *, int *, int *, int *, int *, int, int *, int *, int *, int *, IStorage *) override { return 0; }
	int Shutdown() override { return 0; }
	uint64_t TextureMemoryUsage() const override { return 0; }
	uint64_t BufferMemoryUsage() const override { return 0; }
	uint64_t StreamedMemoryUsage() const override { return 0; }
	uint64_t StagingMemoryUsage() const override { return 0; }
	const TTwGraphicsGpuList &GetGpus() const override { return m_Gpus; }
	void GetVideoModes(CVideoMode *, int, int *, float, int, int, int) override {}
	bool GetCurrentVideoMode(CVideoMode &, float, int, int, int) override { return false; }
	int GetNumScreens() const override { return 0; }
	const char *GetScreenName(int) const override { return ""; }
	void Minimize() override {}
	void HideWindow() override {}
	void ShowWindow() override {}
	void SetWindowParams(int, bool) override {}
	bool SetWindowScreen(int, bool, ivec2 *) override { return false; }
	bool UpdateDisplayMode(int, ivec2 *) override { return false; }
	uint32_t GetWindowId() const override { return 0; }
	int GetWindowScreen() override { return 0; }
	int WindowActive() override { return 0; }
	int WindowOpen() override { return 0; }
	void SetWindowGrab(bool) override {}
	bool ResizeWindow(int, int, int) override { return false; }
	void GetViewportSize(int &, int &) override {}
	void GetDisplayCutoutInsets(int &, int &) override {}
	void NotifyWindow() override {}
	bool IsScreenKeyboardShown() override { return false; }
	void WindowDestroyNtf(uint32_t) override {}
	void WindowCreateNtf(uint32_t) override {}
	bool GetDriverVersion(EGraphicsDriverAgeType, int &, int &, int &, const char *&, EBackendType) override { return false; }
	const char *GetVendorString() override { return ""; }
	const char *GetVersionString() override { return ""; }
	const char *GetRendererString() override { return ""; }
	EBackendType GetBackendType() const override { return BACKEND_TYPE_VULKAN; }
	TGLBackendReadPresentedImageData &GetReadPresentedImageDataFuncUnsafe() override { return m_ReadImage; }
	std::optional<int> ShowMessageBox(const IGraphics::CMessageBox &) override { return std::nullopt; }
};

#endif
