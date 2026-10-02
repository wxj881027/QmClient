#include <engine/client/backend/backend_base.h>
#include <engine/client/backend/metal/backend_metal.h>
#include <engine/client/backend_sdl.h>
#include <engine/graphics.h>
#include <engine/storage.h>

#include <SDL.h>
#include <SDL_metal.h>
#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <string>

#if defined(CONF_PLATFORM_MACOS) && defined(CONF_BACKEND_METAL) && defined(CONF_BACKEND_METAL_READY)
namespace
{
	class CMetalRuntimeBackend
	{
		SDL_Window *m_pWindow = nullptr;
		std::unique_ptr<CCommandProcessorFragment_GLBase> m_pBackend;
		std::unique_ptr<IStorage> m_pStorage;
		std::atomic<uint64_t> m_TextureMemoryUsage{0};
		std::atomic<uint64_t> m_BufferMemoryUsage{0};
		std::atomic<uint64_t> m_StreamMemoryUsage{0};
		std::atomic<uint64_t> m_StagingMemoryUsage{0};
		TTwGraphicsGpuList m_GpuList{};
		TGLBackendReadPresentedImageData m_ReadPresentedImageData;
		SBackendCapabilities m_Capabilities;
		std::string m_Error;

	public:
		~CMetalRuntimeBackend()
		{
			if(m_pBackend)
			{
				CCommandProcessorFragment_GLBase::SCommand_Shutdown Shutdown;
				m_pBackend->RunCommand(&Shutdown);
				CCommandProcessorFragment_GLBase::SCommand_PostShutdown PostShutdown;
				m_pBackend->RunCommand(&PostShutdown);
			}
			m_pBackend.reset();
			if(m_pWindow != nullptr)
				SDL_DestroyWindow(m_pWindow);
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
		}

