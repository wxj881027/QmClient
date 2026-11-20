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

#include <game/client/QmUi/AntiPingPlayersSettings.h>
#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmUiPerf.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsCardContentBridge.h>
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
	bool CommitBackgroundEntitiesInput(CUi &Ui, CLineInput &Input, const char *pLastBackgroundPath)
	{
		if(Input.IsActive())
			Ui.ReleaseActiveTextInput(&Input);

		// 输入框直接写入配置，用背景最近尝试加载的路径判断是否需要重载。
		char aNormalized[IO_MAX_PATH_LENGTH];
		const bool Changed = BuildBackgroundEntitiesCommitValueFromInput(Input.GetString(), pLastBackgroundPath, aNormalized, sizeof(aNormalized));
		if(str_comp(Input.GetString(), aNormalized) != 0)
			Input.Set(aNormalized);
		return Changed;
	}

	void LogPerfStage(IClient *pClient, const char *pStage, double DurationMs, bool Force = false, const char *pExtra = nullptr)
	{
		QmPerfLogStage("perf/menu", pStage, DurationMs, Force, pClient, nullptr, nullptr, pExtra);
	}
}

uint64_t CMenus::BuildDDNetSettingsCards(const qm_card_catalog::SQmCardBuildContext &Ctx, std::vector<SSettingsCardDefinition> *pCards)
{
	CPerfTimer ShellTimer;
	LogPerfStage(Client(), "ddnet_tab_shell", ShellTimer.ElapsedMs(), false, "page=ddnet");

	const SSettingsContentMetrics DDNetMetrics = Ctx.m_Metrics;
	const float BodySize = DDNetMetrics.m_BodySize;
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
			return;
		ui_widget::NumericField(DDNetCardCtx, GetSettingsNumericFieldState(pId), pId, pOption, Min, Max, Rect, Options);
	};
	const qm_card_registry::SCardDefault *pDemoDefault = qm_card_registry::FindByStableId("deck:ddnet-demo");
	const qm_card_registry::SCardDefault *pGameplayDefault = qm_card_registry::FindByStableId("deck:ddnet-gameplay");
	const qm_card_registry::SCardDefault *pBackgroundDefault = qm_card_registry::FindByStableId("deck:ddnet-background");
	const qm_card_registry::SCardDefault *pMiscellaneousDefault = qm_card_registry::FindByStableId("deck:ddnet-miscellaneous");
	// DDNet 页「网络补偿」子页签的预测主卡（与常规子页签同属 DDNet 页）。
	const qm_card_registry::SCardDefault *pPredictionDefault = qm_card_registry::FindByStableId("deck:ddnet-prediction");
	dbg_assert(pDemoDefault != nullptr && pGameplayDefault != nullptr && pBackgroundDefault != nullptr && pMiscellaneousDefault != nullptr && pPredictionDefault != nullptr, "DDNet settings cards must be registered");
	if(pDemoDefault == nullptr || pGameplayDefault == nullptr || pBackgroundDefault == nullptr || pMiscellaneousDefault == nullptr || pPredictionDefault == nullptr)
		return 0;
	const bool RenderOnly = Ctx.m_ReadOnly;
	const auto BuildDefinitions = [=, this](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(5);
		const SSettingsCardSpec DemoSpec{pDemoDefault->m_pStableId, Localize(pDemoDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pDemoDefault)};
		const SSettingsCardSpec GameplaySpec{pGameplayDefault->m_pStableId, Localize(pGameplayDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pGameplayDefault)};
		const SSettingsCardSpec BackgroundSpec{pBackgroundDefault->m_pStableId, Localize(pBackgroundDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pBackgroundDefault)};
		const SSettingsCardSpec MiscellaneousSpec{pMiscellaneousDefault->m_pStableId, Localize(pMiscellaneousDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pMiscellaneousDefault)};
		const SSettingsCardSpec PredictionSpec{pPredictionDefault->m_pStableId, Localize(pPredictionDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pPredictionDefault)};
		const auto AddCard = [this, &vCards, RenderOnly](const SSettingsCardSpec &Spec, FSettingsCardRenderMeasured Render) {
			SSettingsCardDefinition Definition;
			Definition.m_Spec = Spec;
			Definition.m_RenderMeasured = std::move(Render);
			qm_settings_content::UseDeclaredContent(Definition, Ui(), TextRender());
			if(RenderOnly)
				Definition.m_ContentLayoutInput = {};
			vCards.push_back(std::move(Definition));
		};
		AddCard(DemoSpec, [=, this](CUIRect &ContentRect) {
			CSettingsContentLayoutFlow Flow(ContentRect);
			const bool CanCommit = Flow.Input() && !RenderOnly && !Ui()->RenderOnly();
			CUIRect Button;
			CPerfTimer DemoSectionTimer;

			if(Flow.Control("auto-race-record", DDNetMetrics.m_LineHeight, Button))
			{
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClAutoRaceRecord, "Save the best demo of each race", Localize("Save the best demo of each race"), g_Config.m_ClAutoRaceRecord, &Button) && CanCommit)
				{
					g_Config.m_ClAutoRaceRecord ^= 1;
				}
			}
			if(Flow.Control("replays", DDNetMetrics.m_LineHeight, Button, ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing)))
			{
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClReplays, "Enable replays", Localize("Enable replays"), g_Config.m_ClReplays, &Button) && CanCommit)
				{
					g_Config.m_ClReplays ^= 1;
					if(Client()->State() == IClient::STATE_ONLINE)
					{
						Client()->DemoRecorder_UpdateReplayRecorder();
					}
				}
			}
			Flow.Space(ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing));
			static CSettingsConditionalRows s_ReplayOptionsRows;
			if(s_ReplayOptionsRows.Update(g_Config.m_ClReplays, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
			{
				const CSettingsContentRevealScope ReplayOptionsRevealScope(Ui(), Flow, s_ReplayOptionsRows.Fade(g_Config.m_QmUiMotionLevel));
				if(Flow.Control("replay-length", DDNetMetrics.m_LineHeight, Button))
				{
					DoDDNetNumericField("ddnet-replay-default-length", &g_Config.m_ClReplayLength, &g_Config.m_ClReplayLength, Button, Localize("Default length"), 10, 600, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);
				}
				if(Flow.Control("esc-replay-length", DDNetMetrics.m_LineHeight, Button, ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing)))
				{
					DoDDNetNumericField("ddnet-esc-replay-minutes", &g_Config.m_ClEscReplayLengthMinutes, &g_Config.m_ClEscReplayLengthMinutes, Button, Localize("ESC replay minutes"), 1, 60, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);
				}
				Flow.Space(ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing));
			}

			// 回放与幽灵选项属于同一组，沿同一列排列，避免窄列截断中文标签。

			if(Flow.Control("race-ghost", DDNetMetrics.m_LineHeight, Button))
			{
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClRaceGhost, "Enable ghost", Localize("Enable ghost"), g_Config.m_ClRaceGhost, &Button) && CanCommit)
				{
					g_Config.m_ClRaceGhost ^= 1;
				}
				GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClRaceGhost, &Button, Localize("Show a ghost of your best time after crossing the start line"));
			}
			Flow.Space(ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing));
			static CSettingsConditionalRows s_RaceGhostRows;
			if(s_RaceGhostRows.Update(g_Config.m_ClRaceGhost, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
			{
				const CSettingsContentRevealScope RaceGhostRevealScope(Ui(), Flow, s_RaceGhostRows.Fade(g_Config.m_QmUiMotionLevel));
				if(Flow.Control("show-ghost", DDNetMetrics.m_LineHeight, Button))
				{
					if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClRaceShowGhost, "Show ghost", Localize("Show ghost"), g_Config.m_ClRaceShowGhost, &Button) && CanCommit)
					{
						g_Config.m_ClRaceShowGhost ^= 1;
					}
				}
				if(Flow.Control("ghost-opacity", DDNetMetrics.m_LineHeight, Button, ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing)))
				{
					DoDDNetNumericField("ddnet-race-ghost-opacity", &g_Config.m_ClRaceGhostAlpha, &g_Config.m_ClRaceGhostAlpha, Button, Localize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
				}
				if(Flow.Control("ghost-show-direction", DDNetMetrics.m_LineHeight, Button, ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing)))
				{
					if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_QmRankGhostShowDirection, "Rank ghost direction", Localize("Show ghost direction"), g_Config.m_QmRankGhostShowDirection, &Button) && CanCommit)
						g_Config.m_QmRankGhostShowDirection ^= 1;
				}
				if(Flow.Control("save-ghost", DDNetMetrics.m_LineHeight, Button, ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing)))
				{
					if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClRaceSaveGhost, "Save ghost", Localize("Save ghost"), g_Config.m_ClRaceSaveGhost, &Button) && CanCommit)
					{
						g_Config.m_ClRaceSaveGhost ^= 1;
					}
				}
				// 组边界这一档保持无条件声明（同文件其它开关块同写法）：收起时它照样占位，
				// 展开那一帧不会先少一档再补回来；块内子选项关掉就整块不声明，打开时走行展开动画。
				static CSettingsConditionalRows s_RaceSaveGhostRows;
				if(s_RaceSaveGhostRows.Update(g_Config.m_ClRaceSaveGhost, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
				{
					const CSettingsContentRevealScope RaceSaveGhostRevealScope(Ui(), Flow, s_RaceSaveGhostRows.Fade(g_Config.m_QmUiMotionLevel));
					if(Flow.Control("ghost-save-best", DDNetMetrics.m_LineHeight, Button))
					{
						if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClRaceGhostSaveBest, "Only save improvements", Localize("Only save improvements"), g_Config.m_ClRaceGhostSaveBest, &Button) && CanCommit)
						{
							g_Config.m_ClRaceGhostSaveBest ^= 1;
						}
					}
					Flow.Space(ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing));
				}
			}
			if(CanCommit)
				LogPerfStage(Client(), "ddnet_demo_section", DemoSectionTimer.ElapsedMs(), false, "page=ddnet section=demo");
		});

		AddCard(GameplaySpec, [=, this](CUIRect &ContentRect) {
			CSettingsContentLayoutFlow Flow(ContentRect);
			const bool CanCommit = Flow.Input() && !RenderOnly && !Ui()->RenderOnly();
			CUIRect Button;
			CPerfTimer GameplaySectionTimer;

			CUIRect GameplayRow;

			// 显示其他玩家独占一行：它是主控行，影子不透明度是它的从属行。
			if(Flow.Control("show-others", DDNetMetrics.m_LineHeight, GameplayRow))
			{
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClShowOthers, "Show others", Localize("Show others"), g_Config.m_ClShowOthers == SHOW_OTHERS_ON, &GameplayRow) && CanCommit)
					g_Config.m_ClShowOthers = g_Config.m_ClShowOthers != SHOW_OTHERS_ON ? SHOW_OTHERS_ON : SHOW_OTHERS_OFF;
			}
			Flow.Space(ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing));
			// 约定 A：影子不透明度是「显示其他玩家」的内部配置项（从属行）。
			// 主控关闭时整行不参与布局、不占高、不可交互；用 != OFF 覆盖「仅己方队伍」，
			// 那条路径同样会画出半透明的其他玩家影子。
			static SettingsDependentRows s_ShowOthersOpacityRows;
			if(const auto ShowOthersOpacityRows = s_ShowOthersOpacityRows.Reveal(Ui(), Flow, g_Config.m_ClShowOthers != SHOW_OTHERS_OFF, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
			{
				if(Flow.Control("show-others-opacity", DDNetMetrics.m_LineHeight, Button))
				{
					DoDDNetNumericField("ddnet-show-others-opacity", &g_Config.m_ClShowOthersAlpha, &g_Config.m_ClShowOthersAlpha, Button, Localize("Other players' shadow opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
					GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClShowOthersAlpha, &Button, Localize("Opacity of other teams' tees and name plates"));
				}
				Flow.Space(ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing));
			}
			// 行距由上面那一档 Space 与从属行的收尾 Space 二选一给出（同回放卡写法），
			// 这里不再声明行前间距，否则从属行隐藏/显示两个分支会各多出一档。
			if(Flow.Control("own-team", DDNetMetrics.m_LineHeight, Button))
			{
				static int s_ShowOwnTeamId = 0;
				// 本行显式退出「标签缩略」：既不进 SettingsLabelMetadata 的缩略表，也不做括号拆分。
				// 标签源串与上一行同源，缩略后的主标签就是同一个「Show others」→「显示其他玩家」，
				// 同一张卡里两行同名，玩家分不清第三个开关到底管什么；整句「仅显示同队伍」才是正题。
				// 搜索/高亮仍按 pTextId 定位，不受显示文案影响。
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, -1, &s_ShowOwnTeamId, "Show others (own team only)", Localize("Show others (own team only)"), g_Config.m_ClShowOthers == SHOW_OTHERS_ONLY_TEAM, &Button, SLabelProperties{}, true, -1.0f, false) && CanCommit)
				{
					g_Config.m_ClShowOthers = g_Config.m_ClShowOthers != SHOW_OTHERS_ONLY_TEAM ? SHOW_OTHERS_ONLY_TEAM : SHOW_OTHERS_OFF;
				}
			}

			if(Flow.Control("background-quads", DDNetMetrics.m_LineHeight, Button, ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing)))
			{
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClShowQuads, "Show background quads", Localize("Show background quads"), g_Config.m_ClShowQuads, &Button) && CanCommit)
				{
					g_Config.m_ClShowQuads ^= 1;
				}
				GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClShowQuads, &Button, Localize("Quads are used for background decoration"));
			}
			if(Flow.Control("default-zoom", DDNetMetrics.m_LineHeight, Button, ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing)))
			{
				const int PreviousZoom = g_Config.m_ClDefaultZoom;
				DoDDNetNumericField("ddnet-default-zoom", &g_Config.m_ClDefaultZoom, &g_Config.m_ClDefaultZoom, Button, Localize("Default zoom"), 0, 20);
				if(CanCommit && g_Config.m_ClDefaultZoom != PreviousZoom)
					GameClient()->m_Camera.SetZoom(CCamera::ZoomStepsToValue(g_Config.m_ClDefaultZoom - 10), g_Config.m_ClSmoothZoomTime, true);
			}
			if(CanCommit)
				LogPerfStage(Client(), "ddnet_gameplay_section", GameplaySectionTimer.ElapsedMs(), false, "page=ddnet section=gameplay");
		});

		// 「网络补偿」子页签的客户端预测卡：预测余量 + 延迟补偿（主控）+ 预测事件 + 上行冗余。
		// 这些行原来在「游戏玩法」卡里，按用户要求整组搬到独立卡片，语义与控件不变。
		AddCard(PredictionSpec, [=, this](CUIRect &ContentRect) {
			CSettingsContentLayoutFlow Flow(ContentRect);
			const bool CanCommit = Flow.Input() && !RenderOnly && !Ui()->RenderOnly();
			CUIRect Button;
			CPerfTimer PredictionSectionTimer;
			const float FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
			const int MotionLevel = g_Config.m_QmUiMotionLevel;
			const float AntiPingPlayersHeight = ResolveSettingsRadioRowLayout(ContentRect, 3, DDNetMetrics).m_Height;

			// 预测余量：标签用新名，旧名「预测边距 / prediction margin」与配置名进 tooltip，
			// 玩家按旧文档搜索时仍能对上号。
			if(Flow.Control("prediction-margin", DDNetMetrics.m_LineHeight, Button))
			{
				DoDDNetNumericField("ddnet-prediction-margin", &g_Config.m_ClPredictionMargin, &g_Config.m_ClPredictionMargin, Button, Localize("Prediction margin"), 1, 300, &CUi::ms_LinearScrollbarScale, 0u, "");
				GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClPredictionMargin, &Button, Localize("Formerly \"prediction margin\" (cl_prediction_margin)"));
			}

			// 平滑预测余量：削弱 ping 抖动带来的修正跳变，默认关（与旧版本行为一致）。
			if(Flow.Control("prediction-margin-smooth", DDNetMetrics.m_LineHeight, Button, ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing)))
			{
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_QmSmoothPredictionMargin, "Prediction margin smoothing", Localize("Smooth prediction margin"), g_Config.m_QmSmoothPredictionMargin, &Button) && CanCommit)
					g_Config.m_QmSmoothPredictionMargin ^= 1;
				GameClient()->m_Tooltips.DoToolTip(&g_Config.m_QmSmoothPredictionMargin, &Button, Localize("Smooth sudden prediction margin corrections caused by ping spikes"));
			}

			// 主控行：延迟补偿。关掉时三个「延迟补偿：预测…」开关整组不存在（约定 A）。
			if(Flow.Control("anti-ping", DDNetMetrics.m_LineHeight, Button, ui_token::settings::SectionGap(DDNetMetrics.m_LineSpacing)))
			{
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClAntiPing, "AntiPing (latency compensation)", Localize("AntiPing (latency compensation)"), g_Config.m_ClAntiPing, &Button) && CanCommit)
				{
					g_Config.m_ClAntiPing ^= 1;
				}
				GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClAntiPing, &Button, Localize("Try to predict other entities to reduce lag feeling at high latency"));
			}
			Flow.Space(ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing));
			static SettingsDependentRows s_AntiPingRows;
			if(const auto AntiPingRows = s_AntiPingRows.Reveal(Ui(), Flow, g_Config.m_ClAntiPing != 0, FrameDt, MotionLevel))
			{
				if(Flow.Control("anti-ping-players", AntiPingPlayersHeight, Button))
				{
					int Choice = QmAntiPingPlayersSettings::BaseChoice(g_Config.m_ClAntiPingPlayers);
					if(DoSettingsLine_RadioMenu(SETTINGS_DDNET, -1, -1, Button, "anti-ping-players-label", Localize("AntiPing: predict other players"),
						   m_vButtonContainersAntiPingPlayers,
						   {"anti-ping-players-off", "anti-ping-players-all", "anti-ping-players-interact"},
						   {Localize("Off", "AntiPing: predict other players"), Localize("All", "AntiPing: predict other players"), Localize("Interact", "AntiPing: predict other players")},
						   {QmAntiPingPlayersSettings::OFF, QmAntiPingPlayersSettings::ALL, QmAntiPingPlayersSettings::INTERACT},
						   Choice, DDNetMetrics, nullptr, &g_Config.m_ClAntiPingPlayers) &&
						CanCommit)
					{
						g_Config.m_ClAntiPingPlayers = QmAntiPingPlayersSettings::ApplyBaseChoice(g_Config.m_ClAntiPingPlayers, Choice);
					}
				}
				// 冻结玩家是 Interact 档的从属行：选 Off / All 时不参与布局、不占高。
				static SettingsDependentRows s_AntiPingFrozenRows;
				if(const auto AntiPingFrozenRows = s_AntiPingFrozenRows.Reveal(Ui(), Flow, QmAntiPingPlayersSettings::ShowsFrozenOption(g_Config.m_ClAntiPingPlayers), FrameDt, MotionLevel))
				{
					if(Flow.Control("anti-ping-players-frozen", DDNetMetrics.m_LineHeight, Button, ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing)))
					{
						static CButtonContainer s_AntiPingFrozenId;
						const bool FrozenOption = QmAntiPingPlayersSettings::FrozenOptionEnabled(g_Config.m_ClAntiPingPlayers);
						if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &s_AntiPingFrozenId, "anti-ping-players-frozen", Localize("Always predict frozen players", "AntiPing: predict other players"), FrozenOption, &Button) && CanCommit)
						{
							g_Config.m_ClAntiPingPlayers = QmAntiPingPlayersSettings::ApplyFrozenOption(g_Config.m_ClAntiPingPlayers, !FrozenOption);
						}
					}
				}
				if(Flow.Control("anti-ping-weapons", DDNetMetrics.m_LineHeight, Button, ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing)))
				{
					if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClAntiPingWeapons, "AntiPing: predict weapons", Localize("AntiPing: predict weapons"), g_Config.m_ClAntiPingWeapons, &Button) && CanCommit)
					{
						g_Config.m_ClAntiPingWeapons ^= 1;
					}
				}
				if(Flow.Control("anti-ping-grenade", DDNetMetrics.m_LineHeight, Button, ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing)))
				{
					if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClAntiPingGrenade, "AntiPing: predict grenade path", Localize("AntiPing: predict grenade path"), g_Config.m_ClAntiPingGrenade, &Button) && CanCommit)
					{
						g_Config.m_ClAntiPingGrenade ^= 1;
					}
				}
				Flow.Space(ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing));
			}
			// 预测事件：常显行 + 行尾说明（约定 B）。行尾说明是常显短提示，
			// 完整解释仍走 tooltip/配置说明，不占独立行高。
			if(Flow.Control("predict-events", DDNetMetrics.m_LineHeight, Button, ui_token::settings::SectionGap(DDNetMetrics.m_LineSpacing)))
			{
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClPredictEvents, "Predict events (experimental)", Localize("Predict events (experimental)"), g_Config.m_ClPredictEvents, &Button) && CanCommit)
				{
					g_Config.m_ClPredictEvents ^= 1;
				}
				// 行尾说明贴标签列右端（开关左侧），不贴整行右缘——那正好是开关的位置。
				if(Flow.Drawing())
					DoSettingsInlineHint(Ui(), "predict-events", ui_widget::ResolveSwitchRowLabelColumn(Button, DDNetMetrics.m_BodySize), 0.0f, Localize("Sounds and particles"), DDNetMetrics.m_SmallSize);
			}
			if(Flow.Control("upstream-redundancy", DDNetMetrics.m_LineHeight, Button, ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing)))
			{
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_QmUpstreamRedundancy, "Upstream redundancy (experimental)", Localize("Upstream redundancy (experimental)"), g_Config.m_QmUpstreamRedundancy, &Button) && CanCommit)
					g_Config.m_QmUpstreamRedundancy ^= 1;
				GameClient()->m_Tooltips.DoToolTip(&g_Config.m_QmUpstreamRedundancy, &Button, Localize("Experimental: repeats outgoing UDP game packets when round-trip probes go unanswered. Upload traffic may triple; download loss and late duplicate inputs can still cause problems. Disabled with KCP."));
			}
			Flow.Space(ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing));
			// 约定 A：最大包份数是上行冗余发送的从属行。
			static SettingsDependentRows s_UpstreamRedundancyRows;
			if(const auto UpstreamRedundancyRows = s_UpstreamRedundancyRows.Reveal(Ui(), Flow, g_Config.m_QmUpstreamRedundancy != 0, FrameDt, MotionLevel))
			{
				if(Flow.Control("upstream-redundancy-copies", DDNetMetrics.m_LineHeight, Button))
					DoDDNetNumericField("ddnet-upstream-redundancy-copies", &g_Config.m_QmUpstreamRedundancyMaxCopies, &g_Config.m_QmUpstreamRedundancyMaxCopies, Button, Localize("Maximum packet copies"), 2, 3);
				Flow.Space(ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing));
			}
			// 优先游戏流量：属于传输层，不参与预测，所以放在本卡末尾。
			// 它只做 DSCP/802.1p 标记（Windows qWave），不预留带宽；路由器不认标记时没有效果。
			if(Flow.Control("net-qos", DDNetMetrics.m_LineHeight, Button, ui_token::settings::SectionGap(DDNetMetrics.m_LineSpacing)))
			{
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_QmNetQos, "net-qos", Localize("Prioritize game traffic (QoS, Windows only)"), g_Config.m_QmNetQos, &Button) && CanCommit)
					g_Config.m_QmNetQos ^= 1;
				GameClient()->m_Tooltips.DoToolTip(&g_Config.m_QmNetQos, &Button, Localize("Best-effort DSCP marking of the game connection; may be ignored by routers"));
			}
			Flow.Space(ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing));
			if(CanCommit)
				LogPerfStage(Client(), "ddnet_prediction_section", PredictionSectionTimer.ElapsedMs(), false, "page=ddnet section=prediction");
		});

		AddCard(BackgroundSpec, [=, this](CUIRect &ContentRect) {
			CSettingsContentLayoutFlow Flow(ContentRect);
			const bool CanCommit = Flow.Input() && !RenderOnly && !Ui()->RenderOnly();
			bool BackgroundChanged = false;
			CUIRect Button, Label;

			CUIRect ColorRow;
			const float ColorHeight = ResolveSettingsColorRowLayout(ContentRect, DDNetMetrics, false).m_ConsumedHeight;

			// background
			static CButtonContainer s_ResetId1;
			if(Flow.Control("background-color", ColorHeight, ColorRow))
				DoLine_ColorPicker(&s_ResetId1, DDNetMetrics, &ColorRow, Localize("Regular background color"), &g_Config.m_ClBackgroundColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClBackgroundColor)), false);

			static CButtonContainer s_ResetId2;
			if(Flow.Control("background-entities-color", ColorHeight, ColorRow))
				DoLine_ColorPicker(&s_ResetId2, DDNetMetrics, &ColorRow, Localize("Entities background color"), &g_Config.m_ClBackgroundEntitiesColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClBackgroundEntitiesColor)), false);

			CUIRect EditBox, ReloadButton, MapRowRect, CurrentMapRow, TileLayersRow;
			const bool HasMapRow = Flow.Control("background-map", DDNetMetrics.m_LineHeight, MapRowRect);
			const bool HasCurrentMapRow = Flow.Control("background-current-map", DDNetMetrics.m_LineHeight, CurrentMapRow, ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing));
			const bool HasTileLayersRow = Flow.Control("background-tile-layers", DDNetMetrics.m_LineHeight, TileLayersRow, ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing));
			Flow.Space(ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing));
			if(Flow.Declaring())
				return;
			static CLineInput s_BackgroundEntitiesInput(g_Config.m_ClBackgroundEntities, sizeof(g_Config.m_ClBackgroundEntities));
			const char *pLastBackgroundPath = GameClient()->m_Background.MapName();

			if(HasMapRow)
			{
				Label = MapRowRect;
				SplitSettingsControlRow(Label, DDNetMetrics, &Label, &EditBox);
				const auto MapRow = ResolveSettingsInputActionsLayout(EditBox, 2, DDNetMetrics);
				EditBox = MapRow.m_Input;
				ReloadButton = MapRow.m_Actions.m_aButtons[0];
				Button = MapRow.m_Actions.m_aButtons[1];

				DoSettingsMenuLabel(SETTINGS_DDNET, -1, -1, "ddnet-background-map-label", &Label, Localize("Map"), BodySize, TEXTALIGN_ML);

				IUiContext DDNetBackgroundEntitiesTextInputCtx;
				DDNetBackgroundEntitiesTextInputCtx.m_pUi = Ui();
				DDNetBackgroundEntitiesTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_ddnet_background_entities_text_input");
				ui_widget::SInputFieldOptions InputOptions;
				InputOptions.m_FontSize = BodySize;
				const auto InputResult = ui_widget::InputField(DDNetBackgroundEntitiesTextInputCtx, &s_BackgroundEntitiesInput, EditBox, InputOptions);
				if(CanCommit && InputResult.m_Committed)
					BackgroundChanged = CommitBackgroundEntitiesInput(*Ui(), s_BackgroundEntitiesInput, pLastBackgroundPath);

				static CButtonContainer s_BackgroundEntitiesMapPicker, s_BackgroundEntitiesReload;

				if(Ui()->DoButton_QmIcon(&s_BackgroundEntitiesReload, EQmIcon::ARROW_ROTATE_RIGHT, FONT_ICON_ARROW_ROTATE_RIGHT, 0, &ReloadButton, BUTTONFLAG_LEFT) && CanCommit)
				{
					if(s_BackgroundEntitiesInput.IsActive())
						Ui()->ReleaseActiveTextInput(&s_BackgroundEntitiesInput);
					s_BackgroundEntitiesInput.Set("");
					BackgroundChanged = true;
				}

				if(Ui()->DoButton_QmIcon(&s_BackgroundEntitiesMapPicker, EQmIcon::FOLDER, FONT_ICON_FOLDER, 0, &Button, BUTTONFLAG_LEFT) && CanCommit)
				{
					if(s_BackgroundEntitiesInput.IsActive())
						BackgroundChanged |= CommitBackgroundEntitiesInput(*Ui(), s_BackgroundEntitiesInput, pLastBackgroundPath);
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
					SPopupMenuProperties PopupProps = ui_widget::SecondaryPanelProperties(ui_widget::ESecondaryPanelPlacement::ANCHORED);
					Ui()->DoPopupMenu(&s_PopupMapPickerId, Ui()->MouseX(), Ui()->MouseY(), 300.0f, PopupPolicy.m_PreferredHeight, &s_PopupMapPickerContext, PopupMapPicker, PopupProps);
				}
			}
			if(HasCurrentMapRow)
			{
				Button = CurrentMapRow;
				const bool UseCurrentMap = IsCurrentMapBackgroundEntitiesValue(g_Config.m_ClBackgroundEntities);
				static int s_UseCurrentMapId = 0;
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &s_UseCurrentMapId, "Use current map as background", Localize("Use current map as background"), UseCurrentMap, &Button) && CanCommit)
				{
					if(s_BackgroundEntitiesInput.IsActive())
						Ui()->ReleaseActiveTextInput(&s_BackgroundEntitiesInput);
					s_BackgroundEntitiesInput.Set(UseCurrentMap ? "" : CURRENT_MAP);
					BackgroundChanged = true;
				}
			}
			if(CanCommit && BackgroundChanged)
				GameClient()->m_Background.LoadBackground();

			if(HasTileLayersRow)
			{
				Button = TileLayersRow;
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClBackgroundShowTilesLayers, "Show tiles layers from BG map", Localize("Show tiles layers from BG map"), g_Config.m_ClBackgroundShowTilesLayers, &Button) && CanCommit)
					g_Config.m_ClBackgroundShowTilesLayers ^= 1;
			}
		});

		AddCard(MiscellaneousSpec, [=, this](CUIRect &ContentRect) {
			CSettingsContentLayoutFlow Flow(ContentRect);
			const bool CanCommit = Flow.Input() && !RenderOnly && !Ui()->RenderOnly();
			CUIRect Button, Label;
			static CButtonContainer s_ButtonTimeout;
			if(Flow.Control("timeout-code", DDNetMetrics.m_LineHeight, Button))
			{
				if(DoSettingsButton_Menu(SETTINGS_DDNET, -1, -1, &s_ButtonTimeout, "ddnet-new-random-timeout-code", Localize("New random timeout code"), 0, &Button) && CanCommit)
					Client()->GenerateTimeoutSeed();
			}
			// 超时码按钮与「加入后执行」是两段内容：取分区分档，原来的 +5.0 是早期硬编补丁。
			if(Flow.Control("run-on-join-label", DDNetMetrics.m_LineHeight, Label, ui_token::settings::SectionGap(DDNetMetrics.m_LineSpacing)))
			{
				CUIElement &Element = SettingsTextElement(SETTINGS_DDNET, -1, "ddnet-run-on-join-label");
				DoSettingsLabelStreamed(Element, &Label, Localize("Run on join"), BodySize, TEXTALIGN_ML);
			}
			if(Flow.Control("run-on-join-input", DDNetMetrics.m_LineHeight, Button, ui_token::settings::RowGap(DDNetMetrics.m_LineSpacing)))
			{
				static CLineInput s_RunOnJoinInput(g_Config.m_ClRunOnJoin, sizeof(g_Config.m_ClRunOnJoin));
				IUiContext InputCtx;
				InputCtx.m_pUi = Ui();
				InputCtx.m_ScopeHash = MakeUiScopeHash("settings_ddnet_run_on_join_text_input");
				ui_widget::InputField(InputCtx, &s_RunOnJoinInput, Button, Localize("Chat command (e.g. showall 1)"), BodySize);
			}
#if defined(CONF_FAMILY_WINDOWS)
			// 加入后执行区 → 文件扩展名注册：真分区，间距由这一行自己声明（原来先补 1× 再补 2×，叠成 3×）。
			if(Flow.Control("unregister-shell", DDNetMetrics.m_LineHeight, Button, ui_token::settings::SectionGap(DDNetMetrics.m_LineSpacing)))
			{
				static CButtonContainer s_ButtonUnregisterShell;
				if(DoSettingsButton_Menu(SETTINGS_DDNET, -1, -1, &s_ButtonUnregisterShell, "ddnet-unregister-protocol-file-extensions", Localize("Unregister protocol and file extensions"), 0, &Button) && CanCommit)
					Client()->ShellUnregister();
			}
#endif
		});
	};
	const uint64_t DDNetLayoutRevision = RenderOnly;
	if(pCards != nullptr)
	{
		BuildDefinitions(*pCards);
	}
	return DDNetLayoutRevision;
}
