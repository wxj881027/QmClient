#include <engine/client/backend/vulkan/vulkan_result.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>

#include <limits>
#include <new>
#include <stdexcept>

namespace
{
	class VulkanResult : public ::testing::Test
	{
	protected:
		bool m_Recreate = false;
		const char *m_pWarning = nullptr;
		int m_Warnings = 0;
		int m_FaultProbes = 0;
		int m_FaultFailure = 0;

		const char *Describe(VkResult Result, bool Verbose = false)
		{
			return vulkan_diagnostics::DescribeResult(Result, Verbose, m_Recreate, [&](const char *pWarning) {
					m_pWarning = pWarning;
					++m_Warnings; }, [&] {
					++m_FaultProbes;
					if(m_FaultFailure == 1)
						throw std::bad_alloc();
					if(m_FaultFailure == 2)
						throw 7; });
		}
	};

	const char *DescribeWithoutDetails(VkResult Result, bool &Recreate)
	{
		return vulkan_diagnostics::DescribeResult(Result, false, Recreate, [](const char *) {}, [] {});
	}
}

TEST_F(VulkanResult, SuccessDoesNotEmitDiagnosticsOrRequestRecreation)
{
	EXPECT_EQ(Describe(VK_SUCCESS), nullptr);
	EXPECT_FALSE(m_Recreate);
	EXPECT_EQ(m_Warnings, 0);
	EXPECT_EQ(m_FaultProbes, 0);
}

TEST_F(VulkanResult, KnownFatalResultsRetainTheirDescriptionsAndCodes)
{
	const struct
	{
		VkResult m_Result;
		const char *m_pExpected;
	} aCases[] = {
		{VK_ERROR_OUT_OF_HOST_MEMORY, "host ran out of memory (VkResult -1)"},
		{VK_ERROR_OUT_OF_DEVICE_MEMORY, "device ran out of memory (VkResult -2)"},
		{VK_ERROR_INITIALIZATION_FAILED, "initialization failed for unknown reason. (VkResult -3)"},
		{VK_ERROR_INCOMPATIBLE_DRIVER, "no compatible driver found. Vulkan 1.1 is required. (VkResult -9)"},
	};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE((int)Case.m_Result);
		EXPECT_STREQ(Describe(Case.m_Result), Case.m_pExpected);
		EXPECT_FALSE(m_Recreate);
		EXPECT_EQ(m_Warnings, 0);
		EXPECT_EQ(m_FaultProbes, 0);
	}
}

TEST_F(VulkanResult, UnknownResultRetainsItsFullSignedNumericCode)
{
	EXPECT_STREQ(Describe(static_cast<VkResult>(std::numeric_limits<int>::min())), "unknown error (VkResult -2147483648)");
	EXPECT_STREQ(Describe(static_cast<VkResult>(std::numeric_limits<int>::max())), "unknown error (VkResult 2147483647)");
	EXPECT_FALSE(m_Recreate);
	EXPECT_EQ(m_Warnings, 0);
	EXPECT_EQ(m_FaultProbes, 0);
}

TEST_F(VulkanResult, DeviceLostRequestsOptionalFaultDetails)
{
	EXPECT_STREQ(Describe(VK_ERROR_DEVICE_LOST), "device lost (VkResult -4)");
	EXPECT_EQ(m_FaultProbes, 1);
	EXPECT_EQ(m_Warnings, 0);
	EXPECT_FALSE(m_Recreate);
}

TEST_F(VulkanResult, FaultAllocationFailureStillReturnsTheDeviceLostError)
{
	m_FaultFailure = 1;
	EXPECT_STREQ(Describe(VK_ERROR_DEVICE_LOST), "device lost (VkResult -4)");
	EXPECT_EQ(m_FaultProbes, 1);
	EXPECT_FALSE(m_Recreate);
}

TEST_F(VulkanResult, UnexpectedFaultExceptionStillReturnsTheDeviceLostError)
{
	m_FaultFailure = 2;
	EXPECT_STREQ(Describe(VK_ERROR_DEVICE_LOST), "device lost (VkResult -4)");
	EXPECT_EQ(m_FaultProbes, 1);
}

TEST_F(VulkanResult, LaterCallsStillDescribeTheirOwnResultsAfterFaultFailure)
{
	m_FaultFailure = 1;
	EXPECT_NE(Describe(VK_ERROR_DEVICE_LOST), nullptr);
	EXPECT_EQ(Describe(VK_SUCCESS), nullptr);
	EXPECT_STREQ(Describe(VK_ERROR_OUT_OF_HOST_MEMORY), "host ran out of memory (VkResult -1)");
	EXPECT_EQ(m_FaultProbes, 1);
}

TEST_F(VulkanResult, OutOfDateRequestsRecreationWithoutBecomingFatal)
{
	EXPECT_EQ(Describe(VK_ERROR_OUT_OF_DATE_KHR, true), nullptr);
	EXPECT_TRUE(m_Recreate);
	EXPECT_EQ(m_Warnings, 0);
	EXPECT_EQ(m_FaultProbes, 0);
}

TEST_F(VulkanResult, SuboptimalRequestsRecreationWithoutBecomingFatal)
{
	EXPECT_EQ(Describe(VK_SUBOPTIMAL_KHR, true), nullptr);
	EXPECT_TRUE(m_Recreate);
	EXPECT_EQ(m_Warnings, 0);
	EXPECT_EQ(m_FaultProbes, 0);
}

TEST_F(VulkanResult, OtherNonfatalResultsPreservePendingRecreation)
{
	m_Recreate = true;
	for(const VkResult Result : {VK_SUCCESS, VK_ERROR_SURFACE_LOST_KHR, VK_ERROR_NATIVE_WINDOW_IN_USE_KHR})
	{
		SCOPED_TRACE((int)Result);
		EXPECT_EQ(Describe(Result), nullptr);
		EXPECT_TRUE(m_Recreate);
		EXPECT_EQ(m_Warnings, 0);
		EXPECT_EQ(m_FaultProbes, 0);
	}
}

TEST_F(VulkanResult, MissingLayersAndExtensionsKeepTheInitializationWarningPath)
{
	EXPECT_EQ(Describe(VK_ERROR_LAYER_NOT_PRESENT), nullptr);
	EXPECT_STREQ(m_pWarning, "One Vulkan layer was not present. (try to disable them)");
	EXPECT_EQ(m_Warnings, 1);
	EXPECT_EQ(Describe(VK_ERROR_EXTENSION_NOT_PRESENT), nullptr);
	EXPECT_STREQ(m_pWarning, "One Vulkan extension was not present. (try to disable them)");
	EXPECT_EQ(m_Warnings, 2);
	EXPECT_FALSE(m_Recreate);
	EXPECT_EQ(m_FaultProbes, 0);
}

TEST(VulkanResultThreads, AnotherThreadsDiagnosticDoesNotReplaceTheReturnedText)
{
	bool Recreate = false;
	const char *pError = DescribeWithoutDetails(VK_ERROR_OUT_OF_HOST_MEMORY, Recreate);
	ASSERT_STREQ(pError, "host ran out of memory (VkResult -1)");
	ASSERT_TRUE(WaitForAsyncIo([] {
		bool WorkerRecreate = false;
		const char *pWorkerError = DescribeWithoutDetails(VK_ERROR_DEVICE_LOST, WorkerRecreate);
		if(str_comp(pWorkerError, "device lost (VkResult -4)") != 0 || WorkerRecreate)
			throw std::runtime_error("worker lost its Vulkan result");
	}));
	EXPECT_STREQ(pError, "host ran out of memory (VkResult -1)");
}
