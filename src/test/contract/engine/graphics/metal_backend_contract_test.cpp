#include <engine/client/backend/metal/metal_shader_manifest.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <string>

TEST(MetalBackendContract, RuntimeGpuFailureIsPublishedAsAStickyRenderError)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	EXPECT_NE(Source.find("m_GpuFailureFrameId"), std::string::npos);
	EXPECT_NE(Source.find("m_GpuFailureCommandId"), std::string::npos);
	EXPECT_NE(Source.find("m_GpuFailureStatus"), std::string::npos);
	EXPECT_NE(Source.find("m_GpuFailureMutex"), std::string::npos);
	EXPECT_NE(Source.find("pError.domain.UTF8String"), std::string::npos);
	EXPECT_NE(Source.find("pError.code"), std::string::npos);
	EXPECT_NE(Source.find("GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED"), std::string::npos);
	EXPECT_NE(Source.find("stage=completion"), std::string::npos);
	EXPECT_NE(Source.find("command_id="), std::string::npos);
	EXPECT_NE(Source.find("RUN_COMMAND_COMMAND_ERROR"), std::string::npos);
	EXPECT_NE(Source.find("if(!Success)\n\t\t\t\tRecordGpuFailure"), std::string::npos);
}

TEST(MetalBackendContract, RuntimeFailureAllowsOnlyLifecycleCommandsThrough)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	const size_t Guard = Source.find("const bool IsLifecycleCommand");
	const size_t FailureCheck = Source.find("if(!IsLifecycleCommand && SetGpuFailureError())", Guard);
	ASSERT_NE(Guard, std::string::npos);
	ASSERT_NE(FailureCheck, std::string::npos);
	EXPECT_LT(Guard, FailureCheck);
	EXPECT_NE(Source.find("CMD_SHUTDOWN", Guard), std::string::npos);
	EXPECT_NE(Source.find("CMD_POST_SHUTDOWN", Guard), std::string::npos);
}

TEST(MetalBackendContract, BufferContainerCommandsHaveExplicitResourceHandling)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	EXPECT_NE(Source.find("CMD_CREATE_BUFFER_CONTAINER"), std::string::npos);
	EXPECT_NE(Source.find("CMD_UPDATE_BUFFER_CONTAINER"), std::string::npos);
	EXPECT_NE(Source.find("CMD_DELETE_BUFFER_CONTAINER"), std::string::npos);
	EXPECT_NE(Source.find("CMD_INDICES_REQUIRED_NUM_NOTIFY"), std::string::npos);
	EXPECT_NE(Source.find("m_RequiredIndicesNum = std::max"), std::string::npos);
	EXPECT_NE(Source.find("MetalValidateVertexAttribute"), std::string::npos);
	EXPECT_NE(Source.find("if(Stride == 0)"), std::string::npos);
	EXPECT_NE(Source.find("TightStride"), std::string::npos);
}

TEST(MetalBackendContract, TileAndQuadCommandsUseDedicatedMetalPipelines)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	EXPECT_NE(Source.find("CMD_RENDER_TILE_LAYER"), std::string::npos);
	EXPECT_NE(Source.find("CMD_RENDER_BORDER_TILE"), std::string::npos);
	EXPECT_NE(Source.find("CMD_RENDER_QUAD_LAYER_GROUPED"), std::string::npos);
	EXPECT_NE(Source.find("TilePipelineIndex"), std::string::npos);
	EXPECT_NE(Source.find("QuadPipelineIndex"), std::string::npos);
	EXPECT_NE(Source.find("MatchesContainerLayout"), std::string::npos);
	EXPECT_NE(Source.find("MTLIndexTypeUInt32"), std::string::npos);
	EXPECT_NE(Source.find("m_QuadIndexCount"), std::string::npos);
	EXPECT_NE(Source.find("IndexCount % 6 != 0"), std::string::npos);
	EXPECT_NE(Source.find("sizeof(uint32_t)"), std::string::npos);
	EXPECT_NE(Source.find("EnsureQuadIndexCapacity(RequiredIndicesNum)"), std::string::npos);
	EXPECT_NE(Source.find("bool EnsureQuadIndexCapacity(size_t RequiredIndexCount)"), std::string::npos);
	EXPECT_NE(Source.find("EnsureQuadIndexCapacity(MaxQuadEnd * 6)"), std::string::npos);

	const std::string Shader = ReadTestSourceFile("data/shader/metal/qmclient.metal");
	EXPECT_NE(Shader.find("qmclient_tile_vertex"), std::string::npos);
	EXPECT_NE(Shader.find("qmclient_tile_border_vertex"), std::string::npos);
	EXPECT_NE(Shader.find("TexScale.yx"), std::string::npos);
	EXPECT_NE(Shader.find("float4 m_TexCoord [[center_no_perspective]];"), std::string::npos);
	EXPECT_NE(Shader.find("float4 m_TexCoord [[centroid_no_perspective]];"), std::string::npos);
	EXPECT_NE(Shader.find("texture2d_array<float> Texture [[texture(0)]]"), std::string::npos);
	EXPECT_NE(Shader.find("qmclient_tile_border_textured_fragment"), std::string::npos);
	EXPECT_NE(Shader.find("const float2 TexCoord = fract(Input.m_TexCoord.xy);"), std::string::npos);
	EXPECT_NE(Shader.find("const float2 Dx = dfdx(Input.m_TexCoord.xy);"), std::string::npos);
	EXPECT_NE(Shader.find("const float2 Dy = dfdy(Input.m_TexCoord.xy);"), std::string::npos);
	EXPECT_NE(Shader.find("gradient2d(Dx, Dy)"), std::string::npos);
	EXPECT_NE(Source.find("TileBorderTexturedFragment"), std::string::npos);
	EXPECT_NE(Source.find("TileBorderVertex"), std::string::npos);
	EXPECT_NE(Source.find("m_TextureArray == nil"), std::string::npos);
	EXPECT_NE(Source.find("setFragmentTexture:m_vTextureSlots[Command.m_State.m_Texture].m_TextureArray"), std::string::npos);
	EXPECT_NE(Shader.find("qmclient_quad_vertex_grouped"), std::string::npos);
	EXPECT_NE(Shader.find("qmclient_quad_vertex_ungrouped"), std::string::npos);
}

