#include <gtest/gtest.h>
#include <test/test.h>

TEST(GraphicsBackendContract, VulkanDeclaresResolvedExternalPassSupport)
{
	// 无设备测试只能检查后端能力注册；真实 FSAA 捕获与模糊由开发客户端 GPU 场景验证。
	// Vulkan resolve 后读取单采样交换链，不能再上报为必须关闭全局 MSAA。
	const std::string Source = ReadTestSourceFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	EXPECT_NE(Source.find("m_RenderTargetExternalPassRequiresSingleSample = false"), std::string::npos);
}
