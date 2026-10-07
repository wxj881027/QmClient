#include <engine/client/backend/graphics_backend_contract.h>
#include <engine/client/backend/vulkan/backend_vulkan.h>
#include <engine/client/backend_sdl.h>
#include <engine/graphics.h>

#if (defined(CONF_PLATFORM_MACOS) || defined(CONF_PLATFORM_IOS)) && defined(CONF_BACKEND_METAL) && defined(CONF_BACKEND_METAL_READY)
#include <engine/client/backend/metal/backend_metal.h>
#endif

#include <gtest/gtest.h>

TEST(GraphicsBackendContract, NamesAreStable)
{
	EXPECT_STREQ(graphics_backend::BackendName(BACKEND_TYPE_OPENGL), "OpenGL");
	EXPECT_STREQ(graphics_backend::BackendName(BACKEND_TYPE_OPENGL_ES), "GLES");
	EXPECT_STREQ(graphics_backend::BackendName(BACKEND_TYPE_VULKAN), "Vulkan");
	EXPECT_STREQ(graphics_backend::BackendName(BACKEND_TYPE_METAL), "Metal");
}

TEST(GraphicsBackendContract, VulkanVersionSelectionFallsBackThrough13To11)
{
	EXPECT_EQ(ResolveConfiguredVulkanApiVersion(14).m_Minor, 4);
	EXPECT_EQ(ResolveConfiguredVulkanApiVersion(13).m_Minor, 3);
	EXPECT_EQ(ResolveConfiguredVulkanApiVersion(11).m_Minor, 1);
	EXPECT_EQ(ResolveConfiguredVulkanApiVersion(12).m_Minor, 1);

	EXPECT_EQ(ResolveVulkanVersionForLoader({1, 4, 0}, {1, 4, 0}).m_Minor, 4);
	EXPECT_EQ(ResolveVulkanVersionForLoader({1, 4, 0}, {1, 3, 0}).m_Minor, 3);
	EXPECT_EQ(ResolveVulkanVersionForLoader({1, 4, 0}, {1, 2, 0}).m_Minor, 1);
	EXPECT_EQ(ResolveVulkanVersionForLoader({1, 3, 0}, {1, 4, 0}).m_Minor, 3);
}

TEST(GraphicsBackendContract, GraphicsModesMapToPlatformBackends)
{
	// 平台分支先算好再进断言：MSVC 的新预处理器不接受宏实参里出现 #if。
#if defined(CONF_PLATFORM_ANDROID) || defined(CONF_PLATFORM_IOS)
	const char *pExpectedCompatibility = graphics_backend::IsBackendCompiled(BACKEND_TYPE_OPENGL_ES) ? "GLES" : "OpenGL";
#else
	const char *pExpectedCompatibility = "OpenGL";
#endif
	EXPECT_STREQ(graphics_backend::BackendNameForGraphicsMode(graphics_backend::GRAPHICS_MODE_COMPATIBILITY),
		pExpectedCompatibility);
#if defined(CONF_PLATFORM_MACOS)
	const char *pExpectedPerformance = graphics_backend::IsBackendCompiled(BACKEND_TYPE_METAL) ? "Metal" : (graphics_backend::IsBackendCompiled(BACKEND_TYPE_VULKAN) ? "Vulkan" : "OpenGL");
#elif defined(CONF_PLATFORM_IOS)
	const char *pExpectedPerformance = graphics_backend::IsBackendCompiled(BACKEND_TYPE_METAL) ? "Metal" : (graphics_backend::IsBackendCompiled(BACKEND_TYPE_OPENGL_ES) ? "GLES" : "OpenGL");
#elif defined(CONF_PLATFORM_ANDROID)
	const char *pExpectedPerformance = graphics_backend::IsBackendCompiled(BACKEND_TYPE_VULKAN) ? "Vulkan" : "GLES";
#else
	const char *pExpectedPerformance = graphics_backend::IsBackendCompiled(BACKEND_TYPE_VULKAN) ? "Vulkan" : "OpenGL";
#endif
	EXPECT_STREQ(graphics_backend::BackendNameForGraphicsMode(graphics_backend::GRAPHICS_MODE_PERFORMANCE),
		pExpectedPerformance);
}

