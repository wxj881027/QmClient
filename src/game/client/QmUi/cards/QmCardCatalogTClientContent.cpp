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
#include <game/client/QmUi/SettingsContentLayout.h>
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

#include <SDL3/SDL_audio.h>

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

void CMenus::ConfigureSettingsCardSection(SSettingsSection &Section, const char *pTitle, const char *pStableCardId, std::function<float(CUIRect &, bool)> LayoutSection, float TopMargin)
{
	Section.m_pStableCardId = pStableCardId;
	// 已声明的内容不再绘制旧节标题，卡片头由公共 Deck 管理。
	Section.m_MeasureFn = [LayoutSection](CUIRect &Col) {
		std::vector<SSettingsContentRowDeclaration> vRows;
		{
			CSettingsContentLayoutScope Scope(vRows);
			LayoutSection(Col, false);
		}
		CSettingsContentLayoutCache Cache;
		const float Height = Cache.Resolve(vRows, Col.w, 0, {})->Height();
		Col.y += Height;
		Col.h = std::max(0.0f, Col.h - Height);
		return Height;
	};
	Section.m_RenderCompactFn = [LayoutSection](CUIRect &Col) { return LayoutSection(Col, true); };
	Section.m_RenderFullFn = Section.m_RenderCompactFn;
}
float CMenus::LayoutTClientAutoReplyCacheSection(CUIRect &CurrentColumn, bool Render)
{
	CUIRect ReplyRect;
	IUiContext TClientAutoReplyTextInputCtx;
	TClientAutoReplyTextInputCtx.m_pUi = Ui();
	TClientAutoReplyTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	TClientAutoReplyTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	TClientAutoReplyTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_auto_reply_text_inputs");
	TClientAutoReplyTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	const float SavedY = CurrentColumn.y;
	CSettingsContentLayoutFlow Flow(CurrentColumn);

	CUIRect MutedToggle;
	if(Flow.Control("tclient-auto-reply-muted", LineSize, MutedToggle, MarginSmall))
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmAutoReplyMuted, "tclient-auto-reply-muted", Localize("Automatically reply to muted players"), &g_Config.m_QmAutoReplyMuted, &MutedToggle, LineSize);
	if(g_Config.m_QmAutoReplyMuted)
	{
		if(Flow.Control("tclient-auto-reply-muted-message", LineSize, ReplyRect, MarginSmall))
		{
			static CLineInput s_MutedReply(g_Config.m_QmAutoReplyMutedMessage, sizeof(g_Config.m_QmAutoReplyMutedMessage));
			s_MutedReply.SetEmptyText(Localize("I muted you"));
			ui_widget::InputField(TClientAutoReplyTextInputCtx, &s_MutedReply, ReplyRect, nullptr, EditBoxFontSize);
		}
	}
	CUIRect MinimizedToggle;
	if(Flow.Control("tclient-auto-reply-minimized", LineSize, MinimizedToggle, MarginSmall))
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmAutoReplyMinimized, "tclient-auto-reply-minimized", Localize("Automatically reply while the window is unfocused"), &g_Config.m_QmAutoReplyMinimized, &MinimizedToggle, LineSize);
	if(g_Config.m_QmAutoReplyMinimized)
	{
		if(Flow.Control("tclient-auto-reply-minimized-message", LineSize, ReplyRect, MarginSmall))
		{
			static CLineInput s_MinimizedReply(g_Config.m_QmAutoReplyMinimizedMessage, sizeof(g_Config.m_QmAutoReplyMinimizedMessage));
			s_MinimizedReply.SetEmptyText(Localize("I am away from the game window"));
			ui_widget::InputField(TClientAutoReplyTextInputCtx, &s_MinimizedReply, ReplyRect, nullptr, EditBoxFontSize);
		}
	}
	return CurrentColumn.y - SavedY;
}
float CMenus::LayoutTClientPetCacheSection(CUIRect &CurrentColumn, bool Render)
{
	CUIRect Label, Button, PetSkinBox;
	IUiContext TClientPetTextInputCtx;
	TClientPetTextInputCtx.m_pUi = Ui();
	TClientPetTextInputCtx.m_pTooltips = &GameClient()->m_Tooltips;
	TClientPetTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	TClientPetTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	TClientPetTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_pet_text_inputs");
	TClientPetTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	const float SavedY = CurrentColumn.y;
	CSettingsContentLayoutFlow Flow(CurrentColumn);
	CUIRect ShowPetRow;
	if(Flow.Control("tclient-show-pet", LineSize, ShowPetRow, MarginSmall))
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmPetShow, "tclient-show-pet", Localize("Show the pet"), &g_Config.m_QmPetShow, &ShowPetRow, LineSize);
	if(qm_card_catalog::QmCardRenderHook::Personalization(this, CurrentColumn, "tclient:pet", CurrentSettingsContentMetrics(), !Render))
	{
		if(Flow.Control("tclient-pet-size", LineSize, Button, MarginSmall))
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, "tclient-pet-size", &g_Config.m_QmPetSize, &g_Config.m_QmPetSize, &Button, Localize("Pet size"), 10, 500, &CUi::ms_LinearScrollbarScale, 0, "%");
		if(Flow.Control("tclient-pet-alpha", LineSize, Button, MarginSmall))
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-pet-alpha", &g_Config.m_QmPetAlpha, &g_Config.m_QmPetAlpha, &Button, Localize("Pet alpha"), 10, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		if(Flow.Control("tclient-pet-skin", LineSize, PetSkinBox, MarginSmall))
		{
			SplitSettingsControlRow(PetSkinBox, CurrentSettingsContentMetrics(), &Label, &Button);
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Pet Skin:"), FontSize, TEXTALIGN_ML);
			static CLineInput s_PetSkin(g_Config.m_QmPetSkin, sizeof(g_Config.m_QmPetSkin));
			GameClient()->m_Tooltips.DoSettingsToolTipForConfig(&s_PetSkin, &PetSkinBox, g_Config.m_QmPetSkin, &Label);
			ui_widget::InputField(TClientPetTextInputCtx, &s_PetSkin, Button, nullptr, EditBoxFontSize);
		}
	}
	return CurrentColumn.y - SavedY;
}
float CMenus::LayoutTClientHudCacheSection(CUIRect &CurrentColumn, bool Render)
{
	CUIRect Button, NotificationConfig;
	IUiContext TClientHudTextInputCtx;
	TClientHudTextInputCtx.m_pUi = Ui();
	TClientHudTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	TClientHudTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	TClientHudTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_hud_text_inputs");
	TClientHudTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	const float SavedY = CurrentColumn.y;
	CSettingsContentLayoutFlow Flow(CurrentColumn);
	CUIRect Row;
	if(Flow.Control("tclient-mini-vote-hud", LineSize, Row, MarginSmall))
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmMiniVoteHud, "tclient-mini-vote-hud", Localize("Show compact vote HUD"), &g_Config.m_QmMiniVoteHud, &Row, LineSize);
	if(Flow.Control("tclient-mini-debug", LineSize, Row, MarginSmall))
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmMiniDebug, "tclient-mini-debug", Localize("Show position and angle (mini debug)"), &g_Config.m_QmMiniDebug, &Row, LineSize);
	if(Flow.Control("tclient-render-cursor-spec", LineSize, Row, MarginSmall))
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRenderCursorSpec, "tclient-render-cursor-spec", Localize("Show the cursor while free spectating"), &g_Config.m_QmRenderCursorSpec, &Row, LineSize);
	if(Flow.Control("tclient-notify-when-last", LineSize, Row, MarginSmall))
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNotifyWhenLast, "tclient-notify-when-last", Localize("Notify when only one tee is still alive:"), &g_Config.m_QmNotifyWhenLast, &Row, LineSize);
	if(Flow.Control("tclient-show-center-line", LineSize, Row, MarginSmall))
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmShowCenter, "tclient-show-center-line", Localize("Show the screen center line"), &g_Config.m_QmShowCenter, &Row, LineSize);
	if((g_Config.m_QmRenderCursorSpec || g_Config.m_QmNotifyWhenLast || g_Config.m_QmShowCenter) && qm_card_catalog::QmCardRenderHook::Personalization(this, CurrentColumn, "tclient:hud", CurrentSettingsContentMetrics(), !Render))
	{
		if(g_Config.m_QmRenderCursorSpec)
		{
			if(Flow.Control("tclient-freeview-cursor-opacity", LineSize, Button, MarginSmall))
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-freeview-cursor-opacity", &g_Config.m_QmRenderCursorSpecAlpha, &g_Config.m_QmRenderCursorSpecAlpha, &Button, Localize("Freeview cursor opacity"), 0, 100);
		}
		if(g_Config.m_QmNotifyWhenLast)
		{
			if(Flow.Control("tclient-notify-last-text-color", LineSize, NotificationConfig, MarginSmall))
			{
				NotificationConfig.VSplitMid(&Button, &NotificationConfig);
				static CLineInput s_LastInput(g_Config.m_QmNotifyWhenLastText, sizeof(g_Config.m_QmNotifyWhenLastText));
				s_LastInput.SetEmptyText(Localize("You're the last one!"));
				ui_widget::InputField(TClientHudTextInputCtx, &s_LastInput, Button, nullptr, EditBoxFontSize);
				static CButtonContainer s_ClientNotifyWhenLastColor;
				DoLine_ColorPicker(&s_ClientNotifyWhenLastColor, CurrentSettingsContentMetrics(), &NotificationConfig, "", &g_Config.m_QmNotifyWhenLastColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
			}
			if(Flow.Control("tclient-notify-last-x", LineSize, Button, MarginSmall))
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-notify-last-x", &g_Config.m_QmNotifyWhenLastX, &g_Config.m_QmNotifyWhenLastX, &Button, Localize("Horizontal position"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
			if(Flow.Control("tclient-notify-last-y", LineSize, Button, MarginSmall))
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-notify-last-y", &g_Config.m_QmNotifyWhenLastY, &g_Config.m_QmNotifyWhenLastY, &Button, Localize("Vertical position"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
			if(Flow.Control("tclient-notify-last-size", LineSize, Button, MarginSmall))
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-notify-last-size", &g_Config.m_QmNotifyWhenLastSize, &g_Config.m_QmNotifyWhenLastSize, &Button, Localize("Font size"), 1, 50);
		}
		if(g_Config.m_QmShowCenter)
		{
			CUIRect CenterColor;
			if(Flow.Control("tclient-center-line-color", CurrentSettingsContentMetrics().m_ButtonHeight, CenterColor, MarginSmall))
			{
				static CButtonContainer s_ShowCenterLineColor;
				DoLine_ColorPicker(&s_ShowCenterLineColor, CurrentSettingsContentMetrics(), &CenterColor, Localize("Screen center line color"), &g_Config.m_QmShowCenterColor, DefaultConfig::QmShowCenterColor, false, nullptr, true);
			}
			if(Flow.Control("tclient-center-line-width", LineSize, Button, MarginSmall))
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-center-line-width", &g_Config.m_QmShowCenterWidth, &g_Config.m_QmShowCenterWidth, &Button, Localize("Screen center line width"), 0, 20);
		}
	}
	return CurrentColumn.y - SavedY;
}
SSettingsSection CMenus::BuildTClientAutoReplyCacheSection()
{
	SSettingsSection S;
	S.m_pName = "Auto reply";
	ConfigureSettingsCardSection(S, Localizable("Auto reply"), "tclient:auto-reply", [this](CUIRect &Col, bool Render) -> float { return LayoutTClientAutoReplyCacheSection(Col, Render); }, MarginBetweenSections);
	S.m_DependencyConfigInts = {&g_Config.m_QmAutoReplyMuted, &g_Config.m_QmAutoReplyMinimized};
	return S;
}
SSettingsSection CMenus::BuildTClientPetCacheSection()
{
	SSettingsSection S;
	S.m_pName = "Pet";
	ConfigureSettingsCardSection(S, "Pet", "tclient:pet", [this](CUIRect &Col, bool Render) -> float { return LayoutTClientPetCacheSection(Col, Render); }, MarginBetweenSections);
	S.m_DependencyConfigInts = {&g_Config.m_QmPetShow, &g_Config.m_QmPetSize, &g_Config.m_QmPetAlpha};
	return S;
}
SSettingsSection CMenus::BuildTClientHudCacheSection()
{
	SSettingsSection S;
	S.m_pName = "HUD";
	ConfigureSettingsCardSection(S, "HUD", "tclient:hud", [this](CUIRect &Col, bool Render) -> float { return LayoutTClientHudCacheSection(Col, Render); }, Margin);
	S.m_DependencyConfigInts = {
		&g_Config.m_QmMiniVoteHud,
		&g_Config.m_QmMiniDebug,
		&g_Config.m_QmRenderCursorSpec,
		&g_Config.m_QmNotifyWhenLast,
		&g_Config.m_QmNotifyWhenLastX,
		&g_Config.m_QmNotifyWhenLastY,
		&g_Config.m_QmNotifyWhenLastSize,
		&g_Config.m_QmShowCenter,
		&g_Config.m_QmShowCenterWidth,
	};
	S.m_DependencyConfigCols = {&g_Config.m_QmNotifyWhenLastColor, &g_Config.m_QmShowCenterColor};
	return S;
}
