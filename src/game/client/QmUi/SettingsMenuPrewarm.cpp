#include <engine/shared/config.h>

#include <game/client/QmUi/SettingsMenuPrewarmScope.h>
#include <game/client/components/menus.h>
#include <game/client/components/tclient/menus_tclient_internal.h>

#include <algorithm>

using namespace qm_tclient_menu;

void CMenus::BuildQmClientSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView, int Tab)
{
	Tab = std::clamp(Tab, 0, NUMBER_OF_QMCLIENT_SETTINGS_TABS - 1);

	const CSettingsTextPlanValueRestore SettingsPageRestore(g_Config.m_UiSettingsPage);
	const CSettingsTextPlanValueRestore TabRestore(m_QmClientSettingsTab);
	const CSettingsTextPlanCollectionScope CollectionScope(m_MenuTextPlanCollecting, m_pMenuTextPlanCollection, m_MenuTextPlanPendingActive, m_MenuTextPlanPendingItem, vItems);

	g_Config.m_UiSettingsPage = SETTINGS_QMCLIENT;
	m_QmClientSettingsTab = Tab;
	RunSettingsTextPlanMeasurement(Ui(), [&] { RenderSettings(MainView); });
}

void CMenus::BuildTClientSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView, int Tab)
{
	Tab = CanonicalizePersistedTClientTab(Tab);
	const CSettingsTextPlanValueRestore SettingsPageRestore(g_Config.m_UiSettingsPage);
	const CSettingsTextPlanValueRestore TabRestore(m_TClientSettingsTab);
	const CSettingsTextPlanCollectionScope CollectionScope(m_MenuTextPlanCollecting, m_pMenuTextPlanCollection, m_MenuTextPlanPendingActive, m_MenuTextPlanPendingItem, vItems);

	g_Config.m_UiSettingsPage = SETTINGS_TCLIENT;
	m_TClientSettingsTab = Tab;
	RunSettingsTextPlanMeasurement(Ui(), [&] { RenderSettings(MainView); });
}
