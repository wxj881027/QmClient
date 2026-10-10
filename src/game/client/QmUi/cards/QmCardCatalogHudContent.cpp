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

#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/binds.h>
#include <game/client/components/menus.h>
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
#include <string>
#include <unordered_map>
#include <vector>

using namespace FontIcons;

extern std::unordered_map<std::string, CBindSlot> g_CommandBindCache;

void CMenus::RenderQmHudBindStatusContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	// 内置四项状态开关（自外观页 DDRace HUD 卡片迁移）
	DoSettingsToggleGroup(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, Content,
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
	static CButtonContainer s_ReaderButtonDebugGraphToggle;
	static CButtonContainer s_ClearButtonDebugGraphToggle;
	RenderQmHudKeyBindRow(Content, s_ReaderButtonDebugGraphToggle, s_ClearButtonDebugGraphToggle, Localize("Global toggle key"), "toggle dbg_graphs 0 1", LineHeight, BodySize, LineSpacing, LabelWidth);

	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	RenderQmHudLabel("qmclient-debug-graph-panel-opacity", &LabelColumn, Localize("Panel opacity"), BodySize);
	static int s_QmMonitoringHudOpacityInputId;
	GameClient()->m_Tooltips.DoSettingsToolTipForConfig(&s_QmMonitoringHudOpacityInputId, &Row, &g_Config.m_QmMonitoringHudOpacity, &LabelColumn);
	RenderQmSettingsSliderWithValueInput(&s_QmMonitoringHudOpacityInputId, ControlColumn, &g_Config.m_QmMonitoringHudOpacity, 0, 100, "%", PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
}

void CMenus::RenderQmHudDebugModeContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	// 调试模式总开关：任一性能开关开启即视为"调试模式"开启；点击时统一开启/关闭三个开关。
	const bool DebugModeEnabled = g_Config.m_QmPerfDebug != 0 || g_Config.m_QmPerfLogfile != 0 || g_Config.m_QmPerfStutterDiagnostics != 0;

	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(LineHeight, &Row, &Content);
	static int s_QmPerfDebugModeSwitchId;
	if(DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, &s_QmPerfDebugModeSwitchId, "Debug mode", Localize("Debug mode"), DebugModeEnabled, &Row))
	{
		const int NewValue = DebugModeEnabled ? 0 : 1;
		g_Config.m_QmPerfDebug = NewValue;
		g_Config.m_QmPerfLogfile = NewValue;
		g_Config.m_QmPerfStutterDiagnostics = NewValue;
	}
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	auto RenderCheckbox = [this, &Content, &Row, LineHeight, LineSpacing](const void *pId, const char *pText, int *pValue) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		if(DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, pId, pText, Localize(pText), *pValue, &Row))
			*pValue ^= 1;
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	RenderCheckbox(&g_Config.m_QmPerfDebug, "Enable main thread and render stage performance debug logging", &g_Config.m_QmPerfDebug);
	RenderCheckbox(&g_Config.m_QmPerfLogfile, "Write performance debug logs to dedicated file", &g_Config.m_QmPerfLogfile);

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, "qmclient-debug-mode-threshold", &LabelColumn, Localize("Performance debug log threshold (ms)"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
	static int s_QmPerfDebugThresholdMsInputId;
	GameClient()->m_Tooltips.DoSettingsToolTipForConfig(&s_QmPerfDebugThresholdMsInputId, &Row, &g_Config.m_QmPerfDebugThresholdMs, &LabelColumn);
	RenderQmSettingsSliderWithValueInput(&s_QmPerfDebugThresholdMsInputId, ControlColumn, &g_Config.m_QmPerfDebugThresholdMs, 1, 1000, "ms", PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	RenderCheckbox(&g_Config.m_QmPerfStutterDiagnostics, "Enable client stutter diagnostics at startup", &g_Config.m_QmPerfStutterDiagnostics);
}

void CMenus::RenderQmHudDummyMiniViewContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool Expanded, bool PrewarmOnly)
{
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmDummyMiniView, "Enable dummy window", Localize("Enable dummy window"), &g_Config.m_QmDummyMiniView);
	if(!Expanded)
		return;
	Content.HSplitTop(LineHeight * 0.8f, nullptr, &Content);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmDummyMiniViewAuto, "Only show when the other Tee is not on screen", Localize("Only show when the other Tee is not on screen"), &g_Config.m_QmDummyMiniViewAuto);
	CUIRect Row, LabelColumn, ControlColumn;
	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		RenderQmHudLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(pInputId, &Row, pValue, &LabelColumn);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, "%", PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	static int s_QmDummyMiniViewSizeInputId;
	static int s_QmDummyMiniViewZoomInputId;
	RenderValue("qmclient-dummy-window-size", "Dummy window size", &s_QmDummyMiniViewSizeInputId, &g_Config.m_QmDummyMiniViewSize, 50, 200);
	RenderValue("qmclient-dummy-window-zoom", "Dummy window zoom", &s_QmDummyMiniViewZoomInputId, &g_Config.m_QmDummyMiniViewZoom, 10, 300);
}

