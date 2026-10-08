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
#include <game/client/components/qmclient/decorative_throw_policy.h>
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

void CMenus::RenderQmFunctionKeyBindsContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth)
{
	static CButtonContainer s_ReaderButtonDummyPseudo, s_ClearButtonDummyPseudo,
		s_ReaderButtonDeepfly, s_ClearButtonDeepfly,
		s_ReaderButton45Degrees, s_ClearButton45Degrees,
		s_ReaderButtonSmallSens, s_ClearButtonSmallSens,
		s_ReaderButtonLeftJump, s_ClearButtonLeftJump,
		s_ReaderButtonRightJump, s_ClearButtonRightJump,
		s_ReaderButtonWeaponTrajectory, s_ClearButtonWeaponTrajectory,
		s_ReaderButtonTimeoutDisconnect, s_ClearButtonTimeoutDisconnect;
	[[maybe_unused]] static CButtonContainer s_ReaderButtonDeepflyToggle, s_ClearButtonDeepflyToggle;

	RenderQmHudKeyBindRow(Content, s_ReaderButtonDummyPseudo, s_ClearButtonDummyPseudo,
		Localize("HDF"), "+toggle cl_dummy_hammer 1 0", LineHeight, BodySize, LineSpacing, LabelWidth);
	RenderQmHudKeyBindRow(Content, s_ReaderButtonDeepfly, s_ClearButtonDeepfly,
		Localize("DF"), "+fire; +toggle cl_dummy_hammer 1 0", LineHeight, BodySize, LineSpacing, LabelWidth);
	RenderQmHudKeyBindRow(Content, s_ReaderButton45Degrees, s_ClearButton45Degrees,
		Localize("45° Aim"), "echo You are using 45-degree aim;+toggle cl_mouse_max_distance 2 400; +toggle_restore inp_mousesens 1", LineHeight, BodySize, LineSpacing, LabelWidth);
	RenderQmHudKeyBindRow(Content, s_ReaderButtonSmallSens, s_ClearButtonSmallSens,
		Localize("Gap aim rescue"), "+toggle_restore inp_mousesens 1", LineHeight, BodySize, LineSpacing, LabelWidth);
	RenderQmHudKeyBindRow(Content, s_ReaderButtonLeftJump, s_ClearButtonLeftJump,
		Localize("Left jump"), "+jump; +left", LineHeight, BodySize, LineSpacing, LabelWidth);
	RenderQmHudKeyBindRow(Content, s_ReaderButtonRightJump, s_ClearButtonRightJump,
		Localize("Right jump"), "+jump; +right", LineHeight, BodySize, LineSpacing, LabelWidth);
	RenderQmHudKeyBindRow(Content, s_ReaderButtonWeaponTrajectory, s_ClearButtonWeaponTrajectory,
		Localize("Weapon Trajectory"), "+showweapontrajectory", LineHeight, BodySize, LineSpacing, LabelWidth);
	RenderQmHudKeyBindRow(Content, s_ReaderButtonTimeoutDisconnect, s_ClearButtonTimeoutDisconnect,
		Localize("Active disconnect"), "qm_timeout_disconnect", LineHeight, BodySize, LineSpacing, LabelWidth);
}

void CMenus::RenderQmFunctionGoresActorContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	IUiContext TextInputCtx = SettingsUiContext("settings_qmclient_gores_actor_text_inputs", BodySize / ui_token::font::BODY);
	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderQmFunctionCheckbox(&g_Config.m_QmFreezeChatEnabled, "qmclient-gores-actor-enable", Localize("Auto chat in water"), &g_Config.m_QmFreezeChatEnabled, &Row, PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	if(!g_Config.m_QmFreezeChatEnabled)
		return;

	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderQmFunctionCheckbox(&g_Config.m_QmFreezeChatEmoticon, "qmclient-gores-actor-emoticon", Localize("Send emoticon in water"), &g_Config.m_QmFreezeChatEmoticon, &Row, PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	if(g_Config.m_QmFreezeChatEmoticon)
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		DoSettingsScrollbarOption(SETTINGS_QMCLIENT, m_QmClientSettingsTab, m_QmClientSettingsTab, "qmclient-gores-actor-emoticon-id", &g_Config.m_QmFreezeChatEmoticonId, &g_Config.m_QmFreezeChatEmoticonId, &Row, Localize("Emoticon ID"), 0, 15);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-gores-actor-chat-message", &LabelColumn, Localize("Chat message"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
	static CLineInput s_FreezeChatMessageQmClient(g_Config.m_QmFreezeChatMessage, sizeof(g_Config.m_QmFreezeChatMessage));
	s_FreezeChatMessageQmClient.SetEmptyText(Localize("Leave empty to disable"));
	ui_widget::InputField(TextInputCtx, &s_FreezeChatMessageQmClient, ControlColumn, Localize("Leave empty to disable"), BodySize);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	Content.HSplitTop(LineHeight, &Row, &Content);
	DoSettingsScrollbarOption(SETTINGS_QMCLIENT, m_QmClientSettingsTab, m_QmClientSettingsTab, "qmclient-gores-actor-send-probability", &g_Config.m_QmFreezeChatChance, &g_Config.m_QmFreezeChatChance, &Row, Localize("Send probability"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	Content.HSplitTop(LineSpacing, nullptr, &Content);
}

void CMenus::RenderQmFunctionGoresContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	static CButtonContainer s_ReaderButtonGoresToggle, s_ClearButtonGoresToggle;
	static CButtonContainer s_AxiomPasswordToggleButton, s_AxiomDummyPasswordToggleButton;
	static bool s_ShowAxiomPassword = false;
	static bool s_ShowAxiomDummyPassword = false;
	CUIRect Row, LabelColumn, ControlColumn;
	auto RenderCheckbox = [this, &Content, &Row, LineHeight, LineSpacing, PrewarmOnly](const void *pId, const char *pTextId, const char *pText, int *pValue) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		RenderQmFunctionCheckbox(pId, pTextId, Localize(pText), pValue, &Row, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	RenderCheckbox(&g_Config.m_QmGores, "qmclient-gores-enable", "Enable Gores mode", &g_Config.m_QmGores);
	RenderCheckbox(&g_Config.m_QmAxiomAutoLogin, "qmclient-gores-axiom-auto-login", "Auto login Axiom server", &g_Config.m_QmAxiomAutoLogin);
	const char *pAxiomHelp = Localize("Use the passwords registered on Axiom for your main and dummy accounts separately. This option does not register accounts.");
	const float HelpSize = BodySize * 0.85f;
	const float HelpHeight = std::max(HelpSize, TextRender()->TextBoundingBox(HelpSize, pAxiomHelp, -1, std::max(1.0f, Content.w)).m_H);
	Content.HSplitTop(HelpHeight, &Row, &Content);
	SLabelProperties HelpProps;
	HelpProps.m_MaxWidth = Row.w;
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-gores-axiom-help", &Row, pAxiomHelp, HelpSize, TEXTALIGN_TL, HelpProps);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	if(g_Config.m_QmAxiomAutoLogin)
	{
		IUiContext TextInputCtx = SettingsUiContext("settings_qmclient_gores_text_inputs", BodySize / ui_token::font::BODY);
		auto RenderPassword = [&](const char *pTextId, const char *pText, CLineInput &Input, CButtonContainer &ToggleButton, bool &Visible) {
			Content.HSplitTop(LineHeight, &Row, &Content);
			Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
			DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pTextId, &LabelColumn, Localize(pText), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
			Input.SetHidden(!Visible);
			ui_widget::SInputFieldOptions Options;
			Options.m_FontSize = BodySize;
			Options.m_pTrailingActionId = &ToggleButton;
			Options.m_pTrailingActionIcon = Visible ? FONT_ICON_EYE_SLASH : FONT_ICON_EYE;
			Options.m_TrailingActionQmIcon = static_cast<int>(Visible ? EQmIcon::EYE_OFF : EQmIcon::EYE);
			Options.m_TrailingWidth = ControlColumn.h;
			if(ui_widget::InputField(TextInputCtx, &Input, ControlColumn, Options).m_TrailingAction)
				Visible = !Visible;
			Content.HSplitTop(LineSpacing, nullptr, &Content);
		};
		static CLineInput s_AxiomLoginPassword(g_Config.m_QmAxiomLoginPassword, sizeof(g_Config.m_QmAxiomLoginPassword));
		static CLineInput s_AxiomDummyLoginPassword(g_Config.m_QmAxiomDummyLoginPassword, sizeof(g_Config.m_QmAxiomDummyLoginPassword));
		RenderPassword("qmclient-gores-axiom-main-password", "Axiom main account password", s_AxiomLoginPassword, s_AxiomPasswordToggleButton, s_ShowAxiomPassword);
		RenderPassword("qmclient-gores-axiom-dummy-password", "Axiom dummy password", s_AxiomDummyLoginPassword, s_AxiomDummyPasswordToggleButton, s_ShowAxiomDummyPassword);
	}

	RenderCheckbox(&g_Config.m_QmGoresAutoEnable, "qmclient-gores-auto-enable", "Auto enable in Gores mode", &g_Config.m_QmGoresAutoEnable);
	if(g_Config.m_QmGores || g_Config.m_QmGoresAutoEnable)
	{
		RenderCheckbox(&g_Config.m_QmGoresAutoWeaponSwitch, "qmclient-gores-auto-weapon-switch", "Auto weapon switch", &g_Config.m_QmGoresAutoWeaponSwitch);
		RenderCheckbox(&g_Config.m_QmGoresFastInput, "qmclient-gores-fast-input", "Auto-toggle fast input", &g_Config.m_QmGoresFastInput);
		RenderCheckbox(&g_Config.m_QmGoresFastInputOthers, "qmclient-gores-fast-input-others", "Auto-toggle fast input others", &g_Config.m_QmGoresFastInputOthers);
		RenderCheckbox(&g_Config.m_QmGoresDisableIfWeapons, "qmclient-gores-disable-if-weapons", "Disable after picking up other weapons", &g_Config.m_QmGoresDisableIfWeapons);
		RenderCheckbox(&g_Config.m_QmGoresDisableDummyHammer, "qmclient-gores-disable-dummy-hammer", "Temporarily disable dummy hammering", &g_Config.m_QmGoresDisableDummyHammer);
		RenderCheckbox(&g_Config.m_QmGoresHideGuides, "qmclient-gores-hide-guides", "Hide guide lines", &g_Config.m_QmGoresHideGuides);
		RenderCheckbox(&g_Config.m_QmGoresSuppressSwitchAnim, "qmclient-gores-suppress-switch-anim", "Skip switch animation when hammering", &g_Config.m_QmGoresSuppressSwitchAnim);
	}

	Content.HSplitTop(LineHeight, &Row, &Content);
	CUIRect BindLabel, BindKey;
	Row.VSplitLeft(LabelWidth, &BindLabel, &BindKey);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-gores-mode-key", &BindLabel, Localize("Gores mode key"), BodySize, TEXTALIGN_ML, {}, (int)BindLabel.w);
	CBindSlot GoresBind(KEY_UNKNOWN, KeyModifier::NONE);
	const auto GoresIt = g_CommandBindCache.find("toggle qm_gores 0 1");
	if(GoresIt != g_CommandBindCache.end())
		GoresBind = GoresIt->second;
	const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&s_ReaderButtonGoresToggle, &s_ClearButtonGoresToggle, &BindKey, GoresBind, false);
	if(Result.m_Bind == GoresBind)
		return;
	if(GoresBind.m_Key != KEY_UNKNOWN)
		GameClient()->m_Binds.Bind(GoresBind.m_Key, "", false, GoresBind.m_ModifierMask);
	if(Result.m_Bind.m_Key != KEY_UNKNOWN)
	{
		GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, "toggle qm_gores 0 1", false, Result.m_Bind.m_ModifierMask);
		g_CommandBindCache.insert_or_assign("toggle qm_gores 0 1", Result.m_Bind);
	}
	else
	{
		g_CommandBindCache.erase("toggle qm_gores 0 1");
	}
	(void)PrewarmOnly;
}

void CMenus::RenderQmFunctionSoloSplitContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	static CButtonContainer s_ReaderButtonSoloSplit, s_ClearButtonSoloSplit;
	static CButtonContainer s_ReaderButtonSoloSplitEnter, s_ClearButtonSoloSplitEnter;
	static CButtonContainer s_ReaderButtonSoloSplitLeave, s_ClearButtonSoloSplitLeave;
	static const void *s_RestoreTeamInputId = &s_RestoreTeamInputId;
	// 自动锁队配置与 HJAssist 卡共用 qm_auto_team_lock；HJAssist 已用配置地址作控件 id，
	// 两卡可能同屏显示，这里改用独立静态地址避免 hot/active 项串扰。
	static const void *s_AutoTeamLockCheckboxId = &s_AutoTeamLockCheckboxId;
	CUIRect Row, BindLabel, BindKey, LabelColumn, ControlColumn;
	auto RenderCheckbox = [&](const void *pId, const char *pTextId, const char *pText, int *pValue) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		RenderQmFunctionCheckbox(pId, pTextId, Localize(pText), pValue, &Row, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	RenderCheckbox(&g_Config.m_QmSoloSplitLinkDummy, "qmclient-solo-split-link-dummy", "Auto-connect dummy", &g_Config.m_QmSoloSplitLinkDummy);
	RenderCheckbox(s_AutoTeamLockCheckboxId, "qmclient-solo-split-auto-team-lock", "Auto team lock", &g_Config.m_QmAutoTeamLock);
	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-solo-split-restore-team", &LabelColumn, Localize("Team when leaving solo split"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
	RenderQmSettingsSliderWithValueInput(s_RestoreTeamInputId, ControlColumn, &g_Config.m_QmSoloSplitRestoreTeam, 0, 63, "", PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	// 只保留键位行：按下触发 qm_solo_split（toggle 语义，非 toggle 命令）。
	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &BindLabel, &BindKey);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-solo-split-key", &BindLabel, Localize("Solo split key"), BodySize, TEXTALIGN_ML, {}, (int)BindLabel.w);
	CBindSlot SoloSplitBind(KEY_UNKNOWN, KeyModifier::NONE);
	const auto SoloSplitIt = g_CommandBindCache.find("qm_solo_split");
	if(SoloSplitIt != g_CommandBindCache.end())
		SoloSplitBind = SoloSplitIt->second;
	const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&s_ReaderButtonSoloSplit, &s_ClearButtonSoloSplit, &BindKey, SoloSplitBind, false);
	if(Result.m_Bind != SoloSplitBind)
	{
		if(SoloSplitBind.m_Key != KEY_UNKNOWN)
			GameClient()->m_Binds.Bind(SoloSplitBind.m_Key, "", false, SoloSplitBind.m_ModifierMask);
		if(Result.m_Bind.m_Key != KEY_UNKNOWN)
		{
			GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, "qm_solo_split", false, Result.m_Bind.m_ModifierMask);
			g_CommandBindCache.insert_or_assign("qm_solo_split", Result.m_Bind);
		}
		else
			g_CommandBindCache.erase("qm_solo_split");
	}
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	auto RenderBind = [&](const char *pCommand, const char *pTextId, const char *pText, CButtonContainer &Reader, CButtonContainer &Clear) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &BindLabel, &BindKey);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pTextId, &BindLabel, pText, BodySize, TEXTALIGN_ML, {}, (int)BindLabel.w);
		CBindSlot Bind(KEY_UNKNOWN, KeyModifier::NONE);
		const auto It = g_CommandBindCache.find(pCommand);
		if(It != g_CommandBindCache.end())
			Bind = It->second;
		const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&Reader, &Clear, &BindKey, Bind, false);
		if(Result.m_Bind != Bind)
		{
			if(Bind.m_Key != KEY_UNKNOWN)
				GameClient()->m_Binds.Bind(Bind.m_Key, "", false, Bind.m_ModifierMask);
			if(Result.m_Bind.m_Key != KEY_UNKNOWN)
			{
				GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, pCommand, false, Result.m_Bind.m_ModifierMask);
				g_CommandBindCache.insert_or_assign(pCommand, Result.m_Bind);
			}
			else
				g_CommandBindCache.erase(pCommand);
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	RenderBind("qm_solo_split_enter", "qmclient-solo-split-enter-key", Localize("Solo split enter key"), s_ReaderButtonSoloSplitEnter, s_ClearButtonSoloSplitEnter);
	RenderBind("qm_solo_split_leave", "qmclient-solo-split-leave-key", Localize("Solo split leave key"), s_ReaderButtonSoloSplitLeave, s_ClearButtonSoloSplitLeave);
	(void)PrewarmOnly;
}

void CMenus::RenderQmFunctionJumpHintContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderQmFunctionCheckbox(&g_Config.m_QmJumpHint, "Position jump hint", Localize("Position jump hint"), &g_Config.m_QmJumpHint, &Row, PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	static CButtonContainer s_QmJumpHintColorId;
	DoLine_ColorPicker(&s_QmJumpHintColorId, CurrentSettingsContentMetrics(), &Content, Localize("Text color"), &g_Config.m_QmJumpHintColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), false);

	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pTextId, &LabelColumn, Localize(pText), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	static int s_QmJumpHintXInputId;
	static int s_QmJumpHintYInputId;
	static int s_QmJumpHintSizeInputId;
	RenderValue("qmclient-jump-hint-horizontal-position", "Horizontal position", &s_QmJumpHintXInputId, &g_Config.m_QmJumpHintX, 0, 100, "%");
	RenderValue("qmclient-jump-hint-vertical-position", "Vertical position", &s_QmJumpHintYInputId, &g_Config.m_QmJumpHintY, 0, 100, "%");
	RenderValue("qmclient-jump-hint-font-size", "Font size", &s_QmJumpHintSizeInputId, &g_Config.m_QmJumpHintSize, 1, 50);
}

void CMenus::RenderQmFunctionWeaponTrajectoryContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	CUIElement &DisplayModeLabel = SettingsTextElement(SETTINGS_QMCLIENT, m_QmClientSettingsTab, "qmclient-display-mode");
	DoSettingsLabelStreamed(DisplayModeLabel, &LabelColumn, Localize("Display mode"), BodySize, TEXTALIGN_ML);
	static std::vector<const char *> s_WeaponTrajectoryModeNames;
	s_WeaponTrajectoryModeNames = {Localize("Off"), Localize("Show on key"), Localize("Always show")};
	static CUi::SDropDownState s_WeaponTrajectoryModeDropDownState;
	static CScrollRegion s_WeaponTrajectoryModeDropDownScrollRegion;
	s_WeaponTrajectoryModeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_WeaponTrajectoryModeDropDownScrollRegion;
	const int WeaponTrajectoryModeNew = DoSettingsDropDown(&ControlColumn, std::clamp(g_Config.m_QmWeaponTrajectory, 0, 2), s_WeaponTrajectoryModeNames.data(), s_WeaponTrajectoryModeNames.size(), s_WeaponTrajectoryModeDropDownState);
	if(g_Config.m_QmWeaponTrajectory != WeaponTrajectoryModeNew)
		g_Config.m_QmWeaponTrajectory = WeaponTrajectoryModeNew;
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	if(g_Config.m_QmWeaponTrajectory == 0)
		return;

	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderQmFunctionCheckbox(&g_Config.m_QmWeaponTrajectoryGun, "qmclient-weapon-trajectory-gun", Localize("Pistol guide line"), &g_Config.m_QmWeaponTrajectoryGun, &Row, PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderQmFunctionCheckbox(&g_Config.m_QmWeaponTrajectoryNinja, "qmclient-weapon-trajectory-ninja", Localize("Predict ninja path"), &g_Config.m_QmWeaponTrajectoryNinja, &Row, PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	static CButtonContainer s_WeaponTrajectoryColorId;
	DoLine_ColorPicker(&s_WeaponTrajectoryColorId, CurrentSettingsContentMetrics(), &Content, Localize("Guide line color"), &g_Config.m_QmWeaponTrajectoryColor, ColorRGBA(1.0f, 0.6f, 0.2f, 1.0f), false);
	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pTextId, &LabelColumn, Localize(pText), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	static int s_QmWeaponTrajectoryWidthInputId;
	static int s_QmWeaponTrajectoryAlphaInputId;
	RenderValue("qmclient-weapon-trajectory-line-width", "Line width", &s_QmWeaponTrajectoryWidthInputId, &g_Config.m_QmWeaponTrajectoryWidth, 1, 10);
	RenderValue("qmclient-weapon-trajectory-opacity", "Opacity", &s_QmWeaponTrajectoryAlphaInputId, &g_Config.m_QmWeaponTrajectoryAlpha, 0, 100, "%");
}

void CMenus::RenderQmFunctionFriendNotifyContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	IUiContext TextInputCtx = SettingsUiContext("settings_qmclient_friend_enter_text_inputs", BodySize / ui_token::font::BODY);
	CUIRect Row, LabelColumn, ControlColumn;
	auto RenderCheckbox = [&](const void *pId, const char *pTextId, const char *pText, int *pValue) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		RenderQmFunctionCheckbox(pId, pTextId, Localize(pText), pValue, &Row, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pTextId, &LabelColumn, Localize(pText), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	auto RenderText = [&](const char *pTextId, const char *pText, CLineInput *pInput, const char *pPlaceholder) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pTextId, &LabelColumn, Localize(pText), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		pInput->SetEmptyText(Localize(pPlaceholder));
		ui_widget::InputField(TextInputCtx, pInput, ControlColumn, Localize(pPlaceholder), BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};

	static int s_QmFriendAutoFollowDelayInputId;
	static int s_QmFriendOnlineRefreshSecondsInputId;
	RenderCheckbox(&g_Config.m_QmFriendOnlineNotify, "Notify when friends come online", "Notify when friends come online", &g_Config.m_QmFriendOnlineNotify);
	RenderCheckbox(&g_Config.m_QmFriendOnlineAutoRefresh, "Auto refresh server list", "Auto refresh server list", &g_Config.m_QmFriendOnlineAutoRefresh);
	RenderValue("qmclient-friend-auto-follow-delay", "Auto-follow delay", &s_QmFriendAutoFollowDelayInputId, &g_Config.m_QmFriendAutoFollowDelay, 0, 30, "s");
	if(g_Config.m_QmFriendOnlineAutoRefresh)
		RenderValue("qmclient-friend-notifications-refresh-interval", "Refresh interval", &s_QmFriendOnlineRefreshSecondsInputId, &g_Config.m_QmFriendOnlineRefreshSeconds, 5, 300, "s");
	RenderCheckbox(&g_Config.m_QmFriendEnterAutoGreet, "Auto greet friends entering map", "Auto greet friends entering map", &g_Config.m_QmFriendEnterAutoGreet);
	RenderCheckbox(&g_Config.m_QmFriendEnterBroadcast, "Large text announcement for friend joining", "Large text announcement for friend joining", &g_Config.m_QmFriendEnterBroadcast);
	if(g_Config.m_QmFriendEnterBroadcast)
	{
		static CLineInput s_FriendEnterBroadcastText(g_Config.m_QmFriendEnterBroadcastText, sizeof(g_Config.m_QmFriendEnterBroadcastText));
		RenderText("qmclient-friend-notifications-large-text-content", "Large text content", &s_FriendEnterBroadcastText, "Please use %s as friend name");
	}
	if(g_Config.m_QmFriendEnterAutoGreet)
	{
		static CLineInput s_FriendEnterGreetText(g_Config.m_QmFriendEnterGreetText, sizeof(g_Config.m_QmFriendEnterGreetText));
		RenderText("qmclient-friend-notifications-greeting-text", "Greeting text", &s_FriendEnterGreetText, "Leave empty to disable");
	}
}

void CMenus::RenderQmFunctionEmoticonsContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth)
{
	// 自 MiniFeatures 列表迁出（R3）：表情相关开关独立成 qm:emoticons 卡，
	// 避免同一个开关在两张卡里各出现一次。文案 id 沿用本地约定（英文源串作为 textId），
	// 不使用远程的 "qm-emoticons-*" 键，避免 12 语文案整体退回英文。
	static CButtonContainer s_ReaderButtonLaunchEmote;
	static CButtonContainer s_ClearButtonLaunchEmote;
	CUIRect Row;
	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderQmFunctionCheckbox(&g_Config.m_QmShowOtherSuperEmotes, "Show other players' large emoticons", Localize("Show other players' large emoticons"), &g_Config.m_QmShowOtherSuperEmotes, &Row, false);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderQmFunctionCheckbox(&g_Config.m_QmShowOtherLaunchEmotes, "Show other players' launched emoticons", Localize("Show other players' launched emoticons"), &g_Config.m_QmShowOtherLaunchEmotes, &Row, false);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderQmFunctionCheckbox(&g_Config.m_QmDecorativeThrows, "Decorative throws", Localize("Decorative throws"), &g_Config.m_QmDecorativeThrows, &Row, false);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	static CButtonContainer s_aThrowReaders[QmDecorativeThrow::COUNT];
	static CButtonContainer s_aThrowClearers[QmDecorativeThrow::COUNT];
	const char *apThrowLabels[] = {Localize("Throw grass key"), Localize("Throw tomato key"), Localize("Throw egg key")};
	const char *apThrowCommands[] = {"qm_throw grass", "qm_throw tomato", "qm_throw egg"};
	for(int Type = 0; Type < QmDecorativeThrow::COUNT; ++Type)
		RenderQmHudKeyBindRow(Content, s_aThrowReaders[Type], s_aThrowClearers[Type], apThrowLabels[Type], apThrowCommands[Type], LineHeight, BodySize, LineSpacing, LabelWidth);
	RenderQmHudKeyBindRow(Content, s_ReaderButtonLaunchEmote, s_ClearButtonLaunchEmote,
		Localize("Launch emote key"), "toggle_emote_launcher", LineHeight, BodySize, LineSpacing, LabelWidth);
}

