#include <engine/client/backend_sdl.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

namespace
{
	class CFailingGraphicsProcessor : public CGraphicsBackend_Threaded::ICommandProcessor
	{
		SGfxErrorContainer m_Error;
		SGfxWarningContainer m_Warning;

	public:
		int m_RunCount = 0;
		int m_ClearCount = 0;

		void RunBuffer(CCommandBuffer *) override
		{
			++m_RunCount;
			m_Error.m_ErrorType = GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED;
			m_Error.m_vErrors = {{false, "vkQueueSubmit (frame) failed."}, {false, "device lost (VkResult -4)"}, {true, "driver advice"}};
		}
		const SGfxErrorContainer &GetError() const override { return m_Error; }
		void ClearError() override { m_Error = {}; }
		void ClearFatalError() override
		{
			++m_ClearCount;
			ClearError();
		}
		void ErroneousCleanup() override {}
		const SGfxWarningContainer &GetWarning() const override { return m_Warning; }
	};

	// 只替代窗口与 GPU 边界，提交、等待、错误消费均调用生产线程后端。
	class CTestGraphicsBackend : public CGraphicsBackend_Threaded
	{
		TTwGraphicsGpuList m_Gpus;
		TGLBackendReadPresentedImageData m_ReadImage;

	public:
		CTestGraphicsBackend(CFailingGraphicsProcessor &Processor) :
			CGraphicsBackend_Threaded([](const char *, const char *) { return "translated driver advice"; })
		{
			StartProcessor(&Processor);
		}
		~CTestGraphicsBackend() override { StopProcessor(); }
		void Restart(CFailingGraphicsProcessor &Processor)
		{
			StopProcessor();
			StartProcessor(&Processor);
		}

		int Init(const char *, int *, int *, int *, int *, int *, int, int *, int *, int *, int *, IStorage *) override { return 0; }
		int Shutdown() override { return 0; }
		uint64_t TextureMemoryUsage() const override { return 0; }
		uint64_t BufferMemoryUsage() const override { return 0; }
		uint64_t StreamedMemoryUsage() const override { return 0; }
		uint64_t StagingMemoryUsage() const override { return 0; }
		const TTwGraphicsGpuList &GetGpus() const override { return m_Gpus; }
		void GetVideoModes(CVideoMode *, int, int *, float, int, int, int) override {}
		void GetCurrentVideoMode(CVideoMode &, float, int, int, int) override {}
		int GetNumScreens() const override { return 0; }
		const char *GetScreenName(int) const override { return ""; }
		void Minimize() override {}
		void HideWindow() override {}
		void ShowWindow() override {}
		void SetWindowParams(int, bool) override {}
		bool SetWindowScreen(int, bool, ivec2 *) override { return false; }
		bool UpdateDisplayMode(int, ivec2 *) override { return false; }
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
} // namespace

TEST(GraphicsFatalError, ConsumptionBeforeNextSubmissionPreservesBackendDetails)
{
	CFailingGraphicsProcessor Processor;
	CTestGraphicsBackend Backend(Processor);
	CCommandBuffer Buffer(128, 128);
	Backend.RunBuffer(&Buffer);

	ASSERT_TRUE(Backend.TakeFatalError());
	EXPECT_STREQ(Backend.GetFatalError(), "vkQueueSubmit (frame) failed.\ndevice lost (VkResult -4)\ntranslated driver advice");
	EXPECT_EQ(Processor.m_ClearCount, 1);
	EXPECT_FALSE(Backend.HasFatalError());
}

TEST(GraphicsFatalError, ConsumedFaultStopsFurtherBufferExecution)
{
	CFailingGraphicsProcessor Processor;
	CTestGraphicsBackend Backend(Processor);
	CCommandBuffer Buffer(128, 128);
	Backend.RunBuffer(&Buffer);
	ASSERT_TRUE(Backend.TakeFatalError());

	Backend.RunBuffer(&Buffer);
	Backend.WaitForIdle();
	EXPECT_EQ(Processor.m_RunCount, 1);
}

TEST(GraphicsFatalError, RepeatedConsumptionKeepsTheSavedReport)
{
	CFailingGraphicsProcessor Processor;
	CTestGraphicsBackend Backend(Processor);
	CCommandBuffer Buffer(128, 128);
	Backend.RunBuffer(&Buffer);
	ASSERT_TRUE(Backend.TakeFatalError());

	EXPECT_FALSE(Backend.TakeFatalError());
	EXPECT_EQ(Processor.m_ClearCount, 1);
	EXPECT_STREQ(Backend.GetFatalError(), "vkQueueSubmit (frame) failed.\ndevice lost (VkResult -4)\ntranslated driver advice");
}

TEST(GraphicsFatalError, AlreadyProcessedFaultCanBeConsumedWithoutLosingDetails)
{
	CFailingGraphicsProcessor Processor;
	CTestGraphicsBackend Backend(Processor);
	CCommandBuffer Buffer(128, 128);
	Backend.RunBuffer(&Buffer);
	Backend.WaitForIdle();
	Backend.ProcessError(Processor.GetError());

	ASSERT_TRUE(Backend.TakeFatalError());
	EXPECT_STREQ(Backend.GetFatalError(), "vkQueueSubmit (frame) failed.\ndevice lost (VkResult -4)\ntranslated driver advice");
}

TEST(GraphicsFatalError, HealthyProcessorHasNoFaultToConsume)
{
	CFailingGraphicsProcessor Processor;
	CTestGraphicsBackend Backend(Processor);
	EXPECT_FALSE(Backend.TakeFatalError());
	EXPECT_STREQ(Backend.GetFatalError(), "");
	EXPECT_EQ(Processor.m_ClearCount, 0);
}

TEST(GraphicsFatalError, LaterFaultDoesNotReplaceTheFirstReport)
{
	CFailingGraphicsProcessor Processor;
	CTestGraphicsBackend Backend(Processor);
	CCommandBuffer Buffer(128, 128);
	Backend.RunBuffer(&Buffer);
	Backend.WaitForIdle();
	Backend.ProcessError(Processor.GetError());
	SGfxErrorContainer LaterError;
	LaterError.m_ErrorType = GFX_ERROR_TYPE_SWAP_FAILED;
	LaterError.m_vErrors = {{false, "later shutdown fault"}};
	Backend.ProcessError(LaterError);

	ASSERT_TRUE(Backend.TakeFatalError());
	EXPECT_STREQ(Backend.GetFatalError(), "vkQueueSubmit (frame) failed.\ndevice lost (VkResult -4)\ntranslated driver advice");
}

TEST(GraphicsFatalError, RestartedProcessorClearsSavedFaultAndAcceptsNewBuffers)
{
	CFailingGraphicsProcessor Processor;
	CFailingGraphicsProcessor NewProcessor;
	CTestGraphicsBackend Backend(Processor);
	CCommandBuffer Buffer(128, 128);
	Backend.RunBuffer(&Buffer);
	ASSERT_TRUE(Backend.TakeFatalError());

	Backend.Restart(NewProcessor);
	EXPECT_STREQ(Backend.GetFatalError(), "");
	EXPECT_FALSE(Backend.TakeFatalError());
	Backend.RunBuffer(&Buffer);
	ASSERT_TRUE(Backend.TakeFatalError());
	EXPECT_EQ(NewProcessor.m_RunCount, 1);
}