TEST(MetalBackendContract, BufferedTextAndQuadContainerCommandsUseValidatedResources)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	EXPECT_NE(Source.find("CMD_RENDER_TEXT"), std::string::npos);
	EXPECT_NE(Source.find("CMD_RENDER_QUAD_CONTAINER"), std::string::npos);
	EXPECT_NE(Source.find("CMD_RENDER_QUAD_CONTAINER_EX"), std::string::npos);
	EXPECT_NE(Source.find("CMD_RENDER_QUAD_CONTAINER_SPRITE_MULTIPLE"), std::string::npos);
	EXPECT_NE(Source.find("GetStandardQuadDrawResources"), std::string::npos);
	EXPECT_NE(Source.find("MatchesStandardVertexLayout"), std::string::npos);
	EXPECT_NE(Source.find("QuadContainerExPipelineIndex"), std::string::npos);
	EXPECT_NE(Source.find("SpriteMultiplePipelineIndex"), std::string::npos);
	EXPECT_NE(Source.find("instanceCount:RenderCount"), std::string::npos);
	EXPECT_NE(Source.find("METAL_MAX_SPRITES"), std::string::npos);
	EXPECT_NE(Source.find("QuadEnd > std::numeric_limits<size_t>::max() / 6"), std::string::npos);

	const std::string Shader = ReadTestSourceFile("data/shader/metal/qmclient.metal");
	EXPECT_NE(Shader.find("qmclient_quad_container_ex_vertex"), std::string::npos);
	EXPECT_NE(Shader.find("qmclient_quad_container_ex_textured_fragment"), std::string::npos);
	EXPECT_NE(Shader.find("qmclient_sprite_multiple_vertex"), std::string::npos);
	EXPECT_NE(Shader.find("Uniforms.m_aRenderInfo[InstanceId]"), std::string::npos);
}

TEST(MetalBackendContract, VSyncMirrorsTheExplicitGraphicsSetting)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	EXPECT_NE(Source.find("m_VSync = pCommand->m_VSync != 0;"), std::string::npos);
	EXPECT_NE(Source.find("displaySyncEnabled = m_VSync;"), std::string::npos);
	EXPECT_NE(Source.find("pLayer.displaySyncEnabled = m_VSync;"), std::string::npos);
	EXPECT_EQ(Source.find("displaySyncEnabled = YES"), std::string::npos);
	EXPECT_NE(Source.find("layer configured: vsync=%d display_sync=%d allows_next_drawable_timeout=%d max_drawables=%lu"), std::string::npos);
	EXPECT_NE(Source.find("layer vsync changed: requested=%d"), std::string::npos);
}

TEST(MetalBackendContract, ShaderBuildPreservesAppleDeploymentTarget)
{
	const std::string CMake = ReadTestSourceFile("cmake/BuildMetalShaders.cmake");
	EXPECT_NE(CMake.find("set(METAL_DEPLOYMENT_FLAGS)"), std::string::npos);
	EXPECT_NE(CMake.find("-mios-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}"), std::string::npos);
	EXPECT_NE(CMake.find("-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}"), std::string::npos);
	const size_t MetalCommand = CMake.find(" metal ${METAL_DEPLOYMENT_FLAGS} -c ");
	ASSERT_NE(MetalCommand, std::string::npos);
	EXPECT_EQ(CMake.find("metallib ${METAL_DEPLOYMENT_FLAGS}"), std::string::npos);
}

TEST(MetalBackendContract, TileArraysDoNotSampleUninitializedMipLevels)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	EXPECT_NE(Source.find("m_TileRepeatSampler"), std::string::npos);
	EXPECT_NE(Source.find("m_TileClampSampler"), std::string::npos);
	EXPECT_NE(Source.find("MTLSamplerMipFilterNotMipmapped"), std::string::npos);
	EXPECT_NE(Source.find("m_TileClampSampler : m_TileRepeatSampler"), std::string::npos);
}

TEST(MetalBackendContract, ScissorConvertsFromOpenGLCoordinatesAndCachesState)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	const size_t SetScissor = Source.find("void SetScissor(const CCommandBuffer::SState &State)");
	ASSERT_NE(SetScissor, std::string::npos);
	EXPECT_NE(Source.find("Height) - ClipTop", SetScissor), std::string::npos);
	EXPECT_NE(Source.find("ClipTop - ClipBottom", SetScissor), std::string::npos);
	EXPECT_NE(Source.find("m_HasBoundScissorRect", SetScissor), std::string::npos);
	EXPECT_NE(Source.find("ScissorRectsEqual", SetScissor), std::string::npos);
}

TEST(MetalBackendContract, DrawablePoolUsesTimeoutWithoutApplicationFrameCap)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	const size_t Commit = Source.find("bool CommitCurrentFrame(bool Present, bool WaitForCompletion)");
	ASSERT_NE(Commit, std::string::npos);
	EXPECT_EQ(Source.find("PresentBackpressure", Commit), std::string::npos);
	EXPECT_EQ(Source.find("m_pPresentTracker", Commit), std::string::npos);
	EXPECT_NE(Source.find("pLayer.allowsNextDrawableTimeout = !m_VSync;"), std::string::npos);
	EXPECT_NE(Source.find("The drawable pool is finite", 0), std::string::npos);
}

TEST(MetalBackendContract, RecreatedMultisampleAttachmentStartsWithClear)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	const size_t EnsureMsaa = Source.find("bool EnsureMultiSampleTexture(uint32_t Width, uint32_t Height)");
	const size_t Invalidate = Source.find("SetCurrentBackbufferHasContents(false);", EnsureMsaa);
	const size_t Destroy = Source.find("DestroyMultiSampleTexture(m_CurrentFrameSlot);", EnsureMsaa);
	ASSERT_NE(EnsureMsaa, std::string::npos);
	ASSERT_NE(Invalidate, std::string::npos);
	ASSERT_NE(Destroy, std::string::npos);
	EXPECT_LT(Invalidate, Destroy);
}

TEST(MetalBackendContract, MultisampleAttachmentsAreOwnedByFrameSlots)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	EXPECT_NE(Source.find("id<MTLTexture> m_MultiSampleTexture = nil;"), std::string::npos);
	EXPECT_NE(Source.find("std::array<SFrameSlot, gs_FrameSlotCount> m_aFrameSlots{};"), std::string::npos);
	EXPECT_EQ(Source.find("id<MTLTexture> m_MultiSampleTexture = nil;\n\tid<MTLBuffer> m_LastPresentedReadback"), std::string::npos);
	EXPECT_NE(Source.find("void DestroyAllMultiSampleTextures()"), std::string::npos);
	EXPECT_NE(Source.find("WaitForGpuIdle();\n\t\tDestroyAllMultiSampleTextures();"), std::string::npos);
	EXPECT_NE(Source.find("m_aFrameSlots[m_CurrentFrameSlot].m_MultiSampleTexture"), std::string::npos);
}

