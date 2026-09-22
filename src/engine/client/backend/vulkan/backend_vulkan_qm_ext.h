#ifndef ENGINE_CLIENT_BACKEND_VULKAN_BACKEND_VULKAN_QM_EXT_H
#define ENGINE_CLIENT_BACKEND_VULKAN_BACKEND_VULKAN_QM_EXT_H

// QmVulkan 扩展：SDF / GaussianBlur / MSDF 自定义管线的开关与回退状态。
// 与 Core 原生 Vulkan 管线分离——关闭扩展时不创建、不暴露这些能力，
// 灵动岛/圆环走几何或 CPU 兜底，避免部分驱动上 Driver Lost。
namespace qm_vulkan_ext
{
	enum class EEnhancedMode
	{
		OFF = 0,
		AUTO = 1,
		ON = 2,
	};

	enum class EDisableReason
	{
		NONE = 0,
		USER_OFF,
		PIPELINE_CREATE_FAILED,
		DEVICE_LOST,
		AUTO_SESSION_SKIP,
	};

	// 模式与会话内禁用标记 → 是否创建/暴露 Qm 自定义管线。
	[[nodiscard]] bool ShouldLoadEnhancedPipelines(EEnhancedMode Mode, bool SessionDisabled);

	// 管线创建失败时：强制开保持失败可见；自动/关则回退纯净化。
	[[nodiscard]] EDisableReason ResolveCreateFailure(EEnhancedMode Mode);

	// 设备丢失且扩展在跑：自动模式写回关闭，强制开仅记录原因。
	[[nodiscard]] EDisableReason ResolveDeviceLost(EEnhancedMode Mode, bool EnhancedActive);

	[[nodiscard]] EEnhancedMode ModeFromConfig(int ConfigValue);

	[[nodiscard]] const char *DisableReasonLabel(EDisableReason Reason);
} // namespace qm_vulkan_ext

#endif