TEST(GraphicsBackendContract, ForcedViewportUsesTopLeftCoordinatesAcrossBackends)
{
	EXPECT_EQ(graphics_viewport::OpenGLViewportY(1920, 0, 1536), 384);
	EXPECT_EQ(graphics_viewport::OpenGLViewportY(0, 0, 1536), 0);

	const vec2 Position = graphics_viewport::MapTouchPosition({0.75f, 0.5f}, {1920.0f, 1080.0f}, {1350.0f, 1080.0f});
	EXPECT_NEAR(Position.x, 1.0f, 0.0001f);
	EXPECT_NEAR(Position.y, 0.5f, 0.0001f);

	const vec2 ClampedPosition = graphics_viewport::MapTouchPosition({1.0f, 1.0f}, {1920.0f, 1080.0f}, {1350.0f, 1080.0f});
	EXPECT_EQ(ClampedPosition.x, 1.0f);
	EXPECT_EQ(ClampedPosition.y, 1.0f);

	const vec2 Delta = graphics_viewport::MapTouchDelta({0.1f, -0.2f}, {1920.0f, 1080.0f}, {1350.0f, 1080.0f});
	EXPECT_NEAR(Delta.x, 0.142222f, 0.0001f);
	EXPECT_NEAR(Delta.y, -0.2f, 0.0001f);
}

TEST(GraphicsBackendContract, ParsesKnownNamesAndRejectsUnknownNames)
{
	EXPECT_EQ(graphics_backend::ParseBackendName("opengl", BACKEND_TYPE_VULKAN), BACKEND_TYPE_OPENGL);
	const EBackendType ExpectedOpenGLES = graphics_backend::IsBackendSelectable(BACKEND_TYPE_OPENGL_ES) ? BACKEND_TYPE_OPENGL_ES : BACKEND_TYPE_VULKAN;
	EXPECT_EQ(graphics_backend::ParseBackendName("OpenGL ES", ExpectedOpenGLES == BACKEND_TYPE_VULKAN ? BACKEND_TYPE_VULKAN : BACKEND_TYPE_OPENGL), ExpectedOpenGLES);
	const EBackendType ExpectedVulkan = graphics_backend::IsBackendSelectable(BACKEND_TYPE_VULKAN) ? BACKEND_TYPE_VULKAN : BACKEND_TYPE_OPENGL;
	EXPECT_EQ(graphics_backend::ParseBackendName("vUlKaN", BACKEND_TYPE_OPENGL), ExpectedVulkan);
	EXPECT_EQ(graphics_backend::ParseBackendName("unknown", BACKEND_TYPE_OPENGL), BACKEND_TYPE_OPENGL);
	EXPECT_EQ(graphics_backend::ParseBackendName(nullptr, BACKEND_TYPE_VULKAN), BACKEND_TYPE_VULKAN);
}

TEST(GraphicsBackendContract, MetalIsSelectableOnlyWhenCompiledForApplePlatforms)
{
#if (defined(CONF_PLATFORM_MACOS) || defined(CONF_PLATFORM_IOS)) && defined(CONF_BACKEND_METAL) && defined(CONF_BACKEND_METAL_READY)
	EXPECT_TRUE(graphics_backend::IsMetalCompiled());
	EXPECT_EQ(graphics_backend::ParseBackendName("Metal", BACKEND_TYPE_OPENGL), BACKEND_TYPE_METAL);
#else
	EXPECT_FALSE(graphics_backend::IsMetalCompiled());
	EXPECT_EQ(graphics_backend::ParseBackendName("Metal", BACKEND_TYPE_OPENGL), BACKEND_TYPE_OPENGL);
#endif
	EXPECT_TRUE(graphics_backend::IsKnownBackendName("Metal"));
	EXPECT_TRUE(graphics_backend::IsKnownBackendName("OpenGL ES"));
	EXPECT_FALSE(graphics_backend::IsKnownBackendName("custom"));
	EXPECT_EQ(graphics_backend::IsKnownUnavailableBackendName("Metal"), !graphics_backend::IsMetalCompiled());
	EXPECT_FALSE(graphics_backend::IsKnownUnavailableBackendName("OpenGL"));
	EXPECT_FALSE(graphics_backend::IsKnownUnavailableBackendName("custom"));
	EXPECT_FALSE(graphics_backend::IsBackendSelectable(BACKEND_TYPE_AUTO));
}

