#include "QmCardCatalogInternal.h"

#include <engine/shared/config.h>
#include <engine/shared/localization.h>

#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/components/hud_media_island_logic.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/qm_music_hook_registry.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>

// HUD 分类卡片模块（14 张）：卡片的高度测量、重测版本、预布局输入与内容渲染都在这里，
// 页面（栖梦「HUD」页、搜索页）只声明"这一页有这些卡"。
namespace qm_card_catalog
{
	namespace
	{
		using qm_module::EQmModuleId;

		// 开关倒计时显示位置的两个复选框：渲染与卡片预布局必须共用同一组 id，否则同一次点击会被处理两次。
		int s_SwitchCountdownFollowTeeId;
		int s_SwitchCountdownMediaIslandId;

	} // namespace

	const void *SwitchCountdownFollowTeeId()
	{
		return &s_SwitchCountdownFollowTeeId;
	}

	const void *SwitchCountdownMediaIslandId()
	{
		return &s_SwitchCountdownMediaIslandId;
	}

	void QmCardRenderHook::RenderQmHudDynamicIslandContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float LineSpacing, bool OriginalStyle)
	{
		pMenus->RenderQmHudDynamicIslandContent(Content, LineHeight, LineSpacing, OriginalStyle);
	}

	bool BuildHudCard(const SQmCardBuildContext &Ctx, const EQmModuleId Id, SSettingsCardDefinition &Out)
	{
		CMenus *pMenus = Ctx.m_pMenus;
		const SSettingsContentMetrics Metrics = Ctx.m_Metrics;
		const float LineHeight = Metrics.m_LineHeight;
		const float BodySize = Metrics.m_BodySize;
		const float LineSpacing = Metrics.m_LineSpacing;
		const float LabelWidth = Ctx.m_LabelWidth;
		const bool ReadOnly = Ctx.m_ReadOnly;

		const auto Add = [&](const EQmModuleId ModuleId, const char *pStableId, const char *pTitle, const char *pSubtitle, const FSettingsCardRenderMeasured &Render) {
			MakeModuleCard(
				Ctx,
				ModuleId,
				pStableId,
				pTitle,
				pSubtitle,
				Render,
				Out);
			UseDeclaredModuleContent(Ctx, Out);
		};

		switch(Id)
		{
		case EQmModuleId::DummyMiniView:
			Add(Id, "qm:dummy_miniview", "Dummy Window", "Show a small view of the dummy", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudDummyMiniViewContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, g_Config.m_QmDummyMiniView != 0, ReadOnly); });
			return true;
		case EQmModuleId::Coords:
			Add(Id, "qm:coords", "Show Coordinates", "Show coordinates above players", [pMenus, Metrics, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudCoordsContent(pMenus, Content, Metrics, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::PlayerStats:
			Add(Id, "qm:player_stats", "Player data", "Player stats and info display", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudPlayerStatsContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::DebugGraph:
			Add(Id, "qm:debug_graph", "Debug graph", "Debug performance graph panel", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudDebugGraphContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::DebugMode:
			Add(Id, "qm:debug_mode", "Debug mode", "Enable performance debug logging and diagnostics", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudDebugModeContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::InputOverlay:
			Add(Id, "qm:input_overlay", "Input overlay", "Input overlay display", [pMenus, Metrics, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudInputOverlayContent(pMenus, Content, Metrics, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::HudNotifications:
			Add(Id, "qm:hud_notifications", "Notifications", "Show important server prompts and Echo messages as popups", [pMenus, Metrics, LabelWidth, ReadOnly](CUIRect &Content) {
				qm_card_catalog::QmCardRenderHook::RenderQmHudNotificationsBasicContent(pMenus, Content, Metrics, LabelWidth, ReadOnly);
				if(g_Config.m_QmHudNotificationsShowAdvanced)
					qm_card_catalog::QmCardRenderHook::RenderQmHudNotificationsAdvancedContent(pMenus, Content, Metrics, LabelWidth, ReadOnly);
			});
			return true;
		case EQmModuleId::Voice:
			Add(Id, "qm:voice", "Voice", "Voice chat settings and diagnostics", [pMenus, Metrics, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudVoiceContent(pMenus, Content, Metrics, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::DynamicIsland:
			Add(Id, "qm:dynamic_island", "Dynamic Island", "HUD island appearance settings", [pMenus, LineHeight, LineSpacing](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudDynamicIslandContent(pMenus, Content, LineHeight, LineSpacing, g_Config.m_QmHudIslandUseOriginalStyle != 0); });
			return true;
		case EQmModuleId::SystemMediaControls:
			Add(Id, "qm:system_media_controls", "SMTC", "System media control", [pMenus, LineHeight, BodySize, LineSpacing, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudSystemMediaControlsContent(pMenus, Content, LineHeight, BodySize, LineSpacing, ReadOnly); });
			return true;
		case EQmModuleId::Lyrics:
			Add(Id, "qm:lyrics", "Lyrics", "Lyrics sources and display", [pMenus, LineHeight, LineSpacing, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudLyricsContent(pMenus, Content, LineHeight, LineSpacing, ReadOnly); });
			return true;
		case EQmModuleId::Background3D:
			Add(Id, "qm:background_3d", "3D Background", "Configure background 3D particle effects", [pMenus, Metrics, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudBackground3DContent(pMenus, Content, Metrics, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::BindStatusHud:
			Add(Id, "qm:bind_status_hud", "DDRace HUD Pro", "Dummy key/hammer/control/copy status switches", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudBindStatusContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::GoresDrownBoard:
			Add(Id, "qm:gores_drown_board", "Gores drown board", "Team drown death leaderboard", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudGoresDrownBoardContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		default:
			return false;
		}
	}
} // namespace qm_card_catalog

void CMenus::RenderQmHudInputOverlayContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	const auto DeclaredCheckbox = [&](CUIRect &, float Height, float Spacing, const void *pId, const char *pTextId, const char *pText, int *pValue) {
		CUIRect Row;
		const bool Changed = Flow.Control(pTextId, Height, Row) && RenderQmHudCheckbox(Row, Height, 0.0f, pId, pTextId, pText, pValue);
		Flow.Space(Spacing);
		return Changed;
	};
	const float LineHeight = Metrics.m_LineHeight;
	const float BodySize = Metrics.m_BodySize;
	const float LineSpacing = Metrics.m_LineSpacing;
	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmInputOverlay, "Show inputs", Localize("Show inputs"), &g_Config.m_QmInputOverlay);
	if(!g_Config.m_QmInputOverlay)
		return;

	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmInputOverlayKeyCounts, "Show key press counts", Localize("Show key press counts"), &g_Config.m_QmInputOverlayKeyCounts);
	CUIRect ResetRow;
	if(Flow.Control("hud-input-overlay-reset-counts", LineHeight, ResetRow))
	{
		static CButtonContainer s_ResetKeyCounts;
		if(DoButton_Menu(&s_ResetKeyCounts, Localize("Reset key press counts"), 0, &ResetRow) && !PrewarmOnly)
			GameClient()->m_InputOverlay.ResetKeyCounts();
	}
	Flow.Space(LineSpacing);

	if(!qm_card_catalog::QmCardRenderHook::Personalization(this, Content, "qm:input_overlay", Metrics, PrewarmOnly || Ui()->RenderOnly()))
		return;

	CUIRect Row, LabelColumn, ControlColumn;
	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue) {
		if(Flow.Control(pTextId, CurrentSettingsContentMetrics().m_LineHeight, Row))
		{
			SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
			RenderQmHudLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
			RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, "%", PrewarmOnly);
		}
		Flow.Space(LineSpacing);
	};
	static int s_QmInputOverlayScaleInputId;
	static int s_QmInputOverlayMouseScaleInputId;
	static int s_QmInputOverlayOpacityInputId;
	static int s_QmInputOverlayPosXInputId;
	static int s_QmInputOverlayPosYInputId;
	RenderValue("qmclient-input-overlay-keyboard-size", Localizable("Keyboard size"), &s_QmInputOverlayScaleInputId, &g_Config.m_QmInputOverlayScale, 1, 200);
	RenderValue("qmclient-input-overlay-mouse-size", Localizable("Mouse size"), &s_QmInputOverlayMouseScaleInputId, &g_Config.m_QmInputOverlayMouseScale, 1, 200);
	RenderValue("qmclient-input-overlay-opacity", Localizable("Opacity"), &s_QmInputOverlayOpacityInputId, &g_Config.m_QmInputOverlayOpacity, 0, 100);
	RenderValue("qmclient-input-overlay-horizontal-position", Localizable("Horizontal position"), &s_QmInputOverlayPosXInputId, &g_Config.m_QmInputOverlayPosX, 0, 100);
	RenderValue("qmclient-input-overlay-vertical-position", Localizable("Vertical position"), &s_QmInputOverlayPosYInputId, &g_Config.m_QmInputOverlayPosY, 0, 100);
	static int s_CountSize;
	RenderValue("qmclient-input-overlay-count-size", Localizable("Key press count font size"), &s_CountSize, &g_Config.m_QmInputOverlayCountSize, 50, 200);
	static CButtonContainer s_CountColor;
	{
		CUIRect ColorRow;
		if(Flow.Control("hud-QmInputOverlayCountColor", CurrentSettingsContentMetrics().m_ButtonHeight, ColorRow))
		{
			DoLine_ColorPicker(&s_CountColor, Metrics, &ColorRow, Localize("Key press count color"), &g_Config.m_QmInputOverlayCountColor,
				color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmInputOverlayCountColor, true)), false, nullptr, true);
		}
		Flow.Space(CurrentSettingsContentMetrics().m_LineSpacing);
	}
	if(Flow.Control("qmclient-input-overlay-count-label", CurrentSettingsContentMetrics().m_LineHeight, Row))
	{
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		RenderQmHudLabel("qmclient-input-overlay-count-label", &LabelColumn, Localize("Key press count label"), BodySize);
		static CLineInput s_CountLabel(g_Config.m_QmInputOverlayCountLabel, sizeof(g_Config.m_QmInputOverlayCountLabel));
		ui_widget::InputField(SettingsUiContext("settings_input_overlay_count_label", Metrics.m_UiScale), &s_CountLabel, ControlColumn, "", BodySize);
	}
	Flow.Space(LineSpacing);
	if(Flow.Control("hud-input-overlay-reset-appearance", CurrentSettingsContentMetrics().m_LineHeight, Row))
	{
		static CButtonContainer s_CountAppearanceReset;
		if(DoButton_Menu(&s_CountAppearanceReset, Localize("Reset count appearance"), 0, &Row) && !PrewarmOnly && !Ui()->RenderOnly())
		{
			g_Config.m_QmInputOverlayCountColor = DefaultConfig::QmInputOverlayCountColor;
			g_Config.m_QmInputOverlayCountSize = DefaultConfig::QmInputOverlayCountSize;
			str_copy(g_Config.m_QmInputOverlayCountLabel, DefaultConfig::QmInputOverlayCountLabel);
		}
	}
	Flow.Space(LineSpacing);
}

void CMenus::RenderQmHudPlayerStatsContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	const auto DeclaredCheckbox = [&](CUIRect &, float Height, float Spacing, const void *pId, const char *pTextId, const char *pText, int *pValue) {
		CUIRect Row;
		const bool Changed = Flow.Control(pTextId, Height, Row) && RenderQmHudCheckbox(Row, Height, 0.0f, pId, pTextId, pText, pValue);
		Flow.Space(Spacing);
		return Changed;
	};
	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmPlayerStatsHud, "Show player stats HUD", Localize("Show player stats HUD"), &g_Config.m_QmPlayerStatsHud);
	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmPlayerStatsMapProgress, "Map progress bar", Localize("Map progress bar"), &g_Config.m_QmPlayerStatsMapProgress);
	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmPlayerStatsResetOnJoin, "Reset stats when joining a server", Localize("Reset stats when joining a server"), &g_Config.m_QmPlayerStatsResetOnJoin);
	if(g_Config.m_QmPlayerStatsMapProgress)
	{
		DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmPlayerStatsMapProgressStyle, "Use embedded HUD progress bar", Localize("Use embedded HUD progress bar"), &g_Config.m_QmPlayerStatsMapProgressStyle);
		if(!qm_card_catalog::QmCardRenderHook::Personalization(this, Content, "qm:player_stats", CurrentSettingsContentMetrics(), PrewarmOnly || Ui()->RenderOnly()))
			return;

		if(g_Config.m_QmPlayerStatsMapProgressStyle == 0)
		{
			static CButtonContainer s_MapProgressColorId;
			{
				CUIRect ColorRow;
				if(Flow.Control("hud-QmPlayerStatsMapProgressColor", CurrentSettingsContentMetrics().m_ButtonHeight, ColorRow))
				{
					DoLine_ColorPicker(&s_MapProgressColorId, CurrentSettingsContentMetrics(), &ColorRow, Localize("Progress bar color"), &g_Config.m_QmPlayerStatsMapProgressColor, ColorRGBA(36.0f / 255.0f, 199.0f / 255.0f, 100.0f / 255.0f, 1.0f), false, nullptr, true);
				}
				Flow.Space(CurrentSettingsContentMetrics().m_LineSpacing);
			}
			CUIRect Row, LabelColumn, ControlColumn;
			auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
				if(Flow.Control(pTextId, CurrentSettingsContentMetrics().m_LineHeight, Row))
				{
					SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
					RenderQmHudLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
					RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
				}
				Flow.Space(LineSpacing);
			};
			static int s_QmPlayerStatsMapProgressWidthInputId;
			static int s_QmPlayerStatsMapProgressHeightInputId;
			static int s_QmPlayerStatsMapProgressPosXInputId;
			static int s_QmPlayerStatsMapProgressPosYInputId;
			RenderValue("qmclient-player-data-progress-bar-width", Localizable("Progress bar width"), &s_QmPlayerStatsMapProgressWidthInputId, &g_Config.m_QmPlayerStatsMapProgressWidth, 10, 80);
			RenderValue("qmclient-player-data-progress-bar-height", Localizable("Progress bar height"), &s_QmPlayerStatsMapProgressHeightInputId, &g_Config.m_QmPlayerStatsMapProgressHeight, 6, 30);
			RenderValue("qmclient-player-data-horizontal-position", Localizable("Horizontal position"), &s_QmPlayerStatsMapProgressPosXInputId, &g_Config.m_QmPlayerStatsMapProgressPosX, 0, 100, "%");
			RenderValue("qmclient-player-data-vertical-position", Localizable("Vertical position"), &s_QmPlayerStatsMapProgressPosYInputId, &g_Config.m_QmPlayerStatsMapProgressPosY, 0, 100, "%");
		}
		DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmPlayerStatsMapProgressDbgRoute, "Show dotted map route debug", Localize("Show dotted map route debug"), &g_Config.m_QmPlayerStatsMapProgressDbgRoute);
	}
}
