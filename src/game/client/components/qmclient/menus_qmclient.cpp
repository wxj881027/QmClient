#include "menus_qmclient_internal.h"

#include <base/perf_timer.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/QmUi/QmScroll.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsCardCollapseState.h>
#include <game/client/QmUi/UiDogfood.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/cards/QmCardMeasureRevision.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/gameclient.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <vector>

using namespace qm_settings_menu;

namespace
{
	bool PerfDebugEnabled()
	{
		return g_Config.m_QmPerfDebug != 0;
	}

	void LogQmPerfStage(IClient *pClient, const char *pStage, double DurationMs, bool Force = false, const char *pExtra = nullptr)
	{
		if(!PerfDebugEnabled())
			return;
		QmPerfLogStage("perf/qmclient", pStage, DurationMs, Force, pClient, nullptr, nullptr, pExtra);
	}

	const char *QmSettingsTabName(int Tab)
	{
		switch(Tab)
		{
		case CMenus::QMCLIENT_SETTINGS_TAB_VISUAL: return "visuals";
		case CMenus::QMCLIENT_SETTINGS_TAB_FUNCTION: return "functions";
		case CMenus::QMCLIENT_SETTINGS_TAB_HUD: return "hud";
		case CMenus::QMCLIENT_SETTINGS_TAB_CONTRIBUTORS: return "contributors";
		case CMenus::QMCLIENT_SETTINGS_TAB_CONFIG: return "config";
		default: return "unknown";
		}
	}

}

void CMenus::RenderSettingsQmClientHudDeck(CUIRect MainView, bool PrewarmOnly)
{
	using namespace qm_module;
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const float UiScale = Metrics.m_UiScale;
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	const float LabelWidth = ResolveSettingsCardLabelWidth(Page.m_TwoColumns ? Page.m_aColumns[0].w : Page.m_ContentViewport.w, Metrics);
	IUiContext CardCtx = SettingsUiContext("settings_qmclient_hud", UiScale);
	if(ReadOnly)
	{
		CardCtx.m_pAnim = nullptr;
		CardCtx.m_pTree = nullptr;
	}
	static CScrollRegion s_ScrollRegion;
	static std::array<bool, QmModuleCount> s_aCollapsed = {};
	static std::array<CButtonContainer, QmModuleCount> s_aCollapseButtons;
	qm_card_collapse::SyncQmModules(s_aCollapsed);

	auto ToggleCollapsed = [](void *, EQmModuleId Id) {
		const int Index = std::clamp((int)Id, 0, (int)QmModuleCount - 1);
		if(qm_card_collapse::SetQmModuleCollapsed(Id, !s_aCollapsed[Index]))
			s_aCollapsed[Index] = !s_aCollapsed[Index];
	};
	auto MeasureContentRevision = [](EQmModuleId Id) -> uint64_t {
		return qm_card_catalog::MeasureModuleCardRevision(Id);
	};

	// 卡片改由全局卡片目录构造（N3）：页面只声明「这一页有哪些卡片」，测量与渲染都在目录里。
	// 注意：目录的 Hud 清单含独立的 qm:lyrics 卡（本地此前把歌词画在 SMTC 卡内，已在上方移出），
	// 故切换后 HUD 页会多出一张歌词卡——这是远程的结构意图，注册表与布局表本地早已具备。
	const SQmSettingsCardStyle CardStyle = QmSettingsCardStyle(UiScale);
	auto BuildDefinitions = [&](std::vector<SSettingsCardDefinition> &vCards) {
		qm_card_catalog::SQmCardBuildContext CardBuild;
		CardBuild.m_pMenus = this;
		CardBuild.m_ReadOnly = ReadOnly;
		CardBuild.m_Page = Page;
		CardBuild.m_Metrics = Metrics;
		CardBuild.m_LabelWidth = LabelWidth;
		CardBuild.m_UiContext = CardCtx;
		CardBuild.m_Padding = CardStyle.m_Padding;
		CardBuild.m_CornerRadius = CardStyle.m_CornerRadius;
		CardBuild.m_pCollapsed = s_aCollapsed.data();
		CardBuild.m_pCollapseButtons = s_aCollapseButtons.data();
		CardBuild.m_pToggleCollapsed = ToggleCollapsed;
		qm_card_catalog::BuildCards(CardBuild, qm_card_catalog::HudCardStableIds(), vCards);
	};
	uint64_t CardLayoutRevision = 0;
	for(int ModuleIndex = 0; ModuleIndex < (int)QmModuleCount; ++ModuleIndex)
	{
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ MeasureContentRevision((EQmModuleId)ModuleIndex);
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (s_aCollapsed[ModuleIndex] ? 1u : 0u);
	}
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, CardLayoutRevision);

	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = ReadOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = ReadOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !ReadOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !ReadOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !ReadOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = !ReadOnly && Input()->ModifierIsPressed();
	InputState.m_AllowHeaderDrag = !ReadOnly;
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = Ui()->MeasurementOnly() ? nullptr : &ScrollParams;
	static qm_card_order::CModel s_HudPrewarmOrderModel;
	static bool s_HudPrewarmOrderModelInitialized = false;
	static CSettingsCardDeck s_HudPrewarmDeck;
	if(Ui()->MeasurementOnly() && !s_HudPrewarmOrderModelInitialized)
	{
		s_HudPrewarmOrderModel.LoadMerged("", qm_card_registry::BuildDefaultEntries());
		s_HudPrewarmOrderModelInitialized = true;
	}
	qm_card_order::CModel &CardOrderModel = Ui()->MeasurementOnly() ? s_HudPrewarmOrderModel : SettingsCardOrderModel();
	CSettingsCardDeck &CardDeck = Ui()->MeasurementOnly() ? s_HudPrewarmDeck : m_SettingsCardDeck;
	const SSettingsCardDeckResult DeckResult = CardDeck.RenderCached(CardCtx, Page, "hud", DefinitionsRevision, BuildDefinitions, CardOrderModel, Ui()->MeasurementOnly() ? nullptr : &s_ScrollRegion, InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions());
	if(!ReadOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
}

