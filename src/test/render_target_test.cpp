// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <engine/client/backend_sdl.h>
#include <engine/client/graphics_threaded.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <cmath>
#include <limits>
#include <type_traits>

namespace
{
	using TBeginRenderTargetReadback = IGraphics::CRenderTargetReadbackHandle (IGraphics::*)(IGraphics::CRenderTargetHandle);
	using TPollRenderTargetReadback = IGraphics::ERenderTargetReadbackState (IGraphics::*)(IGraphics::CRenderTargetReadbackHandle);
	using TResolveRenderTargetReadback = bool (IGraphics::*)(IGraphics::CRenderTargetReadbackHandle *, CImageInfo &);
	using TCancelRenderTargetReadback = void (IGraphics::*)(IGraphics::CRenderTargetReadbackHandle *);
	using TGaussianBlurRenderTarget = bool (IGraphics::*)(IGraphics::CRenderTargetHandle, IGraphics::CRenderTargetHandle, IGraphics::CRenderTargetHandle, const IGraphics::SGaussianBlurParams &);
	using TDualBlurRenderTarget = bool (IGraphics::*)(IGraphics::CRenderTargetHandle, IGraphics::CRenderTargetHandle, IGraphics::CRenderTargetHandle, IGraphics::CRenderTargetHandle, IGraphics::CRenderTargetHandle, const IGraphics::SGaussianBlurParams &);
	using TCaptureBackbufferToRenderTarget = bool (IGraphics::*)(IGraphics::CRenderTargetHandle);
	using TDrawRenderTarget = void (IGraphics::*)(IGraphics::CRenderTargetHandle, const IGraphics::SRenderTargetDrawParams &);

	std::string ReadFile(const char *pPath)
	{
		return ReadTestSourceFile(pPath);
	}

	std::string ExtractFunctionBody(const std::string &Source, const char *pSignature)
	{
		const size_t SignaturePos = Source.find(pSignature);
		EXPECT_NE(SignaturePos, std::string::npos) << pSignature;
		if(SignaturePos == std::string::npos)
			return {};

		const size_t BodyStart = Source.find('{', SignaturePos);
		EXPECT_NE(BodyStart, std::string::npos) << pSignature;
		if(BodyStart == std::string::npos)
			return {};

		int Depth = 1;
		size_t Pos = BodyStart + 1;
		for(; Pos < Source.size() && Depth > 0; ++Pos)
		{
			if(Source[Pos] == '{')
				++Depth;
			else if(Source[Pos] == '}')
				--Depth;
		}

		EXPECT_EQ(Depth, 0) << pSignature;
		if(Depth != 0 || Pos <= BodyStart + 1)
			return {};
		return Source.substr(BodyStart + 1, Pos - BodyStart - 2);
	}
} // namespace

static_assert(std::is_same_v<decltype(&IGraphics::BeginRenderTargetReadback), TBeginRenderTargetReadback>);
static_assert(std::is_same_v<decltype(&IGraphics::PollRenderTargetReadback), TPollRenderTargetReadback>);
static_assert(std::is_same_v<decltype(&IGraphics::ResolveRenderTargetReadback), TResolveRenderTargetReadback>);
static_assert(std::is_same_v<decltype(&IGraphics::CancelRenderTargetReadback), TCancelRenderTargetReadback>);
static_assert(std::is_same_v<decltype(&IGraphics::GaussianBlurRenderTarget), TGaussianBlurRenderTarget>);
static_assert(std::is_same_v<decltype(&IGraphics::DualBlurRenderTarget), TDualBlurRenderTarget>);
static_assert(std::is_same_v<decltype(&IGraphics::CaptureBackbufferToRenderTarget), TCaptureBackbufferToRenderTarget>);
static_assert(std::is_same_v<decltype(&IGraphics::DrawRenderTarget), TDrawRenderTarget>);

TEST(GraphicsRenderTargetGaussianBlur, KernelIsNormalizedAndMonotonic)
{
	IGraphics::SGaussianBlurParams Params;
	Params.m_Radius = 4;
	Params.m_Sigma = 2.0f;
	std::array<float, IGraphics::GAUSSIAN_BLUR_MAX_RADIUS + 1> aWeights{};
	ASSERT_TRUE(IGraphics::CalculateGaussianBlurKernel(Params, aWeights));

	float Sum = aWeights[0];
	for(int Offset = 1; Offset <= Params.m_Radius; ++Offset)
	{
		EXPECT_GT(aWeights[Offset], 0.0f);
		EXPECT_LT(aWeights[Offset], aWeights[Offset - 1]);
		Sum += 2.0f * aWeights[Offset];
	}
	EXPECT_NEAR(Sum, 1.0f, 0.00001f);
	EXPECT_NEAR(aWeights[1] / aWeights[0], std::exp(-1.0f / (2.0f * Params.m_Sigma * Params.m_Sigma)), 0.00001f);
	for(int Offset = Params.m_Radius + 1; Offset <= IGraphics::GAUSSIAN_BLUR_MAX_RADIUS; ++Offset)
		EXPECT_FLOAT_EQ(aWeights[Offset], 0.0f);
}

