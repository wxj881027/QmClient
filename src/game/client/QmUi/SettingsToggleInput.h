#ifndef GAME_CLIENT_QMUI_SETTINGSTOGGLEINPUT_H
#define GAME_CLIENT_QMUI_SETTINGSTOGGLEINPUT_H

#include "SettingsContentLayout.h"
#include "SettingsToggleGrid.h"
#include "UiSwitchTipPolicy.h"

#include <game/client/ui.h>

inline const char *SettingsToggleDescription(const SSettingsToggleEntry &Entry, const char *pOverride)
{
	if(pOverride != nullptr)
		return pOverride;
	if(Entry.m_pTooltip == nullptr || *Entry.m_pTooltip == 0)
		return nullptr;
	if(Entry.m_pLabel != nullptr && ui_switch_tip::RepeatsLabel(Entry.m_pLabel, Entry.m_pTooltip, false))
		return nullptr;
	return Entry.m_pTooltip;
}

// 复选框先用配置地址绑定命令，再覆盖同一控件的说明。独立控件 ID 也保留绑定。
template<typename TUi, typename TCheckbox>
bool DoSettingsToggleRow(TUi &Ui, const SSettingsToggleEntry &Entry, const CUIRect &Row, const char *pOverride,
	bool ProcessInput, bool Collecting, TCheckbox Checkbox)
{
	const void *pId = Entry.m_pId != nullptr ? Entry.m_pId : Entry.m_pValue;
	const bool Clicked = Checkbox(pId, Entry.m_pValue, SettingsToggleEntryValue(Entry), pOverride == nullptr);
	if(!Collecting && !Ui.RenderOnly() && (pOverride || Entry.m_pTooltip) && Ui.MouseHovered(&Row))
	{
		if(const char *pDescription = SettingsToggleDescription(Entry, pOverride))
			Ui.Tooltips().DoToolTipForRect(pId, &Row, pDescription);
	}
	return ApplySettingsToggleEntry(Entry, Clicked, ProcessInput && !Collecting && !Ui.RenderOnly(), pOverride != nullptr);
}

// 筛选胶囊在输入趟登记命令与说明，绘制趟只消费外观。
template<typename TUi>
bool DoSettingsToggleButtonInput(TUi &Ui, const CSettingsContentLayoutFlow &Flow, const SSettingsToggleEntry &Entry,
	const CUIRect &Button, const CUIRect &Label, const char *pOverride, bool SettingsPage, bool ProcessInput)
{
	if(!Flow.Input() || Ui.RenderOnly())
		return false;
	const void *pId = Entry.m_pId != nullptr ? Entry.m_pId : Entry.m_pValue;
	if(SettingsPage)
		Ui.Tooltips().DoSettingsToolTipForConfig(pId, &Button, Entry.m_pValue, &Label, nullptr, Entry.m_pLabel);
	else
		Ui.DoConfigTooltip(pId, &Button, Entry.m_pValue);
	if((pOverride || Entry.m_pTooltip) && Ui.MouseHovered(&Button))
	{
		const char *pDescription = pOverride != nullptr ? pOverride : Entry.m_pTooltip;
		if(SettingsPage)
			pDescription = SettingsToggleDescription(Entry, pOverride);
		if(pDescription != nullptr)
			Ui.Tooltips().DoToolTip(pId, &Button, pDescription);
	}
	const bool Clicked = ProcessInput && Ui.DoButtonLogic(pId, 0, &Button, pOverride == nullptr ? BUTTONFLAG_LEFT : BUTTONFLAG_NONE);
	return ApplySettingsToggleEntry(Entry, Clicked, ProcessInput, pOverride != nullptr);
}

#endif
