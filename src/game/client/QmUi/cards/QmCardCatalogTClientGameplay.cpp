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

#include <SDL_audio.h>

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
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	CUIRect Button, Label;
	const float UiScale = Ctx.m_Metrics.m_UiScale;
	const SSectionCullContext CullContext{Ctx.m_Page.m_ScrollViewport.y, Ctx.m_Page.m_ScrollViewport.y + Ctx.m_Page.m_ScrollViewport.h, 720.0f};
	const auto ShouldRenderSection = [&](CUIRect Column, float Padding, float Height) {
		Column.HSplitTop(Padding, nullptr, &Column);
		Column.HSplitTop(Height, &Column, nullptr);
		return IsSectionVisible(Column, CullContext);
	};
	const auto LogSettingsStage = [this](const char *pStage, const CPerfTimer &Timer) { LogTClientPerfStage(pStage, Timer.ElapsedMs(), false); };
	auto LayoutInputSection = [&](CUIRect &CurrentColumn, bool Render) {
		CUIRect BoxRect;
		CUIRect TmpButton;
		CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
		BoxRect = CurrentColumn;
		CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpButton, &CurrentColumn);
		if(Render)
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Input"), HeadlineFontSize, TEXTALIGN_ML);
		CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
		CTClientSettingsRowAllocator Rows(CurrentColumn);
		CUIRect Row = Rows.Next();
		if(Render)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmFastInput, "tclient-fast-input", Localize("Fast input (reduce visual latency)"), &g_Config.m_QmFastInput, &Row, LineSize);
		Row = Rows.Next();
		if(Render)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmAutoMargin, "qm-auto-margin", Localize("Auto margin"), &g_Config.m_QmAutoMargin, &Row, LineSize);
		Button = Rows.Next();
		if(Render)
			DoSliderWithScaledValue(&g_Config.m_QmFastInputAmount, &g_Config.m_QmFastInputAmount, &Button, Localize("Amount"), 1, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
		Row = Rows.Next();
		if(Render)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmFastInputOthers, "tclient-fast-input-others", Localize("Fast input others"), &g_Config.m_QmFastInputOthers, &Row, LineSize);
		Row = Rows.Next();
		if(Render)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSubTickAiming, "tclient-sub-tick-aiming", Localize("Sub-Tick aiming"), &g_Config.m_ClSubTickAiming, &Row, LineSize);
		BoxRect.h = CurrentColumn.y - BoxRect.y;
		return BoxRect;
	};
	CUIRect Measured = Content;
	const CUIRect Box = LayoutInputSection(Measured, false);
	const float Header = Box.y - Content.y + HeadlineHeight + MarginSmall;
	const float Height = Measured.y - Content.y - Header;
	if(Render)
	{
		CUIRect LegacyContent = Content;
		LegacyContent.y -= Header;
		LegacyContent.h += Header;
		const CUIRect Clip = ResolveSettingsFocusSafeClipRect(Content, UiScale);
		Ui()->ClipEnable(&Clip);
		LayoutInputSection(LegacyContent, true);
		Ui()->ClipDisable();
		Content.y = LegacyContent.y;
	}
	return Height;
}

