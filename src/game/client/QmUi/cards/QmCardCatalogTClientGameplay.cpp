#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/str.h>
#include <base/system.h>
#include <base/types.h>

#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/image.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/config_tags.h>
#include <engine/shared/jobs.h>
#include <engine/shared/json.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/warning.h>

#include <game/client/QmUi/QmCardOrderModel.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmDropdown.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsFontSelection.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/QmUi/cards/QmCardCatalogTClientInternal.h>
#include <game/client/animstate.h>
#include <game/client/components/binds.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/font_download_storage.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/section_loader.h>
#include <game/client/components/skins.h>
#include <game/client/components/tclient/bindchat.h>
#include <game/client/components/tclient/bindwheel.h>
#include <game/client/components/tclient/trails.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/render.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <SDL3/SDL_audio.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace FontIcons;
using namespace qm_tclient_cards;

float CMenus::LayoutTClientInputCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
{
	const CTClientCardMetricsScope CardMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Button, Label;
	CUIRect Row;
	if(Flow.Control("fast-input", LineSize, Row, RowSpacing))
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmFastInput, "tclient-fast-input", Localize("Fast input (reduce visual latency)"), &g_Config.m_QmFastInput, &Row, LineSize);
	if(Flow.Control("auto-margin", LineSize, Row, RowSpacing))
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmAutoMargin, "qm-auto-margin", Localize("Auto margin"), &g_Config.m_QmAutoMargin, &Row, LineSize);
	if(Flow.Control("input-amount", LineSize, Button, RowSpacing))
		DoSliderWithScaledValue(&g_Config.m_QmFastInputAmount, &g_Config.m_QmFastInputAmount, &Button, Localize("Amount"), 1, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
	if(Flow.Control("fast-input-others", LineSize, Row, RowSpacing))
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmFastInputOthers, "tclient-fast-input-others", Localize("Fast input others"), &g_Config.m_QmFastInputOthers, &Row, LineSize);
	if(Flow.Control("sub-tick", LineSize, Row, RowSpacing))
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSubTickAiming, "tclient-sub-tick-aiming", Localize("Sub-Tick aiming"), &g_Config.m_ClSubTickAiming, &Row, LineSize);
	if(MeasureScope)
	{
		CSettingsContentLayoutCache Cache;
		return Cache.Resolve(vRows, Content.w, 0, {})->Height();
	}
	return Content.y - StartY;
}

float CMenus::LayoutTClientAntiLatencyToolsCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
{
	const CTClientCardMetricsScope CardMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Button, Label;
	if(Flow.Control("prediction-margin", LineSize, Button, RowSpacing))
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-prediction-margin", &g_Config.m_ClPredictionMargin, &g_Config.m_ClPredictionMargin, &Button, Localize("Base prediction margin"), 10, 75, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
	CUIRect Row;
	if(Flow.Control("remove-anti", LineSize, Row, RowSpacing))
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRemoveAnti, "tclient-remove-anti-freeze", Localize("Reduce prediction while frozen"), &g_Config.m_QmRemoveAnti, &Row, LineSize);
	static CSettingsConditionalRows s_RemoveAntiRows;
	if(s_RemoveAntiRows.Update(g_Config.m_QmRemoveAnti, Ctx.m_UiContext.m_FrameDt, g_Config.m_QmUiMotionLevel))
	{
		const CSettingsContentRevealScope RemoveAntiRevealScope(Ui(), Flow, s_RemoveAntiRows.Fade(g_Config.m_QmUiMotionLevel));
		CUIRect AmountButton, DelayButton;
		if(Flow.Input() && !Ui()->RenderOnly() && g_Config.m_QmUnfreezeLagDelayTicks < g_Config.m_QmUnfreezeLagTicks)
			g_Config.m_QmUnfreezeLagDelayTicks = g_Config.m_QmUnfreezeLagTicks;
		if(Flow.Control("maximum-reduction", LineSize, AmountButton, RowSpacing))
			DoSliderWithScaledValue(&g_Config.m_QmUnfreezeLagTicks, &g_Config.m_QmUnfreezeLagTicks, &AmountButton, Localize("Maximum reduction"), 100, 300, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
		if(Flow.Control("reduction-delay", LineSize, DelayButton, RowSpacing))
			DoSliderWithScaledValue(&g_Config.m_QmUnfreezeLagDelayTicks, &g_Config.m_QmUnfreezeLagDelayTicks, &DelayButton, Localize("Delay before reduction"), 100, 3000, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
	}
	if(Flow.Control("unpred-others", LineSize, Row, RowSpacing))
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmUnpredOthersInFreeze, "tclient-unpred-others-in-freeze", Localize("Dont predict other players if you are frozen"), &g_Config.m_QmUnpredOthersInFreeze, &Row, LineSize);
	if(Flow.Control("frozen-margin-toggle", LineSize, Row, RowSpacing))
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmPredMarginInFreeze, "tclient-pred-margin-in-freeze", Localize("Use a fixed prediction margin while frozen"), &g_Config.m_QmPredMarginInFreeze, &Row, LineSize);
	static CSettingsConditionalRows s_PredMarginInFreezeRows;
	if(s_PredMarginInFreezeRows.Update(g_Config.m_QmPredMarginInFreeze, Ctx.m_UiContext.m_FrameDt, g_Config.m_QmUiMotionLevel))
	{
		const CSettingsContentRevealScope PredMarginInFreezeRevealScope(Ui(), Flow, s_PredMarginInFreezeRows.Fade(g_Config.m_QmUiMotionLevel));
		if(Flow.Control("frozen-margin", LineSize, Button, RowSpacing))
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-frozen-margin", &g_Config.m_QmPredMarginInFreezeAmount, &g_Config.m_QmPredMarginInFreezeAmount, &Button, Localize("Frozen prediction margin"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "ms");
	}
	if(MeasureScope)
	{
		CSettingsContentLayoutCache Cache;
		return Cache.Resolve(vRows, Content.w, 0, {})->Height();
	}
	return Content.y - StartY;
}

