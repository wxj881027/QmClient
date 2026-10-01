#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>

#include <array>
#include <fstream>
#include <sstream>
#include <string>

TEST(QmVulkanEnhancedSource, VulkanMarksEnhancedDisabledOnWaitIdleDeviceLost)
{
	const std::string Source = ReadRepoFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	// RecreateSwapChain 的 wait idle 失败路径与 QueueSubmit 路径一致：
	// AUTO 模式下设备丢失必须标记增强管线按会话禁用，持久化写回由
	// CClient::HandleQmGraphicsFatalError 完成。
	const size_t RecreatePos = Source.find("int RecreateSwapChain()");
	ASSERT_NE(RecreatePos, std::string::npos);
	const size_t WaitIdlePos = Source.find("VkResult WaitIdleResult = DeviceWaitIdle();", RecreatePos);
	ASSERT_NE(WaitIdlePos, std::string::npos);
	const size_t MarkPos = Source.find("QmEnhancedMarkDisabled(qm_vulkan_ext::EDisableReason::DEVICE_LOST)", WaitIdlePos);
	ASSERT_NE(MarkPos, std::string::npos);
	EXPECT_LT(MarkPos - WaitIdlePos, 512);
	// 记忆恢复路径的 wait idle 失败同样标记（AllocateVulkanMemory，位于
	// RecreateSwapChain 之前，因此从错误字符串位置反向查找）。
	const size_t RecoveryPos = Source.find("Waiting for device idle during memory recovery failed.");
	ASSERT_NE(RecoveryPos, std::string::npos);
	const size_t RecoveryMarkPos = Source.rfind("QmEnhancedMarkDisabled(qm_vulkan_ext::EDisableReason::DEVICE_LOST)", RecoveryPos);
	ASSERT_NE(RecoveryMarkPos, std::string::npos);
	EXPECT_LT(RecoveryPos - RecoveryMarkPos, 1024);
}

TEST(QmVulkanEnhancedSource, IslandRingFallbackUsesThickness)
{
	const std::string Source = ReadRepoFile("src/game/client/QmUi/QmIslandSurface.cpp");
	EXPECT_NE(Source.find("QuadsDrawFreeform"), std::string::npos);
	EXPECT_NE(Source.find("OuterRadius"), std::string::npos);
	EXPECT_EQ(Source.find("LinesDraw(vLines.data()"), std::string::npos);
}

TEST(QmVulkanEnhancedConfig, MasterAndFeatureSwitchesExist)
{
	const std::string Config = ReadRepoFile("src/engine/shared/config_variables_qmclient.h");
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmEnhancedRendering, qm_enhanced_rendering, 1, 0, 2"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmEnhancedSdf, qm_enhanced_sdf"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmEnhancedBlur, qm_enhanced_blur"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmEnhancedMsdf, qm_enhanced_msdf"), std::string::npos);
}