		bool Init()
		{
			if(SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
			{
				m_Error = SDL_GetError();
				return false;
			}

			m_pWindow = SDL_CreateWindow("QmClient Metal runtime test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 32, 32, SDL_WINDOW_HIDDEN | SDL_WINDOW_METAL);
			if(m_pWindow == nullptr)
			{
				m_Error = SDL_GetError();
				return false;
			}

			CTestInfo TestInfo;
			m_pStorage = TestInfo.CreateTestStorage();
			if(!m_pStorage)
			{
				m_Error = "failed to create test storage";
				return false;
			}
			if(!m_pStorage->FileExists("shader/metal/qmclient.metallib", IStorage::TYPE_ALL))
			{
				m_Error = "Metal shader library is not visible from the data root";
				return false;
			}

			m_pBackend.reset(CreateMetalCommandProcessorFragment());
			if(!m_pBackend)
			{
				m_Error = "failed to create Metal command processor";
				return false;
			}

			std::array<char, 256> aVendor{};
			std::array<char, 256> aVersion{};
			std::array<char, 256> aRenderer{};
			int InitError = 0;
			const char *pErrorString = nullptr;
			CCommandProcessorFragment_GLBase::SCommand_PreInit PreInit;
			PreInit.m_pWindow = m_pWindow;
			PreInit.m_Width = 32;
			PreInit.m_Height = 32;
			PreInit.m_pVendorString = aVendor.data();
			PreInit.m_pVersionString = aVersion.data();
			PreInit.m_pRendererString = aRenderer.data();
			PreInit.m_pGpuList = &m_GpuList;
			PreInit.m_VSync = true;
			PreInit.m_pInitError = &InitError;
			PreInit.m_pErrStringPtr = &pErrorString;
			if(m_pBackend->RunCommand(&PreInit) != RUN_COMMAND_COMMAND_HANDLED || InitError != 0)
			{
				m_Error = pErrorString ? pErrorString : "Metal pre-init failed";
				return false;
			}

			CCommandProcessorFragment_GLBase::SCommand_Init Init;
			Init.m_pWindow = m_pWindow;
			Init.m_Width = 32;
			Init.m_Height = 32;
			Init.m_pStorage = m_pStorage.get();
			Init.m_pTextureMemoryUsage = &m_TextureMemoryUsage;
			Init.m_pBufferMemoryUsage = &m_BufferMemoryUsage;
			Init.m_pStreamMemoryUsage = &m_StreamMemoryUsage;
			Init.m_pStagingMemoryUsage = &m_StagingMemoryUsage;
			Init.m_pGpuList = &m_GpuList;
			Init.m_pReadPresentedImageDataFunc = &m_ReadPresentedImageData;
			Init.m_pCapabilities = &m_Capabilities;
			Init.m_pInitError = &InitError;
			Init.m_pErrStringPtr = &pErrorString;
			Init.m_pVendorString = aVendor.data();
			Init.m_pVersionString = aVersion.data();
			Init.m_pRendererString = aRenderer.data();
			Init.m_RequestedMajor = 0;
			Init.m_RequestedMinor = 0;
			Init.m_RequestedPatch = 0;
			Init.m_RequestedBackend = BACKEND_TYPE_METAL;
			Init.m_GlewMajor = 0;
			Init.m_GlewMinor = 0;
			Init.m_GlewPatch = 0;
			if(m_pBackend->RunCommand(&Init) != RUN_COMMAND_COMMAND_HANDLED || InitError != 0)
			{
				m_Error = pErrorString ? pErrorString : "Metal init failed";
				return false;
			}

			if(!m_Capabilities.m_RenderTargets || !m_Capabilities.m_RenderTargetGaussianBlur || !m_Capabilities.m_BackbufferCapture || !m_Capabilities.m_MediaIslandSdf || !m_Capabilities.m_RoundedRectSdf || !m_Capabilities.m_TexturedMsdf.load(std::memory_order_acquire))
			{
				m_Error = "Metal did not publish P3/P4 SDF and MSDF capabilities";
				return false;
			}
			return true;
		}

		CCommandProcessorFragment_GLBase *Backend() const { return m_pBackend.get(); }
		bool ResizeWindow(int Width, int Height)
		{
			if(m_pWindow == nullptr || m_pBackend == nullptr)
				return false;
			SDL_SetWindowSize(m_pWindow, Width, Height);
			SDL_PumpEvents();
			CCommandBuffer::SCommand_Update_Viewport UpdateViewport;
			UpdateViewport.m_X = 0;
			UpdateViewport.m_Y = 0;
			UpdateViewport.m_Width = Width;
			UpdateViewport.m_Height = Height;
			UpdateViewport.m_ByResize = true;
			return m_pBackend->RunCommand(&UpdateViewport) == RUN_COMMAND_COMMAND_HANDLED;
		}

		bool DrawableSize(int &Width, int &Height) const
		{
			if(m_pWindow == nullptr)
				return false;
			SDL_Metal_GetDrawableSize(m_pWindow, &Width, &Height);
			return Width > 0 && Height > 0;
		}

		bool ReadPresentedImageData(uint32_t &Width, uint32_t &Height, CImageInfo::EImageFormat &Format, std::vector<uint8_t> &vData) const
		{
			return m_ReadPresentedImageData && m_ReadPresentedImageData(Width, Height, Format, vData);
		}

		bool HasTexturedMsdf() const { return m_Capabilities.m_TexturedMsdf.load(std::memory_order_acquire); }
		const std::string &Error() const { return m_Error; }
	};

	bool RunCommand(CCommandProcessorFragment_GLBase *pBackend, const CCommandBuffer::SCommand *pCommand)
	{
		return pBackend->RunCommand(pCommand) == RUN_COMMAND_COMMAND_HANDLED;
	}

	bool RenderSolidQuad(CCommandProcessorFragment_GLBase *pBackend, float Width, float Height, CCommandBuffer::SColor Color)
	{
		std::array<CCommandBuffer::SVertex, 4> aVertices{};
		aVertices[0].m_Pos = {0.0f, 0.0f};
		aVertices[1].m_Pos = {Width, 0.0f};
		aVertices[2].m_Pos = {Width, Height};
		aVertices[3].m_Pos = {0.0f, Height};
		for(CCommandBuffer::SVertex &Vertex : aVertices)
			Vertex.m_Color = Color;

		CCommandBuffer::SCommand_Render Render;
		Render.m_State.m_BlendMode = EBlendMode::NONE;
		Render.m_State.m_Texture = -1;
		Render.m_State.m_ScreenTL = {0.0f, 0.0f};
		Render.m_State.m_ScreenBR = {Width, Height};
		Render.m_PrimType = EPrimitiveType::QUADS;
		Render.m_PrimCount = 1;
		Render.m_pVertices = aVertices.data();
		return RunCommand(pBackend, &Render);
	}

	std::array<CCommandBuffer::SVertex, 4> SdfQuadVertices(float Width, float Height)
	{
		std::array<CCommandBuffer::SVertex, 4> aVertices{};
		aVertices[0].m_Pos = {0.0f, 0.0f};
		aVertices[1].m_Pos = {Width, 0.0f};
		aVertices[2].m_Pos = {Width, Height};
		aVertices[3].m_Pos = {0.0f, Height};
		aVertices[0].m_Tex = {0.0f, 0.0f};
		aVertices[1].m_Tex = {1.0f, 0.0f};
		aVertices[2].m_Tex = {1.0f, 1.0f};
		aVertices[3].m_Tex = {0.0f, 1.0f};
		for(CCommandBuffer::SVertex &Vertex : aVertices)
			Vertex.m_Color = {255, 255, 255, 255};
		return aVertices;
	}

	bool RenderMediaIslandSdf(CCommandProcessorFragment_GLBase *pBackend, float Width, float Height, int BackdropTargetId)
	{
		auto aVertices = SdfQuadVertices(Width, Height);
		CCommandBuffer::SCommand_RenderMediaIslandSdf Render;
		Render.m_State.m_BlendMode = EBlendMode::ALPHA;
		Render.m_State.m_Texture = -1;
		Render.m_State.m_WrapMode = EWrapMode::CLAMP;
		Render.m_State.m_ScreenTL = {0.0f, 0.0f};
		Render.m_State.m_ScreenBR = {Width, Height};
		Render.m_Params.Clear();
		Render.m_Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_RECT] = {0.0f, 0.0f, Width, Height};
		Render.m_Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_MAIN_RECT] = {0.0f, 0.0f, Width, Height};
		Render.m_Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_BACKGROUND] = {1.0f, 0.0f, 0.0f, 0.5f};
		Render.m_Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_MAIN_PARAMS] = {4.0f, 0.0f, 0.0f, 0.0f};
		Render.m_Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_METADATA] = {0.0f, static_cast<float>(IGraphics::CORNER_ALL), 0.0f, 1.0f};
		Render.m_Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_BACKDROP_UV] = {0.0f, 0.0f, 1.0f, 1.0f};
		Render.m_BackdropTargetId = BackdropTargetId;
		Render.m_PrimType = EPrimitiveType::QUADS;
		Render.m_PrimCount = 1;
		Render.m_pVertices = aVertices.data();
		return RunCommand(pBackend, &Render);
	}

	bool RenderRoundedRectSdf(CCommandProcessorFragment_GLBase *pBackend, float Width, float Height)
	{
		auto aVertices = SdfQuadVertices(Width, Height);
		CCommandBuffer::SCommand_RenderRoundedRectSdf Render;
		Render.m_State.m_BlendMode = EBlendMode::ALPHA;
		Render.m_State.m_Texture = -1;
		Render.m_State.m_ScreenTL = {0.0f, 0.0f};
		Render.m_State.m_ScreenBR = {Width, Height};
		Render.m_State.m_ClipEnable = true;
		Render.m_State.m_ClipX = 0;
		Render.m_State.m_ClipY = 0;
		Render.m_State.m_ClipW = static_cast<int>(Width / 2);
		Render.m_State.m_ClipH = static_cast<int>(Height);
		Render.m_Params.m_Rect = {0.0f, 0.0f, Width, Height};
		Render.m_Params.m_FillColor = {0.0f, 1.0f, 0.0f, 1.0f};
		Render.m_Params.m_BorderColor = {1.0f, 0.0f, 0.0f, 1.0f};
		Render.m_Params.m_CornerRadii = {4.0f, 4.0f, 4.0f, 4.0f};
		Render.m_Params.m_Params = {2.0f, 1.0f, 0.0f, 0.0f};
		Render.m_PrimType = EPrimitiveType::QUADS;
		Render.m_PrimCount = 1;
		Render.m_pVertices = aVertices.data();
		return RunCommand(pBackend, &Render);
	}

	bool CreateWhiteMsdfTexture(CCommandProcessorFragment_GLBase *pBackend, int Slot)
	{
		uint8_t *pData = static_cast<uint8_t *>(malloc(4));
		if(pData == nullptr)
			return false;
		pData[0] = 255;
		pData[1] = 255;
		pData[2] = 255;
		pData[3] = 255;
		CCommandBuffer::SCommand_Texture_Create Create;
		Create.m_Slot = Slot;
		Create.m_Width = 1;
		Create.m_Height = 1;
		Create.m_Flags = TextureFlag::NO_MIPMAPS;
		Create.m_pData = pData;
		return RunCommand(pBackend, &Create);
	}

	bool RenderTexturedMsdf(CCommandProcessorFragment_GLBase *pBackend, float Width, float Height, int TextureSlot, CCommandBuffer::SColor Color)
	{
		auto aVertices = SdfQuadVertices(Width, Height);
		for(CCommandBuffer::SVertex &Vertex : aVertices)
			Vertex.m_Color = Color;
		CCommandBuffer::SCommand_RenderTexturedMsdf Render;
		Render.m_State.m_BlendMode = EBlendMode::ALPHA;
		Render.m_State.m_WrapMode = EWrapMode::CLAMP;
		Render.m_State.m_Texture = TextureSlot;
		Render.m_State.m_ScreenTL = {0.0f, 0.0f};
		Render.m_State.m_ScreenBR = {Width, Height};
		Render.m_State.m_ClipEnable = false;
		Render.m_MsdfParams = {4.0f, 1.0f, 1.0f, 0.0f};
		Render.m_PrimType = EPrimitiveType::QUADS;
		Render.m_PrimCount = 1;
		Render.m_pVertices = aVertices.data();
		return RunCommand(pBackend, &Render);
	}

	bool RenderQuadContainerEx(CCommandProcessorFragment_GLBase *pBackend, float Width, float Height, CCommandBuffer::SColorf Color)
	{
		std::array<CCommandBuffer::SVertex, 4> aVertices{};
		aVertices[0].m_Pos = {0.0f, 0.0f};
		aVertices[1].m_Pos = {Width, 0.0f};
		aVertices[2].m_Pos = {Width, Height};
		aVertices[3].m_Pos = {0.0f, Height};
		for(CCommandBuffer::SVertex &Vertex : aVertices)
		{
			Vertex.m_Tex = {0.0f, 0.0f};
			Vertex.m_Color = {255, 255, 255, 255};
		}

		CCommandBuffer::SCommand_CreateBufferObject CreateBuffer;
		CreateBuffer.m_BufferIndex = 0;
		CreateBuffer.m_DeletePointer = false;
		CreateBuffer.m_pUploadData = aVertices.data();
		CreateBuffer.m_DataSize = sizeof(aVertices);
		CreateBuffer.m_Flags = IGraphics::EBufferObjectCreateFlags::BUFFER_OBJECT_CREATE_FLAGS_ONE_TIME_USE_BIT;
		if(!RunCommand(pBackend, &CreateBuffer))
			return false;

		std::array<SBufferContainerInfo::SAttribute, 3> aAttributes{};
		aAttributes[0] = {2, GRAPHICS_TYPE_FLOAT, false, reinterpret_cast<void *>(offsetof(CCommandBuffer::SVertex, m_Pos)), 0};
		aAttributes[1] = {2, GRAPHICS_TYPE_FLOAT, false, reinterpret_cast<void *>(offsetof(CCommandBuffer::SVertex, m_Tex)), 0};
		aAttributes[2] = {4, GRAPHICS_TYPE_UNSIGNED_BYTE, true, reinterpret_cast<void *>(offsetof(CCommandBuffer::SVertex, m_Color)), 0};
		CCommandBuffer::SCommand_CreateBufferContainer CreateContainer;
		CreateContainer.m_BufferContainerIndex = 0;
		CreateContainer.m_Stride = sizeof(CCommandBuffer::SVertex);
		CreateContainer.m_VertBufferBindingIndex = 0;
		CreateContainer.m_AttrCount = aAttributes.size();
		CreateContainer.m_pAttributes = aAttributes.data();
		if(!RunCommand(pBackend, &CreateContainer))
			return false;

		CCommandBuffer::SCommand_RenderQuadContainerEx Render;
		Render.m_State.m_BlendMode = EBlendMode::NONE;
		Render.m_State.m_Texture = -1;
		Render.m_State.m_ScreenTL = {0.0f, 0.0f};
		Render.m_State.m_ScreenBR = {Width, Height};
		Render.m_BufferContainerIndex = 0;
		Render.m_Rotation = 0.0f;
		Render.m_Center = {Width * 0.5f, Height * 0.5f};
		Render.m_VertexColor = Color;
		Render.m_DrawNum = 6;
		Render.m_pOffset = nullptr;
		return RunCommand(pBackend, &Render);
	}
} // namespace

