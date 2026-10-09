#include <engine/client/backend/vulkan/vulkan_rendering_lifecycle.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>
#include <vector>

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
}

TEST(VulkanRenderingPipelines, SwapPassDependencyCoversLoadAfterAttachmentAndTransferReads)
{
	const auto Dependency = QmVulkanSwapPassDependency();
	EXPECT_EQ(Dependency.srcSubpass, VK_SUBPASS_EXTERNAL);
	EXPECT_EQ(Dependency.dstSubpass, 0u);
	EXPECT_NE(Dependency.srcStageMask & VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u);
	EXPECT_NE(Dependency.srcStageMask & VK_PIPELINE_STAGE_TRANSFER_BIT, 0u);
	EXPECT_NE(Dependency.srcAccessMask & VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0u);
	EXPECT_NE(Dependency.srcAccessMask & VK_ACCESS_TRANSFER_READ_BIT, 0u);
	EXPECT_NE(Dependency.dstAccessMask & VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, 0u);
	EXPECT_NE(Dependency.dstAccessMask & VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0u);
}

TEST(VulkanRenderingPipelines, TargetVariantUsesItsPassAndSingleSamplingWithoutChangingSwapInfo)
{
	VkPipelineMultisampleStateCreateInfo Multisampling{};
	Multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	Multisampling.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;
	VkGraphicsPipelineCreateInfo Info{};
	Info.renderPass = Handle<VkRenderPass>(1);
	Info.layout = Handle<VkPipelineLayout>(3);
	Info.pMultisampleState = &Multisampling;
	VkPipeline Pipeline = VK_NULL_HANDLE;
	VkPipeline Target = VK_NULL_HANDLE;
	std::vector<VkRenderPass> vPasses;
	std::vector<VkSampleCountFlagBits> vSamples;
	EXPECT_EQ(CreateQmVulkanGraphicsPipelines(Info, Handle<VkRenderPass>(2), Pipeline, Target,
		[&](const VkGraphicsPipelineCreateInfo &Created, VkPipeline &Output) {
			vPasses.push_back(Created.renderPass);
			vSamples.push_back(Created.pMultisampleState->rasterizationSamples);
			EXPECT_EQ(Created.layout, Info.layout);
			Output = Handle<VkPipeline>(vPasses.size());
			return VK_SUCCESS;
		}), VK_SUCCESS);
	EXPECT_EQ(vPasses, (std::vector<VkRenderPass>{Info.renderPass, Handle<VkRenderPass>(2)}));
	EXPECT_EQ(vSamples, (std::vector<VkSampleCountFlagBits>{VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_1_BIT}));
	EXPECT_EQ(Info.renderPass, Handle<VkRenderPass>(1));
	EXPECT_EQ(Multisampling.rasterizationSamples, VK_SAMPLE_COUNT_4_BIT);
	EXPECT_NE(Pipeline, Target);
}

TEST(VulkanRenderingPipelines, DefaultFailureStopsBeforeCreatingTargetVariant)
{
	VkGraphicsPipelineCreateInfo Info{};
	VkPipeline Pipeline = VK_NULL_HANDLE;
	VkPipeline Target = VK_NULL_HANDLE;
	int Calls = 0;
	EXPECT_EQ(CreateQmVulkanGraphicsPipelines(Info, Handle<VkRenderPass>(2), Pipeline, Target,
		[&](const auto &, auto &) { ++Calls; return VK_ERROR_OUT_OF_DEVICE_MEMORY; }), VK_ERROR_OUT_OF_DEVICE_MEMORY);
	EXPECT_EQ(Calls, 1);
}

TEST(VulkanRenderingPipelines, TargetFailureIsPropagated)
{
	VkPipelineMultisampleStateCreateInfo Multisampling{};
	VkGraphicsPipelineCreateInfo Info{};
	Info.pMultisampleState = &Multisampling;
	VkPipeline Pipeline = VK_NULL_HANDLE;
	VkPipeline Target = VK_NULL_HANDLE;
	int Calls = 0;
	EXPECT_EQ(CreateQmVulkanGraphicsPipelines(Info, Handle<VkRenderPass>(2), Pipeline, Target,
		[&](const auto &, auto &) { return ++Calls == 1 ? VK_SUCCESS : VK_ERROR_DEVICE_LOST; }), VK_ERROR_DEVICE_LOST);
	EXPECT_EQ(Calls, 2);
}

TEST(VulkanRenderingPipelines, DedicatedTargetPipelineIsCreatedOnce)
{
	VkGraphicsPipelineCreateInfo Info{};
	VkPipeline Pipeline = VK_NULL_HANDLE;
	VkPipeline Target = VK_NULL_HANDLE;
	int Calls = 0;
	EXPECT_EQ(CreateQmVulkanGraphicsPipelines(Info, VK_NULL_HANDLE, Pipeline, Target,
		[&](const auto &, auto &) { ++Calls; return VK_SUCCESS; }), VK_SUCCESS);
	EXPECT_EQ(Calls, 1);
}

