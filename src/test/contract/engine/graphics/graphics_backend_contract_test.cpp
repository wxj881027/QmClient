#include <gtest/gtest.h>
#include <test/test.h>

TEST(GraphicsBackendContract, VulkanDeclaresSingleSampleExternalPassConstraint)
{
	// Vulkan 的捕获/模糊通道按单采样 RT 实现，MSAA 下由后端命令层静默跳过；该上报
	// 无法在无 Vulkan+MSAA 设备的测试环境中运行时观察，故以源码合同锁定 Cmd_Init 的
	// 能力声明，防止线程层闸门 SingleSampleFeatureAllowedUnderMsaa 失去输入回到静默失效。
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	EXPECT_NE(Source.find("m_RenderTargetExternalPassRequiresSingleSample = true"), std::string::npos);
}
