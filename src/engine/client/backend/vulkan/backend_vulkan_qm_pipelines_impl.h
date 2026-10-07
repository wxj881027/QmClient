#ifndef ENGINE_CLIENT_BACKEND_VULKAN_BACKEND_VULKAN_QM_PIPELINES_IMPL_H
#define ENGINE_CLIENT_BACKEND_VULKAN_BACKEND_VULKAN_QM_PIPELINES_IMPL_H

// 仅由 backend_vulkan.cpp 在后端类定义后包含，共用其私有 GPU 资源与命令状态。

inline qm_vulkan_ext::EEnhancedMode CCommandProcessorFragment_Vulkan::QmEnhancedMode() const
{
	return qm_vulkan_ext::ModeFromConfig(g_Config.m_QmEnhancedRendering);
}

inline bool CCommandProcessorFragment_Vulkan::QmEnhancedShouldLoad() const
{
	return qm_vulkan_ext::ShouldLoadEnhancedPipelines(QmEnhancedMode(), m_QmEnhancedSessionDisabled);
}

inline void CCommandProcessorFragment_Vulkan::QmEnhancedMarkDisabled(const qm_vulkan_ext::EDisableReason Reason)
{
	m_QmEnhancedSessionDisabled = true;
	m_QmEnhancedDisableReason = Reason;
	m_QmMediaIslandSdfPipelineValid = false;
	m_QmRoundedRectSdfPipelineValid = false;
	m_ProceduralRingPipelineValid = false;
	m_GaussianBlurPipelineValid = false;
	SyncProceduralRingCapability();
	if(m_pBackendCapabilities != nullptr)
	{
		m_pBackendCapabilities->m_MediaIslandSdf = false;
		m_pBackendCapabilities->m_RoundedRectSdf = false;
		m_pBackendCapabilities->m_RenderTargetGaussianBlur = false;
	}
	log_info("vulkan", "Qm enhanced rendering disabled: %s", qm_vulkan_ext::DisableReasonLabel(Reason));
}

inline void CCommandProcessorFragment_Vulkan::SyncProceduralRingCapability()
{
	if(m_pBackendCapabilities != nullptr)
		m_pBackendCapabilities->m_ProceduralRing.store(m_ProceduralRingPipelineValid, std::memory_order_release);
}

inline bool CCommandProcessorFragment_Vulkan::CreateMediaIslandSdfGraphicsPipeline(const char *pVertName, const char *pFragName)
{
	std::array<VkVertexInputAttributeDescription, 3> aAttributeDescriptions = {};
	aAttributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
	aAttributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2};
	aAttributeDescriptions[2] = {2, 0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float) * (2 + 2)};

	std::array<VkDescriptorSetLayout, 2> aSetLayouts = {m_StandardTexturedDescriptorSetLayout, m_QuadUniformDescriptorSetLayout};
	std::array<VkPushConstantRange, 1> aPushConstants{};
	aPushConstants[0] = {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SUniformGPos)};

	bool Ret = true;
	for(size_t i = 0; i < VULKAN_BACKEND_BLEND_MODE_COUNT; ++i)
	{
		for(size_t j = 0; j < VULKAN_BACKEND_CLIP_MODE_COUNT; ++j)
		{
			Ret &= CreateGraphicsPipeline<true>(pVertName, pFragName, m_MediaIslandSdfPipeline, sizeof(CCommandBuffer::SVertex), aAttributeDescriptions, aSetLayouts, aPushConstants, VULKAN_BACKEND_TEXTURE_MODE_TEXTURED, EVulkanBackendBlendModes(i), EVulkanBackendClipModes(j));
		}
	}
	return Ret;
}

inline bool CCommandProcessorFragment_Vulkan::CreateRoundedRectSdfGraphicsPipeline(const char *pVertName, const char *pFragName)
{
	std::array<VkVertexInputAttributeDescription, 3> aAttributeDescriptions = {};
	aAttributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
	aAttributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2};
	aAttributeDescriptions[2] = {2, 0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float) * (2 + 2)};

	std::array<VkDescriptorSetLayout, 1> aSetLayouts = {m_QuadUniformDescriptorSetLayout};
	std::array<VkPushConstantRange, 1> aPushConstants{};
	aPushConstants[0] = {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SUniformGPos)};

	bool Ret = true;
	for(size_t i = 0; i < VULKAN_BACKEND_BLEND_MODE_COUNT; ++i)
	{
		for(size_t j = 0; j < VULKAN_BACKEND_CLIP_MODE_COUNT; ++j)
		{
			Ret &= CreateGraphicsPipeline<true>(pVertName, pFragName, m_RoundedRectSdfPipeline, sizeof(CCommandBuffer::SVertex), aAttributeDescriptions, aSetLayouts, aPushConstants, VULKAN_BACKEND_TEXTURE_MODE_NOT_TEXTURED, EVulkanBackendBlendModes(i), EVulkanBackendClipModes(j));
		}
	}
	return Ret;
}