TEST(MetalBackendContract, MetalPerfReportsEffectiveLayerPresentationState)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	EXPECT_NE(Source.find("display_sync=%d allows_next_drawable_timeout=%d max_drawables=%lu presents_with_transaction=%d"), std::string::npos);
	EXPECT_NE(Source.find("pLayer.displaySyncEnabled"), std::string::npos);
	EXPECT_NE(Source.find("AllowsNextDrawableTimeout"), std::string::npos);
	EXPECT_NE(Source.find("pLayer.maximumDrawableCount"), std::string::npos);
	EXPECT_NE(Source.find("pLayer.framebufferOnly = NO;"), std::string::npos);
}

TEST(MetalBackendContract, ZeroSizedDrawableNeverBlocksOnNextDrawable)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	const size_t BeginEncoder = Source.find("bool BeginRenderEncoder(const MTLClearColor &ClearColor)");
	const size_t NextDrawable = Source.find("[pLayer nextDrawable]", BeginEncoder);
	ASSERT_NE(BeginEncoder, std::string::npos);
	ASSERT_NE(NextDrawable, std::string::npos);
	const size_t ZeroSizeGuard = Source.find("m_DrawableWidth == 0 || m_DrawableHeight == 0", BeginEncoder);
	ASSERT_NE(ZeroSizeGuard, std::string::npos);
	EXPECT_LT(ZeroSizeGuard, NextDrawable);
	EXPECT_NE(Source.find("m_SkipCurrentFrame = true", ZeroSizeGuard), std::string::npos);
}

TEST(MetalBackendContract, PresentedDrawableIsReleasedAfterCommit)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	const size_t Commit = Source.find("[m_CurrentCommandBuffer commit];");
	ASSERT_NE(Commit, std::string::npos);
	const size_t Release = Source.find("ReleaseMetalObject(m_CurrentDrawable);", Commit);
	ASSERT_NE(Release, std::string::npos);
	EXPECT_LT(Commit, Release);
	EXPECT_NE(Source.find("m_CurrentDrawable = nil;", Release), std::string::npos);
}

TEST(MetalBackendContract, DrawableFailureDoesNotContinueOrReportPresent)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	const size_t Commit = Source.find("bool CommitCurrentFrame(bool Present, bool WaitForCompletion)");
	const size_t Requested = Source.find("const bool PresentationRequested", Commit);
	const size_t Return = Source.find("return PresentationSucceeded;", Commit);
	ASSERT_NE(Commit, std::string::npos);
	ASSERT_NE(Requested, std::string::npos);
	ASSERT_NE(Return, std::string::npos);
	EXPECT_NE(Source.find("const bool PresentationSucceeded = !PresentationRequested || BackbufferPresented;", Requested), std::string::npos);
	EXPECT_NE(Source.find("const bool BackbufferPresented = CanPresent && FrameFinalized;", Requested), std::string::npos);
	EXPECT_NE(Source.find("if(BackbufferPresented)\n\t\t\tSetCurrentBackbufferHasContents(false);", Requested), std::string::npos);
	EXPECT_LT(Requested, Return);
}

TEST(MetalBackendContract, DrawableUnavailableCannotReportPresentSuccess)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	const size_t Commit = Source.find("bool CommitCurrentFrame(bool Present, bool WaitForCompletion)");
	const size_t Finalize = Source.find("const bool FrameFinalized", Commit);
	const size_t Return = Source.find("return PresentationSucceeded && m_CurrentCommandBuffer.status == MTLCommandBufferStatusCompleted;", Commit);
	ASSERT_NE(Commit, std::string::npos);
	ASSERT_NE(Finalize, std::string::npos);
	ASSERT_NE(Return, std::string::npos);
	EXPECT_NE(Source.find("const bool PresentationSucceeded = !PresentationRequested || BackbufferPresented;", Finalize), std::string::npos);
	EXPECT_LT(Finalize, Return);
}

TEST(MetalBackendContract, SwapPropagatesDrawablePresentationFailure)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	const size_t Swap = Source.find("ERunCommandReturnTypes Cmd_Swap");
	ASSERT_NE(Swap, std::string::npos);
	EXPECT_NE(Source.find("if(!CommitCurrentFrame(true, false))", Swap), std::string::npos);
	EXPECT_NE(Source.find("SetSwapError();", Swap), std::string::npos);
	EXPECT_NE(Source.find("return RUN_COMMAND_COMMAND_ERROR;", Swap), std::string::npos);
	EXPECT_NE(Source.find("GFX_ERROR_TYPE_SWAP_FAILED", Source.find("void SetSwapError")), std::string::npos);
}

TEST(MetalBackendContract, CommandBufferSlicesPreserveBackbufferUntilPresent)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	const size_t Continue = Source.find("const bool ContinueBackbufferFrame");
	const size_t BeginFrame = Source.find("if(!m_FrameState.BeginFrame(Slot))", Continue);
	ASSERT_NE(Continue, std::string::npos);
	ASSERT_NE(BeginFrame, std::string::npos);
	EXPECT_NE(Source.find("m_CommandBufferCommitted && m_BackbufferContinuationPending && CurrentBackbufferHasContents()", Continue), std::string::npos);
	EXPECT_NE(Source.find("if(!ContinueBackbufferFrame)\n\t\t\tReleaseMetalObject(m_CurrentDrawable);", BeginFrame), std::string::npos);
	EXPECT_NE(Source.find("if(!ContinueBackbufferFrame)\n\t\t{\n\t\t\tm_CurrentDrawable = nil;", BeginFrame), std::string::npos);
	EXPECT_NE(Source.find("if(Present || !CurrentBackbufferHasContents())", Source.find("bool CommitCurrentFrame")), std::string::npos);
	EXPECT_NE(Source.find("id<MTLTexture> m_BackbufferTexture = nil;"), std::string::npos);
	EXPECT_NE(Source.find("const size_t Slot = ContinueBackbufferFrame ? m_CurrentFrameSlot"), std::string::npos);
}

TEST(MetalBackendContract, FailedPresentDoesNotForceContinuationWait)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	const size_t Commit = Source.find("bool CommitCurrentFrame(bool Present, bool WaitForCompletion)");
	const size_t Start = Source.find("const bool ContinueBackbufferFrame", Commit);
	ASSERT_NE(Commit, std::string::npos);
	ASSERT_NE(Start, std::string::npos);
	EXPECT_NE(Source.find("m_BackbufferContinuationPending = CurrentBackbufferHasContents() && !Present;", Commit), std::string::npos);
	EXPECT_NE(Source.find("m_CommandBufferCommitted && m_BackbufferContinuationPending && CurrentBackbufferHasContents()", Start), std::string::npos);
}