void CMenus::RenderQmHudDynamicIslandContent(CUIRect &Content, float LineHeight, float LineSpacing, bool OriginalStyle)
{
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudIslandUseOriginalStyle, "Use original style", Localize("Use original style"), &g_Config.m_QmHudIslandUseOriginalStyle);
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudIslandShowTeam, "Show team", Localize("Show team"), &g_Config.m_QmHudIslandShowTeam);

	if(OriginalStyle)
		return;

	static CButtonContainer s_DynamicIslandBgColorId;
	// 颜色弹窗里的 A(透明度) 是整块板的通透度（亚克力）：开高斯模糊时模糊照旧，
	// 板越透越看得见后面的画面；A=0 时整块板连外圈阴影一起消失。
	DoLine_AlphaColorPicker(&s_DynamicIslandBgColorId, CurrentSettingsContentMetrics(), &Content, Localize("Background color"), &g_Config.m_QmHudIslandBgColor, &g_Config.m_QmHudIslandBgOpacity, 0x9C460E, 80);

	// 钩子倒计时是独立开关：钩住玩家时在钩链中点显示倒计时环。
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHookCountdown, "Enable hook countdown", Localize("Enable hook countdown"), &g_Config.m_QmHookCountdown);

	// 开关倒计时：总开关决定是否显示，两个位置开关决定显示在跟随 Tee 的圆环上还是灵动岛里，可同时勾选。
	const bool CountdownChanged = RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmSwitchCountdown, "Enable switch countdown", Localize("Enable switch countdown"), &g_Config.m_QmSwitchCountdown);
	if(!g_Config.m_QmSwitchCountdown)
		return;

	int FollowTee = QmHudSwitchCountdownShowsFollowTee(g_Config.m_QmSwitchCountdownMode) ? 1 : 0;
	int MediaIsland = QmHudSwitchCountdownShowsMediaIsland(g_Config.m_QmSwitchCountdownMode) ? 1 : 0;
	const int CurrentMode = std::clamp(g_Config.m_QmSwitchCountdownMode, static_cast<int>(EQmSwitchCountdownMode::FOLLOW_TEE), static_cast<int>(EQmSwitchCountdownMode::BOTH));
	bool LocationChanged = false;

	// 位置开关不绑定配置项，用固定地址当按钮 ID，避免取栈变量地址导致 ID 漂移。
	// 控件 id 的权威定义已随卡片目录迁至 QmUi/cards/QmCardCatalogHud.cpp（内部头提供取用入口），
	// 渲染路径与卡片路径必须共用同一组 id，否则同一次点击会被两条路径各处理一次。
	LocationChanged |= ToggleQmHudCountdownLocation(Content, LineHeight, LineSpacing, qm_card_catalog::SwitchCountdownFollowTeeId(), &FollowTee);
	LocationChanged |= ToggleQmHudCountdownLocation(Content, LineHeight, LineSpacing, qm_card_catalog::SwitchCountdownMediaIslandId(), &MediaIsland);

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

