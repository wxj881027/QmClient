#include <engine/client/backend/graphics_backend_contract.h>
#include <engine/client/graphics_threaded.h>
#include <engine/client/screenshot_save.h>
#include <engine/engine.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <generated/data_types.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/support/threaded_graphics_backend.h>
#include <test/test.h>

#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
	CImageInfo MakePixel()
	{
		const uint8_t aPixel[] = {20, 40, 60, 255};
		CImageInfo Image;
		Image.m_pData = static_cast<uint8_t *>(std::malloc(sizeof(aPixel)));
		if(Image.m_pData != nullptr)
		{
			mem_copy(Image.m_pData, aPixel, sizeof(aPixel));
			Image.m_Width = 1;
			Image.m_Height = 1;
			Image.m_Format = CImageInfo::FORMAT_RGBA;
		}
		return Image;
	}

	class CCaptureQueueEngine final : public IEngine
	{
	public:
		std::vector<std::shared_ptr<IJob>> m_vJobs;
		void Init() override {}
		void AddJob(std::shared_ptr<IJob> pJob) override { m_vJobs.push_back(std::move(pJob)); }
		void ShutdownJobs() override { m_vJobs.clear(); }
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&pLogger) override {}
	};

	struct SFrameProbe
	{
		CCommandBuffer *m_pSubmitted = nullptr;
		bool m_FailFrameAllocation = false;
		std::optional<CPredictionAllocationFailure> m_AllocationFailure;
		int m_Shutdowns = 0;
		int m_RenderTargetCreates = 0;
	};

	thread_local SFrameProbe *gs_pFrameProbe = nullptr;

	class CFrameProbeBackend final : public CTestGraphicsBackend
	{
	public:
		CFrameProbeBackend(SFrameProbe &Probe, TTranslateFunc &&Translate) :
			CTestGraphicsBackend(std::move(Translate)), m_Probe(Probe) {}

		int Init(const char *, int *, int *pWidth, int *pHeight, int *pRefreshRate, int *, int, int *pDesktopWidth, int *pDesktopHeight, int *pCurrentWidth, int *pCurrentHeight, IStorage *) override
		{
			*pWidth = *pDesktopWidth = *pCurrentWidth = 800;
			*pHeight = *pDesktopHeight = *pCurrentHeight = 600;
			*pRefreshRate = 60;
			if(m_Probe.m_FailFrameAllocation)
			{
				// 第一块缓冲对象及其两段存储分配完成，下一块缓冲的分配失败。
				m_Probe.m_AllocationFailure.emplace(3);
			}
			return 0;
		}

		int Shutdown() override
		{
			WaitForIdle();
			++m_Probe.m_Shutdowns;
			return 0;
		}

		EBackendType GetBackendType() const override { return BACKEND_TYPE_OPENGL; }
		bool HasRenderTargets() override { return true; }
		int WindowActive() override { return 1; }

		void RunBuffer(CCommandBuffer *pBuffer) override
		{
			// 像线程后端一样保留对前端缓冲的借用，直到下次等待或提交。
			WaitForIdle();
			m_Probe.m_pSubmitted = pBuffer;
		}

		void WaitForIdle() override
		{
			auto *pBuffer = std::exchange(m_Probe.m_pSubmitted, nullptr);
			if(pBuffer == nullptr)
				return;
			for(const auto *pCommand = pBuffer->Head(); pCommand != nullptr; pCommand = pCommand->m_pNext)
			{
				if(pCommand->m_Cmd == CCommandBuffer::CMD_TRY_SWAP_AND_SCREENSHOT)
				{
					const auto *pScreenshot = static_cast<const CCommandBuffer::SCommand_TrySwapAndScreenshot *>(pCommand);
					*pScreenshot->m_pImage = MakePixel();
					*pScreenshot->m_pSwapped = true;
				}
				else if(pCommand->m_Cmd == CCommandBuffer::CMD_RENDER_TARGET_CREATE)
				{
					++m_Probe.m_RenderTargetCreates;
				}
			}
		}

	private:
		SFrameProbe &m_Probe;
	};

	class CGraphicsFrontendTest : public ::testing::Test
	{
	protected:
		SFrameProbe m_Probe;
		CTestInfo m_TestInfo;
		std::unique_ptr<IStorage> m_pStorage;
		CCaptureQueueEngine m_Engine;
		std::unique_ptr<IKernel> m_pKernel;
		std::unique_ptr<CGraphics_Threaded> m_pGraphics;
		const std::unique_ptr<CConfig> m_pOriginalConfig = std::make_unique<CConfig>(g_Config);

		void SetUp() override
		{
			m_pStorage = m_TestInfo.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pKernel.reset(IKernel::Create());
			m_pGraphics = std::make_unique<CGraphics_Threaded>();
			m_pKernel->RegisterInterface(m_pStorage.get(), false);
			m_pKernel->RegisterInterface<IEngine>(&m_Engine, false);
			m_pKernel->RegisterInterface<IEngineGraphics>(m_pGraphics.get(), false);
			g_Config.m_QmGraphicsMode = graphics_backend::GRAPHICS_MODE_COMPATIBILITY;
			g_Config.m_QmGraphicsTrace = 0;
			g_Config.m_QmScreenshotDirectory[0] = '\0';
			gs_pFrameProbe = &m_Probe;
		}

		void TearDown() override
		{
			m_Probe.m_AllocationFailure.reset();
			m_Engine.ShutdownJobs();
			if(m_pGraphics)
				m_pGraphics->Shutdown();
			gs_pFrameProbe = nullptr;
			g_Config = *m_pOriginalConfig;
		}
	};
}

