#include <engine/client/backend/vulkan/backend_vulkan_qm_ext.h>

#include <gtest/gtest.h>

#include <string>

TEST(QmVulkanEnhanced, ModeFromConfigMapsOffAutoOn)
{
	using qm_vulkan_ext::EEnhancedMode;
	EXPECT_EQ(qm_vulkan_ext::ModeFromConfig(0), EEnhancedMode::OFF);
	EXPECT_EQ(qm_vulkan_ext::ModeFromConfig(1), EEnhancedMode::AUTO);
	EXPECT_EQ(qm_vulkan_ext::ModeFromConfig(2), EEnhancedMode::ON);
	EXPECT_EQ(qm_vulkan_ext::ModeFromConfig(-1), EEnhancedMode::OFF);
	EXPECT_EQ(qm_vulkan_ext::ModeFromConfig(99), EEnhancedMode::ON);
}

TEST(QmVulkanEnhanced, ShouldLoadSkipsWhenOffOrSessionDisabled)
{
	using qm_vulkan_ext::EEnhancedMode;
	EXPECT_FALSE(qm_vulkan_ext::ShouldLoadEnhancedPipelines(EEnhancedMode::OFF, false));
	EXPECT_TRUE(qm_vulkan_ext::ShouldLoadEnhancedPipelines(EEnhancedMode::AUTO, false));
	EXPECT_TRUE(qm_vulkan_ext::ShouldLoadEnhancedPipelines(EEnhancedMode::ON, false));
	EXPECT_FALSE(qm_vulkan_ext::ShouldLoadEnhancedPipelines(EEnhancedMode::AUTO, true));
	EXPECT_FALSE(qm_vulkan_ext::ShouldLoadEnhancedPipelines(EEnhancedMode::ON, true));
}

TEST(QmVulkanEnhanced, DeviceLostReasonOnlyWhenEnhancedActive)
{
	using qm_vulkan_ext::EDisableReason;
	using qm_vulkan_ext::EEnhancedMode;
	EXPECT_EQ(qm_vulkan_ext::ResolveDeviceLost(EEnhancedMode::AUTO, true), EDisableReason::DEVICE_LOST);
	EXPECT_EQ(qm_vulkan_ext::ResolveDeviceLost(EEnhancedMode::ON, true), EDisableReason::DEVICE_LOST);
	EXPECT_EQ(qm_vulkan_ext::ResolveDeviceLost(EEnhancedMode::AUTO, false), EDisableReason::NONE);
	EXPECT_EQ(qm_vulkan_ext::ResolveDeviceLost(EEnhancedMode::OFF, false), EDisableReason::NONE);
}