void CMenus::RenderQmHudLyricsContent(CUIRect &Content, float LineHeight, float LineSpacing, bool PrewarmOnly)
{
	// 音乐 Hook 开关：同一时间只能启用一个，点开其中一个时自动关闭其余。
	// 遍历 QmMusicHookRegistry 而非硬编码 Netease/Soda，新增 Hook 只需在注册表登记即自动覆盖。
	size_t HookCount = 0;
	const SQmMusicHookEntry *apHooks = QmMusicHookRegistry(&HookCount);
	for(size_t i = 0; i < HookCount; ++i)
	{
		const SQmMusicHookEntry &Hook = apHooks[i];
		const bool Changed = RenderQmHudCheckbox(Content, LineHeight, LineSpacing, Hook.m_pEnableConfig, Hook.m_pSettingsTextId, Localize(Hook.m_pSettingsText), Hook.m_pEnableConfig);
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
	for(size_t i = 0; i < HookCount; ++i)
	{
		if(*apHooks[i].m_pEnableConfig != 0)
		{
			if(FirstEnabled == -1)
				FirstEnabled = (int)i;
			else
				*apHooks[i].m_pEnableConfig = 0;
		}
	}
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmLyrics, "Enable lyrics", Localize("Enable lyrics"), &g_Config.m_QmLyrics);
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmLyricsInMediaIsland, "Show lyrics inside Dynamic Island", Localize("Show lyrics inside Dynamic Island"), &g_Config.m_QmLyricsInMediaIsland);
	if(g_Config.m_QmKugouHookEnable != 0)
	{
		CUIRect Row, SetupButton, RestoreButton;
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitMid(&SetupButton, &RestoreButton, LineSpacing);
		static CButtonContainer s_KugouSetup, s_KugouRestore;
		const bool Setup = DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, &s_KugouSetup, "qmclient-kugou-setup", Localize("Set up Kugou lyrics"), 0, &SetupButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE);
		const bool Restore = DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, &s_KugouRestore, "qmclient-kugou-restore", Localize("Restore Kugou files"), 0, &RestoreButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE);
		// 预热和只读布局阶段只绘制按钮，安装文件操作由 helper 再次明确确认。
		if(!PrewarmOnly && !Ui()->RenderOnly() && (Setup || Restore))
			GameClient()->m_MusicLyricsIntegration.RunKugouSetup(Restore);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}
	if(g_Config.m_QmKugouHookEnable != 0 || g_Config.m_QmQQMusicHookEnable != 0)
	{
		CUIRect Row;
		Content.HSplitTop(LineHeight, &Row, &Content);
		char aStatus[512] = {};
		GameClient()->m_MusicLyricsIntegration.GetStatus(aStatus, sizeof(aStatus));
		RenderQmHudLabel("qmclient-music-hook-status", &Row, aStatus, ui_token::font::BODY);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}
	if(g_Config.m_QmSpotifyEnable != 0)
	{
		static CLineInput s_SpotifySpDc(g_Config.m_QmSpotifySpDc, sizeof(g_Config.m_QmSpotifySpDc));
		CUIRect Row, LabelColumn, InputColumn;
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(100.0f, &LabelColumn, &InputColumn);
		RenderQmHudLabel("qmclient-lyrics-spotify-sp-dc", &LabelColumn, "spotify_ck", ui_token::font::BODY);
		s_SpotifySpDc.SetHidden(true);
		IUiContext TextInputCtx = SettingsUiContext("settings_qmclient_lyrics_spotify_text_inputs");
		ui_widget::InputField(TextInputCtx, &s_SpotifySpDc, InputColumn, Localize("Paste sp_dc from Spotify web cookies"), ui_token::font::BODY);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}
}

void CMenus::RenderQmHudSystemMediaControlsContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, bool PrewarmOnly)
{
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmSmtcEnable, "Enable system media control", Localize("Enable system media control"), &g_Config.m_QmSmtcEnable);
	if(!g_Config.m_QmSmtcEnable)
		return;

	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmSmtcShowHud, "Show song info in top-left corner", Localize("Show song info in top-left corner"), &g_Config.m_QmSmtcShowHud);
	// Hook 开关与歌词开关已迁出为独立的 qm:lyrics 卡（内容函数仍是 RenderQmHudLyricsContent，
	// 由卡片目录的 Hud 分类模块负责调用）。此处不再渲染，否则歌词与来源开关会在两处各出现一次。
	CUIRect MediaButtons, PrevButton, PlayButton, NextButton;
	Content.HSplitTop(LineHeight, &MediaButtons, &Content);
	MediaButtons.VSplitLeft((MediaButtons.w - LineSpacing * 2.0f) / 3.0f, &PrevButton, &MediaButtons);
	MediaButtons.VSplitLeft(LineSpacing, nullptr, &MediaButtons);
	MediaButtons.VSplitLeft((MediaButtons.w - LineSpacing) / 2.0f, &PlayButton, &MediaButtons);
	MediaButtons.VSplitLeft(LineSpacing, nullptr, &MediaButtons);
	NextButton = MediaButtons;

	static CButtonContainer s_SmtcPrev;
	if(DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, &s_SmtcPrev, "qmclient-smtc-previous", Localize("Previous"), 0, &PrevButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE))
		GameClient()->m_SystemMediaControls.Previous();
	static CButtonContainer s_SmtcPlayPause;
	if(DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, &s_SmtcPlayPause, "qmclient-smtc-play-pause", Localize("Play/Pause"), 0, &PlayButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE))
		GameClient()->m_SystemMediaControls.PlayPause();
	static CButtonContainer s_SmtcNext;
	if(DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, &s_SmtcNext, "qmclient-smtc-next", Localize("Next"), 0, &NextButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE))
		GameClient()->m_SystemMediaControls.Next();
	Content.HSplitTop(LineSpacing, nullptr, &Content);
}

