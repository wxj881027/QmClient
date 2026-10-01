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
	using TGaussianBlurRenderTarget = bool (IGraphics::*)(IGraphics::CRenderTargetHandle, const std::array<IGraphics::CRenderTargetHandle, IGraphics::DUAL_KAWASE_PYRAMID_LEVELS> &, IGraphics::CRenderTargetHandle, const IGraphics::SGaussianBlurParams &);
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

TEST(GraphicsRenderTargetGaussianBlur, ThreadedFrontendBuildsModeSpecificPassChain)
{
	const std::string Source = ReadFile("src/engine/client/graphics_threaded.cpp");
	const std::string Body = ExtractFunctionBody(Source, "bool CGraphics_Threaded::GaussianBlurRenderTarget");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("CalculateGaussianBlurKernel"), std::string::npos);
	EXPECT_NE(Body.find("aTemporary[Index].Id() == Source.Id()"), std::string::npos);
	EXPECT_NE(Body.find("m_vRenderTargetSizes[Source.Id()]"), std::string::npos);
	EXPECT_NE(Body.find("DualKawasePyramidDimension"), std::string::npos);
	EXPECT_NE(Body.find("DUAL_KAWASE_PYRAMID_LEVELS"), std::string::npos);
	EXPECT_NE(Body.find("AddBlurPass"), std::string::npos);
	EXPECT_NE(Body.find("Command.m_Upsample = Upsample"), std::string::npos);
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

TEST(GraphicsRenderTargetDualBlur, MediaIslandUsesHalfResolutionIntermediateTargets)
{
	const std::string Source = ReadFile("src/game/client/components/hud.cpp");
	const std::string Body = ExtractFunctionBody(Source, "IGraphics::CRenderTargetHandle CHud::MediaIslandBlurBackdrop()");
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
		EXPECT_NE(Shader.find("gMode"), std::string::npos) << pShaderPath;
		EXPECT_NE(Shader.find("gPass"), std::string::npos) << pShaderPath;
		EXPECT_NE(Shader.find("gMode == 1"), std::string::npos) << pShaderPath;
		EXPECT_NE(Shader.find("gMode == 2"), std::string::npos) << pShaderPath;
		EXPECT_NE(Shader.find("* 4.0"), std::string::npos) << pShaderPath;
		EXPECT_NE(Shader.find("/ 8.0"), std::string::npos) << pShaderPath;
		EXPECT_NE(Shader.find("* 2.0"), std::string::npos) << pShaderPath;
		EXPECT_NE(Shader.find("/ 12.0"), std::string::npos) << pShaderPath;
		EXPECT_NE(Shader.find("vec4 Result"), std::string::npos) << pShaderPath;
		EXPECT_NE(Shader.find("gWeights[Offset]"), std::string::npos) << pShaderPath;
		EXPECT_EQ(Shader.find("vec4(Result.rgb, 1.0)"), std::string::npos) << pShaderPath;
	}
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