inline bool CCommandProcessorFragment_Vulkan::CreateProceduralRingGraphicsPipeline(const char *pVertName, const char *pFragName)
{
	std::array<VkVertexInputAttributeDescription, 3> aAttributeDescriptions = {};
	aAttributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
	aAttributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2};
	aAttributeDescriptions[2] = {2, 0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float) * (2 + 2)};

	std::array<VkDescriptorSetLayout, 1> aSetLayouts = {m_QuadUniformDescriptorSetLayout};
	std::array<VkPushConstantRange, 1> aPushConstants{};
	aPushConstants[0] = {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SUniformGPos)};

	bool Ret = true;
	for(size_t i = 0; i < VULKAN_BACKEND_BLEND_MODE_COUNT; ++i)
	{
		for(size_t j = 0; j < VULKAN_BACKEND_CLIP_MODE_COUNT; ++j)
		{
			Ret &= CreateGraphicsPipeline<true>(pVertName, pFragName, m_ProceduralRingPipeline, sizeof(CCommandBuffer::SVertex), aAttributeDescriptions, aSetLayouts, aPushConstants, VULKAN_BACKEND_TEXTURE_MODE_NOT_TEXTURED, EVulkanBackendBlendModes(i), EVulkanBackendClipModes(j), false, VK_NULL_HANDLE, VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM, true, false);
		}
	}
	return Ret;
}

inline bool CCommandProcessorFragment_Vulkan::CreateGaussianBlurGraphicsPipeline(const char *pVertName, const char *pFragName)
{
	std::array<VkVertexInputAttributeDescription, 2> aAttributeDescriptions{};
	aAttributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
	aAttributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2};
	std::array<VkDescriptorSetLayout, 1> aSetLayouts = {m_StandardTexturedDescriptorSetLayout};
	std::array<VkPushConstantRange, 1> aPushConstants{};
	aPushConstants[0] = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SUniformGaussianBlur)};
	return CreateGraphicsPipeline<true>(pVertName, pFragName, m_GaussianBlurPipeline, sizeof(CCommandBuffer::SVertex), aAttributeDescriptions, aSetLayouts, aPushConstants,
		VULKAN_BACKEND_TEXTURE_MODE_TEXTURED, VULKAN_BACKEND_BLEND_MODE_NONE, VULKAN_BACKEND_CLIP_MODE_DYNAMIC_SCISSOR_AND_VIEWPORT, false,
		m_VKRenderTargetRenderPass, VK_SAMPLE_COUNT_1_BIT, false);
}

inline void CCommandProcessorFragment_Vulkan::Cmd_RenderMediaIslandSdf_FillExecuteBuffer(SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand_RenderMediaIslandSdf *pCommand)
{
	const size_t AddressModeIndex = GetAddressModeIndex(pCommand->m_State);
	if(pCommand->m_BackdropTargetId >= 0 && (size_t)pCommand->m_BackdropTargetId < m_vRenderTargets.size() &&
		m_vRenderTargets[pCommand->m_BackdropTargetId].m_aVKStandardTexturedDescrSets[AddressModeIndex].m_Descriptor != VK_NULL_HANDLE)
	{
		ExecBuffer.m_aDescriptors[0] = m_vRenderTargets[pCommand->m_BackdropTargetId].m_aVKStandardTexturedDescrSets[AddressModeIndex];
	}
	else if(pCommand->m_State.m_Texture >= 0 && (size_t)pCommand->m_State.m_Texture < m_vTextures.size())
	{
		ExecBuffer.m_aDescriptors[0] = m_vTextures[pCommand->m_State.m_Texture].m_aVKStandardTexturedDescrSets[AddressModeIndex];
	}
	ExecBuffer.m_IndexBuffer = m_IndexBuffer;
	ExecBuffer.m_EstimatedRenderCallCount = 1;
	ExecBufferFillDynamicStates(pCommand->m_State, ExecBuffer);
}

