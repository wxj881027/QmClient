#ifndef ENGINE_CLIENT_BACKEND_VULKAN_VULKAN_RENDERING_LIFECYCLE_H
#define ENGINE_CLIENT_BACKEND_VULKAN_VULKAN_RENDERING_LIFECYCLE_H

#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <cstddef>
#include <vector>

// CLEAR/LOAD 共用此依赖；依赖差异也会破坏 render pass 兼容性。
inline VkSubpassDependency QmVulkanSwapPassDependency()
{
	VkSubpassDependency Dependency{};
	Dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	Dependency.dstSubpass = 0;
	Dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
	Dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
	Dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	Dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
	return Dependency;
}

inline bool QmVulkanPipelineFailureIsFatal(VkResult Result, bool Required)
{
	return Required || Result == VK_ERROR_DEVICE_LOST;
}

template<typename TPipeline>
TPipeline &SelectQmVulkanPipeline(bool TargetActive, bool TargetOnly, TPipeline &SwapPipeline, TPipeline &TargetPipeline)
{
	// 专用于离屏的模糊管线已经放在主槽位；普通管线必须选择离屏变体。
	return TargetActive && !TargetOnly ? TargetPipeline : SwapPipeline;
}

template<typename TCreate>
VkResult CreateQmVulkanGraphicsPipelines(const VkGraphicsPipelineCreateInfo &Info, VkRenderPass TargetPass, VkPipeline &Pipeline, VkPipeline &TargetPipeline, TCreate &&Create)
{
	VkResult Result = Create(Info, Pipeline);
	if(Result != VK_SUCCESS || TargetPass == VK_NULL_HANDLE)
		return Result;
	// 布局和 shader 共用，离屏附件始终单采样，不能继承交换链的 MSAA。
	VkPipelineMultisampleStateCreateInfo TargetMultisampling = *Info.pMultisampleState;
	TargetMultisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	VkGraphicsPipelineCreateInfo TargetInfo = Info;
	TargetInfo.renderPass = TargetPass;
	TargetInfo.pMultisampleState = &TargetMultisampling;
	return Create(TargetInfo, TargetPipeline);
}

template<typename TWait, typename TReclaim, typename TAllocate>
VkResult RetryQmVulkanAllocation(VkResult Result, bool CanRecover, TWait &&Wait, TReclaim &&Reclaim, TAllocate &&Allocate)
{
	if(!CanRecover || (Result != VK_ERROR_OUT_OF_HOST_MEMORY && Result != VK_ERROR_OUT_OF_DEVICE_MEMORY))
		return Result;
	Result = Wait();
	if(Result != VK_SUCCESS)
		return Result;
	Reclaim();
	return Allocate();
}

template<typename TCleanup, typename TInit>
int ReinitializeQmVulkanFrameResources(int SwapResult, bool ImageCountChanged, TCleanup &&Cleanup, TInit &&Init)
{
	if(SwapResult != 0 || !ImageCountChanged)
		return SwapResult;
	Cleanup();
	return Init();
}

template<typename TRelease>
void ReclaimQmVulkanCompletedFrames(size_t FrameCount, size_t RecordingImageIndex, TRelease &&Release)
{
	for(size_t ImageIndex = 0; ImageIndex < FrameCount; ++ImageIndex)
		if(ImageIndex != RecordingImageIndex)
			Release(ImageIndex);
}

template<typename TFree>
void FreeQmVulkanCommandBuffers(VkCommandPool Pool, std::vector<VkCommandBuffer> &Buffers, TFree &&Free)
{
	Buffers.erase(std::remove(Buffers.begin(), Buffers.end(), VkCommandBuffer{}), Buffers.end());
	if(Pool != VK_NULL_HANDLE && !Buffers.empty())
		Free(Pool, static_cast<uint32_t>(Buffers.size()), Buffers.data());
	Buffers.clear();
}

template<typename TBarrier, typename TCopy>
void RecordQmVulkanSnapshotCopy(VkImage SwapImage, VkImage Snapshot, VkExtent2D Extent, bool SnapshotInitialized, TBarrier &&Barrier, TCopy &&Copy)
{
	VkImageMemoryBarrier Source{};
	Source.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	Source.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	Source.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	Source.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	Source.image = SwapImage;
	Source.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	Source.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	Source.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	Source.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	Barrier(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, Source);

	VkImageMemoryBarrier Destination = Source;
	Destination.image = Snapshot;
	Destination.oldLayout = SnapshotInitialized ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
	Destination.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	Destination.srcAccessMask = SnapshotInitialized ? VK_ACCESS_TRANSFER_READ_BIT : 0;
	Destination.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	Barrier(SnapshotInitialized ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, Destination);

	VkImageCopy Region{};
	Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
	Region.dstSubresource = Region.srcSubresource;
	Region.extent = {Extent.width, Extent.height, 1};
	Copy(SwapImage, Snapshot, Region);

	Destination.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	Destination.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	Destination.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	Destination.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, Destination);
	Source.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	Source.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	Source.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	Source.dstAccessMask = 0;
	Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, Source);
}

#endif