TEST(GraphicsRenderTargetGaussianBlur, KernelRejectsInvalidParameters)
{
	std::array<float, IGraphics::GAUSSIAN_BLUR_MAX_RADIUS + 1> aWeights{};
	IGraphics::SGaussianBlurParams Params;

	Params.m_Radius = 0;
	EXPECT_FALSE(IGraphics::CalculateGaussianBlurKernel(Params, aWeights));
	Params.m_Radius = IGraphics::GAUSSIAN_BLUR_MAX_RADIUS + 1;
	EXPECT_FALSE(IGraphics::CalculateGaussianBlurKernel(Params, aWeights));
	Params.m_Radius = 4;
	Params.m_Sigma = 0.0f;
	EXPECT_FALSE(IGraphics::CalculateGaussianBlurKernel(Params, aWeights));
	Params.m_Sigma = std::numeric_limits<float>::infinity();
	EXPECT_FALSE(IGraphics::CalculateGaussianBlurKernel(Params, aWeights));
	Params.m_Sigma = std::numeric_limits<float>::quiet_NaN();
	EXPECT_FALSE(IGraphics::CalculateGaussianBlurKernel(Params, aWeights));
}

TEST(GraphicsRenderTargetGaussianBlur, PassCommandCarriesKernelAndDirection)
{
	CCommandBuffer::SCommand_RenderTarget_GaussianBlurPass Pass;
	Pass.m_SourceTargetId = 3;
	Pass.m_Radius = 4;
	Pass.m_Horizontal = true;
	Pass.m_aWeights[0] = 0.25f;
	EXPECT_EQ(Pass.m_Cmd, CCommandBuffer::CMD_RENDER_TARGET_GAUSSIAN_BLUR_PASS);
	EXPECT_EQ(Pass.m_SourceTargetId, 3);
	EXPECT_EQ(Pass.m_Radius, 4);
	EXPECT_TRUE(Pass.m_Horizontal);
	EXPECT_FLOAT_EQ(Pass.m_aWeights[0], 0.25f);
}

TEST(GraphicsRenderTarget, CommandStructsExposeExpectedFields)
{
	CCommandBuffer::SCommand_RenderTarget_Create Create;
	Create.m_TargetId = 7;
	Create.m_Width = 320;
	Create.m_Height = 180;
	EXPECT_EQ(Create.m_Cmd, CCommandBuffer::CMD_RENDER_TARGET_CREATE);
	EXPECT_EQ(Create.m_TargetId, 7);
	EXPECT_EQ(Create.m_Width, 320);
	EXPECT_EQ(Create.m_Height, 180);

	CCommandBuffer::SCommand_RenderTarget_Draw Draw;
	Draw.m_TargetId = 4;
	Draw.m_X = 1.0f;
	Draw.m_Y = 2.0f;
	Draw.m_W = 3.0f;
	Draw.m_H = 4.0f;
	Draw.m_Alpha = 0.25f;
	Draw.m_PrimCount = 2;
	EXPECT_EQ(Draw.m_Cmd, CCommandBuffer::CMD_RENDER_TARGET_DRAW);
	EXPECT_EQ(Draw.m_TargetId, 4);
	EXPECT_FLOAT_EQ(Draw.m_X, 1.0f);
	EXPECT_FLOAT_EQ(Draw.m_Y, 2.0f);
	EXPECT_FLOAT_EQ(Draw.m_W, 3.0f);
	EXPECT_FLOAT_EQ(Draw.m_H, 4.0f);
	EXPECT_FLOAT_EQ(Draw.m_Alpha, 0.25f);
	EXPECT_EQ(Draw.m_PrimCount, 2U);
}

TEST(GraphicsRenderTarget, DrawAlphaIsClampedAndForwardedToBackends)
{
	const std::string FrontendSource = ReadFile("src/engine/client/graphics_threaded.cpp");
	const std::string FrontendBody = ExtractFunctionBody(FrontendSource, "void CGraphics_Threaded::DrawRenderTarget");
	ASSERT_FALSE(FrontendBody.empty());
	EXPECT_NE(FrontendBody.find("std::clamp(Params.m_Alpha, 0.0f, 1.0f)"), std::string::npos);
	EXPECT_NE(FrontendBody.find("Cmd.m_Alpha"), std::string::npos);
	EXPECT_NE(FrontendBody.find("Params.m_Corners"), std::string::npos);
	EXPECT_NE(FrontendBody.find("Params.m_Rounding"), std::string::npos);
	EXPECT_NE(FrontendBody.find("Cmd.m_pVertices"), std::string::npos);

	const std::string OpenGlBody = ExtractFunctionBody(ReadFile("src/engine/client/backend/opengl/backend_opengl.cpp"), "void CCommandProcessorFragment_OpenGL::Cmd_RenderTarget_Draw");
	const std::string OpenGl3Body = ExtractFunctionBody(ReadFile("src/engine/client/backend/opengl/backend_opengl3.cpp"), "void CCommandProcessorFragment_OpenGL3_3::Cmd_RenderTarget_Draw");
	const std::string VulkanBody = ExtractFunctionBody(ReadFile("src/engine/client/backend/vulkan/backend_vulkan.cpp"), "[[nodiscard]] bool Cmd_RenderTarget_Draw");
	ASSERT_FALSE(OpenGlBody.empty());
	ASSERT_FALSE(OpenGl3Body.empty());
	ASSERT_FALSE(VulkanBody.empty());
	EXPECT_NE(FrontendBody.find("Cmd.m_Alpha * 255.0f + 0.5f"), std::string::npos);
	EXPECT_NE(OpenGlBody.find("pCommand->m_pVertices"), std::string::npos);
	EXPECT_NE(OpenGl3Body.find("pCommand->m_PrimCount"), std::string::npos);
	EXPECT_NE(VulkanBody.find("pCommand->m_PrimCount"), std::string::npos);
}