TEST(GraphicsBackendContract, VersionTupleIdentityIsApiNeutral)
{
	EXPECT_TRUE(graphics_backend::UsesOpenGLVersionTuple(BACKEND_TYPE_OPENGL));
	EXPECT_TRUE(graphics_backend::UsesOpenGLVersionTuple(BACKEND_TYPE_OPENGL_ES));
	EXPECT_FALSE(graphics_backend::UsesOpenGLVersionTuple(BACKEND_TYPE_VULKAN));
	EXPECT_FALSE(graphics_backend::UsesOpenGLVersionTuple(BACKEND_TYPE_METAL));
	EXPECT_TRUE(graphics_backend::PreservesOpenGLVersionTuple(BACKEND_TYPE_METAL));
	EXPECT_FALSE(graphics_backend::PreservesOpenGLVersionTuple(BACKEND_TYPE_VULKAN));
}

TEST(GraphicsBackendContract, FrameSerializationWorkaroundIsVulkanOnly)
{
	EXPECT_FALSE(graphics_backend::RequiresFrameSerializationWorkaround(BACKEND_TYPE_OPENGL));
	EXPECT_FALSE(graphics_backend::RequiresFrameSerializationWorkaround(BACKEND_TYPE_OPENGL_ES));
	EXPECT_TRUE(graphics_backend::RequiresFrameSerializationWorkaround(BACKEND_TYPE_VULKAN));
	EXPECT_FALSE(graphics_backend::RequiresFrameSerializationWorkaround(BACKEND_TYPE_METAL));
}

TEST(GraphicsBackendContract, MetalInitializationIsNotAnOpenGLVersionFailure)
{
	EXPECT_TRUE(IsGraphicsBackendMetalInitError(GRAPHICS_BACKEND_ERROR_CODE_METAL_INIT_FAILED));
	EXPECT_FALSE(IsGraphicsBackendOpenGLRetryableError(GRAPHICS_BACKEND_ERROR_CODE_METAL_INIT_FAILED));
	EXPECT_TRUE(IsGraphicsBackendOpenGLRetryableError(GRAPHICS_BACKEND_ERROR_CODE_GL_CONTEXT_FAILED));
	EXPECT_TRUE(IsGraphicsBackendOpenGLRetryableError(GRAPHICS_BACKEND_ERROR_CODE_GL_VERSION_FAILED));
}

