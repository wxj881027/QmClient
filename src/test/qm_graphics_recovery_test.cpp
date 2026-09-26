#include <engine/client/backend/graphics_backend_contract.h>

#include <gtest/gtest.h>

TEST(QmGraphicsRecovery, WindowedModeRemainsWindowed)
{
	EXPECT_EQ(graphics_backend::RecoveryFullscreenMode(0), 0);
}

TEST(QmGraphicsRecovery, FullscreenModesUseDesktopFullscreen)
{
	EXPECT_EQ(graphics_backend::RecoveryFullscreenMode(1), 2);
	EXPECT_EQ(graphics_backend::RecoveryFullscreenMode(2), 2);
	EXPECT_EQ(graphics_backend::RecoveryFullscreenMode(3), 2);
}

TEST(QmGraphicsRecovery, CrashReportIdentifiesActualBackend)
{
	EXPECT_EQ(graphics_backend::BackendFromCrashReport("Graphics backend: Vulkan 1.4.0\nGraphics error:\nDevice lost\n"), BACKEND_TYPE_VULKAN);
	EXPECT_EQ(graphics_backend::BackendFromCrashReport("Exception module: opengl32.dll\r\nGraphics backend: OpenGL\r\n"), BACKEND_TYPE_OPENGL);
	EXPECT_EQ(graphics_backend::BackendFromCrashReport("Graphics backend: OpenGL ES 3.0.0\n"), BACKEND_TYPE_OPENGL_ES);
	EXPECT_EQ(graphics_backend::BackendFromCrashReport("Graphics backend: GLES 3.0.0\n"), BACKEND_TYPE_OPENGL_ES);
	EXPECT_EQ(graphics_backend::BackendFromCrashReport("Graphics backend: Metal 3.0.0\n"), BACKEND_TYPE_METAL);
	EXPECT_EQ(graphics_backend::BackendFromCrashReport("Report type: graphics_fatal_error\nConfigured graphics backend: Vulkan API 1.4\n"), BACKEND_TYPE_VULKAN);
	EXPECT_EQ(graphics_backend::BackendFromCrashReport("Report type: graphics_fatal_error\nGraphics backend: Vulkan\nConfigured graphics backend: OpenGL\nGraphics error:\nVK_ERROR_DEVICE_LOST\n"), BACKEND_TYPE_VULKAN);
	EXPECT_EQ(graphics_backend::BackendFromCrashReport("Graphics backend: headless\n"), BACKEND_TYPE_AUTO);
	EXPECT_EQ(graphics_backend::BackendFromCrashReport("Graphics error: Vulkan\n"), BACKEND_TYPE_AUTO);
}

TEST(QmGraphicsRecovery, RepeatedFailuresBlockOnlyTheReportedBackend)
{
	graphics_backend::SRecoveryFailures Failures;
	Failures.Record(BACKEND_TYPE_OPENGL);
	EXPECT_FALSE(Failures.IsBlocked(BACKEND_TYPE_OPENGL));
	Failures.Record(BACKEND_TYPE_OPENGL);
	Failures.Record(BACKEND_TYPE_OPENGL);
	EXPECT_EQ(Failures.m_aCount[BACKEND_TYPE_OPENGL], 3);
	EXPECT_TRUE(Failures.IsBlocked(BACKEND_TYPE_OPENGL));
	EXPECT_FALSE(Failures.IsBlocked(BACKEND_TYPE_VULKAN));
	Failures.Record(BACKEND_TYPE_AUTO);
	EXPECT_FALSE(Failures.IsBlocked(BACKEND_TYPE_AUTO));
}

TEST(QmGraphicsRecovery, RecoveryAvoidsCrashedAndRepeatedlyFailedCandidates)
{
	graphics_backend::SRecoveryFailures Failures;
	const EBackendType Compatibility = graphics_backend::ParseBackendName(
		graphics_backend::BackendNameForGraphicsMode(graphics_backend::GRAPHICS_MODE_COMPATIBILITY), BACKEND_TYPE_AUTO);
	const EBackendType Performance = graphics_backend::ParseBackendName(
		graphics_backend::BackendNameForGraphicsMode(graphics_backend::GRAPHICS_MODE_PERFORMANCE), BACKEND_TYPE_AUTO);
	if(Compatibility == Performance)
	{
		EXPECT_EQ(graphics_backend::RecoveryBackend(Failures, Compatibility), BACKEND_TYPE_AUTO);
		return;
	}

	EXPECT_EQ(graphics_backend::RecoveryBackend(Failures, Performance), Compatibility);
	EXPECT_EQ(graphics_backend::ModeForRecoveryBackend(Compatibility), graphics_backend::GRAPHICS_MODE_COMPATIBILITY);
	EXPECT_EQ(graphics_backend::ModeForRecoveryBackend(Performance), graphics_backend::GRAPHICS_MODE_PERFORMANCE);
	EXPECT_EQ(graphics_backend::RecoveryBackend(Failures, BACKEND_TYPE_AUTO), BACKEND_TYPE_AUTO);
	Failures.Record(Compatibility);
	Failures.Record(Compatibility);
	EXPECT_EQ(graphics_backend::RecoveryBackend(Failures, Performance), BACKEND_TYPE_AUTO);
	EXPECT_EQ(graphics_backend::RecoveryBackend(Failures, Compatibility), Performance);
	Failures.Record(Performance);
	Failures.Record(Performance);
	EXPECT_EQ(graphics_backend::RecoveryBackend(Failures, Compatibility), BACKEND_TYPE_AUTO);
}