TEST(MetalBackendContract, DrawableSkipDoesNotDropRenderTargetReadback)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	const size_t Skip = Source.find("bool SkipCurrentFrameCommand");
	const size_t SwitchEnd = Source.find("default:", Skip);
	ASSERT_NE(Skip, std::string::npos);
	ASSERT_NE(SwitchEnd, std::string::npos);
	const std::string SkipCases = Source.substr(Skip, SwitchEnd - Skip);
	EXPECT_EQ(SkipCases.find("CMD_RENDER_TARGET_READBACK"), std::string::npos);
	EXPECT_EQ(SkipCases.find("CMD_TRY_SWAP_AND_READ_PIXEL"), std::string::npos);
	EXPECT_NE(Source.find("bool Cmd_RenderTarget_Readback"), std::string::npos);
}

TEST(MetalBackendContract, GpuIdleInvalidatesRetainedDrawable)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	const size_t WaitForGpuIdle = Source.find("void WaitForGpuIdle()");
	const size_t Drain = Source.find("m_FrameState.DrainFrames();", WaitForGpuIdle);
	ASSERT_NE(WaitForGpuIdle, std::string::npos);
	ASSERT_NE(Drain, std::string::npos);
	const size_t Release = Source.find("ReleaseMetalObject(m_CurrentDrawable);", WaitForGpuIdle);
	const size_t ResetDrawable = Source.find("m_CurrentDrawable = nil;", WaitForGpuIdle);
	const size_t ResetContents = Source.find("Frame.m_BackbufferHasContents = false;", WaitForGpuIdle);
	ASSERT_NE(Release, std::string::npos);
	ASSERT_NE(ResetDrawable, std::string::npos);
	ASSERT_NE(ResetContents, std::string::npos);
	EXPECT_LT(Release, Drain);
	EXPECT_LT(ResetDrawable, Drain);
	EXPECT_LT(ResetContents, Drain);
}

TEST(MetalBackendContract, TextureArrayPathConvertsAtlasAndSamplesArrayLayers)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	EXPECT_NE(Source.find("m_TextureArray"), std::string::npos);
	EXPECT_NE(Source.find("MetalTextureArrayLayout"), std::string::npos);
	EXPECT_NE(Source.find("Texture2DTo3D"), std::string::npos);
	EXPECT_NE(Source.find("向上补齐而不是向下取幂"), std::string::npos);
	EXPECT_NE(Source.find("ArraySourceWidth = ((SourceWidth + 15) / 16) * 16"), std::string::npos);
	EXPECT_NE(Source.find("UploadTextureArray"), std::string::npos);
	EXPECT_NE(Source.find("CMD_RENDER_TEX3D"), std::string::npos);
	EXPECT_NE(Source.find("TextureArrayPipelineIndex"), std::string::npos);
	EXPECT_NE(Source.find("m_2DArrayTextures = true"), std::string::npos);
	EXPECT_EQ(Source.find("m_3DTextures = true"), std::string::npos);

	const std::string Shader = ReadTestSourceFile("data/shader/metal/qmclient.metal");
	EXPECT_NE(Shader.find("texture2d_array<float>"), std::string::npos);
	EXPECT_NE(Shader.find("qmclient_tex_array_vertex"), std::string::npos);
	EXPECT_NE(Shader.find("qmclient_tex_array_fragment"), std::string::npos);
}

TEST(MetalBackendContract, BufferedTileAndQuadCapabilitiesArePublished)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	EXPECT_NE(Source.find("m_pCapabilities->m_TileBuffering = true;"), std::string::npos);
	EXPECT_NE(Source.find("m_pCapabilities->m_QuadBuffering = true;"), std::string::npos);
	EXPECT_NE(Source.find("bool DrawTileLayer"), std::string::npos);
	EXPECT_NE(Source.find("bool DrawQuadLayer"), std::string::npos);
}

TEST(MetalBackendContract, RenderTargetsOwnAttachmentsAndRestoreDrawableRendering)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	EXPECT_NE(Source.find("struct SRenderTarget"), std::string::npos);
	EXPECT_NE(Source.find("m_vRenderTargets"), std::string::npos);
	EXPECT_NE(Source.find("pDescriptor.pixelFormat = MTLPixelFormatBGRA8Unorm"), std::string::npos);
	EXPECT_EQ(Source.find("pDescriptor.pixelFormat = MTLPixelFormatRGBA8Unorm"), std::string::npos);
	EXPECT_NE(Source.find("MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead"), std::string::npos);
	EXPECT_NE(Source.find("pLayer.framebufferOnly = NO"), std::string::npos);
	EXPECT_NE(Source.find("void DestroyAllRenderTargets()"), std::string::npos);
	EXPECT_NE(Source.find("bool Cmd_RenderTarget_Create"), std::string::npos);
	EXPECT_NE(Source.find("bool Cmd_RenderTarget_Destroy"), std::string::npos);
	EXPECT_NE(Source.find("bool Cmd_RenderTarget_Begin"), std::string::npos);
	EXPECT_NE(Source.find("bool Cmd_RenderTarget_End"), std::string::npos);
	EXPECT_NE(Source.find("bool Cmd_RenderTarget_Draw"), std::string::npos);
	EXPECT_NE(Source.find("bool Cmd_RenderTarget_CaptureBackbuffer"), std::string::npos);
	EXPECT_NE(Source.find("CMD_RENDER_TARGET_CREATE"), std::string::npos);
	EXPECT_NE(Source.find("CMD_RENDER_TARGET_DESTROY"), std::string::npos);
	EXPECT_NE(Source.find("CMD_RENDER_TARGET_BEGIN"), std::string::npos);
	EXPECT_NE(Source.find("CMD_RENDER_TARGET_END"), std::string::npos);
	EXPECT_NE(Source.find("CMD_RENDER_TARGET_DRAW"), std::string::npos);
	EXPECT_NE(Source.find("CMD_RENDER_TARGET_CAPTURE_BACKBUFFER"), std::string::npos);
	EXPECT_NE(Source.find("m_RenderTargetState"), std::string::npos);
	EXPECT_NE(Source.find("BeginRenderEncoderForTexture"), std::string::npos);
	EXPECT_NE(Source.find("const size_t UniformOffset = (VertexOffset + VertexBytes + 255) & ~size_t(255)"), std::string::npos);
	EXPECT_NE(Source.find("CurrentBackbufferHasContents() ? MTLLoadActionLoad : MTLLoadActionClear"), std::string::npos);
	EXPECT_NE(Source.find("[m_CurrentRenderEncoder setFragmentTexture:CurrentBackbufferTexture() atIndex:0]"), std::string::npos);
	EXPECT_NE(Source.find("EndActiveEncoders();\n\t\treturn true;\n\t}\n\n\tvoid EndActiveEncoders()"), std::string::npos);
	EXPECT_NE(Source.find("m_RenderTargets = true"), std::string::npos);
	EXPECT_NE(Source.find("m_RenderTargetGaussianBlur = true"), std::string::npos);
	EXPECT_NE(Source.find("m_BackbufferCapture = true"), std::string::npos);
	EXPECT_NE(Source.find("m_pRenderTargetSupportReason = \"supported\""), std::string::npos);
}

