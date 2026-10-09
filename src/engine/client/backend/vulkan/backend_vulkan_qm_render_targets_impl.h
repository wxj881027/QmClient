#ifndef ENGINE_CLIENT_BACKEND_VULKAN_BACKEND_VULKAN_QM_RENDER_TARGETS_IMPL_H
#define ENGINE_CLIENT_BACKEND_VULKAN_BACKEND_VULKAN_QM_RENDER_TARGETS_IMPL_H

// 仅由 backend_vulkan.cpp 在后端类定义后包含，共用其私有 GPU 资源与命令状态。

inline VkFormat CCommandProcessorFragment_Vulkan::RenderTargetReadbackFormat() const
{
	return VK_FORMAT_R8G8B8A8_UNORM;
}

inline bool CCommandProcessorFragment_Vulkan::CreateRenderTargetDescriptorSet(SRenderTarget &Target, size_t DescrIndex)
{
	auto &DescrSet = Target.m_aVKStandardTexturedDescrSets[DescrIndex];
	VkDescriptorSetAllocateInfo DesAllocInfo{};
	DesAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	if(!GetDescriptorPoolForAlloc(DesAllocInfo.descriptorPool, m_StandardTextureDescrPool, &DescrSet, 1, &m_FrameProfileStats))
		return false;
	DesAllocInfo.descriptorSetCount = 1;
	DesAllocInfo.pSetLayouts = &m_StandardTexturedDescriptorSetLayout;
	if(vkAllocateDescriptorSets(m_VKDevice, &DesAllocInfo, &DescrSet.m_Descriptor) != VK_SUCCESS)
	{
		FreeDescriptorSetFromPool(DescrSet);
		return false;
	}
	m_FrameProfileStats.m_DescriptorAllocations++;

	VkDescriptorImageInfo ImageInfo{};
	ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	ImageInfo.imageView = Target.m_ImageView;
	ImageInfo.sampler = m_aSamplers[SUPPORTED_SAMPLER_TYPE_CLAMP_TO_EDGE];

	VkWriteDescriptorSet DescriptorWrite{};
	DescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	DescriptorWrite.dstSet = DescrSet.m_Descriptor;
	DescriptorWrite.dstBinding = 0;
	DescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	DescriptorWrite.descriptorCount = 1;
	DescriptorWrite.pImageInfo = &ImageInfo;
	vkUpdateDescriptorSets(m_VKDevice, 1, &DescriptorWrite, 0, nullptr);
	return true;
}

inline void CCommandProcessorFragment_Vulkan::DestroyRenderTarget(SRenderTarget &Target)
{
	for(auto &DescrSet : Target.m_aVKStandardTexturedDescrSets)
	{
		FreeDescriptorSetFromPool(DescrSet);
		DescrSet = {};
	}
	if(Target.m_Framebuffer != VK_NULL_HANDLE)
		vkDestroyFramebuffer(m_VKDevice, Target.m_Framebuffer, nullptr);
	if(Target.m_ImageView != VK_NULL_HANDLE)
		vkDestroyImageView(m_VKDevice, Target.m_ImageView, nullptr);
	if(Target.m_Image != VK_NULL_HANDLE)
	{
		FreeImageMemBlock(Target.m_ImageMem);
		vkDestroyImage(m_VKDevice, Target.m_Image, nullptr);
	}
	Target = {};
}

inline bool CCommandProcessorFragment_Vulkan::SupportsRenderTargetReadback() const
{
	return m_VKRenderTargetRenderPass != VK_NULL_HANDLE;
}

inline bool CCommandProcessorFragment_Vulkan::SupportsRenderTargetGaussianBlur() const
{
	return SupportsRenderTargetReadback() && m_GaussianBlurPipelineValid;
}

inline bool CCommandProcessorFragment_Vulkan::SupportsBackbufferCapture() const
{
	// 截图路径只验证过单采样交换链；多采样附件虽可恢复，但读取流程仍保持保守限制。
	// 背板仅作为 blit 源，sRGB 格式也可由下方的格式能力检查准入。
	const bool CompatibleFormat = m_VKSurfFormat.format == VK_FORMAT_B8G8R8A8_UNORM || m_VKSurfFormat.format == VK_FORMAT_R8G8B8A8_UNORM ||
				      m_VKSurfFormat.format == VK_FORMAT_B8G8R8A8_SRGB || m_VKSurfFormat.format == VK_FORMAT_R8G8B8A8_SRGB;
	return SupportsRenderTargetReadback() && CompatibleFormat && m_OptimalSwapChainImageBlitting && m_OptimalRGBAImageBlitting;
}

inline const char *CCommandProcessorFragment_Vulkan::RenderTargetReadbackSupportReason() const
{
	if(m_VKRenderTargetRenderPass == VK_NULL_HANDLE)
		return "vulkan_render_target_pass_missing";
	return "supported";
}