TEST(GraphicsRenderTarget, RoundedDrawUsesQuadVertexOrderAndRequeuesVertexData)
{
	const std::string FrontendBody = ExtractFunctionBody(ReadFile("src/engine/client/graphics_threaded.cpp"), "void CGraphics_Threaded::DrawRenderTarget");
	ASSERT_FALSE(FrontendBody.empty());

	const size_t PlainRectBranch = FrontendBody.find("Params.m_Corners == CORNER_NONE || Rounding <= 0.0f");
	const size_t RoundedRectBranch = FrontendBody.find("constexpr int NumSegments = RECT_CORNER_SEGMENTS");
	ASSERT_NE(PlainRectBranch, std::string::npos);
	ASSERT_NE(RoundedRectBranch, std::string::npos);
	EXPECT_LT(PlainRectBranch, RoundedRectBranch);

	const std::string QuadVertexOrder =
		"vec2(Params.m_X + Rounding, Params.m_Y + Rounding),\n"
		"\t\t\t\t\tvec2(Params.m_X + (1.0f - Ca1) * Rounding, Params.m_Y + (1.0f - Sa1) * Rounding),\n"
		"\t\t\t\t\tvec2(Params.m_X + (1.0f - Ca2) * Rounding, Params.m_Y + (1.0f - Sa2) * Rounding),\n"
		"\t\t\t\t\tvec2(Params.m_X + (1.0f - Ca3) * Rounding, Params.m_Y + (1.0f - Sa3) * Rounding)";
	EXPECT_NE(FrontendBody.find(QuadVertexOrder), std::string::npos);
	EXPECT_NE(FrontendBody.find("Cmd.m_PrimCount = vVertices.size() / 4;"), std::string::npos);
	EXPECT_NE(FrontendBody.find("const size_t VerticesSize = vVertices.size() * sizeof(CCommandBuffer::SVertex);"), std::string::npos);

	const size_t AddCommand = FrontendBody.find("AddCmd(Cmd, [&]");
	ASSERT_NE(AddCommand, std::string::npos);
	EXPECT_NE(FrontendBody.find("m_pCommandBuffer->AllocData(VerticesSize)", AddCommand), std::string::npos);
	EXPECT_NE(FrontendBody.find("mem_copy(Cmd.m_pVertices, vVertices.data(), VerticesSize);", AddCommand), std::string::npos);
}

TEST(GraphicsRenderTarget, ModernBackendsSubmitFourVerticesPerIndexedQuad)
{
	const std::string OpenGl3Body = ExtractFunctionBody(ReadFile("src/engine/client/backend/opengl/backend_opengl3.cpp"), "void CCommandProcessorFragment_OpenGL3_3::Cmd_RenderTarget_Draw");
	const std::string VulkanBody = ExtractFunctionBody(ReadFile("src/engine/client/backend/vulkan/backend_vulkan.cpp"), "[[nodiscard]] bool Cmd_RenderTarget_Draw");
	ASSERT_FALSE(OpenGl3Body.empty());
	ASSERT_FALSE(VulkanBody.empty());

	EXPECT_NE(OpenGl3Body.find("UploadStreamBufferData(EPrimitiveType::QUADS, pCommand->m_pVertices, sizeof(CCommandBuffer::SVertex), pCommand->m_PrimCount)"), std::string::npos);
	EXPECT_NE(OpenGl3Body.find("glDrawElements(GL_TRIANGLES, pCommand->m_PrimCount * 6"), std::string::npos);
	EXPECT_NE(VulkanBody.find("sizeof(CCommandBuffer::SVertex) * pCommand->m_PrimCount * 4"), std::string::npos);
	EXPECT_NE(VulkanBody.find("vkCmdDrawIndexed(CommandBuffer, pCommand->m_PrimCount * 6"), std::string::npos);
}

