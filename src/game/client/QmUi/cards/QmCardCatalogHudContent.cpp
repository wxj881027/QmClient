#include "QmCardCatalogInternal.h"

#include <base/lock.h>
#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/SettingsPersonalization.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/binds.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/hook_countdown.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/qmclient/qm_music_hook_registry.h>
#include <game/client/components/qmclient/qmclient_utils.h>
#include <game/client/components/qmclient/translate/translate_backend.h>
#include <game/client/components/qmclient/translate/translate_ui_common.h>
#include <game/client/components/qmclient/translate/translate_ui_settings.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

using namespace FontIcons;

void CMenus::RenderQmHudBindStatusContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	DoSettingsToggleGroup(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, APPEARANCE_TAB_HUD, Content,
		{
			{&g_Config.m_ClShowhudKeyStatusReset, "appearance-show-key-stuck-status", Localize("Show key stuck status")},
			{&g_Config.m_ClShowhudKeyStatusHammer, "appearance-show-hammer-status", Localize("Show hammer status")},
			{&g_Config.m_ClShowhudKeyStatusControl, "appearance-show-dummy-control-status", Localize("Show dummy control status")},
			{&g_Config.m_ClShowhudKeyStatusSync, "appearance-show-dummy-copy-status", Localize("Show dummy copy status")},
		},
		CurrentSettingsContentMetrics(), !PrewarmOnly);
}

void CMenus::RenderQmHudDebugGraphContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	static CButtonContainer s_ReaderButtonDebugGraphToggle;
	static CButtonContainer s_ClearButtonDebugGraphToggle;
	CUIRect BindRow;
	if(Flow.Control("hud-debug-graph-toggle-bind", LineHeight, BindRow, ui_token::settings::RowGap(LineSpacing)))
		RenderQmHudKeyBindRow(BindRow, s_ReaderButtonDebugGraphToggle, s_ClearButtonDebugGraphToggle, Localize("Global toggle key"), "toggle dbg_graphs 0 1", LineHeight, BodySize, 0.0f, LabelWidth);

	CUIRect Row, LabelColumn, ControlColumn;
	if(Flow.Control("qmclient-debug-graph-panel-opacity", CurrentSettingsContentMetrics().m_LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
	{
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		RenderQmHudLabel("qmclient-debug-graph-panel-opacity", &LabelColumn, Localize("Panel opacity"), BodySize);
		static int s_QmMonitoringHudOpacityInputId;
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(&s_QmMonitoringHudOpacityInputId, &Row, &g_Config.m_QmMonitoringHudOpacity, &LabelColumn);
		RenderQmSettingsSliderWithValueInput(&s_QmMonitoringHudOpacityInputId, ControlColumn, &g_Config.m_QmMonitoringHudOpacity, 0, 100, "%", PrewarmOnly);
	}
}

void CMenus::RenderQmHudDebugModeContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	// 调试模式总开关：任一性能开关开启即视为"调试模式"开启；点击时统一开启/关闭三个开关。
	const bool DebugModeEnabled = g_Config.m_QmPerfDebug != 0 || g_Config.m_QmPerfLogfile != 0 || g_Config.m_QmPerfStutterDiagnostics != 0;

	CUIRect Row, LabelColumn, ControlColumn;
	if(Flow.Control("hud-debug-mode-enable", CurrentSettingsContentMetrics().m_LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
	{
		static int s_QmPerfDebugModeSwitchId;
		if(DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, &s_QmPerfDebugModeSwitchId, "Debug mode", Localize("Debug mode"), DebugModeEnabled, &Row))
		{
			const int NewValue = DebugModeEnabled ? 0 : 1;
			g_Config.m_QmPerfDebug = NewValue;
			g_Config.m_QmPerfLogfile = NewValue;
			g_Config.m_QmPerfStutterDiagnostics = NewValue;
		}
	}

	// 调试模式卡片不放个性调整收起区：细调项本身就是排障入口，直接全部可见才能一眼核对状态。
	// 行距由行自己声明（声明阶段记到上一行末尾），行后不再补 Space。
	auto RenderCheckbox = [this, &Flow, &Row, LineSpacing](const void *pId, const char *pText, int *pValue) {
		if(Flow.Control(pText, CurrentSettingsContentMetrics().m_LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
		{
			if(DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, pId, pText, Localize(pText), *pValue, &Row))
				*pValue ^= 1;
		}
	};
	RenderCheckbox(&g_Config.m_QmPerfDebug, "Enable main thread and render stage performance debug logging", &g_Config.m_QmPerfDebug);
	RenderCheckbox(&g_Config.m_QmPerfLogfile, "Write performance debug logs to dedicated file", &g_Config.m_QmPerfLogfile);

	if(Flow.Control("hud-QmPerfDebugThresholdMs", CurrentSettingsContentMetrics().m_LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
	{
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, "qmclient-debug-mode-threshold", &LabelColumn, Localize("Performance debug log threshold (ms)"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		static int s_QmPerfDebugThresholdMsInputId;
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(&s_QmPerfDebugThresholdMsInputId, &Row, &g_Config.m_QmPerfDebugThresholdMs, &LabelColumn);
		RenderQmSettingsSliderWithValueInput(&s_QmPerfDebugThresholdMsInputId, ControlColumn, &g_Config.m_QmPerfDebugThresholdMs, 1, 1000, "ms", PrewarmOnly);
	}

	RenderCheckbox(&g_Config.m_QmPerfStutterDiagnostics, "Enable client stutter diagnostics at startup", &g_Config.m_QmPerfStutterDiagnostics);
}

void CMenus::RenderQmHudDummyMiniViewContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool Expanded, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	// 行距由行自己声明（声明阶段记到上一行末尾），行后不再补 Space。
	const auto DeclaredCheckbox = [&](CUIRect &, float Height, float Spacing, const void *pId, const char *pTextId, const char *pText, int *pValue) {
		CUIRect Row;
		return Flow.Control(pTextId, Height, Row, Spacing) && RenderQmHudCheckbox(Row, Height, 0.0f, pId, pTextId, pText, pValue);
	};
	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmDummyMiniView, "Enable dummy window", Localize("Enable dummy window"), &g_Config.m_QmDummyMiniView);
	if(!g_Config.m_QmDummyMiniView)
		return;
	// 「启用虚拟窗口」→「仅当另一个 Tee 不在屏幕时显示」是一个新分区：间距由这一行声明成分区档。
	// 旧写法先补 LineHeight*0.8 再让行补 1×，两份叠加，实际间距随行高漂移。
	DeclaredCheckbox(Content, LineHeight, ui_token::settings::SectionGap(LineSpacing), &g_Config.m_QmDummyMiniViewAuto, "Only show when the other Tee is not on screen", Localize("Only show when the other Tee is not on screen"), &g_Config.m_QmDummyMiniViewAuto);
	float SectionReveal = 0.0f;
	if(!qm_card_catalog::QmCardRenderHook::PersonalizationReveal(this, Content, "qm:dummy_miniview", CurrentSettingsContentMetrics(), &SectionReveal, PrewarmOnly || Ui()->RenderOnly()))
		return;
	const CSettingsContentRevealScope SectionRevealScope(Ui(), Flow, SectionReveal);

	CUIRect Row, LabelColumn, ControlColumn;
	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue) {
		if(Flow.Control(pTextId, CurrentSettingsContentMetrics().m_LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
		{
			SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
			RenderQmHudLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
			GameClient()->m_Tooltips.DoSettingsToolTipForConfig(pInputId, &Row, pValue, &LabelColumn);
			RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, "%", PrewarmOnly);
		}
	};
	static int s_QmDummyMiniViewSizeInputId;
	static int s_QmDummyMiniViewZoomInputId;
	RenderValue("qmclient-dummy-window-size", "Dummy window size", &s_QmDummyMiniViewSizeInputId, &g_Config.m_QmDummyMiniViewSize, 50, 200);
	RenderValue("qmclient-dummy-window-zoom", "Dummy window zoom", &s_QmDummyMiniViewZoomInputId, &g_Config.m_QmDummyMiniViewZoom, 10, 300);
}

void CMenus::RenderQmHudDynamicIslandContent(CUIRect &Content, float LineHeight, float LineSpacing, bool OriginalStyle)
{
	CSettingsContentLayoutFlow Flow(Content);
	// 行距由行自己声明（声明阶段记到上一行末尾），行后不再补 Space。
	const auto DeclaredCheckbox = [&](CUIRect &, float Height, float Spacing, const void *pId, const char *pTextId, const char *pText, int *pValue) {
		CUIRect Row;
		return Flow.Control(pTextId, Height, Row, Spacing) && RenderQmHudCheckbox(Row, Height, 0.0f, pId, pTextId, pText, pValue);
	};
	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudIslandUseOriginalStyle, "Use original style", Localize("Use original style"), &g_Config.m_QmHudIslandUseOriginalStyle);
	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudIslandShowTeam, "Show team", Localize("Show team"), &g_Config.m_QmHudIslandShowTeam);

	if(g_Config.m_QmHudIslandUseOriginalStyle)
		return;

	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmFinishSummary, "Show finish summary", Localize("Show finish summary"), &g_Config.m_QmFinishSummary);
	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmFinishSummaryPreview, "Preview finish summary", Localize("Preview finish summary"), &g_Config.m_QmFinishSummaryPreview);

	// 钩子倒计时是独立开关：钩住玩家时在钩链中点显示倒计时环。
	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHookCountdown, "Enable hook countdown", Localize("Enable hook countdown"), &g_Config.m_QmHookCountdown);
	// 开关倒计时：总开关决定是否显示，两个位置开关决定显示在跟随 Tee 的圆环上还是灵动岛里，可同时勾选。
	const bool CountdownChanged = DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmSwitchCountdown, "Enable switch countdown", Localize("Enable switch countdown"), &g_Config.m_QmSwitchCountdown);
	if(g_Config.m_QmSwitchCountdown)
	{
		int FollowTee = QmHudSwitchCountdownShowsFollowTee(g_Config.m_QmSwitchCountdownMode) ? 1 : 0;
		int MediaIsland = QmHudSwitchCountdownShowsMediaIsland(g_Config.m_QmSwitchCountdownMode) ? 1 : 0;
		const int CurrentMode = std::clamp(g_Config.m_QmSwitchCountdownMode, static_cast<int>(EQmSwitchCountdownMode::FOLLOW_TEE), static_cast<int>(EQmSwitchCountdownMode::BOTH));
		bool LocationChanged = false;

		// 位置开关不绑定配置项，用固定地址当按钮 ID，避免取栈变量地址导致 ID 漂移。
		// 控件 id 的权威定义已随卡片目录迁至 QmUi/cards/QmCardCatalogHud.cpp（内部头提供取用入口），
		// 渲染路径与卡片路径必须共用同一组 id，否则同一次点击会被两条路径各处理一次。
		LocationChanged |= DeclaredCheckbox(Content, LineHeight, LineSpacing, qm_card_catalog::SwitchCountdownFollowTeeId(), "qmclient-switch-countdown-follow-tee", Localize("Follow Tee"), &FollowTee);
		LocationChanged |= DeclaredCheckbox(Content, LineHeight, LineSpacing, qm_card_catalog::SwitchCountdownMediaIslandId(), "qmclient-switch-countdown-media-island", Localize("Show in Dynamic Island"), &MediaIsland);

		if(CountdownChanged || LocationChanged)
		{
			// 两个位置都不勾时倒计时无处可显示，直接关掉总开关，避免界面与渲染结果互相打架。
			if(FollowTee == 0 && MediaIsland == 0)
			{
				g_Config.m_QmSwitchCountdown = 0;
				FollowTee = 0;
				MediaIsland = 1;
			}
			g_Config.m_QmSwitchCountdownMode = QmHudSwitchCountdownModeFromLocations(FollowTee != 0, MediaIsland != 0, CurrentMode);
		}
	}
	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudIslandAlwaysShowTrackDetails, "Always show song title and artist", Localize("Always show song title and artist"), &g_Config.m_QmHudIslandAlwaysShowTrackDetails);
	float SectionReveal = 0.0f;
	if(!qm_card_catalog::QmCardRenderHook::PersonalizationReveal(this, Content, "qm:dynamic_island", CurrentSettingsContentMetrics(), &SectionReveal, Ui()->RenderOnly()))
		return;
	const CSettingsContentRevealScope SectionRevealScope(Ui(), Flow, SectionReveal);

	static CButtonContainer s_DynamicIslandBgColorId;
	// 颜色弹窗里的 A(透明度) 是整块板的通透度（亚克力）：开高斯模糊时模糊照旧，
	// 板越透越看得见后面的画面；A=0 时整块板连外圈阴影一起消失。
	{
		CUIRect ColorRow;
		if(Flow.Control("hud-QmHudIslandBgColor", CurrentSettingsContentMetrics().m_ButtonHeight, ColorRow, ui_token::settings::RowGap(LineSpacing)))
		{
			DoLine_AlphaColorPicker(&s_DynamicIslandBgColorId, CurrentSettingsContentMetrics(), &ColorRow, Localize("Background color"), &g_Config.m_QmHudIslandBgColor, &g_Config.m_QmHudIslandBgOpacity, CConfig::ms_QmHudIslandBgColor, CConfig::ms_QmHudIslandBgOpacity);
		}
	}

	static CSettingsConditionalRows s_HookCountdownRows;
	if(s_HookCountdownRows.Update(g_Config.m_QmHookCountdown, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
	{
		const CSettingsContentRevealScope HookCountdownRevealScope(Ui(), Flow, s_HookCountdownRows.Fade(g_Config.m_QmUiMotionLevel));
		// 尺寸：50%–200%，与 HUD 其它百分比配置同一口径。
		CUIRect SizeRow;
		if(Flow.Control("qmclient-hook-countdown-size", CurrentSettingsContentMetrics().m_LineHeight, SizeRow, ui_token::settings::RowGap(CurrentSettingsContentMetrics().m_LineSpacing)))
		{
			CUIRect SizeLabel, SizeControl;
			SplitSettingsControlRow(SizeRow, CurrentSettingsContentMetrics(), &SizeLabel, &SizeControl);
			DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, "qmclient-hook-countdown-size-label", &SizeLabel, Localize("Hook countdown size"), CurrentSettingsContentMetrics().m_BodySize, TEXTALIGN_ML, {}, (int)SizeLabel.w);
			static int s_HookCountdownSizeId;
			RenderQmSettingsSliderWithValueInput(&s_HookCountdownSizeId, SizeControl, &g_Config.m_QmHookCountdownSize, 50, 200, "%", Ui()->RenderOnly());
		}

		// 环色是三段色阶（普通 / 警告 / 紧急），三段并排占一行：同一组颜色横向排开，
		// 既省掉两行高度，也能一眼看出它们是同一条时间轴上的颜色。
		// 无限钩是另一套彩虹色，这里只管普通钩的进度环。
		{
			CUIRect ColorRow;
			if(Flow.Control("qmclient-hook-countdown-colors", CurrentSettingsContentMetrics().m_ButtonHeight, ColorRow, ui_token::settings::RowGap(LineSpacing)))
			{
				CUIRect ColorLabel, ColorControls;
				SplitSettingsControlRow(ColorRow, CurrentSettingsContentMetrics(), &ColorLabel, &ColorControls);
				DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, "qmclient-hook-countdown-colors-label", &ColorLabel, Localize("Hook countdown colors"), CurrentSettingsContentMetrics().m_BodySize, TEXTALIGN_ML, {}, (int)ColorLabel.w);
				constexpr float SwatchGap = 4.0f;
				const float SwatchWidth = (ColorControls.w - SwatchGap * 2.0f) / 3.0f;
				unsigned int *apStageColors[] = {&g_Config.m_QmHookCountdownColor, &g_Config.m_QmHookCountdownWarningColor, &g_Config.m_QmHookCountdownUrgentColor};
				for(int Stage = 0; Stage < 3; ++Stage)
				{
					CUIRect Swatch = ColorControls;
					Swatch.x += (SwatchWidth + SwatchGap) * Stage;
					Swatch.w = SwatchWidth;
					DoButton_ColorPicker(&Swatch, apStageColors[Stage], false);
				}
			}
		}

		// 无限钩的指示样式：∞ 符号或圆环，两者都用同一套彩虹流动色。
		CUIRect StyleRow;
		if(Flow.Control("qmclient-hook-countdown-endless-style", CurrentSettingsContentMetrics().m_LineHeight, StyleRow, ui_token::settings::RowGap(LineSpacing)))
		{
			CUIRect StyleLabel, StyleControl;
			SplitSettingsControlRow(StyleRow, CurrentSettingsContentMetrics(), &StyleLabel, &StyleControl);
			DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, "qmclient-hook-countdown-endless-style-label", &StyleLabel, Localize("Endless hook indicator"), CurrentSettingsContentMetrics().m_BodySize, TEXTALIGN_ML, {}, (int)StyleLabel.w);
			// 两个选项互斥：点哪个就把配置设成对应档位。
			int InfinityStyle = g_Config.m_QmHookCountdownEndlessStyle == 0 ? 1 : 0;
			int RingStyle = g_Config.m_QmHookCountdownEndlessStyle == 1 ? 1 : 0;
			static CButtonContainer s_InfinityStyleId;
			static CButtonContainer s_RingStyleId;
			CUIRect InfinityButton, RingButton;
			StyleControl.VSplitMid(&InfinityButton, &RingButton, 4.0f);
			const bool RenderOnly = Ui()->RenderOnly();
			if(DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, &s_InfinityStyleId, "qmclient-hook-countdown-style-infinity", Localize("Infinity symbol"), InfinityStyle, &InfinityButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE) && !RenderOnly)
				g_Config.m_QmHookCountdownEndlessStyle = 0;
			if(DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, &s_RingStyleId, "qmclient-hook-countdown-style-ring", Localize("Rainbow ring"), RingStyle, &RingButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE) && !RenderOnly)
				g_Config.m_QmHookCountdownEndlessStyle = 1;
		}
	}
}