void CMenus::RenderQmFunctionMiniFeaturesContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CUIRect Row;
	auto RenderCheckbox = [this, &Content, &Row, LineHeight, LineSpacing, PrewarmOnly](const void *pId, const char *pText, int *pValue) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		RenderQmFunctionCheckbox(pId, pText, Localize(pText), pValue, &Row, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	auto RenderCheckboxTipped = [this, &Content, &Row, LineHeight, LineSpacing, PrewarmOnly](const void *pId, const char *pText, const char *pTooltip, int *pValue) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		RenderQmFunctionCheckbox(pId, pText, Localize(pText), pValue, &Row, PrewarmOnly, pTooltip);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	RenderCheckbox(&g_Config.m_QmFootParticles, "Local particle effects", &g_Config.m_QmFootParticles);
	RenderCheckbox(&g_Config.m_QmClientMarkTrail, "Remote particle effects", &g_Config.m_QmClientMarkTrail);
	RenderCheckbox(&g_Config.m_QmClientShowBadge, "Show Qm badge", &g_Config.m_QmClientShowBadge);
	RenderCheckbox(&g_Config.m_QmAutoUpdate, "Automatic updates", &g_Config.m_QmAutoUpdate);
	RenderCheckbox(&g_Config.m_QmShowOutdatedVersionWarning, "Show outdated version warning", &g_Config.m_QmShowOutdatedVersionWarning);
	// 计分板相关的 5 项（更好的计分板/积分检查/死亡后显示/滚轮滚动/过滤器）只在 qm:better_scoreboard 卡里渲染一次，
	// 这里不再重复，避免同一条配置在两张卡里各出现一遍。
	RenderCheckbox(&g_Config.m_QmHideJoinServerInfo, "Hide server information on join", &g_Config.m_QmHideJoinServerInfo);
	RenderCheckboxTipped(&g_Config.m_QmShowTuneZoneColors, "Show tune zone colors", Localize("Color map tune zones by their tune zone number"), &g_Config.m_QmShowTuneZoneColors);
	// 回退语义在加载期读取：开关变化由 CGameClient::OnRender 的兜底轮询统一触发热重载，
	// 这里只负责渲染复选框，不在渲染遍里做加载副作用。
	RenderCheckboxTipped(&g_Config.m_QmBlankAssetFallback, "Blank asset auto fallback", Localize("Automatically fall back to the default asset when a custom asset sprite is fully transparent; turn off to keep blank sprites invisible (e.g. to hide effects)"), &g_Config.m_QmBlankAssetFallback);
	RenderCheckbox(&g_Config.m_QmMessageMerge, "Message merging", &g_Config.m_QmMessageMerge);
	RenderCheckbox(&g_Config.m_QmShortServerNames, "Short server names", &g_Config.m_QmShortServerNames);
	RenderCheckbox(&g_Config.m_QmRepeatEnabled, "Enable repeat", &g_Config.m_QmRepeatEnabled);
	RenderCheckbox(&g_Config.m_QmRandomEmoteOnHit, "Random emoticon", &g_Config.m_QmRandomEmoteOnHit);
	// 两个表情开关已迁入 qm:emoticons 卡（RenderQmFunctionEmoticonsContent），此处不再重复渲染。
	RenderCheckbox(&g_Config.m_QmComboPopup, "Combo", &g_Config.m_QmComboPopup);
	RenderCheckbox(&g_Config.m_QmSayNoPop, "Hide input emoticon", &g_Config.m_QmSayNoPop);
	// 关闭赞助提醒时不弹确认框，而是用同样式的灵动岛问一句，
	// 避免把「关掉一个提醒」变成需要连点两次的操作。
	Content.HSplitTop(LineHeight, &Row, &Content);
	const int SponsorNudgeBefore = g_Config.m_QmSponsorNudge;
	RenderQmFunctionCheckbox(&g_Config.m_QmSponsorNudge, "Sponsor reminder", Localize("Sponsor reminder"), &g_Config.m_QmSponsorNudge, &Row, PrewarmOnly);
	if(!PrewarmOnly && SponsorNudgeBefore != 0 && g_Config.m_QmSponsorNudge == 0)
		GameClient()->ShowSponsorNudgeFarewell();
	else if(!PrewarmOnly && SponsorNudgeBefore == 0 && g_Config.m_QmSponsorNudge != 0)
		GameClient()->HideSponsorNudgeFarewell();
	Content.HSplitTop(LineSpacing, nullptr, &Content);
}