TEST(GraphicsRenderTarget, VulkanExternalDrawsInvalidateRawBindingCache)
{
	const std::string Source = ReadFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	const std::array<const char *, 2> apSignatures = {
		"[[nodiscard]] bool Cmd_RenderTarget_Draw",
		"[[nodiscard]] bool Cmd_RenderTarget_GaussianBlurPass",
	};
	for(const char *pSignature : apSignatures)
	{
		const std::string Body = ExtractFunctionBody(Source, pSignature);
		ASSERT_FALSE(Body.empty()) << pSignature;
		const size_t RawDraw = Body.find("vkCmdDrawIndexed");
		ASSERT_NE(RawDraw, std::string::npos) << pSignature;
		const size_t CacheReset = Body.find("ResetDrawCommandState(0);", RawDraw);
		const size_t Return = Body.find("return true;", RawDraw);
		EXPECT_NE(CacheReset, std::string::npos) << pSignature;
		ASSERT_NE(Return, std::string::npos) << pSignature;
		EXPECT_LT(CacheReset, Return) << pSignature;
	}
}

TEST(GraphicsRenderTargetBackbufferCapture, CommandCarriesDestinationTarget)
{
	CCommandBuffer::SCommand_RenderTarget_CaptureBackbuffer Capture;
	Capture.m_TargetId = 5;
	EXPECT_EQ(Capture.m_Cmd, CCommandBuffer::CMD_RENDER_TARGET_CAPTURE_BACKBUFFER);
	EXPECT_EQ(Capture.m_TargetId, 5);
}

TEST(GraphicsRenderTarget, ReadbackCommandStructExposesExpectedFields)
{
	CCommandBuffer::SCommand_RenderTarget_Readback Readback;
	Readback.m_TargetId = 3;
	Readback.m_pImage = nullptr;
	EXPECT_EQ(Readback.m_Cmd, CCommandBuffer::CMD_RENDER_TARGET_READBACK);
	EXPECT_EQ(Readback.m_TargetId, 3);
	EXPECT_EQ(Readback.m_pImage, nullptr);
}

TEST(GraphicsRenderTarget, ReadbackContractExposesExpectedTypes)
{
	IGraphics::CRenderTargetReadbackHandle Handle;
	EXPECT_FALSE(Handle.IsValid());
	EXPECT_EQ(Handle.Id(), -1);
	EXPECT_EQ(Handle.Generation(), 0u);
	EXPECT_EQ(IGraphics::ERenderTargetReadbackState::INVALID, IGraphics::ERenderTargetReadbackState::INVALID);
	EXPECT_EQ(IGraphics::ERenderTargetReadbackState::PENDING, IGraphics::ERenderTargetReadbackState::PENDING);
	EXPECT_EQ(IGraphics::ERenderTargetReadbackState::READY, IGraphics::ERenderTargetReadbackState::READY);
	EXPECT_EQ(IGraphics::ERenderTargetReadbackState::FAILED, IGraphics::ERenderTargetReadbackState::FAILED);
}

TEST(GraphicsRenderTarget, BeginCommandDoesNotInheritCurrentClip)
{
	const std::string Source = ReadFile("src/engine/client/graphics_threaded.cpp");
	const std::string Body = ExtractFunctionBody(Source, "bool CGraphics_Threaded::BeginRenderTarget");
	ASSERT_FALSE(Body.empty());

	const size_t AddCmd = Body.find("AddCmd(Cmd);");
	const size_t DisableClip = Body.find("Cmd.m_State.m_ClipEnable = false;");
	ASSERT_NE(AddCmd, std::string::npos);
	ASSERT_NE(DisableClip, std::string::npos);
	EXPECT_LT(DisableClip, AddCmd);
}

TEST(GraphicsRenderTarget, AsyncBeginReadbackDoesNotWaitForIdle)
{
	const std::string Source = ReadFile("src/engine/client/graphics_threaded.cpp");
	const std::string Body = ExtractFunctionBody(Source, "IGraphics::CRenderTargetReadbackHandle CGraphics_Threaded::BeginRenderTargetReadback");
	ASSERT_FALSE(Body.empty());
	EXPECT_EQ(Body.find("WaitForIdle();"), std::string::npos);
	EXPECT_NE(Body.find("KickCommandBuffer();"), std::string::npos);
	EXPECT_NE(Body.find("CCommandBuffer::SCommand_Signal"), std::string::npos);
}

TEST(GraphicsRenderTarget, SyncReadRenderTargetUsesAsyncContract)
{
	const std::string Source = ReadFile("src/engine/client/graphics_threaded.cpp");
	const std::string Body = ExtractFunctionBody(Source, "bool CGraphics_Threaded::ReadRenderTarget");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("BeginRenderTargetReadback"), std::string::npos);
	EXPECT_NE(Body.find("PollRenderTargetReadback"), std::string::npos);
	EXPECT_NE(Body.find("ResolveRenderTargetReadback"), std::string::npos);
	EXPECT_NE(Body.find("CancelRenderTargetReadback"), std::string::npos);
}