void CMenus::RenderQmHudLyricsContent(CUIRect &Content, float LineHeight, float LineSpacing, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	// 行距由行自己声明（声明阶段记到上一行末尾），行后不再补 Space。
	const auto DeclaredCheckbox = [&](CUIRect &, float Height, float Spacing, const void *pId, const char *pTextId, const char *pText, int *pValue) {
		CUIRect Row;
		return Flow.Control(pTextId, Height, Row, Spacing) && RenderQmHudCheckbox(Row, Height, 0.0f, pId, pTextId, pText, pValue);
	};
	// 音乐 Hook 开关：同一时间只能启用一个，点开其中一个时自动关闭其余。
	// 遍历 QmMusicHookRegistry 而非硬编码 Netease/Soda，新增 Hook 只需在注册表登记即自动覆盖。
	size_t HookCount = 0;
	const SQmMusicHookEntry *apHooks = QmMusicHookRegistry(&HookCount);
	for(size_t i = 0; i < HookCount; ++i)
	{
		const SQmMusicHookEntry &Hook = apHooks[i];
		const bool Changed = DeclaredCheckbox(Content, LineHeight, LineSpacing, Hook.m_pEnableConfig, Hook.m_pSettingsTextId, Localize(Hook.m_pSettingsText), Hook.m_pEnableConfig);
		if(Changed && *Hook.m_pEnableConfig != 0)
		{
			// 互斥：打开一个 Hook 时自动关闭其余 Hook。
			for(size_t j = 0; j < HookCount; ++j)
			{
				if(j != i)
					*apHooks[j].m_pEnableConfig = 0;
			}
		}
	}
	// 兜底：配置被外部直接改成多个 Hook 同时开启时，保留第一个，关闭其余。
	int FirstEnabled = -1;
	for(size_t i = 0; Flow.Input() && !Ui()->RenderOnly() && i < HookCount; ++i)
	{
		if(*apHooks[i].m_pEnableConfig != 0)
		{
			if(FirstEnabled == -1)
				FirstEnabled = (int)i;
			else
				*apHooks[i].m_pEnableConfig = 0;
		}
	}
	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmLyrics, "Enable lyrics", Localize("Enable lyrics"), &g_Config.m_QmLyrics);
	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmLyricsCommunity, "AMLL community lyrics", Localize("AMLL community lyrics"), &g_Config.m_QmLyricsCommunity);
	DoSettingsToggleGroup(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, Content,
		{
			{&g_Config.m_QmWorldLyrics, "World lyrics", Localize("World lyrics"), nullptr, 0, Localize("Show glowing, floating lyrics in the map. Each new line stays where it appears; previous lines rise and fade out")},
			{&g_Config.m_QmSinging, "Singing", Localize("Singing"), nullptr, 0, Localize("Double-tap the bound key within 400 ms to send the current lyric to public chat")},
			{&g_Config.m_QmHumming, "Humming", Localize("Humming"), nullptr, 0, Localize("Send three consecutive lyrics to public chat at random intervals of 360 to 1200 seconds, with 3 seconds between lines. Successfully sending a lyric with Singing cancels pending humming lines and restarts the random wait")},
		},
		CurrentSettingsContentMetrics(), !PrewarmOnly);
	float SectionReveal = 0.0f;
	if(qm_card_catalog::QmCardRenderHook::PersonalizationReveal(this, Content, "qm:lyrics", CurrentSettingsContentMetrics(), &SectionReveal, PrewarmOnly || Ui()->RenderOnly()))
	{
		const CSettingsContentRevealScope SectionRevealScope(Ui(), Flow, SectionReveal);
		DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmLyricsInMediaIsland, "Show lyrics in Dynamic Island", Localize("Show lyrics in Dynamic Island"), &g_Config.m_QmLyricsInMediaIsland);
		DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmLyricsSeparateIsland, "Separate Dynamic Island", Localize("Separate Dynamic Island"), &g_Config.m_QmLyricsSeparateIsland);
		DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmLyricsIslandFollowWidth, "Follow main island width", Localize("Follow main island width"), &g_Config.m_QmLyricsIslandFollowWidth);
		{
			CUIRect Row;
			if(Flow.Control("qmclient-lyrics-language", CurrentSettingsContentMetrics().m_LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
			{
				CUIRect Label, Control;
				SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &Label, &Control);
				RenderQmHudLabel("qmclient-lyrics-language-label", &Label, Localize("Lyrics language"), CurrentSettingsContentMetrics().m_BodySize);
				const char *apNames[] = {Localize("Original only"), Localize("Translation only"), Localize("Original and translation")};
				static CUi::SDropDownState s_LanguageDropDown;
				static CScrollRegion s_LanguageScroll;
				s_LanguageDropDown.m_SelectionPopupContext.m_pScrollRegion = &s_LanguageScroll;
				const int Selected = DoSettingsDropDown(&Control, g_Config.m_QmLyricsLanguage, apNames, std::size(apNames), s_LanguageDropDown, {}, &g_Config.m_QmLyricsLanguage, nullptr, &Row);
				if(!PrewarmOnly && !Ui()->RenderOnly() && Selected >= 0)
					g_Config.m_QmLyricsLanguage = Selected;
			}
		}
	}
	if(g_Config.m_QmKugouHookEnable != 0)
	{
		CUIRect Row, SetupButton, RestoreButton;
		if(Flow.Control("hud-lyrics-kugou-actions", CurrentSettingsContentMetrics().m_LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
		{
			static ui_widget::SSettingsActionGroupState s_KugouActionWidths;
			const char *apActionLabels[] = {Localize("Set up Kugou lyrics"), Localize("Restore Kugou files")};
			const auto Actions = ui_widget::PrepareSettingsActionGroup(*Ui(), s_KugouActionWidths, Row, apActionLabels, 2, CurrentSettingsContentMetrics());
			SetupButton = Actions.m_aButtons[0];
			RestoreButton = Actions.m_aButtons[1];
			static CButtonContainer s_KugouSetup, s_KugouRestore;
			const bool Setup = DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, &s_KugouSetup, "qmclient-kugou-setup", Localize("Set up Kugou lyrics"), 0, &SetupButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE);
			const bool Restore = DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, &s_KugouRestore, "qmclient-kugou-restore", Localize("Restore Kugou files"), 0, &RestoreButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE);
			// 预热和只读布局阶段只绘制按钮，安装文件操作由 helper 再次明确确认。
			if(!PrewarmOnly && !Ui()->RenderOnly() && (Setup || Restore))
				GameClient()->m_MusicLyricsIntegration.RunKugouSetup(Restore);
		}
	}
	if(g_Config.m_QmKugouHookEnable != 0 || g_Config.m_QmQQMusicHookEnable != 0)
	{
		CUIRect Row;
		if(Flow.Control("qmclient-music-hook-status", CurrentSettingsContentMetrics().m_LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
		{
			char aStatus[512] = {};
			GameClient()->m_MusicLyricsIntegration.GetStatus(aStatus, sizeof(aStatus));
			RenderQmHudLabel("qmclient-music-hook-status", &Row, aStatus, CurrentSettingsContentMetrics().m_BodySize);
		}
	}
	static CSettingsConditionalRows s_SpotifyLyricsRows;
	if(s_SpotifyLyricsRows.Update(g_Config.m_QmSpotifyEnable != 0, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
	{
		const CSettingsContentRevealScope SpotifyLyricsRevealScope(Ui(), Flow, s_SpotifyLyricsRows.Fade(g_Config.m_QmUiMotionLevel));
		static CLineInput s_SpotifySpDc(g_Config.m_QmSpotifySpDc, sizeof(g_Config.m_QmSpotifySpDc));
		CUIRect Row, LabelColumn, InputColumn;
		if(Flow.Control("qmclient-lyrics-spotify-sp-dc", CurrentSettingsContentMetrics().m_LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
		{
			SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &InputColumn);
			RenderQmHudLabel("qmclient-lyrics-spotify-sp-dc", &LabelColumn, "spotify_ck", CurrentSettingsContentMetrics().m_BodySize);
			static ui_widget::SPasswordFieldState s_SpotifySpDcState;
			IUiContext TextInputCtx = SettingsUiContext("settings_qmclient_lyrics_spotify_text_inputs");
			ui_widget::SPasswordFieldOptions SpDcOptions;
			SpDcOptions.m_pPlaceholder = Localize("Paste sp_dc from Spotify web cookies");
			SpDcOptions.m_FontSize = CurrentSettingsContentMetrics().m_BodySize;
			ui_widget::PasswordField(TextInputCtx, &s_SpotifySpDc, InputColumn, &s_SpotifySpDcState, SpDcOptions);
		}
	}
	// LX Music 由本机 HTTP 轮询,连接状态只能靠一行文本反馈:
	// 端口冲突(常见于 LX 数据同步服务)与开放 API 未开启都在这里说明。
	if(g_Config.m_QmLxMusicEnable != 0)
	{
		CUIRect Row;
		// 开放 API 已开启时不再显示按钮:点它只会得到「已经是开启状态」。
		// 写入动作必须由用户触发,因为 LX 退出时会覆盖配置文件。
		if(GameClient()->m_LxMusicIntegration.NeedsOpenApiSetup() &&
			Flow.Control("qmclient-lyrics-lxmusic-enable-api", CurrentSettingsContentMetrics().m_LineHeight, Row))
		{
			static CButtonContainer s_LxEnableApi;
			if(DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD,
				   &s_LxEnableApi, "qmclient-lyrics-lxmusic-enable-api", Localize("Turn on LX Music open API"), 0, &Row,
				   BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE) &&
				!PrewarmOnly && !Ui()->RenderOnly())
				GameClient()->m_LxMusicIntegration.EnableOpenApi();
			Flow.Space(LineSpacing);
		}
		if(Flow.Control("qmclient-lyrics-lxmusic-status", CurrentSettingsContentMetrics().m_LineHeight, Row))
		{
			char aStatus[512] = {};
			GameClient()->m_LxMusicIntegration.GetStatus(aStatus, sizeof(aStatus));
			RenderQmHudLabel("qmclient-lyrics-lxmusic-status", &Row, aStatus, ui_token::font::BODY);
		}
		Flow.Space(LineSpacing);
	}
}

void CMenus::RenderQmHudSystemMediaControlsContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	// 行距由行自己声明（声明阶段记到上一行末尾），行后不再补 Space。
	const auto DeclaredCheckbox = [&](CUIRect &, float Height, float Spacing, const void *pId, const char *pTextId, const char *pText, int *pValue) {
		CUIRect Row;
		const bool Changed = Flow.Control(pTextId, Height, Row, Spacing) && DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, -1, pId, pTextId, pText, *pValue, &Row);
		if(Changed)
			*pValue ^= 1;
		return Changed;
	};
	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmSmtcEnable, "Enable system media control", Localize("Enable system media control"), &g_Config.m_QmSmtcEnable);
	if(!g_Config.m_QmSmtcEnable)
		return;

	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmSmtcShowHud, "Show song info in top-left corner", Localize("Show song info in top-left corner"), &g_Config.m_QmSmtcShowHud);
	// Hook 开关与歌词开关已迁出为独立的 qm:lyrics 卡（内容函数仍是 RenderQmHudLyricsContent，
	// 由卡片目录的 Hud 分类模块负责调用）。此处不再渲染，否则歌词与来源开关会在两处各出现一次。
	CUIRect MediaButtons, PrevButton, PlayButton, NextButton;
	if(Flow.Control("hud-media-actions", LineHeight, MediaButtons, ui_token::settings::RowGap(LineSpacing)))
	{
		static ui_widget::SSettingsActionGroupState s_MediaActionWidths;
		const char *apActionLabels[] = {Localize("Previous"), Localize("Play/Pause"), Localize("Next")};
		const auto Actions = ui_widget::PrepareSettingsActionGroup(*Ui(), s_MediaActionWidths, MediaButtons, apActionLabels, 3, CurrentSettingsContentMetrics());
		PrevButton = Actions.m_aButtons[0];
		PlayButton = Actions.m_aButtons[1];
		NextButton = Actions.m_aButtons[2];

		static CButtonContainer s_SmtcPrev;
		if(DoSettingsButton_Menu(SETTINGS_DDNET, -1, -1, &s_SmtcPrev, "qmclient-smtc-previous", Localize("Previous"), 0, &PrevButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE))
			GameClient()->m_SystemMediaControls.Previous();
		static CButtonContainer s_SmtcPlayPause;
		if(DoSettingsButton_Menu(SETTINGS_DDNET, -1, -1, &s_SmtcPlayPause, "qmclient-smtc-play-pause", Localize("Play/Pause"), 0, &PlayButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE))
			GameClient()->m_SystemMediaControls.PlayPause();
		static CButtonContainer s_SmtcNext;
		if(DoSettingsButton_Menu(SETTINGS_DDNET, -1, -1, &s_SmtcNext, "qmclient-smtc-next", Localize("Next"), 0, &NextButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE))
			GameClient()->m_SystemMediaControls.Next();
	}
}

