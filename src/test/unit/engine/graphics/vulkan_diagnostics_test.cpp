#include <engine/client/backend/vulkan/vulkan_diagnostics.h>

#include <gtest/gtest.h>

#include <new>

namespace
{
	class VulkanDiagnostics : public ::testing::Test
	{
	protected:
		SGfxErrorContainer m_Error;
		SGfxWarningContainer m_Warning;
		std::atomic<bool> m_HasError{false};
	};
}

TEST_F(VulkanDiagnostics, RuntimeFailureIsVisibleBeforeBuildingTheReport)
{
	vulkan_diagnostics::PublishError(m_Error, m_Warning, m_HasError, true, GFX_ERROR_TYPE_RENDER_RECORDING, [&](bool First) {
		EXPECT_TRUE(First);
		EXPECT_TRUE(m_HasError);
		EXPECT_EQ(m_Error.m_ErrorType, GFX_ERROR_TYPE_RENDER_RECORDING);
		m_Error.m_vErrors.push_back({false, "recording failed"});
	});
	ASSERT_EQ(m_Error.m_vErrors.size(), 1U);
	EXPECT_EQ(m_Error.m_vErrors[0].m_Err, "recording failed");
	EXPECT_EQ(m_Warning.m_WarningType, GFX_WARNING_TYPE_NONE);
}

TEST_F(VulkanDiagnostics, ReportAllocationFailureStillStopsRendering)
{
	vulkan_diagnostics::PublishError(m_Error, m_Warning, m_HasError, true, GFX_ERROR_TYPE_OUT_OF_MEMORY_BUFFER, [](bool) { throw std::bad_alloc(); });
	EXPECT_TRUE(m_HasError);
	EXPECT_EQ(m_Error.m_ErrorType, GFX_ERROR_TYPE_OUT_OF_MEMORY_BUFFER);
	EXPECT_TRUE(m_Error.m_vErrors.empty());
}

TEST_F(VulkanDiagnostics, LaterReportFailureKeepsTheFirstErrorAndExistingDetails)
{
	vulkan_diagnostics::PublishError(m_Error, m_Warning, m_HasError, true, GFX_ERROR_TYPE_RENDER_RECORDING, [&](bool) {
		m_Error.m_vErrors.push_back({false, "first failure"});
	});
	vulkan_diagnostics::PublishError(m_Error, m_Warning, m_HasError, true, GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, [&](bool First) {
		EXPECT_FALSE(First);
		throw std::bad_alloc();
	});
	EXPECT_TRUE(m_HasError);
	EXPECT_EQ(m_Error.m_ErrorType, GFX_ERROR_TYPE_RENDER_RECORDING);
	ASSERT_EQ(m_Error.m_vErrors.size(), 1U);
	EXPECT_EQ(m_Error.m_vErrors[0].m_Err, "first failure");
}

TEST_F(VulkanDiagnostics, UnexpectedReportExceptionRetainsTheFailureMarker)
{
	vulkan_diagnostics::PublishError(m_Error, m_Warning, m_HasError, true, GFX_ERROR_TYPE_UNKNOWN, [&](bool) {
		m_Error.m_vErrors.push_back({false, "partial detail"});
		throw 7;
	});
	EXPECT_TRUE(m_HasError);
	EXPECT_EQ(m_Error.m_ErrorType, GFX_ERROR_TYPE_UNKNOWN);
	EXPECT_EQ(m_Error.m_vErrors.size(), 1U);
}

TEST_F(VulkanDiagnostics, InitializationFailureIsPublishedAsAWarningBeforeReportFailure)
{
	vulkan_diagnostics::PublishError(m_Error, m_Warning, m_HasError, false, GFX_ERROR_TYPE_INIT, [&](bool First) {
		EXPECT_FALSE(First);
		EXPECT_FALSE(m_HasError);
		EXPECT_EQ(m_Error.m_ErrorType, GFX_ERROR_TYPE_NONE);
		EXPECT_EQ(m_Warning.m_WarningType, GFX_WARNING_TYPE_INIT_FAILED);
		throw std::bad_alloc();
	});
	EXPECT_FALSE(m_HasError);
	EXPECT_EQ(m_Error.m_ErrorType, GFX_ERROR_TYPE_NONE);
	EXPECT_EQ(m_Warning.m_WarningType, GFX_WARNING_TYPE_INIT_FAILED);
}

TEST_F(VulkanDiagnostics, RuntimeErrorAfterInitializationWarningStillHasAFirstError)
{
	vulkan_diagnostics::PublishError(m_Error, m_Warning, m_HasError, false, GFX_ERROR_TYPE_INIT, [](bool) {});
	vulkan_diagnostics::PublishError(m_Error, m_Warning, m_HasError, true, GFX_ERROR_TYPE_SWAP_FAILED, [&](bool First) { EXPECT_TRUE(First); });
	EXPECT_TRUE(m_HasError);
	EXPECT_EQ(m_Error.m_ErrorType, GFX_ERROR_TYPE_SWAP_FAILED);
}

TEST_F(VulkanDiagnostics, WarningTypeSurvivesReportAllocationFailure)
{
	vulkan_diagnostics::PublishWarning(m_Warning, GFX_WARNING_MISSING_EXTENSION, [&] {
		EXPECT_EQ(m_Warning.m_WarningType, GFX_WARNING_MISSING_EXTENSION);
		throw std::bad_alloc();
	});
	EXPECT_EQ(m_Warning.m_WarningType, GFX_WARNING_MISSING_EXTENSION);
	EXPECT_TRUE(m_Warning.m_vWarnings.empty());
	EXPECT_FALSE(m_HasError);
}

TEST_F(VulkanDiagnostics, AWarningReportCanBeAddedAfterAnEarlierFailure)
{
	vulkan_diagnostics::PublishWarning(m_Warning, GFX_WARNING_MISSING_EXTENSION, [] { throw 7; });
	vulkan_diagnostics::PublishWarning(m_Warning, GFX_WARNING_LOW_ON_MEMORY, [&] { m_Warning.m_vWarnings.emplace_back("memory warning"); });
	EXPECT_EQ(m_Warning.m_WarningType, GFX_WARNING_LOW_ON_MEMORY);
	ASSERT_EQ(m_Warning.m_vWarnings.size(), 1U);
	EXPECT_EQ(m_Warning.m_vWarnings[0], "memory warning");
}