TEST(GraphicsRenderTarget, ResolveAndCancelDefendInvalidHandles)
{
	const std::string Source = ReadFile("src/engine/client/graphics_threaded.cpp");
	const std::string ResolveBody = ExtractFunctionBody(Source, "bool CGraphics_Threaded::ResolveRenderTargetReadback");
	const std::string CancelBody = ExtractFunctionBody(Source, "void CGraphics_Threaded::CancelRenderTargetReadback");
	ASSERT_FALSE(ResolveBody.empty());
	ASSERT_FALSE(CancelBody.empty());
	EXPECT_NE(ResolveBody.find("pHandle == nullptr"), std::string::npos);
	EXPECT_NE(ResolveBody.find("!pHandle->IsValid()"), std::string::npos);
	EXPECT_NE(CancelBody.find("pHandle == nullptr"), std::string::npos);
	EXPECT_NE(CancelBody.find("!pHandle->IsValid()"), std::string::npos);
}

TEST(GraphicsRenderTarget, BackendCapabilitiesDefaultToNoRenderTarget)
{
	SBackendCapabilities Capabilities{};
	EXPECT_FALSE(Capabilities.m_RenderTargets);
	EXPECT_FALSE(Capabilities.m_RenderTargetGaussianBlur);
	EXPECT_FALSE(Capabilities.m_BackbufferCapture);
	EXPECT_FALSE(Capabilities.m_RenderTargetExternalPassRequiresSingleSample);
	EXPECT_STREQ(Capabilities.m_pRenderTargetSupportReason, "not_initialized");
}

TEST(GraphicsRenderTargetBackbufferCapture, ThreadedFrontendValidatesAndQueuesCapture)
{
	const std::string Source = ReadFile("src/engine/client/graphics_threaded.cpp");
	const std::string Body = ExtractFunctionBody(Source, "bool CGraphics_Threaded::CaptureBackbufferToRenderTarget");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("IsBackbufferCaptureSupported()"), std::string::npos);
	EXPECT_NE(Body.find("m_RenderTargetActive"), std::string::npos);
	EXPECT_NE(Body.find("m_vRenderTargetIndices"), std::string::npos);
	EXPECT_NE(Body.find("FlushVertices();"), std::string::npos);
	EXPECT_NE(Body.find("SCommand_RenderTarget_CaptureBackbuffer"), std::string::npos);
	EXPECT_NE(Body.find("AddCmd(Cmd);"), std::string::npos);
}

TEST(GraphicsRenderTargetBackbufferCapture, OpenGlUsesFramebufferBlitOnlyOnModernBackend)
{
	const std::string BaseSource = ReadFile("src/engine/client/backend/opengl/backend_opengl.cpp");
	const std::string ModernSource = ReadFile("src/engine/client/backend/opengl/backend_opengl3.cpp");
	const std::string Body = ExtractFunctionBody(ModernSource, "void CCommandProcessorFragment_OpenGL3_3::Cmd_RenderTarget_CaptureBackbuffer");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(BaseSource.find("m_BackbufferCapture = false"), std::string::npos);
	EXPECT_NE(ModernSource.find("m_BackbufferCapture = pCommand->m_pCapabilities->m_RenderTargets"), std::string::npos);
	EXPECT_NE(Body.find("GL_READ_FRAMEBUFFER_BINDING"), std::string::npos);
	EXPECT_NE(Body.find("GL_DRAW_FRAMEBUFFER_BINDING"), std::string::npos);
	EXPECT_NE(Body.find("glBlitFramebuffer"), std::string::npos);
	EXPECT_NE(Body.find("GL_COLOR_BUFFER_BIT, GL_LINEAR"), std::string::npos);
	EXPECT_EQ(Body.find("glReadPixels"), std::string::npos);
}

TEST(GraphicsRenderTargetBackbufferCapture, VulkanBlitsCurrentSwapImageAndRestoresLayouts)
{
	const std::string Source = ReadFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	const std::string SupportBody = ExtractFunctionBody(Source, "[[nodiscard]] bool SupportsBackbufferCapture() const");
	const std::string CaptureBody = ExtractFunctionBody(Source, "[[nodiscard]] bool Cmd_RenderTarget_CaptureBackbuffer");
	ASSERT_FALSE(SupportBody.empty());
	ASSERT_FALSE(CaptureBody.empty());
	EXPECT_EQ(SupportBody.find("!HasMultiSampling()"), std::string::npos);
	EXPECT_NE(CaptureBody.find("HasMultiSampling()"), std::string::npos);
	EXPECT_NE(SupportBody.find("m_OptimalSwapChainImageBlitting"), std::string::npos);
	EXPECT_NE(SupportBody.find("m_OptimalRGBAImageBlitting"), std::string::npos);
	EXPECT_NE(SupportBody.find("VK_FORMAT_B8G8R8A8_UNORM"), std::string::npos);
	EXPECT_NE(SupportBody.find("VK_FORMAT_R8G8B8A8_UNORM"), std::string::npos);
	EXPECT_NE(Source.find("VK_IMAGE_USAGE_TRANSFER_DST_BIT"), std::string::npos);
	EXPECT_NE(CaptureBody.find("EndSwapRenderPassForExternalWork();"), std::string::npos);
	EXPECT_NE(CaptureBody.find("VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL"), std::string::npos);
	EXPECT_NE(CaptureBody.find("VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL"), std::string::npos);
	EXPECT_NE(CaptureBody.find("vkCmdBlitImage"), std::string::npos);
	EXPECT_NE(CaptureBody.find("GetPresentedImageViewport()"), std::string::npos);
	EXPECT_NE(CaptureBody.find("Target.m_Height, 0"), std::string::npos);
	EXPECT_NE(CaptureBody.find("VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL"), std::string::npos);
	EXPECT_NE(CaptureBody.find("VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR"), std::string::npos);
	EXPECT_NE(CaptureBody.find("BeginSwapRenderPass(m_VKRenderPassLoad);"), std::string::npos);
	EXPECT_EQ(CaptureBody.find("ReadRenderTarget"), std::string::npos);
	EXPECT_EQ(CaptureBody.find("SubmitCurrentCommandsAndRestartSwapPass"), std::string::npos);
}