TEST(MetalBackendRuntime, RenderTargetGaussianBlurAndReadbackUseNativeGpu)
{
	CMetalRuntimeBackend Runtime;
	ASSERT_TRUE(Runtime.Init()) << Runtime.Error();
	CCommandProcessorFragment_GLBase *pBackend = Runtime.Backend();
	ASSERT_NE(pBackend, nullptr);

	pBackend->StartCommands(11, 2);
	for(int TargetId = 0; TargetId < 3; ++TargetId)
	{
		CCommandBuffer::SCommand_RenderTarget_Create Create;
		Create.m_TargetId = TargetId;
		Create.m_Width = 16;
		Create.m_Height = 16;
		ASSERT_TRUE(RunCommand(pBackend, &Create));
	}

	CCommandBuffer::SCommand_RenderTarget_Begin BeginSource;
	BeginSource.m_TargetId = 0;
	BeginSource.m_ClearColor = {1.0f, 0.0f, 0.0f, 1.0f};
	ASSERT_TRUE(RunCommand(pBackend, &BeginSource));
	CCommandBuffer::SCommand_RenderTarget_End EndSource;
	ASSERT_TRUE(RunCommand(pBackend, &EndSource));

	IGraphics::SGaussianBlurParams Params;
	Params.m_Radius = 2;
	Params.m_Sigma = 1.0f;
	std::array<float, IGraphics::GAUSSIAN_BLUR_MAX_RADIUS + 1> aWeights{};
	ASSERT_TRUE(IGraphics::CalculateGaussianBlurKernel(Params, aWeights));

	CCommandBuffer::SCommand_RenderTarget_Begin BeginTemporary;
	BeginTemporary.m_TargetId = 1;
	BeginTemporary.m_ClearColor = {0.0f, 0.0f, 0.0f, 0.0f};
	ASSERT_TRUE(RunCommand(pBackend, &BeginTemporary));
	CCommandBuffer::SCommand_RenderTarget_GaussianBlurPass Horizontal;
	Horizontal.m_SourceTargetId = 0;
	Horizontal.m_Radius = Params.m_Radius;
	Horizontal.m_Horizontal = true;
	Horizontal.m_aWeights = aWeights;
	ASSERT_TRUE(RunCommand(pBackend, &Horizontal));
	CCommandBuffer::SCommand_RenderTarget_End EndTemporary;
	ASSERT_TRUE(RunCommand(pBackend, &EndTemporary));

	CCommandBuffer::SCommand_RenderTarget_Begin BeginDestination;
	BeginDestination.m_TargetId = 2;
	BeginDestination.m_ClearColor = {0.0f, 0.0f, 0.0f, 0.0f};
	ASSERT_TRUE(RunCommand(pBackend, &BeginDestination));
	CCommandBuffer::SCommand_RenderTarget_GaussianBlurPass Vertical;
	Vertical.m_SourceTargetId = 1;
	Vertical.m_Radius = Params.m_Radius;
	Vertical.m_Horizontal = false;
	Vertical.m_aWeights = aWeights;
	ASSERT_TRUE(RunCommand(pBackend, &Vertical));
	CCommandBuffer::SCommand_RenderTarget_End EndDestination;
	ASSERT_TRUE(RunCommand(pBackend, &EndDestination));

	CImageInfo Image;
	CCommandBuffer::SCommand_RenderTarget_Readback Readback;
	Readback.m_TargetId = 2;
	Readback.m_pImage = &Image;
	ASSERT_TRUE(RunCommand(pBackend, &Readback));
	pBackend->EndCommands();

	ASSERT_NE(Image.m_pData, nullptr);
	ASSERT_EQ(Image.m_Width, 16);
	ASSERT_EQ(Image.m_Height, 16);
	ASSERT_EQ(Image.m_Format, CImageInfo::FORMAT_RGBA);
	const auto *pPixels = static_cast<const uint8_t *>(Image.m_pData);
	EXPECT_GE(pPixels[0], 250);
	EXPECT_LE(pPixels[1], 2);
	EXPECT_LE(pPixels[2], 2);
	EXPECT_GE(pPixels[3], 250);
	Image.Free();
}

