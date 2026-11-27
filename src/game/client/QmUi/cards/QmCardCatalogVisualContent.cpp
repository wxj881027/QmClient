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

#include <game/client/QmUi/QmChatBubble.h>
#include <game/client/QmUi/SettingsBindCache.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/binds.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/bind_index.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/qmclient/qm_music_hook_registry.h>
#include <game/client/components/qmclient/qmclient_utils.h>
#include <game/client/components/qmclient/translate/translate_backend.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

using namespace FontIcons;

void CMenus::RenderQmVisualStreamerContent(CUIRect &Content, float LineHeight, float LineSpacing)
{
	SSettingsContentMetrics Metrics = CurrentSettingsContentMetrics();
	Metrics.m_LineHeight = LineHeight;
	Metrics.m_LineSpacing = LineSpacing;
	CSettingsContentLayoutFlow Flow(Content);
	DoSettingsToggleGroup(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, Content,
		{
			{&g_Config.m_QmStreamerFilterWords, "Filter sensitive words", Localize("Filter sensitive words")},
			{&g_Config.m_QmStreamerHideNames, "Replace non-friend names with ID", Localize("Replace non-friend names with ID")},
			{&g_Config.m_QmStreamerHideSkins, "Replace non-friend skins with default", Localize("Replace non-friend skins with default")},
			{&g_Config.m_QmStreamerScoreboardDefaultFlags, "Use default flags on scoreboard", Localize("Use default flags on scoreboard")},
		},
		Metrics);
	// 组和下面的单词表文件行都改成「行自己声明行距」了：组尾不再补 Space，
	// 单词表首行自己带行前距，这里不需要边界行距。
	RenderQmWordFilterFileContent(Content, true, LineHeight, LineSpacing, Metrics.m_BodySize);
}

