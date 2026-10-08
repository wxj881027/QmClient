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

float CMenus::LayoutTClientVisualNameplateCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
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
	auto LayoutVisualNameplateSection = [&](CUIRect &CurrentColumn, bool Render) {
		CUIRect BoxRect = CurrentColumn;
		CUIRect TmpLabel;
		IUiContext TClientWhiteFeetTextInputCtx;
		TClientWhiteFeetTextInputCtx.m_pUi = Ui();
		TClientWhiteFeetTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
		TClientWhiteFeetTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
		TClientWhiteFeetTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_white_feet_text_inputs");
		TClientWhiteFeetTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
		auto ShouldRenderVisualBlock = [&](float Height) {
			return Render && ShouldRenderSection(CurrentColumn, 0.0f, Height);
		};
		CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
		BoxRect = CurrentColumn;
		CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpLabel, &CurrentColumn);
		if(Render)
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Visual: Nameplates"), HeadlineFontSize, TEXTALIGN_ML);
		CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);

		const int NameplateRowCount = 7 + (g_Config.m_QmWhiteFeet ? 1 : 0);
		const bool RenderNameplateRows = ShouldRenderVisualBlock(TClientSettingsRowsHeight(NameplateRowCount));
		CTClientSettingsRowAllocator Rows(CurrentColumn);
		CUIRect PingCircleRow = Rows.Next();
		CUIRect CountryRow = Rows.Next();
		CUIRect SkinsRow = Rows.Next();
		CUIRect FreezeStarsRow = Rows.Next();
		CUIRect ColorFreezeRow = Rows.Next();
		CUIRect FreezeKatanaRow = Rows.Next();
		CUIRect WhiteFeetRow = Rows.Next();
		CUIRect FeetBox;
		if(g_Config.m_QmWhiteFeet)
			FeetBox = Rows.Next();
		if(RenderNameplateRows)
		{
			CPerfTimer NameplateTimer;
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNameplatePingCircle, "tclient-nameplate-ping-circle", Localize("Show ping colored circle in nameplates"), &g_Config.m_QmNameplatePingCircle, &PingCircleRow, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNameplateCountry, "tclient-nameplate-country", Localize("Show country flags in nameplates"), &g_Config.m_QmNameplateCountry, &CountryRow, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNameplateSkins, "tclient-nameplate-skins", Localize("Show skin names in nameplate"), &g_Config.m_QmNameplateSkins, &SkinsRow, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFreezeStars, "tclient-freeze-stars", Localize("Freeze stars"), &g_Config.m_ClFreezeStars, &FreezeStarsRow, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmColorFreeze, "tclient-color-freeze", Localize("Use colored skins for frozen tees"), &g_Config.m_QmColorFreeze, &ColorFreezeRow, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmFreezeKatana, "tclient-freeze-katana", Localize("Show katan on frozen players"), &g_Config.m_QmFreezeKatana, &FreezeKatanaRow, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmWhiteFeet, "tclient-white-feet", Localize("Render all custom colored feet as white feet skin"), &g_Config.m_QmWhiteFeet, &WhiteFeetRow, LineSize);
			LogSettingsStage("tclient_settings_left_visual_nameplates", NameplateTimer);
		}
		if(RenderNameplateRows && g_Config.m_QmWhiteFeet)
		{
			FeetBox.VSplitMid(&FeetBox, nullptr);
			static CLineInput s_WhiteFeet(g_Config.m_QmWhiteFeetSkin, sizeof(g_Config.m_QmWhiteFeetSkin));
			s_WhiteFeet.SetEmptyText("x_ninja");
			ui_widget::InputField(TClientWhiteFeetTextInputCtx, &s_WhiteFeet, FeetBox, nullptr, EditBoxFontSize);
		}
		BoxRect.h = CurrentColumn.y - BoxRect.y;
		return BoxRect;
	};
	CUIRect Measured = Content;
	const CUIRect Box = LayoutVisualNameplateSection(Measured, false);
	const float Header = Box.y - Content.y + HeadlineHeight + MarginSmall;
	const float Height = Measured.y - Content.y - Header;
	if(Render)
	{
		CUIRect LegacyContent = Content;
		LegacyContent.y -= Header;
		LegacyContent.h += Header;
		const CUIRect Clip = ResolveSettingsFocusSafeClipRect(Content, UiScale);
		Ui()->ClipEnable(&Clip);
		LayoutVisualNameplateSection(LegacyContent, true);
		Ui()->ClipDisable();
		Content.y = LegacyContent.y;
	}
	return Height;
}

float CMenus::LayoutTClientVisualEffectsCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
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
	auto LayoutVisualEffectsSection = [&](CUIRect &CurrentColumn, bool Render) {
		CUIRect BoxRect = CurrentColumn;
		CUIRect TmpLabel;
		CUIRect TinyTeeConfig;
		auto ShouldRenderVisualBlock = [&](float Height) {
			return Render && ShouldRenderSection(CurrentColumn, 0.0f, Height);
		};
		CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
		BoxRect = CurrentColumn;
		CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpLabel, &CurrentColumn);
		if(Render)
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Visual: Effects"), HeadlineFontSize, TEXTALIGN_ML);
		CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
		CTClientSettingsRowAllocator Rows(CurrentColumn);
		const SSettingsContentMetrics ContentMetrics = ResolveSettingsContentMetrics(CurrentColumn.w);
		const float TinyTeeModeHeight = ResolveSettingsRadioRowLayout(CurrentColumn, 3, ContentMetrics).m_Height;
		const bool RenderTinyTeeMode = ShouldRenderVisualBlock(TinyTeeModeHeight);
		CUIRect Row = Rows.Next(TinyTeeModeHeight);
		if(RenderTinyTeeMode)
		{
			int Value = g_Config.m_QmTinyTees ? (g_Config.m_QmTinyTeesOthers ? 2 : 1) : 0;
			CPerfTimer TinyTeeModeTimer;
			if(DoSettingsLine_RadioMenu(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, Row, "tclient-smaller-tees-label", Localize("Smaller tees"), s_vTinyTeeModeButtons, {"tclient-smaller-tees-none", "tclient-smaller-tees-self", "tclient-smaller-tees-all"}, {Localize("None"), Localize("Self"), Localize("All")}, {0, 1, 2}, Value, ContentMetrics))
			{
				g_Config.m_QmTinyTees = Value > 0 ? 1 : 0;
				g_Config.m_QmTinyTeesOthers = Value > 1 ? 1 : 0;
			}
			LogSettingsStage("tclient_settings_left_visual_tiny_tee_mode", TinyTeeModeTimer);
		}
		if(g_Config.m_QmTinyTees > 0)
		{
			const bool RenderTinyTeeSize = ShouldRenderVisualBlock(LineSize);
			TinyTeeConfig = Rows.Next();
			if(RenderTinyTeeSize)
			{
				CPerfTimer TinyTeeSizeTimer;
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-tiny-tee-size", &g_Config.m_QmTinyTeeSize, &g_Config.m_QmTinyTeeSize, &TinyTeeConfig, Localize("Tiny Tee Size"), 85, 115);
				LogSettingsStage("tclient_settings_left_visual_tiny_tee_size", TinyTeeSizeTimer);
			}
		}

		const bool RenderJellyToggle = ShouldRenderVisualBlock(LineSize);
		Row = Rows.Next();
		if(RenderJellyToggle)
		{
			CPerfTimer MainControlsTimer;
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmJellyTee, "tclient-enable-jelly-tee", Localize("Enable Jelly Tee"), &g_Config.m_QmJellyTee, &Row, LineSize);
			LogSettingsStage("tclient_settings_left_visual_main_controls", MainControlsTimer);
		}
		if(g_Config.m_QmJellyTee)
		{
			const bool RenderJellyRows = ShouldRenderVisualBlock(TClientSettingsRowsHeight(3));
			CUIRect JellyOthersRow = Rows.Next();
			CUIRect JellyStrengthRow = Rows.Next();
			CUIRect JellyDurationRow = Rows.Next();
			if(RenderJellyRows)
			{
				CPerfTimer JellyTimer;
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmJellyTeeOthers, "tclient-jelly-others", Localize("Jelly others"), &g_Config.m_QmJellyTeeOthers, &JellyOthersRow, LineSize);
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-jelly-strength", &g_Config.m_QmJellyTeeStrength, &g_Config.m_QmJellyTeeStrength, &JellyStrengthRow, Localize("Jelly strength"), 0, 1000);
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-jelly-duration", &g_Config.m_QmJellyTeeDuration, &g_Config.m_QmJellyTeeDuration, &JellyDurationRow, Localize("Jelly duration"), 1, 500);
				LogSettingsStage("tclient_settings_left_visual_jelly", JellyTimer);
			}
		}
		const float FakeFlagsHeight = ResolveSettingsRadioRowLayout(CurrentColumn, 3, ContentMetrics).m_Height;
		const bool RenderFakeFlags = ShouldRenderVisualBlock(FakeFlagsHeight + MarginSmall + LineSize);
		CUIRect FakeFlagsRow = Rows.Next(FakeFlagsHeight);
		CUIRect MovingTilesRow = Rows.Next();
		if(RenderFakeFlags)
		{
			static std::vector<CButtonContainer> s_vButtonContainers = {{}, {}, {}};
			int Value = g_Config.m_QmFakeCtfFlags;
			CPerfTimer FakeFlagsTimer;
			if(DoSettingsLine_RadioMenu(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, FakeFlagsRow, "tclient-fake-ctf-flags-label", Localize("Fake CTF flags"), s_vButtonContainers, {"tclient-fake-ctf-flags-none", "tclient-fake-ctf-flags-red", "tclient-fake-ctf-flags-blue"}, {Localize("None"), Localize("Red"), Localize("Blue")}, {0, 1, 2}, Value, ContentMetrics))
				g_Config.m_QmFakeCtfFlags = Value;
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmMovingTilesEntities, "tclient-moving-tiles-entities", Localize("Show moving tiles in entities"), &g_Config.m_QmMovingTilesEntities, &MovingTilesRow, LineSize);
			LogSettingsStage("tclient_settings_left_visual_fake_flags", FakeFlagsTimer);
		}
		BoxRect.h = CurrentColumn.y - BoxRect.y;
		return BoxRect;
	};
	CUIRect Measured = Content;
	const CUIRect Box = LayoutVisualEffectsSection(Measured, false);
	const float Header = Box.y - Content.y + HeadlineHeight + MarginSmall;
	const float Height = Measured.y - Content.y - Header;
	if(Render)
	{
		CUIRect LegacyContent = Content;
		LegacyContent.y -= Header;
		LegacyContent.h += Header;
		const CUIRect Clip = ResolveSettingsFocusSafeClipRect(Content, UiScale);
		Ui()->ClipEnable(&Clip);
		LayoutVisualEffectsSection(LegacyContent, true);
		Ui()->ClipDisable();
		Content.y = LegacyContent.y;
	}
	return Height;
}