TEST(MetalBackendRuntime, BackbufferCaptureAndTrySwapReadbacksShareNativePresentedFrame)
{
	CMetalRuntimeBackend Runtime;
	ASSERT_TRUE(Runtime.Init()) << Runtime.Error();
	CCommandProcessorFragment_GLBase *pBackend = Runtime.Backend();
	ASSERT_NE(pBackend, nullptr);

	CMetalRuntimeBackend RoundedRuntime;
	ASSERT_TRUE(RoundedRuntime.Init()) << RoundedRuntime.Error();
	pBackend = RoundedRuntime.Backend();
	pBackend->StartCommands(4, 1);
	CCommandBuffer::SCommand_RenderTarget_Create Create;
	Create.m_TargetId = 0;
	Create.m_Width = 16;
	Create.m_Height = 16;
	ASSERT_TRUE(RunCommand(pBackend, &Create));
	CCommandBuffer::SCommand_Clear ClearGreen;
	ClearGreen.m_Color = {0.0f, 1.0f, 0.0f, 1.0f};
	ClearGreen.m_ForceClear = true;
	ASSERT_TRUE(RunCommand(pBackend, &ClearGreen));
	CCommandBuffer::SCommand_RenderTarget_CaptureBackbuffer Capture;
	Capture.m_TargetId = 0;
	ASSERT_TRUE(RunCommand(pBackend, &Capture));
	CImageInfo CapturedImage;
	CCommandBuffer::SCommand_RenderTarget_Readback Readback;
	Readback.m_TargetId = 0;
	Readback.m_pImage = &CapturedImage;
	ASSERT_TRUE(RunCommand(pBackend, &Readback));
	pBackend->EndCommands();

	ASSERT_NE(CapturedImage.m_pData, nullptr);
	const auto *pCapturedPixels = static_cast<const uint8_t *>(CapturedImage.m_pData);
	EXPECT_LE(pCapturedPixels[0], 2);
	EXPECT_GE(pCapturedPixels[1], 250);
	EXPECT_LE(pCapturedPixels[2], 2);
	CapturedImage.Free();

	pBackend->StartCommands(2, 0);
	CImageInfo ScreenshotImage;
	bool Swapped = false;
	CCommandBuffer::SCommand_TrySwapAndScreenshot Screenshot;
	Screenshot.m_pImage = &ScreenshotImage;
	Screenshot.m_pSwapped = &Swapped;
	ASSERT_TRUE(RunCommand(pBackend, &Screenshot));
	ASSERT_TRUE(Swapped);
	CCommandBuffer::SColorf PixelColor;
	CCommandBuffer::SCommand_TrySwapAndReadPixel ReadPixel;
	ReadPixel.m_Position = {0, 0};
	ReadPixel.m_pColor = &PixelColor;
	ReadPixel.m_pSwapped = &Swapped;
	ASSERT_TRUE(RunCommand(pBackend, &ReadPixel));
	pBackend->EndCommands();

	ASSERT_NE(ScreenshotImage.m_pData, nullptr);
	const auto *pScreenshotPixels = static_cast<const uint8_t *>(ScreenshotImage.m_pData);
	EXPECT_LE(pScreenshotPixels[0], 2);
	EXPECT_GE(pScreenshotPixels[1], 250);
	EXPECT_LE(pScreenshotPixels[2], 2);
	EXPECT_NEAR(PixelColor.r, 0.0f, 0.01f);
	EXPECT_NEAR(PixelColor.g, 1.0f, 0.01f);
	EXPECT_NEAR(PixelColor.b, 0.0f, 0.01f);
	EXPECT_NEAR(PixelColor.a, 1.0f, 0.01f);
	uint32_t VideoWidth = 0;
	uint32_t VideoHeight = 0;
	CImageInfo::EImageFormat VideoFormat = static_cast<CImageInfo::EImageFormat>(-1);
	std::vector<uint8_t> vVideoData;
	ASSERT_TRUE(RoundedRuntime.ReadPresentedImageData(VideoWidth, VideoHeight, VideoFormat, vVideoData));
	EXPECT_EQ(VideoWidth, ScreenshotImage.m_Width);
	EXPECT_EQ(VideoHeight, ScreenshotImage.m_Height);
	EXPECT_EQ(VideoFormat, CImageInfo::FORMAT_RGBA);
	ASSERT_EQ(vVideoData.size(), static_cast<size_t>(VideoWidth) * VideoHeight * 4);
	EXPECT_LE(vVideoData[0], 2);
	EXPECT_GE(vVideoData[1], 250);
	EXPECT_LE(vVideoData[2], 2);
	ScreenshotImage.Free();
}