TEST(GraphicsBackendContract, MetalFactoryRejectsUnimplementedCommandsWithoutStickyInitError)
{
#if (defined(CONF_PLATFORM_MACOS) || defined(CONF_PLATFORM_IOS)) && defined(CONF_BACKEND_METAL) && defined(CONF_BACKEND_METAL_READY)
	CCommandProcessorFragment_GLBase *pMetal = CreateMetalCommandProcessorFragment();
	ASSERT_NE(pMetal, nullptr);

	CCommandProcessorFragment_GLBase::SCommand_PreInit PreInit;
	PreInit.m_pWindow = nullptr;
	PreInit.m_Width = 0;
	PreInit.m_Height = 0;
	PreInit.m_pVendorString = nullptr;
	PreInit.m_pVersionString = nullptr;
	PreInit.m_pRendererString = nullptr;
	PreInit.m_pGpuList = nullptr;
	PreInit.m_VSync = true;
	PreInit.m_pInitError = nullptr;
	PreInit.m_pErrStringPtr = nullptr;
	EXPECT_EQ(pMetal->RunCommand(&PreInit), RUN_COMMAND_COMMAND_HANDLED);

	CCommandBuffer::SCommand_Render Render;
	EXPECT_EQ(pMetal->RunCommand(&Render), RUN_COMMAND_COMMAND_ERROR);
	const SGfxErrorContainer &RenderError = pMetal->GetError();
	ASSERT_EQ(RenderError.m_ErrorType, GFX_ERROR_TYPE_RENDER_CMD_FAILED);
	ASSERT_EQ(RenderError.m_vErrors.size(), 1U);
	EXPECT_NE(RenderError.m_vErrors[0].m_Err.find("backend_state=uninitialized"), std::string::npos);
	EXPECT_NE(RenderError.m_vErrors[0].m_Err.find("frame_id=0"), std::string::npos);

	CCommandProcessorFragment_SDL::SCommand_Init SdlInit;
	EXPECT_EQ(pMetal->RunCommand(&SdlInit), RUN_COMMAND_COMMAND_UNHANDLED);

	delete pMetal;
	pMetal = CreateMetalCommandProcessorFragment();
	ASSERT_NE(pMetal, nullptr);
	int InitError = 0;
	const char *pErrorString = nullptr;
	CCommandProcessorFragment_GLBase::SCommand_Init Init;
	Init.m_pWindow = nullptr;
	Init.m_Width = 0;
	Init.m_Height = 0;
	Init.m_pStorage = nullptr;
	Init.m_pTextureMemoryUsage = nullptr;
	Init.m_pBufferMemoryUsage = nullptr;
	Init.m_pStreamMemoryUsage = nullptr;
	Init.m_pStagingMemoryUsage = nullptr;
	Init.m_pGpuList = nullptr;
	Init.m_pReadPresentedImageDataFunc = nullptr;
	Init.m_pCapabilities = nullptr;
	Init.m_pInitError = &InitError;
	Init.m_pErrStringPtr = &pErrorString;
	Init.m_pVendorString = nullptr;
	Init.m_pVersionString = nullptr;
	Init.m_pRendererString = nullptr;
	Init.m_RequestedMajor = 0;
	Init.m_RequestedMinor = 0;
	Init.m_RequestedPatch = 0;
	Init.m_RequestedBackend = BACKEND_TYPE_METAL;
	Init.m_GlewMajor = 0;
	Init.m_GlewMinor = 0;
	Init.m_GlewPatch = 0;
	EXPECT_EQ(pMetal->RunCommand(&Init), RUN_COMMAND_COMMAND_HANDLED);
	EXPECT_EQ(InitError, -1);
	ASSERT_NE(pErrorString, nullptr);
	EXPECT_EQ(pMetal->GetError().m_ErrorType, GFX_ERROR_TYPE_NONE);

	delete pMetal;
#else
	SUCCEED();
#endif
}

TEST(GraphicsBackendContract, SafeBackendConfigIsDeterministic)
{
	constexpr auto SafeConfig = graphics_backend::SafeBackendConfig();
	EXPECT_STREQ(SafeConfig.m_pBackend, "OpenGL");
	EXPECT_EQ(SafeConfig.m_GLMajor, 4);
	EXPECT_EQ(SafeConfig.m_GLMinor, 1);
	EXPECT_EQ(SafeConfig.m_GLPatch, 0);
	EXPECT_EQ(SafeConfig.m_FsaaSamples, 0);
	EXPECT_EQ(SafeConfig.m_Fullscreen, 0);
	EXPECT_EQ(SafeConfig.m_Borderless, 0);
}

TEST(GraphicsBackendContract, ConfiguredIdentityMatchesOnlyRelevantVersionFields)
{
	EXPECT_TRUE(graphics_backend::MatchesConfiguredBackend(BACKEND_TYPE_OPENGL, "OpenGL", 4, 1, 0, "opengl", 4, 1, 0));
	EXPECT_FALSE(graphics_backend::MatchesConfiguredBackend(BACKEND_TYPE_OPENGL, "OpenGL", 4, 1, 0, "opengl", 3, 3, 0));
#if defined(CONF_BACKEND_VULKAN)
	EXPECT_TRUE(graphics_backend::MatchesConfiguredBackend(BACKEND_TYPE_VULKAN, "Vulkan", 0, 0, 0, "vulkan", 4, 1, 0));
#else
	EXPECT_FALSE(graphics_backend::MatchesConfiguredBackend(BACKEND_TYPE_VULKAN, "Vulkan", 0, 0, 0, "vulkan", 4, 1, 0));
#endif
#if (defined(CONF_PLATFORM_MACOS) || defined(CONF_PLATFORM_IOS)) && defined(CONF_BACKEND_METAL) && defined(CONF_BACKEND_METAL_READY)
	EXPECT_TRUE(graphics_backend::MatchesConfiguredBackend(BACKEND_TYPE_METAL, "Metal", 0, 0, 0, "metal", 4, 1, 0));
#else
	EXPECT_FALSE(graphics_backend::MatchesConfiguredBackend(BACKEND_TYPE_METAL, "Metal", 0, 0, 0, "metal", 4, 1, 0));
#endif
}