void CMenus::RenderQmVisualEntityOverlayContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	const bool CanCommit = Flow.Input() && !PrewarmOnly && !Ui()->RenderOnly();
	auto RenderSlider = [&](const void *pInputId, int *pValue, const char *pTextId, const char *pTitle) {
		CUIRect Row, LabelColumn, ControlColumn;
		// 行距由行自己声明（声明阶段记到上一行末尾），行后不再补 Space。
		const bool HasRow = Flow.Control(pTextId, LineHeight, Row, ui_token::settings::RowGap(LineSpacing));
		if(!HasRow)
			return;
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		Ui()->DoLabel(&LabelColumn, pTitle, BodySize, TEXTALIGN_ML);
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(pInputId, &Row, pValue, &LabelColumn);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, 0, 100, "%", PrewarmOnly);
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
	// 显示实体层和原来的叠层不透明度共用配置，合并后只保留一个控件。
	RenderSlider(&s_ClOverlayEntitiesInputId, &g_Config.m_ClOverlayEntities, "m_ClOverlayEntities", Localize("Overlay entities"));
	CUIRect Row;
	if(Flow.Control("text-entities", LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
	{
		if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClTextEntities, "Show text entities", Localize("Show text entities"), g_Config.m_ClTextEntities, &Row) && CanCommit)
			g_Config.m_ClTextEntities ^= 1;
	}
	static CSettingsConditionalRows s_TextEntitiesSizeRows;
	if(s_TextEntitiesSizeRows.Update(g_Config.m_ClTextEntities, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
	{
		const CSettingsContentRevealScope TextEntitiesSizeRevealScope(Ui(), Flow, s_TextEntitiesSizeRows.Fade(g_Config.m_QmUiMotionLevel));
		if(Flow.Control("text-entities-size", LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
		{
			CUIRect LabelColumn, ControlColumn;
			SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
			RenderQmVisualLabel("ddnet-text-entities-size", &LabelColumn, Localize("Size"), BodySize);
			GameClient()->m_Tooltips.DoSettingsToolTipForConfig(&g_Config.m_ClTextEntitiesSize, &Row, &g_Config.m_ClTextEntitiesSize, &LabelColumn);
			const int PreviousSize = g_Config.m_ClTextEntitiesSize;
			RenderQmSettingsSliderWithValueInput(&g_Config.m_ClTextEntitiesSize, ControlColumn, &g_Config.m_ClTextEntitiesSize, 20, 100, "", PrewarmOnly, CUi::SCROLLBAR_OPTION_DELAYUPDATE);
			if(CanCommit && PreviousSize != g_Config.m_ClTextEntitiesSize)
				GameClient()->m_MapImages.SetTextureScale(g_Config.m_ClTextEntitiesSize);
		}
	}
	float SectionReveal = 0.0f;
	if(!qm_card_catalog::QmCardRenderHook::PersonalizationReveal(this, Content, "qm:entity_overlay", CurrentSettingsContentMetrics(), &SectionReveal, PrewarmOnly || Ui()->RenderOnly()))
		return;
	const CSettingsContentRevealScope SectionRevealScope(Ui(), Flow, SectionReveal);

	RenderSlider(&s_QmEntityOverlayDeathAlphaInputId, &g_Config.m_QmEntityOverlayDeathAlpha, "m_QmEntityOverlayDeathAlpha", Localize("Death opacity"));
	RenderSlider(&s_QmEntityOverlayFreezeAlphaInputId, &g_Config.m_QmEntityOverlayFreezeAlpha, "m_QmEntityOverlayFreezeAlpha", Localize("Freeze opacity"));
	static CButtonContainer s_QmEntityOverlayFreezeColorId;
	CUIRect FreezeColorRow;
	if(Flow.Control("entity-freeze-color", LineHeight, FreezeColorRow, ui_token::settings::RowGap(LineSpacing)))
	{
		DoLine_ColorPicker(&s_QmEntityOverlayFreezeColorId, CurrentSettingsContentMetrics(), &FreezeColorRow, Localize("Freeze tile color"), &g_Config.m_QmEntityOverlayFreezeColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmEntityOverlayFreezeColor)), false);
	}
	RenderSlider(&s_QmEntityOverlayUnfreezeAlphaInputId, &g_Config.m_QmEntityOverlayUnfreezeAlpha, "m_QmEntityOverlayUnfreezeAlpha", Localize("Unfreeze opacity"));
	RenderSlider(&s_QmEntityOverlayDeepFreezeAlphaInputId, &g_Config.m_QmEntityOverlayDeepFreezeAlpha, "m_QmEntityOverlayDeepFreezeAlpha", Localize("Deep freeze opacity"));
	RenderSlider(&s_QmEntityOverlayDeepUnfreezeAlphaInputId, &g_Config.m_QmEntityOverlayDeepUnfreezeAlpha, "m_QmEntityOverlayDeepUnfreezeAlpha", Localize("Deep unfreeze opacity"));
	RenderSlider(&s_QmEntityOverlayTeleAlphaInputId, &g_Config.m_QmEntityOverlayTeleAlpha, "m_QmEntityOverlayTeleAlpha", Localize("Teleport opacity"));
	RenderSlider(&s_QmEntityOverlayTeleCheckpointAlphaInputId, &g_Config.m_QmEntityOverlayTeleCheckpointAlpha, "m_QmEntityOverlayTeleCheckpointAlpha", Localize("CP opacity"));
	RenderSlider(&s_QmEntityOverlaySwitchAlphaInputId, &g_Config.m_QmEntityOverlaySwitchAlpha, "m_QmEntityOverlaySwitchAlpha", Localize("Switch opacity"));
}

void CMenus::RenderQmVisualCollisionHitboxContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);

	int HitboxModeEnabled = g_Config.m_QmHitboxMode || g_Config.m_QmShowCollisionHitbox;
	{
		CUIRect Row;
		// 行距由行自己声明，行后不再补 Space。
		const bool HasRow = Flow.Control("qm-hitbox-enabled", LineHeight, Row, ui_token::settings::RowGap(LineSpacing));
		if(HasRow && DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, &g_Config.m_QmHitboxMode, "Enable collision hitboxes", Localize("Enable collision hitboxes"), HitboxModeEnabled, &Row))
		{
			HitboxModeEnabled ^= 1;
			g_Config.m_QmHitboxMode = HitboxModeEnabled;
			g_Config.m_QmShowCollisionHitbox = 0;
		}
	}
	static SettingsDependentRows s_HitboxRows;
	const auto HitboxRows = s_HitboxRows.Reveal(Ui(), Flow, HitboxModeEnabled, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel);
	if(!HitboxRows)
		return;

	DoSettingsToggleGroup(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, Content,
		{
			{&g_Config.m_QmHitboxShowMap, "Freeze and death borders", Localize("Freeze and death borders")},
			{&g_Config.m_QmHitboxShowFreezeProjectiles, "Freeze projectiles and lasers", Localize("Freeze projectiles and lasers")},
			{&g_Config.m_QmHitboxShowTeeCollision, "Tee collision circle", Localize("Tee collision circle")},
			{&g_Config.m_QmHitboxShowTeeFreeze, "Freeze and death check points", Localize("Freeze and death check points")},
			{&g_Config.m_QmHitboxShowPickups, "Shield pickup range", Localize("Shield pickup range")},
			{&g_Config.m_QmHitboxShowHammer, "Hammer and hook range", Localize("Hammer and hook range")},
			{&g_Config.m_QmHitboxShowProjectiles, "Projectiles, explosions and regular lasers", Localize("Projectiles, explosions and regular lasers")},
		},
		CurrentSettingsContentMetrics(), !PrewarmOnly);

	CUIRect Row, LabelColumn, ControlColumn;
	if(Flow.Control("qm-hitbox-scope", LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
	{
		static std::vector<const char *> s_HitboxScopeDropDownNames;
		s_HitboxScopeDropDownNames = {Localize("Self only"), Localize("Self and dummy"), Localize("All players", "Collision hitbox")};
		static CUi::SDropDownState s_HitboxScopeDropDownState;
		static CScrollRegion s_HitboxScopeDropDownScrollRegion;
		s_HitboxScopeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_HitboxScopeDropDownScrollRegion;
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, "Show hitboxes for", &LabelColumn, Localize("Show hitboxes for"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		const int HitboxScope = std::clamp(g_Config.m_QmHitboxPlayerScope, 0, 2);
		const int HitboxScopeNew = DoSettingsDropDown(&ControlColumn, HitboxScope, s_HitboxScopeDropDownNames.data(), s_HitboxScopeDropDownNames.size(), s_HitboxScopeDropDownState, {}, &g_Config.m_QmHitboxPlayerScope, nullptr, &Row);
		if(g_Config.m_QmHitboxPlayerScope != HitboxScopeNew)
			g_Config.m_QmHitboxPlayerScope = HitboxScopeNew;
	}

	float SectionReveal = 0.0f;
	if(!qm_card_catalog::QmCardRenderHook::PersonalizationReveal(this, Content, "qm:collision_hitbox", CurrentSettingsContentMetrics(), &SectionReveal, PrewarmOnly || Ui()->RenderOnly()))
		return;
	const CSettingsContentRevealScope SectionRevealScope(Ui(), Flow, SectionReveal);

	static CButtonContainer s_FreezeColorId;
	CUIRect s_FreezeColorIdRow;
	if(Flow.Control("s_FreezeColorId", LineHeight, s_FreezeColorIdRow, ui_token::settings::RowGap(LineSpacing)))
	{
		CUIRect ColorRow = s_FreezeColorIdRow;
		DoLine_ColorPicker(&s_FreezeColorId, CurrentSettingsContentMetrics(), &ColorRow, Localize("Freeze hitbox color"), &g_Config.m_QmHitboxColorFreeze, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmHitboxColorFreeze)), false);
	}
	static CButtonContainer s_TeeColorId;
	CUIRect s_TeeColorIdRow;
	if(Flow.Control("s_TeeColorId", LineHeight, s_TeeColorIdRow, ui_token::settings::RowGap(LineSpacing)))
	{
		CUIRect ColorRow = s_TeeColorIdRow;
		DoLine_ColorPicker(&s_TeeColorId, CurrentSettingsContentMetrics(), &ColorRow, Localize("Tee collision color"), &g_Config.m_QmHitboxColorTee, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmHitboxColorTee)), false);
	}
	static CButtonContainer s_WeaponColorId;
	CUIRect s_WeaponColorIdRow;
	if(Flow.Control("s_WeaponColorId", LineHeight, s_WeaponColorIdRow, ui_token::settings::RowGap(LineSpacing)))
	{
		CUIRect ColorRow = s_WeaponColorIdRow;
		DoLine_ColorPicker(&s_WeaponColorId, CurrentSettingsContentMetrics(), &ColorRow, Localize("Weapon range color"), &g_Config.m_QmHitboxColorWeapon, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmHitboxColorWeapon)), false);
	}

	if(Flow.Control("qm-hitbox-opacity", LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
	{
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, "Hitbox line opacity", &LabelColumn, Localize("Hitbox line opacity"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		static int s_QmHitboxAlphaInputId;
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(&s_QmHitboxAlphaInputId, &Row, &g_Config.m_QmHitboxAlpha, &LabelColumn);
		RenderQmSettingsSliderWithValueInput(&s_QmHitboxAlphaInputId, ControlColumn, &g_Config.m_QmHitboxAlpha, 0, 100, "%", PrewarmOnly);
	}
}

void CMenus::RenderQmVisualWeaponAnimationContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, float ContentGap, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmWeaponSwitchAnim, "Weapon switch animation", Localize("Weapon switch animation"), &g_Config.m_QmWeaponSwitchAnim);
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmWeaponReloadAnim, "Play a flip animation while reloading weapons", Localize("Play a flip animation while reloading weapons"), &g_Config.m_QmWeaponReloadAnim);
	Flow.Space(ContentGap);

	// 锤子旋转模式：锤子像其他武器一样跟随准星旋转。独立于开关动画选项，
	// 不受上方动画开关 early-return 影响（自视觉页字体区迁移至此武器动画卡片）。
	{
		CUIRect HammerRow, HammerLabel, HammerControl;
		const bool HasHammerRow = Flow.Control("qm-weapon-hammer-mode", LineHeight, HammerRow, ui_token::settings::RowGap(LineSpacing));
		if(HasHammerRow)
		{
			SplitSettingsControlRow(HammerRow, CurrentSettingsContentMetrics(), &HammerLabel, &HammerControl);
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
		}
	}

	if(!g_Config.m_QmWeaponSwitchAnim && !g_Config.m_QmWeaponReloadAnim)
		return;
	float SectionReveal = 0.0f;
	if(!qm_card_catalog::QmCardRenderHook::PersonalizationReveal(this, Content, "qm:weapon_animation", CurrentSettingsContentMetrics(), &SectionReveal, PrewarmOnly || Ui()->RenderOnly()))
		return;
	const CSettingsContentRevealScope SectionRevealScope(Ui(), Flow, SectionReveal);

	CUIRect Row, LabelColumn, ControlColumn;
	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int Min, int Max, const char *pSuffix = "") {
		const bool HasRow = Flow.Control(pTextId, LineHeight, Row, ui_token::settings::RowGap(LineSpacing));
		if(!HasRow)
			return;
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		RenderQmVisualLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(pInputId, &Row, pValue, &LabelColumn);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, Min, Max, pSuffix, PrewarmOnly);
	};

	if(g_Config.m_QmWeaponReloadAnim)
	{
		static int s_QmWeaponReloadAnimProbabilityInputId;
		RenderValue("qmclient-weapon-reload-animation-probability", "Weapon reload animation probability", &s_QmWeaponReloadAnimProbabilityInputId, &g_Config.m_QmWeaponReloadAnimProbability, 0, 100, "%");
	}

	if(Flow.Control("qm-weapon-scope", LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
	{
		static std::vector<const char *> s_WeaponSwitchAnimScopeDropDownNames;
		s_WeaponSwitchAnimScopeDropDownNames = {Localize("Self only"), Localize("Local"), Localize("All players")};
		static CUi::SDropDownState s_WeaponSwitchAnimScopeDropDownState;
		static CScrollRegion s_WeaponSwitchAnimScopeDropDownScrollRegion;
		s_WeaponSwitchAnimScopeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_WeaponSwitchAnimScopeDropDownScrollRegion;
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		RenderQmVisualLabel("qmclient-weapon-switch-animation-range", &LabelColumn, Localize("Animation range"), BodySize);
		const int Scope = std::clamp(g_Config.m_QmWeaponSwitchAnimScope, 0, 2);
		const int NewScope = DoSettingsDropDown(&ControlColumn, Scope, s_WeaponSwitchAnimScopeDropDownNames.data(), s_WeaponSwitchAnimScopeDropDownNames.size(), s_WeaponSwitchAnimScopeDropDownState, {}, &g_Config.m_QmWeaponSwitchAnimScope, nullptr, &Row);
		if(g_Config.m_QmWeaponSwitchAnimScope != NewScope)
			g_Config.m_QmWeaponSwitchAnimScope = NewScope;
	}

	if(!g_Config.m_QmWeaponSwitchAnim)
		return;

	static int s_QmWeaponSwitchAnimDurationInputId;
	static int s_QmWeaponSwitchAnimDistanceInputId;
	static int s_QmWeaponSwitchAnimRotationInputId;
	RenderValue("qmclient-weapon-switch-duration", "Weapon switch duration", &s_QmWeaponSwitchAnimDurationInputId, &g_Config.m_QmWeaponSwitchAnimDurationMs, 50, 2000, "ms");
	RenderValue("qmclient-weapon-switch-distance", "Weapon switch distance", &s_QmWeaponSwitchAnimDistanceInputId, &g_Config.m_QmWeaponSwitchAnimDistance, 0, 100);
	RenderValue("qmclient-weapon-switch-rotation", "Weapon switch rotation", &s_QmWeaponSwitchAnimRotationInputId, &g_Config.m_QmWeaponSwitchAnimRotation, 0, 1440, "deg");

	if(Flow.Control("qm-weapon-easing", LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
	{
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
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
	}
}

void CMenus::RenderQmVisualChatBubbleContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmChatBubble, "qmclient-chat-bubble-enable", Localize("Show chat bubbles above players"), &g_Config.m_QmChatBubble);
	if(!g_Config.m_QmChatBubble)
		return;

	CUIRect StyleRow, StyleLabel, StyleControl;
	// 行距由行自己声明（声明阶段记到上一行末尾），行后不再补 Space。
	if(Flow.Control("qm-chat-bubble-style", LineHeight, StyleRow, ui_token::settings::RowGap(LineSpacing)))
	{
		SplitSettingsControlRow(StyleRow, CurrentSettingsContentMetrics(), &StyleLabel, &StyleControl);
		RenderQmVisualLabel("qm-chat-bubble-style-label", &StyleLabel, Localize("Style"), BodySize);
		const char *apStyles[] = {Localize("Comic dialogue"), Localize("Candy gradient"), Localize("Cloud bubble"), Localize("Default")};
		static CUi::SDropDownState s_StyleState;
		static CScrollRegion s_StyleScroll;
		s_StyleState.m_SelectionPopupContext.m_pScrollRegion = &s_StyleScroll;
		const int Selected = DoSettingsDropDown(&StyleControl, std::clamp(g_Config.m_QmChatBubbleStyle, 0, qm_chat_bubble::STYLE_COUNT - 1), apStyles, std::size(apStyles), s_StyleState, {}, &g_Config.m_QmChatBubbleStyle, nullptr, &StyleRow);
		if(Flow.Input() && !PrewarmOnly && !Ui()->RenderOnly())
			g_Config.m_QmChatBubbleStyle = Selected;
	}

	CUIRect PreviewRow;
	if(Flow.Control("qm-chat-bubble-preview", LineHeight * 5.0f, PreviewRow, ui_token::settings::RowGap(LineSpacing)) && Flow.Drawing() && !Ui()->DrawingSuppressed())
	{
		CUIRect PreviewLabel, Preview;
		PreviewRow.HSplitTop(LineHeight, &PreviewLabel, &Preview);
		Ui()->DoLabel(&PreviewLabel, Localize("Preview"), BodySize, TEXTALIGN_ML);
		const auto Style = qm_chat_bubble::StyleFromConfig(g_Config.m_QmChatBubbleStyle);
		const float FontSize = static_cast<float>(g_Config.m_QmChatBubbleFontSize);
		const char *pText = Localize("Let's play together!");
		const unsigned int PreviousFlags = TextRender()->GetRenderFlags();
		const ColorRGBA PreviousColor = TextRender()->GetTextColor();
		const ColorRGBA PreviousOutline = TextRender()->GetTextOutlineColor();
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
		CTextCursor Measure;
		Measure.m_FontSize = FontSize;
		Measure.m_LineWidth = std::max(40.0f, std::min(qm_chat_bubble::MAX_TEXT_WIDTH, Preview.w - 40.0f));
		Measure.m_Flags = 0;
		TextRender()->TextEx(&Measure, pText);
		const vec2 TextSize(Measure.m_LongestLineWidth, Measure.Height());
		const auto Natural = qm_chat_bubble::Layout(vec2(0, 0), TextSize, FontSize, 1.0f, Style);
		const float FullHeight = Natural.m_Size.y * (Style == qm_chat_bubble::EStyle::CLOUD ? 1.25f : 1.0f) + Natural.m_TailHeight + 5.0f;
		const float Scale = std::max(0.0f, std::min({1.0f, (Preview.w - 12.0f) / (Natural.m_Size.x + 8.0f), (Preview.h - 8.0f) / FullHeight}));
		const vec2 Anchor(Preview.x + Preview.w * 0.5f, Preview.y + (Preview.h - FullHeight * Scale) * 0.5f + (FullHeight - 4.0f) * Scale);
		const auto Bubble = qm_chat_bubble::Layout(Anchor, TextSize, FontSize, Scale, Style);
		const auto Colors = qm_chat_bubble::Palette(Style, g_Config.m_QmChatBubbleCustomColors != 0,
			color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmChatBubbleBgColor, true)), color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmChatBubbleTextColor, false)), g_Config.m_QmChatBubbleAlpha / 100.0f);
		if(Scale > 0.0f)
		{
			if(Style == qm_chat_bubble::EStyle::CLOUD)
				GameClient()->m_NamePlates.RenderCloudBubble(Bubble, Colors);
			else
				qm_chat_bubble::Render(Graphics(), Bubble, Colors);
			TextRender()->TextColor(Colors.m_Text);
			TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha((g_Config.m_QmChatBubbleCustomColors || Style == qm_chat_bubble::EStyle::DEFAULT) ? Colors.m_Text.a : 0.0f));
			CTextCursor Cursor;
			Cursor.m_FontSize = FontSize * Scale;
			Cursor.m_LineWidth = Measure.m_LineWidth * Scale;
			Cursor.m_Flags = TEXTFLAG_RENDER;
			Cursor.SetPosition(Bubble.m_TextPosition);
			qm_chat_bubble::RenderText(TextRender(), Cursor, pText, Colors.m_Text,
				TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha((g_Config.m_QmChatBubbleCustomColors || Style == qm_chat_bubble::EStyle::DEFAULT) ? Colors.m_Text.a : 0.0f));
		}
		TextRender()->SetRenderFlags(PreviousFlags);
		TextRender()->TextColor(PreviousColor);
		TextRender()->TextOutlineColor(PreviousOutline);
	}

	float SectionReveal = 0.0f;
	if(!qm_card_catalog::QmCardRenderHook::PersonalizationReveal(this, Content, "qm:chat_bubble", CurrentSettingsContentMetrics(), &SectionReveal, PrewarmOnly || Ui()->RenderOnly()))
		return;
	const CSettingsContentRevealScope SectionRevealScope(Ui(), Flow, SectionReveal);

	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int Min, int Max, const char *pSuffix = "") {
		CUIRect Row, LabelColumn, ControlColumn;
		const bool HasRow = Flow.Control(pTextId, LineHeight, Row, ui_token::settings::RowGap(LineSpacing));
		if(!HasRow)
			return;
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		RenderQmVisualLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(pInputId, &Row, pValue, &LabelColumn);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, Min, Max, pSuffix, PrewarmOnly);
	};
	static int s_QmChatBubbleDurationInputId;
	static int s_QmChatBubbleAlphaInputId;
	static int s_QmChatBubbleFontSizeInputId;
	RenderValue("qmclient-chat-bubble-duration", "Duration", &s_QmChatBubbleDurationInputId, &g_Config.m_QmChatBubbleDuration, 1, 30, "s");
	RenderValue("qmclient-chat-bubble-opacity", "Bubble opacity", &s_QmChatBubbleAlphaInputId, &g_Config.m_QmChatBubbleAlpha, 0, 100, "%");
	RenderValue("qmclient-chat-bubble-font-size", "Font size", &s_QmChatBubbleFontSizeInputId, &g_Config.m_QmChatBubbleFontSize, 8, 32);

	CUIRect Row, LabelColumn, ControlColumn;
	if(Flow.Control("qm-bubble-animation", LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
	{
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		RenderQmVisualLabel("qmclient-chat-bubble-animation", &LabelColumn, Localize("Animation"), BodySize);
		if(qm_chat_bubble::StyleFromConfig(g_Config.m_QmChatBubbleStyle) == qm_chat_bubble::EStyle::CLOUD)
			RenderQmVisualLabel("qm-chat-bubble-cloud-motion", &ControlColumn, Localize("Gather and disperse"), BodySize);
		else
		{
			static std::vector<const char *> s_ChatBubbleAnimDropDownNames;
			s_ChatBubbleAnimDropDownNames = {Localize("Dissolve"), Localize("Shrink"), Localize("Bounce")};
			static CUi::SDropDownState s_ChatBubbleAnimDropDownState;
			static CScrollRegion s_ChatBubbleAnimDropDownScrollRegion;
			s_ChatBubbleAnimDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_ChatBubbleAnimDropDownScrollRegion;
			const int Animation = DoSettingsDropDown(&ControlColumn, g_Config.m_QmChatBubbleAnimation, s_ChatBubbleAnimDropDownNames.data(), s_ChatBubbleAnimDropDownNames.size(), s_ChatBubbleAnimDropDownState, {}, &g_Config.m_QmChatBubbleAnimation, nullptr, &Row);
			if(g_Config.m_QmChatBubbleAnimation != Animation)
				g_Config.m_QmChatBubbleAnimation = Animation;
		}
	}

	// 下面这一行自己声明行距，这里不再补边界行距。
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmChatBubbleCustomColors, "qm-chat-bubble-custom-colors", Localize("Use custom colors"), &g_Config.m_QmChatBubbleCustomColors);
	if(!g_Config.m_QmChatBubbleCustomColors)
		return;

	static CButtonContainer s_ChatBubbleBgColorId;
	static CButtonContainer s_ChatBubbleTextColorId;
	CUIRect s_ChatBubbleBgColorIdRow;
	if(Flow.Control("s_ChatBubbleBgColorId", LineHeight, s_ChatBubbleBgColorIdRow, ui_token::settings::RowGap(LineSpacing)))
	{
		CUIRect ColorRow = s_ChatBubbleBgColorIdRow;
		// 重置默认值直接取配置项自己的出厂值（CConfig::ms_*），不再手抄一份颜色字面量。
		DoLine_ColorPicker(&s_ChatBubbleBgColorId, CurrentSettingsContentMetrics(), &ColorRow, Localize("Background color"), &g_Config.m_QmChatBubbleBgColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmChatBubbleBgColor, true)), false, nullptr, true);
	}
	CUIRect s_ChatBubbleTextColorIdRow;
	if(Flow.Control("s_ChatBubbleTextColorId", LineHeight, s_ChatBubbleTextColorIdRow, ui_token::settings::RowGap(LineSpacing)))
	{
		CUIRect ColorRow = s_ChatBubbleTextColorIdRow;
		DoLine_ColorPicker(&s_ChatBubbleTextColorId, CurrentSettingsContentMetrics(), &ColorRow, Localize("Text color"), &g_Config.m_QmChatBubbleTextColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmChatBubbleTextColor)), false);
	}
}

void CMenus::RenderQmVisualFocusModeContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float ColumnGap, float LabelWidth)
{
	CSettingsContentLayoutFlow Flow(Content);
	SSettingsContentMetrics Metrics = CurrentSettingsContentMetrics();
	Metrics.m_LineHeight = LineHeight;
	Metrics.m_LineSpacing = LineSpacing;
	const float SmallSize = Metrics.m_SmallSize;
	static CButtonContainer s_ReaderButtonFocusToggle, s_ClearButtonFocusToggle;
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmFocusMode, "qmclient-focus-mode-enable", Localize("Enable Zen mode"), &g_Config.m_QmFocusMode);
	CUIRect Row;
	float SectionReveal = 0.0f;
	if(qm_card_catalog::QmCardRenderHook::PersonalizationReveal(this, Content, "qm:focus_mode", Metrics, &SectionReveal, Ui()->RenderOnly()))
	{
		const CSettingsContentRevealScope SectionRevealScope(Ui(), Flow, SectionReveal);
		// 分区标题也是普通行：行距由它自己声明，行后不再补 Space。
		const auto RenderSection = [&](const char *pTextId, const char *pLabel, float SpacingBefore) {
			if(Flow.Control(pTextId, SmallSize, Row, SpacingBefore))
			{
				TextRender()->TextColor(ColorRGBA(0.72f, 0.72f, 0.78f, 0.86f));
				RenderQmVisualLabel(pTextId, &Row, Localize(pLabel), SmallSize);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
		};
		// 首行紧接收起行：行距仍由这一行自己声明，helper 不再补行尾 Space。
		RenderSection("qmclient-focus-section-interface", "Interface", LineSpacing);
		DoSettingsToggleGroup(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, Content,
			{
				{&g_Config.m_QmFocusModeHideHud, "qmclient-focus-hide-hud", Localize("Hide HUD")},
				{&g_Config.m_QmFocusModeHideMapProgress, "qmclient-focus-hide-map-progress", Localize("Hide map progress")},
				{&g_Config.m_QmFocusModeHideInfoMessages, "qmclient-focus-hide-info-messages", Localize("Hide kill/finish messages")},
				{&g_Config.m_QmFocusModeHideScoreboard, "qmclient-focus-hide-scoreboard", Localize("Hide scoreboard")},
			},
			Metrics);
		RenderSection("qmclient-focus-section-players", "Players", LineSpacing);
		DoSettingsToggleGroup(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, Content,
			{
				{&g_Config.m_QmFocusModeHideNames, "qmclient-focus-hide-names", Localize("Hide names")},
				{&g_Config.m_QmFocusModeHideNameplates, "qmclient-focus-hide-nameplates", Localize("Hide nameplates")},
				{&g_Config.m_QmFocusModeHideDirectionIndicators, "qmclient-focus-hide-direction-indicators", Localize("Hide direction indicators")},
				{&g_Config.m_QmFocusModeHideGuideLines, "qmclient-focus-hide-guide-lines", Localize("Hide guide lines")},
			},
			Metrics);
		RenderSection("qmclient-focus-section-visuals", "Visuals", LineSpacing);
		DoSettingsToggleGroup(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, Content,
			{
				{&g_Config.m_QmFocusModeHideJumpEffects, "qmclient-focus-hide-jump-effects", Localize("Hide jump effects")},
				{&g_Config.m_QmFocusModeHideKillEffects, "qmclient-focus-hide-kill-effects", Localize("Hide death/respawn effects")},
				{&g_Config.m_QmFocusModeHideExplosionEffects, "qmclient-focus-hide-explosion-effects", Localize("Hide explosion effects")},
				{&g_Config.m_QmFocusModeHideFreezeEffects, "qmclient-focus-hide-freeze-effects", Localize("Hide freeze effects")},
				{&g_Config.m_QmFocusModeHideHammerEffects, "qmclient-focus-hide-hammer-effects", Localize("Hide hammer effects")},
				{&g_Config.m_QmFocusModeHideMuzzleEffects, "qmclient-focus-hide-muzzle-effects", Localize("Hide weapon muzzle flashes")},
			},
			Metrics);
		RenderSection("qmclient-focus-section-audio", "Audio", LineSpacing);
		DoSettingsToggleGroup(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, Content,
			{
				{&g_Config.m_QmFocusModeMuteJumpSounds, "qmclient-focus-mute-jump-sounds", Localize("Mute jump sounds")},
				{&g_Config.m_QmFocusModeMuteDeathSounds, "qmclient-focus-mute-death-sounds", Localize("Mute death/respawn sounds")},
				{&g_Config.m_QmFocusModeMuteHammerSounds, "qmclient-focus-mute-hammer-sounds", Localize("Mute hammer sounds")},
			},
			Metrics);
		RenderSection("qmclient-focus-section-chat", "Chat", LineSpacing);
		DoSettingsToggleGroup(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, Content,
			{
				{&g_Config.m_QmFocusModeHideChat, "qmclient-focus-hide-chat", Localize("Hide player messages")},
				{&g_Config.m_QmFocusModeHideSystemInfoMessages, "qmclient-focus-hide-system-info-messages", Localize("Hide join/version prompts")},
				{&g_Config.m_QmFocusModeHideSystemMessages, "qmclient-focus-hide-system-messages", Localize("Hide server prompt notifications")},
				{&g_Config.m_QmFocusModeHideEcho, "qmclient-focus-hide-echo", Localize("Hide Echo messages")},
			},
			Metrics);
		// 收起区结束：组不再留尾距，而下面那行在收起区之外，由这里补一档边界行距（收起时本段不声明）。
		// 这处不能改由下一行自己声明：下一行是手写行、没有行前间距参数，且它落在收起区之外，
		// 这一档必须跟着收起动画一起缩放，所以保留在区间内由这里补。
		Flow.Space(ui_token::settings::RowGap(LineSpacing));
	}
	if(!Flow.Control("qmclient-focus-mode-key", LineHeight, Row))
		return;
	CUIRect BindLabel, BindKey;
	SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &BindLabel, &BindKey);
	RenderQmVisualLabel("qmclient-focus-mode-key", &BindLabel, Localize("Zen mode key"), BodySize);
	const CBindSlot FocusBind = QmLookupCommandBind(GameClient()->m_Binds, "toggle qm_focus_mode 0 1");
	const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&s_ReaderButtonFocusToggle, &s_ClearButtonFocusToggle, &BindKey, FocusBind, false);
	if(Flow.Input() && !Ui()->RenderOnly() && Result.m_Bind != FocusBind)
		GameClient()->m_Binds.Rebind(FocusBind, Result.m_Bind, "toggle qm_focus_mode 0 1");
}