TEST(MetalBackendContract, RenderTargetReadbackCopiesBgraToRgbaBeforeSignaling)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	EXPECT_NE(Source.find("bool Cmd_RenderTarget_Readback"), std::string::npos);
	EXPECT_NE(Source.find("id<MTLBuffer> EncodeTextureReadback"), std::string::npos);
	EXPECT_NE(Source.find("bool CommitCurrentFrameForReadback"), std::string::npos);
	EXPECT_NE(Source.find("FinalizeFrameWithoutPresent"), std::string::npos);
	EXPECT_NE(Source.find("CopyBgraToRgba"), std::string::npos);
	EXPECT_NE(Source.find("id<MTLBuffer> PresentedReadback = nil;"), std::string::npos);
	EXPECT_NE(Source.find("const bool FrameCompleted = HasDrawable ? CommitCurrentFrame(true, true) : CommitCurrentFrameForReadback();"), std::string::npos);
	EXPECT_NE(Source.find("m_FrameState.MarkReadbackPresented();"), std::string::npos);
	EXPECT_NE(Source.find("m_FrameState.ConsumeReadbackPresented()"), std::string::npos);
	EXPECT_NE(Source.find("pDst[Pixel * 4 + 3] = pSrc[Pixel * 4 + 3];"), std::string::npos);
	EXPECT_NE(Source.find("CMD_RENDER_TARGET_READBACK"), std::string::npos);
}

TEST(MetalBackendContract, PresentedReadbackWaitsBeforeConsumingSharedBuffer)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	const size_t Helper = Source.find("bool WaitForPresentedReadback()");
	const size_t Screenshot = Source.find("ERunCommandReturnTypes Cmd_Screenshot");
	const size_t ReadPixel = Source.find("ERunCommandReturnTypes Cmd_ReadPixel");
	ASSERT_NE(Helper, std::string::npos);
	ASSERT_NE(Screenshot, std::string::npos);
	ASSERT_NE(ReadPixel, std::string::npos);
	EXPECT_NE(Source.find("waitUntilCompleted", Helper), std::string::npos);
	EXPECT_NE(Source.find("MTLCommandBufferStatusCompleted", Helper), std::string::npos);
	EXPECT_NE(Source.find("if(!WaitForPresentedReadback())", Screenshot), std::string::npos);
	EXPECT_NE(Source.find("if(!WaitForPresentedReadback()", ReadPixel), std::string::npos);
}

TEST(MetalBackendContract, NormalSwapCapturesReadbackOnlyWhileVideoRecording)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	const size_t Swap = Source.find("ERunCommandReturnTypes Cmd_Swap");
	const size_t MultiSampling = Source.find("bool Cmd_MultiSampling", Swap);
	ASSERT_NE(Swap, std::string::npos);
	ASSERT_NE(MultiSampling, std::string::npos);
	const std::string SwapSource = Source.substr(Swap, MultiSampling - Swap);
	EXPECT_NE(SwapSource.find("VideoCaptureActive()"), std::string::npos);
	EXPECT_NE(SwapSource.find("EncodeDrawableReadback"), std::string::npos);
	EXPECT_NE(SwapSource.find("CommitCurrentFrame(true, false)"), std::string::npos);
	EXPECT_EQ(Source.find("m_LastPresentedDrawable"), std::string::npos);
}

TEST(MetalBackendContract, TextureResourceFailuresArePropagated)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	const size_t TextureCreate = Source.find("case CCommandBuffer::CMD_TEXTURE_CREATE");
	const size_t TextureUpdate = Source.find("case CCommandBuffer::CMD_TEXTURE_UPDATE");
	const size_t TextCreate = Source.find("case CCommandBuffer::CMD_TEXT_TEXTURES_CREATE");
	const size_t TextUpdate = Source.find("case CCommandBuffer::CMD_TEXT_TEXTURE_UPDATE");
	ASSERT_NE(TextureCreate, std::string::npos);
	ASSERT_NE(TextureUpdate, std::string::npos);
	ASSERT_NE(TextCreate, std::string::npos);
	ASSERT_NE(TextUpdate, std::string::npos);
	EXPECT_NE(Source.find("SetResourceCommandError(pCommand)", TextureCreate), std::string::npos);
	EXPECT_NE(Source.find("SetResourceCommandError(pCommand)", TextureUpdate), std::string::npos);
	EXPECT_NE(Source.find("SetResourceCommandError(pCommand)", TextCreate), std::string::npos);
	EXPECT_NE(Source.find("SetResourceCommandError(pCommand)", TextUpdate), std::string::npos);
}

TEST(MetalBackendContract, MultisampleBackbufferUsesResolveAttachmentAndRealDeviceSupport)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	EXPECT_NE(Source.find("bool Cmd_MultiSampling"), std::string::npos);
	EXPECT_NE(Source.find("CMD_MULTISAMPLING"), std::string::npos);
	EXPECT_NE(Source.find("[m_Device supportsTextureSampleCount:2]"), std::string::npos);
	EXPECT_NE(Source.find("MTLTextureType2DMultisample"), std::string::npos);
	EXPECT_NE(Source.find("pDescriptor.sampleCount = m_MultiSamplingCount"), std::string::npos);
	EXPECT_NE(Source.find("MTLStoreActionStoreAndMultisampleResolve"), std::string::npos);
	EXPECT_NE(Source.find("pPass.colorAttachments[0].resolveTexture = ResolveTexture"), std::string::npos);
	EXPECT_NE(Source.find("CreatePipelineStates(m_MultiSamplingCount, m_aMultiSamplePipelineStates)"), std::string::npos);
	EXPECT_NE(Source.find("const size_t UniformOffset = (VertexOffset + Bytes + 255) & ~size_t(255)"), std::string::npos);
	EXPECT_NE(Source.find("m_CurrentDrawable != nil"), std::string::npos);
	EXPECT_NE(Source.find("m_RenderEncoderStarted || m_CurrentRenderEncoder != nil || m_CurrentBlitEncoder != nil"), std::string::npos);
	EXPECT_NE(Source.find("SetCurrentBackbufferHasContents(false)"), std::string::npos);
	EXPECT_NE(Source.find("Requested %u FSAA samples, using %u."), std::string::npos);
}

