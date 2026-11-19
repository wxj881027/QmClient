#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <type_traits>
#include <vector>

namespace vulkan_swapchain
{
	VkResult Replace(VkDevice Device, VkSwapchainCreateInfoKHR Info, VkSwapchainKHR &SwapChain, PFN_vkCreateSwapchainKHR pfnCreate, PFN_vkDestroySwapchainKHR pfnDestroy);
	int Rebuild(const std::function<int()> &Build, const std::function<void()> &Rollback, const std::function<bool()> &RefreshSurface, bool &RecoveredWithNewSurface);
	bool ContainsPixel(VkExtent2D Extent, VkOffset2D Position);
	VkSampleCountFlagBits SelectSampleCount(VkSampleCountFlags Supported, uint32_t Requested);
}

namespace vulkan_memory
{
	std::optional<uint32_t> FindType(const VkPhysicalDeviceMemoryProperties &Memory, uint32_t TypeBits, VkMemoryPropertyFlags Properties);
}

namespace
{
	template<typename THandle>
	THandle Handle(uintptr_t Value)
	{
		if constexpr(std::is_pointer_v<THandle>)
			return reinterpret_cast<THandle>(Value);
		else
			return static_cast<THandle>(Value);
	}

	struct SSwapChainDriver
	{
		VkSwapchainKHR m_ExpectedOld = Handle<VkSwapchainKHR>(1);
		VkSwapchainKHR m_New = Handle<VkSwapchainKHR>(2);
		VkResult m_Result = VK_SUCCESS;
		bool m_CreateCalled = false;
		std::vector<VkSwapchainKHR> m_vDestroyed;

		static VKAPI_ATTR VkResult VKAPI_CALL Create(VkDevice Device, const VkSwapchainCreateInfoKHR *pInfo, const VkAllocationCallbacks *, VkSwapchainKHR *pNew)
		{
			auto &Driver = *reinterpret_cast<SSwapChainDriver *>(Device);
			EXPECT_EQ(pInfo->oldSwapchain, Driver.m_ExpectedOld);
			EXPECT_TRUE(Driver.m_vDestroyed.empty());
			Driver.m_CreateCalled = true;
			if(Driver.m_Result == VK_SUCCESS)
				*pNew = Driver.m_New;
			return Driver.m_Result;
		}

		static VKAPI_ATTR void VKAPI_CALL Destroy(VkDevice Device, VkSwapchainKHR SwapChain, const VkAllocationCallbacks *)
		{
			auto &Driver = *reinterpret_cast<SSwapChainDriver *>(Device);
			EXPECT_TRUE(Driver.m_CreateCalled);
			Driver.m_vDestroyed.push_back(SwapChain);
		}

		VkResult Replace(VkSwapchainKHR &SwapChain)
		{
			VkSwapchainCreateInfoKHR Info{};
			Info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
			return vulkan_swapchain::Replace(reinterpret_cast<VkDevice>(this), Info, SwapChain, Create, Destroy);
		}
	};
}

TEST(VulkanSwapChainRecovery, SuccessfulReplacementReleasesRetiredSwapChainAndPublishesNewHandle)
{
	SSwapChainDriver Driver;
	VkSwapchainKHR SwapChain = Driver.m_ExpectedOld;
	EXPECT_EQ(Driver.Replace(SwapChain), VK_SUCCESS);
	EXPECT_EQ(SwapChain, Driver.m_New);
	EXPECT_EQ(Driver.m_vDestroyed, std::vector<VkSwapchainKHR>{Driver.m_ExpectedOld});
}

TEST(VulkanSwapChainRecovery, FailedReplacementStillReleasesRetiredSwapChain)
{
	SSwapChainDriver Driver;
	Driver.m_Result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
	VkSwapchainKHR SwapChain = Driver.m_ExpectedOld;
	EXPECT_EQ(Driver.Replace(SwapChain), VK_ERROR_OUT_OF_DEVICE_MEMORY);
	EXPECT_EQ(SwapChain, VK_NULL_HANDLE);
	EXPECT_EQ(Driver.m_vDestroyed, std::vector<VkSwapchainKHR>{Driver.m_ExpectedOld});
}

TEST(VulkanSwapChainRecovery, FirstBuildWithoutPreviousSwapChainDoesNotDestroyAHandle)
{
	SSwapChainDriver Driver;
	Driver.m_ExpectedOld = VK_NULL_HANDLE;
	VkSwapchainKHR SwapChain = VK_NULL_HANDLE;
	EXPECT_EQ(Driver.Replace(SwapChain), VK_SUCCESS);
	EXPECT_EQ(SwapChain, Driver.m_New);
	EXPECT_TRUE(Driver.m_vDestroyed.empty());
}