float CMenus::LayoutTClientAntiLatencyToolsCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
{
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	CUIRect Button, Label;
	const float UiScale = Ctx.m_Metrics.m_UiScale;
	const SSectionCullContext CullContext{Ctx.m_Page.m_ScrollViewport.y, Ctx.m_Page.m_ScrollViewport.y + Ctx.m_Page.m_ScrollViewport.h, 720.0f};
	const auto ShouldRenderSection = [&](CUIRect Column, float Padding, float Height) {
		Column.HSplitTop(Padding, nullptr, &Column);
		Column.HSplitTop(Height, &Column, nullptr);
		return IsSectionVisible(Column, CullContext);
	};
	const auto LogSettingsStage = [this](const char *pStage, const CPerfTimer &Timer) { LogTClientPerfStage(pStage, Timer.ElapsedMs(), false); };
	auto LayoutAntiLatencyToolsSection = [&](CUIRect &CurrentColumn, bool Render) {
		CUIRect BoxRect;
		CUIRect TmpButton;
		CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
		BoxRect = CurrentColumn;
		CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpButton, &CurrentColumn);
		if(Render)
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Anti Latency Tools"), HeadlineFontSize, TEXTALIGN_ML);
		CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
		CTClientSettingsRowAllocator Rows(CurrentColumn);
		Button = Rows.Next();
		if(Render)
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-prediction-margin", &g_Config.m_ClPredictionMargin, &g_Config.m_ClPredictionMargin, &Button, Localize("Base prediction margin"), 10, 75, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
		CUIRect Row = Rows.Next();
		if(Render)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRemoveAnti, "tclient-remove-anti-freeze", Localize("Reduce prediction while frozen"), &g_Config.m_QmRemoveAnti, &Row, LineSize);
		if(g_Config.m_QmRemoveAnti)
		{
			CUIRect AmountButton = Rows.Next();
			CUIRect DelayButton = Rows.Next();
			if(Render)
			{
				if(g_Config.m_QmUnfreezeLagDelayTicks < g_Config.m_QmUnfreezeLagTicks)
					g_Config.m_QmUnfreezeLagDelayTicks = g_Config.m_QmUnfreezeLagTicks;
				DoSliderWithScaledValue(&g_Config.m_QmUnfreezeLagTicks, &g_Config.m_QmUnfreezeLagTicks, &AmountButton, Localize("Maximum reduction"), 100, 300, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
				DoSliderWithScaledValue(&g_Config.m_QmUnfreezeLagDelayTicks, &g_Config.m_QmUnfreezeLagDelayTicks, &DelayButton, Localize("Delay before reduction"), 100, 3000, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
			}
		}
		Row = Rows.Next();
		if(Render)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmUnpredOthersInFreeze, "tclient-unpred-others-in-freeze", Localize("Dont predict other players if you are frozen"), &g_Config.m_QmUnpredOthersInFreeze, &Row, LineSize);
		Row = Rows.Next();
		if(Render)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmPredMarginInFreeze, "tclient-pred-margin-in-freeze", Localize("Use a fixed prediction margin while frozen"), &g_Config.m_QmPredMarginInFreeze, &Row, LineSize);
		if(g_Config.m_QmPredMarginInFreeze)
		{
			Button = Rows.Next();
			if(Render)
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-frozen-margin", &g_Config.m_QmPredMarginInFreezeAmount, &g_Config.m_QmPredMarginInFreezeAmount, &Button, Localize("Frozen prediction margin"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "ms");
		}
		BoxRect.h = CurrentColumn.y - BoxRect.y;
		return BoxRect;
	};
	CUIRect Measured = Content;
	const CUIRect Box = LayoutAntiLatencyToolsSection(Measured, false);
	const float Header = Box.y - Content.y + HeadlineHeight + MarginSmall;
	const float Height = Measured.y - Content.y - Header;
	if(Render)
	{
		CUIRect LegacyContent = Content;
		LegacyContent.y -= Header;
		LegacyContent.h += Header;
		const CUIRect Clip = ResolveSettingsFocusSafeClipRect(Content, UiScale);
		Ui()->ClipEnable(&Clip);
		LayoutAntiLatencyToolsSection(LegacyContent, true);
		Ui()->ClipDisable();
		Content.y = LegacyContent.y;
	}
	return Height;
}