void CMenus::RenderSettingsQmClientFunctionDeck(CUIRect MainView, bool PrewarmOnly)
{
	using namespace qm_module;
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const float UiScale = Metrics.m_UiScale;
	const SQmSettingsCardStyle CardStyle = QmSettingsCardStyle(UiScale);
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	const float LabelWidth = ResolveSettingsCardLabelWidth(Page.m_TwoColumns ? Page.m_aColumns[0].w : Page.m_ContentViewport.w, Metrics);
	IUiContext CardCtx = SettingsUiContext("settings_qmclient_function", UiScale);
	if(ReadOnly)
	{
		CardCtx.m_pAnim = nullptr;
		CardCtx.m_pTree = nullptr;
	}
	static CScrollRegion s_ScrollRegion;
	static std::array<bool, QmModuleCount> s_aCollapsed = {};
	static std::array<CButtonContainer, QmModuleCount> s_aCollapseButtons;
	qm_card_collapse::SyncQmModules(s_aCollapsed);

	const size_t FavoriteMapCount = GameClient()->TClientComponent().GetFavoriteMaps().size();
	qm_card_catalog::FavoriteMapsLayoutRevision(FavoriteMapCount);
	auto ToggleCollapsed = [](void *pUser, EQmModuleId Id) {
		const int Index = std::clamp((int)Id, 0, (int)QmModuleCount - 1);
		const bool WasCollapsed = s_aCollapsed[Index];
		const bool Collapsed = !WasCollapsed;
		if(!qm_card_collapse::SetQmModuleCollapsed(Id, Collapsed))
			return;
		s_aCollapsed[Index] = Collapsed;
		if(Id == EQmModuleId::FavoriteMaps && WasCollapsed != Collapsed && !Collapsed)
		{
			const size_t FavoriteMapCount = static_cast<CMenus *>(pUser)->GameClient()->TClientComponent().GetFavoriteMaps().size();
			qm_card_catalog::FavoriteMapsLayoutRevision(FavoriteMapCount);
		}
	};
	auto MeasureContentRevision = [this](EQmModuleId Id) -> uint64_t {
		return qm_card_catalog::MeasureModuleCardRevision(Id, ResolveFunctionCardLayoutState(GameClient()->TClientComponent().GetFavoriteMaps().size()));
	};
	// 卡片改由全局卡片目录构造（N3）：页面只声明「这一页有哪些卡片」，测量与渲染都在目录里。
	// 内容量状态（词条过滤/关键词回复/收藏地图）经 m_pFunctionLayout 注入，目录测量依赖它。
	const qm_card_catalog::SQmFunctionCardLayoutState FunctionCardLayout = ResolveFunctionCardLayoutState(GameClient()->TClientComponent().GetFavoriteMaps().size());
	auto BuildDefinitions = [&](std::vector<SSettingsCardDefinition> &vCards) {
		qm_card_catalog::SQmCardBuildContext CardBuild;
		CardBuild.m_pMenus = this;
		CardBuild.m_ReadOnly = ReadOnly;
		CardBuild.m_Page = Page;
		CardBuild.m_Metrics = Metrics;
		CardBuild.m_LabelWidth = LabelWidth;
		CardBuild.m_UiContext = CardCtx;
		CardBuild.m_Padding = CardStyle.m_Padding;
		CardBuild.m_CornerRadius = CardStyle.m_CornerRadius;
		CardBuild.m_pCollapsed = s_aCollapsed.data();
		CardBuild.m_pFunctionLayout = &FunctionCardLayout;
		CardBuild.m_pCollapseButtons = s_aCollapseButtons.data();
		CardBuild.m_pToggleCollapsed = ToggleCollapsed;
		CardBuild.m_pToggleCollapsedUser = this;
		qm_card_catalog::BuildCards(CardBuild, qm_card_catalog::FunctionCardStableIds(), vCards);
	};
	uint64_t CardLayoutRevision = 0;
	for(int ModuleIndex = 0; ModuleIndex < (int)QmModuleCount; ++ModuleIndex)
	{
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ MeasureContentRevision((EQmModuleId)ModuleIndex);
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (s_aCollapsed[ModuleIndex] ? 1u : 0u);
	}
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, CardLayoutRevision);

	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = ReadOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = ReadOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !ReadOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !ReadOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !ReadOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = !ReadOnly && Input()->ModifierIsPressed();
	InputState.m_AllowHeaderDrag = !ReadOnly;
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = Ui()->MeasurementOnly() ? nullptr : &ScrollParams;
	static qm_card_order::CModel s_FunctionPrewarmOrderModel;
	static bool s_FunctionPrewarmOrderModelInitialized = false;
	static CSettingsCardDeck s_FunctionPrewarmDeck;
	if(Ui()->MeasurementOnly() && !s_FunctionPrewarmOrderModelInitialized)
	{
		s_FunctionPrewarmOrderModel.LoadMerged("", qm_card_registry::BuildDefaultEntries());
		s_FunctionPrewarmOrderModelInitialized = true;
	}
	qm_card_order::CModel &CardOrderModel = Ui()->MeasurementOnly() ? s_FunctionPrewarmOrderModel : SettingsCardOrderModel();
	CSettingsCardDeck &CardDeck = Ui()->MeasurementOnly() ? s_FunctionPrewarmDeck : m_SettingsCardDeck;
	const uint64_t MeasurementEnvironmentRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, 0);
	const SSettingsCardDeckResult DeckResult = CardDeck.RenderCached(CardCtx, Page, "function", DefinitionsRevision, BuildDefinitions, CardOrderModel, Ui()->MeasurementOnly() ? nullptr : &s_ScrollRegion, InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions(), MeasurementEnvironmentRevision);
	if(!ReadOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
}

