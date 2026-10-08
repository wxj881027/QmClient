/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/system.h>

#include <engine/client/backend/graphics_backend_contract.h>
#include <engine/external/tinyexpr.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/shared/protocol7.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/updater.h>

#include <generated/protocol.h>

#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmUiPerf.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsIconOptions.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/QmUi/cards/QmCardCatalogTeeMetrics.h>
#include <game/client/animstate.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/message_gradient.h>
#include <game/client/components/qmclient/modes.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/qmclient/settings_resource_preview.h>
#include <game/client/components/qmclient/tee_color_code.h>
#include <game/client/components/qmclient/tee_hue_cycle.h>
#include <game/client/components/qmclient/tee_skin_apply.h>
#include <game/client/components/skins.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using namespace FontIcons;
using namespace std::chrono_literals;

namespace
{
	static bool ApplyBackgroundEntitiesInputValue(CLineInput &Input)
	{
		char aNormalized[IO_MAX_PATH_LENGTH];
		const bool Changed = BuildBackgroundEntitiesCommitValueFromInput(Input.GetString(), g_Config.m_ClBackgroundEntities, aNormalized, sizeof(aNormalized));
		if(Changed)
			str_copy(g_Config.m_ClBackgroundEntities, aNormalized, sizeof(g_Config.m_ClBackgroundEntities));
		if(Input.IsActive())
			Input.Deactivate();
		return Changed;
	}

	static void SyncBackgroundEntitiesInput(CLineInput &Input, char *pSync, int SyncSize)
	{
		char aNormalizedConfig[IO_MAX_PATH_LENGTH];
		BuildBackgroundEntitiesValueFromInput(g_Config.m_ClBackgroundEntities, aNormalizedConfig, sizeof(aNormalizedConfig));
		if(str_comp(pSync, aNormalizedConfig) != 0)
		{
			if(!Input.IsActive())
				Input.Set(aNormalizedConfig);
		}
		str_copy(pSync, aNormalizedConfig, SyncSize);
	}

	static bool CommitBackgroundEntitiesInputIfActive(CLineInput &Input, char *pSync, int SyncSize)
	{
		if(!Input.IsActive())
			return false;

		const bool Changed = ApplyBackgroundEntitiesInputValue(Input);
		SyncBackgroundEntitiesInput(Input, pSync, SyncSize);
		return Changed;
	}

	static bool ToggleCurrentMapBackground(CLineInput &Input)
	{
		const bool UseCurrentMap = IsCurrentMapBackgroundEntitiesValue(g_Config.m_ClBackgroundEntities);
		Input.Deactivate();
		if(UseCurrentMap)
			g_Config.m_ClBackgroundEntities[0] = '\0';
		else
			str_copy(g_Config.m_ClBackgroundEntities, CURRENT_MAP);
		return true;
	}

	void LogPerfStage(IClient *pClient, const char *pStage, double DurationMs, bool Force = false, const char *pExtra = nullptr)
	{
		QmPerfLogStage("perf/menu", pStage, DurationMs, Force, pClient, nullptr, nullptr, pExtra);
	}
}

