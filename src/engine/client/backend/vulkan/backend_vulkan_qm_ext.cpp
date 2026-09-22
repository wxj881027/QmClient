#include "backend_vulkan_qm_ext.h"

namespace qm_vulkan_ext
{
	bool ShouldLoadEnhancedPipelines(const EEnhancedMode Mode, const bool SessionDisabled)
	{
		if(SessionDisabled)
			return false;
		return Mode == EEnhancedMode::AUTO || Mode == EEnhancedMode::ON;
	}

	EDisableReason ResolveCreateFailure(const EEnhancedMode Mode)
	{
		(void)Mode;
		return EDisableReason::PIPELINE_CREATE_FAILED;
	}

	EDisableReason ResolveDeviceLost(const EEnhancedMode Mode, const bool EnhancedActive)
	{
		if(!EnhancedActive)
			return EDisableReason::NONE;
		if(Mode == EEnhancedMode::AUTO)
			return EDisableReason::DEVICE_LOST;
		if(Mode == EEnhancedMode::ON)
			return EDisableReason::DEVICE_LOST;
		return EDisableReason::NONE;
	}

	EEnhancedMode ModeFromConfig(const int ConfigValue)
	{
		if(ConfigValue <= 0)
			return EEnhancedMode::OFF;
		if(ConfigValue == 1)
			return EEnhancedMode::AUTO;
		return EEnhancedMode::ON;
	}

	const char *DisableReasonLabel(const EDisableReason Reason)
	{
		switch(Reason)
		{
		case EDisableReason::NONE: return "";
		case EDisableReason::USER_OFF: return "enhanced rendering off";
		case EDisableReason::PIPELINE_CREATE_FAILED: return "pipeline create failed";
		case EDisableReason::DEVICE_LOST: return "device lost";
		case EDisableReason::AUTO_SESSION_SKIP: return "auto skipped this session";
		}
		return "";
	}
} // namespace qm_vulkan_ext