float CMenus::LayoutTClientPlayerIndicatorCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
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
	auto LayoutPlayerIndicatorSection = [&](CUIRect &CurrentColumn, bool Render) {
		CUIRect BoxRect;
		CUIRect TmpRect;
		CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
		BoxRect = CurrentColumn;
		CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
		if(Render)
		{
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-player-indicator-title", &Label, Localize("Player indicator"), HeadlineFontSize, TEXTALIGN_ML);
		}
		CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
		CTClientSettingsRowAllocator Rows(CurrentColumn);
		const bool RenderBaseRows = Render && ShouldRenderSection(CurrentColumn, 0.0f, TClientSettingsRowsHeight(6));
		CUIRect EnabledRow = Rows.Next();
		CUIRect HideVisibleRow = Rows.Next();
		CUIRect FreezeOnlyRow = Rows.Next();
		CUIRect TeamOnlyRow = Rows.Next();
		CUIRect TeesRow = Rows.Next();
		CUIRect WarListRow = Rows.Next();
		if(RenderBaseRows)
		{
			CPerfTimer BaseTimer;
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmPlayerIndicator, "tclient-player-indicator-enabled", Localize("Show any enabled Indicators"), &g_Config.m_QmPlayerIndicator, &EnabledRow, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmIndicatorHideVisible, "tclient-indicator-hide-visible", Localize("Hide indicator for tees on your screen"), &g_Config.m_QmIndicatorHideVisible, &HideVisibleRow, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmPlayerIndicatorFreeze, "tclient-player-indicator-freeze-only", Localize("Show only freeze Players"), &g_Config.m_QmPlayerIndicatorFreeze, &FreezeOnlyRow, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmIndicatorTeamOnly, "tclient-indicator-team-only", Localize("Only show after joining a team"), &g_Config.m_QmIndicatorTeamOnly, &TeamOnlyRow, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmIndicatorTees, "tclient-indicator-tees", Localize("Render tiny tees instead of circles"), &g_Config.m_QmIndicatorTees, &TeesRow, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmWarListIndicator, "tclient-warlist-indicator", Localize("Use warlist groups for indicator"), &g_Config.m_QmWarListIndicator, &WarListRow, LineSize);
			LogSettingsStage("tclient_settings_left_player_indicator_base", BaseTimer);
		}

		const int DistanceRowCount = 3 + (g_Config.m_QmIndicatorVariableDistance ? 3 : 1);
		const bool RenderDistanceRows = Render && ShouldRenderSection(CurrentColumn, 0.0f, TClientSettingsRowsHeight(DistanceRowCount));
		CUIRect RadiusRow = Rows.Next();
		CUIRect OpacityRow = Rows.Next();
		CUIRect VariableDistanceRow = Rows.Next();
		CUIRect OffsetRow = Rows.Next();
		CUIRect MaxOffsetRow;
		CUIRect MaxDistanceRow;
		if(g_Config.m_QmIndicatorVariableDistance)
		{
			MaxOffsetRow = Rows.Next();
			MaxDistanceRow = Rows.Next();
		}
		if(RenderDistanceRows)
		{
			CPerfTimer DistanceTimer;
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-radius", &g_Config.m_QmIndicatorRadius, &g_Config.m_QmIndicatorRadius, &RadiusRow, Localize("Indicator size"), 1, 16);
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-opacity", &g_Config.m_QmIndicatorOpacity, &g_Config.m_QmIndicatorOpacity, &OpacityRow, Localize("Indicator opacity"), 0, 100);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmIndicatorVariableDistance, "tclient-indicator-variable-distance", Localize("Change indicator offset based on distance to other tees"), &g_Config.m_QmIndicatorVariableDistance, &VariableDistanceRow, LineSize);
			if(g_Config.m_QmIndicatorVariableDistance)
			{
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-offset", &g_Config.m_QmIndicatorOffset, &g_Config.m_QmIndicatorOffset, &OffsetRow, Localize("Indicator min offset"), 16, 200);
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-offset-max", &g_Config.m_QmIndicatorOffsetMax, &g_Config.m_QmIndicatorOffsetMax, &MaxOffsetRow, Localize("Indicator max offset"), 16, 200);
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-max-distance", &g_Config.m_QmIndicatorMaxDistance, &g_Config.m_QmIndicatorMaxDistance, &MaxDistanceRow, Localize("Indicator max distance"), 500, 7000);
			}
			else
			{
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-offset", &g_Config.m_QmIndicatorOffset, &g_Config.m_QmIndicatorOffset, &OffsetRow, Localize("Indicator offset"), 16, 200);
			}
			LogSettingsStage("tclient_settings_left_player_indicator_distance", DistanceTimer);
		}

		const bool ShowWarListIndicatorOptions = g_Config.m_QmWarListIndicator;
		if(ShowWarListIndicatorOptions)
		{
			const bool RenderWarListRows = Render && ShouldRenderSection(CurrentColumn, 0.0f, TClientSettingsRowsHeight(4));
			CUIRect ColorsRow = Rows.Next();
			CUIRect AllRow = Rows.Next();
			CUIRect EnemyRow = Rows.Next();
			CUIRect TeamRow = Rows.Next();
			if(RenderWarListRows)
			{
				CPerfTimer WarListTimer;
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmWarListIndicatorColors, "tclient-warlist-indicator-colors", Localize("Use warlist colors instead of regular colors"), &g_Config.m_QmWarListIndicatorColors, &ColorsRow, LineSize);
				char aBuf[128];
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmWarListIndicatorAll, "tclient-warlist-indicator-all", Localize("Show all warlist groups"), &g_Config.m_QmWarListIndicatorAll, &AllRow, LineSize);
				str_format(aBuf, sizeof(aBuf), Localize("Show %s group"), GameClient()->m_WarList.m_WarTypes.at(1)->m_aWarName);
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmWarListIndicatorEnemy, "tclient-warlist-indicator-enemy", aBuf, &g_Config.m_QmWarListIndicatorEnemy, &EnemyRow, LineSize);
				str_format(aBuf, sizeof(aBuf), Localize("Show %s group"), GameClient()->m_WarList.m_WarTypes.at(2)->m_aWarName);
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmWarListIndicatorTeam, "tclient-warlist-indicator-team", aBuf, &g_Config.m_QmWarListIndicatorTeam, &TeamRow, LineSize);
				LogSettingsStage("tclient_settings_left_player_indicator_warlist", WarListTimer);
			}
		}

		const bool ShowIndicatorColorOptions = !g_Config.m_QmWarListIndicatorColors || !g_Config.m_QmWarListIndicator;
		if(ShowIndicatorColorOptions)
		{
			const bool RenderColorRows = Render && ShouldRenderSection(CurrentColumn, 0.0f, TClientSettingsRowsHeight(3));
			CUIRect AliveColorRow = Rows.Next();
			CUIRect FreezeColorRow = Rows.Next();
			CUIRect SavedColorRow = Rows.Next();
			if(RenderColorRows)
			{
				CPerfTimer ColorsTimer;
				static CButtonContainer s_IndicatorAliveColorId, s_IndicatorDeadColorId, s_IndicatorSavedColorId;
				DoLine_ColorPicker(&s_IndicatorAliveColorId, CurrentSettingsContentMetrics(), &AliveColorRow, Localize("Indicator alive color"), &g_Config.m_QmIndicatorAlive, ColorRGBA(0.0f, 0.0f, 0.0f), false);
				DoLine_ColorPicker(&s_IndicatorDeadColorId, CurrentSettingsContentMetrics(), &FreezeColorRow, Localize("Indicator in freeze color"), &g_Config.m_QmIndicatorFreeze, ColorRGBA(0.0f, 0.0f, 0.0f), false);
				DoLine_ColorPicker(&s_IndicatorSavedColorId, CurrentSettingsContentMetrics(), &SavedColorRow, Localize("Indicator safe color"), &g_Config.m_QmIndicatorSaved, ColorRGBA(0.0f, 0.0f, 0.0f), false);
				LogSettingsStage("tclient_settings_left_player_indicator_colors", ColorsTimer);
			}
		}

		BoxRect.h = CurrentColumn.y - BoxRect.y;
		return BoxRect;
	};
	CUIRect Measured = Content;
	const CUIRect Box = LayoutPlayerIndicatorSection(Measured, false);
	const float Header = Box.y - Content.y + HeadlineHeight + MarginSmall;
	const float Height = Measured.y - Content.y - Header;
	if(Render)
	{
		CUIRect LegacyContent = Content;
		LegacyContent.y -= Header;
		LegacyContent.h += Header;
		const CUIRect Clip = ResolveSettingsFocusSafeClipRect(Content, UiScale);
		Ui()->ClipEnable(&Clip);
		LayoutPlayerIndicatorSection(LegacyContent, true);
		Ui()->ClipDisable();
		Content.y = LegacyContent.y;
	}
	return Height;
}

float CMenus::LayoutTClientTeeStatusBarCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
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
	auto LayoutTeeStatusBarSection = [&](CUIRect &CurrentColumn, bool Render) {
		CUIRect BoxRect;
		CUIRect TmpRect;
		CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
		BoxRect = CurrentColumn;
		CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
		if(Render)
		{
			CUIElement &TeeStatusBarTitle = SettingsTextElement(SETTINGS_TCLIENT, m_TClientSettingsTab, "tclient-tee-status-bar-title");
			DoSettingsLabelStreamed(TeeStatusBarTitle, &Label, Localize("Tee status bar"), HeadlineFontSize, TEXTALIGN_ML, TClientFixedLabelProperties(HeadlineFontSize, Label.w));
		}
		CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
		CTClientSettingsRowAllocator Rows(CurrentColumn);
		CUIRect Row = Rows.Next();
		if(Render)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmShowFrozenHud, "tclient-show-frozen-hud", Localize("Show tee status bar"), &g_Config.m_QmShowFrozenHud, &Row, LineSize);
		Row = Rows.Next();
		if(Render)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmShowFrozenHudSkins, "tclient-frozen-hud-skins", Localize("Use custom skins instead of the ninja tee"), &g_Config.m_QmShowFrozenHudSkins, &Row, LineSize);
		Row = Rows.Next();
		if(Render)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmFrozenHudTeamOnly, "tclient-frozen-hud-team-only", Localize("Only show after joining a team"), &g_Config.m_QmFrozenHudTeamOnly, &Row, LineSize);
		Button = Rows.Next();
		if(Render)
		{
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-frozen-max-rows", &g_Config.m_QmFrozenMaxRows, &g_Config.m_QmFrozenMaxRows, &Button, Localize("Maximum rows"), 1, 6);
		}
		Button = Rows.Next();
		if(Render)
		{
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-frozen-hud-tee-size", &g_Config.m_QmFrozenHudTeeSize, &g_Config.m_QmFrozenHudTeeSize, &Button, Localize("Tee size"), 8, 27);
		}
		CUIRect CheckBoxRect = Rows.Next();
		if(Render)
		{
			if(DoTClientSettingsButton_CheckBox(&g_Config.m_QmShowFrozenText, "tclient-show-frozen-text", Localize("Show the number of tees still alive"), g_Config.m_QmShowFrozenText >= 1, &CheckBoxRect))
				g_Config.m_QmShowFrozenText = g_Config.m_QmShowFrozenText >= 1 ? 0 : 1;
		}
		if(g_Config.m_QmShowFrozenText)
		{
			CUIRect CheckBoxRect2 = Rows.Next();
			if(Render)
			{
				if(DoTClientSettingsButton_CheckBox(&s_CountFrozenText, "tclient-show-frozen-count-text", Localize("Show the number of frozen tees"), g_Config.m_QmShowFrozenText == 2, &CheckBoxRect2))
					g_Config.m_QmShowFrozenText = g_Config.m_QmShowFrozenText != 2 ? 2 : 1;
			}
		}
		BoxRect.h = CurrentColumn.y - BoxRect.y;
		return BoxRect;
	};
	CUIRect Measured = Content;
	const CUIRect Box = LayoutTeeStatusBarSection(Measured, false);
	const float Header = Box.y - Content.y + HeadlineHeight + MarginSmall;
	const float Height = Measured.y - Content.y - Header;
	if(Render)
	{
		CUIRect LegacyContent = Content;
		LegacyContent.y -= Header;
		LegacyContent.h += Header;
		const CUIRect Clip = ResolveSettingsFocusSafeClipRect(Content, UiScale);
		Ui()->ClipEnable(&Clip);
		LayoutTeeStatusBarSection(LegacyContent, true);
		Ui()->ClipDisable();
		Content.y = LegacyContent.y;
	}
	return Height;
}

float CMenus::LayoutTClientTileOutlinesCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
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
	auto LayoutTileOutlinesSection = [&](CUIRect &CurrentColumn, bool Render) {
		CUIRect BoxRect;
		CUIRect TmpRect;
		auto ShouldRenderTileOutlineBlock = [&](float Height) {
			return Render && ShouldRenderSection(CurrentColumn, 0.0f, Height);
		};
		CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
		BoxRect = CurrentColumn;
		CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
		if(Render)
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Tile outlines"), HeadlineFontSize, TEXTALIGN_ML);
		CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
		CTClientSettingsRowAllocator Rows(CurrentColumn);
		const bool RenderBaseRows = ShouldRenderTileOutlineBlock(TClientSettingsRowsHeight(4));
		CUIRect EnabledRow = Rows.Next();
		CUIRect EntitiesRow = Rows.Next();
		CUIRect OpacityRow = Rows.Next();
		CUIRect SolidOpacityRow = Rows.Next();
		if(RenderBaseRows)
		{
			CPerfTimer TileOutlinesBaseTimer;
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmOutline, "tclient-outline-enabled", Localize("Show all enabled outlines"), &g_Config.m_QmOutline, &EnabledRow, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmOutlineEntities, "tclient-outline-entities", Localize("Only show outlines in the entities layer"), &g_Config.m_QmOutlineEntities, &EntitiesRow, LineSize);
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-outline-opacity", &g_Config.m_QmOutlineAlpha, &g_Config.m_QmOutlineAlpha, &OpacityRow, Localize("Outline opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-outline-solid-opacity", &g_Config.m_QmOutlineSolidAlpha, &g_Config.m_QmOutlineSolidAlpha, &SolidOpacityRow, Localize("Solid tile outline opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
			LogSettingsStage("tclient_settings_right_tile_outlines_base", TileOutlinesBaseTimer);
		}

		auto DoOutlineType = [&](const char *pStage, CButtonContainer &ButtonContainer, const char *pName, int &Enable, int &Width, unsigned int &Color, const unsigned int &ColorDefault) {
			const bool RenderRows = ShouldRenderTileOutlineBlock(TClientSettingsRowsHeight(2));
			CUIRect ColorRow = Rows.Next();
			CUIRect WidthRow = Rows.Next();
			if(RenderRows)
			{
				CPerfTimer OutlineTypeTimer;
				DoLine_ColorPicker(&ButtonContainer, CurrentSettingsContentMetrics(), &ColorRow, pName, &Color, ColorDefault, true, &Enable, true);
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-outline-width", &Width, &Width, &WidthRow, Localize("Width", "Outlines"), 1, 16);
				LogSettingsStage(pStage, OutlineTypeTimer);
			}
		};

		static CButtonContainer s_aOutlineButtonContainers[5];
		static CButtonContainer s_OutlineDeepFreezeColorId;
		static CButtonContainer s_OutlineDeepUnfreezeColorId;
		DoOutlineType("tclient_settings_right_tile_outlines_solid", s_aOutlineButtonContainers[0], Localize("Solid"), g_Config.m_QmOutlineSolid, g_Config.m_QmOutlineWidthSolid, g_Config.m_QmOutlineColorSolid, DefaultConfig::QmOutlineColorSolid);
		DoOutlineType("tclient_settings_right_tile_outlines_freeze", s_aOutlineButtonContainers[1], Localize("Freeze"), g_Config.m_QmOutlineFreeze, g_Config.m_QmOutlineWidthFreeze, g_Config.m_QmOutlineColorFreeze, DefaultConfig::QmOutlineColorFreeze);
		{
			const bool RenderColorRow = ShouldRenderTileOutlineBlock(LineSize);
			CUIRect ColorRow = Rows.Next();
			if(RenderColorRow)
			{
				CPerfTimer DeepFreezeTimer;
				DoLine_ColorPicker(&s_OutlineDeepFreezeColorId, CurrentSettingsContentMetrics(), &ColorRow, Localize("Deep freeze color"), &g_Config.m_QmOutlineColorDeepFreeze, DefaultConfig::QmOutlineColorDeepFreeze, false, nullptr, true);
				LogSettingsStage("tclient_settings_right_tile_outlines_deepfreeze_color", DeepFreezeTimer);
			}
		}
		DoOutlineType("tclient_settings_right_tile_outlines_unfreeze", s_aOutlineButtonContainers[2], Localize("Unfreeze"), g_Config.m_QmOutlineUnfreeze, g_Config.m_QmOutlineWidthUnfreeze, g_Config.m_QmOutlineColorUnfreeze, DefaultConfig::QmOutlineColorUnfreeze);
		{
			const bool RenderColorRow = ShouldRenderTileOutlineBlock(LineSize);
			CUIRect ColorRow = Rows.Next();
			if(RenderColorRow)
			{
				CPerfTimer DeepUnfreezeTimer;
				DoLine_ColorPicker(&s_OutlineDeepUnfreezeColorId, CurrentSettingsContentMetrics(), &ColorRow, Localize("Deep unfreeze color"), &g_Config.m_QmOutlineColorDeepUnfreeze, DefaultConfig::QmOutlineColorDeepUnfreeze, false, nullptr, true);
				LogSettingsStage("tclient_settings_right_tile_outlines_deepunfreeze_color", DeepUnfreezeTimer);
			}
		}
		DoOutlineType("tclient_settings_right_tile_outlines_kill", s_aOutlineButtonContainers[3], Localize("Kill"), g_Config.m_QmOutlineKill, g_Config.m_QmOutlineWidthKill, g_Config.m_QmOutlineColorKill, DefaultConfig::QmOutlineColorKill);
		DoOutlineType("tclient_settings_right_tile_outlines_tele", s_aOutlineButtonContainers[4], Localize("Tele"), g_Config.m_QmOutlineTele, g_Config.m_QmOutlineWidthTele, g_Config.m_QmOutlineColorTele, DefaultConfig::QmOutlineColorTele);
		BoxRect.h = CurrentColumn.y - BoxRect.y;
		return BoxRect;
	};
	CUIRect Measured = Content;
	const CUIRect Box = LayoutTileOutlinesSection(Measured, false);
	const float Header = Box.y - Content.y + HeadlineHeight + MarginSmall;
	const float Height = Measured.y - Content.y - Header;
	if(Render)
	{
		CUIRect LegacyContent = Content;
		LegacyContent.y -= Header;
		LegacyContent.h += Header;
		const CUIRect Clip = ResolveSettingsFocusSafeClipRect(Content, UiScale);
		Ui()->ClipEnable(&Clip);
		LayoutTileOutlinesSection(LegacyContent, true);
		Ui()->ClipDisable();
		Content.y = LegacyContent.y;
	}
	return Height;
}

float CMenus::LayoutTClientGhostToolsCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
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
	auto LayoutGhostToolsSection = [&](CUIRect &CurrentColumn, bool Render) {
		CUIRect BoxRect;
		CUIRect TmpRect;
		CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
		BoxRect = CurrentColumn;
		CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
		if(Render)
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Ghost tools"), HeadlineFontSize, TEXTALIGN_ML);
		CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
		CTClientSettingsRowAllocator Rows(CurrentColumn);
		CUIRect Row = Rows.Next();
		if(Render)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmShowOthersGhosts, "tclient-show-others-ghosts", Localize("Show unpredicted ghosts for other players"), &g_Config.m_QmShowOthersGhosts, &Row, LineSize);
		Row = Rows.Next();
		if(Render)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmSwapGhosts, "tclient-swap-ghosts", Localize("Swap ghosts with regular players"), &g_Config.m_QmSwapGhosts, &Row, LineSize);
		Button = Rows.Next();
		if(Render)
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-predicted-ghost-opacity", &g_Config.m_QmPredGhostsAlpha, &g_Config.m_QmPredGhostsAlpha, &Button, Localize("Predicted ghost opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		Button = Rows.Next();
		if(Render)
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-unpredicted-ghost-opacity", &g_Config.m_QmUnpredGhostsAlpha, &g_Config.m_QmUnpredGhostsAlpha, &Button, Localize("Unpredicted ghost opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		Row = Rows.Next();
		if(Render)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmHideFrozenGhosts, "tclient-hide-frozen-ghosts", Localize("Hide ghosts of frozen players"), &g_Config.m_QmHideFrozenGhosts, &Row, LineSize);
		Row = Rows.Next();
		if(Render)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRenderGhostAsCircle, "tclient-render-ghost-as-circle", Localize("Render ghosts as circles"), &g_Config.m_QmRenderGhostAsCircle, &Row, LineSize);
		Row = Rows.Next();
		if(Render)
		{
			static CButtonContainer s_ReaderButtonGhost, s_ClearButtonGhost;
			DoLine_KeyReader(Row, s_ReaderButtonGhost, s_ClearButtonGhost, Localize("Toggle ghost key"), "toggle qm_show_others_ghosts 0 1");
		}
		BoxRect.h = CurrentColumn.y - BoxRect.y;
		return BoxRect;
	};
	CUIRect Measured = Content;
	const CUIRect Box = LayoutGhostToolsSection(Measured, false);
	const float Header = Box.y - Content.y + HeadlineHeight + MarginSmall;
	const float Height = Measured.y - Content.y - Header;
	if(Render)
	{
		CUIRect LegacyContent = Content;
		LegacyContent.y -= Header;
		LegacyContent.h += Header;
		const CUIRect Clip = ResolveSettingsFocusSafeClipRect(Content, UiScale);
		Ui()->ClipEnable(&Clip);
		LayoutGhostToolsSection(LegacyContent, true);
		Ui()->ClipDisable();
		Content.y = LegacyContent.y;
	}
	return Height;
}