inline void CCommandProcessorFragment_Vulkan::Cmd_RenderRoundedRectSdf_FillExecuteBuffer(SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand_RenderRoundedRectSdf *pCommand)
{
	ExecBuffer.m_IndexBuffer = m_IndexBuffer;
	ExecBuffer.m_EstimatedRenderCallCount = 1;
	ExecBufferFillDynamicStates(pCommand->m_State, ExecBuffer);
}

inline void CCommandProcessorFragment_Vulkan::Cmd_RenderProceduralRing_FillExecuteBuffer(SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand_RenderProceduralRing *pCommand)
{
	ExecBuffer.m_IndexBuffer = m_IndexBuffer;
	ExecBuffer.m_EstimatedRenderCallCount = 1;
	ExecBufferFillDynamicStates(pCommand->m_State, ExecBuffer);
}

inline bool CCommandProcessorFragment_Vulkan::Cmd_RenderMediaIslandSdf(const CCommandBuffer::SCommand_RenderMediaIslandSdf *pCommand, SRenderCommandExecuteBuffer &ExecBuffer)
{
	if(!m_QmMediaIslandSdfPipelineValid)
		return true;
	static_assert(sizeof(IGraphics::SMediaIslandSdfParams) <= 512 * sizeof(IGraphics::SRenderSpriteInfo));

	std::array<float, (size_t)4 * 2> m;
	GetStateMatrix(pCommand->m_State, m);

	bool IsTextured;
	size_t BlendModeIndex;
	size_t DynamicIndex;
	size_t AddressModeIndex;
	GetStateIndices(ExecBuffer, pCommand->m_State, IsTextured, BlendModeIndex, DynamicIndex, AddressModeIndex);
	if(!IsTextured)
		return false;
	(void)AddressModeIndex;
	auto &PipeLayout = GetPipeLayout(m_MediaIslandSdfPipeline, true, BlendModeIndex, DynamicIndex);
	auto &PipeLine = GetPipeline(m_MediaIslandSdfPipeline, true, BlendModeIndex, DynamicIndex);

	VkCommandBuffer *pCommandBuffer;
	if(!GetGraphicCommandBuffer(pCommandBuffer, ExecBuffer.m_ThreadIndex))
		return false;
	auto &CommandBuffer = *pCommandBuffer;
	BindPipeline(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer, PipeLine, pCommand->m_State);

	VkBuffer VKBuffer;
	SDeviceMemoryBlock VKBufferMem;
	size_t BufferOff = 0;
	if(!CreateStreamVertexBuffer(ExecBuffer.m_ThreadIndex, VKBuffer, VKBufferMem, BufferOff, pCommand->m_pVertices, sizeof(CCommandBuffer::SVertex) * pCommand->m_PrimCount * 4))
		return false;
	BindVertexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, VKBuffer, (VkDeviceSize)BufferOff);
	BindIndexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer.m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
	BindDescriptorSet(ExecBuffer.m_ThreadIndex, CommandBuffer, PipeLayout, 0, ExecBuffer.m_aDescriptors[0]);

	SDeviceDescriptorSet UniDescrSet;
	if(!GetUniformBufferObject(ExecBuffer.m_ThreadIndex, true, UniDescrSet, 1, &pCommand->m_Params, sizeof(pCommand->m_Params)))
		return false;
	BindDescriptorSet(ExecBuffer.m_ThreadIndex, CommandBuffer, PipeLayout, 1, UniDescrSet);
	vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SUniformGPos), m.data());
	DrawIndexed(ExecBuffer.m_ThreadIndex, CommandBuffer, 6, 1, 0, 0, 0);
	return true;
}