void CMenus::RenderQmHudNotificationsBasicContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)
{
	const float LineHeight = Metrics.m_LineHeight;
	const float BodySize = Metrics.m_BodySize;
	const float LineSpacing = Metrics.m_LineSpacing;
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsSystem, "Show important server prompts as notifications", Localize("Show important server prompts as notifications"), &g_Config.m_QmHudNotificationsSystem);
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsEcho, "Route Echo messages to notifications", Localize("Route Echo messages to notifications"), &g_Config.m_QmHudNotificationsEcho);
	CUIRect Row, LabelColumn, ControlColumn;
	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		RenderQmHudLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(pInputId, &Row, pValue, &LabelColumn);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	static int s_QmHudNotificationHoldInputId;
	static int s_QmHudNotificationTextSizeInputId;
	RenderValue("qmclient-notifications-hold-time", "Notification hold time", &s_QmHudNotificationHoldInputId, &g_Config.m_QmHudNotificationsHoldMs, 500, 10000, "ms");
	RenderValue("qmclient-notifications-text-size", "Notification text size", &s_QmHudNotificationTextSizeInputId, &g_Config.m_QmHudNotificationsTextSize, 1, 24);
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsShowAdvanced, "Advanced options", Localize("Advanced options"), &g_Config.m_QmHudNotificationsShowAdvanced);
}