TEST(VulkanSwapChainRecovery, PartialBuildIsRolledBackBeforeReplacingSurface)
{
	int Builds = 0;
	int Rollbacks = 0;
	bool ResourcesLive = false;
	bool RecoveredWithNewSurface = false;
	const int Result = vulkan_swapchain::Rebuild(
		[&] {
			EXPECT_FALSE(ResourcesLive);
			ResourcesLive = true;
			return ++Builds == 1 ? -1 : 0;
		},
		[&] {
			EXPECT_TRUE(ResourcesLive);
			ResourcesLive = false;
			++Rollbacks;
		},
		[&] {
			EXPECT_FALSE(ResourcesLive);
			return true;
		},
		RecoveredWithNewSurface);
	EXPECT_EQ(Result, 0);
	EXPECT_EQ(Builds, 2);
	EXPECT_EQ(Rollbacks, 1);
	EXPECT_TRUE(ResourcesLive);
	EXPECT_TRUE(RecoveredWithNewSurface);
}

TEST(VulkanSwapChainRecovery, FailedSecondBuildAlsoRollsBackItsResources)
{
	int Builds = 0;
	int Rollbacks = 0;
	bool RecoveredWithNewSurface = true;
	const int Result = vulkan_swapchain::Rebuild(
		[&] { return ++Builds == 1 ? -1 : -2; },
		[&] { ++Rollbacks; },
		[] { return true; },
		RecoveredWithNewSurface);
	EXPECT_EQ(Result, -2);
	EXPECT_EQ(Builds, 2);
	EXPECT_EQ(Rollbacks, 2);
	EXPECT_FALSE(RecoveredWithNewSurface);
}

TEST(VulkanSwapChainRecovery, FailedSurfaceRefreshKeepsFirstErrorWithoutRetryingBuild)
{
	int Builds = 0;
	int Rollbacks = 0;
	bool RecoveredWithNewSurface = true;
	const int Result = vulkan_swapchain::Rebuild(
		[&] {
			++Builds;
			return -1;
		},
		[&] { ++Rollbacks; },
		[] { return false; },
		RecoveredWithNewSurface);
	EXPECT_EQ(Result, -1);
	EXPECT_EQ(Builds, 1);
	EXPECT_EQ(Rollbacks, 1);
	EXPECT_FALSE(RecoveredWithNewSurface);
}

TEST(VulkanSwapChainRecovery, SuccessfulFirstBuildKeepsItsResourcesAndSurface)
{
	bool RecoveredWithNewSurface = true;
	const int Result = vulkan_swapchain::Rebuild(
		[] { return 0; },
		[] { ADD_FAILURE() << "Successful build must retain its resources."; },
		[] {
			ADD_FAILURE() << "Successful build must retain the original surface.";
			return false;
		},
		RecoveredWithNewSurface);
	EXPECT_EQ(Result, 0);
	EXPECT_FALSE(RecoveredWithNewSurface);
}

TEST(VulkanMemoryType, PrefersCompatibleHostCachedMemory)
{
	VkPhysicalDeviceMemoryProperties Memory{};
	Memory.memoryTypeCount = 2;
	Memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	Memory.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

	const auto Selected = vulkan_memory::FindType(Memory, 0b11, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
	ASSERT_TRUE(Selected);
	EXPECT_EQ(*Selected, 1u);
}

TEST(VulkanMemoryType, UsesCompatibleUncachedHostMemoryWhenCachedTypeIsUnavailable)
{
	VkPhysicalDeviceMemoryProperties Memory{};
	Memory.memoryTypeCount = 3;
	Memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	Memory.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	Memory.memoryTypes[2].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

	const auto Selected = vulkan_memory::FindType(Memory, 0b011, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
	ASSERT_TRUE(Selected);
	EXPECT_EQ(*Selected, 1u);
}

TEST(VulkanMemoryType, RejectsAllocationWhenSupportedTypesLackRequiredProperties)
{
	VkPhysicalDeviceMemoryProperties Memory{};
	Memory.memoryTypeCount = 2;
	Memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	Memory.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	EXPECT_FALSE(vulkan_memory::FindType(Memory, 0b10, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
}

TEST(VulkanSwapChainReadback, UsesSubmittedImageExtentAfterWindowGrows)
{
	const VkExtent2D SnapshotExtent{800, 600};
	EXPECT_TRUE(vulkan_swapchain::ContainsPixel(SnapshotExtent, {799, 599}));
	EXPECT_FALSE(vulkan_swapchain::ContainsPixel(SnapshotExtent, {800, 599}));
	EXPECT_FALSE(vulkan_swapchain::ContainsPixel(SnapshotExtent, {799, 600}));
	EXPECT_FALSE(vulkan_swapchain::ContainsPixel(SnapshotExtent, {-1, 0}));
	EXPECT_FALSE(vulkan_swapchain::ContainsPixel(SnapshotExtent, {0, -1}));
}

TEST(VulkanSwapChainMultisampling, SelectsOnlySupportedSamplesAtOrBelowRequest)
{
	const VkSampleCountFlags Supported = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_16_BIT;
	EXPECT_EQ(vulkan_swapchain::SelectSampleCount(Supported, 2), VK_SAMPLE_COUNT_1_BIT);
	EXPECT_EQ(vulkan_swapchain::SelectSampleCount(Supported, 8), VK_SAMPLE_COUNT_4_BIT);
	EXPECT_EQ(vulkan_swapchain::SelectSampleCount(Supported, 16), VK_SAMPLE_COUNT_16_BIT);
}