void CMenus::RenderQmVisualSpectatorModeContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmCinematicCamera, "qmclient-spectator-smoothing", Localize("Smooth free spectator camera"), &g_Config.m_QmCinematicCamera);
	static CSettingsConditionalRows s_CinematicCameraRows;
	if(s_CinematicCameraRows.Update(g_Config.m_QmCinematicCamera, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
	{
		const CSettingsContentRevealScope CinematicCameraRevealScope(Ui(), Flow, s_CinematicCameraRows.Fade(g_Config.m_QmUiMotionLevel));
		static int s_SmoothnessInputId;
		CUIRect Row, LabelColumn, ControlColumn;
		const bool HasRow = Flow.Control("qm-spectator-smoothness", LineHeight, Row, ui_token::settings::RowGap(LineSpacing));
		if(!HasRow)
			return;
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		RenderQmVisualLabel("qmclient-cinematic-camera-smoothness", &LabelColumn, Localize("Cinematic smoothness"), BodySize);
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(&s_SmoothnessInputId, &Row, &g_Config.m_QmCinematicCameraSmoothness, &LabelColumn);
		RenderQmSettingsSliderWithValueInput(&s_SmoothnessInputId, ControlColumn, &g_Config.m_QmCinematicCameraSmoothness, 0, 100, "%", PrewarmOnly);
	}
}

void CMenus::RenderQmVisualCameraViewContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pId, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "", unsigned Flags = 0u) {
		CUIRect Row, LabelColumn, ControlColumn;
		// 行距由行自己声明，行后不再补 Space。
		const bool HasRow = Flow.Control(pTextId, LineHeight, Row, ui_token::settings::RowGap(LineSpacing));
		if(!HasRow)
			return;
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		RenderQmVisualLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(pId, &Row, pValue, &LabelColumn);
		RenderQmSettingsSliderWithValueInput(pId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly, Flags);
	};
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmCameraDrift, "Enable camera drift", Localize("Enable camera drift"), &g_Config.m_QmCameraDrift);
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmDynamicFov, "Enable dynamic FOV", Localize("Enable dynamic FOV"), &g_Config.m_QmDynamicFov);
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmZoomInstantReverse, "Instant zoom reverse", Localize("Instant zoom reverse"), &g_Config.m_QmZoomInstantReverse);

	if(g_Config.m_QmCameraDrift)
	{
		static int s_QmCameraDriftAmountInputId;
		static int s_QmCameraDriftSmoothnessInputId;
		RenderValue("qmclient-camera-drift-intensity", "Drift intensity", &s_QmCameraDriftAmountInputId, &g_Config.m_QmCameraDriftAmount, 0, 200);
		RenderValue("qmclient-camera-drift-smoothness", "Drift smoothness", &s_QmCameraDriftSmoothnessInputId, &g_Config.m_QmCameraDriftSmoothness, 0, 100, "%");
		RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmCameraDriftReverse, "Drift direction", Localize("Drift direction"), &g_Config.m_QmCameraDriftReverse);
	}
	if(g_Config.m_QmDynamicFov)
	{
		static int s_QmDynamicFovAmountInputId;
		static int s_QmDynamicFovSmoothnessInputId;
		RenderValue("qmclient-camera-dynamic-fov-intensity", "Dynamic FOV intensity", &s_QmDynamicFovAmountInputId, &g_Config.m_QmDynamicFovAmount, 0, 200);
		RenderValue("qmclient-camera-dynamic-fov-smoothness", "Dynamic FOV smoothness", &s_QmDynamicFovSmoothnessInputId, &g_Config.m_QmDynamicFovSmoothness, 0, 100, "%");
	}
	static int s_QmUiScaleInputId;
	RenderValue("qmclient-ui-scale", "UI scale", &s_QmUiScaleInputId, &g_Config.m_QmUiScale, 50, 200, "%", CUi::SCROLLBAR_OPTION_DELAYUPDATE);
	const char *apAspectPresetNames[] = {Localize("Off"), "5:4", "4:3", "3:2", "16:9", "21:9", Localize("Custom")};
	static CUi::SDropDownState s_AspectPresetDropDownState;
	static CScrollRegion s_AspectPresetDropDownScrollRegion;
	s_AspectPresetDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_AspectPresetDropDownScrollRegion;
	CUIRect Row, LabelColumn, ControlColumn;
	const bool HasPresetRow = Flow.Control("qm-camera-aspect-preset", LineHeight, Row, ui_token::settings::RowGap(LineSpacing));
	bool AspectChanged = false;
	if(HasPresetRow)
	{
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		RenderQmVisualLabel("qmclient-camera-aspect-ratio-preset", &LabelColumn, Localize("Aspect ratio preset"), BodySize);
		const int CurrentPreset = std::clamp(g_Config.m_QmAspectPreset, 0, 6);
		const int NewPreset = DoSettingsDropDown(&ControlColumn, CurrentPreset, apAspectPresetNames, (int)std::size(apAspectPresetNames), s_AspectPresetDropDownState, {}, &g_Config.m_QmAspectPreset, &g_Config.m_QmAspectRatio, &Row);
		AspectChanged = NewPreset != CurrentPreset;
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
	}
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
	char aAspectInfo[128];
	if(EffectiveAspectValue > 0)
		str_format(aAspectInfo, sizeof(aAspectInfo), "%s %.2f:1", Localize("Current aspect ratio:"), EffectiveAspectValue / 100.0f);
	else
		str_copy(aAspectInfo, Localize("Current aspect ratio: Show default"), sizeof(aAspectInfo));
	// Flow.Text 只有行后间距、没有「行前间距」参数：这一行之上的一档仍由上一行补。
	Flow.Space(ui_token::settings::RowGap(LineSpacing));
	Flow.Text("qm-camera-effective-aspect", aAspectInfo, 0.0f, BodySize, ui_token::settings::RowGap(LineSpacing), [this](const char *pValue, float Width, float FontSize) { return TextRender()->TextBoundingBox(FontSize, pValue, -1, Width).m_H + FontSize * 0.25f; }, [this, BodySize](const CUIRect &TextRow, const char *pValue) { SLabelProperties Props; Props.m_MaxWidth = TextRow.w; Ui()->DoLabel(&TextRow, pValue, BodySize, TEXTALIGN_ML, Props); });
	if(AspectChanged && !PrewarmOnly && Flow.Input() && !Ui()->RenderOnly())
		GameClient()->TClientComponent().QueueAspectApply();
}
