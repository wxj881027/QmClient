#ifndef ENGINE_CLIENT_BACKEND_VULKAN_BACKEND_VULKAN_QM_EXT_H
#define ENGINE_CLIENT_BACKEND_VULKAN_BACKEND_VULKAN_QM_EXT_H

#include <atomic>
#include <type_traits>

// QmVulkan 扩展：SDF / 解析环图 自定义管线的开关与回退状态。
// 关闭增强模式时不创建、不暴露 SDF 与圆环能力；背景模糊由独立的 UI 总开关控制，
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

	class CDeferredDeviceLoss
	{
		std::atomic<bool> m_Reported{false};

	public:
		// 录制线程只报告事件，不读取配置或修改仍被其它线程借用的管线状态。
		void Report() noexcept { m_Reported.store(true, std::memory_order_release); }

		// 所有录制线程空闲后，由所有者读取当前策略并发布禁用状态。
		template<typename TReadMode, typename TDisable>
		bool ConsumeAfterRecording(bool SessionDisabled, TReadMode &&ReadMode, TDisable &&Disable) noexcept
		{
			static_assert(std::is_nothrow_invocable_r_v<EEnhancedMode, TReadMode &>);
			static_assert(std::is_nothrow_invocable_v<TDisable &, EDisableReason>);
			if(!m_Reported.load(std::memory_order_acquire) || !m_Reported.exchange(false, std::memory_order_acq_rel))
				return false;
			if(SessionDisabled || ReadMode() != EEnhancedMode::AUTO)
				return false;
			Disable(EDisableReason::DEVICE_LOST);
			return true;
		}
	};
} // namespace qm_vulkan_ext

#endif