inline bool CCommandProcessorFragment_Vulkan::Cmd_RenderRoundedRectSdf(const CCommandBuffer::SCommand_RenderRoundedRectSdf *pCommand, SRenderCommandExecuteBuffer &ExecBuffer)
{
	if(!m_QmRoundedRectSdfPipelineValid)
		return true;
	std::array<float, (size_t)4 * 2> m;
	GetStateMatrix(pCommand->m_State, m);

	bool IsTextured;
	size_t BlendModeIndex;
	size_t DynamicIndex;
	size_t AddressModeIndex;
	GetStateIndices(ExecBuffer, pCommand->m_State, IsTextured, BlendModeIndex, DynamicIndex, AddressModeIndex);
	(void)IsTextured;
	(void)AddressModeIndex;
	auto &PipeLayout = GetPipeLayout(m_RoundedRectSdfPipeline, false, BlendModeIndex, DynamicIndex);
	auto &PipeLine = GetPipeline(m_RoundedRectSdfPipeline, false, BlendModeIndex, DynamicIndex);

	VkCommandBuffer *pCommandBuffer;
	if(!GetGraphicCommandBuffer(pCommandBuffer, ExecBuffer.m_ThreadIndex))
		return false;
	auto &CommandBuffer = *pCommandBuffer;
	BindPipeline(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer, PipeLine, pCommand->m_State);

	VkBuffer VKBuffer;
	SDeviceMemoryBlock VKBufferMem;
	size_t BufferOff = 0;
	if(!CreateStreamVertexBuffer(ExecBuffer.m_ThreadIndex, VKBuffer, VKBufferMem, BufferOff, pCommand->m_pVertices, sizeof(CCommandBuffer::SVertex) * pCommand->m_PrimCount * 4))
		return false;
	BindVertexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, VKBuffer, (VkDeviceSize)BufferOff);
	BindIndexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer.m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);

	SDeviceDescriptorSet UniDescrSet;
	if(!GetUniformBufferObject(ExecBuffer.m_ThreadIndex, true, UniDescrSet, 1, &pCommand->m_Params, sizeof(pCommand->m_Params)))
		return false;
	BindDescriptorSet(ExecBuffer.m_ThreadIndex, CommandBuffer, PipeLayout, 0, UniDescrSet);
	vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SUniformGPos), m.data());
	DrawIndexed(ExecBuffer.m_ThreadIndex, CommandBuffer, 6, 1, 0, 0, 0);
	return true;
}

inline bool CCommandProcessorFragment_Vulkan::Cmd_RenderProceduralRing(const CCommandBuffer::SCommand_RenderProceduralRing *pCommand, SRenderCommandExecuteBuffer &ExecBuffer)
{
	if(!m_ProceduralRingPipelineValid)
		return true;

	std::array<float, (size_t)4 * 2> m;
	GetStateMatrix(pCommand->m_State, m);

	bool IsTextured;
	size_t BlendModeIndex;
	size_t DynamicIndex;
	size_t AddressModeIndex;
	GetStateIndices(ExecBuffer, pCommand->m_State, IsTextured, BlendModeIndex, DynamicIndex, AddressModeIndex);
	(void)IsTextured;
	(void)AddressModeIndex;
	auto &PipeLayout = GetPipeLayout(m_ProceduralRingPipeline, false, BlendModeIndex, DynamicIndex);
	auto &PipeLine = GetPipeline(m_ProceduralRingPipeline, false, BlendModeIndex, DynamicIndex);

	VkCommandBuffer *pCommandBuffer;
	if(!GetGraphicCommandBuffer(pCommandBuffer, ExecBuffer.m_ThreadIndex))
		return false;
	auto &CommandBuffer = *pCommandBuffer;
	BindPipeline(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer, PipeLine, pCommand->m_State);

	VkBuffer VKBuffer;
	SDeviceMemoryBlock VKBufferMem;
	size_t BufferOff = 0;
	if(!CreateStreamVertexBuffer(ExecBuffer.m_ThreadIndex, VKBuffer, VKBufferMem, BufferOff, pCommand->m_pVertices, sizeof(CCommandBuffer::SVertex) * pCommand->m_PrimCount * 4))
		return false;
	BindVertexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, VKBuffer, (VkDeviceSize)BufferOff);
	BindIndexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer.m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);

	SDeviceDescriptorSet UniDescrSet;
	if(!GetUniformBufferObject(ExecBuffer.m_ThreadIndex, true, UniDescrSet, 1, &pCommand->m_RingParams, sizeof(pCommand->m_RingParams)))
		return false;
	BindDescriptorSet(ExecBuffer.m_ThreadIndex, CommandBuffer, PipeLayout, 0, UniDescrSet);
	vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SUniformGPos), m.data());
	DrawIndexed(ExecBuffer.m_ThreadIndex, CommandBuffer, 6, 1, 0, 0, 0);
	return true;
}