float CMenus::LayoutTClientAntiPingSmoothingCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
{
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	CUIRect Button, Label;
	const float UiScale = Ctx.m_Metrics.m_UiScale;
	const SSectionCullContext CullContext{Ctx.m_Page.m_ScrollViewport.y, Ctx.m_Page.m_ScrollViewport.y + Ctx.m_Page.m_ScrollViewport.h, 720.0f};
	const auto ShouldRenderSection = [&](CUIRect Column, float Padding, float Height) {
		Column.HSplitTop(Padding, nullptr, &Column);
		Column.HSplitTop(Height, &Column, nullptr);
		return IsSectionVisible(Column, CullContext);
	};
	const auto LogSettingsStage = [this](const char *pStage, const CPerfTimer &Timer) { LogTClientPerfStage(pStage, Timer.ElapsedMs(), false); };
	auto LayoutAntiPingSmoothingSection = [&](CUIRect &CurrentColumn, bool Render) {
		CUIRect BoxRect;
		CUIRect TmpButton;
		CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
		BoxRect = CurrentColumn;
		CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpButton, &CurrentColumn);
		if(Render)
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Anti Ping Smoothing"), HeadlineFontSize, TEXTALIGN_ML);
		CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
		CTClientSettingsRowAllocator Rows(CurrentColumn);
		CUIRect Row = Rows.Next();
		if(Render)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmAntiPingImproved, "tclient-antiping-improved", Localize("Use new smoothing algorithm"), &g_Config.m_QmAntiPingImproved, &Row, LineSize);
		Row = Rows.Next();
		if(Render)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmAntiPingStableDirection, "tclient-antiping-stable-direction", Localize("Optimistic prediction along stable direction"), &g_Config.m_QmAntiPingStableDirection, &Row, LineSize);
		Row = Rows.Next();
		if(Render)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmAntiPingNegativeBuffer, "tclient-antiping-negative-buffer", Localize("Negative stability buffer (for Gores)"), &g_Config.m_QmAntiPingNegativeBuffer, &Row, LineSize);
		Button = Rows.Next();
		if(Render)
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-antiping-uncertainty-scale", &g_Config.m_QmAntiPingUncertaintyScale, &g_Config.m_QmAntiPingUncertaintyScale, &Button, Localize("Uncertainty duration"), 50, 400, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");
		BoxRect.h = CurrentColumn.y - BoxRect.y;
		return BoxRect;
	};
	CUIRect Measured = Content;
	const CUIRect Box = LayoutAntiPingSmoothingSection(Measured, false);
	const float Header = Box.y - Content.y + HeadlineHeight + MarginSmall;
	const float Height = Measured.y - Content.y - Header;
	if(Render)
	{
		CUIRect LegacyContent = Content;
		LegacyContent.y -= Header;
		LegacyContent.h += Header;
		const CUIRect Clip = ResolveSettingsFocusSafeClipRect(Content, UiScale);
		Ui()->ClipEnable(&Clip);
		LayoutAntiPingSmoothingSection(LegacyContent, true);
		Ui()->ClipDisable();
		Content.y = LegacyContent.y;
	}
	return Height;
}

float CMenus::LayoutTClientAutoExecuteCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
{
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	CUIRect Button, Label;
	const float UiScale = Ctx.m_Metrics.m_UiScale;
	const SSectionCullContext CullContext{Ctx.m_Page.m_ScrollViewport.y, Ctx.m_Page.m_ScrollViewport.y + Ctx.m_Page.m_ScrollViewport.h, 720.0f};
	const auto ShouldRenderSection = [&](CUIRect Column, float Padding, float Height) {
		Column.HSplitTop(Padding, nullptr, &Column);
		Column.HSplitTop(Height, &Column, nullptr);
		return IsSectionVisible(Column, CullContext);
	};
	const auto LogSettingsStage = [this](const char *pStage, const CPerfTimer &Timer) { LogTClientPerfStage(pStage, Timer.ElapsedMs(), false); };
	auto LayoutAutoExecuteSection = [&](CUIRect &CurrentColumn, bool Render) {
		CUIRect BoxRect;
		CUIRect TmpRect;
		CUIRect Box;
		IUiContext TClientAutoExecuteTextInputCtx;
		TClientAutoExecuteTextInputCtx.m_pUi = Ui();
		TClientAutoExecuteTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
		TClientAutoExecuteTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
		TClientAutoExecuteTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_auto_execute_text_inputs");
		TClientAutoExecuteTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
		CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
		BoxRect = CurrentColumn;
		CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
		if(Render)
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Auto execute"), HeadlineFontSize, TEXTALIGN_ML);
		CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
		CTClientSettingsRowAllocator Rows(CurrentColumn);

		const bool RenderBeforeConnectInput = Render && ShouldRenderSection(CurrentColumn, 0.0f, TClientSettingsRowsHeight(3));
		Box = Rows.Next();
		if(RenderBeforeConnectInput)
		{
			Box.VSplitMid(&Label, &Button);
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Execute before connecting"), FontSize, TEXTALIGN_ML);
			static CLineInput s_LineInput(g_Config.m_QmExecuteOnConnect, sizeof(g_Config.m_QmExecuteOnConnect));
			ui_widget::InputField(TClientAutoExecuteTextInputCtx, &s_LineInput, Button, nullptr, EditBoxFontSize);
		}

		const bool RenderOnConnectInput = RenderBeforeConnectInput;
		Box = Rows.Next();
		if(RenderOnConnectInput)
		{
			Box.VSplitMid(&Label, &Button);
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Execute on connect"), FontSize, TEXTALIGN_ML);
			static CLineInput s_LineInput(g_Config.m_QmExecuteOnJoin, sizeof(g_Config.m_QmExecuteOnJoin));
			ui_widget::InputField(TClientAutoExecuteTextInputCtx, &s_LineInput, Button, nullptr, EditBoxFontSize);
		}

		const bool RenderDelaySlider = RenderBeforeConnectInput;
		Button = Rows.Next();
		if(RenderDelaySlider)
			DoSliderWithScaledValue(&g_Config.m_QmExecuteOnJoinDelay, &g_Config.m_QmExecuteOnJoinDelay, &Button, Localize("Delay"), 140, 2000, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
		BoxRect.h = CurrentColumn.y - BoxRect.y;
		return BoxRect;
	};
	CUIRect Measured = Content;
	const CUIRect Box = LayoutAutoExecuteSection(Measured, false);
	const float Header = Box.y - Content.y + HeadlineHeight + MarginSmall;
	const float Height = Measured.y - Content.y - Header;
	if(Render)
	{
		CUIRect LegacyContent = Content;
		LegacyContent.y -= Header;
		LegacyContent.h += Header;
		const CUIRect Clip = ResolveSettingsFocusSafeClipRect(Content, UiScale);
		Ui()->ClipEnable(&Clip);
		LayoutAutoExecuteSection(LegacyContent, true);
		Ui()->ClipDisable();
		Content.y = LegacyContent.y;
	}
	return Height;
}

float CMenus::LayoutTClientVotingCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
{
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	CUIRect Button, Label;
	const float UiScale = Ctx.m_Metrics.m_UiScale;
	const SSectionCullContext CullContext{Ctx.m_Page.m_ScrollViewport.y, Ctx.m_Page.m_ScrollViewport.y + Ctx.m_Page.m_ScrollViewport.h, 720.0f};
	const auto ShouldRenderSection = [&](CUIRect Column, float Padding, float Height) {
		Column.HSplitTop(Padding, nullptr, &Column);
		Column.HSplitTop(Height, &Column, nullptr);
		return IsSectionVisible(Column, CullContext);
	};
	const auto LogSettingsStage = [this](const char *pStage, const CPerfTimer &Timer) { LogTClientPerfStage(pStage, Timer.ElapsedMs(), false); };
	auto LayoutVotingSection = [&](CUIRect &CurrentColumn, bool Render) {
		CUIRect BoxRect;
		CUIRect TmpRect;
		CUIRect VoteMessage;
		IUiContext TClientVotingTextInputCtx;
		TClientVotingTextInputCtx.m_pUi = Ui();
		TClientVotingTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
		TClientVotingTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
		TClientVotingTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_voting_text_inputs");
		TClientVotingTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
		CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
		BoxRect = CurrentColumn;
		CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
		if(Render)
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Voting"), HeadlineFontSize, TEXTALIGN_ML);
		CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
		CTClientSettingsRowAllocator Rows(CurrentColumn);
		const SSettingsContentMetrics ContentMetrics = Ctx.m_Metrics;
		const float AutoVoteHeight = ResolveSettingsRadioRowLayout(CurrentColumn, 3, ContentMetrics).m_Height;
		CUIRect Row = Rows.Next(AutoVoteHeight);

		if(Render)
		{
			static std::vector<CButtonContainer> s_vAutoMapVoteButtons = {{}, {}, {}};
			int AutoMapVote = std::clamp(g_Config.m_QmAutoVoteWhenFar, 0, 2);
			if(DoSettingsLine_RadioMenu(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, Row, "tclient-auto-map-vote-label", Localize("Auto map vote"), s_vAutoMapVoteButtons, {"tclient-auto-map-vote-off", "tclient-auto-map-vote-agree", "tclient-auto-map-vote-reject"}, {Localize("Off"), Localize("Auto agree vote"), Localize("Auto reject vote")}, {0, 2, 1}, AutoMapVote, ContentMetrics))
				g_Config.m_QmAutoVoteWhenFar = AutoMapVote;
		}
		Button = Rows.Next();
		if(Render)
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-auto-vote-minimum-time", &g_Config.m_QmAutoVoteWhenFarTime, &g_Config.m_QmAutoVoteWhenFarTime, &Button, Localize("Minimum time"), 1, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, Localize(" min"));

		VoteMessage = Rows.Next();
		if(Render)
		{
			VoteMessage.VSplitMid(&Label, &VoteMessage);
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Message to send in chat:"), FontSize, TEXTALIGN_ML);
			static CLineInput s_VoteMessage(g_Config.m_QmAutoVoteWhenFarMessage, sizeof(g_Config.m_QmAutoVoteWhenFarMessage));
			s_VoteMessage.SetEmptyText(Localize("Leave empty to disable"));
			ui_widget::InputField(TClientVotingTextInputCtx, &s_VoteMessage, VoteMessage, nullptr, EditBoxFontSize);
		}
		BoxRect.h = CurrentColumn.y - BoxRect.y;
		return BoxRect;
	};
	CUIRect Measured = Content;
	const CUIRect Box = LayoutVotingSection(Measured, false);
	const float Header = Box.y - Content.y + HeadlineHeight + MarginSmall;
	const float Height = Measured.y - Content.y - Header;
	if(Render)
	{
		CUIRect LegacyContent = Content;
		LegacyContent.y -= Header;
		LegacyContent.h += Header;
		const CUIRect Clip = ResolveSettingsFocusSafeClipRect(Content, UiScale);
		Ui()->ClipEnable(&Clip);
		LayoutVotingSection(LegacyContent, true);
		Ui()->ClipDisable();
		Content.y = LegacyContent.y;
	}
	return Height;
}

float CMenus::LayoutTClientFinishNameCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
{
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	CUIRect Button, Label;
	const float UiScale = Ctx.m_Metrics.m_UiScale;
	const SSectionCullContext CullContext{Ctx.m_Page.m_ScrollViewport.y, Ctx.m_Page.m_ScrollViewport.y + Ctx.m_Page.m_ScrollViewport.h, 720.0f};
	const auto ShouldRenderSection = [&](CUIRect Column, float Padding, float Height) {
		Column.HSplitTop(Padding, nullptr, &Column);
		Column.HSplitTop(Height, &Column, nullptr);
		return IsSectionVisible(Column, CullContext);
	};
	const auto LogSettingsStage = [this](const char *pStage, const CPerfTimer &Timer) { LogTClientPerfStage(pStage, Timer.ElapsedMs(), false); };
	auto LayoutFinishNameSection = [&](CUIRect &CurrentColumn, bool Render) {
		CUIRect BoxRect;
		CUIRect TmpRect;
		CUIRect FinishNameBox;
		IUiContext TClientFinishNameTextInputCtx;
		TClientFinishNameTextInputCtx.m_pUi = Ui();
		TClientFinishNameTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
		TClientFinishNameTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
		TClientFinishNameTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_finish_name_text_inputs");
		TClientFinishNameTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
		CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
		BoxRect = CurrentColumn;
		CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
		if(Render)
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Finish Name"), HeadlineFontSize, TEXTALIGN_ML);
		CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
		CTClientSettingsRowAllocator Rows(CurrentColumn);
		CUIRect ToggleRow = Rows.Next();
		if(Render)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmChangeNameNearFinish, "tclient-change-name-near-finish", Localize("Attempt to change your name when near finish"), &g_Config.m_QmChangeNameNearFinish, &ToggleRow, LineSize);
		if(g_Config.m_QmChangeNameNearFinish)
		{
			FinishNameBox = Rows.Next();
			if(Render)
			{
				FinishNameBox.VSplitMid(&Label, &Button);
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Finish Name:"), FontSize, TEXTALIGN_ML);
				static CLineInput s_FinishName(g_Config.m_QmFinishName, sizeof(g_Config.m_QmFinishName));
				ui_widget::InputField(TClientFinishNameTextInputCtx, &s_FinishName, Button, nullptr, EditBoxFontSize);
			}
		}
		BoxRect.h = CurrentColumn.y - BoxRect.y;
		return BoxRect;
	};
	CUIRect Measured = Content;
	const CUIRect Box = LayoutFinishNameSection(Measured, false);
	const float Header = Box.y - Content.y + HeadlineHeight + MarginSmall;
	const float Height = Measured.y - Content.y - Header;
	if(Render)
	{
		CUIRect LegacyContent = Content;
		LegacyContent.y -= Header;
		LegacyContent.h += Header;
		const CUIRect Clip = ResolveSettingsFocusSafeClipRect(Content, UiScale);
		Ui()->ClipEnable(&Clip);
		LayoutFinishNameSection(LegacyContent, true);
		Ui()->ClipDisable();
		Content.y = LegacyContent.y;
	}
	return Height;
}