TEST(GraphicsBackendContract, BackendCapabilitiesDefaultToUnsupported)
{
	SBackendCapabilities Capabilities;
	EXPECT_FALSE(Capabilities.m_TileBuffering);
	EXPECT_FALSE(Capabilities.m_QuadBuffering);
	EXPECT_FALSE(Capabilities.m_TextBuffering);
	EXPECT_FALSE(Capabilities.m_QuadContainerBuffering);
	EXPECT_FALSE(Capabilities.m_MipMapping);
	EXPECT_FALSE(Capabilities.m_NPOTTextures);
	EXPECT_FALSE(Capabilities.m_3DTextures);
	EXPECT_FALSE(Capabilities.m_2DArrayTextures);
	EXPECT_FALSE(Capabilities.m_2DArrayTexturesAsExtension);
	EXPECT_FALSE(Capabilities.m_ShaderSupport);
	EXPECT_FALSE(Capabilities.m_ProceduralRing.load(std::memory_order_relaxed));
	EXPECT_FALSE(Capabilities.m_RenderTargets);
	EXPECT_FALSE(Capabilities.m_RenderTargetGaussianBlur);
	EXPECT_FALSE(Capabilities.m_BackbufferCapture);
	EXPECT_FALSE(Capabilities.m_RenderTargetExternalPassRequiresSingleSample);
	EXPECT_STREQ(Capabilities.m_pRenderTargetSupportReason, "not_initialized");
	EXPECT_FALSE(Capabilities.m_TrianglesAsQuads);
	EXPECT_EQ(Capabilities.m_ContextMajor, 0);
	EXPECT_EQ(Capabilities.m_ContextMinor, 0);
	EXPECT_EQ(Capabilities.m_ContextPatch, 0);
	EXPECT_EQ(Capabilities.m_DetectedContextMajor, 0);
	EXPECT_EQ(Capabilities.m_DetectedContextMinor, 0);
	EXPECT_EQ(Capabilities.m_DetectedContextPatch, 0);
}