IGraphicsBackend *CreateGraphicsBackend(TTranslateFunc &&Translate)
{
	dbg_assert(gs_pFrameProbe != nullptr, "Graphics frontend test has no active backend probe");
	return new CFrameProbeBackend(*gs_pFrameProbe, std::move(Translate));
}

TEST_F(CGraphicsFrontendTest, ShutsDownAfterFrameBufferAllocationFailure)
{
	m_Probe.m_FailFrameAllocation = true;
	const int Result = m_pGraphics->Init();
	ASSERT_TRUE(m_Probe.m_AllocationFailure.has_value());
	const bool FaultTriggered = m_Probe.m_AllocationFailure->Triggered();
	m_Probe.m_AllocationFailure.reset();

	EXPECT_TRUE(FaultTriggered);
	EXPECT_NE(Result, 0);
	EXPECT_FALSE(m_pGraphics->IsBackendInitialized());
	EXPECT_EQ(m_Probe.m_Shutdowns, 1);

	m_Probe.m_FailFrameAllocation = false;
	EXPECT_EQ(m_pGraphics->Init(), 0);
}

TEST_F(CGraphicsFrontendTest, ReinitializationPreservesFirstSubmittedFrame)
{
	ASSERT_EQ(m_pGraphics->Init(), 0);
	m_pGraphics->Swap();
	ASSERT_NE(m_Probe.m_pSubmitted, nullptr);

	ASSERT_EQ(m_pGraphics->Init(), 0);
	m_pGraphics->Swap();
	ASSERT_NE(m_Probe.m_pSubmitted, nullptr);

	// 首帧交换命令在后端尚未结束借用时仍可执行。
	bool HasSwap = false;
	for(const auto *pCommand = m_Probe.m_pSubmitted->Head(); pCommand != nullptr; pCommand = pCommand->m_pNext)
		HasSwap |= pCommand->m_Cmd == CCommandBuffer::CMD_SWAP;
	EXPECT_TRUE(HasSwap);
}

TEST_F(CGraphicsFrontendTest, RenderTargetAllocationFailureLeavesNoCreateCommand)
{
	ASSERT_EQ(m_pGraphics->Init(), 0);
	IGraphics::CRenderTargetHandle Target;
	bool AllocationFailed;
	{
		CPredictionAllocationFailure Failure;
		Target = m_pGraphics->CreateRenderTarget(320, 240);
		AllocationFailed = Failure.Triggered();
	}
	EXPECT_TRUE(AllocationFailed);
	EXPECT_FALSE(Target.IsValid());
	m_pGraphics->Swap();
	m_pGraphics->WaitForIdle();
	EXPECT_EQ(m_Probe.m_RenderTargetCreates, 0);

	Target = m_pGraphics->CreateRenderTarget(320, 240);
	ASSERT_TRUE(Target.IsValid());
	m_pGraphics->Swap();
	m_pGraphics->WaitForIdle();
	EXPECT_EQ(m_Probe.m_RenderTargetCreates, 1);
}

TEST_F(CGraphicsFrontendTest, SwapClosesInterruptedVideoFrameBeforePresenting)
{
	ASSERT_EQ(m_pGraphics->Init(), 0);
	auto &Graphics = static_cast<IGraphics &>(*m_pGraphics);
	ASSERT_TRUE(Graphics.BeginVideoFrame(1600, 1200, 1));

	m_pGraphics->Swap();

	ASSERT_NE(m_Probe.m_pSubmitted, nullptr);
	std::vector<unsigned> vCommands;
	for(const auto *pCommand = m_Probe.m_pSubmitted->Head(); pCommand != nullptr; pCommand = pCommand->m_pNext)
	{
		if(pCommand->m_Cmd == CCommandBuffer::CMD_RENDER_TARGET_END ||
			pCommand->m_Cmd == CCommandBuffer::CMD_RENDER_TARGET_DESTROY ||
			pCommand->m_Cmd == CCommandBuffer::CMD_SWAP)
			vCommands.push_back(pCommand->m_Cmd);
	}
	const std::vector<unsigned> vExpectedCommands{
		CCommandBuffer::CMD_RENDER_TARGET_END,
		CCommandBuffer::CMD_RENDER_TARGET_DESTROY,
		CCommandBuffer::CMD_SWAP,
	};
	EXPECT_EQ(vCommands, vExpectedCommands);
	EXPECT_EQ(m_pGraphics->ScreenWidth(), 800);
	EXPECT_EQ(m_pGraphics->ScreenHeight(), 600);
	EXPECT_TRUE(Graphics.BeginVideoFrame(1600, 1200, 1));
	Graphics.StopVideoFrame();
}

