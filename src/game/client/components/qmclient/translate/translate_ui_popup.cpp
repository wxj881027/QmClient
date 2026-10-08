#include "translate_ui_popup.h"

#include "translate_backend.h"
#include "translate_ui_common.h"

#include <engine/shared/config.h>

#include <game/client/QmUi/UiTokens.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

namespace
{
	const char *BackendWarning()
	{
		if(TranslateBackendNeedsConfiguration())
			return Localize("Translation service is not configured correctly.");
		return nullptr;
	}
}

void CTranslateSettingsPopup::Open(CUi *pUi, vec2 BottomRight, CGameClient *pGameClient)
{
	m_pUi = pUi;
	m_pGameClient = pGameClient;
	const ui_widget::SSecondaryPanelMetrics Metrics = ui_widget::ResolveSecondaryPanelMetrics(pUi->Screen()->w);
	const float Width = minimum(240.0f, pUi->Screen()->w);
	// 保留提示行，切换后端时出现的配置提示不改变弹层尺寸。
	const float Height = minimum(Metrics.ContentHeight(2, 3, 1) + CUi::PopupMenuContentInset(), pUi->Screen()->h);
	const SPopupMenuProperties Props = ui_widget::SecondaryPanelProperties();
	const float X = std::clamp(BottomRight.x - Width, 0.0f, maximum(0.0f, pUi->Screen()->w - Width));
	const float Y = std::clamp(BottomRight.y - Height, 0.0f, maximum(0.0f, pUi->Screen()->h - Height));
	pUi->DoPopupMenu(this, X, Y, Width, Height, this, Render, Props);
}

CUi::EPopupMenuFunctionResult CTranslateSettingsPopup::Render(void *pContext, CUIRect View, bool Active)
{
	auto &State = *static_cast<CTranslateSettingsPopup *>(pContext);
	IUiContext Ctx;
	Ctx.m_pUi = State.m_pUi;
	if(State.m_pGameClient != nullptr && State.m_pGameClient->UiRuntimeV2() != nullptr)
	{
		Ctx.m_pAnim = &State.m_pGameClient->UiRuntimeV2()->AnimRuntime();
		Ctx.m_pTree = &State.m_pGameClient->UiRuntimeV2()->Tree();
		Ctx.m_ScopeHash = MakeUiScopeHash("translate_settings_popup");
		Ctx.m_FrameDt = State.m_pGameClient->UiRuntimeV2()->FrameDt();
	}
	const SUiTheme SharedTheme = ResolveInputFallbackTheme(g_Config.m_QmUiFocusColor);
	const SQmDropdownVisualStyle Style = QmSettingsDropdownVisualStyle(SharedTheme, ResolveConfiguredSecondaryPanelTheme().m_Border);
	ui_widget::CSecondaryPanel Panel(Ctx, View, Active, ui_widget::ResolveSecondaryPanelMetrics(State.m_pUi->Screen()->w), Style);
	if(Panel.Header(State.m_Title, State.m_CloseButton, Localize("Translation Settings")))
		return CUi::POPUP_CLOSE_CURRENT_AND_DESCENDANTS;

	Panel.ToggleRow(State.m_InboundToggle, Localize("Auto-translate incoming messages"), g_Config.m_QmTranslateAuto);
	Panel.ToggleRow(State.m_OutboundToggle, Localize("Auto-translate outgoing messages"), g_Config.m_QmTranslateAutoOutgoing);
	const auto Languages = NTranslateUi::NamesWithCustom(NTranslateUi::LanguageNames());
	const int Inbound = NTranslateUi::LanguageIndexForDisplay(g_Config.m_QmTranslateTarget);
	const int NewInbound = Panel.DropdownRow(State.m_InboundLabel, Localize("Incoming language"), Inbound, Languages.data(), Languages.size(), State.m_InboundDropdown);
	if(Active && !State.m_pUi->RenderOnly())
		NTranslateUi::CommitLanguage(g_Config.m_QmTranslateTarget, sizeof(g_Config.m_QmTranslateTarget), Inbound, NewInbound);
	const int Outbound = NTranslateUi::LanguageIndexForDisplay(g_Config.m_QmTranslateOutgoingTarget);
	const int NewOutbound = Panel.DropdownRow(State.m_OutboundLabel, Localize("Outgoing language"), Outbound, Languages.data(), Languages.size(), State.m_OutboundDropdown);
	if(Active && !State.m_pUi->RenderOnly())
		NTranslateUi::CommitLanguage(g_Config.m_QmTranslateOutgoingTarget, sizeof(g_Config.m_QmTranslateOutgoingTarget), Outbound, NewOutbound);
	if(Active && !State.m_pUi->RenderOnly())
		NTranslateUi::NormalizeBackend(g_Config.m_QmTranslateBackend, sizeof(g_Config.m_QmTranslateBackend));
	const auto Backends = NTranslateUi::NamesWithCustom(NTranslateUi::BackendNames());
	const int Backend = NTranslateUi::BackendIndexForDisplay(g_Config.m_QmTranslateBackend);
	const int NewBackend = Panel.DropdownRow(State.m_BackendLabel, Localize("Translation service"), Backend, Backends.data(), Backends.size(), State.m_BackendDropdown);
	if(Active && !State.m_pUi->RenderOnly())
		NTranslateUi::CommitBackend(g_Config.m_QmTranslateBackend, sizeof(g_Config.m_QmTranslateBackend), Backend, NewBackend);
	Panel.Notice(State.m_Warning, BackendWarning());
	return CUi::POPUP_KEEP_OPEN;
}