inline void CCommandProcessorFragment_Vulkan::BeginSwapRenderPass(VkRenderPass RenderPass)
{
	auto &CommandBuffer = GetMainGraphicCommandBuffer();
	VkRenderPassBeginInfo RenderPassInfo{};
	RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	RenderPassInfo.renderPass = RenderPass;
	RenderPassInfo.framebuffer = m_vFramebufferList[m_CurImageIndex];
	RenderPassInfo.renderArea.offset = {0, 0};
	RenderPassInfo.renderArea.extent = m_VKSwapImgAndViewportExtent.m_SwapImageViewport;
	VkClearValue ClearColorVal = {{{m_aClearColor[0], m_aClearColor[1], m_aClearColor[2], m_aClearColor[3]}}};
	RenderPassInfo.clearValueCount = 1;
	RenderPassInfo.pClearValues = &ClearColorVal;
	const VkSubpassContents SubpassContents = (m_ThreadCount > 1 && !m_ForceSingleThreadedRender) ? VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS : VK_SUBPASS_CONTENTS_INLINE;
	vkCmdBeginRenderPass(CommandBuffer, &RenderPassInfo, SubpassContents);
	m_SwapRenderPassActive = true;
	for(auto &LastPipe : m_vLastPipeline)
		LastPipe = VK_NULL_HANDLE;
}

inline bool CCommandProcessorFragment_Vulkan::EndSwapRenderPassForExternalWork()
{
	FinishRenderThreads();
	if(m_HasError)
		return false;
	auto &CommandBuffer = GetMainGraphicCommandBuffer();
	if(!ExecutePendingRenderThreadCommandBuffers(CommandBuffer))
		return false;
	if(m_SwapRenderPassActive)
	{
		vkCmdEndRenderPass(CommandBuffer);
		m_SwapRenderPassActive = false;
	}
	m_ForceSingleThreadedRender = true;
	for(auto &LastPipe : m_vLastPipeline)
		LastPipe = VK_NULL_HANDLE;
	return true;
}