void CMenus::RenderQmHudNotificationsBasicContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	// 行距由行自己声明（声明阶段记到上一行末尾），行后不再补 Space。
	// 同一个卡片下面还会接「高级选项」的内容，所以这里不能留尾距，否则接缝处会叠成双倍。
	const auto DeclaredCheckbox = [&](CUIRect &, float Height, float Spacing, const void *pId, const char *pTextId, const char *pText, int *pValue) {
		CUIRect Row;
		return Flow.Control(pTextId, Height, Row, Spacing) && RenderQmHudCheckbox(Row, Height, 0.0f, pId, pTextId, pText, pValue);
	};
	const float LineHeight = Metrics.m_LineHeight;
	const float BodySize = Metrics.m_BodySize;
	const float LineSpacing = Metrics.m_LineSpacing;
	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsSystem, "Show important server prompts as notifications", Localize("Show important server prompts as notifications"), &g_Config.m_QmHudNotificationsSystem);
	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsEcho, "Route Echo messages to notifications", Localize("Route Echo messages to notifications"), &g_Config.m_QmHudNotificationsEcho);
	CUIRect Row, LabelColumn, ControlColumn;
	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		if(Flow.Control(pTextId, CurrentSettingsContentMetrics().m_LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
		{
			SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
			RenderQmHudLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
			GameClient()->m_Tooltips.DoSettingsToolTipForConfig(pInputId, &Row, pValue, &LabelColumn);
			RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
		}
	};
	static int s_QmHudNotificationHoldInputId;
	static int s_QmHudNotificationTextSizeInputId;
	RenderValue("qmclient-notifications-hold-time", "Notification hold time", &s_QmHudNotificationHoldInputId, &g_Config.m_QmHudNotificationsHoldMs, 500, 10000, "ms");
	RenderValue("qmclient-notifications-text-size", "Notification text size", &s_QmHudNotificationTextSizeInputId, &g_Config.m_QmHudNotificationsTextSize, 1, 24);
	// 「个性调整」折叠行：高级内容在 AdvancedContent 里按同一配置位做显隐，
	// 这里只负责入口本身；行 id（"Advanced options"）与文案保持不变，搜索与持久化不破。
	static CButtonContainer s_NotificationsAdvancedFoldRowButton;
	CUIRect NotificationsAdvancedFoldRow;
	// 折叠行就是普通一行，行前只留一档行距（不再画分割线）。
	if(Flow.Control("Advanced options", LineHeight, NotificationsAdvancedFoldRow, ui_token::settings::RowGap(LineSpacing)))
	{
		const SSettingsFoldRowStyle NotificationsAdvancedFoldStyle = ResolveSettingsFoldRowStyle(
			SettingsCardDeckVisualOptions().m_BorderColor,
			ResolveConfiguredAccentColor().WithAlpha(1.0f));
		const bool NotificationsAdvancedClicked = SettingsFoldRow(Ui(), &s_NotificationsAdvancedFoldRowButton, NotificationsAdvancedFoldRow, Localize("Personal adjustments"), Metrics.m_SmallSize, g_Config.m_QmHudNotificationsShowAdvanced != 0, NotificationsAdvancedFoldStyle);
		if(NotificationsAdvancedClicked && Flow.Input() && !PrewarmOnly && !Ui()->RenderOnly())
			g_Config.m_QmHudNotificationsShowAdvanced = g_Config.m_QmHudNotificationsShowAdvanced ? 0 : 1;
	}
}

