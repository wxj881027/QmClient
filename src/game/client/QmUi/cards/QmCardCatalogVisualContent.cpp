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

void CMenus::RenderQmVisualStreamerContent(CUIRect &Content, float LineHeight, float LineSpacing)
{
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmStreamerHideNames, "Replace non-friend names with ID", Localize("Replace non-friend names with ID"), &g_Config.m_QmStreamerHideNames);
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmStreamerHideSkins, "Replace non-friend skins with default", Localize("Replace non-friend skins with default"), &g_Config.m_QmStreamerHideSkins);
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmStreamerScoreboardDefaultFlags, "Use default flags on scoreboard", Localize("Use default flags on scoreboard"), &g_Config.m_QmStreamerScoreboardDefaultFlags);
}

void CMenus::RenderQmVisualTranslateUiContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing)
{
	NTranslateUiSettings::RenderTranslateUiModule(this, Content, LineHeight, BodySize, LineSpacing);
}

void CMenus::RenderQmVisualEntityOverlayContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	auto RenderSlider = [&](const void *pInputId, int *pValue, const char *pTitle) {
		CUIRect Row, LabelColumn, ControlColumn;
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		Ui()->DoLabel(&LabelColumn, pTitle, BodySize, TEXTALIGN_ML);
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(pInputId, &Row, pValue, &LabelColumn);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, 0, 100, "%", PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};

	static int s_QmEntityOverlayDeathAlphaInputId;
	static int s_QmEntityOverlayFreezeAlphaInputId;
	static int s_QmEntityOverlayUnfreezeAlphaInputId;
	static int s_QmEntityOverlayDeepFreezeAlphaInputId;
	static int s_QmEntityOverlayDeepUnfreezeAlphaInputId;
	static int s_QmEntityOverlayTeleAlphaInputId;
	static int s_QmEntityOverlayTeleCheckpointAlphaInputId;
	static int s_QmEntityOverlaySwitchAlphaInputId;
	static int s_ClOverlayEntitiesInputId;
	RenderSlider(&s_QmEntityOverlayDeathAlphaInputId, &g_Config.m_QmEntityOverlayDeathAlpha, Localize("Death opacity"));
	RenderSlider(&s_QmEntityOverlayFreezeAlphaInputId, &g_Config.m_QmEntityOverlayFreezeAlpha, Localize("Freeze opacity"));
	RenderSlider(&s_QmEntityOverlayUnfreezeAlphaInputId, &g_Config.m_QmEntityOverlayUnfreezeAlpha, Localize("Unfreeze opacity"));
	RenderSlider(&s_QmEntityOverlayDeepFreezeAlphaInputId, &g_Config.m_QmEntityOverlayDeepFreezeAlpha, Localize("Deep freeze opacity"));
	RenderSlider(&s_QmEntityOverlayDeepUnfreezeAlphaInputId, &g_Config.m_QmEntityOverlayDeepUnfreezeAlpha, Localize("Deep unfreeze opacity"));
	RenderSlider(&s_QmEntityOverlayTeleAlphaInputId, &g_Config.m_QmEntityOverlayTeleAlpha, Localize("Teleport opacity"));
	RenderSlider(&s_QmEntityOverlayTeleCheckpointAlphaInputId, &g_Config.m_QmEntityOverlayTeleCheckpointAlpha, Localize("CP opacity"));
	RenderSlider(&s_QmEntityOverlaySwitchAlphaInputId, &g_Config.m_QmEntityOverlaySwitchAlpha, Localize("Switch opacity"));
	RenderSlider(&s_ClOverlayEntitiesInputId, &g_Config.m_ClOverlayEntities, Localize("Tune layer opacity"));
}

