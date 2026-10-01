#include "translate_ui_popup.h"

#include "translate_backend.h"
#include "translate_ui_common.h"

#include <engine/shared/config.h>

#include <game/localization.h>

namespace
{
	const char *BackendWarning()
	{
		if(NTranslateUi::FindBackendIndex(g_Config.m_QmTranslateBackend) == NTranslateUi::BACKEND_TENCENT_CLOUD)
		{
			if(g_Config.m_QmTranslateTcSecretId[0] == '\0' || g_Config.m_QmTranslateTcSecretKey[0] == '\0')
				return Localize("Tencent Cloud API not configured");
		}
		else if(NTranslateUi::FindBackendIndex(g_Config.m_QmTranslateBackend) == NTranslateUi::BACKEND_LLM)
		{
			if(GetSelectedTranslateLlmKey()[0] == '\0')
				return Localize("LLM API key not configured");
		}
		return nullptr;
	}
}

void CTranslateSettingsPopup::Open(CUi *pUi, vec2 BottomRight)
{
	m_pUi = pUi;
	const ui_widget::SSecondaryPanelMetrics Metrics;
	const float Width = minimum(240.0f, pUi->Screen()->w);
	// 保留提示行，切换后端时出现的配置提示不改变弹层尺寸。
	const float Height = Metrics.ContentHeight(2, 3, 1) + CUi::PopupMenuContentInset();
	SPopupMenuProperties Props;
	Props.m_BackgroundColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmTranslateMenuBgColor, true));
	Props.m_BlockUnderlyingPointerInput = true;
	Props.m_BlockUnderlyingScroll = true;
	const float X = std::clamp(BottomRight.x - Width, 0.0f, maximum(0.0f, pUi->Screen()->w - Width));
	const float Y = std::clamp(BottomRight.y - Height, 0.0f, maximum(0.0f, pUi->Screen()->h - Height));
	pUi->DoPopupMenu(this, X, Y, Width, Height, this, Render, Props);
}

CUi::EPopupMenuFunctionResult CTranslateSettingsPopup::Render(void *pContext, CUIRect View, bool Active)
{
	auto &State = *static_cast<CTranslateSettingsPopup *>(pContext);
	SUiTheme Theme{};
	Theme.m_Accent = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmTranslateMenuOptionSelected, true));
	IUiContext Ctx;
	Ctx.m_pUi = State.m_pUi;
	Ctx.m_pTheme = &Theme;
	SQmDropdownVisualStyle Style;
	Style.m_TriggerColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmTranslateMenuOptionNormal, true));
	Style.m_ActiveEntryColor = Theme.m_Accent;
	Style.m_PopupBackgroundColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmTranslateMenuBgColor, true));
	ui_widget::CSecondaryPanel Panel(Ctx, View, Active, {}, Style);
	if(Panel.Header(State.m_Title, State.m_CloseButton, Localize("Translation Settings")))
		return CUi::POPUP_CLOSE_CURRENT_AND_DESCENDANTS;

	Panel.ToggleRow(State.m_InboundToggle, Localize("Auto-translate incoming messages"), g_Config.m_QmTranslateAuto);
	Panel.ToggleRow(State.m_OutboundToggle, Localize("Auto-translate outgoing messages"), g_Config.m_QmTranslateAutoOutgoing);
	const auto &Languages = NTranslateUi::LanguageNames();
	const int Inbound = NTranslateUi::LanguageIndexForDisplay(g_Config.m_QmTranslateTarget);
	const int NewInbound = Panel.DropdownRow(State.m_InboundLabel, Localize("Incoming language"), Inbound, Languages.data(), Languages.size(), State.m_InboundDropdown);
	NTranslateUi::CommitLanguage(g_Config.m_QmTranslateTarget, sizeof(g_Config.m_QmTranslateTarget), Inbound, NewInbound);
	const int Outbound = NTranslateUi::LanguageIndexForDisplay(g_Config.m_QmTranslateOutgoingTarget);
	const int NewOutbound = Panel.DropdownRow(State.m_OutboundLabel, Localize("Outgoing language"), Outbound, Languages.data(), Languages.size(), State.m_OutboundDropdown);
	NTranslateUi::CommitLanguage(g_Config.m_QmTranslateOutgoingTarget, sizeof(g_Config.m_QmTranslateOutgoingTarget), Outbound, NewOutbound);
	if(Active && !State.m_pUi->RenderOnly())
		NTranslateUi::NormalizeBackend(g_Config.m_QmTranslateBackend, sizeof(g_Config.m_QmTranslateBackend));
	const auto Backends = NTranslateUi::BackendNames();
	const int Backend = NTranslateUi::BackendIndexForDisplay(g_Config.m_QmTranslateBackend);
	const int NewBackend = Panel.DropdownRow(State.m_BackendLabel, Localize("Translation service"), Backend, Backends.data(), Backends.size(), State.m_BackendDropdown);
	NTranslateUi::CommitBackend(g_Config.m_QmTranslateBackend, sizeof(g_Config.m_QmTranslateBackend), Backend, NewBackend);
	Panel.Notice(State.m_Warning, BackendWarning());
	return CUi::POPUP_KEEP_OPEN;
}
