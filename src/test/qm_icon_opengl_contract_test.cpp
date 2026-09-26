#include "qmclient_source_contract_test.h"

#include <gtest/gtest.h>

TEST(QmIconOpenGlContract, ClearsMsdfCapabilityWithProgramLifecycle)
{
	const std::string Backend = ReadRepoFile("src/engine/client/backend/opengl/backend_opengl.cpp");
	const std::string BackendHeader = ReadRepoFile("src/engine/client/backend/opengl/backend_opengl.h");
	const std::string ModernBackend = ReadRepoFile("src/engine/client/backend/opengl/backend_opengl3.cpp");
	EXPECT_NE(BackendHeader.find("SBackendCapabilities *m_pBackendCapabilities = nullptr;"), std::string::npos);
	EXPECT_NE(Backend.find("m_pBackendCapabilities = pCommand->m_pCapabilities;"), std::string::npos);
	EXPECT_NE(Backend.find("m_pBackendCapabilities->m_TexturedMsdf.store(false, std::memory_order_release);"), std::string::npos);
	const size_t Shutdown = ModernBackend.find("void CCommandProcessorFragment_OpenGL3_3::Cmd_Shutdown");
	ASSERT_NE(Shutdown, std::string::npos);
	const size_t ProgramDelete = ModernBackend.find("m_pTexturedMsdfProgram->DeleteProgram();", Shutdown);
	ASSERT_NE(ProgramDelete, std::string::npos);
	const std::string ShutdownSection = ModernBackend.substr(Shutdown, ProgramDelete - Shutdown);
	EXPECT_NE(ShutdownSection.find("m_pBackendCapabilities->m_TexturedMsdf.store(false, std::memory_order_release);"), std::string::npos);
	EXPECT_NE(ShutdownSection.find("m_pBackendCapabilities = nullptr;"), std::string::npos);
}