void CMenus::RenderQmHudNotificationsAdvancedContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)
{
	const float LineHeight = Metrics.m_LineHeight;
	const float BodySize = Metrics.m_BodySize;
	const float LineSpacing = Metrics.m_LineSpacing;
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsUseCategoryFilters, "Use notification category filters", Localize("Use notification category filters"), &g_Config.m_QmHudNotificationsUseCategoryFilters);
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
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsCompatSolo, "Detect compatible solo prompts from custom servers", Localize("Detect compatible solo prompts from custom servers"), &g_Config.m_QmHudNotificationsCompatSolo);

	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(Metrics.m_SmallSize, &Row, &Content);
	TextRender()->TextColor(ColorRGBA(0.9f, 0.9f, 0.9f, 0.8f));
	RenderQmHudLabel("qmclient-notifications-basic-info-note", &Row, Localize("Join, version, rules, and help messages stay in chat instead of popups"), Metrics.m_SmallSize);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	static CButtonContainer s_QmHudNotificationBgColorId;
	DoLine_ColorPicker(&s_QmHudNotificationBgColorId, Metrics, &Content, Localize("Notification background"), &g_Config.m_QmHudNotificationsBgColor, ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f), false, nullptr, true);
	static CButtonContainer s_QmHudNotificationTextColorId;
	DoLine_ColorPicker(&s_QmHudNotificationTextColorId, Metrics, &Content, Localize("System prompt text color"), &g_Config.m_QmHudNotificationsTextColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), false, nullptr, true);
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsEchoInheritColor, "Echo follows the original chat color", Localize("Echo follows the original chat color"), &g_Config.m_QmHudNotificationsEchoInheritColor);
	static CButtonContainer s_QmHudNotificationEchoTextColorId;
	DoLine_ColorPicker(&s_QmHudNotificationEchoTextColorId, Metrics, &Content, Localize("Echo text color when not inheriting chat color"), &g_Config.m_QmHudNotificationsEchoTextColor, ColorRGBA(0.5f, 0.78f, 1.0f, 1.0f), false, nullptr, true);

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	RenderQmHudLabel("qmclient-notifications-popup-animation", &LabelColumn, Localize("Popup animation"), BodySize);
	const char *apHudNotificationAnimDropDownNames[] = {Localize("Fade and slide"), Localize("Fade only"), Localize("No animation")};
	static CUi::SDropDownState s_HudNotificationAnimDropDownState;
	static CScrollRegion s_HudNotificationAnimDropDownScrollRegion;
	s_HudNotificationAnimDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_HudNotificationAnimDropDownScrollRegion;
	const int AnimSelectedNew = DoSettingsDropDown(&ControlColumn, g_Config.m_QmHudNotificationsAnimType, apHudNotificationAnimDropDownNames, std::size(apHudNotificationAnimDropDownNames), s_HudNotificationAnimDropDownState, {}, &g_Config.m_QmHudNotificationsAnimType, nullptr, &Row);
	if(g_Config.m_QmHudNotificationsAnimType != AnimSelectedNew)
		g_Config.m_QmHudNotificationsAnimType = AnimSelectedNew;
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		RenderQmHudLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(pInputId, &Row, pValue, &LabelColumn);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
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

	Content.HSplitTop(LineHeight, &Row, &Content);
	DoQmSettingsCheckboxAuto(&g_Config.m_QmNameplateCoordsOwn, "Show own coordinates", Localize("Show own coordinates"), &g_Config.m_QmNameplateCoordsOwn, &Row, LineHeight);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	Content.HSplitTop(LineHeight, &Row, &Content);
	DoQmSettingsCheckboxAuto(&g_Config.m_QmNameplateCoords, "Show other players' coordinates", Localize("Show other players' coordinates"), &g_Config.m_QmNameplateCoords, &Row, LineHeight);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	Content.HSplitTop(LineHeight, &Row, &Content);
	DoQmSettingsCheckboxAuto(&g_Config.m_QmNameplateCoordX, "Show X", Localize("Show X"), &g_Config.m_QmNameplateCoordX, &Row, LineHeight);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	Content.HSplitTop(LineHeight, &Row, &Content);
	DoQmSettingsCheckboxAuto(&g_Config.m_QmNameplateCoordY, "Show Y", Localize("Show Y"), &g_Config.m_QmNameplateCoordY, &Row, LineHeight);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	Content.HSplitTop(LineHeight, &Row, &Content);
	DoQmSettingsCheckboxAuto(&g_Config.m_QmNameplateCoordXAlignHint, "X alignment hint with me", Localize("X alignment hint with me"), &g_Config.m_QmNameplateCoordXAlignHint, &Row, LineHeight);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	Content.HSplitTop(LineHeight, &Row, &Content);
	DoQmSettingsCheckboxAuto(&g_Config.m_QmNameplateCoordXAlignHintStrict, "Strict mode", Localize("Strict mode"), &g_Config.m_QmNameplateCoordXAlignHintStrict, &Row, LineHeight);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
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
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	static CButtonContainer s_CoordXAlignHintColorId;
	DoLine_ColorPicker(&s_CoordXAlignHintColorId, Metrics, &Content, Localize("X alignment color"), &g_Config.m_QmNameplateCoordXAlignHintColor, ColorRGBA(1.0f, 0.82f, 0.2f, 1.0f), false, nullptr, false, false);
}

void CMenus::RenderQmHudGoresDrownBoardContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	// 行序与卡片目录的预布局输入一致：总开关、显示人数、不透明度、显示玩家 Tee。
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmGoresDrownBoard, "Show Gores drown board", Localize("Show Gores drown board"), &g_Config.m_QmGoresDrownBoard);
	if(!g_Config.m_QmGoresDrownBoard)
		return;

	CUIRect Row, LabelColumn, ControlColumn;
	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		RenderQmHudLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(pInputId, &Row, pValue, &LabelColumn);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	static int s_QmGoresDrownBoardMaxPlayersInputId;
	static int s_QmGoresDrownBoardOpacityInputId;
	RenderValue("qmclient-gores-drown-board-max-players", "Players shown", &s_QmGoresDrownBoardMaxPlayersInputId, &g_Config.m_QmGoresDrownBoardMaxPlayers, 1, 16);
	RenderValue("qmclient-gores-drown-board-opacity", "Card opacity", &s_QmGoresDrownBoardOpacityInputId, &g_Config.m_QmGoresDrownBoardOpacity, 0, 100, "%");
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmGoresDrownBoardShowTee, "Show player Tee", Localize("Show player Tee"), &g_Config.m_QmGoresDrownBoardShowTee);
}