TEST(GraphicsRenderTargetBackbufferCapture, RuntimeMultiSamplingChangesKeepCapabilityInSync)
{
	const std::string Source = ReadFile("src/engine/client/graphics_threaded.cpp");
	const std::string SupportBody = ExtractFunctionBody(Source, "bool CGraphics_Threaded::IsBackbufferCaptureSupported");
	const std::string SetBody = ExtractFunctionBody(Source, "bool CGraphics_Threaded::SetMultiSampling");
	const std::string SwapBody = ExtractFunctionBody(Source, "void CGraphics_Threaded::Swap");
	ASSERT_FALSE(SupportBody.empty());
	ASSERT_FALSE(SetBody.empty());
	ASSERT_FALSE(SwapBody.empty());
	EXPECT_NE(SupportBody.find("m_GLRenderTargetExternalPassRequiresSingleSample"), std::string::npos);
	EXPECT_NE(SupportBody.find("m_MultiSamplingCount"), std::string::npos);
	EXPECT_NE(SetBody.find("m_PendingMultiSamplingCount"), std::string::npos);
	EXPECT_NE(SwapBody.find("m_PendingMultiSamplingCount"), std::string::npos);
}

TEST(GraphicsRenderTargetBackbufferCapture, VulkanRecordsPostCaptureDrawsInline)
{
	const std::string Source = ReadFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	const std::string Body = ExtractFunctionBody(Source, "[[nodiscard]] bool GetGraphicCommandBuffer");
	const std::string StartBody = ExtractFunctionBody(Source, "void StartCommands");
	const std::string PrepareBody = ExtractFunctionBody(Source, "[[nodiscard]] bool PrepareFrame");
	ASSERT_FALSE(Body.empty());
	ASSERT_FALSE(StartBody.empty());
	ASSERT_FALSE(PrepareBody.empty());
	EXPECT_NE(Body.find("m_ThreadCount < 2 || m_ForceSingleThreadedRender"), std::string::npos);
	EXPECT_NE(Body.find("m_vMainDrawCommandBuffers[m_CurImageIndex]"), std::string::npos);
	EXPECT_EQ(StartBody.find("m_ForceSingleThreadedRender = false"), std::string::npos);
	EXPECT_NE(PrepareBody.find("m_ForceSingleThreadedRender = false"), std::string::npos);
}

TEST(GraphicsRenderTargetBackbufferCapture, VulkanLoadPassSynchronizesAttachmentReads)
{
	const std::string Source = ReadFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	const std::string Body = ExtractFunctionBody(Source, "[[nodiscard]] bool CreateRenderPass");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("VK_ACCESS_COLOR_ATTACHMENT_READ_BIT"), std::string::npos);
	EXPECT_NE(Body.find("VK_PIPELINE_STAGE_TRANSFER_BIT"), std::string::npos);
}

TEST(GraphicsRenderTargetGaussianBlur, ThreadedFrontendBuildsHorizontalAndVerticalPasses)
{
	const std::string Source = ReadFile("src/engine/client/graphics_threaded.cpp");
	const std::string Body = ExtractFunctionBody(Source, "bool CGraphics_Threaded::GaussianBlurRenderTarget");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("CalculateGaussianBlurKernel"), std::string::npos);
	EXPECT_NE(Body.find("Source.Id() == Temporary.Id()"), std::string::npos);
	EXPECT_NE(Body.find("m_vRenderTargetSizes[Source.Id()]"), std::string::npos);
	EXPECT_NE(Body.find("Horizontal.m_Horizontal = true"), std::string::npos);
	EXPECT_NE(Body.find("Vertical.m_Horizontal = false"), std::string::npos);
	EXPECT_NE(Body.find("BeginRenderTarget(Temporary"), std::string::npos);
	EXPECT_NE(Body.find("BeginRenderTarget(Destination"), std::string::npos);
}

