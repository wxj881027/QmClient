#include "qmclient_source_contract_test.h"

#include <engine/client/backend/vulkan/backend_vulkan.h>
#include <engine/client/backend_sdl.h>

#include <gtest/gtest.h>

TEST(QmGraphicsOpenGlProbeContract, UsesRuntimeContextDetection)
{
	const SOpenGLVersion AutoGL = AutoOpenGLProbeVersion(EBackendType::BACKEND_TYPE_OPENGL);
	const SOpenGLVersion AutoGLES = AutoOpenGLProbeVersion(EBackendType::BACKEND_TYPE_OPENGL_ES);
	EXPECT_EQ(AutoGL.m_Major, 4);
#if defined(CONF_PLATFORM_MACOS)
	EXPECT_EQ(AutoGL.m_Minor, 1);
#else
	EXPECT_EQ(AutoGL.m_Minor, 6);
#endif
	EXPECT_EQ(AutoGLES.m_Major, 3);
	EXPECT_EQ(AutoGLES.m_Minor, 0);
	EXPECT_TRUE(IsOpenGLVersionAtLeast({4, 6, 0}, {4, 5, 0}));
	EXPECT_FALSE(IsOpenGLVersionAtLeast({4, 5, 0}, {4, 6, 0}));
	EXPECT_TRUE(IsOpenGLVersionAtLeast({3, 3, 0}, {3, 3, 0}));
	EXPECT_FALSE(IsOpenGLVersionAtLeast({1, 2, 0}, {1, 2, 1}));
	SOpenGLVersion ProbeVersion{4, 6, 0};
	for(int Minor = 5; Minor >= 0; --Minor)
	{
		EXPECT_TRUE(NextAutoOpenGLProbeVersion(ProbeVersion));
		EXPECT_EQ(ProbeVersion.m_Major, 4);
		EXPECT_EQ(ProbeVersion.m_Minor, Minor);
	}
	EXPECT_TRUE(NextAutoOpenGLProbeVersion(ProbeVersion));
	EXPECT_EQ(ProbeVersion.m_Major, 3);
	EXPECT_EQ(ProbeVersion.m_Minor, 3);
	EXPECT_TRUE(NextAutoOpenGLProbeVersion(ProbeVersion));
	EXPECT_EQ(ProbeVersion.m_Minor, 2);
	EXPECT_TRUE(ShouldSyncActualOpenGLVersion(EBackendType::BACKEND_TYPE_OPENGL, {3, 3, 0}, {4, 1, 0}));
	EXPECT_TRUE(ShouldSyncActualOpenGLVersion(EBackendType::BACKEND_TYPE_OPENGL, {4, 6, 0}, {4, 1, 0}));
	EXPECT_FALSE(ShouldSyncActualOpenGLVersion(EBackendType::BACKEND_TYPE_OPENGL, {3, 0, 0}, {4, 1, 0}));
	EXPECT_FALSE(ShouldSyncActualOpenGLVersion(EBackendType::BACKEND_TYPE_VULKAN, {3, 3, 0}, {4, 1, 0}));
}
