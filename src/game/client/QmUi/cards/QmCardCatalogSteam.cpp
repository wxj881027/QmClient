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

void CMenus::RenderQmSteamContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly() || Flow.Declaring();
	static CSteamProbeState s_Probe;
	static CLineInputBuffered<1024> s_ManualPathInput;
	static CButtonContainer s_DetectButton;
	if(Flow.Input() && !s_ManualPathInput.IsActive() && str_comp(s_ManualPathInput.GetString(), g_Config.m_QmSteamClientPath) != 0)
		s_ManualPathInput.Set(g_Config.m_QmSteamClientPath);
	if(!ReadOnly)
		s_Probe.Finish(g_Config.m_QmSteamClientPath);
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
	TextRow("qm-steam-client-path", PathPresentation.m_pText, Metrics.m_SmallSize);
	if(Flow.Control("qm-steam-path-input", Metrics.m_LineHeight, Row))
		if(ui_widget::InputField(Ctx, &s_ManualPathInput, Row, "", Metrics.m_BodySize) && !ReadOnly)
			str_copy(g_Config.m_QmSteamClientPath, s_ManualPathInput.GetString());
	Flow.Space(Metrics.m_LineSpacing);
	if(Flow.Control("qm-steam-detect", Metrics.m_LineHeight, Row))
		if(ui_widget::SecondaryButton(Ctx, &s_DetectButton, Localize("Detect Steam again"), Row, PrewarmOnly || s_Probe.Pending()) && Flow.Input() && !ReadOnly)
			s_Probe.RequestRefresh();
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
	if(Flow.Control("qm-steam-resolved-path", Metrics.m_LineHeight, Row))
	{
		SLabelProperties Props;
		Props.m_MaxWidth = Row.w;
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Row, s_Probe.HasResult() ? Info.m_aPath : "", Metrics.m_SmallSize, TEXTALIGN_ML, Props);
		if(s_Probe.HasResult() && Info.m_aPath[0] != '\0')
			GameClient()->m_Tooltips.DoToolTipForRect(&Info, &Row, Info.m_aPath);
	}
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