TEST(GraphicsRenderTargetGaussianBlur, FrontendRejectsNestedRenderTargets)
{
	const std::string Source = ReadFile("src/engine/client/graphics_threaded.cpp");
	const std::string BeginBody = ExtractFunctionBody(Source, "bool CGraphics_Threaded::BeginRenderTarget");
	const std::string EndBody = ExtractFunctionBody(Source, "void CGraphics_Threaded::EndRenderTarget");
	ASSERT_FALSE(BeginBody.empty());
	ASSERT_FALSE(EndBody.empty());
	EXPECT_NE(BeginBody.find("m_RenderTargetActive"), std::string::npos);
	EXPECT_NE(BeginBody.find("m_RenderTargetActive = true"), std::string::npos);
	EXPECT_NE(EndBody.find("!m_RenderTargetActive"), std::string::npos);
	EXPECT_NE(EndBody.find("m_RenderTargetActive = false"), std::string::npos);
}

TEST(GraphicsRenderTargetDualBlur, FrontendUsesDownsampledBlurAndUpsample)
{
	const std::string Source = ReadFile("src/engine/client/graphics_threaded.cpp");
	const std::string Body = ExtractFunctionBody(Source, "bool CGraphics_Threaded::DualBlurRenderTarget");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("std::array<CRenderTargetHandle, 5>"), std::string::npos);
	EXPECT_NE(Body.find("DownsampleSize.x > SourceSize.x"), std::string::npos);
	EXPECT_NE(Body.find("BeginRenderTarget(Downsample"), std::string::npos);
	EXPECT_NE(Body.find("DrawRenderTarget(Source"), std::string::npos);
	EXPECT_NE(Body.find("GaussianBlurRenderTarget(Downsample, DownsampleTemporary, DownsampleBlurred"), std::string::npos);
	EXPECT_NE(Body.find("BeginRenderTarget(Destination"), std::string::npos);
	EXPECT_NE(Body.find("DrawRenderTarget(DownsampleBlurred"), std::string::npos);
}

TEST(GraphicsRenderTargetDualBlur, MediaIslandUsesHalfResolutionIntermediateTargets)
{
	const std::string Source = ReadFile("src/game/client/components/hud.cpp");
	const std::string Body = ExtractFunctionBody(Source, "bool CHud::PrepareMediaIslandBlur");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("(BlurWidth + 1) / 2"), std::string::npos);
	EXPECT_NE(Body.find("(BlurHeight + 1) / 2"), std::string::npos);
	EXPECT_NE(Body.find("CreateRenderTarget(DualBlurWidth, DualBlurHeight)"), std::string::npos);
	EXPECT_NE(Body.find("Graphics()->DualBlurRenderTarget"), std::string::npos);
	EXPECT_EQ(Body.find("Graphics()->GaussianBlurRenderTarget"), std::string::npos);
}

TEST(GraphicsRenderTargetGaussianBlur, ShadersAccumulateRgbaWithBoundedKernel)
{
	const std::array<const char *, 2> apShaderPaths = {
		"data/shader/gaussian_blur.frag",
		"data/shader/vulkan/gaussian_blur.frag",
	};
	for(const char *pShaderPath : apShaderPaths)
	{
		const std::string Shader = ReadFile(pShaderPath);
		EXPECT_NE(Shader.find("GAUSSIAN_BLUR_MAX_RADIUS"), std::string::npos) << pShaderPath;
		EXPECT_NE(Shader.find("vec4 Result"), std::string::npos) << pShaderPath;
		EXPECT_NE(Shader.find("gWeights[Offset]"), std::string::npos) << pShaderPath;
		EXPECT_EQ(Shader.find("vec4(Result.rgb, 1.0)"), std::string::npos) << pShaderPath;
	}
}

TEST(GraphicsRenderTargetGaussianBlur, VulkanUsesSingleSampleRenderTargetPipeline)
{
	const std::string Source = ReadFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	const std::string Body = ExtractFunctionBody(Source, "[[nodiscard]] bool CreateGaussianBlurGraphicsPipeline");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("m_VKRenderTargetRenderPass"), std::string::npos);
	EXPECT_NE(Body.find("VK_SAMPLE_COUNT_1_BIT"), std::string::npos);
	EXPECT_NE(Source.find("m_RenderTargetGaussianBlur = SupportsRenderTargetGaussianBlur()"), std::string::npos);
}

TEST(GraphicsRenderTargetGaussianBlur, OpenGlPublishesCapabilityOnlyForLinkedProgram)
{
	const std::string Source = ReadFile("src/engine/client/backend/opengl/backend_opengl3.cpp");
	EXPECT_NE(Source.find("shader/gaussian_blur.vert"), std::string::npos);
	EXPECT_NE(Source.find("shader/gaussian_blur.frag"), std::string::npos);
	EXPECT_NE(Source.find("m_RenderTargetGaussianBlur = pCommand->m_pCapabilities->m_RenderTargets && m_GaussianBlurProgramValid"), std::string::npos);
}

TEST(GraphicsRenderTargetGaussianBlur, VulkanRenderTargetPublishesWritesBeforeSampling)
{
	const std::string Source = ReadFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	const std::string Body = ExtractFunctionBody(Source, "[[nodiscard]] bool CreateRenderPass");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT"), std::string::npos);
	EXPECT_NE(Body.find("VK_ACCESS_SHADER_READ_BIT"), std::string::npos);
	EXPECT_NE(Body.find("FinalLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? 2 : 1"), std::string::npos);
}