TEST(MetalBackendRuntime, NormalSwapDoesNotRetainDrawableForDeferredReadback)
{
	CMetalRuntimeBackend Runtime;
	ASSERT_TRUE(Runtime.Init()) << Runtime.Error();
	CCommandProcessorFragment_GLBase *pBackend = Runtime.Backend();
	ASSERT_NE(pBackend, nullptr);

	pBackend->StartCommands(4, 1);
	CCommandBuffer::SCommand_Clear ClearGreen;
	ClearGreen.m_Color = {0.0f, 1.0f, 0.0f, 1.0f};
	ClearGreen.m_ForceClear = true;
	ASSERT_TRUE(RunCommand(pBackend, &ClearGreen));
	CCommandBuffer::SCommand_Swap Swap;
	ASSERT_TRUE(RunCommand(pBackend, &Swap));
	pBackend->EndCommands();
	pBackend->StartCommands(1, 0);
	pBackend->EndCommands();

	uint32_t Width = 0;
	uint32_t Height = 0;
	CImageInfo::EImageFormat Format = static_cast<CImageInfo::EImageFormat>(-1);
	std::vector<uint8_t> vData;
	EXPECT_FALSE(Runtime.ReadPresentedImageData(Width, Height, Format, vData));
}

TEST(MetalBackendRuntime, NormalSwapInvalidatesEarlierPresentedReadback)
{
	CMetalRuntimeBackend Runtime;
	ASSERT_TRUE(Runtime.Init()) << Runtime.Error();
	CCommandProcessorFragment_GLBase *pBackend = Runtime.Backend();
	ASSERT_NE(pBackend, nullptr);

	pBackend->StartCommands(4, 1);
	CCommandBuffer::SCommand_Clear ClearRed;
	ClearRed.m_Color = {1.0f, 0.0f, 0.0f, 1.0f};
	ClearRed.m_ForceClear = true;
	ASSERT_TRUE(RunCommand(pBackend, &ClearRed));
	CImageInfo FirstImage;
	bool FirstSwapped = false;
	CCommandBuffer::SCommand_TrySwapAndScreenshot FirstScreenshot;
	FirstScreenshot.m_pImage = &FirstImage;
	FirstScreenshot.m_pSwapped = &FirstSwapped;
	ASSERT_TRUE(RunCommand(pBackend, &FirstScreenshot));
	pBackend->EndCommands();
	ASSERT_TRUE(FirstSwapped);
	ASSERT_NE(FirstImage.m_pData, nullptr);
	EXPECT_GE(static_cast<const uint8_t *>(FirstImage.m_pData)[0], 250);
	FirstImage.Free();

	pBackend->StartCommands(4, 1);
	CCommandBuffer::SCommand_Clear ClearGreen;
	ClearGreen.m_Color = {0.0f, 1.0f, 0.0f, 1.0f};
	ClearGreen.m_ForceClear = true;
	ASSERT_TRUE(RunCommand(pBackend, &ClearGreen));
	CCommandBuffer::SCommand_Swap Swap;
	ASSERT_TRUE(RunCommand(pBackend, &Swap));
	pBackend->EndCommands();
	pBackend->StartCommands(1, 0);
	pBackend->EndCommands();

	uint32_t Width = 0;
	uint32_t Height = 0;
	CImageInfo::EImageFormat Format = static_cast<CImageInfo::EImageFormat>(-1);
	std::vector<uint8_t> vData;
	EXPECT_FALSE(Runtime.ReadPresentedImageData(Width, Height, Format, vData));
}