TEST(VulkanRenderingPipelines, ActiveTargetSelectsTargetVariantAndEndRestoresSwapVariant)
{
	int Swap = 1;
	int Target = 2;
	EXPECT_EQ(&SelectQmVulkanPipeline(false, false, Swap, Target), &Swap);
	EXPECT_EQ(&SelectQmVulkanPipeline(true, false, Swap, Target), &Target);
	EXPECT_EQ(&SelectQmVulkanPipeline(false, false, Swap, Target), &Swap);
	EXPECT_EQ(&SelectQmVulkanPipeline(true, true, Swap, Target), &Swap);
}

TEST(VulkanRenderingPipelines, OptionalFailureCanFallbackButDeviceLossCannot)
{
	EXPECT_FALSE(QmVulkanPipelineFailureIsFatal(VK_ERROR_OUT_OF_HOST_MEMORY, false));
	EXPECT_FALSE(QmVulkanPipelineFailureIsFatal(VK_ERROR_INITIALIZATION_FAILED, false));
	EXPECT_TRUE(QmVulkanPipelineFailureIsFatal(VK_ERROR_OUT_OF_DEVICE_MEMORY, true));
	EXPECT_TRUE(QmVulkanPipelineFailureIsFatal(VK_ERROR_DEVICE_LOST, false));
}

TEST(VulkanRenderingRecovery, AllocationRetriesOnlyAfterIdleAndReclamation)
{
	std::vector<int> vEvents;
	EXPECT_EQ(RetryQmVulkanAllocation(VK_ERROR_OUT_OF_DEVICE_MEMORY, true,
		[&] { vEvents.push_back(1); return VK_SUCCESS; },
		[&] { vEvents.push_back(2); },
		[&] { vEvents.push_back(3); return VK_SUCCESS; }), VK_SUCCESS);
	EXPECT_EQ(vEvents, (std::vector<int>{1, 2, 3}));
}

TEST(VulkanRenderingRecovery, WorkerFailureDoesNotWaitReclaimOrRetry)
{
	int Calls = 0;
	EXPECT_EQ(RetryQmVulkanAllocation(VK_ERROR_OUT_OF_HOST_MEMORY, false,
		[&] { ++Calls; return VK_SUCCESS; }, [&] { ++Calls; }, [&] { ++Calls; return VK_SUCCESS; }), VK_ERROR_OUT_OF_HOST_MEMORY);
	EXPECT_EQ(Calls, 0);
}

TEST(VulkanRenderingRecovery, DeviceLossWhileWaitingStopsBeforeReclamation)
{
	int Calls = 0;
	EXPECT_EQ(RetryQmVulkanAllocation(VK_ERROR_OUT_OF_DEVICE_MEMORY, true,
		[] { return VK_ERROR_DEVICE_LOST; }, [&] { ++Calls; }, [&] { ++Calls; return VK_SUCCESS; }), VK_ERROR_DEVICE_LOST);
	EXPECT_EQ(Calls, 0);
}

TEST(VulkanRenderingRecovery, RepeatedAllocationFailureDoesNotLoop)
{
	int Retries = 0;
	EXPECT_EQ(RetryQmVulkanAllocation(VK_ERROR_OUT_OF_DEVICE_MEMORY, true,
		[] { return VK_SUCCESS; }, [] {}, [&] { ++Retries; return VK_ERROR_OUT_OF_DEVICE_MEMORY; }), VK_ERROR_OUT_OF_DEVICE_MEMORY);
	EXPECT_EQ(Retries, 1);
}

TEST(VulkanRenderingRecovery, NonMemoryFailureDoesNotAttemptRecovery)
{
	int Calls = 0;
	EXPECT_EQ(RetryQmVulkanAllocation(VK_ERROR_DEVICE_LOST, true,
		[&] { ++Calls; return VK_SUCCESS; }, [&] { ++Calls; }, [&] { ++Calls; return VK_SUCCESS; }), VK_ERROR_DEVICE_LOST);
	EXPECT_EQ(Calls, 0);
}

TEST(VulkanRenderingRecovery, ReclamationKeepsCurrentRecordingFrameResources)
{
	std::vector<size_t> vReleased;
	ReclaimQmVulkanCompletedFrames(3, 1, [&](size_t ImageIndex) { vReleased.push_back(ImageIndex); });
	EXPECT_EQ(vReleased, (std::vector<size_t>{0, 2}));
}

TEST(VulkanRenderingRecovery, SingleRecordingFrameCannotBeReclaimed)
{
	int Calls = 0;
	ReclaimQmVulkanCompletedFrames(1, 0, [&](size_t) { ++Calls; });
	EXPECT_EQ(Calls, 0);
}

TEST(VulkanRenderingRecovery, PartialCommandBufferAllocationFreesOnlyCreatedHandles)
{
	std::vector<VkCommandBuffer> vBuffers{Handle<VkCommandBuffer>(1), VK_NULL_HANDLE, Handle<VkCommandBuffer>(2)};
	std::vector<VkCommandBuffer> vFreed;
	const VkCommandPool Pool = Handle<VkCommandPool>(3);
	FreeQmVulkanCommandBuffers(Pool, vBuffers, [&](VkCommandPool CreatedPool, uint32_t Count, const VkCommandBuffer *pBuffers) {
		EXPECT_EQ(CreatedPool, Pool);
		vFreed.assign(pBuffers, pBuffers + Count);
	});
	EXPECT_EQ(vFreed, (std::vector<VkCommandBuffer>{Handle<VkCommandBuffer>(1), Handle<VkCommandBuffer>(2)}));
	EXPECT_TRUE(vBuffers.empty());
	FreeQmVulkanCommandBuffers(Pool, vBuffers, [](VkCommandPool, uint32_t, const VkCommandBuffer *) { ADD_FAILURE(); });
}