void CMenus::RenderSettingsQmClientVisualDeck(CUIRect MainView, bool PrewarmOnly)
{
	using namespace qm_module;
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const float UiScale = Metrics.m_UiScale;
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	const float LabelWidth = ResolveSettingsCardLabelWidth(Page.m_TwoColumns ? Page.m_aColumns[0].w : Page.m_ContentViewport.w, Metrics);
	IUiContext CardCtx = SettingsUiContext("settings_qmclient_visual", UiScale);
	if(ReadOnly)
	{
		CardCtx.m_pAnim = nullptr;
		CardCtx.m_pTree = nullptr;
	}
	static CScrollRegion s_ScrollRegion;
	static std::array<bool, QmModuleCount> s_aCollapsed = {};
	static std::array<CButtonContainer, QmModuleCount> s_aCollapseButtons;
	qm_card_collapse::SyncQmModules(s_aCollapsed);

	auto ToggleCollapsed = [](void *, EQmModuleId Id) {
		const int Index = std::clamp((int)Id, 0, (int)QmModuleCount - 1);
		if(qm_card_collapse::SetQmModuleCollapsed(Id, !s_aCollapsed[Index]))
			s_aCollapsed[Index] = !s_aCollapsed[Index];
	};
	auto MeasureContentRevision = [](EQmModuleId Id) -> uint64_t {
		return qm_card_catalog::MeasureModuleCardRevision(Id);
	};

	// 卡片改由全局卡片目录构造（N3）：页面只声明「这一页有哪些卡片」，测量与渲染都在目录里。
	const SQmSettingsCardStyle CardStyle = QmSettingsCardStyle(UiScale);
	auto BuildDefinitions = [&](std::vector<SSettingsCardDefinition> &vCards) {
		qm_card_catalog::SQmCardBuildContext CardBuild;
		CardBuild.m_pMenus = this;
		CardBuild.m_ReadOnly = ReadOnly;
		CardBuild.m_Page = Page;
		CardBuild.m_Metrics = Metrics;
		CardBuild.m_LabelWidth = LabelWidth;
		CardBuild.m_UiContext = CardCtx;
		CardBuild.m_Padding = CardStyle.m_Padding;
		CardBuild.m_CornerRadius = CardStyle.m_CornerRadius;
		CardBuild.m_pCollapsed = s_aCollapsed.data();
		CardBuild.m_pCollapseButtons = s_aCollapseButtons.data();
		CardBuild.m_pToggleCollapsed = ToggleCollapsed;
		qm_card_catalog::BuildCards(CardBuild, qm_card_catalog::VisualCardStableIds(), vCards);
	};
	uint64_t CardLayoutRevision = 0;
	for(int ModuleIndex = 0; ModuleIndex < (int)QmModuleCount; ++ModuleIndex)
	{
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ MeasureContentRevision((EQmModuleId)ModuleIndex);
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (s_aCollapsed[ModuleIndex] ? 1u : 0u);
	}
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, CardLayoutRevision);

	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = ReadOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = ReadOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !ReadOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !ReadOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !ReadOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = !ReadOnly && Input()->ModifierIsPressed();
	InputState.m_AllowHeaderDrag = !ReadOnly;
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = Ui()->MeasurementOnly() ? nullptr : &ScrollParams;
	static qm_card_order::CModel s_VisualPrewarmOrderModel;
	static bool s_VisualPrewarmOrderModelInitialized = false;
	static CSettingsCardDeck s_VisualPrewarmDeck;
	if(Ui()->MeasurementOnly() && !s_VisualPrewarmOrderModelInitialized)
	{
		s_VisualPrewarmOrderModel.LoadMerged("", qm_card_registry::BuildDefaultEntries());
		s_VisualPrewarmOrderModelInitialized = true;
	}
	qm_card_order::CModel &CardOrderModel = Ui()->MeasurementOnly() ? s_VisualPrewarmOrderModel : SettingsCardOrderModel();
	CSettingsCardDeck &CardDeck = Ui()->MeasurementOnly() ? s_VisualPrewarmDeck : m_SettingsCardDeck;
	const SSettingsCardDeckResult DeckResult = CardDeck.RenderCached(CardCtx, Page, "visual", DefinitionsRevision, BuildDefinitions, CardOrderModel, Ui()->MeasurementOnly() ? nullptr : &s_ScrollRegion, InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions());
	if(!ReadOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
}