TEST(MetalBackendRuntime, ReadPixelSwapInvalidatesEarlierPresentedReadbackWithoutVideoCapture)
{
	CMetalRuntimeBackend Runtime;
	ASSERT_TRUE(Runtime.Init()) << Runtime.Error();
	CCommandProcessorFragment_GLBase *pBackend = Runtime.Backend();
	ASSERT_NE(pBackend, nullptr);

	pBackend->StartCommands(4, 1);
	CCommandBuffer::SCommand_Clear ClearRed;
	ClearRed.m_Color = {1.0f, 0.0f, 0.0f, 1.0f};
	ClearRed.m_ForceClear = true;
	ASSERT_TRUE(RunCommand(pBackend, &ClearRed));
	CImageInfo FirstImage;
	bool FirstSwapped = false;
	CCommandBuffer::SCommand_TrySwapAndScreenshot FirstScreenshot;
	FirstScreenshot.m_pImage = &FirstImage;
	FirstScreenshot.m_pSwapped = &FirstSwapped;
	ASSERT_TRUE(RunCommand(pBackend, &FirstScreenshot));
	pBackend->EndCommands();
	ASSERT_TRUE(FirstSwapped);
	FirstImage.Free();

	pBackend->StartCommands(4, 1);
	CCommandBuffer::SCommand_Clear ClearGreen;
	ClearGreen.m_Color = {0.0f, 1.0f, 0.0f, 1.0f};
	ClearGreen.m_ForceClear = true;
	ASSERT_TRUE(RunCommand(pBackend, &ClearGreen));
	CCommandBuffer::SColorf PixelColor;
	bool Swapped = false;
	CCommandBuffer::SCommand_TrySwapAndReadPixel ReadPixel;
	ReadPixel.m_Position = {0, 0};
	ReadPixel.m_pColor = &PixelColor;
	ReadPixel.m_pSwapped = &Swapped;
	ASSERT_TRUE(RunCommand(pBackend, &ReadPixel));
	pBackend->EndCommands();
	pBackend->StartCommands(1, 0);
	pBackend->EndCommands();
	ASSERT_TRUE(Swapped);
	EXPECT_NEAR(PixelColor.r, 0.0f, 0.01f);
	EXPECT_NEAR(PixelColor.g, 1.0f, 0.01f);

	uint32_t Width = 0;
	uint32_t Height = 0;
	CImageInfo::EImageFormat Format = static_cast<CImageInfo::EImageFormat>(-1);
	std::vector<uint8_t> vData;
	EXPECT_FALSE(Runtime.ReadPresentedImageData(Width, Height, Format, vData));
}