inline int CCommandProcessorFragment_Vulkan::InitQmPipelines()
{
	// ==== QmVulkan 扩展管线：仅在增强渲染启用时创建 ====
	m_QmMediaIslandSdfPipelineValid = false;
	m_QmRoundedRectSdfPipelineValid = false;
	m_ProceduralRingPipelineValid = false;
	m_GaussianBlurPipelineValid = false;
	if(QmEnhancedShouldLoad())
	{
		if(g_Config.m_QmEnhancedSdf)
		{
			m_QmMediaIslandSdfPipelineValid = CreateMediaIslandSdfGraphicsPipeline("shader/vulkan/media_island_sdf.vert.spv", "shader/vulkan/media_island_sdf.frag.spv");
			m_QmRoundedRectSdfPipelineValid = CreateRoundedRectSdfGraphicsPipeline("shader/vulkan/rounded_rect_sdf.vert.spv", "shader/vulkan/rounded_rect_sdf.frag.spv");
			if(!m_QmMediaIslandSdfPipelineValid || !m_QmRoundedRectSdfPipelineValid)
			{
				m_MediaIslandSdfPipeline.Destroy(m_VKDevice);
				m_RoundedRectSdfPipeline.Destroy(m_VKDevice);
				m_QmMediaIslandSdfPipelineValid = false;
				m_QmRoundedRectSdfPipelineValid = false;
				if(QmEnhancedMode() == qm_vulkan_ext::EEnhancedMode::ON)
				{
					SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Creating Qm SDF pipelines failed.");
					return -1;
				}
				QmEnhancedMarkDisabled(qm_vulkan_ext::EDisableReason::PIPELINE_CREATE_FAILED);
			}
		}
		if(!m_QmEnhancedSessionDisabled && g_Config.m_QmEnhancedProceduralRing)
		{
			m_ProceduralRingPipelineValid = CreateProceduralRingGraphicsPipeline("shader/vulkan/procedural_ring.vert.spv", "shader/vulkan/procedural_ring.frag.spv");
			SyncProceduralRingCapability();
			if(!m_ProceduralRingPipelineValid)
			{
				m_ProceduralRingPipeline.Destroy(m_VKDevice);
				if(m_ProceduralRingPipelineRequired)
				{
					SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Recreating the procedural ring pipeline failed.");
					return -1;
				}
				SetWarning(EGfxWarningType::GFX_WARNING_TYPE_INIT_FAILED, "Procedural ring pipeline unavailable, falling back to geometry.");
			}
			else
			{
				m_ProceduralRingPipelineRequired = true;
			}
		}
		else
		{
			SyncProceduralRingCapability();
		}
		if(!m_QmEnhancedSessionDisabled && g_Config.m_QmEnhancedBlur)
		{
			m_GaussianBlurPipelineValid = CreateGaussianBlurGraphicsPipeline("shader/vulkan/gaussian_blur.vert.spv", "shader/vulkan/gaussian_blur.frag.spv");
			if(!m_GaussianBlurPipelineValid && QmEnhancedMode() == qm_vulkan_ext::EEnhancedMode::ON)
				return -1;
		}
	}
	else
	{
		SyncProceduralRingCapability();
	}
	return 0;
}

inline void CCommandProcessorFragment_Vulkan::CleanupQmPipelines()
{
	m_MediaIslandSdfPipeline.Destroy(m_VKDevice);
	m_RoundedRectSdfPipeline.Destroy(m_VKDevice);
	m_ProceduralRingPipeline.Destroy(m_VKDevice);
	m_ProceduralRingPipelineValid = false;
	SyncProceduralRingCapability();
	m_GaussianBlurPipeline.Destroy(m_VKDevice);
	m_GaussianBlurPipelineValid = false;
}

inline void CCommandProcessorFragment_Vulkan::RegisterQmPipelineCommands()
{
	m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_MEDIA_ISLAND_SDF)] = {true, [this](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) { Cmd_RenderMediaIslandSdf_FillExecuteBuffer(ExecBuffer, static_cast<const CCommandBuffer::SCommand_RenderMediaIslandSdf *>(pBaseCommand)); }, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderMediaIslandSdf(static_cast<const CCommandBuffer::SCommand_RenderMediaIslandSdf *>(pBaseCommand), ExecBuffer); }};
	m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_ROUNDED_RECT_SDF)] = {true, [this](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) { Cmd_RenderRoundedRectSdf_FillExecuteBuffer(ExecBuffer, static_cast<const CCommandBuffer::SCommand_RenderRoundedRectSdf *>(pBaseCommand)); }, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderRoundedRectSdf(static_cast<const CCommandBuffer::SCommand_RenderRoundedRectSdf *>(pBaseCommand), ExecBuffer); }};
	m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_PROCEDURAL_RING)] = {true, [this](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) { Cmd_RenderProceduralRing_FillExecuteBuffer(ExecBuffer, static_cast<const CCommandBuffer::SCommand_RenderProceduralRing *>(pBaseCommand)); }, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderProceduralRing(static_cast<const CCommandBuffer::SCommand_RenderProceduralRing *>(pBaseCommand), ExecBuffer); }};
}

#endif
