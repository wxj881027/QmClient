#include "qmclient_source_contract_test.h"

#include <gtest/gtest.h>

TEST(QmGraphicsBackendFallbackContract, UnavailableVulkanFallsBackToAutoDetectedOpenGl)
{
	const std::string Init = ExtractSourceFunctionBody(ReadRepoFile("src/engine/client/graphics_threaded.cpp"), "int CGraphics_Threaded::Init");
	EXPECT_NE(Init.find("Falling back to automatically detected OpenGL"), std::string::npos);
	EXPECT_NE(Init.find("RestoreAutomaticOpenGLConfig"), std::string::npos);
	EXPECT_NE(Init.find("IssueInit()"), std::string::npos);
}
