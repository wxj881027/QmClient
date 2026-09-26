#include "qmclient_source_contract_test.h"

#include <gtest/gtest.h>

TEST(QmIconVulkanContract, ExposesMsdfCapabilityOnlyAfterPipelineCreation)
{
	const std::string Source = ReadRepoFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	const std::string BackendHeader = ReadRepoFile("src/engine/client/backend_sdl.h");
	const size_t Create = Source.find("m_TexturedMsdfPipelineValid = CreateTexturedMsdfGraphicsPipeline");
	const size_t Gaussian = Source.find("m_GaussianBlurPipelineValid = CreateGaussianBlurGraphicsPipeline", Create);
	ASSERT_NE(Create, std::string::npos);
	ASSERT_NE(Gaussian, std::string::npos);
	const std::string InitSection = Source.substr(Create, Gaussian - Create);
	EXPECT_NE(InitSection.find("m_TexturedMsdfPipeline.Destroy"), std::string::npos);
	EXPECT_NE(InitSection.find("falling back to alpha icon atlas"), std::string::npos);
	const size_t RequiredCheck = InitSection.find("if(m_TexturedMsdfPipelineRequired)");
	const size_t FallbackWarning = InitSection.find("falling back to alpha icon atlas");
	const size_t CapabilityEnable = InitSection.find("m_TexturedMsdfPipelineRequired = true");
	ASSERT_NE(RequiredCheck, std::string::npos);
	ASSERT_NE(FallbackWarning, std::string::npos);
	ASSERT_NE(CapabilityEnable, std::string::npos);
	EXPECT_NE(InitSection.find("return -1", RequiredCheck), std::string::npos);
	EXPECT_EQ(InitSection.substr(0, RequiredCheck).find("return -1"), std::string::npos);
	EXPECT_LT(RequiredCheck, FallbackWarning);
	EXPECT_LT(FallbackWarning, CapabilityEnable);
	EXPECT_NE(Source.find("m_TexturedMsdfPipelineValid = false"), std::string::npos);
	EXPECT_NE(Source.find("m_pBackendCapabilities = pCommand->m_pCapabilities"), std::string::npos);
	EXPECT_NE(Source.find("SyncTexturedMsdfCapability()"), std::string::npos);
	EXPECT_NE(Source.find("m_pBackendCapabilities->m_TexturedMsdf.store(m_TexturedMsdfPipelineValid"), std::string::npos);
	EXPECT_NE(BackendHeader.find("std::atomic<bool> m_TexturedMsdf{false};"), std::string::npos);
	EXPECT_NE(Source.find("if(!m_TexturedMsdfPipelineValid)\n\t\t\treturn true;"), std::string::npos);
	EXPECT_NE(Source.find("if(RecreateSwapChain() != 0)\n\t\t\t\treturn false;"), std::string::npos);
	EXPECT_EQ(Source.find("m_pCapabilities->m_TexturedMsdf = m_TexturedMsdfPipelineValid"), std::string::npos);
	EXPECT_NE(Source.find("VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM, true, false"), std::string::npos);
}
