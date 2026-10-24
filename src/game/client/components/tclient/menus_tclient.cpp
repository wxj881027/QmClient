#include "menus_tclient_internal.h"

#include <base/perf_timer.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/QmUi/UiNavigation.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

using namespace qm_tclient_cards;
using namespace qm_tclient_menu;

void CMenus::RenderSettingsTClient(CUIRect MainView, bool PrewarmOnly)
{
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	if(!ReadOnly)
		EnsureSettingsBindCache();

	ApplyTClientContentMetrics(MainView.w);
	CPerfTimer RenderTimer;
	if(!ReadOnly)
		AdvancePreviewTime(Client()->RenderFrameTime());

	CUIRect TabBar;
	int ActiveTab = m_TClientSettingsTab;
	if(ActiveTab < 0 || ActiveTab >= NUMBER_OF_TCLIENT_TABS)
		ActiveTab = TCLIENT_TAB_SETTINGS;
	if(!ReadOnly)
		m_TClientSettingsTab = ActiveTab;

	MainView = TClientSettingsContentView(MainView, &TabBar);
	const float TabWidth = TabBar.w / NUMBER_OF_TCLIENT_TABS;
	static CButtonContainer s_aPageTabs[NUMBER_OF_TCLIENT_TABS] = {};
	// 译文只在本次绘制使用；同文件重载也会释放语言库的旧字符串。
	const char *apTClientTabNames[NUMBER_OF_TCLIENT_TABS] = {};
	apTClientTabNames[TCLIENT_TAB_SETTINGS] = Localize("Settings");
	apTClientTabNames[TCLIENT_TAB_BINDWHEEL] = Localize("Bind Wheel");
	apTClientTabNames[TCLIENT_TAB_WARLIST] = Localize("War List");
	apTClientTabNames[TCLIENT_TAB_BINDCHAT] = Localize("Chat Binds");
	apTClientTabNames[TCLIENT_TAB_STATUSBAR] = Localize("Status Bar");

	// 胶囊 Tabbar：可见页签槽位先算完，再画容器与滑块，最后画页签文字。
	CUIRect aTClientTabSlots[NUMBER_OF_TCLIENT_TABS];
	int aTClientTabPages[NUMBER_OF_TCLIENT_TABS];
	int NumTClientTabs = 0;
	int ActiveTClientTab = -1;
	{
		CUIRect TabsRemainder = TabBar;
		for(int Tab = 0; Tab < NUMBER_OF_TCLIENT_TABS; ++Tab)
		{
			TabsRemainder.VSplitLeft(TabWidth, &aTClientTabSlots[NumTClientTabs], &TabsRemainder);
			aTClientTabPages[NumTClientTabs] = Tab;
			if(ActiveTab == Tab)
				ActiveTClientTab = NumTClientTabs;
			++NumTClientTabs;
		}
	}
	if(NumTClientTabs > 0)
	{
		const IUiContext TClientTabBarCtx = TabBarUiContext();
		const int SelectedTab = ui_widget::CapsuleTabBar(TClientTabBarCtx, MakeUiScopeHash("settings_tclient_tabs_capsule"), aTClientTabSlots, NumTClientTabs, ActiveTClientTab, SettingsCapsuleTabBarStyle());
		if(!ReadOnly && SelectedTab >= 0)
		{
			ActiveTab = aTClientTabPages[SelectedTab];
			m_TClientSettingsTab = ActiveTab;
		}
		for(int TabIndex = 0; TabIndex < NumTClientTabs; ++TabIndex)
		{
			const int Tab = aTClientTabPages[TabIndex];
			DoButton_MenuTab(&s_aPageTabs[Tab], apTClientTabNames[Tab], ActiveTab == Tab, &aTClientTabSlots[TabIndex], IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f, nullptr, nullptr, -1.0f, true);
		}
	}

	CUIRect ContentView = MainView;
	// 子 Tab 的入场由设置 Card Deck 统一处理，避免和页面级位移动效叠加。
	const bool TransitionActive = false;

	{
		CPerfTimer StageTimer;
		if(ActiveTab == TCLIENT_TAB_SETTINGS)
		{
			RenderSettingsTClientSettings(ContentView, ReadOnly);
		}
		if(ActiveTab == TCLIENT_TAB_BINDCHAT)
			RenderSettingsTClientChatBinds(ContentView, ReadOnly);
		if(ActiveTab == TCLIENT_TAB_BINDWHEEL)
			RenderSettingsTClientBindWheel(ContentView, ReadOnly);
		if(ActiveTab == TCLIENT_TAB_WARLIST)
			RenderSettingsTClientWarList(ContentView, ReadOnly);
		if(ActiveTab == TCLIENT_TAB_STATUSBAR)
			RenderSettingsTClientStatusBar(ContentView, ReadOnly);
		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "tab=%d transition=%d", ActiveTab, TransitionActive ? 1 : 0);
		LogTClientPerfStageEx("tclient_tab", nullptr, ETClientSettingsPerfStage::TAB_SHELL, StageTimer.ElapsedMs(), TransitionActive, aExtra);
		const char *pTabShellStage = nullptr;
		switch(ActiveTab)
		{
		case TCLIENT_TAB_BINDCHAT: pTabShellStage = "tclient_tab_3_shell"; break;
		case TCLIENT_TAB_STATUSBAR: pTabShellStage = "tclient_tab_4_shell"; break;
		default: break;
		}
		if(pTabShellStage != nullptr)
			LogTClientPerfStage(pTabShellStage, StageTimer.ElapsedMs(), TransitionActive, aExtra);
		LogTClientPerfStage("tclient_tab_content", StageTimer.ElapsedMs(), TransitionActive, aExtra);
	}

	char aExtra[96];
	str_format(aExtra, sizeof(aExtra), "tab=%d transition=%d", ActiveTab, TransitionActive ? 1 : 0);
	LogTClientPerfStage("tclient_page_total", RenderTimer.ElapsedMs(), false, aExtra);
	if(!ReadOnly)
	{
		m_SettingsRuntimeMetadata.m_LastTClientTab = ActiveTab;
		m_SettingsRuntimeMetadata.m_Valid = true;
	}
}

void CMenus::RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly)
{
	RenderSettingsCatalogPage(MainView, "tclient", PrewarmOnly);
}

void CMenus::RenderSettingsTClientBindWheel(CUIRect MainView, bool PrewarmOnly)
{
	RenderSettingsCatalogPage(MainView, "tclient-bind-wheel", PrewarmOnly);
}

void CMenus::RenderSettingsTClientChatBinds(CUIRect MainView, bool PrewarmOnly)
{
	RenderSettingsCatalogPage(MainView, "tclient-chat-binds", PrewarmOnly);
}

void CMenus::RenderSettingsTClientWarList(CUIRect MainView, bool PrewarmOnly)
{
	RenderSettingsCatalogPage(MainView, "tclient-warlist", PrewarmOnly);
}

void CMenus::RenderSettingsTClientStatusBar(CUIRect MainView, bool PrewarmOnly)
{
	RenderSettingsCatalogPage(MainView, "tclient-status-bar", PrewarmOnly);
}

void CMenus::RenderSettingsTClientProfiles(CUIRect MainView, bool PrewarmOnly)
{
	RenderSettingsCatalogPage(MainView, "tclient-profiles", PrewarmOnly);
}

void CMenus::RenderSettingsTClientConfigs(CUIRect MainView, bool PrewarmOnly)
{
	RenderSettingsCatalogPage(MainView, "tclient-configs", PrewarmOnly);
}