TEST_F(CGraphicsFrontendTest, ShutdownClearsInterruptedOffscreenFrameBeforeReinitialization)
{
	ASSERT_EQ(m_pGraphics->Init(), 0);
	auto &Graphics = static_cast<IGraphics &>(*m_pGraphics);
	ASSERT_TRUE(Graphics.BeginOffscreenFrame(1600, 1200));

	m_pGraphics->Shutdown();

	EXPECT_FALSE(m_pGraphics->IsRenderTargetSupported());
	EXPECT_EQ(m_pGraphics->ScreenWidth(), 800);
	EXPECT_EQ(m_pGraphics->ScreenHeight(), 600);
	ASSERT_EQ(m_pGraphics->Init(), 0);
	EXPECT_TRUE(Graphics.BeginOffscreenFrame(1600, 1200));
}

TEST_F(CGraphicsFrontendTest, UnloadingOneMissingSpriteKeepsTheOtherSpriteOnTheBlankTexture)
{
	ASSERT_EQ(m_pGraphics->Init(), 0);
	CDataSpriteset Set{nullptr, 1, 1};
	const CDataSprite Sprite{"test", &Set, 1, 0, 1, 1};
	screenshot_save::COwnedImage Atlas(MakePixel());
	ASSERT_NE(Atlas.Image().m_pData, nullptr);
	auto First = m_pGraphics->LoadSpriteTexture(Atlas.Image(), std::nullopt, &Sprite);
	const auto Second = m_pGraphics->LoadSpriteTexture(Atlas.Image(), std::nullopt, &Sprite);
	ASSERT_TRUE(First.IsValid());
	ASSERT_FALSE(First.IsNullTexture());
	ASSERT_EQ(First.Id(), Second.Id());
	ASSERT_EQ(First.Generation(), Second.Generation());

	m_pGraphics->UnloadTexture(&First);
	EXPECT_FALSE(First.IsValid());
	m_pGraphics->TextureSet(Second);
	m_pGraphics->QuadsBegin();
	const IGraphics::CQuadItem Quad(0.0f, 0.0f, 1.0f, 1.0f);
	m_pGraphics->QuadsDrawTL(&Quad, 1);
	m_pGraphics->QuadsEnd();
	m_pGraphics->Swap();

	ASSERT_NE(m_Probe.m_pSubmitted, nullptr);
	const auto *pCommand = m_Probe.m_pSubmitted->Head();
	while(pCommand != nullptr && pCommand->m_Cmd != CCommandBuffer::CMD_RENDER)
		pCommand = pCommand->m_pNext;
	ASSERT_NE(pCommand, nullptr);
	const auto *pRender = static_cast<const CCommandBuffer::SCommand_Render *>(pCommand);
	EXPECT_EQ(pRender->m_State.m_Texture, Second.Id());
}

TEST_F(CGraphicsFrontendTest, MovedTextureWithUnrepresentableWidthIsRejected)
{
	ASSERT_EQ(m_pGraphics->Init(), 0);
	screenshot_save::COwnedImage Source(MakePixel());
	ASSERT_NE(Source.Image().m_pData, nullptr);
	Source.Image().m_Width = static_cast<size_t>(std::numeric_limits<int>::max()) + 1;

	const auto Texture = m_pGraphics->LoadTextureRawMove(Source.Image(), 0);

	EXPECT_FALSE(Texture.IsValid());
	EXPECT_EQ(Source.Image().m_pData, nullptr);
}

TEST_F(CGraphicsFrontendTest, OffscreenSaveKeepsPendingWindowScreenshotPath)
{
	ASSERT_EQ(m_pGraphics->Init(), 0);
	std::string WindowPath;
	auto &Graphics = static_cast<IGraphics &>(*m_pGraphics);
	Graphics.TakeScreenshot("window", [&](CImageInfo &&Image, const char *pPath) {
		WindowPath = pPath;
	});

	screenshot_save::COwnedImage Pose(MakePixel());
	ASSERT_NE(Pose.Image().m_pData, nullptr);
	ASSERT_TRUE(Graphics.TakeScreenshotFromImage(std::move(Pose.Image()), "pose", nullptr));

	// 离屏合影先提交，窗口截图在同帧 Swap 时读取挂起的路径。
	m_pGraphics->Swap();
	EXPECT_TRUE(WindowPath.starts_with("screenshots/window_")) << WindowPath;
	EXPECT_EQ(m_Engine.m_vJobs.size(), 2u);
}
