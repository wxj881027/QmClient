#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CONFIG_OVERRIDE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CONFIG_OVERRIDE_H

#include <cstddef>

struct SConfigIntOverrideEntry
{
	int *m_pTarget;
	int *m_pSavedValue;
	int m_OverrideValue;
};

enum class EFocusConfigGroup
{
	NONE,
	HUD,
	UI,
	NAMES,
};

enum class EFocusConfigTarget
{
	CL_SHOWHUD,
	CL_SHOWHUD_DDRACE,
	TC_STATUS_BAR,
	TC_NOTIFY_WHEN_LAST,
	QM_PLAYER_STATS_MAP_PROGRESS,
	QM_SMTC_SHOW_HUD,
	QM_DUMMY_MINI_VIEW,
	QM_INPUT_OVERLAY,
	QM_VOICE_SHOW_OVERLAY,
	CL_NAME_PLATES,
	CL_NAME_PLATES_OWN,
};

inline void UpdateConfigIntOverride(int &Target, int OverrideValue, bool ShouldOverride, bool &Overridden, int &SavedValue)
{
	if(ShouldOverride)
	{
		if(!Overridden)
		{
			SavedValue = Target;
			Overridden = true;
		}
		else if(Target != OverrideValue)
		{
			SavedValue = Target;
		}
		Target = OverrideValue;
	}
	else if(Overridden)
	{
		Target = SavedValue;
		Overridden = false;
	}
}

inline void UpdateConfigIntOverrides(const SConfigIntOverrideEntry *pEntries, size_t NumEntries, bool ShouldOverride, bool &Overridden)
{
	if(ShouldOverride)
	{
		if(!Overridden)
		{
			for(size_t i = 0; i < NumEntries; i++)
				*pEntries[i].m_pSavedValue = *pEntries[i].m_pTarget;
			Overridden = true;
		}
		else
		{
			for(size_t i = 0; i < NumEntries; i++)
			{
				if(*pEntries[i].m_pTarget != pEntries[i].m_OverrideValue)
					*pEntries[i].m_pSavedValue = *pEntries[i].m_pTarget;
			}
		}

		for(size_t i = 0; i < NumEntries; i++)
			*pEntries[i].m_pTarget = pEntries[i].m_OverrideValue;
	}
	else if(Overridden)
	{
		for(size_t i = 0; i < NumEntries; i++)
			*pEntries[i].m_pTarget = *pEntries[i].m_pSavedValue;
		Overridden = false;
	}
}

inline int DeriveGoresFastInputValue(bool GoresActive, bool AutoToggleFastInput)
{
	return GoresActive && AutoToggleFastInput ? 1 : 0;
}

inline int DeriveGoresFastInputOthersValue(bool GoresActive, bool AutoToggleFastInputOthers)
{
	return GoresActive && AutoToggleFastInputOthers ? 1 : 0;
}

inline int DeriveGoresLinkedConfigValue(bool PreviousGoresActive, bool CurrentGoresActive, bool AutoToggleConfig, int CurrentValue)
{
	if(CurrentGoresActive)
		return AutoToggleConfig ? 1 : CurrentValue;
	if(PreviousGoresActive && AutoToggleConfig)
		return 0;
	return CurrentValue;
}

inline int DeriveGoresAutoTogglePreference(bool CurrentGoresActive, bool PreviousGoresActive, int PreviousAutoToggle, int CurrentAutoToggle, int PreviousLinkedConfig, int CurrentLinkedConfig)
{
	if(CurrentGoresActive)
		return CurrentAutoToggle != 0 ? 1 : 0;
	if(PreviousGoresActive && !CurrentGoresActive)
		return CurrentAutoToggle != 0 ? 1 : 0;
	if(CurrentLinkedConfig != PreviousLinkedConfig && CurrentAutoToggle == PreviousAutoToggle)
		return CurrentLinkedConfig != 0 ? 1 : 0;
	return CurrentAutoToggle != 0 ? 1 : 0;
}

inline EFocusConfigGroup GetFocusConfigGroup(EFocusConfigTarget Target)
{
	switch(Target)
	{
	case EFocusConfigTarget::CL_SHOWHUD:
	case EFocusConfigTarget::CL_SHOWHUD_DDRACE:
		return EFocusConfigGroup::HUD;
	case EFocusConfigTarget::TC_STATUS_BAR:
	case EFocusConfigTarget::TC_NOTIFY_WHEN_LAST:
	case EFocusConfigTarget::QM_PLAYER_STATS_MAP_PROGRESS:
	case EFocusConfigTarget::QM_SMTC_SHOW_HUD:
	case EFocusConfigTarget::QM_DUMMY_MINI_VIEW:
	case EFocusConfigTarget::QM_INPUT_OVERLAY:
	case EFocusConfigTarget::QM_VOICE_SHOW_OVERLAY:
		return EFocusConfigGroup::UI;
	case EFocusConfigTarget::CL_NAME_PLATES:
	case EFocusConfigTarget::CL_NAME_PLATES_OWN:
		return EFocusConfigGroup::NAMES;
	default:
		return EFocusConfigGroup::NONE;
	}
}

inline bool ShouldHideFocusUiOverlays(bool FocusActive, bool HideUi)
{
	return FocusActive && HideUi;
}

inline bool ShouldRenderFocusFilteredChatLine(bool FocusHideChat, bool FocusHideEcho, bool IsClientMsg, bool ForceVisible)
{
	if(ForceVisible)
		return true;
	if(FocusHideChat && FocusHideEcho)
		return false;
	if(FocusHideChat && !FocusHideEcho)
		return IsClientMsg;
	if(FocusHideEcho && !FocusHideChat && IsClientMsg)
		return false;
	return true;
}

inline bool ShouldRenderAnyFocusFilteredChat(bool FocusHideChat, bool FocusHideEcho, bool HasForceVisibleLine)
{
	return !(FocusHideChat && FocusHideEcho) || HasForceVisibleLine;
}

#endif
