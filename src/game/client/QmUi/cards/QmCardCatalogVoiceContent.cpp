#include "QmCardCatalogInternal.h"

#include <base/lock.h>
#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/client/audio_sdl3.h>
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

namespace
{
	CUi::SDropDownState s_VoiceInputDeviceDropDownState;
	CUi::SDropDownState s_VoiceOutputDeviceDropDownState;
	CUi::SDropDownState s_VoiceBitrateProfileDropDownState;
	CUi::SDropDownState s_VoiceNoiseSuppressModeDropDownState;
	void LogQmPerfStage(IClient *pClient, const char *pStage, double DurationMs, bool Force, const char *pExtra)
	{
		if(g_Config.m_QmPerfDebug)
			QmPerfLogStage("perf/qmclient", pStage, DurationMs, Force, pClient, nullptr, nullptr, pExtra);
	}
}

bool qm_card_catalog::VoiceHasPendingInput()
{
	return s_VoiceInputDeviceDropDownState.m_SelectionPopupContext.m_SelectionIndex >= 0 ||
	       s_VoiceOutputDeviceDropDownState.m_SelectionPopupContext.m_SelectionIndex >= 0 ||
	       s_VoiceBitrateProfileDropDownState.m_SelectionPopupContext.m_SelectionIndex >= 0 ||
	       s_VoiceNoiseSuppressModeDropDownState.m_SelectionPopupContext.m_SelectionIndex >= 0;
}

