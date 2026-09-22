#include <engine/client/backend/vulkan/backend_vulkan_qm_ext.h>

#include <gtest/gtest.h>

#include <test/test.h>

#include <fstream>
#include <sstream>
#include <string>

namespace
{
	std::string ReadRepoFile(const char *pPath)
	{
		return ReadTestSourceFile(pPath);
	}
} // namespace

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

TEST(QmVulkanEnhancedSource, VulkanGatesQmPipelinesBehindEnhancedSwitch)
{
	const std::string Source = ReadRepoFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	EXPECT_NE(Source.find("QmEnhancedShouldLoad()"), std::string::npos);
	EXPECT_NE(Source.find("m_QmMediaIslandSdfPipelineValid"), std::string::npos);
	EXPECT_NE(Source.find("if(!m_QmMediaIslandSdfPipelineValid)"), std::string::npos);
	EXPECT_NE(Source.find("if(!m_QmRoundedRectSdfPipelineValid)"), std::string::npos);
	EXPECT_NE(Source.find("QmEnhancedMarkDisabled(qm_vulkan_ext::EDisableReason::DEVICE_LOST)"), std::string::npos);
	// 能力必须跟随真实管线，而不是无条件 true。
	EXPECT_NE(Source.find("m_pCapabilities->m_MediaIslandSdf = m_QmMediaIslandSdfPipelineValid"), std::string::npos);
	EXPECT_NE(Source.find("m_pCapabilities->m_RoundedRectSdf = m_QmRoundedRectSdfPipelineValid"), std::string::npos);
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
