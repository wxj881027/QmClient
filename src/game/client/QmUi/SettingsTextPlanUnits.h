#ifndef GAME_CLIENT_QMUI_SETTINGSTEXTPLANUNITS_H
#define GAME_CLIENT_QMUI_SETTINGSTEXTPLANUNITS_H

#include <span>
#include <vector>

enum ESettingsMenuTextPlanCollectionUnitKind
{
	MENU_TEXT_PLAN_UNIT_VISIBLE_SETTINGS = 0,
	MENU_TEXT_PLAN_UNIT_TCLIENT_TAB,
	MENU_TEXT_PLAN_UNIT_QMCLIENT_TAB,
	MENU_TEXT_PLAN_UNIT_BASE_PAGE,
	MENU_TEXT_PLAN_UNIT_INGAME_ESC,
};
struct SSettingsMenuTextPlanCollectionUnit
{
	ESettingsMenuTextPlanCollectionUnitKind m_Kind = MENU_TEXT_PLAN_UNIT_VISIBLE_SETTINGS;
	int m_Page = -1;
	int m_Tab = -1;
};

inline std::vector<SSettingsMenuTextPlanCollectionUnit> QmSettingsTextPlanUnits(bool IngameEsc, int CurrentPage, int CurrentTab,
	bool PreferQmClient, int TClientPage, int QmClientPage, int LastTClientTab, int LastQmClientTab,
	int NumTClientTabs, int NumQmClientTabs, int HiddenQmClientTab, std::span<const int> BasePages)
{
	// ESC 预热只收集游戏菜单，不能借空闲更新渲染隐藏设置页并触发字体目录等冷加载。
	if(IngameEsc)
		return {{MENU_TEXT_PLAN_UNIT_INGAME_ESC, -1, -1}};
	std::vector<SSettingsMenuTextPlanCollectionUnit> Units{{MENU_TEXT_PLAN_UNIT_VISIBLE_SETTINGS, CurrentPage, CurrentTab}};
	const SSettingsMenuTextPlanCollectionUnit TClient{MENU_TEXT_PLAN_UNIT_TCLIENT_TAB, TClientPage, LastTClientTab};
	const SSettingsMenuTextPlanCollectionUnit QmClient{MENU_TEXT_PLAN_UNIT_QMCLIENT_TAB, QmClientPage, LastQmClientTab};
	Units.push_back(PreferQmClient ? QmClient : TClient);
	Units.push_back(PreferQmClient ? TClient : QmClient);
	for(int Tab = 0; Tab < NumTClientTabs; ++Tab)
		if(Tab != LastTClientTab)
			Units.push_back({MENU_TEXT_PLAN_UNIT_TCLIENT_TAB, TClientPage, Tab});
	for(int Tab = 0; Tab < NumQmClientTabs; ++Tab)
		if(Tab != HiddenQmClientTab && Tab != LastQmClientTab)
			Units.push_back({MENU_TEXT_PLAN_UNIT_QMCLIENT_TAB, QmClientPage, Tab});
	for(int Page : BasePages)
		Units.push_back({MENU_TEXT_PLAN_UNIT_BASE_PAGE, Page, -1});
	return Units;
}
#endif