uint64_t CMenus::BuildDDNetSettingsCards(const qm_card_catalog::SQmCardBuildContext &Ctx, std::vector<SSettingsCardDefinition> *pCards)
{
	const CUIRect MainView = Ctx.m_Page.m_ContentViewport;
	CPerfTimer ShellTimer;
	CUIRect Button, Left, Right, LeftLeft, Label;
	LogPerfStage(Client(), "ddnet_tab_shell", ShellTimer.ElapsedMs(), false, "page=ddnet");

	const SSettingsContentMetrics DDNetMetrics = Ctx.m_Metrics;
	const float UiScale = DDNetMetrics.m_UiScale;
	const float BodySize = DDNetMetrics.m_BodySize;
	const float DDNetRowPitch = DDNetMetrics.m_LineHeight + DDNetMetrics.m_LineSpacing;
	const auto SplitDDNetRow = [DDNetMetrics](CUIRect &View, CUIRect *pRow) {
		View.HSplitTop(DDNetMetrics.m_LineHeight, pRow, &View);
		View.HSplitTop(DDNetMetrics.m_LineSpacing, nullptr, &View);
	};
	const IUiContext DDNetCardCtx = Ctx.m_UiContext;
	const auto DoDDNetNumericField = [this, DDNetCardCtx, BodySize](const char *pTextId, const void *pId, int *pOption, const CUIRect &Rect, const char *pLabel, int Min, int Max, const IScrollbarScale *pScale = &CUi::ms_LinearScrollbarScale, unsigned Flags = 0u, const char *pSuffix = "", const char *pMaxText = nullptr) {
		ui_widget::SNumericFieldOptions Options;
		Options.m_pLabel = pLabel;
		Options.m_pSuffix = pSuffix;
		Options.m_pScale = pScale;
		Options.m_Flags = Flags;
		Options.m_pMaxText = pMaxText;
		Options.m_FontSize = BodySize;
		Options.m_LabelAlign = TEXTALIGN_ML;
		Options.m_CommitPolicy = (Flags & CUi::SCROLLBAR_OPTION_DELAYUPDATE) != 0 ? ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT : ui_widget::EInputCommitPolicy::LIVE;
		if(PrepareSettingsNumericFieldLabel(SETTINGS_DDNET, -1, -1, pTextId, Rect, pLabel, Flags, Options))
			return false;
		return ui_widget::NumericField(DDNetCardCtx, GetSettingsNumericFieldState(pId), pId, pOption, Min, Max, Rect, Options);
	};
	const qm_card_registry::SCardDefault *pDemoDefault = qm_card_registry::FindByStableId("deck:ddnet-demo");
	const qm_card_registry::SCardDefault *pGameplayDefault = qm_card_registry::FindByStableId("deck:ddnet-gameplay");
	const qm_card_registry::SCardDefault *pBackgroundDefault = qm_card_registry::FindByStableId("deck:ddnet-background");
	const qm_card_registry::SCardDefault *pMiscellaneousDefault = qm_card_registry::FindByStableId("deck:ddnet-miscellaneous");
	dbg_assert(pDemoDefault != nullptr && pGameplayDefault != nullptr && pBackgroundDefault != nullptr && pMiscellaneousDefault != nullptr, "DDNet settings cards must be registered");
	if(pDemoDefault == nullptr || pGameplayDefault == nullptr || pBackgroundDefault == nullptr || pMiscellaneousDefault == nullptr)
		return 0;
	const float CardChromeHeight = BuildSettingsCardFrame({0.0f, 0.0f, 1.0f, 0.0f}, {nullptr, nullptr, "subtitle"}, 0.0f, UiScale).m_Rect.h;
	const bool RenderOnly = Ctx.m_ReadOnly;
	const auto BuildDefinitions = [=, this](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(4);
		const SSettingsCardSpec DemoSpec{pDemoDefault->m_pStableId, Localize(pDemoDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pDemoDefault)};
		const SSettingsCardSpec GameplaySpec{pGameplayDefault->m_pStableId, Localize(pGameplayDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pGameplayDefault)};
		const SSettingsCardSpec BackgroundSpec{pBackgroundDefault->m_pStableId, Localize(pBackgroundDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pBackgroundDefault)};
		const SSettingsCardSpec MiscellaneousSpec{pMiscellaneousDefault->m_pStableId, Localize(pMiscellaneousDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pMiscellaneousDefault)};
		const auto AddCard = [&vCards](const SSettingsCardSpec &Spec, float MinHeight, float ChromeHeight, FSettingsCardRender Render) {
			SSettingsCardDefinition Definition;
			Definition.m_Spec = Spec;
			// MinHeight 已经按当前开关状态计算，测量阶段不能依赖上一帧绘制结果。
			Definition.m_Measure = [MinHeight, ChromeHeight](float) { return maximum(0.0f, MinHeight - ChromeHeight); };
			Definition.m_Render = std::move(Render);
			Definition.m_MeasureRevision = static_cast<uint64_t>(maximum(0.0f, MinHeight) * 1000.0f);
			vCards.push_back(std::move(Definition));
		};
		const auto ProcessDemoPreLayoutInput = [this, SplitDDNetRow](CUIRect ContentRect) {
			if(m_MenuTextPlanCollecting)
				return false;
			CUIRect Left = ContentRect;
			CUIRect Right;
			CUIRect Button;
			bool Changed = false;
			SplitDDNetRow(Left, &Button);
			SplitDDNetRow(Left, &Button);
			if(Ui()->DoButtonLogic(&g_Config.m_ClReplays, 0, &Button, BUTTONFLAG_LEFT))
			{
				g_Config.m_ClReplays ^= 1;
				if(Client()->State() == IClient::STATE_ONLINE)
					Client()->DemoRecorder_UpdateReplayRecorder();
				Changed = true;
			}
			if(g_Config.m_ClReplays)
			{
				SplitDDNetRow(Left, &Button);
				SplitDDNetRow(Left, &Button);
			}
			Right = Left;
			SplitDDNetRow(Right, &Button);
			if(Ui()->DoButtonLogic(&g_Config.m_ClRaceGhost, 0, &Button, BUTTONFLAG_LEFT))
			{
				g_Config.m_ClRaceGhost ^= 1;
				Changed = true;
			}
			if(g_Config.m_ClRaceGhost)
			{
				SplitDDNetRow(Right, &Button);
				SplitDDNetRow(Right, &Button);
				SplitDDNetRow(Right, &Button);
				if(Ui()->DoButtonLogic(&g_Config.m_ClRaceSaveGhost, 0, &Button, BUTTONFLAG_LEFT))
				{
					g_Config.m_ClRaceSaveGhost ^= 1;
					Changed = true;
				}
			}
			return Changed;
		};
		const auto ProcessGameplayPreLayoutInput = [this, SplitDDNetRow, DDNetMetrics, UiScale](CUIRect ContentRect) {
			if(m_MenuTextPlanCollecting)
				return false;
			CUIRect Gameplay = ContentRect;
			CUIRect PreLayoutButton;
			CUIRect GameplayRow;
			CUIRect TextEntitiesLabel;
			SplitDDNetRow(Gameplay, &PreLayoutButton);
			SplitDDNetRow(Gameplay, &GameplayRow);
			GameplayRow.VSplitLeft(std::clamp(GameplayRow.w * 0.38f, 140.0f * UiScale, 240.0f * UiScale), &TextEntitiesLabel, &PreLayoutButton);
			PreLayoutButton.VSplitLeft(DDNetMetrics.m_LineSpacing, nullptr, &PreLayoutButton);
			if(Ui()->DoButtonLogic(&g_Config.m_ClTextEntities, 0, &TextEntitiesLabel, BUTTONFLAG_LEFT))
			{
				g_Config.m_ClTextEntities ^= 1;
				return true;
			}
			for(int Row = 0; Row < 6; ++Row)
				SplitDDNetRow(Gameplay, &PreLayoutButton);
			SplitDDNetRow(Gameplay, &PreLayoutButton);
			if(!Ui()->DoButtonLogic(&g_Config.m_ClAntiPing, 0, &PreLayoutButton, BUTTONFLAG_LEFT))
				return false;
			g_Config.m_ClAntiPing ^= 1;
			return true;
		};

		// demo
		const bool ReplaysLayout = g_Config.m_ClReplays != 0;
		const bool RaceGhostLayout = g_Config.m_ClRaceGhost != 0;
		const bool RaceSaveGhostLayout = g_Config.m_ClRaceSaveGhost != 0;
		const float DemoRows = ResolveDDNetDemoRows(ReplaysLayout, RaceGhostLayout, RaceSaveGhostLayout);
		const float DemoMinCardHeight = CardChromeHeight + DDNetRowPitch * DemoRows;
		AddCard(DemoSpec, DemoMinCardHeight, CardChromeHeight, [=, this](CUIRect ContentRect) mutable {
			CPerfTimer DemoSectionTimer;
			CUIRect Demo = ContentRect;
			Left = Demo;

			SplitDDNetRow(Left, &Button);
			if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClAutoRaceRecord, "Save the best demo of each race", Localize("Save the best demo of each race"), g_Config.m_ClAutoRaceRecord, &Button))
			{
				g_Config.m_ClAutoRaceRecord ^= 1;
			}

			SplitDDNetRow(Left, &Button);
			if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClReplays, "Enable replays", Localize("Enable replays"), g_Config.m_ClReplays, &Button))
			{
				g_Config.m_ClReplays ^= 1;
				if(Client()->State() == IClient::STATE_ONLINE)
				{
					Client()->DemoRecorder_UpdateReplayRecorder();
				}
			}

			if(g_Config.m_ClReplays)
			{
				SplitDDNetRow(Left, &Button);
				DoDDNetNumericField("ddnet-replay-default-length", &g_Config.m_ClReplayLength, &g_Config.m_ClReplayLength, Button, Localize("Default length"), 10, 600, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);

				SplitDDNetRow(Left, &Button);
				DoDDNetNumericField("ddnet-esc-replay-minutes", &g_Config.m_ClEscReplayLengthMinutes, &g_Config.m_ClEscReplayLengthMinutes, Button, Localize("ESC replay minutes"), 1, 60, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);
			}

			// 回放与幽灵选项属于同一组，沿同一列排列，避免窄列截断中文标签。
			Right = Left;
			SplitDDNetRow(Right, &Button);
			if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClRaceGhost, "Enable ghost", Localize("Enable ghost"), g_Config.m_ClRaceGhost, &Button))
			{
				g_Config.m_ClRaceGhost ^= 1;
			}
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClRaceGhost, &Button, Localize("When you cross the start line, show a ghost tee replicating the movements of your best time"));

			if(g_Config.m_ClRaceGhost)
			{
				SplitDDNetRow(Right, &Button);
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClRaceShowGhost, "Show ghost", Localize("Show ghost"), g_Config.m_ClRaceShowGhost, &Button))
				{
					g_Config.m_ClRaceShowGhost ^= 1;
				}

				SplitDDNetRow(Right, &Button);
				DoDDNetNumericField("ddnet-race-ghost-opacity", &g_Config.m_ClRaceGhostAlpha, &g_Config.m_ClRaceGhostAlpha, Button, Localize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");

				SplitDDNetRow(Right, &Button);
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClRaceSaveGhost, "Save ghost", Localize("Save ghost"), g_Config.m_ClRaceSaveGhost, &Button))
				{
					g_Config.m_ClRaceSaveGhost ^= 1;
				}

				if(g_Config.m_ClRaceSaveGhost)
				{
					SplitDDNetRow(Right, &Button);
					if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClRaceGhostSaveBest, "Only save improvements", Localize("Only save improvements"), g_Config.m_ClRaceGhostSaveBest, &Button))
					{
						g_Config.m_ClRaceGhostSaveBest ^= 1;
					}
				}
			}
			LogPerfStage(Client(), "ddnet_demo_section", DemoSectionTimer.ElapsedMs(), false, "page=ddnet section=demo");
		});
		vCards.back().m_Measure = [DDNetRowPitch](float) {
			return DDNetRowPitch * ResolveDDNetDemoRows(g_Config.m_ClReplays != 0, g_Config.m_ClRaceGhost != 0, g_Config.m_ClRaceSaveGhost != 0);
		};
		vCards.back().m_MeasureRevision =
			((uint64_t)(g_Config.m_ClReplays != 0) << 0) |
			((uint64_t)(g_Config.m_ClRaceGhost != 0) << 1) |
			((uint64_t)(g_Config.m_ClRaceSaveGhost != 0) << 2);
		vCards.back().m_PreLayoutInput = ProcessDemoPreLayoutInput;

		// gameplay
		const bool TextEntitiesLayout = g_Config.m_ClTextEntities != 0;
		const bool AntiPingLayout = g_Config.m_ClAntiPing != 0;
		const float GameplayMinCardHeight = CardChromeHeight + DDNetRowPitch * ResolveDDNetGameplayRows(TextEntitiesLayout, AntiPingLayout);
		AddCard(GameplaySpec, GameplayMinCardHeight, CardChromeHeight, [=, this](CUIRect ContentRect) mutable {
			CPerfTimer GameplaySectionTimer;
			CUIRect Gameplay = ContentRect;
			CUIRect GameplayRow;

			SplitDDNetRow(Gameplay, &Button);
			DoDDNetNumericField("ddnet-overlay-entities", &g_Config.m_ClOverlayEntities, &g_Config.m_ClOverlayEntities, Button, Localize("Overlay entities"), 0, 100);

			SplitDDNetRow(Gameplay, &GameplayRow);
			GameplayRow.VSplitLeft(std::clamp(GameplayRow.w * 0.38f, 140.0f * UiScale, 240.0f * UiScale), &LeftLeft, &Button);
			Button.VSplitLeft(DDNetMetrics.m_LineSpacing, nullptr, &Button);

			if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClTextEntities, "Show text entities", Localize("Show text entities"), g_Config.m_ClTextEntities, &LeftLeft))
				g_Config.m_ClTextEntities ^= 1;

			if(g_Config.m_ClTextEntities)
			{
				if(DoDDNetNumericField("ddnet-text-entities-size", &g_Config.m_ClTextEntitiesSize, &g_Config.m_ClTextEntitiesSize, Button, Localize("Size"), 20, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE))
					GameClient()->m_MapImages.SetTextureScale(g_Config.m_ClTextEntitiesSize);
			}

			SplitDDNetRow(Gameplay, &GameplayRow);
			GameplayRow.VSplitLeft(std::clamp(GameplayRow.w * 0.38f, 140.0f * UiScale, 240.0f * UiScale), &LeftLeft, &Button);
			Button.VSplitLeft(DDNetMetrics.m_LineSpacing, nullptr, &Button);

			if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClShowOthers, "Show others", Localize("Show others"), g_Config.m_ClShowOthers == SHOW_OTHERS_ON, &LeftLeft))
				g_Config.m_ClShowOthers = g_Config.m_ClShowOthers != SHOW_OTHERS_ON ? SHOW_OTHERS_ON : SHOW_OTHERS_OFF;

			DoDDNetNumericField("ddnet-show-others-opacity", &g_Config.m_ClShowOthersAlpha, &g_Config.m_ClShowOthersAlpha, Button, Localize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");

			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClShowOthersAlpha, &Button, Localize("Adjust the opacity of entities belonging to other teams, such as tees and name plates"));

			SplitDDNetRow(Gameplay, &Button);
			static int s_ShowOwnTeamId = 0;
			if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &s_ShowOwnTeamId, "Show others (own team only)", Localize("Show others (own team only)"), g_Config.m_ClShowOthers == SHOW_OTHERS_ONLY_TEAM, &Button))
			{
				g_Config.m_ClShowOthers = g_Config.m_ClShowOthers != SHOW_OTHERS_ONLY_TEAM ? SHOW_OTHERS_ONLY_TEAM : SHOW_OTHERS_OFF;
			}

			SplitDDNetRow(Gameplay, &Button);
			if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClShowQuads, "Show background quads", Localize("Show background quads"), g_Config.m_ClShowQuads, &Button))
			{
				g_Config.m_ClShowQuads ^= 1;
			}
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClShowQuads, &Button, Localize("Quads are used for background decoration"));

			SplitDDNetRow(Gameplay, &Button);
			if(DoDDNetNumericField("ddnet-default-zoom", &g_Config.m_ClDefaultZoom, &g_Config.m_ClDefaultZoom, Button, Localize("Default zoom"), 0, 20))
				GameClient()->m_Camera.SetZoom(CCamera::ZoomStepsToValue(g_Config.m_ClDefaultZoom - 10), g_Config.m_ClSmoothZoomTime, true);

			SplitDDNetRow(Gameplay, &Button);
			DoDDNetNumericField("ddnet-prediction-margin", &g_Config.m_ClPredictionMargin, &g_Config.m_ClPredictionMargin, Button, Localize("Prediction margin"), 1, 300, &CUi::ms_LinearScrollbarScale, 0u, "");

			SplitDDNetRow(Gameplay, &Button);
			if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClPredictEvents, "Predict events (experimental)", Localize("Predict events (experimental)"), g_Config.m_ClPredictEvents, &Button))
			{
				g_Config.m_ClPredictEvents ^= 1;
			}

			SplitDDNetRow(Gameplay, &Button);
			if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClAntiPing, "AntiPing (latency compensation)", Localize("AntiPing (latency compensation)"), g_Config.m_ClAntiPing, &Button))
			{
				g_Config.m_ClAntiPing ^= 1;
			}
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClAntiPing, &Button, Localize("Try to predict other entities to reduce lag feeling at high latency"));

			if(g_Config.m_ClAntiPing)
			{
				SplitDDNetRow(Gameplay, &Button);
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClAntiPingPlayers, "AntiPing: predict other players", Localize("AntiPing: predict other players"), g_Config.m_ClAntiPingPlayers, &Button))
				{
					g_Config.m_ClAntiPingPlayers ^= 1;
				}

				SplitDDNetRow(Gameplay, &Button);
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClAntiPingWeapons, "AntiPing: predict weapons", Localize("AntiPing: predict weapons"), g_Config.m_ClAntiPingWeapons, &Button))
				{
					g_Config.m_ClAntiPingWeapons ^= 1;
				}

				SplitDDNetRow(Gameplay, &Button);
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClAntiPingGrenade, "AntiPing: predict grenade path", Localize("AntiPing: predict grenade path"), g_Config.m_ClAntiPingGrenade, &Button))
				{
					g_Config.m_ClAntiPingGrenade ^= 1;
				}
			}
			LogPerfStage(Client(), "ddnet_gameplay_section", GameplaySectionTimer.ElapsedMs(), false, "page=ddnet section=gameplay");
		});
		vCards.back().m_Measure = [DDNetRowPitch](float) {
			return DDNetRowPitch * ResolveDDNetGameplayRows(g_Config.m_ClTextEntities != 0, g_Config.m_ClAntiPing != 0);
		};
		vCards.back().m_MeasureRevision =
			((uint64_t)(g_Config.m_ClTextEntities != 0) << 0) |
			((uint64_t)(g_Config.m_ClAntiPing != 0) << 1);
		vCards.back().m_PreLayoutInput = ProcessGameplayPreLayoutInput;

		const float DDNetColorPickerRowHeight = DDNetMetrics.m_ButtonHeight + DDNetMetrics.m_LineSpacing;
		const float BackgroundMinCardHeight = CardChromeHeight + DDNetColorPickerRowHeight * 2.0f + DDNetRowPitch * 3.0f + 2.0f;
		CPerfTimer ControlsSectionTimer;
		AddCard(BackgroundSpec, BackgroundMinCardHeight, CardChromeHeight, [=, this](CUIRect ContentRect) mutable {
			CUIRect Background = ContentRect;

			// background
			ColorRGBA GreyDefault(0.5f, 0.5f, 0.5f, 1);

			static CButtonContainer s_ResetId1;
			DoLine_ColorPicker(&s_ResetId1, DDNetMetrics, &Background, Localize("Regular background color"), &g_Config.m_ClBackgroundColor, GreyDefault, false);

			static CButtonContainer s_ResetId2;
			DoLine_ColorPicker(&s_ResetId2, DDNetMetrics, &Background, Localize("Entities background color"), &g_Config.m_ClBackgroundEntitiesColor, GreyDefault, false);

			CUIRect EditBox, ReloadButton;
			SplitDDNetRow(Background, &Label);
			Background.HSplitTop(2.0f, nullptr, &Background);
			Label.VSplitLeft(100.0f, &Label, &EditBox);
			EditBox.VSplitRight(60.0f, &EditBox, &Button);
			Button.VSplitMid(&ReloadButton, &Button, 5.0f);
			EditBox.VSplitRight(5.0f, &EditBox, nullptr);

			DoSettingsMenuLabel(SETTINGS_DDNET, -1, -1, "ddnet-background-map-label", &Label, Localize("Map"), BodySize, TEXTALIGN_ML);

			static CLineInput s_BackgroundEntitiesInput(g_Config.m_ClBackgroundEntities, sizeof(g_Config.m_ClBackgroundEntities));
			static char s_aBackgroundEntitiesSync[sizeof(g_Config.m_ClBackgroundEntities)] = "";
			const bool WasInputActive = s_BackgroundEntitiesInput.IsActive();
			IUiContext DDNetBackgroundEntitiesTextInputCtx;
			DDNetBackgroundEntitiesTextInputCtx.m_pUi = Ui();
			DDNetBackgroundEntitiesTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_ddnet_background_entities_text_input");
			const bool InputCommitted = ui_widget::InputField(DDNetBackgroundEntitiesTextInputCtx, &s_BackgroundEntitiesInput, EditBox, nullptr, BodySize);
			bool BackgroundChanged = false;
			if(InputCommitted)
				BackgroundChanged = ApplyBackgroundEntitiesInputValue(s_BackgroundEntitiesInput);
			else if(ShouldCommitBackgroundEntitiesInputOnBlur(WasInputActive, s_BackgroundEntitiesInput.IsActive(), s_BackgroundEntitiesInput.GetString(), s_aBackgroundEntitiesSync))
				BackgroundChanged = ApplyBackgroundEntitiesInputValue(s_BackgroundEntitiesInput);
			SyncBackgroundEntitiesInput(s_BackgroundEntitiesInput, s_aBackgroundEntitiesSync, sizeof(s_aBackgroundEntitiesSync));

			static CButtonContainer s_BackgroundEntitiesMapPicker, s_BackgroundEntitiesReload;

			if(Ui()->DoButton_QmIcon(&s_BackgroundEntitiesReload, EQmIcon::ARROW_ROTATE_RIGHT, FONT_ICON_ARROW_ROTATE_RIGHT, 0, &ReloadButton, BUTTONFLAG_LEFT))
			{
				CommitBackgroundEntitiesInputIfActive(s_BackgroundEntitiesInput, s_aBackgroundEntitiesSync, sizeof(s_aBackgroundEntitiesSync));
				g_Config.m_ClBackgroundEntities[0] = '\0';
				s_BackgroundEntitiesInput.Set("");
				s_aBackgroundEntitiesSync[0] = '\0';
				BackgroundChanged = true;
			}

			if(Ui()->DoButton_QmIcon(&s_BackgroundEntitiesMapPicker, EQmIcon::FOLDER, FONT_ICON_FOLDER, 0, &Button, BUTTONFLAG_LEFT))
			{
				BackgroundChanged |= CommitBackgroundEntitiesInputIfActive(s_BackgroundEntitiesInput, s_aBackgroundEntitiesSync, sizeof(s_aBackgroundEntitiesSync));
				static SPopupMenuId s_PopupMapPickerId;
				static CPopupMapPickerContext s_PopupMapPickerContext;
				s_PopupMapPickerContext.m_pMenus = this;
				s_PopupMapPickerContext.m_aCurrentMapFolder[0] = '\0';
				str_copy(s_PopupMapPickerContext.m_aRootPath, "maps", sizeof(s_PopupMapPickerContext.m_aRootPath));
				str_copy(s_PopupMapPickerContext.m_aFallbackRootPath, "mapres", sizeof(s_PopupMapPickerContext.m_aFallbackRootPath));
				s_PopupMapPickerContext.m_aValuePrefix[0] = '\0';
				str_copy(s_PopupMapPickerContext.m_aFallbackValuePrefix, "mapres", sizeof(s_PopupMapPickerContext.m_aFallbackValuePrefix));
				s_PopupMapPickerContext.m_pTargetConfig = g_Config.m_ClBackgroundEntities;
				s_PopupMapPickerContext.m_TargetConfigSize = sizeof(g_Config.m_ClBackgroundEntities);
				s_PopupMapPickerContext.MapListPopulate();
				const SQmDropdownPopupPolicy PopupPolicy = QmResolveDropdownPopupPolicy((int)s_PopupMapPickerContext.m_vMaps.size(), 20.0f, 0.0f, false, 0.0f, CUi::PopupMenuContentInset(), 1);
				SPopupMenuProperties PopupProps;
				PopupProps.m_BlockUnderlyingScroll = true;
				PopupProps.m_CenterInViewport = true;
				PopupProps.m_BlockUnderlyingPointerInput = true;
				PopupProps.m_Animate = true;
				Ui()->DoPopupMenu(&s_PopupMapPickerId, Ui()->MouseX(), Ui()->MouseY(), 300.0f, PopupPolicy.m_PreferredHeight, &s_PopupMapPickerContext, PopupMapPicker, PopupProps);
			}

			SplitDDNetRow(Background, &Button);
			const bool UseCurrentMap = IsCurrentMapBackgroundEntitiesValue(g_Config.m_ClBackgroundEntities);
			static int s_UseCurrentMapId = 0;
			if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &s_UseCurrentMapId, "Use current map as background", Localize("Use current map as background"), UseCurrentMap, &Button))
			{
				BackgroundChanged |= CommitBackgroundEntitiesInputIfActive(s_BackgroundEntitiesInput, s_aBackgroundEntitiesSync, sizeof(s_aBackgroundEntitiesSync));
				BackgroundChanged |= ToggleCurrentMapBackground(s_BackgroundEntitiesInput);
				SyncBackgroundEntitiesInput(s_BackgroundEntitiesInput, s_aBackgroundEntitiesSync, sizeof(s_aBackgroundEntitiesSync));
			}

			if(BackgroundChanged)
				GameClient()->m_Background.LoadBackground();

			SplitDDNetRow(Background, &Button);
			if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClBackgroundShowTilesLayers, "Show tiles layers from BG map", Localize("Show tiles layers from BG map"), g_Config.m_ClBackgroundShowTilesLayers, &Button))
				g_Config.m_ClBackgroundShowTilesLayers ^= 1;
		});

		float MiscellaneousMinContentHeight = DDNetRowPitch * 3.0f + 5.0f + 2.0f;