void CMenus::RenderSettingsQmClient(CUIRect MainView, bool PrewarmOnly)
{
	// 文本计划收集等 RenderOnly 场景也要保证绑定缓存可用（幂等）。
	EnsureSettingsBindCache();

	RenderSettingsQmClientContent(MainView, PrewarmOnly);
}

void CMenus::RenderSettingsQmClientContent(CUIRect MainView, bool PrewarmOnly)
{
	using namespace qm_module;

	// feat-003 dogfood: when dbg_qm_ui_dogfood is on, take over the QmClient
	// settings panel and render the widget gallery. First visible verification
	// of feat-002 (animation runtime) + feat-003 (tokens + 11 widgets).
	if(g_Config.m_DbgQmUiDogfood != 0)
	{
		if(PrewarmOnly)
			return;

		IUiContext Ctx;
		Ctx.m_pUi = Ui();
		Ctx.m_pMenus = this;
		Ctx.m_pTextRender = TextRender();
		Ctx.m_pTooltips = &GameClient()->m_Tooltips;
		Ctx.m_pAnim = PrewarmOnly ? nullptr : &GameClient()->UiRuntimeV2()->AnimRuntime();
		Ctx.m_pTree = PrewarmOnly ? nullptr : &GameClient()->UiRuntimeV2()->Tree();
		Ctx.m_ScopeHash = MakeUiScopeHash("qm_ui_dogfood");
		Ctx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
		RenderQmUiDogfood(Ctx, MainView);
		return;
	}

	CPerfTimer RenderTimer;
	const float QmClientUiScale = ResolveSettingsContentMetrics(MainView.w).m_UiScale;
	bool TabTransitionActive = false;
	static bool s_QmTabTelemetryInitialized = false;
	static int s_PrevQmTab = QMCLIENT_SETTINGS_TAB_VISUAL;
	CUIRect TabContentClip = MainView;

	{
		if(m_QmClientSettingsTab < 0 || m_QmClientSettingsTab >= NUMBER_OF_QMCLIENT_SETTINGS_TABS)
			m_QmClientSettingsTab = QMCLIENT_SETTINGS_TAB_VISUAL;
		// 贡献者子页签已并入顶层「贡献者」页；旧持久化索引占位值回落到首个子页签。
		if(m_QmClientSettingsTab == QMCLIENT_SETTINGS_TAB_CONTRIBUTORS)
			m_QmClientSettingsTab = QMCLIENT_SETTINGS_TAB_VISUAL;

		CUIRect TabBar;
		const SSettingsSubTabLayoutFrame QmClientSubTabs = ResolveSettingsSubTabLayout(MainView, QmClientUiScale);
		TabBar = QmClientSubTabs.m_TabBarRect;
		MainView = QmClientSubTabs.m_ContentRect;
		// 贡献者枚举保留占位以兼容旧配置，可见页签使用独立列表。
		constexpr int aVisibleQmTabs[] = {QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_CONFIG};
		constexpr int NumQmTabs = (int)std::size(aVisibleQmTabs);
		const float TabWidth = TabBar.w / (float)NumQmTabs;
		static CButtonContainer s_aPageTabs[NUMBER_OF_QMCLIENT_SETTINGS_TABS] = {};
		const char *apQmTabNames[NUMBER_OF_QMCLIENT_SETTINGS_TABS] = {};
		apQmTabNames[QMCLIENT_SETTINGS_TAB_VISUAL] = Localize("Visuals");
		apQmTabNames[QMCLIENT_SETTINGS_TAB_FUNCTION] = Localize("Functions");
		apQmTabNames[QMCLIENT_SETTINGS_TAB_HUD] = Localize("HUD");
		apQmTabNames[QMCLIENT_SETTINGS_TAB_CONFIG] = Localize("Config");

		{
			CPerfTimer StageTimer;
			// 胶囊 Tabbar：槽位先算完，再画容器与滑块，最后画页签文字 —— 滑块压在文字之下。
			CUIRect aQmTabSlots[NumQmTabs];
			CUIRect QmTabsRemainder = TabBar;
			for(int Tab = 0; Tab < NumQmTabs; ++Tab)
				QmTabsRemainder.VSplitLeft(TabWidth, &aQmTabSlots[Tab], &QmTabsRemainder);
			int ActiveQmTabSlot = -1;
			for(int Slot = 0; Slot < NumQmTabs; ++Slot)
			{
				if(aVisibleQmTabs[Slot] == m_QmClientSettingsTab)
					ActiveQmTabSlot = Slot;
			}
			const IUiContext QmTabBarCtx = TabBarUiContext();
			const int SelectedTab = ui_widget::CapsuleTabBar(QmTabBarCtx, MakeUiScopeHash("settings_qmclient_tabs_capsule"), aQmTabSlots, NumQmTabs, ActiveQmTabSlot, SettingsCapsuleTabBarStyle());
			if(!PrewarmOnly && SelectedTab >= 0)
				m_QmClientSettingsTab = aVisibleQmTabs[SelectedTab];
			for(int Tab = 0; Tab < NumQmTabs; ++Tab)
			{
				const int PageTab = aVisibleQmTabs[Tab];
				DoButton_MenuTab(&s_aPageTabs[PageTab], apQmTabNames[PageTab], m_QmClientSettingsTab == PageTab, &aQmTabSlots[Tab], IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f, nullptr, nullptr, -1.0f, true);
			}

			char aTabExtra[96];
			str_format(aTabExtra, sizeof(aTabExtra), "tab=%s", QmSettingsTabName(m_QmClientSettingsTab));
			LogQmPerfStage(Client(), "tabbar", StageTimer.ElapsedMs(), false, aTabExtra);
		}

		if(!PrewarmOnly)
		{
			if(!s_QmTabTelemetryInitialized)
			{
				s_PrevQmTab = m_QmClientSettingsTab;
				s_QmTabTelemetryInitialized = true;
			}
			else if(m_QmClientSettingsTab != s_PrevQmTab)
			{
				if(PerfDebugEnabled())
				{
					char aPayload[128];
					str_format(aPayload, sizeof(aPayload), "event=tab_switch from=%s to=%s", QmSettingsTabName(s_PrevQmTab), QmSettingsTabName(m_QmClientSettingsTab));
					QmPerfLogPayload("perf/qmclient", aPayload, Client(), CurrentQmUiPerfPage());
				}
				s_PrevQmTab = m_QmClientSettingsTab;
			}
		}

		CUIRect ContentView = MainView;
		// 子 Tab 的入场由设置 Card Deck 统一处理，避免和页面局部状态叠加。
		TabTransitionActive = false;
		TabContentClip = MainView;
		if(TabTransitionActive)
		{
			Ui()->ClipEnable(&TabContentClip);
		}

		if(m_QmClientSettingsTab == QMCLIENT_SETTINGS_TAB_CONFIG)
		{
			CPerfTimer StageTimer;
			if(!PrewarmOnly)
				RenderSettingsTClientConfigs(ContentView);
			char aConfigExtra[96];
			str_format(aConfigExtra, sizeof(aConfigExtra), "tab=%s transition=%d", QmSettingsTabName(m_QmClientSettingsTab), TabTransitionActive ? 1 : 0);
			LogQmPerfStage(Client(), "config_tab_total", StageTimer.ElapsedMs(), TabTransitionActive, aConfigExtra);
			if(TabTransitionActive)
				Ui()->ClipDisable();
			LogQmPerfStage(Client(), "render_total", RenderTimer.ElapsedMs(), false, aConfigExtra);
			return;
		}
		if(m_QmClientSettingsTab == QMCLIENT_SETTINGS_TAB_VISUAL)
		{
			RenderSettingsQmClientVisualDeck(ContentView, PrewarmOnly);
			if(TabTransitionActive)
				Ui()->ClipDisable();
			return;
		}
		if(m_QmClientSettingsTab == QMCLIENT_SETTINGS_TAB_FUNCTION)
		{
			RenderSettingsQmClientFunctionDeck(ContentView, PrewarmOnly);
			if(TabTransitionActive)
				Ui()->ClipDisable();
			return;
		}
		if(m_QmClientSettingsTab == QMCLIENT_SETTINGS_TAB_HUD)
		{
			RenderSettingsQmClientHudDeck(ContentView, PrewarmOnly);
			if(TabTransitionActive)
				Ui()->ClipDisable();
			return;
		}
		MainView = ContentView;
	}
}
