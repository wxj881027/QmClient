#include <engine/client/graphics_threaded.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>

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

TEST(GraphicsRenderTargetGaussianBlur, DualKawasePyramidUsesHalfAndQuarterResolution)
{
	EXPECT_EQ(IGraphics::DualKawasePyramidDimension(1920, 0), 960);
	EXPECT_EQ(IGraphics::DualKawasePyramidDimension(1920, 1), 480);
	EXPECT_EQ(IGraphics::DualKawasePyramidDimension(721, 0), 361);
	EXPECT_EQ(IGraphics::DualKawasePyramidDimension(721, 1), 181);
	EXPECT_EQ(IGraphics::DualKawasePyramidDimension(0, 0), 0);
	EXPECT_EQ(IGraphics::DualKawasePyramidDimension(1920, -1), 0);
}

TEST(GraphicsRenderTarget, ReadbackHandleStartsInvalid)
{
	IGraphics::CRenderTargetReadbackHandle Handle;
	EXPECT_FALSE(Handle.IsValid());
	EXPECT_EQ(Handle.Id(), -1);
	EXPECT_EQ(Handle.Generation(), 0u);
}

TEST(GraphicsRenderTarget, CommandConstructorsSelectTheirDispatcherOpcodes)
{
	// opcode 是后端分派入口，验证生产构造函数，避免测试自行赋值后再读取。
	EXPECT_EQ(CCommandBuffer::SCommand_RenderTarget_Create().m_Cmd, CCommandBuffer::CMD_RENDER_TARGET_CREATE);
	EXPECT_EQ(CCommandBuffer::SCommand_RenderTarget_Draw().m_Cmd, CCommandBuffer::CMD_RENDER_TARGET_DRAW);
	EXPECT_EQ(CCommandBuffer::SCommand_RenderTarget_CaptureBackbuffer().m_Cmd, CCommandBuffer::CMD_RENDER_TARGET_CAPTURE_BACKBUFFER);
	EXPECT_EQ(CCommandBuffer::SCommand_RenderTarget_Readback().m_Cmd, CCommandBuffer::CMD_RENDER_TARGET_READBACK);
	EXPECT_EQ(CCommandBuffer::SCommand_RenderTarget_GaussianBlurPass().m_Cmd, CCommandBuffer::CMD_RENDER_TARGET_GAUSSIAN_BLUR_PASS);
}