void CMenus::RenderQmHudNotificationsAdvancedContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	// 这一块由 Basic 内容里的「个性调整」折叠行驱动：显隐走与其它条件行同一条曲线，
	// 收起动画播完之前继续占位，避免高度先塌、内容再凭空消失。收缩到 0 就不再声明。
	static CSettingsRowReveal s_NotificationsAdvancedSectionReveal;
	const bool NotificationsAdvancedDeclared = s_NotificationsAdvancedSectionReveal.Update(g_Config.m_QmHudNotificationsShowAdvanced != 0, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel);
	if(!NotificationsAdvancedDeclared)
		return;
	const SQmListEntryVisual NotificationsAdvancedVisual = s_NotificationsAdvancedSectionReveal.Visual(g_Config.m_QmUiMotionLevel);
	const CSettingsContentRevealScope NotificationsAdvancedRevealScope(Ui(), Flow, NotificationsAdvancedVisual.m_Fade);
	// 行距由行自己声明（声明阶段记到上一行末尾），行后不再补 Space，卡底不会多留一档。
	const auto DeclaredCheckbox = [&](CUIRect &, float Height, float Spacing, const void *pId, const char *pTextId, const char *pText, int *pValue) {
		CUIRect Row;
		return Flow.Control(pTextId, Height, Row, Spacing) && RenderQmHudCheckbox(Row, Height, 0.0f, pId, pTextId, pText, pValue);
	};
	const float LineHeight = Metrics.m_LineHeight;
	const float BodySize = Metrics.m_BodySize;
	const float LineSpacing = Metrics.m_LineSpacing;
	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsUseCategoryFilters, "Use notification category filters", Localize("Use notification category filters"), &g_Config.m_QmHudNotificationsUseCategoryFilters);
	if(g_Config.m_QmHudNotificationsUseCategoryFilters)
	{
		DoSettingsToggleGroup(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, Content,
			{
				{&g_Config.m_QmHudNotificationsShowPrompts, "Show important server prompts", Localize("Show important server prompts")},
				{&g_Config.m_QmHudNotificationsShowUnknown, "Show unknown server messages", Localize("Show unknown server messages")},
				{&g_Config.m_QmHudNotificationsShowBasicInfo, "Show basic server information", Localize("Show basic server information")},
				{&g_Config.m_QmHudNotificationsShowHelpInfo, "Show server help and usage messages", Localize("Show server help and usage messages")},
			},
			Metrics, !PrewarmOnly);
	}
	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsCompatSolo, "Detect compatible solo prompts from custom servers", Localize("Detect compatible solo prompts from custom servers"), &g_Config.m_QmHudNotificationsCompatSolo);
	// Flow.Text 行只带行后间距，没有「行前间距」参数：它上面的间距仍由上一行补一档。
	Flow.Space(ui_token::settings::RowGap(LineSpacing));

	CUIRect Row, LabelColumn, ControlColumn;
	Flow.Text("qmclient-notifications-basic-info-note", Localize("Join, version, rules, and help messages stay in chat instead of popups"), 0.0f, Metrics.m_SmallSize, 0.0f, [this](const char *pText, float Width, float Size) { return TextRender()->TextBoundingBox(Size, pText, -1, Width).m_H; }, [this, &Metrics](const CUIRect &Rect, const char *pText) {
			CUIRect Row = Rect;
			TextRender()->TextColor(ColorRGBA(0.9f, 0.9f, 0.9f, 0.8f));
			RenderQmHudLabel("qmclient-notifications-basic-info-note", &Row, pText, Metrics.m_SmallSize);
			TextRender()->TextColor(TextRender()->DefaultTextColor()); });
	static CButtonContainer s_QmHudNotificationBgColorId;
	{
		CUIRect ColorRow;
		if(Flow.Control("hud-QmHudNotificationsBgColor", CurrentSettingsContentMetrics().m_ButtonHeight, ColorRow, ui_token::settings::RowGap(CurrentSettingsContentMetrics().m_LineSpacing)))
		{
			DoLine_ColorPicker(&s_QmHudNotificationBgColorId, Metrics, &ColorRow, Localize("Notification background"), &g_Config.m_QmHudNotificationsBgColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmHudNotificationsBgColor, true)), false, nullptr, true);
		}
	}
	static CButtonContainer s_QmHudNotificationTextColorId;
	{
		CUIRect ColorRow;
		if(Flow.Control("hud-QmHudNotificationsTextColor", CurrentSettingsContentMetrics().m_ButtonHeight, ColorRow, ui_token::settings::RowGap(CurrentSettingsContentMetrics().m_LineSpacing)))
		{
			DoLine_ColorPicker(&s_QmHudNotificationTextColorId, Metrics, &ColorRow, Localize("System prompt text color"), &g_Config.m_QmHudNotificationsTextColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmHudNotificationsTextColor, true)), false, nullptr, true);
		}
	}
	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsEchoInheritColor, "Echo follows the original chat color", Localize("Echo follows the original chat color"), &g_Config.m_QmHudNotificationsEchoInheritColor);
	static CButtonContainer s_QmHudNotificationEchoTextColorId;
	{
		CUIRect ColorRow;
		if(Flow.Control("hud-QmHudNotificationsEchoTextColor", CurrentSettingsContentMetrics().m_ButtonHeight, ColorRow, ui_token::settings::RowGap(CurrentSettingsContentMetrics().m_LineSpacing)))
		{
			DoLine_ColorPicker(&s_QmHudNotificationEchoTextColorId, Metrics, &ColorRow, Localize("Echo text color when not inheriting chat color"), &g_Config.m_QmHudNotificationsEchoTextColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmHudNotificationsEchoTextColor, true)), false, nullptr, true);
		}
	}

	if(Flow.Control("qmclient-notifications-popup-animation", CurrentSettingsContentMetrics().m_LineHeight, Row, ui_token::settings::RowGap(CurrentSettingsContentMetrics().m_LineSpacing)))
	{
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		RenderQmHudLabel("qmclient-notifications-popup-animation", &LabelColumn, Localize("Popup animation"), BodySize);
		const char *apHudNotificationAnimDropDownNames[] = {Localize("Fade and slide"), Localize("Fade only"), Localize("No animation")};
		static CUi::SDropDownState s_HudNotificationAnimDropDownState;
		static CScrollRegion s_HudNotificationAnimDropDownScrollRegion;
		s_HudNotificationAnimDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_HudNotificationAnimDropDownScrollRegion;
		const int AnimSelectedNew = DoSettingsDropDown(&ControlColumn, g_Config.m_QmHudNotificationsAnimType, apHudNotificationAnimDropDownNames, std::size(apHudNotificationAnimDropDownNames), s_HudNotificationAnimDropDownState, {}, &g_Config.m_QmHudNotificationsAnimType, nullptr, &Row);
		if(g_Config.m_QmHudNotificationsAnimType != AnimSelectedNew)
			g_Config.m_QmHudNotificationsAnimType = AnimSelectedNew;
	}

	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		if(Flow.Control(pTextId, CurrentSettingsContentMetrics().m_LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
		{
			SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
			RenderQmHudLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
			GameClient()->m_Tooltips.DoSettingsToolTipForConfig(pInputId, &Row, pValue, &LabelColumn);
			RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
		}
	};
	static int s_QmHudNotificationAnimInputId;
	static int s_QmHudNotificationMaxVisibleInputId;
	static int s_QmHudNotificationEdgeMarginInputId;
	RenderValue("qmclient-notifications-animation-duration", "Animation duration", &s_QmHudNotificationAnimInputId, &g_Config.m_QmHudNotificationsAnimMs, 0, 2000, "ms");
	RenderValue("qmclient-notifications-max-visible", "Max visible notifications", &s_QmHudNotificationMaxVisibleInputId, &g_Config.m_QmHudNotificationsMaxVisible, 1, 8);
	RenderValue("qmclient-notifications-edge-margin", "Edge margin", &s_QmHudNotificationEdgeMarginInputId, &g_Config.m_QmHudNotificationsEdgeMargin, 0, 32);
}