TEST(GraphicsRenderTarget, VulkanBackendDeclaresRenderTargetSupport)
{
	const std::string Source = ReadFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	const size_t MultiSamplingInit = Source.find("m_MultiSamplingCount = (g_Config.m_GfxFsaaSamples & 0xFFFFFFFE)");
	const size_t InitVulkan = Source.find("InitVulkan<true>()");
	const size_t RenderTargetsCapability = Source.find("m_RenderTargets = SupportsRenderTargetReadback()");
	ASSERT_NE(MultiSamplingInit, std::string::npos);
	ASSERT_NE(InitVulkan, std::string::npos);
	ASSERT_NE(RenderTargetsCapability, std::string::npos);
	EXPECT_LT(MultiSamplingInit, RenderTargetsCapability);
	EXPECT_LT(InitVulkan, RenderTargetsCapability);
	EXPECT_NE(Source.find("m_VKRenderTargetRenderPass != VK_NULL_HANDLE"), std::string::npos);
	EXPECT_NE(Source.find("RenderTargetReadbackSupportReason()"), std::string::npos);
	EXPECT_NE(Source.find("RenderTargetReadbackFormat()"), std::string::npos);
	EXPECT_NE(Source.find("VK_FORMAT_R8G8B8A8_UNORM"), std::string::npos);
	EXPECT_NE(Source.find("SubmitCurrentCommandsAndRestartSwapPass()"), std::string::npos);
	EXPECT_NE(Source.find("m_OptimalSwapChainImageBlitting && m_OptimalRGBAImageBlitting && m_LinearRGBAImageBlitting"), std::string::npos);
}

TEST(GraphicsRenderTarget, VulkanSwapRenderPassUsesInlineAfterForcedSingleThreadedRecording)
{
	const std::string Source = ReadFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	const std::string Body = ExtractFunctionBody(Source, "void BeginSwapRenderPass");
	ASSERT_FALSE(Body.empty());

	const size_t SubpassContents = Body.find("SubpassContents");
	const size_t ForceSingleThreaded = Body.find("m_ForceSingleThreadedRender");
	const size_t BeginRenderPass = Body.find("vkCmdBeginRenderPass");
	ASSERT_NE(SubpassContents, std::string::npos);
	ASSERT_NE(ForceSingleThreaded, std::string::npos);
	ASSERT_NE(BeginRenderPass, std::string::npos);
	EXPECT_LT(SubpassContents, BeginRenderPass);
	EXPECT_LT(ForceSingleThreaded, BeginRenderPass);
	EXPECT_NE(Body.find("VK_SUBPASS_CONTENTS_INLINE"), std::string::npos);
	EXPECT_NE(Body.find("VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS"), std::string::npos);
}

TEST(GraphicsRenderTarget, VulkanIntermediateSwapPassSubmitUsesFenceInsteadOfQueueIdle)
{
	const std::string Source = ReadFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	const std::string Body = ExtractFunctionBody(Source, "[[nodiscard]] bool SubmitCurrentCommandsAndRestartSwapPass()");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("vkResetFences"), std::string::npos);
	EXPECT_NE(Body.find("QueueSubmit("), std::string::npos);
	EXPECT_NE(Body.find("WaitForFences("), std::string::npos);
	EXPECT_EQ(Body.find("vkQueueSubmit("), std::string::npos);
	EXPECT_EQ(Body.find("vkQueueWaitIdle("), std::string::npos);
}

TEST(GraphicsRenderTarget, VulkanRenderTargetReadbackUsesFenceInsteadOfQueueIdle)
{
	const std::string Source = ReadFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	const std::string Body = ExtractFunctionBody(Source, "[[nodiscard]] bool Cmd_RenderTarget_Readback");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("vkResetFences"), std::string::npos);
	EXPECT_NE(Body.find("QueueSubmit("), std::string::npos);
	EXPECT_NE(Body.find("WaitForFences("), std::string::npos);
	EXPECT_NE(Body.find("InvalidateMappedMemoryRanges("), std::string::npos);
	EXPECT_EQ(Body.find("vkQueueSubmit("), std::string::npos);
	EXPECT_EQ(Body.find("vkQueueWaitIdle("), std::string::npos);
	EXPECT_EQ(Body.find("vkInvalidateMappedMemoryRanges("), std::string::npos);
}

TEST(GraphicsRenderTarget, VulkanPreviewReadbackDoesNotDependOnSwapchainMsaa)
{
	const std::string Source = ReadFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	const std::string SupportBody = ExtractFunctionBody(Source, "[[nodiscard]] bool SupportsRenderTargetReadback() const");
	const std::string CreateBody = ExtractFunctionBody(Source, "[[nodiscard]] bool Cmd_RenderTarget_Create");
	ASSERT_FALSE(SupportBody.empty());
	ASSERT_FALSE(CreateBody.empty());
	EXPECT_EQ(SupportBody.find("!HasMultiSampling()"), std::string::npos);
	EXPECT_EQ(CreateBody.find("HasMultiSampling() ||"), std::string::npos);
}
