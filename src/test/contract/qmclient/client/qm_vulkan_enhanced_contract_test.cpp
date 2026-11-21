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
	// 设备丢失先登记延迟处理，由 ApplyDeferredQmDeviceLoss 按 AUTO 策略禁用增强管线，
	// 持久化写回由 CClient::HandleQmGraphicsFatalError 完成。
	const size_t RecreatePos = Source.find("int RecreateSwapChain()");
	ASSERT_NE(RecreatePos, std::string::npos);
	const size_t WaitIdlePos = Source.find("VkResult WaitIdleResult = DeviceWaitIdle();", RecreatePos);
	ASSERT_NE(WaitIdlePos, std::string::npos);
	const size_t MarkPos = Source.find("m_QmDeferredDeviceLoss.Report();", WaitIdlePos);
	ASSERT_NE(MarkPos, std::string::npos);
	EXPECT_LT(MarkPos - WaitIdlePos, 512);
	// 记忆恢复路径的 wait idle 失败同样标记（AllocateVulkanMemory，位于
	// RecreateSwapChain 之前，因此从错误字符串位置反向查找）。
	const size_t RecoveryPos = Source.find("Waiting for device idle during memory recovery failed.");
	ASSERT_NE(RecoveryPos, std::string::npos);
	const size_t RecoveryMarkPos = Source.rfind("m_QmDeferredDeviceLoss.Report();", RecoveryPos);
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