void CMenus::RenderQmHudCoordsContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	const float LineHeight = Metrics.m_LineHeight;
	const float BodySize = Metrics.m_BodySize;
	const float LineSpacing = Metrics.m_LineSpacing;
	CUIRect Row, LabelCol, ControlCol;
	auto DoQmSettingsCheckboxAuto = [this](const void *pId, const char *pTextId, const char *pText, int *pValue, CUIRect *pRect, float) {
		Ui()->DoConfigTooltip(pId, pRect, pValue);
		const char *pOverrideTooltip = TemporaryOverrideTooltip(pValue);
		SLabelProperties LabelProps;
		if(pOverrideTooltip != nullptr)
		{
			LabelProps.SetColor(ui_token::color::TEXT_DISABLED);
			// 灰化行不占 hover，补一次只读的按钮逻辑让提示能激活（返回值丢弃，不写值）。
			if(!Ui()->RenderOnly())
			{
				Ui()->DoButtonLogic(pId, 0, pRect, BUTTONFLAG_NONE);
				GameClient()->m_Tooltips.DoToolTip(pId, pRect, pOverrideTooltip);
			}
		}
		const bool Changed = DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, pId, pTextId, pText, *pValue, pRect, LabelProps, pOverrideTooltip == nullptr) != 0;
		if(Changed)
			*pValue ^= 1;
		return Changed;
	};
	auto DoQmSettingsLabel = [this](const char *pTextId, CUIRect *pRect, const char *pText, float FontSize) {
		RenderQmHudLabel(pTextId, pRect, pText, FontSize);
	};

	if(Flow.Control("Show own coordinates", CurrentSettingsContentMetrics().m_LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
	{
		DoQmSettingsCheckboxAuto(&g_Config.m_QmNameplateCoordsOwn, "Show own coordinates", Localize("Show own coordinates"), &g_Config.m_QmNameplateCoordsOwn, &Row, LineHeight);
	}

	if(Flow.Control("Show other players' coordinates", CurrentSettingsContentMetrics().m_LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
	{
		DoQmSettingsCheckboxAuto(&g_Config.m_QmNameplateCoords, "Show other players' coordinates", Localize("Show other players' coordinates"), &g_Config.m_QmNameplateCoords, &Row, LineHeight);
	}

	if(Flow.Control("Show X", CurrentSettingsContentMetrics().m_LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
	{
		DoQmSettingsCheckboxAuto(&g_Config.m_QmNameplateCoordX, "Show X", Localize("Show X"), &g_Config.m_QmNameplateCoordX, &Row, LineHeight);
	}

	if(Flow.Control("Show Y", CurrentSettingsContentMetrics().m_LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
	{
		DoQmSettingsCheckboxAuto(&g_Config.m_QmNameplateCoordY, "Show Y", Localize("Show Y"), &g_Config.m_QmNameplateCoordY, &Row, LineHeight);
	}

	if(Flow.Control("X alignment hint with me", CurrentSettingsContentMetrics().m_LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
	{
		DoQmSettingsCheckboxAuto(&g_Config.m_QmNameplateCoordXAlignHint, "X alignment hint with me", Localize("X alignment hint with me"), &g_Config.m_QmNameplateCoordXAlignHint, &Row, LineHeight);
	}

	float SectionReveal = 0.0f;
	if(!qm_card_catalog::QmCardRenderHook::PersonalizationReveal(this, Content, "qm:coords", Metrics, &SectionReveal, PrewarmOnly || Ui()->RenderOnly()))
		return;
	const CSettingsContentRevealScope SectionRevealScope(Ui(), Flow, SectionReveal);

	if(Flow.Control("Strict mode", CurrentSettingsContentMetrics().m_LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
	{
		DoQmSettingsCheckboxAuto(&g_Config.m_QmNameplateCoordXAlignHintStrict, "Strict mode", Localize("Strict mode"), &g_Config.m_QmNameplateCoordXAlignHintStrict, &Row, LineHeight);
	}

	if(Flow.Control("qmclient-show-coordinates-detection-time", CurrentSettingsContentMetrics().m_LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
	{
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelCol, &ControlCol);
		DoQmSettingsLabel("qmclient-show-coordinates-detection-time", &LabelCol, Localize("Detection time"), BodySize);
		static int s_CoordXAlignHintWindowSliderId;
		ui_widget::SNumericFieldState *pState = GetSettingsNumericFieldState(&s_CoordXAlignHintWindowSliderId);
		ui_widget::SNumericFieldOptions Options;
		Options.m_pSuffix = "ms";
		Options.m_FontSize = BodySize;
		Options.m_ValueStep = 100;
		IUiContext InputCtx;
		InputCtx.m_pUi = Ui();
		InputCtx.m_pAnim = PrewarmOnly ? nullptr : &GameClient()->UiRuntimeV2()->AnimRuntime();
		InputCtx.m_pTree = PrewarmOnly ? nullptr : &GameClient()->UiRuntimeV2()->Tree();
		InputCtx.m_ScopeHash = MakeUiScopeHash("qmclient_coord_x_align_hint_window");
		InputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
		const int OriginalValue = g_Config.m_QmNameplateCoordXAlignHintWindowMs;
		ui_widget::NumericField(InputCtx, pState, &s_CoordXAlignHintWindowSliderId, &g_Config.m_QmNameplateCoordXAlignHintWindowMs, 100, 3000, ControlCol, Options);
		if(PrewarmOnly || Ui()->RenderOnly())
			g_Config.m_QmNameplateCoordXAlignHintWindowMs = OriginalValue;
	}

	static CButtonContainer s_CoordXAlignHintColorId;
	{
		CUIRect ColorRow;
		if(Flow.Control("hud-QmNameplateCoordXAlignHintColor", CurrentSettingsContentMetrics().m_ButtonHeight, ColorRow, ui_token::settings::RowGap(LineSpacing)))
		{
			DoLine_ColorPicker(&s_CoordXAlignHintColorId, Metrics, &ColorRow, Localize("X alignment color"), &g_Config.m_QmNameplateCoordXAlignHintColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmNameplateCoordXAlignHintColor)), false, nullptr, false, false);
		}
	}
}

void CMenus::RenderQmHudGoresDrownBoardContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	// 行距由行自己声明（声明阶段记到上一行末尾），行后不再补 Space。
	const auto DeclaredCheckbox = [&](CUIRect &, float Height, float Spacing, const void *pId, const char *pTextId, const char *pText, int *pValue) {
		CUIRect Row;
		return Flow.Control(pTextId, Height, Row, Spacing) && RenderQmHudCheckbox(Row, Height, 0.0f, pId, pTextId, pText, pValue);
	};
	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmGoresDrownBoard, "Show Gores drown board", Localize("Show Gores drown board"), &g_Config.m_QmGoresDrownBoard);
	if(!g_Config.m_QmGoresDrownBoard)
		return;

	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmGoresDrownBoardIncludeTeam0, "Include Team 0", Localize("Include Team 0"), &g_Config.m_QmGoresDrownBoardIncludeTeam0);

	CUIRect Row, LabelColumn, ControlColumn;
	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		if(Flow.Control(pTextId, CurrentSettingsContentMetrics().m_LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
		{
			SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
			RenderQmHudLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
			GameClient()->m_Tooltips.DoSettingsToolTipForConfig(pInputId, &Row, pValue, &LabelColumn);
			RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
		}
	};
	static int s_QmGoresDrownBoardMaxPlayersInputId;
	static int s_QmGoresDrownBoardOpacityInputId;
	RenderValue("qmclient-gores-drown-board-max-players", "Players shown", &s_QmGoresDrownBoardMaxPlayersInputId, &g_Config.m_QmGoresDrownBoardMaxPlayers, 1, 16);
	RenderValue("qmclient-gores-drown-board-opacity", "Card opacity", &s_QmGoresDrownBoardOpacityInputId, &g_Config.m_QmGoresDrownBoardOpacity, 0, 100, "%");
	DeclaredCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmGoresDrownBoardShowTee, "Show player Tee", Localize("Show player Tee"), &g_Config.m_QmGoresDrownBoardShowTee);
}
