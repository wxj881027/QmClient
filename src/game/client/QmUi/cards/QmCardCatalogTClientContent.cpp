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

#include <SDL_audio.h>

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
	const float LegacyHeaderHeight = TopMargin + HeadlineHeight + MarginSmall;
	Section.m_MeasureFn = [LayoutSection, LegacyHeaderHeight](CUIRect &Col) -> float {
		const float SavedY = Col.y;
		CUIRect LegacyContent = Col;
		LayoutSection(LegacyContent, false);
		Col.y = LegacyContent.y - LegacyHeaderHeight;
		return Col.y - SavedY;
	};
	Section.m_RenderCompactFn = [this, LayoutSection, LegacyHeaderHeight](CUIRect &Col) -> float {
		const float SavedY = Col.y;
		CUIRect LegacyContent = Col;
		LegacyContent.y -= LegacyHeaderHeight;
		LegacyContent.h += LegacyHeaderHeight;
		const float UiScale = CurrentSettingsContentMetrics().m_UiScale;
		const CUIRect SectionClip = ResolveSettingsFocusSafeClipRect(Col, UiScale);
		Ui()->ClipEnable(&SectionClip);
		LayoutSection(LegacyContent, true);
		Ui()->ClipDisable();
		Col.y = LegacyContent.y;
		return Col.y - SavedY;
	};
	Section.m_RenderFullFn = Section.m_RenderCompactFn;
}
float CMenus::LayoutTClientAutoReplyCacheSection(CUIRect &CurrentColumn, bool Render)
{
	CUIRect Label, ReplyRect, TmpRect;
	CUIRect BoxRect;
	IUiContext TClientAutoReplyTextInputCtx;
	TClientAutoReplyTextInputCtx.m_pUi = Ui();
	TClientAutoReplyTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	TClientAutoReplyTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	TClientAutoReplyTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_auto_reply_text_inputs");
	TClientAutoReplyTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	const float SavedY = CurrentColumn.y;
	CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
	BoxRect = CurrentColumn;
	CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
	if(Render)
	{
		DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-auto-reply-title", &Label, Localize("Auto reply"), HeadlineFontSize, TEXTALIGN_ML);
	}
	CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
	CTClientSettingsRowAllocator Rows(CurrentColumn);

	CUIRect MutedToggle = Rows.Next();
	if(Render)
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmAutoReplyMuted, "tclient-auto-reply-muted", Localize("Automatically reply to muted players"), &g_Config.m_QmAutoReplyMuted, &MutedToggle, LineSize);
	if(g_Config.m_QmAutoReplyMuted)
	{
		ReplyRect = Rows.Next();
		if(Render)
		{
			static CLineInput s_MutedReply(g_Config.m_QmAutoReplyMutedMessage, sizeof(g_Config.m_QmAutoReplyMutedMessage));
			s_MutedReply.SetEmptyText(Localize("I muted you"));
			ui_widget::InputField(TClientAutoReplyTextInputCtx, &s_MutedReply, ReplyRect, nullptr, EditBoxFontSize);
		}
	}
	CUIRect MinimizedToggle = Rows.Next();
	if(Render)
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmAutoReplyMinimized, "tclient-auto-reply-minimized", Localize("Automatically reply while the window is unfocused"), &g_Config.m_QmAutoReplyMinimized, &MinimizedToggle, LineSize);
	if(g_Config.m_QmAutoReplyMinimized)
	{
		ReplyRect = Rows.Next();
		if(Render)
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
	CUIRect Label, Button, TmpRect, PetSkinBox;
	CUIRect BoxRect;
	IUiContext TClientPetTextInputCtx;
	TClientPetTextInputCtx.m_pUi = Ui();
	TClientPetTextInputCtx.m_pTooltips = &GameClient()->m_Tooltips;
	TClientPetTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	TClientPetTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	TClientPetTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_pet_text_inputs");
	TClientPetTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	const float SavedY = CurrentColumn.y;
	CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
	BoxRect = CurrentColumn;
	CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
	if(Render)
		DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Pet"), HeadlineFontSize, TEXTALIGN_ML);
	CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
	CTClientSettingsRowAllocator Rows(CurrentColumn);
	CUIRect ShowPetRow = Rows.Next();
	if(Render)
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmPetShow, "tclient-show-pet", Localize("Show the pet"), &g_Config.m_QmPetShow, &ShowPetRow, LineSize);
	Button = Rows.Next();
	if(Render)
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, "tclient-pet-size", &g_Config.m_QmPetSize, &g_Config.m_QmPetSize, &Button, Localize("Pet size"), 10, 500, &CUi::ms_LinearScrollbarScale, 0, "%");
	Button = Rows.Next();
	if(Render)
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-pet-alpha", &g_Config.m_QmPetAlpha, &g_Config.m_QmPetAlpha, &Button, Localize("Pet alpha"), 10, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	PetSkinBox = Rows.Next();
	if(Render)
	{
		PetSkinBox.VSplitMid(&Label, &Button);
		DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Pet Skin:"), FontSize, TEXTALIGN_ML);
		static CLineInput s_PetSkin(g_Config.m_QmPetSkin, sizeof(g_Config.m_QmPetSkin));
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(&s_PetSkin, &PetSkinBox, g_Config.m_QmPetSkin, &Label);
		ui_widget::InputField(TClientPetTextInputCtx, &s_PetSkin, Button, nullptr, EditBoxFontSize);
	}
	return CurrentColumn.y - SavedY;
}
float CMenus::LayoutTClientHudCacheSection(CUIRect &CurrentColumn, bool Render)
{
	CUIRect Label, Button, NotificationConfig, TmpRect;
	CUIRect BoxRect;
	IUiContext TClientHudTextInputCtx;
	TClientHudTextInputCtx.m_pUi = Ui();
	TClientHudTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	TClientHudTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	TClientHudTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_hud_text_inputs");
	TClientHudTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	const float SavedY = CurrentColumn.y;
	CurrentColumn.HSplitTop(Margin, nullptr, &CurrentColumn);
	BoxRect = CurrentColumn;
	CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
	if(Render)
		DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("HUD"), HeadlineFontSize, TEXTALIGN_ML);
	CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
	CTClientSettingsRowAllocator Rows(CurrentColumn);
	CUIRect Row = Rows.Next();
	if(Render)
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmMiniVoteHud, "tclient-mini-vote-hud", Localize("Show compact vote HUD"), &g_Config.m_QmMiniVoteHud, &Row, LineSize);
	Row = Rows.Next();
	if(Render)
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmMiniDebug, "tclient-mini-debug", Localize("Show position and angle (mini debug)"), &g_Config.m_QmMiniDebug, &Row, LineSize);
	Row = Rows.Next();
	if(Render)
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRenderCursorSpec, "tclient-render-cursor-spec", Localize("Show the cursor while free spectating"), &g_Config.m_QmRenderCursorSpec, &Row, LineSize);
	if(g_Config.m_QmRenderCursorSpec)
	{
		Button = Rows.Next();
		if(Render)
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-freeview-cursor-opacity", &g_Config.m_QmRenderCursorSpecAlpha, &g_Config.m_QmRenderCursorSpecAlpha, &Button, Localize("Freeview cursor opacity"), 0, 100);
	}
	Row = Rows.Next();
	if(Render)
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNotifyWhenLast, "tclient-notify-when-last", Localize("Notify when only one tee is still alive:"), &g_Config.m_QmNotifyWhenLast, &Row, LineSize);
	if(g_Config.m_QmNotifyWhenLast)
	{
		NotificationConfig = Rows.Next();
		const CUIRect NotificationX = Rows.Next();
		const CUIRect NotificationY = Rows.Next();
		const CUIRect NotificationSize = Rows.Next();
		if(Render)
		{
			NotificationConfig.VSplitMid(&Button, &NotificationConfig);
			static CLineInput s_LastInput(g_Config.m_QmNotifyWhenLastText, sizeof(g_Config.m_QmNotifyWhenLastText));
			s_LastInput.SetEmptyText(Localize("You're the last one!"));
			ui_widget::InputField(TClientHudTextInputCtx, &s_LastInput, Button, nullptr, EditBoxFontSize);
			static CButtonContainer s_ClientNotifyWhenLastColor;
			DoLine_ColorPicker(&s_ClientNotifyWhenLastColor, CurrentSettingsContentMetrics(), &NotificationConfig, "", &g_Config.m_QmNotifyWhenLastColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-notify-last-x", &g_Config.m_QmNotifyWhenLastX, &g_Config.m_QmNotifyWhenLastX, &NotificationX, Localize("Horizontal position"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-notify-last-y", &g_Config.m_QmNotifyWhenLastY, &g_Config.m_QmNotifyWhenLastY, &NotificationY, Localize("Vertical position"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-notify-last-size", &g_Config.m_QmNotifyWhenLastSize, &g_Config.m_QmNotifyWhenLastSize, &NotificationSize, Localize("Font size"), 1, 50);
		}
	}
	Row = Rows.Next();
	if(Render)
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmShowCenter, "tclient-show-center-line", Localize("Show the screen center line"), &g_Config.m_QmShowCenter, &Row, LineSize);
	if(g_Config.m_QmShowCenter)
	{
		CUIRect CenterColor = Rows.Next();
		const CUIRect CenterWidth = Rows.Next();
		if(Render)
		{
			static CButtonContainer s_ShowCenterLineColor;
			DoLine_ColorPicker(&s_ShowCenterLineColor, CurrentSettingsContentMetrics(), &CenterColor, Localize("Screen center line color"), &g_Config.m_QmShowCenterColor, DefaultConfig::QmShowCenterColor, false, nullptr, true);
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-center-line-width", &g_Config.m_QmShowCenterWidth, &g_Config.m_QmShowCenterWidth, &CenterWidth, Localize("Screen center line width"), 0, 20);
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
