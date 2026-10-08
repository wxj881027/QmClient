#include "QmCardCatalog.h"
#include "QmCardCatalogTClientInternal.h"

#include <base/perf_timer.h>
#include <base/str.h>

#include <engine/shared/config.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmScroll.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui_scrollregion.h>

#include <array>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace qm_card_catalog
{

	uint64_t QmCardRenderHook::PrepareStandardCardFamily(const SQmCardBuildContext &Ctx, int Family, std::vector<SSettingsCardDefinition> *pCards)
	{
		if(Ctx.m_pMenus == nullptr)
			return 0;
		switch(Family)
		{
		case 0: return Ctx.m_pMenus->BuildPlayerSettingsCards(Ctx, pCards);
		case 1: return Ctx.m_pMenus->BuildGraphicsSettingsCards(Ctx, pCards);
		case 2: return Ctx.m_pMenus->BuildSoundSettingsCards(Ctx, pCards);
		case 3: return Ctx.m_pMenus->BuildDDNetSettingsCards(Ctx, pCards);
		case 10: return Ctx.m_pMenus->BuildTee7SettingsCards(Ctx, pCards);
		default:
			if(Family >= 4 && Family < (int)10)
				return Ctx.m_pMenus->BuildAppearanceSettingsCards(Ctx, pCards, Family - 4);
			return 0;
		}
	}

	uint64_t QmCardRenderHook::PrepareStandardCards(const SQmCardBuildContext &Ctx, const std::vector<const char *> &vStableIds)
	{
		std::array<bool, 11> aPrepared{};
		uint64_t Revision = 0;
		for(const char *pStableId : vStableIds)
		{
			const int Family = StandardCardFamily(pStableId);
			if(Family < 0 || aPrepared[Family])
				continue;
			aPrepared[Family] = true;
			Revision = Revision * 1099511628211ULL ^ PrepareStandardCardFamily(Ctx, Family, nullptr);
		}
		return Revision;
	}

	bool QmCardRenderHook::BuildStandardCard(const SQmCardBuildContext &Ctx, const char *pStableId, SSettingsCardDefinition &Out)
	{
		const int Family = StandardCardFamily(pStableId);
		if(Family < 0)
			return false;
		std::vector<SSettingsCardDefinition> vCards;
		const uint64_t Revision = PrepareStandardCardFamily(Ctx, Family, &vCards);
		for(auto &Card : vCards)
		{
			if(str_comp(Card.m_Spec.m_pStableId, pStableId) != 0)
				continue;
			Out = std::move(Card);
			Out.m_MeasureRevision = Out.m_MeasureRevision * 1099511628211ULL ^ Revision;
			return true;
		}
		return false;
	}
}

void CMenus::RenderSettingsCatalogPage(CUIRect MainView, const char *pTab, bool PrewarmOnly)
{
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const bool TrackTClient = !ReadOnly && str_comp(pTab, "tclient") == 0;
	CPerfTimer LayoutBudgetTimer;
	std::optional<qm_tclient_cards::CUiRenderOnlyGuard> ReadOnlyGuard;
	if(ReadOnly && !Ui()->RenderOnly())
		ReadOnlyGuard.emplace(Ui());
	static std::map<std::string, CScrollRegion> s_ScrollRegions;
	CScrollRegion *pScroll = ReadOnly ? nullptr : &s_ScrollRegions[pTab];
	qm_card_catalog::SQmCardBuildContext Ctx;
	Ctx.m_pMenus = this;
	Ctx.m_ReadOnly = ReadOnly;
	Ctx.m_Metrics = ResolveSettingsContentMetrics(MainView.w);
	Ctx.m_Page = SettingsPageLayout(MainView, Ctx.m_Metrics.m_UiScale);
	Ctx.m_UiContext = SettingsUiContext(pTab, Ctx.m_Metrics.m_UiScale);
	if(ReadOnly)
	{
		Ctx.m_UiContext.m_pAnim = nullptr;
		Ctx.m_UiContext.m_pTree = nullptr;
	}
	Ctx.m_pScrollRegion = pScroll;
	const auto &vIds = qm_card_catalog::CatalogPageStableIds(pTab);
	const uint64_t ContentRevision = qm_card_catalog::QmCardRenderHook::PrepareStandardCards(Ctx, vIds) ^ qm_card_catalog::QmCardRenderHook::PrepareTClientCards(Ctx, vIds);
	const uint64_t Revision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, ContentRevision ^ str_quickhash(pTab));
	const auto BuildDefinitions = [Ctx, &vIds](std::vector<SSettingsCardDefinition> &vCards) {
		qm_card_catalog::BuildCards(Ctx, vIds, vCards);
	};
	if(!ReadOnly && !m_SettingsCardFocusStableId.empty())
	{
		m_SettingsCardDeck.RequestReveal(m_SettingsCardFocusStableId.c_str());
		m_SettingsCardFocusStableId.clear();
	}
	const auto ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, Ctx.m_Metrics.m_UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	SSettingsCardDeckFrameDiagnostics Diagnostics;
	SSettingsCardDeckInput InputState;
	InputState.m_pDiagnostics = TrackTClient ? &Diagnostics : nullptr;
	InputState.m_AllowHeaderDrag = !ReadOnly;
	if(!ReadOnly)
	{
		InputState.m_MouseX = Ui()->MouseX();
		InputState.m_MouseY = Ui()->MouseY();
		InputState.m_MousePressed = Ui()->MouseButtonClicked(0);
		InputState.m_MouseDown = Ui()->MouseButton(0);
		InputState.m_MouseReleased = !InputState.m_MouseDown && Ui()->LastMouseButton(0);
		InputState.m_CtrlPressed = Input()->ModifierIsPressed();
		InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
		InputState.m_pScrollParams = &ScrollParams;
	}
	const auto Result = SettingsCardDeckForRenderPass().RenderCached(Ctx.m_UiContext, Ctx.m_Page, pTab, Revision, BuildDefinitions, SettingsCardOrderModelForRenderPass(), pScroll, InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions());
	if(!ReadOnly && Result.m_OrderChanged)
		SaveSettingsCardOrderModel();
	if(TrackTClient)
	{
		m_SettingsTClientCurrentScrollY = pScroll->ContentScrollOffsetY();
		m_SettingsRuntimeMetadata.m_LastScrollPage = SETTINGS_TCLIENT;
		m_SettingsRuntimeMetadata.m_LastScrollY = m_SettingsTClientCurrentScrollY;
		m_SettingsRuntimeMetadata.m_Valid = true;
		SSettingsUiBudgetFrame UiBudget;
		UiBudget.m_LayoutMs = LayoutBudgetTimer.ElapsedMs();
		UiBudget.m_VisibleWidgets = static_cast<int>(Diagnostics.m_RenderedCardCount);
		UiBudget.m_Tab = m_TClientSettingsTab;
		UiBudget.m_Subtab = m_TClientSettingsTab;
		LogSettingsUiBudget("settings:tclient", UiBudget);
	}
}