TEST(MetalBackendContract, GaussianBlurUsesSingleSamplePingPongPasses)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	EXPECT_NE(Source.find("bool CreateGaussianBlurPipeline()"), std::string::npos);
	EXPECT_NE(Source.find("qmclient_gaussian_blur_fragment"), std::string::npos);
	EXPECT_NE(Source.find("pPipelineDescriptor.rasterSampleCount = 1"), std::string::npos);
	EXPECT_NE(Source.find("bool Cmd_RenderTarget_GaussianBlurPass"), std::string::npos);
	EXPECT_NE(Source.find("pCommand->m_SourceTargetId == DestinationTargetId"), std::string::npos);
	EXPECT_NE(Source.find("Source.m_Width != Destination.m_Width"), std::string::npos);
	EXPECT_NE(Source.find("EndActiveEncoders();\n\t\tif(!BeginRenderEncoder"), std::string::npos);
	EXPECT_NE(Source.find("setFragmentBuffer:Frame.m_VertexBuffer offset:FragmentUniformOffset atIndex:1"), std::string::npos);
	EXPECT_NE(Source.find("CMD_RENDER_TARGET_GAUSSIAN_BLUR_PASS"), std::string::npos);
	EXPECT_NE(Source.find("m_RenderTargetGaussianBlur = true"), std::string::npos);

	const std::string Shader = ReadTestSourceFile("data/shader/metal/qmclient.metal");
	EXPECT_NE(Shader.find("float GaussianBlurWeight"), std::string::npos);
	EXPECT_NE(Shader.find("fragment float4 qmclient_gaussian_blur_fragment"), std::string::npos);
	EXPECT_NE(Shader.find("if(Offset > Radius)"), std::string::npos);
	EXPECT_NE(Shader.find("Texture.sample(Sampler, Input.m_TexCoord + SampleOffset)"), std::string::npos);
}

TEST(MetalBackendContract, QmSdfCommandsUseMatchedPipelinesAndLayout)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	EXPECT_NE(Source.find("CreateSdfPipelineStates"), std::string::npos);
	EXPECT_NE(Source.find("m_aMediaIslandSdfPipelines"), std::string::npos);
	EXPECT_NE(Source.find("m_aRoundedRectSdfPipelines"), std::string::npos);
	EXPECT_NE(Source.find("m_aMultiSampleMediaIslandSdfPipelines"), std::string::npos);
	EXPECT_NE(Source.find("CMD_RENDER_MEDIA_ISLAND_SDF"), std::string::npos);
	EXPECT_NE(Source.find("CMD_RENDER_ROUNDED_RECT_SDF"), std::string::npos);
	EXPECT_NE(Source.find("setFragmentBuffer:Frame.m_VertexBuffer offset:ParamsOffset atIndex:1"), std::string::npos);
	EXPECT_NE(Source.find("m_MediaIslandSdf = SdfPipelinesAvailable"), std::string::npos);
	EXPECT_NE(Source.find("m_RoundedRectSdf = SdfPipelinesAvailable"), std::string::npos);

	const std::string Shader = ReadTestSourceFile("data/shader/metal/qmclient.metal");
	EXPECT_NE(Shader.find("qmclient_media_island_sdf_fragment"), std::string::npos);
	EXPECT_NE(Shader.find("qmclient_rounded_rect_sdf_fragment"), std::string::npos);
	EXPECT_NE(Shader.find("constant float4 *Data [[buffer(1)]]"), std::string::npos);
	EXPECT_NE(Shader.find("MEDIA_ISLAND_ITEM_STRIDE = 3"), std::string::npos);
	EXPECT_NE(Shader.find("MEDIA_ISLAND_BACKDROP_UV = 44"), std::string::npos);
}

TEST(MetalBackendContract, TexturedMsdfUsesDedicatedPipelinesAndAtomicCapability)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	EXPECT_NE(Source.find("CreateTexturedMsdfPipelineStates"), std::string::npos);
	EXPECT_NE(Source.find("m_aTexturedMsdfPipelines"), std::string::npos);
	EXPECT_NE(Source.find("m_aMultiSampleTexturedMsdfPipelines"), std::string::npos);
	EXPECT_NE(Source.find("CMD_RENDER_TEXTURED_MSDF"), std::string::npos);
	EXPECT_NE(Source.find("bool DrawTexturedMsdf"), std::string::npos);
	EXPECT_NE(Source.find("setFragmentTexture:Texture.m_Texture atIndex:0"), std::string::npos);
	EXPECT_NE(Source.find("setFragmentBuffer:Frame.m_VertexBuffer offset:ParamsOffset atIndex:1"), std::string::npos);
	EXPECT_NE(Source.find("m_TexturedMsdf.store(TexturedMsdfPipelinesAvailable, std::memory_order_release)"), std::string::npos);
	EXPECT_NE(Source.find("m_TexturedMsdf.store(false, std::memory_order_release)"), std::string::npos);
	EXPECT_NE(Source.find("CreateTexturedMsdfPipelineStates(SupportedCount, m_aMultiSampleTexturedMsdfPipelines)"), std::string::npos);

	const std::string Shader = ReadTestSourceFile("data/shader/metal/qmclient.metal");
	EXPECT_NE(Shader.find("qmclient_textured_msdf_fragment"), std::string::npos);
	EXPECT_NE(Shader.find("QmClientMedian"), std::string::npos);
	EXPECT_NE(Shader.find("fwidth(Input.m_TexCoord)"), std::string::npos);
	EXPECT_NE(Shader.find("Input.m_Color.a * Opacity"), std::string::npos);
}

TEST(MetalBackendContract, ShaderManifestCoversAllBackendFamiliesAndMetalEntrypoints)
{
	const std::string MetalShader = ReadTestSourceFile("data/shader/metal/qmclient.metal");
	ASSERT_FALSE(MetalShader.empty());
	for(const char *pEntrypoint : g_aMetalShaderEntrypoints)
		EXPECT_NE(MetalShader.find(pEntrypoint), std::string::npos) << pEntrypoint;

	for(const SMetalShaderFamilyManifest &Family : g_aMetalShaderFamilies)
	{
		const std::string OpenGLVertex = ReadTestSourceFile((std::string("data/shader/") + Family.m_pOpenGLName + ".vert").c_str());
		const std::string OpenGLFragment = ReadTestSourceFile((std::string("data/shader/") + Family.m_pOpenGLName + ".frag").c_str());
		const std::string VulkanVertex = ReadTestSourceFile((std::string("data/shader/vulkan/") + Family.m_pVulkanName + ".vert").c_str());
		const std::string VulkanFragment = ReadTestSourceFile((std::string("data/shader/vulkan/") + Family.m_pVulkanName + ".frag").c_str());
		EXPECT_FALSE(OpenGLVertex.empty()) << Family.m_pOpenGLName;
		EXPECT_FALSE(OpenGLFragment.empty()) << Family.m_pOpenGLName;
		EXPECT_FALSE(VulkanVertex.empty()) << Family.m_pVulkanName;
		EXPECT_FALSE(VulkanFragment.empty()) << Family.m_pVulkanName;
	}
}

