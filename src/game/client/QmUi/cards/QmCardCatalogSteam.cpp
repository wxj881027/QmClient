#include "QmCardCatalogInternal.h"

#include <engine/engine.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>
#include <engine/steam.h>
#include <engine/textrender.h>

#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/components/menus.h>
#include <game/client/components/tooltips.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <memory>
#include <string>

namespace
{
	class CSteamProbeJob : public IJob
	{
		void Run() override { m_Info = SteamInspectClient(m_ManualPath.c_str()); }

	public:
		const std::string m_ManualPath;
		SSteamClientInfo m_Info;
		explicit CSteamProbeJob(const char *pManualPath) : m_ManualPath(pManualPath) {}
	};
}

void CMenus::RenderQmSteamContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, bool PrewarmOnly)
{
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	static std::shared_ptr<CSteamProbeJob> s_pProbe;
	static SSteamClientInfo s_Info;
	static bool s_HasResult = false;
	static bool s_RefreshRequested = false;
	static std::string s_CheckedManualPath;
	static CLineInputBuffered<1024> s_ManualPathInput;
	static CButtonContainer s_DetectButton;
	if(!s_ManualPathInput.IsActive() && str_comp(s_ManualPathInput.GetString(), g_Config.m_QmSteamClientPath) != 0)
		s_ManualPathInput.Set(g_Config.m_QmSteamClientPath);
	if(!ReadOnly && s_pProbe != nullptr && s_pProbe->Done())
	{
		// 只读取已完成任务；编辑路径期间返回的旧结果不覆盖新选择。
		if(s_pProbe->State() == IJob::STATE_DONE && s_pProbe->m_ManualPath == g_Config.m_QmSteamClientPath)
		{
			s_Info = s_pProbe->m_Info;
			s_CheckedManualPath = s_pProbe->m_ManualPath;
			s_HasResult = true;
		}
		s_pProbe.reset();
	}
	IUiContext Ctx = SettingsUiContext("settings_steam", Metrics.m_UiScale);
	RenderQmFunctionCheckboxRow(Content, Metrics.m_LineHeight, Metrics.m_LineSpacing,
		&g_Config.m_QmSteamAutoLaunch, "qm-steam-auto-launch",
		Localize("Launch Steam automatically when starting externally"), &g_Config.m_QmSteamAutoLaunch, ReadOnly);
	CUIRect Row;
	SLabelProperties WrapProps;
	WrapProps.m_MaxWidth = Content.w;
	const char *pPathLabel = Localize("Steam client path (leave empty to detect automatically)");
	const float LabelHeight = std::max(Metrics.m_LineHeight, TextRender()->TextBoundingBox(Metrics.m_SmallSize, pPathLabel, -1, std::max(1.0f, Content.w)).m_H);
	Content.HSplitTop(LabelHeight, &Row, &Content);
	Ui()->DoLabel(&Row, pPathLabel, Metrics.m_SmallSize, TEXTALIGN_ML, WrapProps);
	Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
	Content.HSplitTop(Metrics.m_LineHeight, &Row, &Content);
	if(ui_widget::InputField(Ctx, &s_ManualPathInput, Row, "", Metrics.m_BodySize) && !ReadOnly)
		str_copy(g_Config.m_QmSteamClientPath, s_ManualPathInput.GetString());
	Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
	Content.HSplitTop(Metrics.m_LineHeight, &Row, &Content);
	if(ui_widget::SecondaryButton(Ctx, &s_DetectButton, Localize("Detect Steam again"), Row, ReadOnly || s_pProbe != nullptr))
		s_RefreshRequested = true;
	Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
	if(!ReadOnly && s_pProbe == nullptr && (s_RefreshRequested || (!s_ManualPathInput.IsActive() && (!s_HasResult || s_CheckedManualPath != g_Config.m_QmSteamClientPath))))
	{
		s_pProbe = std::make_shared<CSteamProbeJob>(g_Config.m_QmSteamClientPath);
		Engine()->AddJob(s_pProbe);
		s_RefreshRequested = false;
	}
	const char *pStatus;
	if(s_pProbe != nullptr || !s_HasResult)
		pStatus = Localize("Checking Steam...");
	else if(s_Info.m_aPath[0] == '\0')
		pStatus = Localize("Steam not found");
	else if(s_Info.m_Running)
		pStatus = Localize("Steam is running");
	else if(s_Info.m_RunningKnown)
		pStatus = Localize("Steam is installed but not running");
	else
		pStatus = Localize("Steam is installed");
	float StatusHeight = Metrics.m_LineHeight;
	const char *apStatuses[] = {Localize("Checking Steam..."), Localize("Steam not found"), Localize("Steam is running"), Localize("Steam is installed but not running"), Localize("Steam is installed")};
	for(const char *pText : apStatuses)
		StatusHeight = std::max(StatusHeight, TextRender()->TextBoundingBox(Metrics.m_BodySize, pText, -1, std::max(1.0f, Content.w)).m_H);
	Content.HSplitTop(StatusHeight, &Row, &Content);
	Ui()->DoLabel(&Row, pStatus, Metrics.m_BodySize, TEXTALIGN_ML, WrapProps);
	Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
	Content.HSplitTop(Metrics.m_LineHeight, &Row, &Content);
	SLabelProperties Props;
	Props.m_MaxWidth = Row.w;
	Props.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&Row, s_HasResult ? s_Info.m_aPath : "", Metrics.m_SmallSize, TEXTALIGN_ML, Props);
	if(s_HasResult && s_Info.m_aPath[0] != '\0')
		GameClient()->m_Tooltips.DoToolTipForRect(&s_Info, &Row, s_Info.m_aPath);
	Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
	const char *pInvalidPath = Localize("Manual Steam path is invalid; using automatic detection");
	const char *pSelectHelp = Localize("Enter the Steam client program path, without command arguments");
	const float HelpHeight = std::max({Metrics.m_LineHeight,
		TextRender()->TextBoundingBox(Metrics.m_SmallSize, pInvalidPath, -1, std::max(1.0f, Content.w)).m_H,
		TextRender()->TextBoundingBox(Metrics.m_SmallSize, pSelectHelp, -1, std::max(1.0f, Content.w)).m_H});
	Content.HSplitTop(HelpHeight, &Row, &Content);
	Ui()->DoLabel(&Row, s_HasResult && s_Info.m_ManualPathRejected ? pInvalidPath : pSelectHelp, Metrics.m_SmallSize, TEXTALIGN_ML, WrapProps);
	Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
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
			[Metrics = Ctx.m_Metrics](float) {
				return CardRows(Metrics, 7.0f);
			},
			g_Config.m_QmSteamAutoLaunch != 0 ? 1u : 0u,
			{},
			Out);
		return true;
	}
} // namespace qm_card_catalog