void CMenus::RenderQmHudVoiceContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const float LineHeight = Metrics.m_LineHeight;
	const float BodySize = Metrics.m_BodySize;
	const float LineSpacing = Metrics.m_LineSpacing;
	const float UiScale = Metrics.m_UiScale;
	CUIRect Row, LabelCol, ControlCol;
	auto DoQmSettingsCheckboxAuto = [this, &Flow, ReadOnly](const void *pId, const char *pTextId, const char *pText, int *pValue, CUIRect *pRect, float) {
		Ui()->DoConfigTooltip(pId, pRect, pValue);
		const bool Changed = DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, pId, pTextId, pText, *pValue, pRect) != 0;
		if(Changed && Flow.Input() && !ReadOnly)
			*pValue ^= 1;
		return Changed;
	};
	auto DoQmSettingsLabel = [this](const char *pTextId, CUIRect *pRect, const char *pText, float FontSize) {
		RenderQmHudLabel(pTextId, pRect, pText, FontSize);
	};
	auto DoQmSettingsMenuButton = [this](CButtonContainer *pButton, const char *pTextId, const char *pText, const CUIRect *pRect) {
		return DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, pButton, pTextId, pText, 0, pRect);
	};
	// 刷新这类动作只放图标，说明走悬浮提示，文字按钮在窄列里太占地方。
	auto DoQmSettingsIconButton = [this](CButtonContainer *pButton, const char *pTextId, EQmIcon Icon, const char *pIcon, const char *pTooltip, const CUIRect *pRect) {
		const bool Clicked = Ui()->DoButton_QmIcon(pButton, Icon, pIcon, 0, pRect, IGraphics::CORNER_ALL) != 0;
		GameClient()->m_Tooltips.DoToolTip(pButton, pRect, pTooltip);
		return Clicked;
	};
	auto RenderSliderWithValueInput = [this, PrewarmOnly](const void *pId, const CUIRect &ControlColumn, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		RenderQmSettingsSliderWithValueInput(pId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
	};
	IUiContext QmClientVoiceTextInputCtx;
	QmClientVoiceTextInputCtx.m_pUi = Ui();
	QmClientVoiceTextInputCtx.m_pAnim = PrewarmOnly || Ui()->RenderOnly() ? nullptr : &GameClient()->UiRuntimeV2()->AnimRuntime();
	QmClientVoiceTextInputCtx.m_pTree = PrewarmOnly || Ui()->RenderOnly() ? nullptr : &GameClient()->UiRuntimeV2()->Tree();
	QmClientVoiceTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_qmclient_voice_text_inputs");
	QmClientVoiceTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();

	if(Flow.Control("voice-enabled", LineHeight, Row))
		DoQmSettingsCheckboxAuto(&g_Config.m_QmVoiceEnable, "Enable voice", Localize("Enable voice"), &g_Config.m_QmVoiceEnable, &Row, LineHeight);
	if(g_Config.m_QmVoiceEnable)
		Flow.Space(LineSpacing);

	if(g_Config.m_QmVoiceEnable)
	{
		auto AddVoiceSectionLabel = [&](const char *pTitle, const char *pHint) {
			const auto Measure = [this](const char *pText, float Width, float FontSize) { return TextRender()->TextBoundingBox(FontSize, pText, -1, Width).m_H + FontSize * 0.25f; };
			const auto Draw = [this](float FontSize) {
				return [this, FontSize](const CUIRect &Rect, const char *pText) {
					SLabelProperties Props;
					Props.m_MaxWidth = Rect.w;
					Ui()->DoLabel(&Rect, pText, FontSize, TEXTALIGN_ML, Props);
				};
			};
			Flow.Text("voice-status-title", pTitle, 0, Metrics.m_HeadlineSize, 0, Measure, Draw(Metrics.m_HeadlineSize), LineHeight * 0.78f);
			if(pHint && pHint[0])
				Flow.Text("voice-status-hint", pHint, 0, Metrics.m_SmallSize, 0, Measure, Draw(Metrics.m_SmallSize), LineHeight * 0.68f);
			Flow.Space(LineSpacing * 0.75f);
		};

		const bool NeedVoiceDiagnostics = g_Config.m_QmVoiceShowAdvanced && g_Config.m_QmVoiceShowConnectionStatus;
		VoiceUtils::SVoiceUiStatus VoiceUiStatus{};
		if(NeedVoiceDiagnostics)
			GameClient()->m_Voice.Voice().ExportUiStatus(VoiceUiStatus);
		auto LocalizeVoiceUiMicStatus = [&](const VoiceUtils::SVoiceUiStatus &Status) {
			const char *pState = VoiceUtils::VoiceUiMicStatus(Status);
			if(str_comp(pState, "muted") == 0)
				return Localize("Muted");
			if(str_comp(pState, "unavailable") == 0)
				return Localize("Not open, check input device or mic permission");
			if(str_comp(pState, "ready") == 0)
				return Localize("Opened");
			if(str_comp(pState, "waiting") == 0)
				return Localize("Waiting to open");
			return Localize("Not enabled");
		};
		auto LocalizeVoiceUiOutputStatus = [&](const VoiceUtils::SVoiceUiStatus &Status) {
			const char *pState = VoiceUtils::VoiceUiOutputStatus(Status);
			if(str_comp(pState, "unavailable") == 0)
				return Localize("Not open, check output device");
			if(str_comp(pState, "ready") == 0)
				return Localize("Opened");
			if(str_comp(pState, "waiting") == 0)
				return Localize("Waiting to open");
			return Localize("Not enabled");
		};
		auto LocalizeVoiceUiServerStatus = [&](const VoiceUtils::SVoiceUiStatus &Status, char *pBuf, size_t BufSize) {
			const char *pState = VoiceUtils::VoiceUiServerStatus(Status);
			if(str_comp(pState, "local_test") == 0)
				str_copy(pBuf, Localize("Local test mode, no server needed"), BufSize);
			else if(str_comp(pState, "offline") == 0)
				str_copy(pBuf, Localize("Not connected to server"), BufSize);
			else if(str_comp(pState, "resolving") == 0)
				str_copy(pBuf, Localize("Parsing voice server address"), BufSize);
			else if(str_comp(pState, "socket_error") == 0)
				str_copy(pBuf, Localize("UDP socket not open"), BufSize);
			else if(str_comp(pState, "connected") == 0)
				str_format(pBuf, BufSize, "%s (%d ms)", Localize("Connected"), maximum(Status.m_PingMs, 0));
			else if(str_comp(pState, "connected_no_ping") == 0)
				str_copy(pBuf, Localize("Connected, waiting for first ping"), BufSize);
			else
				str_copy(pBuf, Localize("Unknown status"), BufSize);
		};
		auto LocalizeVoiceUiRoomStatus = [&](const VoiceUtils::SVoiceUiStatus &Status, char *pBuf, size_t BufSize) {
			const char *pState = VoiceUtils::VoiceUiRoomStatus(Status);
			if(str_comp(pState, "local_test") == 0)
				str_copy(pBuf, Localize("Local test mode"), BufSize);
			else if(str_comp(pState, "offline") == 0)
				str_copy(pBuf, Localize("Not connected to server"), BufSize);
			else if(str_comp(pState, "matched") == 0)
				str_format(pBuf, BufSize, "%s (%d)", Localize("Matched with callable peer"), Status.m_ActivePeerCount);
			else if(str_comp(pState, "waiting_peer") == 0)
				str_copy(pBuf, Localize("No callable peer found"), BufSize);
			else
				str_copy(pBuf, Localize("Unknown status"), BufSize);
		};
		auto LocalizeVoiceUiTransportStatus = [&](const VoiceUtils::SVoiceUiStatus &Status) {
			const char *pState = VoiceUtils::VoiceUiTransportStatus(Status);
			if(str_comp(pState, "tx_rx_active") == 0)
				return Localize("Sending and receiving");
			if(str_comp(pState, "tx_active") == 0)
				return Localize("Sending, waiting for peer echo");
			if(str_comp(pState, "rx_active") == 0)
				return Localize("Receiving");
			if(str_comp(pState, "idle_with_peer") == 0)
				return Localize("Connected, no one is speaking");
			if(str_comp(pState, "idle_no_peer") == 0)
				return Localize("No peer");
			return Localize("Not enabled");
		};
		auto LocalizeVoiceUiInputRouteStatus = [&](const VoiceUtils::SVoiceUiStatus &Status, char *pBuf, size_t BufSize) {
			const char *pState = VoiceUtils::VoiceUiInputRouteStatus(Status);
			const char *pRequested = Status.m_aRequestedInputDevice[0] != '\0' ? Status.m_aRequestedInputDevice : Localize("Default");
			const char *pResolved = Status.m_aResolvedInputDevice[0] != '\0' ? Status.m_aResolvedInputDevice : Localize("System default");
			if(str_comp(pState, "using_selected") == 0)
				str_format(pBuf, BufSize, "%s: %s", Localize("Switched to"), pRequested);
			else if(str_comp(pState, "using_default") == 0)
				str_format(pBuf, BufSize, "%s (%s)", Localize("Use default input"), pResolved);
			else if(str_comp(pState, "switching_selected") == 0)
				str_format(pBuf, BufSize, "%s: %s", Localize("Switching"), pRequested);
			else if(str_comp(pState, "switching_default") == 0)
				str_copy(pBuf, Localize("Switching back to default input"), BufSize);
			else if(str_comp(pState, "permission_denied") == 0)
				str_copy(pBuf, Localize("Microphone permission denied by system"), BufSize);
			else if(str_comp(pState, "selected_failed") == 0)
				str_format(pBuf, BufSize, "%s: %s", Localize("Switch failed"), pRequested);
			else if(str_comp(pState, "default_failed") == 0)
				str_copy(pBuf, Localize("Default input open failed"), BufSize);
			else if(str_comp(pState, "waiting") == 0)
				str_copy(pBuf, Localize("Waiting to open input device"), BufSize);
			else
				str_copy(pBuf, Localize("Not enabled"), BufSize);
		};
		auto LocalizeVoiceUiOutputRouteStatus = [&](const VoiceUtils::SVoiceUiStatus &Status, char *pBuf, size_t BufSize) {
			const char *pState = VoiceUtils::VoiceUiOutputRouteStatus(Status);
			const char *pRequested = Status.m_aRequestedOutputDevice[0] != '\0' ? Status.m_aRequestedOutputDevice : Localize("Default");
			const char *pResolved = Status.m_aResolvedOutputDevice[0] != '\0' ? Status.m_aResolvedOutputDevice : Localize("System default");
			if(str_comp(pState, "using_selected") == 0)
				str_format(pBuf, BufSize, "%s: %s", Localize("Switched to"), pRequested);
			else if(str_comp(pState, "using_default") == 0)
				str_format(pBuf, BufSize, "%s (%s)", Localize("Use default output"), pResolved);
			else if(str_comp(pState, "switching_selected") == 0)
				str_format(pBuf, BufSize, "%s: %s", Localize("Switching"), pRequested);
			else if(str_comp(pState, "switching_default") == 0)
				str_copy(pBuf, Localize("Switching back to default output"), BufSize);
			else if(str_comp(pState, "selected_failed") == 0)
				str_format(pBuf, BufSize, "%s: %s", Localize("Switch failed"), pRequested);
			else if(str_comp(pState, "default_failed") == 0)
				str_copy(pBuf, Localize("Default output open failed"), BufSize);
			else if(str_comp(pState, "waiting") == 0)
				str_copy(pBuf, Localize("Waiting to open output device"), BufSize);
			else
				str_copy(pBuf, Localize("Not enabled"), BufSize);
		};
		auto LocalizeVoiceUiAudioIssue = [&](const VoiceUtils::SVoiceUiStatus &Status) {
			const char *pIssue = VoiceUtils::VoiceUiAudioIssueKey(Status);
			if(str_comp(pIssue, "none") == 0)
				return Localize("No audio issues detected");
			if(str_comp(pIssue, "input_device_not_found") == 0)
				return Localize("Input device not found");
			if(str_comp(pIssue, "output_device_not_found") == 0)
				return Localize("Output device not found");
			if(str_comp(pIssue, "no_capture_devices") == 0)
				return Localize("No input device available");
			if(str_comp(pIssue, "no_output_devices") == 0)
				return Localize("No output device available");
			if(str_comp(pIssue, "open_capture_failed") == 0)
				return Localize("Input device open failed");
			if(str_comp(pIssue, "open_output_failed") == 0)
				return Localize("Output device open failed");
			if(str_comp(pIssue, "permission_denied") == 0)
				return Localize("Microphone permission denied by system");
			if(str_comp(pIssue, "backend_init_failed") == 0)
				return Localize("Audio backend init failed");
			return Localize("Unclassified audio issue");
		};
		auto RenderVoiceStatusRow = [&](const char *pId, const char *pTitle, const char *pValue) {
			const auto Columns = ResolveSettingsControlRowLayout(Content, Metrics);
			const float PrimaryFraction = Content.w > 0 ? (Columns.m_ControlRect.x - Content.x) / Content.w : 0;
			const float Gap = Columns.m_ControlRect.x - Columns.m_LabelRect.x - Columns.m_LabelRect.w;
			Flow.TextColumns(pId, pTitle, pValue, BodySize, LineHeight, PrimaryFraction, Gap, LineSpacing * 0.75f, [this](const char *pText, float Width, float Size) { return TextRender()->TextBoundingBox(Size, pText, -1, Width).m_H; }, [&](const CUIRect &Column, const char *pText) {
				SLabelProperties Props;
				Props.m_MaxWidth = Column.w;
				Ui()->DoLabel(&Column, pText, Column.x == Content.x ? BodySize : Metrics.m_SmallSize, TEXTALIGN_ML, Props); });
		};

		auto LocalizeVoiceUiActionHint = [&](const VoiceUtils::SVoiceUiStatus &Status) {
			const char *pHint = VoiceUtils::VoiceUiActionHint(Status);
			if(str_comp(pHint, "select_input_device") == 0)
				return Localize("Try reselecting input device, confirm default mic or headset mic is online");
			if(str_comp(pHint, "select_output_device") == 0)
				return Localize("Try reselecting output device, confirm headphones/speakers are online");
			if(str_comp(pHint, "retry_input_open") == 0)
				return Localize("Input device open failed, try reconnecting headset/mic or reselecting input device");
			if(str_comp(pHint, "retry_output_open") == 0)
				return Localize("Output device open failed, try reconnecting speakers/headphones or reselecting output device");
			if(str_comp(pHint, "grant_mic_permission") == 0)
				return Localize("Allow mic permission in system settings, then reopen voice");
			if(str_comp(pHint, "check_audio_backend") == 0)
				return Localize("Audio backend init failed, try switching devices and check details");
			if(str_comp(pHint, "inspect_audio_log") == 0)
				return Localize("Audio init failed, check details below and logs");
			if(str_comp(pHint, "check_input") == 0)
				return Localize("Check input device, system default mic, and mic permission first");
			if(str_comp(pHint, "check_output") == 0)
				return Localize("Check output device, confirm headphones/speakers are still online");
			if(str_comp(pHint, "join_server") == 0)
				return Localize("Connect to server first to establish voice network link");
			if(str_comp(pHint, "check_server") == 0)
				return Localize("Check if voice server address is reachable");
			if(str_comp(pHint, "wait_connection") == 0)
				return Localize("Waiting for the voice WebSocket connection");
			if(str_comp(pHint, "retry_socket") == 0)
				return Localize("Try toggling voice or reconnecting to server");
			if(str_comp(pHint, "check_room") == 0)
				return Localize("Confirm both are on same server, same room, and support voice");
			if(str_comp(pHint, "wait_peer") == 0)
				return Localize("Sending locally, suggest the other party unmute or confirm they can receive");
			if(str_comp(pHint, "enable_voice") == 0)
				return Localize("Please enable voice first");
			return Localize("Status normal, check details below if still experiencing issues");
		};

		char aVoiceServerStatus[128]{};
		char aVoiceRoomStatus[128]{};
		char aVoiceTransportStatus[128]{};
		char aVoiceTransportDetail[160]{};
		char aVoiceInputRouteStatus[160]{};
		char aVoiceOutputRouteStatus[160]{};
		if(NeedVoiceDiagnostics)
		{
			LocalizeVoiceUiServerStatus(VoiceUiStatus, aVoiceServerStatus, sizeof(aVoiceServerStatus));
			LocalizeVoiceUiRoomStatus(VoiceUiStatus, aVoiceRoomStatus, sizeof(aVoiceRoomStatus));
			LocalizeVoiceUiInputRouteStatus(VoiceUiStatus, aVoiceInputRouteStatus, sizeof(aVoiceInputRouteStatus));
			LocalizeVoiceUiOutputRouteStatus(VoiceUiStatus, aVoiceOutputRouteStatus, sizeof(aVoiceOutputRouteStatus));
			str_copy(aVoiceTransportStatus, LocalizeVoiceUiTransportStatus(VoiceUiStatus), sizeof(aVoiceTransportStatus));
			if(VoiceUiStatus.m_TxAgeMs >= 0 || VoiceUiStatus.m_RxAgeMs >= 0)
			{
				str_format(aVoiceTransportDetail, sizeof(aVoiceTransportDetail), "%s: tx=%dms rx=%dms mic=%.0f%%",
					aVoiceTransportStatus,
					VoiceUiStatus.m_TxAgeMs,
					VoiceUiStatus.m_RxAgeMs,
					(double)std::clamp(VoiceUiStatus.m_MicLevel * 100.0f, 0.0f, 100.0f));
			}
			else
			{
				str_copy(aVoiceTransportDetail, aVoiceTransportStatus, sizeof(aVoiceTransportDetail));
			}
		}

		if(Flow.Control("voice-room-password", LineHeight, Row))
		{
			SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelCol, &ControlCol);
			DoQmSettingsLabel("qmclient-voice-room-password", &LabelCol, Localize("Room password"), BodySize);
			static CLineInput s_VoiceToken(g_Config.m_QmVoiceToken, sizeof(g_Config.m_QmVoiceToken));
			static ui_widget::SPasswordFieldState s_VoiceTokenState;
			ui_widget::SPasswordFieldOptions VoiceTokenOptions;
			VoiceTokenOptions.m_pPlaceholder = Localize("Leave empty to join public room");
			VoiceTokenOptions.m_FontSize = BodySize;
			ui_widget::PasswordField(QmClientVoiceTextInputCtx, &s_VoiceToken, ControlCol, &s_VoiceTokenState, VoiceTokenOptions);
		}
		Flow.Space(LineSpacing);

		if(Flow.Control("voice-mic-muted", LineHeight, Row))
		{
			DoQmSettingsCheckboxAuto(&g_Config.m_QmVoiceMicMute, "Mute microphone", Localize("Mute microphone"), &g_Config.m_QmVoiceMicMute, &Row, LineHeight);
		}
		Flow.Space(LineSpacing);

		if(Flow.Control("voice-mic-volume", LineHeight, Row))
		{
			{
				CUIRect LabelColValue, ControlColValue;
				SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColValue, &ControlColValue);
				DoQmSettingsLabel("qmclient-voice-microphone-volume", &LabelColValue, Localize("Microphone volume"), BodySize);
				static int s_QmVoiceMicVolumeInputId;
				RenderSliderWithValueInput(&s_QmVoiceMicVolumeInputId, ControlColValue, &g_Config.m_QmVoiceMicVolume, 0, 300, "%");
			}
		}
		Flow.Space(LineSpacing);

		if(Flow.Control("voice-vad", LineHeight, Row))
		{
			DoQmSettingsCheckboxAuto(&g_Config.m_QmVoiceVadEnable, "Auto unmute when speaking", Localize("Auto unmute when speaking"), &g_Config.m_QmVoiceVadEnable, &Row, LineHeight);
		}
		Flow.Space(LineSpacing);

		if(Flow.Control("voice-advanced", LineHeight, Row))
		{
			DoQmSettingsCheckboxAuto(&g_Config.m_QmVoiceShowAdvanced, "Advanced options", Localize("Advanced options"), &g_Config.m_QmVoiceShowAdvanced, &Row, LineHeight);
		}
		if(g_Config.m_QmVoiceShowAdvanced)
			Flow.Space(LineSpacing);

		if(g_Config.m_QmVoiceShowAdvanced)
		{
			if(Flow.Control("voice-connection-status", LineHeight, Row))
			{
				DoQmSettingsCheckboxAuto(&g_Config.m_QmVoiceShowConnectionStatus, "Show voice connection status", Localize("Show voice connection status"), &g_Config.m_QmVoiceShowConnectionStatus, &Row, LineHeight);
			}
			Flow.Space(LineSpacing);

			if(g_Config.m_QmVoiceShowConnectionStatus)
			{
				AddVoiceSectionLabel(Localize("Current status"), Localize("Start here to quickly diagnose if the issue is with device, server, or room"));
				RenderVoiceStatusRow("voice-status-mic", Localize("Microphone"), LocalizeVoiceUiMicStatus(VoiceUiStatus));
				RenderVoiceStatusRow("voice-status-speaker", Localize("Speaker"), LocalizeVoiceUiOutputStatus(VoiceUiStatus));
				RenderVoiceStatusRow("voice-status-input-route", Localize("Input switch"), aVoiceInputRouteStatus);
				RenderVoiceStatusRow("voice-status-output-route", Localize("Output switch"), aVoiceOutputRouteStatus);
				RenderVoiceStatusRow("voice-status-server", Localize("Server"), aVoiceServerStatus);
				RenderVoiceStatusRow("voice-status-room", Localize("Room"), aVoiceRoomStatus);
				RenderVoiceStatusRow("voice-status-transport", Localize("Send & Receive"), aVoiceTransportDetail);
				RenderVoiceStatusRow("voice-status-suggestion", Localize("Troubleshooting suggestions"), LocalizeVoiceUiActionHint(VoiceUiStatus));
				RenderVoiceStatusRow("voice-status-audio-issue", Localize("Audio issue"), LocalizeVoiceUiAudioIssue(VoiceUiStatus));
				const char *pPrimaryError = VoiceUtils::VoiceUiPrimaryError(VoiceUiStatus);
				RenderVoiceStatusRow("voice-status-reason", Localize("Detailed reason"), pPrimaryError[0] != '\0' ? pPrimaryError : Localize("No audio issues detected"));
				Flow.Space(LineSpacing * 0.5f);
			}

			if(Flow.Control("voice-server-url", LineHeight, Row))
			{
				SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelCol, &ControlCol);
				DoQmSettingsLabel("qmclient-voice-server-ip", &LabelCol, Localize("Server URL"), BodySize);
				static CLineInput s_VoiceServer(g_Config.m_QmVoiceServer, sizeof(g_Config.m_QmVoiceServer));
				if(!PrewarmOnly && !Ui()->RenderOnly())
					s_VoiceServer.SetEmptyText("wss://qmclient.icu/ws/voice");
				ui_widget::InputField(QmClientVoiceTextInputCtx, &s_VoiceServer, ControlCol, "wss://qmclient.icu/ws/voice", BodySize);
			}
			Flow.Space(LineSpacing);

			if(Flow.Control("voice-input-device", LineHeight, Row))
			{
				SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelCol, &ControlCol);
				DoQmSettingsLabel("qmclient-voice-input-device", &LabelCol, Localize("Input device"), BodySize);
				static std::vector<std::string> s_VoiceInputDeviceDisplayNames;
				static std::vector<std::string> s_VoiceInputDeviceConfigValues;
				static std::vector<const char *> s_VoiceInputDeviceDropDownNames;
				static std::vector<VoiceUtils::SVoiceDeviceDropdownEntry> s_VoiceInputDeviceEntries;
				static CScrollRegion s_VoiceInputDeviceDropDownScrollRegion;
				static bool s_VoiceInputDevicesInitialized = false;
				s_VoiceInputDeviceDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_VoiceInputDeviceDropDownScrollRegion;
				auto RefreshVoiceInputDeviceList = [&]() {
					CPerfTimer StageTimer;
					s_VoiceInputDeviceDisplayNames.clear();
					s_VoiceInputDeviceConfigValues.clear();
					s_VoiceInputDeviceDropDownNames.clear();
					std::vector<std::string> vDetectedDeviceNames;
					for(const auto &Device : SdlAudioDevices(true))
						vDetectedDeviceNames.emplace_back(Device.m_Name);

					VoiceUtils::BuildVoiceDeviceDropdownEntries(
						vDetectedDeviceNames,
						g_Config.m_QmVoiceInputDevice,
						Localize("Default"),
						Localize("Disconnected"),
						s_VoiceInputDeviceEntries);

					s_VoiceInputDeviceDisplayNames.reserve(s_VoiceInputDeviceEntries.size());
					s_VoiceInputDeviceConfigValues.reserve(s_VoiceInputDeviceEntries.size());
					s_VoiceInputDeviceDropDownNames.reserve(s_VoiceInputDeviceDisplayNames.size());
					for(const auto &Entry : s_VoiceInputDeviceEntries)
					{
						s_VoiceInputDeviceDisplayNames.push_back(Entry.m_DisplayName);
						s_VoiceInputDeviceConfigValues.push_back(Entry.m_ConfigValue);
						s_VoiceInputDeviceDropDownNames.push_back(s_VoiceInputDeviceDisplayNames.back().c_str());
					}

					char aVoiceExtra[96];
					str_format(aVoiceExtra, sizeof(aVoiceExtra), "devices=%d", (int)s_VoiceInputDeviceDisplayNames.size());
					LogQmPerfStage(Client(), "voice_device_enum", StageTimer.ElapsedMs(), false, aVoiceExtra);
				};
				if(Flow.Input() && !ReadOnly && !s_VoiceInputDevicesInitialized)
				{
					RefreshVoiceInputDeviceList();
					s_VoiceInputDevicesInitialized = true;
				}

				CUIRect VoiceInputDropDownRect;
				CUIRect VoiceInputRefreshButton;
				ControlCol.VSplitRight(LineHeight, &VoiceInputDropDownRect, &VoiceInputRefreshButton);

				static CButtonContainer s_VoiceInputRefreshButton;
				if(s_VoiceInputDeviceEntries.empty())
				{
					DoQmSettingsLabel("qmclient-voice-input-default", &VoiceInputDropDownRect, Localize("Default"), BodySize);
					DoQmSettingsIconButton(&s_VoiceInputRefreshButton, "qmclient-voice-input-refresh", EQmIcon::ARROW_ROTATE_RIGHT, "\xEE\x80\xB6", Localize("Refresh"), &VoiceInputRefreshButton);
				}
				else
				{
					const int VoiceInputSelectedOld = VoiceUtils::VoiceFindSelectedDeviceIndex(s_VoiceInputDeviceEntries, g_Config.m_QmVoiceInputDevice);
					const int VoiceInputSelectedNew = DoSettingsDropDown(&VoiceInputDropDownRect, VoiceInputSelectedOld, s_VoiceInputDeviceDropDownNames.data(), s_VoiceInputDeviceDropDownNames.size(), s_VoiceInputDeviceDropDownState, {}, g_Config.m_QmVoiceInputDevice, nullptr, &Row);
					if(Flow.Input() && !ReadOnly && VoiceInputSelectedNew >= 0 && VoiceInputSelectedNew != VoiceInputSelectedOld && (size_t)VoiceInputSelectedNew < s_VoiceInputDeviceConfigValues.size())
						str_copy(g_Config.m_QmVoiceInputDevice, s_VoiceInputDeviceConfigValues[VoiceInputSelectedNew].c_str(), sizeof(g_Config.m_QmVoiceInputDevice));
					if(DoQmSettingsIconButton(&s_VoiceInputRefreshButton, "qmclient-voice-input-refresh", EQmIcon::ARROW_ROTATE_RIGHT, "\xEE\x80\xB6", Localize("Refresh"), &VoiceInputRefreshButton) && Flow.Input() && !ReadOnly)
						RefreshVoiceInputDeviceList();
				}
			}
			Flow.Space(LineSpacing);

			if(Flow.Control("voice-output-device", LineHeight, Row))
			{
				SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelCol, &ControlCol);
				DoQmSettingsLabel("qmclient-voice-output-device", &LabelCol, Localize("Output device"), BodySize);
				static std::vector<std::string> s_VoiceOutputDeviceDisplayNames;
				static std::vector<std::string> s_VoiceOutputDeviceConfigValues;
				static std::vector<const char *> s_VoiceOutputDeviceDropDownNames;
				static std::vector<VoiceUtils::SVoiceDeviceDropdownEntry> s_VoiceOutputDeviceEntries;
				static CScrollRegion s_VoiceOutputDeviceDropDownScrollRegion;
				static bool s_VoiceOutputDevicesInitialized = false;
				s_VoiceOutputDeviceDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_VoiceOutputDeviceDropDownScrollRegion;
				auto RefreshVoiceOutputDeviceList = [&]() {
					CPerfTimer StageTimer;
					s_VoiceOutputDeviceDisplayNames.clear();
					s_VoiceOutputDeviceConfigValues.clear();
					s_VoiceOutputDeviceDropDownNames.clear();
					std::vector<std::string> vDetectedDeviceNames;
					for(const auto &Device : SdlAudioDevices(false))
						vDetectedDeviceNames.emplace_back(Device.m_Name);

					VoiceUtils::BuildVoiceDeviceDropdownEntries(
						vDetectedDeviceNames,
						g_Config.m_QmVoiceOutputDevice,
						Localize("Default"),
						Localize("Disconnected"),
						s_VoiceOutputDeviceEntries);

					s_VoiceOutputDeviceDisplayNames.reserve(s_VoiceOutputDeviceEntries.size());
					s_VoiceOutputDeviceConfigValues.reserve(s_VoiceOutputDeviceEntries.size());
					s_VoiceOutputDeviceDropDownNames.reserve(s_VoiceOutputDeviceDisplayNames.size());
					for(const auto &Entry : s_VoiceOutputDeviceEntries)
					{
						s_VoiceOutputDeviceDisplayNames.push_back(Entry.m_DisplayName);
						s_VoiceOutputDeviceConfigValues.push_back(Entry.m_ConfigValue);
						s_VoiceOutputDeviceDropDownNames.push_back(s_VoiceOutputDeviceDisplayNames.back().c_str());
					}

					char aVoiceExtra[96];
					str_format(aVoiceExtra, sizeof(aVoiceExtra), "devices=%d", (int)s_VoiceOutputDeviceDisplayNames.size());
					LogQmPerfStage(Client(), "voice_output_device_enum", StageTimer.ElapsedMs(), false, aVoiceExtra);
				};
				if(Flow.Input() && !ReadOnly && !s_VoiceOutputDevicesInitialized)
				{
					RefreshVoiceOutputDeviceList();
					s_VoiceOutputDevicesInitialized = true;
				}

				CUIRect VoiceOutputDropDownRect;
				CUIRect VoiceOutputRefreshButton;
				ControlCol.VSplitRight(LineHeight, &VoiceOutputDropDownRect, &VoiceOutputRefreshButton);

				static CButtonContainer s_VoiceOutputRefreshButton;
				if(s_VoiceOutputDeviceEntries.empty())
				{
					DoQmSettingsLabel("qmclient-voice-output-default", &VoiceOutputDropDownRect, Localize("Default"), BodySize);
					DoQmSettingsIconButton(&s_VoiceOutputRefreshButton, "qmclient-voice-output-refresh", EQmIcon::ARROW_ROTATE_RIGHT, "\xEE\x80\xB6", Localize("Refresh"), &VoiceOutputRefreshButton);
				}
				else
				{
					const int VoiceOutputSelectedOld = VoiceUtils::VoiceFindSelectedDeviceIndex(s_VoiceOutputDeviceEntries, g_Config.m_QmVoiceOutputDevice);
					const int VoiceOutputSelectedNew = DoSettingsDropDown(&VoiceOutputDropDownRect, VoiceOutputSelectedOld, s_VoiceOutputDeviceDropDownNames.data(), s_VoiceOutputDeviceDropDownNames.size(), s_VoiceOutputDeviceDropDownState, {}, g_Config.m_QmVoiceOutputDevice, nullptr, &Row);
					if(Flow.Input() && !ReadOnly && VoiceOutputSelectedNew >= 0 && VoiceOutputSelectedNew != VoiceOutputSelectedOld && (size_t)VoiceOutputSelectedNew < s_VoiceOutputDeviceConfigValues.size())
						str_copy(g_Config.m_QmVoiceOutputDevice, s_VoiceOutputDeviceConfigValues[VoiceOutputSelectedNew].c_str(), sizeof(g_Config.m_QmVoiceOutputDevice));
					if(DoQmSettingsIconButton(&s_VoiceOutputRefreshButton, "qmclient-voice-output-refresh", EQmIcon::ARROW_ROTATE_RIGHT, "\xEE\x80\xB6", Localize("Refresh"), &VoiceOutputRefreshButton) && Flow.Input() && !ReadOnly)
						RefreshVoiceOutputDeviceList();
				}
			}
			Flow.Space(LineSpacing);

			if(Flow.Control("voice-bitrate", LineHeight, Row))
			{
				{
					static std::vector<const char *> s_VoiceBitrateProfileDropDownNames;
					s_VoiceBitrateProfileDropDownNames = {
						Localize("Auto"),
						"24 kbps",
						"32 kbps",
						"48 kbps",
						"64 kbps",
					};
					static CScrollRegion s_VoiceBitrateProfileDropDownScrollRegion;
					s_VoiceBitrateProfileDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_VoiceBitrateProfileDropDownScrollRegion;

					SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelCol, &ControlCol);
					DoQmSettingsLabel("qmclient-voice-bitrate", &LabelCol, Localize("Voice bitrate"), BodySize);
					const int CurrentBitrateProfile = std::clamp(g_Config.m_QmVoiceBitrateProfile, 0, 4);
					const int NewBitrateProfile = DoSettingsDropDown(&ControlCol, CurrentBitrateProfile, s_VoiceBitrateProfileDropDownNames.data(), s_VoiceBitrateProfileDropDownNames.size(), s_VoiceBitrateProfileDropDownState, {}, &g_Config.m_QmVoiceBitrateProfile, nullptr, &Row);
					if(Flow.Input() && !ReadOnly && CurrentBitrateProfile != NewBitrateProfile)
						g_Config.m_QmVoiceBitrateProfile = NewBitrateProfile;
				}
			}
			Flow.Space(LineSpacing);

			if(Flow.Control("voice-noise-reduction", LineHeight, Row))
			{
				{
					static std::vector<const char *> s_VoiceNoiseSuppressModeDropDownNames;
					s_VoiceNoiseSuppressModeDropDownNames = {
						Localize("No noise reduction"),
						Localize("Simple noise reduction"),
					};
					static CScrollRegion s_VoiceNoiseSuppressModeDropDownScrollRegion;
					s_VoiceNoiseSuppressModeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_VoiceNoiseSuppressModeDropDownScrollRegion;

					SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelCol, &ControlCol);
					DoQmSettingsLabel("qmclient-voice-noise-reduction-mode", &LabelCol, Localize("Noise reduction mode"), BodySize);
					const int CurrentNoiseSuppressMode = (g_Config.m_QmVoiceNoiseSuppressEnable == 1 ? 1 : 0);
					const int NewNoiseSuppressMode = DoSettingsDropDown(&ControlCol, CurrentNoiseSuppressMode, s_VoiceNoiseSuppressModeDropDownNames.data(), s_VoiceNoiseSuppressModeDropDownNames.size(), s_VoiceNoiseSuppressModeDropDownState, {}, &g_Config.m_QmVoiceNoiseSuppressEnable, nullptr, &Row);
					if(Flow.Input() && !ReadOnly && CurrentNoiseSuppressMode != NewNoiseSuppressMode)
						g_Config.m_QmVoiceNoiseSuppressEnable = NewNoiseSuppressMode;
				}
			}
			Flow.Space(LineSpacing);

			if((g_Config.m_QmVoiceNoiseSuppressEnable == 1 ? 1 : 0) != 0)
			{
				if(Flow.Control("voice-noise-strength", LineHeight, Row))
				{
					{
						CUIRect LabelColValue, ControlColValue;
						SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColValue, &ControlColValue);
						const char *pNoiseSuppressStrengthLabel = Localize("Simple noise reduction strength");
						Ui()->DoLabel(&LabelColValue, pNoiseSuppressStrengthLabel, BodySize, TEXTALIGN_ML);
						static int s_QmVoiceNoiseSuppressStrengthInputId;
						RenderSliderWithValueInput(&s_QmVoiceNoiseSuppressStrengthInputId, ControlColValue, &g_Config.m_QmVoiceNoiseSuppressStrength, 0, 100, "%");
					}
				}
				Flow.Space(LineSpacing);
			}

			if(Flow.Control("voice-agc", LineHeight, Row))
			{
				DoQmSettingsCheckboxAuto(&g_Config.m_QmVoiceAgcEnable, "Auto gain control for mic (AGC, experimental)", Localize("Auto gain control for mic (AGC, experimental)"), &g_Config.m_QmVoiceAgcEnable, &Row, LineHeight);
			}
			Flow.Space(LineSpacing);

			if(g_Config.m_QmVoiceVadEnable)
			{
				if(Flow.Control("voice-vad-threshold", LineHeight, Row))
				{
					{
						CUIRect LabelColValue, ControlColValue;
						SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColValue, &ControlColValue);
						DoQmSettingsLabel("qmclient-voice-speech-trigger-threshold", &LabelColValue, Localize("Speech trigger threshold"), BodySize);
						static int s_QmVoiceVadThresholdInputId;
						RenderSliderWithValueInput(&s_QmVoiceVadThresholdInputId, ControlColValue, &g_Config.m_QmVoiceVadThreshold, 0, 100, "%");
					}
				}
				Flow.Space(LineSpacing);

				if(Flow.Control("voice-vad-delay", LineHeight, Row))
				{
					{
						CUIRect LabelColValue, ControlColValue;
						SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColValue, &ControlColValue);
						DoQmSettingsLabel("qmclient-voice-activation-release-delay", &LabelColValue, Localize("Voice activation release delay"), BodySize);
						static int s_QmVoiceVadReleaseDelayMsInputId;
						RenderSliderWithValueInput(&s_QmVoiceVadReleaseDelayMsInputId, ControlColValue, &g_Config.m_QmVoiceVadReleaseDelayMs, 0, 1000, "ms");
					}
				}
				Flow.Space(LineSpacing);
			}

			Flow.Space(LineSpacing * 1.15f);

			if(Flow.Control("voice-playback-volume", LineHeight, Row))
			{
				{
					CUIRect LabelColValue, ControlColValue;
					SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColValue, &ControlColValue);
					DoQmSettingsLabel("qmclient-voice-playback-volume", &LabelColValue, Localize("Playback volume"), BodySize);
					static int s_QmVoiceVolumeInputId;
					RenderSliderWithValueInput(&s_QmVoiceVolumeInputId, ControlColValue, &g_Config.m_QmVoiceVolume, 0, 400, "%");
				}
			}
			Flow.Space(LineSpacing);

			if(Flow.Control("voice-stereo", LineHeight, Row))
			{
				DoQmSettingsCheckboxAuto(&g_Config.m_QmVoiceStereo, "Enable stereo positioning", Localize("Enable stereo positioning"), &g_Config.m_QmVoiceStereo, &Row, LineHeight);
			}
			Flow.Space(LineSpacing);

			if(g_Config.m_QmVoiceStereo)
			{
				if(Flow.Control("voice-stereo-width", LineHeight, Row))
				{
					{
						CUIRect LabelColValue, ControlColValue;
						SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColValue, &ControlColValue);
						DoQmSettingsLabel("qmclient-voice-left-right-channel-width", &LabelColValue, Localize("Left/right channel width"), BodySize);
						static int s_QmVoiceStereoWidthInputId;
						RenderSliderWithValueInput(&s_QmVoiceStereoWidthInputId, ControlColValue, &g_Config.m_QmVoiceStereoWidth, 0, 200, "%");
					}
				}
				Flow.Space(LineSpacing);
			}

			if(Flow.Control("voice-radius", LineHeight, Row))
			{
				{
					CUIRect LabelColValue, ControlColValue;
					SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColValue, &ControlColValue);
					DoQmSettingsLabel("qmclient-voice-distance-radius-tiles", &LabelColValue, Localize("Voice distance radius (tiles)"), BodySize);
					static int s_QmVoiceRadiusInputId;
					RenderSliderWithValueInput(&s_QmVoiceRadiusInputId, ControlColValue, &g_Config.m_QmVoiceRadius, 1, 400);
				}
			}
			Flow.Space(LineSpacing);

			if(Flow.Control("voice-group-global", LineHeight, Row))
			{
				DoQmSettingsCheckboxAuto(&g_Config.m_QmVoiceGroupGlobal, "Full map listen in same room", Localize("Full map listen in same room"), &g_Config.m_QmVoiceGroupGlobal, &Row, LineHeight);
			}
		}
	}
}