TEST(GraphicsBackendContract, BackendCapabilitiesResetClearsPreviousBackendState)
{
	SBackendCapabilities Capabilities;
	Capabilities.m_TileBuffering = true;
	Capabilities.m_QuadBuffering = true;
	Capabilities.m_TextBuffering = true;
	Capabilities.m_QuadContainerBuffering = true;
	Capabilities.m_MipMapping = true;
	Capabilities.m_NPOTTextures = true;
	Capabilities.m_3DTextures = true;
	Capabilities.m_2DArrayTextures = true;
	Capabilities.m_2DArrayTexturesAsExtension = true;
	Capabilities.m_ShaderSupport = true;
	Capabilities.m_MediaIslandSdf = true;
	Capabilities.m_RoundedRectSdf = true;
	Capabilities.m_ProceduralRing.store(true, std::memory_order_relaxed);
	Capabilities.m_RenderTargets = true;
	Capabilities.m_RenderTargetGaussianBlur = true;
	Capabilities.m_BackbufferCapture = true;
	Capabilities.m_RenderTargetExternalPassRequiresSingleSample = true;
	Capabilities.m_pRenderTargetSupportReason = "supported";
	Capabilities.m_TrianglesAsQuads = true;
	Capabilities.m_ContextMajor = 4;
	Capabilities.m_ContextMinor = 1;
	Capabilities.m_ContextPatch = 2;
	Capabilities.m_DetectedContextMajor = 4;
	Capabilities.m_DetectedContextMinor = 1;
	Capabilities.m_DetectedContextPatch = 2;

	Capabilities.Reset();

	EXPECT_FALSE(Capabilities.m_TileBuffering);
	EXPECT_FALSE(Capabilities.m_QuadBuffering);
	EXPECT_FALSE(Capabilities.m_TextBuffering);
	EXPECT_FALSE(Capabilities.m_QuadContainerBuffering);
	EXPECT_FALSE(Capabilities.m_MipMapping);
	EXPECT_FALSE(Capabilities.m_NPOTTextures);
	EXPECT_FALSE(Capabilities.m_3DTextures);
	EXPECT_FALSE(Capabilities.m_2DArrayTextures);
	EXPECT_FALSE(Capabilities.m_2DArrayTexturesAsExtension);
	EXPECT_FALSE(Capabilities.m_ShaderSupport);
	EXPECT_FALSE(Capabilities.m_MediaIslandSdf);
	EXPECT_FALSE(Capabilities.m_RoundedRectSdf);
	EXPECT_FALSE(Capabilities.m_ProceduralRing.load(std::memory_order_relaxed));
	EXPECT_FALSE(Capabilities.m_RenderTargets);
	EXPECT_FALSE(Capabilities.m_RenderTargetGaussianBlur);
	EXPECT_FALSE(Capabilities.m_BackbufferCapture);
	EXPECT_FALSE(Capabilities.m_RenderTargetExternalPassRequiresSingleSample);
	EXPECT_STREQ(Capabilities.m_pRenderTargetSupportReason, "not_initialized");
	EXPECT_FALSE(Capabilities.m_TrianglesAsQuads);
	EXPECT_EQ(Capabilities.m_ContextMajor, 0);
	EXPECT_EQ(Capabilities.m_ContextMinor, 0);
	EXPECT_EQ(Capabilities.m_ContextPatch, 0);
	EXPECT_EQ(Capabilities.m_DetectedContextMajor, 0);
	EXPECT_EQ(Capabilities.m_DetectedContextMinor, 0);
	EXPECT_EQ(Capabilities.m_DetectedContextPatch, 0);
}

TEST(GraphicsBackendContract, SingleSampleFeaturesFallBackUnderMsaa)
{
	// 双因子闸门：后端支持且（无单采样约束或 MSAA 关闭）时，捕获/模糊能力才对外可用。
	EXPECT_TRUE(IGraphics::SingleSampleFeatureAllowedUnderMsaa(true, false, 0));
	EXPECT_TRUE(IGraphics::SingleSampleFeatureAllowedUnderMsaa(true, false, 4));
	EXPECT_TRUE(IGraphics::SingleSampleFeatureAllowedUnderMsaa(true, true, 0));
	EXPECT_FALSE(IGraphics::SingleSampleFeatureAllowedUnderMsaa(true, true, 2));
	EXPECT_FALSE(IGraphics::SingleSampleFeatureAllowedUnderMsaa(true, true, 4));
	EXPECT_FALSE(IGraphics::SingleSampleFeatureAllowedUnderMsaa(false, false, 0));
	EXPECT_FALSE(IGraphics::SingleSampleFeatureAllowedUnderMsaa(false, true, 0));
}

TEST(GraphicsBackendContract, CrashReportIdentifiesActualBackend)
{
	// 运行期图形故障报告按实际崩溃后端归类，供诊断与打包反馈使用。
	EXPECT_EQ(graphics_backend::BackendFromCrashReport("Graphics backend: Vulkan 1.4.0\nGraphics error:/nDevice lost\n"), BACKEND_TYPE_VULKAN);
	EXPECT_EQ(graphics_backend::BackendFromCrashReport("Exception module: opengl32.dll\r\nGraphics backend: OpenGL\r\n"), BACKEND_TYPE_OPENGL);
	EXPECT_EQ(graphics_backend::BackendFromCrashReport("Graphics backend: OpenGL ES 3.0.0\n"), BACKEND_TYPE_OPENGL_ES);
	EXPECT_EQ(graphics_backend::BackendFromCrashReport("Graphics backend: GLES 3.0.0\n"), BACKEND_TYPE_OPENGL_ES);
	EXPECT_EQ(graphics_backend::BackendFromCrashReport("Graphics backend: Metal 3.0.0\n"), BACKEND_TYPE_METAL);
	EXPECT_EQ(graphics_backend::BackendFromCrashReport("Report type: graphics_fatal_error\nConfigured graphics backend: Vulkan API 1.4\n"), BACKEND_TYPE_VULKAN);
	EXPECT_EQ(graphics_backend::BackendFromCrashReport("Report type: graphics_fatal_error\nGraphics backend: Vulkan\nConfigured graphics backend: OpenGL\nGraphics error:/nVK_ERROR_DEVICE_LOST/n"), BACKEND_TYPE_VULKAN);
	EXPECT_EQ(graphics_backend::BackendFromCrashReport("Graphics backend: headless\n"), BACKEND_TYPE_AUTO);
	EXPECT_EQ(graphics_backend::BackendFromCrashReport("Graphics error: Vulkan\n"), BACKEND_TYPE_AUTO);
}