TEST(MetalBackendContract, CleanupWaitsBeforeReleasingCommandBuffers)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	const size_t Wait = Source.find("void WaitForGpuIdle()");
	const size_t Release = Source.find("void ReleaseGpuObjects()");
	const size_t ErroneousCleanup = Source.find("void ErroneousCleanup() override");
	const size_t Destructor = Source.find("~CCommandProcessorFragment_Metal() override");
	ASSERT_NE(Wait, std::string::npos);
	ASSERT_NE(Release, std::string::npos);
	ASSERT_NE(ErroneousCleanup, std::string::npos);
	ASSERT_NE(Destructor, std::string::npos);
	const size_t ErroneousWait = Source.find("WaitForGpuIdle();", ErroneousCleanup);
	const size_t ErroneousContainerRelease = Source.find("DestroyAllBufferContainers();", ErroneousWait);
	const size_t ErroneousBufferRelease = Source.find("DestroyAllBuffers();", ErroneousContainerRelease);
	const size_t ErroneousGpuRelease = Source.find("ReleaseGpuObjects();", ErroneousBufferRelease);
	const size_t DestructorWait = Source.find("WaitForGpuIdle();", Destructor);
	const size_t DestructorContainerRelease = Source.find("DestroyAllBufferContainers();", DestructorWait);
	const size_t DestructorBufferRelease = Source.find("DestroyAllBuffers();", DestructorContainerRelease);
	const size_t DestructorGpuRelease = Source.find("ReleaseGpuObjects();", DestructorBufferRelease);
	EXPECT_NE(ErroneousWait, std::string::npos);
	EXPECT_NE(ErroneousContainerRelease, std::string::npos);
	EXPECT_NE(ErroneousBufferRelease, std::string::npos);
	EXPECT_NE(ErroneousGpuRelease, std::string::npos);
	EXPECT_NE(DestructorWait, std::string::npos);
	EXPECT_NE(DestructorContainerRelease, std::string::npos);
	EXPECT_NE(DestructorBufferRelease, std::string::npos);
	EXPECT_NE(DestructorGpuRelease, std::string::npos);
	EXPECT_LT(ErroneousWait, ErroneousContainerRelease);
	EXPECT_LT(ErroneousContainerRelease, ErroneousBufferRelease);
	EXPECT_LT(ErroneousBufferRelease, ErroneousGpuRelease);
	EXPECT_LT(DestructorWait, DestructorContainerRelease);
	EXPECT_LT(DestructorContainerRelease, DestructorBufferRelease);
	EXPECT_LT(DestructorBufferRelease, DestructorGpuRelease);
	EXPECT_LT(Release, Wait);
	EXPECT_LT(Wait, ErroneousCleanup);
}

TEST(MetalBackendContract, FrameSlotsKeepVertexBuffersAndOwnCrossCommandEncoders)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	const size_t ReleaseFrameSlot = Source.find("void ReleaseFrameSlotResources(size_t Slot)");
	const size_t NextMethod = Source.find("\n\tvoid ", ReleaseFrameSlot + 1);
	ASSERT_NE(ReleaseFrameSlot, std::string::npos);
	ASSERT_NE(NextMethod, std::string::npos);
	const std::string FrameSlotRelease = Source.substr(ReleaseFrameSlot, NextMethod - ReleaseFrameSlot);
	EXPECT_NE(FrameSlotRelease.find("ReleaseMetalObject(Frame.m_CommandBuffer);"), std::string::npos);
	EXPECT_NE(FrameSlotRelease.find("Frame.m_CommandBuffer = nil;"), std::string::npos);
	EXPECT_EQ(FrameSlotRelease.find("ReleaseMetalObject(Frame.m_VertexBuffer);"), std::string::npos);

	EXPECT_NE(Source.find("RetainMetalObject([m_CurrentCommandBuffer renderCommandEncoderWithDescriptor:pPass])"), std::string::npos);
	EXPECT_NE(Source.find("RetainMetalObject([m_CurrentCommandBuffer blitCommandEncoder])"), std::string::npos);
	EXPECT_NE(Source.find("RetainMetalObject([pLayer nextDrawable])"), std::string::npos);
	EXPECT_NE(Source.find("ReleaseMetalObject(m_CurrentRenderEncoder);"), std::string::npos);
	EXPECT_NE(Source.find("ReleaseMetalObject(m_CurrentBlitEncoder);"), std::string::npos);
	EXPECT_NE(Source.find("ReleaseMetalObject(m_CurrentDrawable);"), std::string::npos);
}

TEST(MetalBackendContract, StaticEqualSizeBufferRecreationUsesSafeInPlaceUpdate)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	const size_t Helper = Source.find("bool RecreateBuffer(int Slot, size_t DataBytes, const void *pData, int Flags)");
	ASSERT_NE(Helper, std::string::npos);
	EXPECT_NE(Source.find("!OneTimeUse && !Existing.m_OneTimeUse && Existing.m_DataBytes == DataBytes", Helper), std::string::npos);
	EXPECT_NE(Source.find("UpdateBuffer(Slot, 0, DataBytes, pData)", Helper), std::string::npos);
	EXPECT_NE(Source.find("DestroyBuffer(Slot);\n\t\treturn CreateBuffer(Slot, DataBytes, pData, Flags);", Helper), std::string::npos);

	const size_t RecreateCommand = Source.find("case CCommandBuffer::CMD_RECREATE_BUFFER_OBJECT:");
	ASSERT_NE(RecreateCommand, std::string::npos);
	EXPECT_NE(Source.find("RecreateBuffer(pCommand->m_BufferIndex", RecreateCommand), std::string::npos);
}

TEST(MetalBackendContract, BufferReuseAppearsInMetalPerformanceSample)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	EXPECT_NE(Source.find("m_MetalPerfBufferReuseCount"), std::string::npos);
	EXPECT_NE(Source.find("m_MetalPerfBufferReuseBytes"), std::string::npos);
	EXPECT_NE(Source.find("buffer_reuse_count=%llu buffer_reuse_bytes=%llu"), std::string::npos);
}