float CMenus::LayoutTClientAntiPingSmoothingCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
{
	const CTClientCardMetricsScope CardMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Button, Label;
	CUIRect Row;
	if(Flow.Control("new-smoothing", LineSize, Row, RowSpacing))
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmAntiPingImproved, "tclient-antiping-improved", Localize("Use new smoothing algorithm"), &g_Config.m_QmAntiPingImproved, &Row, LineSize);
	if(Flow.Control("stable-direction", LineSize, Row, RowSpacing))
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmAntiPingStableDirection, "tclient-antiping-stable-direction", Localize("Optimistic prediction along stable direction"), &g_Config.m_QmAntiPingStableDirection, &Row, LineSize);
	if(Flow.Control("negative-buffer", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmAntiPingNegativeBuffer, "tclient-antiping-negative-buffer", Localize("Negative stability buffer"), &g_Config.m_QmAntiPingNegativeBuffer, &Row, LineSize);
		// 约定 B：原来的括号注解塞在标签里，改成同一行的行尾说明——标签只留正题，
		// 注解常显、小一档字号、次要色，且不占独立行高。
		if(Flow.Drawing())
			DoSettingsInlineHint(Ui(), "negative-buffer", Row, 0.0f, Localize("for Gores"), Ctx.m_Metrics.m_SmallSize);
	}
	if(Flow.Control("uncertainty", LineSize, Button, RowSpacing))
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-antiping-uncertainty-scale", &g_Config.m_QmAntiPingUncertaintyScale, &g_Config.m_QmAntiPingUncertaintyScale, &Button, Localize("Uncertainty duration"), 50, 400, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");
	if(MeasureScope)
	{
		CSettingsContentLayoutCache Cache;
		return Cache.Resolve(vRows, Content.w, 0, {})->Height();
	}
	return Content.y - StartY;
}

float CMenus::LayoutTClientAutoExecuteCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
{
	const CTClientCardMetricsScope CardMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Button, Label;
	CUIRect Box;
	IUiContext TClientAutoExecuteTextInputCtx;
	TClientAutoExecuteTextInputCtx.m_pUi = Ui();
	TClientAutoExecuteTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	TClientAutoExecuteTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	TClientAutoExecuteTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_auto_execute_text_inputs");
	TClientAutoExecuteTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	if(Flow.Control("before-connect", LineSize, Box, RowSpacing))
	{
		Box.VSplitMid(&Label, &Button);
		DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Execute before connecting"), FontSize, TEXTALIGN_ML);
		static CLineInput s_LineInput(g_Config.m_QmExecuteOnConnect, sizeof(g_Config.m_QmExecuteOnConnect));
		ui_widget::InputField(TClientAutoExecuteTextInputCtx, &s_LineInput, Button, nullptr, EditBoxFontSize);
	}
	if(Flow.Control("on-connect", LineSize, Box, RowSpacing))
	{
		Box.VSplitMid(&Label, &Button);
		DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Execute on connect"), FontSize, TEXTALIGN_ML);
		static CLineInput s_LineInput(g_Config.m_QmExecuteOnJoin, sizeof(g_Config.m_QmExecuteOnJoin));
		ui_widget::InputField(TClientAutoExecuteTextInputCtx, &s_LineInput, Button, nullptr, EditBoxFontSize);
	}
	if(Flow.Control("join-delay", LineSize, Button, RowSpacing))
		DoSliderWithScaledValue(&g_Config.m_QmExecuteOnJoinDelay, &g_Config.m_QmExecuteOnJoinDelay, &Button, Localize("Delay"), 140, 2000, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
	if(MeasureScope)
	{
		CSettingsContentLayoutCache Cache;
		return Cache.Resolve(vRows, Content.w, 0, {})->Height();
	}
	return Content.y - StartY;
}

float CMenus::LayoutTClientVotingCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
{
	const CTClientCardMetricsScope CardMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Button, Label;
	CUIRect VoteMessage;
	IUiContext TClientVotingTextInputCtx;
	TClientVotingTextInputCtx.m_pUi = Ui();
	TClientVotingTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	TClientVotingTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	TClientVotingTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_voting_text_inputs");
	TClientVotingTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	const SSettingsContentMetrics ContentMetrics = Ctx.m_Metrics;
	const float AutoVoteHeight = ResolveSettingsRadioRowLayout(Content, 3, ContentMetrics).m_Height;
	CUIRect Row;
	if(Flow.Control("map-vote", AutoVoteHeight, Row, RowSpacing))
	{
		static std::vector<CButtonContainer> s_vAutoMapVoteButtons = {{}, {}, {}};
		int AutoMapVote = std::clamp(g_Config.m_QmAutoVoteWhenFar, 0, 2);
		if(DoSettingsLine_RadioMenu(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, Row, "tclient-auto-map-vote-label", Localize("Auto map vote"), s_vAutoMapVoteButtons, {"tclient-auto-map-vote-off", "tclient-auto-map-vote-agree", "tclient-auto-map-vote-reject"}, {Localize("Off"), Localize("Auto agree vote"), Localize("Auto reject vote")}, {0, 2, 1}, AutoMapVote, ContentMetrics, nullptr, &g_Config.m_QmAutoVoteWhenFar))
			g_Config.m_QmAutoVoteWhenFar = AutoMapVote;
	}
	if(Flow.Control("min-time", LineSize, Button, RowSpacing))
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-auto-vote-minimum-time", &g_Config.m_QmAutoVoteWhenFarTime, &g_Config.m_QmAutoVoteWhenFarTime, &Button, Localize("Minimum time"), 1, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, Localize(" min"));

	if(Flow.Control("vote-message", LineSize, VoteMessage, RowSpacing))
	{
		VoteMessage.VSplitMid(&Label, &VoteMessage);
		DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Message to send in chat:"), FontSize, TEXTALIGN_ML);
		static CLineInput s_VoteMessage(g_Config.m_QmAutoVoteWhenFarMessage, sizeof(g_Config.m_QmAutoVoteWhenFarMessage));
		s_VoteMessage.SetEmptyText(Localize("Leave empty to disable"));
		ui_widget::InputField(TClientVotingTextInputCtx, &s_VoteMessage, VoteMessage, nullptr, EditBoxFontSize);
	}
	if(MeasureScope)
	{
		CSettingsContentLayoutCache Cache;
		return Cache.Resolve(vRows, Content.w, 0, {})->Height();
	}
	return Content.y - StartY;
}

float CMenus::LayoutTClientFinishNameCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
{
	const CTClientCardMetricsScope CardMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Button, Label;
	CUIRect FinishNameBox;
	IUiContext TClientFinishNameTextInputCtx;
	TClientFinishNameTextInputCtx.m_pUi = Ui();
	TClientFinishNameTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	TClientFinishNameTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	TClientFinishNameTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_finish_name_text_inputs");
	TClientFinishNameTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	CUIRect ToggleRow;
	if(Flow.Control("finish-name-toggle", LineSize, ToggleRow, RowSpacing))
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmChangeNameNearFinish, "tclient-change-name-near-finish", Localize("Attempt to change your name when near finish"), &g_Config.m_QmChangeNameNearFinish, &ToggleRow, LineSize);
	static CSettingsConditionalRows s_FinishNameRows;
	if(s_FinishNameRows.Update(g_Config.m_QmChangeNameNearFinish, Ctx.m_UiContext.m_FrameDt, g_Config.m_QmUiMotionLevel))
	{
		const CSettingsContentRevealScope FinishNameRevealScope(Ui(), Flow, s_FinishNameRows.Fade(g_Config.m_QmUiMotionLevel));
		if(Flow.Control("finish-name", LineSize, FinishNameBox, RowSpacing))
		{
			FinishNameBox.VSplitMid(&Label, &Button);
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Finish Name:"), FontSize, TEXTALIGN_ML);
			static CLineInput s_FinishName(g_Config.m_QmFinishName, sizeof(g_Config.m_QmFinishName));
			ui_widget::InputField(TClientFinishNameTextInputCtx, &s_FinishName, Button, nullptr, EditBoxFontSize);
		}
	}
	if(MeasureScope)
	{
		CSettingsContentLayoutCache Cache;
		return Cache.Resolve(vRows, Content.w, 0, {})->Height();
	}
	return Content.y - StartY;
}