TEST(VulkanRenderingRecovery, FailedAllocationBatchDoesNotCallFreeWithZeroHandles)
{
	std::vector<VkCommandBuffer> vBuffers(3, VK_NULL_HANDLE);
	FreeQmVulkanCommandBuffers(Handle<VkCommandPool>(1), vBuffers,
		[](VkCommandPool, uint32_t, const VkCommandBuffer *) { ADD_FAILURE(); });
	EXPECT_TRUE(vBuffers.empty());
}

TEST(VulkanRenderingRecovery, SwapFailureDoesNotReinitializeFrameResources)
{
	int Calls = 0;
	EXPECT_EQ(ReinitializeQmVulkanFrameResources(-2, true, [&] { ++Calls; }, [&] { ++Calls; return 0; }), -2);
	EXPECT_EQ(Calls, 0);
}

TEST(VulkanRenderingRecovery, ImageCountChangePropagatesFrameInitializationFailure)
{
	std::vector<int> vEvents;
	EXPECT_EQ(ReinitializeQmVulkanFrameResources(0, true,
		[&] { vEvents.push_back(1); }, [&] { vEvents.push_back(2); return -3; }), -3);
	EXPECT_EQ(vEvents, (std::vector<int>{1, 2}));
}

TEST(VulkanRenderingRecovery, UnchangedImageCountPreservesFrameResources)
{
	int Calls = 0;
	EXPECT_EQ(ReinitializeQmVulkanFrameResources(0, false, [&] { ++Calls; }, [&] { ++Calls; return -1; }), 0);
	EXPECT_EQ(Calls, 0);
}

TEST(VulkanRenderingReadback, SnapshotCopyPublishesOwnedImageAndRestoresSwapForPresentation)
{
	const VkImage Swap = Handle<VkImage>(1);
	const VkImage Snapshot = Handle<VkImage>(2);
	std::vector<VkImageMemoryBarrier> vBarriers;
	std::vector<int> vEvents;
	RecordQmVulkanSnapshotCopy(Swap, Snapshot, {1280, 720}, false,
		[&](VkPipelineStageFlags SourceStage, VkPipelineStageFlags, const VkImageMemoryBarrier &Barrier) {
			if(vBarriers.empty())
				EXPECT_EQ(SourceStage, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
			vEvents.push_back(1);
			vBarriers.push_back(Barrier);
		},
		[&](VkImage Source, VkImage Destination, const VkImageCopy &Region) {
			vEvents.push_back(2);
			EXPECT_EQ(Source, Swap);
			EXPECT_EQ(Destination, Snapshot);
			EXPECT_EQ(Region.extent.width, 1280u);
			EXPECT_EQ(Region.extent.height, 720u);
		});
	EXPECT_EQ(vEvents, (std::vector<int>{1, 1, 2, 1, 1}));
	ASSERT_EQ(vBarriers.size(), 4u);
	EXPECT_EQ(vBarriers[0].image, Swap);
	EXPECT_EQ(vBarriers[0].srcAccessMask, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
	EXPECT_EQ(vBarriers[1].oldLayout, VK_IMAGE_LAYOUT_UNDEFINED);
	EXPECT_EQ(vBarriers[2].image, Snapshot);
	EXPECT_EQ(vBarriers[2].newLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	EXPECT_EQ(vBarriers[2].srcAccessMask, VK_ACCESS_TRANSFER_WRITE_BIT);
	EXPECT_EQ(vBarriers[2].dstAccessMask, VK_ACCESS_TRANSFER_READ_BIT);
	EXPECT_EQ(vBarriers[3].image, Swap);
	EXPECT_EQ(vBarriers[3].newLayout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}

TEST(VulkanRenderingReadback, RepeatedCaptureWaitsForPreviousSnapshotReads)
{
	std::vector<VkImageMemoryBarrier> vBarriers;
	RecordQmVulkanSnapshotCopy(Handle<VkImage>(1), Handle<VkImage>(2), {32, 16}, true,
		[&](VkPipelineStageFlags, VkPipelineStageFlags, const VkImageMemoryBarrier &Barrier) { vBarriers.push_back(Barrier); },
		[](VkImage, VkImage, const VkImageCopy &) {});
	ASSERT_EQ(vBarriers.size(), 4u);
	EXPECT_EQ(vBarriers[1].oldLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	EXPECT_EQ(vBarriers[1].srcAccessMask, VK_ACCESS_TRANSFER_READ_BIT);
	EXPECT_EQ(vBarriers[1].dstAccessMask, VK_ACCESS_TRANSFER_WRITE_BIT);
}