inline bool CCommandProcessorFragment_Vulkan::SubmitCurrentCommandsAndRestartSwapPass()
{
	if(!EndSwapRenderPassForExternalWork())
		return false;
	UploadNonFlushedBuffers<true>();
	if(m_HasError)
		return false;
	auto &CommandBuffer = GetMainGraphicCommandBuffer();
	if(!CheckVulkanResult(vkEndCommandBuffer(CommandBuffer), GFX_ERROR_TYPE_RENDER_RECORDING, "vkEndCommandBuffer (intermediate frame) failed."))
		return false;

	VkSubmitInfo SubmitInfo{};
	SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	SubmitInfo.commandBufferCount = 1;
	SubmitInfo.pCommandBuffers = &CommandBuffer;
	std::array<VkCommandBuffer, 2> aCommandBuffers = {};
	if(m_vUsedMemoryCommandBuffer[m_CurImageIndex])
	{
		auto &MemoryCommandBuffer = m_vMemoryCommandBuffers[m_CurImageIndex];
		if(!CheckVulkanResult(vkEndCommandBuffer(MemoryCommandBuffer), GFX_ERROR_TYPE_RENDER_RECORDING, "vkEndCommandBuffer (intermediate memory) failed."))
			return false;
		aCommandBuffers[0] = MemoryCommandBuffer;
		aCommandBuffers[1] = CommandBuffer;
		SubmitInfo.commandBufferCount = 2;
		SubmitInfo.pCommandBuffers = aCommandBuffers.data();
		m_vUsedMemoryCommandBuffer[m_CurImageIndex] = false;
	}
	std::array<VkSemaphore, 1> aWaitSemaphores = {m_AcquireImageSemaphore};
	std::array<VkPipelineStageFlags, 1> aWaitStages = {(VkPipelineStageFlags)VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	if(!m_AcquireSemaphoreConsumed)
	{
		SubmitInfo.waitSemaphoreCount = aWaitSemaphores.size();
		SubmitInfo.pWaitSemaphores = aWaitSemaphores.data();
		SubmitInfo.pWaitDstStageMask = aWaitStages.data();
		m_AcquireSemaphoreConsumed = true;
	}
	VkFence SubmitFence = m_vQueueSubmitFences[m_CurImageIndex];
	VkResult ResetSubmitFenceResult = vkResetFences(m_VKDevice, 1, &SubmitFence);
	if(ResetSubmitFenceResult != VK_SUCCESS)
	{
		const char *pCritErrorMsg = CheckVulkanCriticalError(ResetSubmitFenceResult);
		if(pCritErrorMsg != nullptr)
			SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Resetting intermediate graphics queue submit fence failed.", pCritErrorMsg);
		else
			SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Resetting intermediate graphics queue submit fence failed.");
		return false;
	}
	VkResult QueueSubmitRes = QueueSubmit(m_VKGraphicsQueue, 1, &SubmitInfo, SubmitFence);
	if(QueueSubmitRes != VK_SUCCESS)
	{
		const char *pCritErrorMsg = CheckVulkanCriticalError(QueueSubmitRes);
		if(pCritErrorMsg != nullptr)
			SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Submitting intermediate graphics queue command buffer failed.", pCritErrorMsg);
		else
			SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Submitting intermediate graphics queue command buffer failed.");
		return false;
	}
	VkResult WaitSubmitFenceResult = WaitForFences(1, &SubmitFence, VK_TRUE, std::numeric_limits<uint64_t>::max());
	if(WaitSubmitFenceResult != VK_SUCCESS)
	{
		const char *pCritErrorMsg = CheckVulkanCriticalError(WaitSubmitFenceResult);
		if(pCritErrorMsg != nullptr)
			SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Waiting for intermediate graphics queue submit fence failed.", pCritErrorMsg);
		else
			SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Waiting for intermediate graphics queue submit fence failed.");
		return false;
	}

	if(!CheckVulkanResult(vkResetCommandBuffer(CommandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT), GFX_ERROR_TYPE_RENDER_RECORDING, "vkResetCommandBuffer (intermediate frame) failed."))
		return false;
	VkCommandBufferBeginInfo BeginInfo{};
	BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if(!CheckVulkanResult(vkBeginCommandBuffer(CommandBuffer, &BeginInfo), GFX_ERROR_TYPE_RENDER_RECORDING, "vkBeginCommandBuffer (intermediate frame) failed."))
		return false;
	BeginSwapRenderPass(m_VKRenderPassLoad);
	// 命令缓冲已被重置，其中记录过的 index buffer、descriptor 与动态状态绑定
	// 全部不存在了；必须同步清空 CPU 侧缓存，否则后续绘制会因缓存命中而
	// 跳过重新绑定，产生无效绘制（Vulkan 校验层报未绑定资源，部分驱动
	// 表现为 device lost）。BeginSwapRenderPass 只清了管线缓存。
	ResetDrawCommandState(0);
	return true;
}

inline bool CCommandProcessorFragment_Vulkan::Cmd_RenderTarget_Create(const CCommandBuffer::SCommand_RenderTarget_Create *pCommand)
{
	if(pCommand->m_TargetId < 0 || pCommand->m_Width <= 0 || pCommand->m_Height <= 0 || m_VKRenderTargetRenderPass == VK_NULL_HANDLE)
		return true;
	const size_t TargetId = (size_t)pCommand->m_TargetId;
	while(TargetId >= m_vRenderTargets.size())
		m_vRenderTargets.resize((m_vRenderTargets.size() * 2) + 1);

	SRenderTarget &Target = m_vRenderTargets[TargetId];
	DestroyRenderTarget(Target);
	Target.m_Width = pCommand->m_Width;
	Target.m_Height = pCommand->m_Height;
	if(!CreateImage(Target.m_Width, Target.m_Height, 1, 1, RenderTargetReadbackFormat(), VK_IMAGE_TILING_OPTIMAL, Target.m_Image, Target.m_ImageMem, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT))
	{
		DestroyRenderTarget(Target);
		return false;
	}
	Target.m_ImageView = CreateImageView(Target.m_Image, RenderTargetReadbackFormat(), VK_IMAGE_VIEW_TYPE_2D, 1, 1);
	if(Target.m_ImageView == VK_NULL_HANDLE)
	{
		DestroyRenderTarget(Target);
		return false;
	}

	VkFramebufferCreateInfo FramebufferInfo{};
	FramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	FramebufferInfo.renderPass = m_VKRenderTargetRenderPass;
	FramebufferInfo.attachmentCount = 1;
	FramebufferInfo.pAttachments = &Target.m_ImageView;
	FramebufferInfo.width = Target.m_Width;
	FramebufferInfo.height = Target.m_Height;
	FramebufferInfo.layers = 1;
	if(vkCreateFramebuffer(m_VKDevice, &FramebufferInfo, nullptr, &Target.m_Framebuffer) != VK_SUCCESS)
	{
		DestroyRenderTarget(Target);
		return false;
	}
	if(!CreateRenderTargetDescriptorSet(Target, 0) || !CreateRenderTargetDescriptorSet(Target, 1))
	{
		DestroyRenderTarget(Target);
		return false;
	}
	return true;
}

inline bool CCommandProcessorFragment_Vulkan::Cmd_RenderTarget_Destroy(const CCommandBuffer::SCommand_RenderTarget_Destroy *pCommand)
{
	if(pCommand->m_TargetId < 0 || (size_t)pCommand->m_TargetId >= m_vRenderTargets.size())
		return true;
	if(m_RenderingPaused)
	{
		// 渲染暂停时 GPU 已空闲且无活跃录制，直接销毁即可，
		// 避免对已结束且已提交的交换链渲染通道再次调用 vkCmdEndRenderPass
		DestroyRenderTarget(m_vRenderTargets[pCommand->m_TargetId]);
		return true;
	}
	if(m_RenderTargetActive)
	{
		// 渲染目标渲染通道进行中：既不能中途提交当前命令缓冲（会打断渲染通道），
		// 也不能在渲染通道中间结束命令缓冲，延迟到 Cmd_RenderTarget_End 后统一销毁
		if(m_ActiveRenderTargetId == pCommand->m_TargetId)
			return true;
		m_vPendingRenderTargetDestroy.emplace_back(pCommand->m_TargetId, m_vRenderTargets[pCommand->m_TargetId].m_Image);
		return true;
	}
	if(m_vRenderTargets[pCommand->m_TargetId].m_Image != VK_NULL_HANDLE && !SubmitCurrentCommandsAndRestartSwapPass())
		return false;
	DestroyRenderTarget(m_vRenderTargets[pCommand->m_TargetId]);
	return true;
}

inline bool CCommandProcessorFragment_Vulkan::Cmd_RenderTarget_Begin(const CCommandBuffer::SCommand_RenderTarget_Begin *pCommand)
{
	if(m_RenderingPaused)
		return true;
	if(pCommand->m_TargetId < 0 || (size_t)pCommand->m_TargetId >= m_vRenderTargets.size() || m_RenderTargetActive)
		return true;
	SRenderTarget &Target = m_vRenderTargets[pCommand->m_TargetId];
	if(Target.m_Framebuffer == VK_NULL_HANDLE)
		return true;
	if(!EndSwapRenderPassForExternalWork())
		return false;
	auto &CommandBuffer = GetMainGraphicCommandBuffer();
	if(Target.m_Layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && !ImageBarrier(CommandBuffer, Target.m_Image, 0, 1, 0, 1, RenderTargetReadbackFormat(), Target.m_Layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL))
		return false;
	Target.m_Layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkRenderPassBeginInfo RenderPassInfo{};
	RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	RenderPassInfo.renderPass = m_VKRenderTargetRenderPass;
	RenderPassInfo.framebuffer = Target.m_Framebuffer;
	RenderPassInfo.renderArea.offset = {0, 0};
	RenderPassInfo.renderArea.extent = {Target.m_Width, Target.m_Height};
	VkClearValue ClearColorVal = {{{pCommand->m_ClearColor.r, pCommand->m_ClearColor.g, pCommand->m_ClearColor.b, pCommand->m_ClearColor.a}}};
	RenderPassInfo.clearValueCount = 1;
	RenderPassInfo.pClearValues = &ClearColorVal;
	vkCmdBeginRenderPass(CommandBuffer, &RenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	m_SavedHasDynamicViewport = m_HasDynamicViewport;
	m_SavedDynamicViewportOffset = m_DynamicViewportOffset;
	m_SavedDynamicViewportSize = m_DynamicViewportSize;
	m_HasDynamicViewport = true;
	m_DynamicViewportOffset = {0, 0};
	m_DynamicViewportSize = {Target.m_Width, Target.m_Height};
	m_RenderTargetActive = true;
	m_ActiveRenderTargetId = pCommand->m_TargetId;
	for(auto &LastPipe : m_vLastPipeline)
		LastPipe = VK_NULL_HANDLE;
	return true;
}

inline bool CCommandProcessorFragment_Vulkan::Cmd_RenderTarget_End(const CCommandBuffer::SCommand_RenderTarget_End *pCommand)
{
	(void)pCommand;
	if(m_RenderingPaused)
		return true;
	if(!m_RenderTargetActive || m_ActiveRenderTargetId < 0 || (size_t)m_ActiveRenderTargetId >= m_vRenderTargets.size())
		return true;
	auto &CommandBuffer = GetMainGraphicCommandBuffer();
	vkCmdEndRenderPass(CommandBuffer);
	SRenderTarget &Target = m_vRenderTargets[m_ActiveRenderTargetId];
	Target.m_Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	m_RenderTargetActive = false;
	m_ActiveRenderTargetId = -1;
	m_HasDynamicViewport = m_SavedHasDynamicViewport;
	m_DynamicViewportOffset = m_SavedDynamicViewportOffset;
	m_DynamicViewportSize = m_SavedDynamicViewportSize;
	BeginSwapRenderPass(m_VKRenderPassLoad);

	// 处理渲染目标渲染通道期间收到的销毁请求：先提交并等待 GPU 完成，再逐个销毁
	if(!m_vPendingRenderTargetDestroy.empty())
	{
		if(!SubmitCurrentCommandsAndRestartSwapPass())
			return false;
		for(const auto &PendingDestroy : m_vPendingRenderTargetDestroy)
		{
			if(PendingDestroy.first < m_vRenderTargets.size() && PendingDestroy.second != VK_NULL_HANDLE &&
				m_vRenderTargets[PendingDestroy.first].m_Image == PendingDestroy.second)
				DestroyRenderTarget(m_vRenderTargets[PendingDestroy.first]);
		}
		m_vPendingRenderTargetDestroy.clear();
	}
	return true;
}

inline bool CCommandProcessorFragment_Vulkan::Cmd_RenderTarget_Readback(const CCommandBuffer::SCommand_RenderTarget_Readback *pCommand)
{
	if(pCommand->m_TargetId < 0 || (size_t)pCommand->m_TargetId >= m_vRenderTargets.size() || pCommand->m_pImage == nullptr)
		return true;
	if(m_RenderingPaused)
		return true;
	SRenderTarget &Target = m_vRenderTargets[pCommand->m_TargetId];
	if(Target.m_Image == VK_NULL_HANDLE || Target.m_Width == 0 || Target.m_Height == 0)
		return true;
	if(!SubmitCurrentCommandsAndRestartSwapPass())
		return false;

	uint8_t *pResImageData;
	if(!PreparePresentedImageDataImage(pResImageData, Target.m_Width, Target.m_Height))
		return false;
	VkCommandBuffer *pCommandBuffer;
	if(!GetMemoryCommandBuffer(pCommandBuffer))
		return false;
	VkCommandBuffer &CommandBuffer = *pCommandBuffer;
	if(!ImageBarrier(CommandBuffer, Target.m_Image, 0, 1, 0, 1, RenderTargetReadbackFormat(), Target.m_Layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL))
		return false;
	Target.m_Layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	if(!ImageBarrier(CommandBuffer, m_GetPresentedImgDataHelperImage, 0, 1, 0, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
		return false;

	bool IsB8G8R8A8 = RenderTargetReadbackFormat() == VK_FORMAT_B8G8R8A8_UNORM || RenderTargetReadbackFormat() == VK_FORMAT_B8G8R8A8_SRGB;
	const bool IsR8G8B8A8 = RenderTargetReadbackFormat() == VK_FORMAT_R8G8B8A8_UNORM || RenderTargetReadbackFormat() == VK_FORMAT_R8G8B8A8_SRGB;
	if((IsR8G8B8A8 || IsB8G8R8A8) && m_OptimalSwapChainImageBlitting && m_OptimalRGBAImageBlitting && m_LinearRGBAImageBlitting)
	{
		VkImageBlit ImageBlitRegion{};
		ImageBlitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		ImageBlitRegion.srcSubresource.layerCount = 1;
		ImageBlitRegion.srcOffsets[1] = {(int32_t)Target.m_Width, (int32_t)Target.m_Height, 1};
		ImageBlitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		ImageBlitRegion.dstSubresource.layerCount = 1;
		ImageBlitRegion.dstOffsets[1] = {(int32_t)Target.m_Width, (int32_t)Target.m_Height, 1};
		vkCmdBlitImage(CommandBuffer, Target.m_Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_GetPresentedImgDataHelperImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ImageBlitRegion, VK_FILTER_NEAREST);
		IsB8G8R8A8 = false;
	}
	else
	{
		VkImageCopy ImageCopyRegion{};
		ImageCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		ImageCopyRegion.srcSubresource.layerCount = 1;
		ImageCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		ImageCopyRegion.dstSubresource.layerCount = 1;
		ImageCopyRegion.extent = {Target.m_Width, Target.m_Height, 1};
		vkCmdCopyImage(CommandBuffer, Target.m_Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_GetPresentedImgDataHelperImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ImageCopyRegion);
	}
	if(!ImageBarrier(CommandBuffer, m_GetPresentedImgDataHelperImage, 0, 1, 0, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL))
		return false;
	if(!ImageBarrier(CommandBuffer, Target.m_Image, 0, 1, 0, 1, RenderTargetReadbackFormat(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
		return false;
	Target.m_Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	if(!CheckVulkanResult(vkEndCommandBuffer(CommandBuffer), GFX_ERROR_TYPE_RENDER_RECORDING, "vkEndCommandBuffer (render target readback) failed."))
		return false;
	m_vUsedMemoryCommandBuffer[m_CurImageIndex] = false;
	VkSubmitInfo SubmitInfo{};
	SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	SubmitInfo.commandBufferCount = 1;
	SubmitInfo.pCommandBuffers = &CommandBuffer;
	VkResult ResetFenceResult = vkResetFences(m_VKDevice, 1, &m_GetPresentedImgDataHelperFence);
	if(ResetFenceResult != VK_SUCCESS)
	{
		const char *pErr = CheckVulkanCriticalError(ResetFenceResult);
		if(pErr != nullptr)
			SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Resetting render target readback fence failed: %s.", pErr);
		else
			SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Resetting render target readback fence failed.");
		return false;
	}
	VkResult QueueSubmitResult = QueueSubmit(m_VKGraphicsQueue, 1, &SubmitInfo, m_GetPresentedImgDataHelperFence);
	if(QueueSubmitResult != VK_SUCCESS)
	{
		const char *pErr = CheckVulkanCriticalError(QueueSubmitResult);
		if(pErr != nullptr)
			SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Submitting render target readback command buffer failed: %s.", pErr);
		else
			SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Submitting render target readback command buffer failed.");
		return false;
	}
	VkResult WaitForFencesResult = WaitForFences(1, &m_GetPresentedImgDataHelperFence, VK_TRUE, std::numeric_limits<uint64_t>::max());
	if(WaitForFencesResult != VK_SUCCESS)
	{
		const char *pErr = CheckVulkanCriticalError(WaitForFencesResult);
		if(pErr != nullptr)
			SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Waiting for render target readback fence failed: %s.", pErr);
		else
			SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Waiting for render target readback fence failed.");
		return false;
	}

	VkMappedMemoryRange MemRange{};
	MemRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	MemRange.memory = m_GetPresentedImgDataHelperMem.m_Mem;
	MemRange.offset = m_GetPresentedImgDataHelperMappedLayoutOffset;
	MemRange.size = VK_WHOLE_SIZE;
	VkResult InvalidateResult = InvalidateMappedMemoryRanges(1, &MemRange);
	if(InvalidateResult != VK_SUCCESS)
	{
		const char *pErr = CheckVulkanCriticalError(InvalidateResult);
		if(pErr != nullptr)
			SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Invalidating render target readback memory failed: %s.", pErr);
		else
			SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Invalidating render target readback memory failed.");
		return false;
	}

	const size_t PixelSize = 4;
	const size_t ImageSize = (size_t)Target.m_Width * Target.m_Height * PixelSize;
	auto *pPixels = static_cast<uint8_t *>(malloc(ImageSize));
	if(pPixels == nullptr)
		return false;
	for(uint32_t Y = 0; Y < Target.m_Height; ++Y)
	{
		mem_copy(pPixels + (size_t)Y * Target.m_Width * PixelSize, pResImageData + (size_t)Y * m_GetPresentedImgDataHelperMappedLayoutPitch, (size_t)Target.m_Width * PixelSize);
	}
	if(IsB8G8R8A8)
	{
		for(size_t Pixel = 0; Pixel < (size_t)Target.m_Width * Target.m_Height; ++Pixel)
			std::swap(pPixels[Pixel * 4], pPixels[Pixel * 4 + 2]);
	}
	pCommand->m_pImage->m_Width = Target.m_Width;
	pCommand->m_pImage->m_Height = Target.m_Height;
	pCommand->m_pImage->m_Format = CImageInfo::FORMAT_RGBA;
	pCommand->m_pImage->m_pData = pPixels;
	return true;
}

inline bool CCommandProcessorFragment_Vulkan::Cmd_RenderTarget_Draw(const CCommandBuffer::SCommand_RenderTarget_Draw *pCommand)
{
	if(m_RenderingPaused)
		return true;
	if(pCommand->m_TargetId < 0 || (size_t)pCommand->m_TargetId >= m_vRenderTargets.size() || pCommand->m_W <= 0.0f || pCommand->m_H <= 0.0f || pCommand->m_pVertices == nullptr || pCommand->m_PrimCount == 0)
		return true;
	SRenderTarget &Target = m_vRenderTargets[pCommand->m_TargetId];
	if(Target.m_Image == VK_NULL_HANDLE || Target.m_aVKStandardTexturedDescrSets[0].m_Descriptor == VK_NULL_HANDLE)
		return true;
	FinishRenderThreads();
	if(m_HasError)
		return false;
	auto &CommandBuffer = GetMainGraphicCommandBuffer();
	if(!ExecutePendingRenderThreadCommandBuffers(CommandBuffer))
		return false;
	if(Target.m_Layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		if(!ImageBarrier(CommandBuffer, Target.m_Image, 0, 1, 0, 1, RenderTargetReadbackFormat(), Target.m_Layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
			return false;
		Target.m_Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	SRenderCommandExecuteBuffer ExecBuffer;
	ExecBuffer.m_ThreadIndex = 0;
	ExecBufferFillDynamicStates(pCommand->m_State, ExecBuffer);
	const size_t BlendModeIndex = GetBlendModeIndex(pCommand->m_State);
	const size_t DynamicIndex = GetDynamicModeIndexFromExecBuffer(ExecBuffer);
	const size_t AddressModeIndex = GetAddressModeIndex(pCommand->m_State);
	auto &PipeLayout = GetStandardPipeLayout(false, true, BlendModeIndex, DynamicIndex);
	auto &PipeLine = GetStandardPipe(false, true, BlendModeIndex, DynamicIndex);
	BindPipeline(0, CommandBuffer, ExecBuffer, PipeLine, pCommand->m_State);

	VkBuffer VKBuffer;
	SDeviceMemoryBlock VKBufferMem;
	size_t BufferOff = 0;
	if(!CreateStreamVertexBuffer(0, VKBuffer, VKBufferMem, BufferOff, pCommand->m_pVertices, sizeof(CCommandBuffer::SVertex) * pCommand->m_PrimCount * 4))
		return false;
	std::array<VkBuffer, 1> aVertexBuffers = {VKBuffer};
	std::array<VkDeviceSize, 1> aOffsets = {(VkDeviceSize)BufferOff};
	vkCmdBindVertexBuffers(CommandBuffer, 0, 1, aVertexBuffers.data(), aOffsets.data());
	vkCmdBindIndexBuffer(CommandBuffer, m_RenderIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PipeLayout, 0, 1, &Target.m_aVKStandardTexturedDescrSets[AddressModeIndex].m_Descriptor, 0, nullptr);

	std::array<float, (size_t)4 * 2> m;
	GetStateMatrix(pCommand->m_State, m);
	vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SUniformGPos), m.data());
	vkCmdDrawIndexed(CommandBuffer, pCommand->m_PrimCount * 6, 1, 0, 0, 0);
	ResetDrawCommandState(0);
	return true;
}

inline bool CCommandProcessorFragment_Vulkan::Cmd_RenderTarget_CaptureBackbuffer(const CCommandBuffer::SCommand_RenderTarget_CaptureBackbuffer *pCommand)
{
	if(!m_CaptureBackbufferProbeDone)
	{
		m_CaptureBackbufferProbeDone = true;
		const bool FormatOk = m_VKSurfFormat.format == VK_FORMAT_B8G8R8A8_UNORM || m_VKSurfFormat.format == VK_FORMAT_R8G8B8A8_UNORM ||
				      m_VKSurfFormat.format == VK_FORMAT_B8G8R8A8_SRGB || m_VKSurfFormat.format == VK_FORMAT_R8G8B8A8_SRGB;
		const bool Supported = SupportsBackbufferCapture();
		const bool Multisampled = HasMultiSampling();
		const bool CanCapture = !m_RenderingPaused && Supported && !Multisampled && !m_RenderTargetActive && m_SwapRenderPassActive &&
					pCommand->m_TargetId >= 0 && (size_t)pCommand->m_TargetId < m_vRenderTargets.size() && m_CurImageIndex < m_vSwapChainImages.size();
		if(!CanCapture || g_Config.m_QmGraphicsTrace >= 1)
			log_info("gfx/vulkan", "backbuffer capture probe: %s (paused=%d supported=%d format=%d format_ok=%d swap_blit=%d rgba_blit=%d multisample=%d target_active=%d swap_pass=%d)",
				CanCapture ? "active" : "skipped",
				(int)m_RenderingPaused, (int)Supported, (int)m_VKSurfFormat.format, (int)FormatOk,
				(int)m_OptimalSwapChainImageBlitting, (int)m_OptimalRGBAImageBlitting,
				(int)Multisampled, (int)m_RenderTargetActive, (int)m_SwapRenderPassActive);
	}
	if(m_RenderingPaused || !SupportsBackbufferCapture() || HasMultiSampling() || m_RenderTargetActive || !m_SwapRenderPassActive || pCommand->m_TargetId < 0 ||
		(size_t)pCommand->m_TargetId >= m_vRenderTargets.size() || m_CurImageIndex >= m_vSwapChainImages.size())
	{
		// 静默跳过会让模糊目标保留上一帧的旧内容，是画面闪烁的候选来源之一；
		// 用 trace 开关暴露具体是哪个条件命中
		if(g_Config.m_QmGraphicsTrace >= 1)
			dbg_msg("vulkan", "backbuffer capture skipped: paused=%d capture_supported=%d msaa=%d target_active=%d swap_pass_active=%d",
				m_RenderingPaused ? 1 : 0, SupportsBackbufferCapture() ? 1 : 0, HasMultiSampling() ? 1 : 0,
				m_RenderTargetActive ? 1 : 0, m_SwapRenderPassActive ? 1 : 0);
		return true;
	}
	SRenderTarget &Target = m_vRenderTargets[pCommand->m_TargetId];
	if(Target.m_Image == VK_NULL_HANDLE || Target.m_Width == 0 || Target.m_Height == 0 ||
		(Target.m_Layout != VK_IMAGE_LAYOUT_UNDEFINED && Target.m_Layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
		return true;

	if(!EndSwapRenderPassForExternalWork())
		return false;
	auto &CommandBuffer = GetMainGraphicCommandBuffer();
	VkImage &SwapImage = m_vSwapChainImages[m_CurImageIndex];
	if(!ImageBarrier(CommandBuffer, SwapImage, 0, 1, 0, 1, m_VKSurfFormat.format, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL))
		return false;
	if(!ImageBarrier(CommandBuffer, Target.m_Image, 0, 1, 0, 1, RenderTargetReadbackFormat(), Target.m_Layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
		return false;
	Target.m_Layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

	VkImageBlit BlitRegion{};
	BlitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	BlitRegion.srcSubresource.layerCount = 1;
	const VkExtent2D SourceViewport = m_VKSwapImgAndViewportExtent.GetPresentedImageViewport();
	BlitRegion.srcOffsets[1] = {
		(int32_t)SourceViewport.width,
		(int32_t)SourceViewport.height,
		1};
	BlitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	BlitRegion.dstSubresource.layerCount = 1;
	BlitRegion.dstOffsets[0] = {0, (int32_t)Target.m_Height, 0};
	BlitRegion.dstOffsets[1] = {(int32_t)Target.m_Width, 0, 1};
	vkCmdBlitImage(CommandBuffer, SwapImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		Target.m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &BlitRegion,
		m_OptimalSwapChainImageLinearBlitting ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);

	if(!ImageBarrier(CommandBuffer, Target.m_Image, 0, 1, 0, 1, RenderTargetReadbackFormat(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
		return false;
	Target.m_Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	if(!ImageBarrier(CommandBuffer, SwapImage, 0, 1, 0, 1, m_VKSurfFormat.format, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR))
		return false;

	BeginSwapRenderPass(m_VKRenderPassLoad);
	return true;
}

inline bool CCommandProcessorFragment_Vulkan::Cmd_RenderTarget_GaussianBlurPass(const CCommandBuffer::SCommand_RenderTarget_GaussianBlurPass *pCommand)
{
	if(m_RenderingPaused)
		return true;
	if(!m_GaussianBlurPipelineValid || HasMultiSampling() || !m_RenderTargetActive || m_ActiveRenderTargetId < 0 || pCommand->m_SourceTargetId < 0 ||
		(size_t)m_ActiveRenderTargetId >= m_vRenderTargets.size() || (size_t)pCommand->m_SourceTargetId >= m_vRenderTargets.size() ||
		pCommand->m_SourceTargetId == m_ActiveRenderTargetId || pCommand->m_Radius < 1 || pCommand->m_Radius > IGraphics::GAUSSIAN_BLUR_MAX_RADIUS)
		return true;
	SRenderTarget &Source = m_vRenderTargets[pCommand->m_SourceTargetId];
	const SRenderTarget &Destination = m_vRenderTargets[m_ActiveRenderTargetId];
	const bool DualKawase = pCommand->m_Mode == IGraphics::EBlurMode::DUAL;
	if(Source.m_Image == VK_NULL_HANDLE || Source.m_Layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
		(!DualKawase && (Source.m_Width != Destination.m_Width || Source.m_Height != Destination.m_Height)) ||
		(DualKawase && (pCommand->m_Upsample ? (Source.m_Width >= Destination.m_Width || Source.m_Height >= Destination.m_Height) : (Source.m_Width <= Destination.m_Width || Source.m_Height <= Destination.m_Height))) ||
		Source.m_aVKStandardTexturedDescrSets[VULKAN_BACKEND_ADDRESS_MODE_CLAMP_EDGES].m_Descriptor == VK_NULL_HANDLE)
		return true;

	FinishRenderThreads();
	if(m_HasError)
		return false;
	auto &CommandBuffer = GetMainGraphicCommandBuffer();
	if(!ExecutePendingRenderThreadCommandBuffers(CommandBuffer))
		return false;
	CCommandBuffer::SState State{};
	State.m_Texture = -1;
	State.m_BlendMode = EBlendMode::NONE;
	State.m_ClipEnable = false;
	SRenderCommandExecuteBuffer ExecBuffer;
	ExecBuffer.m_ThreadIndex = 0;
	ExecBufferFillDynamicStates(State, ExecBuffer);
	constexpr size_t BlendModeIndex = VULKAN_BACKEND_BLEND_MODE_NONE;
	constexpr size_t DynamicIndex = VULKAN_BACKEND_CLIP_MODE_DYNAMIC_SCISSOR_AND_VIEWPORT;
	auto &PipeLayout = GetPipeLayout(m_GaussianBlurPipeline, true, BlendModeIndex, DynamicIndex);
	auto &PipeLine = GetPipeline(m_GaussianBlurPipeline, true, BlendModeIndex, DynamicIndex);
	BindPipeline(0, CommandBuffer, ExecBuffer, PipeLine, State);

	CCommandBuffer::SVertex aVertices[4]{};
	aVertices[0].m_Pos = vec2(-1.0f, -1.0f);
	aVertices[0].m_Tex = vec2(0.0f, 0.0f);
	aVertices[1].m_Pos = vec2(1.0f, -1.0f);
	aVertices[1].m_Tex = vec2(1.0f, 0.0f);
	aVertices[2].m_Pos = vec2(1.0f, 1.0f);
	aVertices[2].m_Tex = vec2(1.0f, 1.0f);
	aVertices[3].m_Pos = vec2(-1.0f, 1.0f);
	aVertices[3].m_Tex = vec2(0.0f, 1.0f);
	VkBuffer VertexBuffer;
	SDeviceMemoryBlock VertexBufferMem;
	size_t VertexBufferOffset = 0;
	if(!CreateStreamVertexBuffer(0, VertexBuffer, VertexBufferMem, VertexBufferOffset, aVertices, sizeof(aVertices)))
		return false;
	const std::array<VkBuffer, 1> aVertexBuffers = {VertexBuffer};
	const std::array<VkDeviceSize, 1> aOffsets = {(VkDeviceSize)VertexBufferOffset};
	vkCmdBindVertexBuffers(CommandBuffer, 0, 1, aVertexBuffers.data(), aOffsets.data());
	vkCmdBindIndexBuffer(CommandBuffer, m_RenderIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PipeLayout, 0, 1,
		&Source.m_aVKStandardTexturedDescrSets[VULKAN_BACKEND_ADDRESS_MODE_CLAMP_EDGES].m_Descriptor, 0, nullptr);

	SUniformGaussianBlur PushConstants{};
	const bool Gaussian = pCommand->m_Mode == IGraphics::EBlurMode::GAUSSIAN;
	PushConstants.m_TexelOffset = vec2(
		Gaussian && !pCommand->m_Horizontal ? 0.0f : 1.0f / Source.m_Width,
		Gaussian && pCommand->m_Horizontal ? 0.0f : 1.0f / Source.m_Height);
	PushConstants.m_Radius = pCommand->m_Radius;
	PushConstants.m_Mode = static_cast<int32_t>(pCommand->m_Mode);
	PushConstants.m_aWeights = pCommand->m_aWeights;
	PushConstants.m_Pass = pCommand->m_Pass;
	vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &PushConstants);
	vkCmdDrawIndexed(CommandBuffer, 6, 1, 0, 0, 0);
	ResetDrawCommandState(0);
	return true;
}

inline void CCommandProcessorFragment_Vulkan::CleanupQmRenderTargets()
{
	for(auto &RenderTarget : m_vRenderTargets)
		DestroyRenderTarget(RenderTarget);
	m_vRenderTargets.clear();
}

inline void CCommandProcessorFragment_Vulkan::RegisterQmRenderTargetCommands()
{
	m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_TARGET_CREATE)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderTarget_Create(static_cast<const CCommandBuffer::SCommand_RenderTarget_Create *>(pBaseCommand)); }};
	m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_TARGET_DESTROY)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderTarget_Destroy(static_cast<const CCommandBuffer::SCommand_RenderTarget_Destroy *>(pBaseCommand)); }};
	m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_TARGET_BEGIN)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderTarget_Begin(static_cast<const CCommandBuffer::SCommand_RenderTarget_Begin *>(pBaseCommand)); }};
	m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_TARGET_END)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderTarget_End(static_cast<const CCommandBuffer::SCommand_RenderTarget_End *>(pBaseCommand)); }};
	m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_TARGET_DRAW)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderTarget_Draw(static_cast<const CCommandBuffer::SCommand_RenderTarget_Draw *>(pBaseCommand)); }};
	m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_TARGET_CAPTURE_BACKBUFFER)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderTarget_CaptureBackbuffer(static_cast<const CCommandBuffer::SCommand_RenderTarget_CaptureBackbuffer *>(pBaseCommand)); }};
	m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_TARGET_GAUSSIAN_BLUR_PASS)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderTarget_GaussianBlurPass(static_cast<const CCommandBuffer::SCommand_RenderTarget_GaussianBlurPass *>(pBaseCommand)); }};
	m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_TARGET_READBACK)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderTarget_Readback(static_cast<const CCommandBuffer::SCommand_RenderTarget_Readback *>(pBaseCommand)); }};
}

#endif
