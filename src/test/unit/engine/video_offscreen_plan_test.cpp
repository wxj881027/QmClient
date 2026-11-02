// 导出离屏计划的纯函数行为：何时离屏、目标尺寸怎么取、采样数怎么降档。
// 生产入口是 CClient::StartVideo（client.cpp），行为全部收敛在 PlanVideoOffscreenFrame。
#include <engine/client/video_export.h>

#include <gtest/gtest.h>

TEST(VideoOffscreenPlan, FollowGlobalWithoutUpscaleKeepsTheWindowPath)
{
	// -1 跟随全局且输出不大于窗口：沿用读窗口帧缓冲，窗口的 FSAA 自然带进导出。
	const auto Plan = PlanVideoOffscreenFrame(1920, 1080, 0, 0, -1, 0, 8);
	EXPECT_FALSE(Plan.m_Offscreen);
	EXPECT_FALSE(Plan.m_Upscale);
	EXPECT_EQ(Plan.m_RenderWidth, 1920);
	EXPECT_EQ(Plan.m_RenderHeight, 1080);
	EXPECT_EQ(Plan.m_SampleCount, 1);
}

TEST(VideoOffscreenPlan, ExplicitFsaaForcesOffscreenEvenWhenGlobalIsOff)
{
	// 显式 4x：游玩可以关 AA（全局 0），导出仍按 4x 离屏多采样。
	const auto Plan = PlanVideoOffscreenFrame(1280, 720, 0, 0, 4, 0, 8);
	EXPECT_TRUE(Plan.m_Offscreen);
	EXPECT_FALSE(Plan.m_Upscale);
	// 目标尺寸 = 窗口尺寸（输出没有放大诉求），只叠采样。
	EXPECT_EQ(Plan.m_RenderWidth, 1280);
	EXPECT_EQ(Plan.m_RenderHeight, 720);
	EXPECT_EQ(Plan.m_SampleCount, 4);
}

TEST(VideoOffscreenPlan, ExplicitOffOverridesGlobalMultisampling)
{
	// 显式 0：即使窗口开着 8x FSAA，导出也强制单采样。
	const auto Plan = PlanVideoOffscreenFrame(1280, 720, 0, 0, 0, 8, 8);
	EXPECT_TRUE(Plan.m_Offscreen);
	EXPECT_EQ(Plan.m_SampleCount, 1);
}

TEST(VideoOffscreenPlan, OutputLargerThanWindowRendersAtOutputSize)
{
	// 输出大于窗口：按输出尺寸离屏渲染（真分辨率导出），且不可降级。
	const auto Plan = PlanVideoOffscreenFrame(1280, 720, 1920, 1080, -1, 0, 8);
	EXPECT_TRUE(Plan.m_Offscreen);
	EXPECT_TRUE(Plan.m_Upscale);
	EXPECT_EQ(Plan.m_RenderWidth, 1920);
	EXPECT_EQ(Plan.m_RenderHeight, 1080);
	// 跟随全局时按全局档位采样：超采样被迫离屏后，导出画质不应低于游玩画质。
	const auto PlanWithGlobalFsaa = PlanVideoOffscreenFrame(1280, 720, 1920, 1080, -1, 4, 8);
	EXPECT_EQ(PlanWithGlobalFsaa.m_SampleCount, 4);
}

TEST(VideoOffscreenPlan, UpscaleOnlyInOneDimensionStillUsesOutputSize)
{
	// 只有宽度超出也算超采样：目标宽高比必须跟输出一致，不能按维取 max 拼出错误比例。
	const auto Plan = PlanVideoOffscreenFrame(2560, 1440, 3840, 2160, -1, 0, 1);
	EXPECT_TRUE(Plan.m_Upscale);
	EXPECT_EQ(Plan.m_RenderWidth, 3840);
	EXPECT_EQ(Plan.m_RenderHeight, 2160);
}

TEST(VideoOffscreenPlan, BackendSampleCapAndPixelBudgetReduceSamples)
{
	// 后端上限压档：请求 8x 但设备只支持 4x。
	const auto Capped = PlanVideoOffscreenFrame(1920, 1080, 0, 0, 8, 0, 4);
	EXPECT_EQ(Capped.m_SampleCount, 4);
	// 后端不支持多采样（上限 1）：请求被压回单采样，离屏本身仍然成立。
	const auto Unsupported = PlanVideoOffscreenFrame(1920, 1080, 0, 0, 8, 0, 1);
	EXPECT_TRUE(Unsupported.m_Offscreen);
	EXPECT_EQ(Unsupported.m_SampleCount, 1);
	// 像素预算：8K(约 3317 万像素) × 8 超过 1.28 亿预算，逐级降到 2x(约 6634 万)。
	const auto Budget = PlanVideoOffscreenFrame(1280, 720, 7680, 4320, 8, 0, 8);
	EXPECT_EQ(Budget.m_RenderWidth, 7680);
	EXPECT_EQ(Budget.m_SampleCount, 2);
	// 4K(约 829 万像素) × 8 = 约 6634 万，预算内不降档。
	const auto Fits = PlanVideoOffscreenFrame(3840, 2160, 0, 0, 8, 0, 8);
	EXPECT_EQ(Fits.m_SampleCount, 8);
}