TEST(MetalBackendRuntime, QmSdfPipelinesRenderBackdropAlphaAndClip)
{
	CMetalRuntimeBackend Runtime;
	ASSERT_TRUE(Runtime.Init()) << Runtime.Error();
	CCommandProcessorFragment_GLBase *pBackend = Runtime.Backend();
	ASSERT_NE(pBackend, nullptr);

	pBackend->StartCommands(6, 1);
	CCommandBuffer::SCommand_RenderTarget_Create CreateBackdrop;
	CreateBackdrop.m_TargetId = 0;
	CreateBackdrop.m_Width = 32;
	CreateBackdrop.m_Height = 32;
	ASSERT_TRUE(RunCommand(pBackend, &CreateBackdrop));
	CCommandBuffer::SCommand_RenderTarget_Begin BeginBackdrop;
	BeginBackdrop.m_TargetId = 0;
	BeginBackdrop.m_ClearColor = {0.0f, 0.0f, 1.0f, 1.0f};
	ASSERT_TRUE(RunCommand(pBackend, &BeginBackdrop));
	CCommandBuffer::SCommand_RenderTarget_End EndBackdrop;
	ASSERT_TRUE(RunCommand(pBackend, &EndBackdrop));
	CCommandBuffer::SCommand_Clear Clear;
	Clear.m_Color = {0.0f, 0.0f, 0.0f, 1.0f};
	Clear.m_ForceClear = true;
	ASSERT_TRUE(RunCommand(pBackend, &Clear));
	ASSERT_TRUE(RenderMediaIslandSdf(pBackend, 32.0f, 32.0f, 0));
	CImageInfo MediaImage;
	bool Swapped = false;
	CCommandBuffer::SCommand_TrySwapAndScreenshot MediaScreenshot;
	MediaScreenshot.m_pImage = &MediaImage;
	MediaScreenshot.m_pSwapped = &Swapped;
	ASSERT_TRUE(RunCommand(pBackend, &MediaScreenshot));
	ASSERT_TRUE(Swapped);
	pBackend->EndCommands();

	ASSERT_NE(MediaImage.m_pData, nullptr);
	const auto *pMediaPixels = static_cast<const uint8_t *>(MediaImage.m_pData);
	const size_t MediaCenter = (16U * MediaImage.m_Width + 16U) * 4U;
	EXPECT_NEAR(pMediaPixels[MediaCenter + 0], 128, 3);
	EXPECT_LE(pMediaPixels[MediaCenter + 1], 2);
	EXPECT_NEAR(pMediaPixels[MediaCenter + 2], 128, 3);
	EXPECT_GE(pMediaPixels[MediaCenter + 3], 250);
	MediaImage.Free();

	Swapped = false;
	pBackend->StartCommands(4, 1);
	ASSERT_TRUE(RunCommand(pBackend, &Clear));
	ASSERT_TRUE(RenderRoundedRectSdf(pBackend, 32.0f, 32.0f));
	CImageInfo RoundedRectImage;
	CCommandBuffer::SCommand_TrySwapAndScreenshot RoundedRectScreenshot;
	RoundedRectScreenshot.m_pImage = &RoundedRectImage;
	RoundedRectScreenshot.m_pSwapped = &Swapped;
	ASSERT_TRUE(RunCommand(pBackend, &RoundedRectScreenshot));
	ASSERT_TRUE(Swapped);
	pBackend->EndCommands();

	ASSERT_NE(RoundedRectImage.m_pData, nullptr);
	const auto *pRoundedRectPixels = static_cast<const uint8_t *>(RoundedRectImage.m_pData);
	const size_t ClippedCenter = (16U * RoundedRectImage.m_Width + 8U) * 4U;
	const size_t ClearCenter = (16U * RoundedRectImage.m_Width + 24U) * 4U;
	EXPECT_LE(pRoundedRectPixels[ClippedCenter + 0], 2);
	EXPECT_GE(pRoundedRectPixels[ClippedCenter + 1], 250);
	EXPECT_LE(pRoundedRectPixels[ClippedCenter + 2], 2);
	EXPECT_LE(pRoundedRectPixels[ClearCenter + 0], 2);
	EXPECT_LE(pRoundedRectPixels[ClearCenter + 1], 2);
	EXPECT_LE(pRoundedRectPixels[ClearCenter + 2], 2);
	RoundedRectImage.Free();
}

TEST(MetalBackendRuntime, TexturedMsdfPipelineRendersTintAndOpacity)
{
	CMetalRuntimeBackend Runtime;
	ASSERT_TRUE(Runtime.Init()) << Runtime.Error();
	CCommandProcessorFragment_GLBase *pBackend = Runtime.Backend();
	ASSERT_NE(pBackend, nullptr);

	pBackend->StartCommands(4, 1);
	ASSERT_TRUE(CreateWhiteMsdfTexture(pBackend, 0));
	CCommandBuffer::SCommand_Clear Clear;
	Clear.m_Color = {0.0f, 0.0f, 0.0f, 1.0f};
	Clear.m_ForceClear = true;
	ASSERT_TRUE(RunCommand(pBackend, &Clear));
	ASSERT_TRUE(RenderTexturedMsdf(pBackend, 32.0f, 32.0f, 0, {0, 128, 255, 128}));
	CImageInfo Image;
	bool Swapped = false;
	CCommandBuffer::SCommand_TrySwapAndScreenshot Screenshot;
	Screenshot.m_pImage = &Image;
	Screenshot.m_pSwapped = &Swapped;
	ASSERT_TRUE(RunCommand(pBackend, &Screenshot));
	ASSERT_TRUE(Swapped);
	pBackend->EndCommands();

	ASSERT_NE(Image.m_pData, nullptr);
	const auto *pPixels = static_cast<const uint8_t *>(Image.m_pData);
	const size_t Center = (16U * Image.m_Width + 16U) * 4U;
	EXPECT_LE(pPixels[Center + 0], 2);
	EXPECT_NEAR(pPixels[Center + 1], 64, 2);
	EXPECT_NEAR(pPixels[Center + 2], 128, 2);
	EXPECT_GT(pPixels[Center + 3], 0);
	Image.Free();
}

