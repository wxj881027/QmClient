#include "QmCardCatalogInternal.h"
#include "QmSteamProbeState.h"

#include <engine/engine.h>
#include <engine/shared/config.h>
#include <engine/steam.h>
#include <engine/textrender.h>

#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsLabelMetadata.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/components/menus.h>
#include <game/client/components/tooltips.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <memory>

// Windows 探测结果经常是 d:\... 这种小写盘符，直接回填会和用户手写的 D:\ 看起来像两条路径。
void NormalizeSteamDriveLetter(char *pPath) noexcept
{
	if(pPath == nullptr || pPath[0] == '\0' || pPath[1] != ':')
		return;
	if(pPath[0] >= 'a' && pPath[0] <= 'z')
		pPath[0] = (char)(pPath[0] - 'a' + 'A');
}
void CMenus::RenderQmSteamContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly() || Flow.Declaring();
	static CSteamProbeState s_Probe;
	// 自动回填只做一次，避免用户清空输入框后又被检测结果填回去。
	static bool s_AutoPathFilled = false;
	static CLineInputBuffered<1024> s_ManualPathInput;
	static CButtonContainer s_DetectButton;
	if(Flow.Input() && !s_ManualPathInput.IsActive() && str_comp(s_ManualPathInput.GetString(), g_Config.m_QmSteamClientPath) != 0)
		s_ManualPathInput.Set(g_Config.m_QmSteamClientPath);
	const bool AutoFillPath = !s_AutoPathFilled && g_Config.m_QmSteamClientPath[0] == '\0' && !s_ManualPathInput.IsActive();
	if(!ReadOnly && s_Probe.Finish(g_Config.m_QmSteamClientPath, AutoFillPath, NormalizeSteamDriveLetter) && AutoFillPath && s_Probe.Info().m_aPath[0] != '\0')
	{
		// 完整检查键已与本次结果一起发布，回填只复制固定缓冲，不再分配。
		str_copy(g_Config.m_QmSteamClientPath, s_Probe.Info().m_aPath);
		s_ManualPathInput.Set(s_Probe.Info().m_aPath);
		s_AutoPathFilled = true;
	}
	IUiContext Ctx = SettingsUiContext("settings_steam", Metrics.m_UiScale);
	CUIRect Row;
	if(Flow.Control("qm-steam-auto-launch", Metrics.m_LineHeight, Row))
		RenderQmFunctionCheckbox(&g_Config.m_QmSteamAutoLaunch, "qm-steam-auto-launch", Localize("Launch Steam automatically when starting externally"), &g_Config.m_QmSteamAutoLaunch, &Row, ReadOnly);
	Flow.Space(Metrics.m_LineSpacing);
	const auto TextRow = [&](const char *pId, const char *pText, float Size) {
		Flow.Text(pId, pText, 0.0f, Size, Metrics.m_LineSpacing, [this](const char *pValue, float Width, float FontSize) { return TextRender()->TextBoundingBox(FontSize, pValue, -1, Width).m_H + FontSize * 0.25f; }, [this, pId, Size](const CUIRect &TextRect, const char *pValue) {
				SLabelProperties Props;
				Props.m_MaxWidth = TextRect.w;
				DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pId, &TextRect, pValue, Size, TEXTALIGN_ML, Props); }, Metrics.m_LineHeight);
	};
	const char *pPathLabel = Localize("Steam client path (leave empty to detect automatically)");
	const auto PathPresentation = ResolveSettingsLabelPresentation("qm-steam-client-path", pPathLabel, [](const char *pKey) { return Localize(pKey); });
	// 路径行压成一行：标签在左、输入框在右，重新检测收成输入框右侧的方形图标按钮，不再各占一行。
	if(Flow.Control("qm-steam-client-path", Metrics.m_LineHeight, Row))
	{
		CUIRect PathLabel, PathControls, PathInput, PathDetectButton;
		SplitSettingsControlRow(Row, Metrics, &PathLabel, &PathControls);
		const auto InputActions = ResolveSettingsInputActionsLayout(PathControls, 1, Metrics, Metrics.m_ButtonHeight);
		PathInput = InputActions.m_Input;
		PathDetectButton = InputActions.m_Actions.m_aButtons[0];
		SLabelProperties Props;
		Props.m_MaxWidth = PathLabel.w;
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qm-steam-client-path", &PathLabel, PathPresentation.m_pText, Metrics.m_BodySize, TEXTALIGN_ML, Props);
		if(ui_widget::InputField(Ctx, &s_ManualPathInput, PathInput, "", Metrics.m_BodySize) && !ReadOnly)
			str_copy(g_Config.m_QmSteamClientPath, s_ManualPathInput.GetString());
		// 重新检测只留图标，说明走悬浮提示，和卡片里其它刷新图标保持一致。
		if(Ui()->DoButton_QmIcon(&s_DetectButton, EQmIcon::ARROW_ROTATE_RIGHT, "\xEE\x80\xB6", 0, &PathDetectButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, !PrewarmOnly && !s_Probe.Pending()) && Flow.Input() && !ReadOnly)
			s_Probe.RequestRefresh();
		GameClient()->m_Tooltips.DoToolTip(&s_DetectButton, &PathDetectButton, Localize("Detect Steam again"));
	}
	Flow.Space(Metrics.m_LineSpacing);
	if(!ReadOnly)
		s_Probe.TryStart(g_Config.m_QmSteamClientPath, !s_ManualPathInput.IsActive(), [](const char *pPath) { return std::make_shared<CSteamProbeJob>(pPath, SteamInspectClient); }, [this](const auto &pJob) { return Engine()->TryAddJob(pJob); });
	const SSteamClientInfo &Info = s_Probe.Info();
	const char *pStatus;
	if(s_Probe.Pending() || !s_Probe.HasResult())
		pStatus = Localize("Checking Steam...");
	else if(Info.m_aPath[0] == '\0')
		pStatus = Localize("Steam not found");
	else if(Info.m_Running)
		pStatus = Localize("Steam is running");
	else if(Info.m_RunningKnown)
		pStatus = Localize("Steam is installed but not running");
	else
		pStatus = Localize("Steam is installed");
	TextRow("qm-steam-status", pStatus, Metrics.m_BodySize);
	Flow.Space(Metrics.m_LineSpacing);
	const char *pInvalidPath = Localize("Manual Steam path is invalid; using automatic detection");
	const char *pSelectHelp = Localize("Enter the Steam client program path, without command arguments");
	TextRow("qm-steam-help", s_Probe.HasResult() && Info.m_ManualPathRejected ? pInvalidPath : pSelectHelp, Metrics.m_SmallSize);
}

namespace qm_card_catalog
{

	bool BuildSteamCard(const SQmCardBuildContext &Ctx, const qm_module::EQmModuleId Id, SSettingsCardDefinition &Out)
	{
		MakeModuleCard(
			Ctx,
			Id,
			"qm:steam",
			"Steam integration",
			"Automatically launch Steam when the client is started externally, so Steam can track your playtime",
			[Ctx](CUIRect &Content) { QmCardRenderHook::RenderQmSteamContent(Ctx.m_pMenus, Content, Ctx.m_Metrics, Ctx.m_ReadOnly); },
			Out);
		UseDeclaredModuleContent(Ctx, Out);
		return true;
	}
} // namespace qm_card_catalog