float CMenus::LayoutTClientRainbowCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
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
	auto LayoutRainbowSection = [&](CUIRect &CurrentColumn, bool Render) {
		CUIRect BoxRect;
		CUIRect TmpRect;
		CUIRect RainbowDropDownRect;
		CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
		BoxRect = CurrentColumn;
		CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
		if(Render)
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Rainbow"), HeadlineFontSize, TEXTALIGN_ML);
		CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
		CTClientSettingsRowAllocator Rows(CurrentColumn);
		CUIRect Row = Rows.Next();
		if(Render)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRainbowTees, "tclient-rainbow-tees", Localize("Rainbow Tees"), &g_Config.m_QmRainbowTees, &Row, LineSize);
		Row = Rows.Next();
		if(Render)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRainbowWeapon, "tclient-rainbow-weapons", Localize("Rainbow weapons"), &g_Config.m_QmRainbowWeapon, &Row, LineSize);
		Row = Rows.Next();
		if(Render)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRainbowHook, "tclient-rainbow-hook", Localize("Rainbow hook"), &g_Config.m_QmRainbowHook, &Row, LineSize);
		Row = Rows.Next();
		if(Render)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRainbowOthers, "tclient-rainbow-others", Localize("Rainbow others"), &g_Config.m_QmRainbowOthers, &Row, LineSize);
		static std::vector<const char *> s_RainbowDropDownNames;
		s_RainbowDropDownNames = {Localize("Rainbow"), Localize("Pulse"), Localize("Black"), Localize("Random")};
		static CUi::SDropDownState s_RainbowDropDownState;
		static CScrollRegion s_RainbowDropDownScrollRegion;
		s_RainbowDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_RainbowDropDownScrollRegion;
		int RainbowSelectedOld = g_Config.m_QmRainbowMode - 1;
		RainbowDropDownRect = Rows.Next();
		if(Render)
		{
			const int RainbowSelectedNew = DoSettingsDropDown(&RainbowDropDownRect, RainbowSelectedOld, s_RainbowDropDownNames.data(), s_RainbowDropDownNames.size(), s_RainbowDropDownState);
			if(RainbowSelectedOld != RainbowSelectedNew)
				g_Config.m_QmRainbowMode = RainbowSelectedNew + 1;
		}
		Button = Rows.Next();
		if(Render)
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-rainbow-speed", &g_Config.m_QmRainbowSpeed, &g_Config.m_QmRainbowSpeed, &Button, Localize("Rainbow speed"), 0, 5000, &CUi::ms_LogarithmicScrollbarScale, 0, "%");
		BoxRect.h = CurrentColumn.y - BoxRect.y;
		return BoxRect;
	};
	CUIRect Measured = Content;
	const CUIRect Box = LayoutRainbowSection(Measured, false);
	const float Header = Box.y - Content.y + HeadlineHeight + MarginSmall;
	const float Height = Measured.y - Content.y - Header;
	if(Render)
	{
		CUIRect LegacyContent = Content;
		LegacyContent.y -= Header;
		LegacyContent.h += Header;
		const CUIRect Clip = ResolveSettingsFocusSafeClipRect(Content, UiScale);
		Ui()->ClipEnable(&Clip);
		LayoutRainbowSection(LegacyContent, true);
		Ui()->ClipDisable();
		Content.y = LegacyContent.y;
	}
	return Height;
}

float CMenus::LayoutTClientTeeTrailsCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
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
	auto LayoutTeeTrailsSection = [&](CUIRect &CurrentColumn, bool Render) {
		CUIRect BoxRect;
		CUIRect TmpRect;
		CUIRect TrailDropDownRect;
		CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
		BoxRect = CurrentColumn;
		CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
		if(Render)
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Tee Trails"), HeadlineFontSize, TEXTALIGN_ML);
		CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
		CTClientSettingsRowAllocator Rows(CurrentColumn);
		CUIRect TrailEnabledRow = Rows.Next();
		CUIRect TrailOthersRow = Rows.Next();
		CUIRect TrailFadeRow = Rows.Next();
		CUIRect TrailTaperRow = Rows.Next();
		CUIRect TrailStyleColorsRow = Rows.Next();
		if(Render)
		{
			CPerfTimer BaseTimer;
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmTeeTrail, "tclient-tee-trail-enabled", Localize("Enable tee trails"), &g_Config.m_QmTeeTrail, &TrailEnabledRow, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmTeeTrailOthers, "tclient-tee-trail-others", Localize("Show other tees' trails"), &g_Config.m_QmTeeTrailOthers, &TrailOthersRow, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmTeeTrailFade, "tclient-tee-trail-fade", Localize("Fade trail alpha"), &g_Config.m_QmTeeTrailFade, &TrailFadeRow, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmTeeTrailTaper, "tclient-tee-trail-taper", Localize("Taper trail width"), &g_Config.m_QmTeeTrailTaper, &TrailTaperRow, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmTeeTrailStyleColors, "tclient-tee-trail-style-colors", Localize("Use style colors"), &g_Config.m_QmTeeTrailStyleColors, &TrailStyleColorsRow, LineSize);
			LogSettingsStage("tclient_settings_right_tee_trails_base", BaseTimer);
		}
		static std::vector<const char *> s_TrailDropDownNames;
		s_TrailDropDownNames = {Localize("Solid"), Localize("Tee"), Localize("Rainbow"), Localize("Speed")};
		s_TrailDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_TrailDropDownScrollRegion;
		int TrailSelectedOld = g_Config.m_QmTeeTrailColorMode - 1;
		TrailDropDownRect = Rows.Next();
		if(Render)
		{
			CPerfTimer DropDownTimer;
			const int TrailSelectedNew = DoSettingsDropDown(&TrailDropDownRect, TrailSelectedOld, s_TrailDropDownNames.data(), s_TrailDropDownNames.size(), s_TrailDropDownState);
			if(TrailSelectedOld != TrailSelectedNew)
				g_Config.m_QmTeeTrailColorMode = TrailSelectedNew + 1;
			LogSettingsStage("tclient_settings_right_tee_trails_dropdown", DropDownTimer);
		}
		static std::vector<const char *> s_TrailStyleNames;
		s_TrailStyleNames = {Localize("Original"), Localize("Manga: Ink Slash"), Localize("Magic: Spirit Script"), Localize("Pixel: Layer Shift")};
		s_TrailStyleDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_TrailStyleDropDownScrollRegion;
		CUIRect TrailStyleDropDownRect = Rows.Next();
		if(Render)
		{
			const int TrailStyleOld = qm_tee_trail::ResolveStyle(g_Config.m_QmTeeTrailStyle);
			const int TrailStyleNew = DoSettingsDropDown(&TrailStyleDropDownRect, TrailStyleOld, s_TrailStyleNames.data(), s_TrailStyleNames.size(), s_TrailStyleDropDownState);
			if(TrailStyleNew != TrailStyleOld)
				g_Config.m_QmTeeTrailStyle = TrailStyleNew;
		}
		if(g_Config.m_QmTeeTrailColorMode == CTrails::COLORMODE_SOLID)
		{
			CUIRect ColorRow = Rows.Next();
			if(Render)
			{
				CPerfTimer ColorTimer;
				static CButtonContainer s_TeeTrailColor;
				DoLine_ColorPicker(&s_TeeTrailColor, CurrentSettingsContentMetrics(), &ColorRow, Localize("Tee trail color"), &g_Config.m_QmTeeTrailColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
				LogSettingsStage("tclient_settings_right_tee_trails_color", ColorTimer);
			}
		}
		CUIRect WidthRow = Rows.Next();
		CUIRect LengthRow = Rows.Next();
		CUIRect AlphaRow = Rows.Next();
		if(Render)
		{
			CPerfTimer SlidersTimer;
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-tee-trail-width", &g_Config.m_QmTeeTrailWidth, &g_Config.m_QmTeeTrailWidth, &WidthRow, Localize("Trail width"), 0, 20);
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-tee-trail-length", &g_Config.m_QmTeeTrailLength, &g_Config.m_QmTeeTrailLength, &LengthRow, Localize("Trail length"), 0, 200);
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-tee-trail-alpha", &g_Config.m_QmTeeTrailAlpha, &g_Config.m_QmTeeTrailAlpha, &AlphaRow, Localize("Trail alpha"), 0, 100);
			LogSettingsStage("tclient_settings_right_tee_trails_sliders", SlidersTimer);
		}
		BoxRect.h = CurrentColumn.y - BoxRect.y;
		return BoxRect;
	};
	CUIRect Measured = Content;
	const CUIRect Box = LayoutTeeTrailsSection(Measured, false);
	const float Header = Box.y - Content.y + HeadlineHeight + MarginSmall;
	const float Height = Measured.y - Content.y - Header;
	if(Render)
	{
		CUIRect LegacyContent = Content;
		LegacyContent.y -= Header;
		LegacyContent.h += Header;
		const CUIRect Clip = ResolveSettingsFocusSafeClipRect(Content, UiScale);
		Ui()->ClipEnable(&Clip);
		LayoutTeeTrailsSection(LegacyContent, true);
		Ui()->ClipDisable();
		Content.y = LegacyContent.y;
	}
	return Height;
}

float CMenus::LayoutTClientBackgroundDrawCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
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
	auto LayoutBackgroundDrawSection = [&](CUIRect &CurrentColumn, bool Render) {
		CUIRect BoxRect;
		CUIRect TmpRect;
		CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
		BoxRect = CurrentColumn;
		CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
		if(Render)
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Background Draw"), HeadlineFontSize, TEXTALIGN_ML);
		CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
		CTClientSettingsRowAllocator Rows(CurrentColumn);
		CUIRect ColorRow = Rows.Next();
		static CButtonContainer s_BgDrawColor;
		if(Render)
			DoLine_ColorPicker(&s_BgDrawColor, CurrentSettingsContentMetrics(), &ColorRow, Localize("Color"), &g_Config.m_QmBgDrawColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);

		Button = Rows.Next();
		if(Render)
		{
			if(g_Config.m_QmBgDrawFadeTime == 0)
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-bg-draw-fade-time", &g_Config.m_QmBgDrawFadeTime, &g_Config.m_QmBgDrawFadeTime, &Button, Localize("Stroke fade time"), 0, 600, &CUi::ms_LinearScrollbarScale, 0, Localize(" seconds (never)"));
			else
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-bg-draw-fade-time", &g_Config.m_QmBgDrawFadeTime, &g_Config.m_QmBgDrawFadeTime, &Button, Localize("Stroke fade time"), 0, 600, &CUi::ms_LinearScrollbarScale, 0, Localize(" seconds"));
		}
		Button = Rows.Next();
		if(Render)
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-bg-draw-width", &g_Config.m_QmBgDrawWidth, &g_Config.m_QmBgDrawWidth, &Button, Localize("Width"), 1, 50);
		CUIRect KeyRow = Rows.Next();
		if(Render)
		{
			static CButtonContainer s_ReaderButtonDraw, s_ClearButtonDraw;
			DoLine_KeyReader(KeyRow, s_ReaderButtonDraw, s_ClearButtonDraw, Localize("Draw where mouse is"), "+bg_draw");
		}
		BoxRect.h = CurrentColumn.y - BoxRect.y;
		return BoxRect;
	};
	CUIRect Measured = Content;
	const CUIRect Box = LayoutBackgroundDrawSection(Measured, false);
	const float Header = Box.y - Content.y + HeadlineHeight + MarginSmall;
	const float Height = Measured.y - Content.y - Header;
	if(Render)
	{
		CUIRect LegacyContent = Content;
		LegacyContent.y -= Header;
		LegacyContent.h += Header;
		const CUIRect Clip = ResolveSettingsFocusSafeClipRect(Content, UiScale);
		Ui()->ClipEnable(&Clip);
		LayoutBackgroundDrawSection(LegacyContent, true);
		Ui()->ClipDisable();
		Content.y = LegacyContent.y;
	}
	return Height;
}