TEST(MetalBackendRuntime, MultiSamplingAndResizeRecreateNativePresentedResources)
{
	CMetalRuntimeBackend Runtime;
	ASSERT_TRUE(Runtime.Init()) << Runtime.Error();
	CCommandProcessorFragment_GLBase *pBackend = Runtime.Backend();
	ASSERT_NE(pBackend, nullptr);

	for(uint32_t RequestedCount : {4U, 0U, 4U, 0U})
	{
		pBackend->StartCommands(1, 0);
		uint32_t ActualCount = 0;
		bool Changed = false;
		CCommandBuffer::SCommand_MultiSampling MultiSampling;
		MultiSampling.m_RequestedMultiSamplingCount = RequestedCount;
		MultiSampling.m_pRetMultiSamplingCount = &ActualCount;
		MultiSampling.m_pRetOk = &Changed;
		ASSERT_TRUE(RunCommand(pBackend, &MultiSampling));
		ASSERT_TRUE(Changed);
		if(RequestedCount == 4 && ActualCount != 4)
			GTEST_SKIP() << "Metal device does not support 4x MSAA";
		ASSERT_EQ(ActualCount, RequestedCount);
		pBackend->EndCommands();
		ASSERT_TRUE(Runtime.HasTexturedMsdf());

		pBackend->StartCommands(3, 2);
		CCommandBuffer::SCommand_Clear Clear;
		Clear.m_Color = RequestedCount == 0 ? CCommandBuffer::SColorf{0.0f, 0.0f, 1.0f, 1.0f} : CCommandBuffer::SColorf{1.0f, 0.0f, 0.0f, 1.0f};
		Clear.m_ForceClear = true;
		ASSERT_TRUE(RunCommand(pBackend, &Clear));
		ASSERT_TRUE(RenderSolidQuad(pBackend, 32.0f, 32.0f, RequestedCount == 0 ? CCommandBuffer::SColor{0, 0, 255, 255} : CCommandBuffer::SColor{255, 0, 0, 255}));
		CImageInfo Image;
		bool Swapped = false;
		CCommandBuffer::SCommand_TrySwapAndScreenshot Screenshot;
		Screenshot.m_pImage = &Image;
		Screenshot.m_pSwapped = &Swapped;
		ASSERT_TRUE(RunCommand(pBackend, &Screenshot));
		ASSERT_TRUE(Swapped);
		pBackend->EndCommands();

		ASSERT_NE(Image.m_pData, nullptr);
		const auto *pPixels = static_cast<const uint8_t *>(Image.m_pData);
		if(RequestedCount == 0)
		{
			EXPECT_LE(pPixels[0], 2);
			EXPECT_LE(pPixels[1], 2);
			EXPECT_GE(pPixels[2], 250);
		}
		else
		{
			EXPECT_GE(pPixels[0], 250);
			EXPECT_LE(pPixels[1], 2);
			EXPECT_LE(pPixels[2], 2);
		}
		Image.Free();
	}

	ASSERT_TRUE(Runtime.ResizeWindow(48, 24));
	int DrawableWidth = 0;
	int DrawableHeight = 0;
	ASSERT_TRUE(Runtime.DrawableSize(DrawableWidth, DrawableHeight));
	EXPECT_NE(DrawableWidth, 32);
	EXPECT_NE(DrawableHeight, 32);

	pBackend->StartCommands(2, 1);
	CCommandBuffer::SCommand_Clear Clear;
	Clear.m_Color = {0.0f, 1.0f, 0.0f, 1.0f};
	Clear.m_ForceClear = true;
	ASSERT_TRUE(RunCommand(pBackend, &Clear));
	CImageInfo Image;
	bool Swapped = false;
	CCommandBuffer::SCommand_TrySwapAndScreenshot Screenshot;
	Screenshot.m_pImage = &Image;
	Screenshot.m_pSwapped = &Swapped;
	ASSERT_TRUE(RunCommand(pBackend, &Screenshot));
	ASSERT_TRUE(Swapped);
	pBackend->EndCommands();

	ASSERT_NE(Image.m_pData, nullptr);
	EXPECT_EQ(Image.m_Width, DrawableWidth);
	EXPECT_EQ(Image.m_Height, DrawableHeight);
	const auto *pPixels = static_cast<const uint8_t *>(Image.m_pData);
	EXPECT_LE(pPixels[0], 2);
	EXPECT_GE(pPixels[1], 250);
	EXPECT_LE(pPixels[2], 2);
	Image.Free();
}

TEST(MetalBackendRuntime, QuadContainerExBindsFragmentUniforms)
{
	CMetalRuntimeBackend Runtime;
	ASSERT_TRUE(Runtime.Init()) << Runtime.Error();
	CCommandProcessorFragment_GLBase *pBackend = Runtime.Backend();
	ASSERT_NE(pBackend, nullptr);

	pBackend->StartCommands(5, 2);
	ASSERT_TRUE(RenderQuadContainerEx(pBackend, 32.0f, 32.0f, {0.0f, 0.5f, 1.0f, 1.0f}));
	CImageInfo Image;
	bool Swapped = false;
	CCommandBuffer::SCommand_TrySwapAndScreenshot Screenshot;
	Screenshot.m_pImage = &Image;
	Screenshot.m_pSwapped = &Swapped;
	ASSERT_TRUE(RunCommand(pBackend, &Screenshot));
	ASSERT_TRUE(Swapped);
	pBackend->EndCommands();

	ASSERT_NE(Image.m_pData, nullptr);
	const auto *pPixels = static_cast<const uint8_t *>(Image.m_pData);
	EXPECT_LE(pPixels[0], 2);
	EXPECT_NEAR(pPixels[1], 128, 2);
	EXPECT_GE(pPixels[2], 250);
	EXPECT_GE(pPixels[3], 250);
	Image.Free();
}
#endif