TEST(MetalBackendContract, GpuExecutionTimingAppearsInMetalPerformanceSample)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	EXPECT_NE(Source.find("Buffer.GPUStartTime"), std::string::npos);
	EXPECT_NE(Source.find("Buffer.GPUEndTime"), std::string::npos);
	EXPECT_NE(Source.find("const bool RecordGpuTiming = m_MetalPerfEnabled;"), std::string::npos);
	EXPECT_NE(Source.find("if(RecordGpuTiming)\n\t\t\t\tRecordMetalGpuExecutionTiming(Buffer, GpuTimingGeneration);"), std::string::npos);
	EXPECT_NE(Source.find("m_MetalPerfGeneration.load(std::memory_order_acquire) != Generation"), std::string::npos);
	EXPECT_NE(Source.find("gpu_execution_count=%llu gpu_execution_ms_sum=%.3f gpu_execution_ms_max=%.3f gpu_execution_unavailable_count=%llu"), std::string::npos);
}

TEST(MetalBackendContract, OneTimeBuffersAreRetiredPerFrameSlotBeforeReuse)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	EXPECT_NE(Source.find("m_aTransientBuffers"), std::string::npos);
	EXPECT_NE(Source.find("m_aRetiredTransientBuffers"), std::string::npos);
	EXPECT_NE(Source.find("m_aLastUsedFrameIds"), std::string::npos);
	EXPECT_NE(Source.find("m_aRetiredTransientBuffers[m_CurrentFrameSlot]"), std::string::npos);
	EXPECT_NE(Source.find("PreferredBufferSlot(Buffer)"), std::string::npos);
	EXPECT_NE(Source.find("PreferredFrameId"), std::string::npos);
	EXPECT_NE(Source.find("RecycleTransientBuffer(Slot, Buffer)"), std::string::npos);
	EXPECT_NE(Source.find("m_aTransientBuffers[m_CurrentFrameSlot]"), std::string::npos);
	EXPECT_NE(Source.find("transient_pool_reuse_count=%llu transient_pool_reuse_bytes=%llu"), std::string::npos);
}

TEST(MetalBackendContract, PersistentBuffersAreNotMutatedOrReleasedWhileInFlight)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	const size_t Destroy = Source.find("void DestroyBuffer(int Slot)");
	const size_t Update = Source.find("bool UpdateBuffer(int Slot, size_t Offset, size_t DataBytes, const void *pData)");
	const size_t Recreate = Source.find("bool RecreateBuffer(int Slot, size_t DataBytes, const void *pData, int Flags)");
	const size_t Prepare = Source.find("bool PrepareContainerPipeline(const CCommandBuffer::SState &State");
	ASSERT_NE(Destroy, std::string::npos);
	ASSERT_NE(Update, std::string::npos);
	ASSERT_NE(Recreate, std::string::npos);
	ASSERT_NE(Prepare, std::string::npos);
	EXPECT_NE(Source.find("WaitForBufferIdle(Buffer)", Destroy), std::string::npos);
	EXPECT_NE(Source.find("WaitForGpuIdle()", Destroy), std::string::npos);
	const size_t Deferred = Source.find("if(DeferredRelease)", Destroy);
	const size_t DeferredElse = Source.find("else if(Buffer.m_Buffer != nil && Buffer.m_OneTimeUse)", Deferred);
	const size_t DeferredReleaseCall = Source.find("ReleaseMetalObject(Buffer.m_Buffer);", Deferred);
	ASSERT_NE(Deferred, std::string::npos);
	ASSERT_NE(DeferredElse, std::string::npos);
	ASSERT_NE(DeferredReleaseCall, std::string::npos);
	const std::string DeferredBody = Source.substr(Deferred, DeferredElse - Deferred);
	EXPECT_EQ(DeferredBody.find("SubBufferMemory(Buffer.m_DataBytes)"), std::string::npos);
	EXPECT_EQ(DeferredBody.find("ReleaseMetalObject(Buffer.m_Buffer)"), std::string::npos);
	EXPECT_LT(Deferred, DeferredElse);
	EXPECT_LT(DeferredElse, DeferredReleaseCall);
	EXPECT_NE(Source.find("if(Buffer.m_OneTimeUse)", Destroy), std::string::npos);
	EXPECT_NE(Source.find("m_aLastUsedFrameIds[m_CurrentFrameSlot]", Prepare), std::string::npos);
	EXPECT_NE(Source.find("BufferInFlightMask(Buffer, false)"), std::string::npos);
	EXPECT_NE(Source.find("for(size_t Slot = 0; Slot < gs_FrameSlotCount; ++Slot)"), std::string::npos);
	EXPECT_NE(Source.find("WaitForBufferIdle(Buffer)", Update), std::string::npos);
	EXPECT_NE(Source.find("!IsBufferInFlight(Existing)", Recreate), std::string::npos);
	EXPECT_NE(Source.find("m_aLastUsedFrameIds[m_CurrentFrameSlot]", Prepare), std::string::npos);
}

TEST(MetalBackendContract, DrawableAcquisitionUsesTimeoutAndCompletedSlotsDoNotWait)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/metal/backend_metal.mm");
	EXPECT_NE(Source.find("pLayer.allowsNextDrawableTimeout = !m_VSync;"), std::string::npos);
	EXPECT_NE(Source.find("m_CurrentDrawable = RetainMetalObject([pLayer nextDrawable]);"), std::string::npos);
	EXPECT_NE(Source.find("copyFromTexture:CurrentBackbufferTexture() sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)"), std::string::npos);
	EXPECT_NE(Source.find("m_SkipCurrentFrame = true;"), std::string::npos);
	EXPECT_NE(Source.find("bool SkipCurrentFrameCommand(int Command) const"), std::string::npos);
	EXPECT_NE(Source.find("m_FrameState.FinalizeFrameWithoutPresent()"), std::string::npos);
	const size_t SlotWait = Source.find("const MTLCommandBufferStatus Status = Frame.m_CommandBuffer.status;");
	ASSERT_NE(SlotWait, std::string::npos);
	const size_t WaitCall = Source.find("[Frame.m_CommandBuffer waitUntilCompleted];", SlotWait);
	ASSERT_NE(WaitCall, std::string::npos);
	EXPECT_LT(SlotWait, WaitCall);
	EXPECT_NE(Source.find("Status != MTLCommandBufferStatusCompleted && Status != MTLCommandBufferStatusError"), std::string::npos);
}