void CMenus::RenderQmVisualCollisionHitboxContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	auto RenderCheckbox = [&](const void *pId, const char *pTextId, const char *pText, int *pValue) {
		CUIRect Row;
		Content.HSplitTop(LineHeight, &Row, &Content);
		if(DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, pId, pTextId, pText, *pValue, &Row))
			*pValue ^= 1;
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};

	int HitboxModeEnabled = g_Config.m_QmHitboxMode || g_Config.m_QmShowCollisionHitbox;
	{
		CUIRect Row;
		Content.HSplitTop(LineHeight, &Row, &Content);
		if(DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, &g_Config.m_QmHitboxMode, "Show hitbox mode", Localize("Show hitbox mode"), HitboxModeEnabled, &Row))
		{
			HitboxModeEnabled ^= 1;
			g_Config.m_QmHitboxMode = HitboxModeEnabled;
			g_Config.m_QmShowCollisionHitbox = 0;
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}
	if(!HitboxModeEnabled)
		return;

	RenderCheckbox(&g_Config.m_QmHitboxShowMap, "Map danger border", Localize("Map danger border"), &g_Config.m_QmHitboxShowMap);
	RenderCheckbox(&g_Config.m_QmHitboxShowTeeCollision, "Tee collision (Tee to Tee)", Localize("Tee collision (Tee to Tee)"), &g_Config.m_QmHitboxShowTeeCollision);
	RenderCheckbox(&g_Config.m_QmHitboxShowTeeFreeze, "Tee freeze probe (Tee to Freeze)", Localize("Tee freeze probe (Tee to Freeze)"), &g_Config.m_QmHitboxShowTeeFreeze);
	RenderCheckbox(&g_Config.m_QmHitboxShowTeeDeath, "Tee death probe", Localize("Tee death probe"), &g_Config.m_QmHitboxShowTeeDeath);
	RenderCheckbox(&g_Config.m_QmHitboxShowPickups, "Pickup range", Localize("Pickup range"), &g_Config.m_QmHitboxShowPickups);
	RenderCheckbox(&g_Config.m_QmHitboxShowHammer, "Hammer interaction", Localize("Hammer interaction"), &g_Config.m_QmHitboxShowHammer);
	RenderCheckbox(&g_Config.m_QmHitboxShowProjectiles, "Projectile / explosion range", Localize("Projectile / explosion range"), &g_Config.m_QmHitboxShowProjectiles);
	RenderCheckbox(&g_Config.m_QmHitboxShowFreezeProjectiles, "Freeze projectile collision volume", Localize("Freeze projectile collision volume"), &g_Config.m_QmHitboxShowFreezeProjectiles);
	RenderCheckbox(&g_Config.m_QmHitboxShowLasers, "Laser / shotgun interaction", Localize("Laser / shotgun interaction"), &g_Config.m_QmHitboxShowLasers);
	RenderCheckbox(&g_Config.m_QmHitboxShowFreezeLasers, "Freeze laser collision volume", Localize("Freeze laser collision volume"), &g_Config.m_QmHitboxShowFreezeLasers);
	RenderCheckbox(&g_Config.m_QmHitboxShowHook, "Hook interaction", Localize("Hook interaction"), &g_Config.m_QmHitboxShowHook);

	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(LineHeight, &Row, &Content);
	static std::vector<const char *> s_HitboxScopeDropDownNames;
	s_HitboxScopeDropDownNames = {Localize("Local only"), Localize("Local + Dummy"), Localize("All players")};
	static CUi::SDropDownState s_HitboxScopeDropDownState;
	static CScrollRegion s_HitboxScopeDropDownScrollRegion;
	s_HitboxScopeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_HitboxScopeDropDownScrollRegion;
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, "qmclient-hitbox-player-range", &LabelColumn, Localize("Player range"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
	const int HitboxScope = std::clamp(g_Config.m_QmHitboxPlayerScope, 0, 2);
	const int HitboxScopeNew = DoSettingsDropDown(&ControlColumn, HitboxScope, s_HitboxScopeDropDownNames.data(), s_HitboxScopeDropDownNames.size(), s_HitboxScopeDropDownState, {}, &g_Config.m_QmHitboxPlayerScope, nullptr, &Row);
	if(g_Config.m_QmHitboxPlayerScope != HitboxScopeNew)
		g_Config.m_QmHitboxPlayerScope = HitboxScopeNew;
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	static CButtonContainer s_FreezeColorId;
	DoLine_ColorPicker(&s_FreezeColorId, CurrentSettingsContentMetrics(), &Content, Localize("Freeze border color"), &g_Config.m_QmHitboxColorFreeze, ColorRGBA(1.0f, 0.0f, 1.0f), false);
	static CButtonContainer s_TeeColorId;
	DoLine_ColorPicker(&s_TeeColorId, CurrentSettingsContentMetrics(), &Content, Localize("Tee hitbox color"), &g_Config.m_QmHitboxColorTee, ColorRGBA(0.0f, 1.0f, 1.0f), false);
	static CButtonContainer s_WeaponColorId;
	DoLine_ColorPicker(&s_WeaponColorId, CurrentSettingsContentMetrics(), &Content, Localize("Weapon range color"), &g_Config.m_QmHitboxColorWeapon, ColorRGBA(1.0f, 1.0f, 0.0f), false);

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, "qmclient-hitbox-opacity", &LabelColumn, Localize("Opacity"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
	static int s_QmHitboxAlphaInputId;
	GameClient()->m_Tooltips.DoSettingsToolTipForConfig(&s_QmHitboxAlphaInputId, &Row, &g_Config.m_QmHitboxAlpha, &LabelColumn);
	RenderQmSettingsSliderWithValueInput(&s_QmHitboxAlphaInputId, ControlColumn, &g_Config.m_QmHitboxAlpha, 0, 100, "%", PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
}

void CMenus::RenderQmVisualWeaponAnimationContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, float ContentGap, bool PrewarmOnly)
{
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmWeaponSwitchAnim, "Weapon switch animation", Localize("Weapon switch animation"), &g_Config.m_QmWeaponSwitchAnim);
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmWeaponReloadAnim, "Play a flip animation while reloading weapons", Localize("Play a flip animation while reloading weapons"), &g_Config.m_QmWeaponReloadAnim);
	Content.HSplitTop(ContentGap, nullptr, &Content);

	// 锤子旋转模式：锤子像其他武器一样跟随准星旋转。独立于开关动画选项，
	// 不受上方动画开关 early-return 影响（自视觉页字体区迁移至此武器动画卡片）。
	{
		CUIRect HammerRow, HammerLabel, HammerControl;
		Content.HSplitTop(LineHeight, &HammerRow, &Content);
		HammerRow.VSplitLeft(LabelWidth, &HammerLabel, &HammerControl);
		RenderQmVisualLabel("qmclient-hammer-mode", &HammerLabel, Localize("Hammer Mode"), BodySize);
		static std::vector<const char *> s_HammerModeDropDownNames;
		s_HammerModeDropDownNames = {Localize("Normal", "Hammer Mode"), Localize("Rotate with cursor", "Hammer Mode"), Localize("Rotate with cursor like gun", "Hammer Mode")};
		static CUi::SDropDownState s_HammerModeDropDownState;
		static CScrollRegion s_HammerModeDropDownScrollRegion;
		s_HammerModeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_HammerModeDropDownScrollRegion;
		const int HammerMode = std::clamp(g_Config.m_QmHammerRotatesWithCursor, 0, 2);
		const int NewHammerMode = DoSettingsDropDown(&HammerControl, HammerMode, s_HammerModeDropDownNames.data(), s_HammerModeDropDownNames.size(), s_HammerModeDropDownState, {}, &g_Config.m_QmHammerRotatesWithCursor, nullptr, &HammerRow);
		if(g_Config.m_QmHammerRotatesWithCursor != NewHammerMode)
			g_Config.m_QmHammerRotatesWithCursor = NewHammerMode;
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}

	CUIRect Row, LabelColumn, ControlColumn;
	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int Min, int Max, const char *pSuffix = "") {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		RenderQmVisualLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(pInputId, &Row, pValue, &LabelColumn);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, Min, Max, pSuffix, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};

	if(g_Config.m_QmWeaponReloadAnim)
	{
		static int s_QmWeaponReloadAnimProbabilityInputId;
		RenderValue("qmclient-weapon-reload-animation-probability", "Weapon reload animation probability", &s_QmWeaponReloadAnimProbabilityInputId, &g_Config.m_QmWeaponReloadAnimProbability, 0, 100, "%");
	}

	if(!g_Config.m_QmWeaponSwitchAnim && !g_Config.m_QmWeaponReloadAnim)
		return;

	Content.HSplitTop(LineHeight, &Row, &Content);
	static std::vector<const char *> s_WeaponSwitchAnimScopeDropDownNames;
	s_WeaponSwitchAnimScopeDropDownNames = {Localize("Self only"), Localize("Local"), Localize("All players")};
	static CUi::SDropDownState s_WeaponSwitchAnimScopeDropDownState;
	static CScrollRegion s_WeaponSwitchAnimScopeDropDownScrollRegion;
	s_WeaponSwitchAnimScopeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_WeaponSwitchAnimScopeDropDownScrollRegion;
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	RenderQmVisualLabel("qmclient-weapon-switch-animation-range", &LabelColumn, Localize("Animation range"), BodySize);
	const int Scope = std::clamp(g_Config.m_QmWeaponSwitchAnimScope, 0, 2);
	const int NewScope = DoSettingsDropDown(&ControlColumn, Scope, s_WeaponSwitchAnimScopeDropDownNames.data(), s_WeaponSwitchAnimScopeDropDownNames.size(), s_WeaponSwitchAnimScopeDropDownState, {}, &g_Config.m_QmWeaponSwitchAnimScope, nullptr, &Row);
	if(g_Config.m_QmWeaponSwitchAnimScope != NewScope)
		g_Config.m_QmWeaponSwitchAnimScope = NewScope;
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	if(!g_Config.m_QmWeaponSwitchAnim)
		return;

	static int s_QmWeaponSwitchAnimDurationInputId;
	static int s_QmWeaponSwitchAnimDistanceInputId;
	static int s_QmWeaponSwitchAnimRotationInputId;
	RenderValue("qmclient-weapon-switch-duration", "Weapon switch duration", &s_QmWeaponSwitchAnimDurationInputId, &g_Config.m_QmWeaponSwitchAnimDurationMs, 50, 2000, "ms");
	RenderValue("qmclient-weapon-switch-distance", "Weapon switch distance", &s_QmWeaponSwitchAnimDistanceInputId, &g_Config.m_QmWeaponSwitchAnimDistance, 0, 100);
	RenderValue("qmclient-weapon-switch-rotation", "Weapon switch rotation", &s_QmWeaponSwitchAnimRotationInputId, &g_Config.m_QmWeaponSwitchAnimRotation, 0, 1440, "deg");

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	RenderQmVisualLabel("qmclient-weapon-switch-easing", &LabelColumn, Localize("Weapon switch easing"), BodySize);
	static std::vector<const char *> s_WeaponSwitchAnimEasingDropDownNames;
	s_WeaponSwitchAnimEasingDropDownNames = {Localize("Ease out cubic"), Localize("Elastic back"), Localize("Linear"), Localize("Ease in out quad")};
	static CUi::SDropDownState s_WeaponSwitchAnimEasingDropDownState;
	static CScrollRegion s_WeaponSwitchAnimEasingDropDownScrollRegion;
	s_WeaponSwitchAnimEasingDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_WeaponSwitchAnimEasingDropDownScrollRegion;
	const int Easing = std::clamp(g_Config.m_QmWeaponSwitchAnimEasing, 0, 3);
	const int NewEasing = DoSettingsDropDown(&ControlColumn, Easing, s_WeaponSwitchAnimEasingDropDownNames.data(), s_WeaponSwitchAnimEasingDropDownNames.size(), s_WeaponSwitchAnimEasingDropDownState, {}, &g_Config.m_QmWeaponSwitchAnimEasing, nullptr, &Row);
	if(g_Config.m_QmWeaponSwitchAnimEasing != NewEasing)
		g_Config.m_QmWeaponSwitchAnimEasing = NewEasing;
	Content.HSplitTop(LineSpacing, nullptr, &Content);
}

void CMenus::RenderQmVisualChatBubbleContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmChatBubble, "qmclient-chat-bubble-enable", Localize("Show chat bubbles above players"), &g_Config.m_QmChatBubble);
	if(!g_Config.m_QmChatBubble)
		return;

	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int Min, int Max, const char *pSuffix = "") {
		CUIRect Row, LabelColumn, ControlColumn;
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		RenderQmVisualLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(pInputId, &Row, pValue, &LabelColumn);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, Min, Max, pSuffix, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	static int s_QmChatBubbleDurationInputId;
	static int s_QmChatBubbleAlphaInputId;
	static int s_QmChatBubbleFontSizeInputId;
	RenderValue("qmclient-chat-bubble-duration", "Duration", &s_QmChatBubbleDurationInputId, &g_Config.m_QmChatBubbleDuration, 1, 30, "s");
	RenderValue("qmclient-chat-bubble-opacity", "Bubble opacity", &s_QmChatBubbleAlphaInputId, &g_Config.m_QmChatBubbleAlpha, 0, 100, "%");
	RenderValue("qmclient-chat-bubble-font-size", "Font size", &s_QmChatBubbleFontSizeInputId, &g_Config.m_QmChatBubbleFontSize, 8, 32);

	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	RenderQmVisualLabel("qmclient-chat-bubble-animation", &LabelColumn, Localize("Animation"), BodySize);
	static std::vector<const char *> s_ChatBubbleAnimDropDownNames;
	s_ChatBubbleAnimDropDownNames = {Localize("Dissolve"), Localize("Shrink"), Localize("Bounce")};
	static CUi::SDropDownState s_ChatBubbleAnimDropDownState;
	static CScrollRegion s_ChatBubbleAnimDropDownScrollRegion;
	s_ChatBubbleAnimDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_ChatBubbleAnimDropDownScrollRegion;
	const int Animation = DoSettingsDropDown(&ControlColumn, g_Config.m_QmChatBubbleAnimation, s_ChatBubbleAnimDropDownNames.data(), s_ChatBubbleAnimDropDownNames.size(), s_ChatBubbleAnimDropDownState, {}, &g_Config.m_QmChatBubbleAnimation, nullptr, &Row);
	if(g_Config.m_QmChatBubbleAnimation != Animation)
		g_Config.m_QmChatBubbleAnimation = Animation;
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	static CButtonContainer s_ChatBubbleBgColorId;
	static CButtonContainer s_ChatBubbleTextColorId;
	DoLine_ColorPicker(&s_ChatBubbleBgColorId, CurrentSettingsContentMetrics(), &Content, Localize("Background color"), &g_Config.m_QmChatBubbleBgColor, ColorRGBA(0.0f, 0.0f, 0.0f, 0.8f), false, nullptr, true);
	DoLine_ColorPicker(&s_ChatBubbleTextColorId, CurrentSettingsContentMetrics(), &Content, Localize("Text color"), &g_Config.m_QmChatBubbleTextColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), false);
}

void CMenus::RenderQmVisualFocusModeContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float ColumnGap, float LabelWidth)
{
	const float SmallSize = CurrentSettingsContentMetrics().m_SmallSize;
	static CButtonContainer s_ReaderButtonFocusToggle, s_ClearButtonFocusToggle;
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmFocusMode, "qmclient-focus-mode-enable", Localize("Enable Zen mode"), &g_Config.m_QmFocusMode);
	CUIRect LeftColumn, RightColumn, Row;
	Content.VSplitMid(&LeftColumn, &RightColumn, ColumnGap);
	auto RenderSection = [&](CUIRect &Target, const char *pTextId, const char *pLabel) {
		Target.HSplitTop(SmallSize, &Row, &Target);
		TextRender()->TextColor(ColorRGBA(0.72f, 0.72f, 0.78f, 0.86f));
		RenderQmVisualLabel(pTextId, &Row, Localize(pLabel), SmallSize);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		Target.HSplitTop(LineSpacing, nullptr, &Target);
	};
	auto RenderCheckbox = [&](CUIRect &Target, int *pConfig, const char *pTextId, const char *pLabel) {
		Target.HSplitTop(LineHeight, &Row, &Target);
		if(DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, pConfig, pTextId, Localize(pLabel), *pConfig, &Row))
			*pConfig ^= 1;
		Target.HSplitTop(LineSpacing, nullptr, &Target);
	};
	RenderSection(LeftColumn, "qmclient-focus-section-interface", "Interface");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideHud, "qmclient-focus-hide-hud", "Hide HUD");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideMapProgress, "qmclient-focus-hide-map-progress", "Hide map progress");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideInfoMessages, "qmclient-focus-hide-info-messages", "Hide kill/finish messages");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideScoreboard, "qmclient-focus-hide-scoreboard", "Hide scoreboard");
	RenderSection(LeftColumn, "qmclient-focus-section-players", "Players");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideNames, "qmclient-focus-hide-names", "Hide names");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideNameplates, "qmclient-focus-hide-nameplates", "Hide nameplates");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideDirectionIndicators, "qmclient-focus-hide-direction-indicators", "Hide direction indicators");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideGuideLines, "qmclient-focus-hide-guide-lines", "Hide guide lines");
	RenderSection(LeftColumn, "qmclient-focus-section-visuals", "Visuals");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideJumpEffects, "qmclient-focus-hide-jump-effects", "Hide jump effects");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideKillEffects, "qmclient-focus-hide-kill-effects", "Hide death/respawn effects");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideExplosionEffects, "qmclient-focus-hide-explosion-effects", "Hide explosion effects");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideFreezeEffects, "qmclient-focus-hide-freeze-effects", "Hide freeze effects");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideHammerEffects, "qmclient-focus-hide-hammer-effects", "Hide hammer effects");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideMuzzleEffects, "qmclient-focus-hide-muzzle-effects", "Hide weapon muzzle flashes");
	RenderSection(RightColumn, "qmclient-focus-section-audio", "Audio");
	RenderCheckbox(RightColumn, &g_Config.m_QmFocusModeMuteJumpSounds, "qmclient-focus-mute-jump-sounds", "Mute jump sounds");
	RenderCheckbox(RightColumn, &g_Config.m_QmFocusModeMuteDeathSounds, "qmclient-focus-mute-death-sounds", "Mute death/respawn sounds");
	RenderCheckbox(RightColumn, &g_Config.m_QmFocusModeMuteHammerSounds, "qmclient-focus-mute-hammer-sounds", "Mute hammer sounds");
	RenderSection(RightColumn, "qmclient-focus-section-chat", "Chat");
	RenderCheckbox(RightColumn, &g_Config.m_QmFocusModeHideChat, "qmclient-focus-hide-chat", "Hide player messages");
	RenderCheckbox(RightColumn, &g_Config.m_QmFocusModeHideSystemInfoMessages, "qmclient-focus-hide-system-info-messages", "Hide join/version prompts");
	RenderCheckbox(RightColumn, &g_Config.m_QmFocusModeHideSystemMessages, "qmclient-focus-hide-system-messages", "Hide server prompt notifications");
	RenderCheckbox(RightColumn, &g_Config.m_QmFocusModeHideEcho, "qmclient-focus-hide-echo", "Hide Echo messages");
	Content.y = std::max(LeftColumn.y, RightColumn.y);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	Content.HSplitTop(LineHeight, &Row, &Content);
	CUIRect BindLabel, BindKey;
	Row.VSplitLeft(LabelWidth, &BindLabel, &BindKey);
	RenderQmVisualLabel("qmclient-focus-mode-key", &BindLabel, Localize("Zen mode key"), BodySize);
	CBindSlot FocusBind(KEY_UNKNOWN, KeyModifier::NONE);
	if(const auto FocusIt = g_CommandBindCache.find("toggle qm_focus_mode 0 1"); FocusIt != g_CommandBindCache.end())
		FocusBind = FocusIt->second;
	const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&s_ReaderButtonFocusToggle, &s_ClearButtonFocusToggle, &BindKey, FocusBind, false);
	if(Result.m_Bind != FocusBind)
	{
		if(FocusBind.m_Key != KEY_UNKNOWN)
			GameClient()->m_Binds.Bind(FocusBind.m_Key, "", false, FocusBind.m_ModifierMask);
		if(Result.m_Bind.m_Key != KEY_UNKNOWN)
		{
			GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, "toggle qm_focus_mode 0 1", false, Result.m_Bind.m_ModifierMask);
			g_CommandBindCache.insert_or_assign(std::string("toggle qm_focus_mode 0 1"), Result.m_Bind);
		}
		else
			g_CommandBindCache.erase("toggle qm_focus_mode 0 1");
	}
}

void CMenus::RenderQmVisualCameraViewContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pId, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "", unsigned Flags = 0u) {
		CUIRect Row, LabelColumn, ControlColumn;
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		RenderQmVisualLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(pId, &Row, pValue, &LabelColumn);
		RenderQmSettingsSliderWithValueInput(pId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly, Flags);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmCameraDrift, "Enable camera drift", Localize("Enable camera drift"), &g_Config.m_QmCameraDrift);
	if(g_Config.m_QmCameraDrift)
	{
		static int s_QmCameraDriftAmountInputId;
		static int s_QmCameraDriftSmoothnessInputId;
		RenderValue("qmclient-camera-drift-intensity", "Drift intensity", &s_QmCameraDriftAmountInputId, &g_Config.m_QmCameraDriftAmount, 0, 200);
		RenderValue("qmclient-camera-drift-smoothness", "Drift smoothness", &s_QmCameraDriftSmoothnessInputId, &g_Config.m_QmCameraDriftSmoothness, 0, 100, "%");
		RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmCameraDriftReverse, "Drift direction", Localize("Drift direction"), &g_Config.m_QmCameraDriftReverse);
	}
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmDynamicFov, "Enable dynamic FOV", Localize("Enable dynamic FOV"), &g_Config.m_QmDynamicFov);
	if(g_Config.m_QmDynamicFov)
	{
		static int s_QmDynamicFovAmountInputId;
		static int s_QmDynamicFovSmoothnessInputId;
		RenderValue("qmclient-camera-dynamic-fov-intensity", "Dynamic FOV intensity", &s_QmDynamicFovAmountInputId, &g_Config.m_QmDynamicFovAmount, 0, 200);
		RenderValue("qmclient-camera-dynamic-fov-smoothness", "Dynamic FOV smoothness", &s_QmDynamicFovSmoothnessInputId, &g_Config.m_QmDynamicFovSmoothness, 0, 100, "%");
	}
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmCinematicCamera, "Cinematic camera", Localize("Cinematic camera"), &g_Config.m_QmCinematicCamera);
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmZoomInstantReverse, "Instant zoom reverse", Localize("Instant zoom reverse"), &g_Config.m_QmZoomInstantReverse);
	static int s_QmUiScaleInputId;
	RenderValue("qmclient-ui-scale", "UI scale", &s_QmUiScaleInputId, &g_Config.m_QmUiScale, 50, 200, "%", CUi::SCROLLBAR_OPTION_DELAYUPDATE);
	const char *apAspectPresetNames[] = {Localize("Off"), "5:4", "4:3", "3:2", "16:9", "21:9", Localize("Custom")};
	static CUi::SDropDownState s_AspectPresetDropDownState;
	static CScrollRegion s_AspectPresetDropDownScrollRegion;
	s_AspectPresetDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_AspectPresetDropDownScrollRegion;
	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	RenderQmVisualLabel("qmclient-camera-aspect-ratio-preset", &LabelColumn, Localize("Aspect ratio preset"), BodySize);
	const int CurrentPreset = std::clamp(g_Config.m_QmAspectPreset, 0, 6);
	const int NewPreset = DoSettingsDropDown(&ControlColumn, CurrentPreset, apAspectPresetNames, (int)std::size(apAspectPresetNames), s_AspectPresetDropDownState, {}, &g_Config.m_QmAspectPreset, &g_Config.m_QmAspectRatio, &Row);
	bool AspectChanged = NewPreset != CurrentPreset;
	if(AspectChanged)
	{
		g_Config.m_QmAspectPreset = NewPreset;
		switch(NewPreset)
		{
		case 1: g_Config.m_QmAspectRatio = 125; break;
		case 2: g_Config.m_QmAspectRatio = 133; break;
		case 3: g_Config.m_QmAspectRatio = 150; break;
		case 4: g_Config.m_QmAspectRatio = 178; break;
		case 5: g_Config.m_QmAspectRatio = 233; break;
		case 6:
			if(g_Config.m_QmAspectRatio < 100)
				g_Config.m_QmAspectRatio = 178;
			break;
		default: break;
		}
	}
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	if(g_Config.m_QmAspectPreset == 6)
	{
		static int s_QmAspectRatioInputId;
		const int OldAspectRatio = g_Config.m_QmAspectRatio;
		RenderValue("qmclient-camera-custom-aspect-ratio", "Custom aspect ratio", &s_QmAspectRatioInputId, &g_Config.m_QmAspectRatio, 100, 300);
		AspectChanged |= OldAspectRatio != g_Config.m_QmAspectRatio;
	}
	int EffectiveAspectValue = 0;
	switch(g_Config.m_QmAspectPreset)
	{
	case 1: EffectiveAspectValue = 125; break;
	case 2: EffectiveAspectValue = 133; break;
	case 3: EffectiveAspectValue = 150; break;
	case 4: EffectiveAspectValue = 178; break;
	case 5: EffectiveAspectValue = 233; break;
	case 6: EffectiveAspectValue = std::clamp(g_Config.m_QmAspectRatio, 100, 300); break;
	default: break;
	}
	Content.HSplitTop(BodySize, &Row, &Content);
	char aAspectInfo[128];
	if(EffectiveAspectValue > 0)
		str_format(aAspectInfo, sizeof(aAspectInfo), "%s %.2f:1", Localize("Current aspect ratio:"), EffectiveAspectValue / 100.0f);
	else
		str_copy(aAspectInfo, Localize("Current aspect ratio: Show default"), sizeof(aAspectInfo));
	Ui()->DoLabel(&Row, aAspectInfo, BodySize, TEXTALIGN_ML);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	if(AspectChanged && !PrewarmOnly)
		GameClient()->TClientComponent().QueueAspectApply();
}