#if defined(CONF_FAMILY_WINDOWS)
		MiscellaneousMinContentHeight += DDNetRowPitch + 10.0f;
#endif
		const float MiscellaneousMinCardHeight = CardChromeHeight + MiscellaneousMinContentHeight;
		AddCard(MiscellaneousSpec, MiscellaneousMinCardHeight, CardChromeHeight, [=, this](CUIRect ContentRect) mutable {
			CUIRect Miscellaneous = ContentRect;
			// miscellaneous
			static CButtonContainer s_ButtonTimeout;
			SplitDDNetRow(Miscellaneous, &Button);
			if(DoSettingsButton_Menu(SETTINGS_DDNET, -1, -1, &s_ButtonTimeout, "ddnet-new-random-timeout-code", Localize("New random timeout code"), 0, &Button))
			{
				Client()->GenerateTimeoutSeed();
			}

			Miscellaneous.HSplitTop(5.0f, nullptr, &Miscellaneous);
			SplitDDNetRow(Miscellaneous, &Label);
			Miscellaneous.HSplitTop(2.0f, nullptr, &Miscellaneous);
			CUIElement &RunOnJoinLabelElement = SettingsTextElement(SETTINGS_DDNET, -1, "ddnet-run-on-join-label");
			DoSettingsLabelStreamed(RunOnJoinLabelElement, &Label, Localize("Run on join"), BodySize, TEXTALIGN_ML);
			SplitDDNetRow(Miscellaneous, &Button);
			static CLineInput s_RunOnJoinInput(g_Config.m_ClRunOnJoin, sizeof(g_Config.m_ClRunOnJoin));
			s_RunOnJoinInput.SetEmptyText(Localize("Chat command (e.g. showall 1)"));
			IUiContext DDNetRunOnJoinTextInputCtx;
			DDNetRunOnJoinTextInputCtx.m_pUi = Ui();
			DDNetRunOnJoinTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_ddnet_run_on_join_text_input");
			ui_widget::InputField(DDNetRunOnJoinTextInputCtx, &s_RunOnJoinInput, Button, nullptr, BodySize);

#if defined(CONF_FAMILY_WINDOWS)
			static CButtonContainer s_ButtonUnregisterShell;
			Miscellaneous.HSplitTop(10.0f, nullptr, &Miscellaneous);
			SplitDDNetRow(Miscellaneous, &Button);
			if(DoSettingsButton_Menu(SETTINGS_DDNET, -1, -1, &s_ButtonUnregisterShell, "ddnet-unregister-protocol-file-extensions", Localize("Unregister protocol and file extensions"), 0, &Button))
			{
				Client()->ShellUnregister();
			}
#endif
		});
		LogPerfStage(Client(), "ddnet_controls_section", ControlsSectionTimer.ElapsedMs(), false, "page=ddnet section=background_misc");
	};
	uint64_t DDNetLayoutRevision = static_cast<uint64_t>(RenderOnly ? 1 : 0);
	DDNetLayoutRevision = DDNetLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClReplays != 0);
	DDNetLayoutRevision = DDNetLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClRaceGhost != 0);
	DDNetLayoutRevision = DDNetLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClRaceSaveGhost != 0);
	DDNetLayoutRevision = DDNetLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClTextEntities != 0);
	DDNetLayoutRevision = DDNetLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClAntiPing != 0);
	if(pCards != nullptr)
	{
		BuildDefinitions(*pCards);
	}
	return DDNetLayoutRevision;
}