TEST(QmNewUiMenuBranches, OpenGLSelectionUsesRuntimeContextDetection)
{
	const SOpenGLVersion AutoGL = AutoOpenGLProbeVersion(EBackendType::BACKEND_TYPE_OPENGL);
	const SOpenGLVersion AutoGLES = AutoOpenGLProbeVersion(EBackendType::BACKEND_TYPE_OPENGL_ES);
	EXPECT_EQ(AutoGL.m_Major, 4);
#if defined(CONF_PLATFORM_MACOS)
	EXPECT_EQ(AutoGL.m_Minor, 1);
#else
	EXPECT_EQ(AutoGL.m_Minor, 6);
#endif
	EXPECT_EQ(AutoGLES.m_Major, 3);
	EXPECT_EQ(AutoGLES.m_Minor, 0);
	EXPECT_TRUE(IsOpenGLVersionAtLeast({4, 6, 0}, {4, 5, 0}));
	EXPECT_FALSE(IsOpenGLVersionAtLeast({4, 5, 0}, {4, 6, 0}));
	EXPECT_TRUE(IsOpenGLVersionAtLeast({3, 3, 0}, {3, 3, 0}));
	EXPECT_FALSE(IsOpenGLVersionAtLeast({1, 2, 0}, {1, 2, 1}));
	SOpenGLVersion ProbeVersion{4, 6, 0};
	for(int Minor = 5; Minor >= 0; --Minor)
	{
		EXPECT_TRUE(NextAutoOpenGLProbeVersion(ProbeVersion));
		EXPECT_EQ(ProbeVersion.m_Major, 4);
		EXPECT_EQ(ProbeVersion.m_Minor, Minor);
	}
	EXPECT_TRUE(NextAutoOpenGLProbeVersion(ProbeVersion));
	EXPECT_EQ(ProbeVersion.m_Major, 3);
	EXPECT_EQ(ProbeVersion.m_Minor, 3);
	EXPECT_TRUE(NextAutoOpenGLProbeVersion(ProbeVersion));
	EXPECT_EQ(ProbeVersion.m_Minor, 2);

	const SOpenGLVersion Actual41{4, 1, 0};
	EXPECT_TRUE(ShouldSyncActualOpenGLVersion(EBackendType::BACKEND_TYPE_OPENGL, {3, 3, 0}, Actual41));
	EXPECT_TRUE(ShouldSyncActualOpenGLVersion(EBackendType::BACKEND_TYPE_OPENGL, {4, 6, 0}, Actual41));
	EXPECT_FALSE(ShouldSyncActualOpenGLVersion(EBackendType::BACKEND_TYPE_OPENGL, {3, 0, 0}, Actual41));
	EXPECT_FALSE(ShouldSyncActualOpenGLVersion(EBackendType::BACKEND_TYPE_VULKAN, {3, 3, 0}, Actual41));
	EXPECT_TRUE(ShouldSyncActualOpenGLVersion(EBackendType::BACKEND_TYPE_OPENGL_ES, {3, 0, 0}, {3, 2, 0}));
	EXPECT_FALSE(ShouldSyncActualOpenGLVersion(EBackendType::BACKEND_TYPE_OPENGL_ES, {1, 0, 0}, {3, 2, 0}));
}
