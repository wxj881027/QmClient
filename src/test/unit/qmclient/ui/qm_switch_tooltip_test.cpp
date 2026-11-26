#include <game/client/QmUi/UiSwitchTipPolicy.h>

#include <gtest/gtest.h>

TEST(SettingsSwitchTooltip, RecognizesLocalizedRepetitionsWithoutConfigTable)
{
	for(const auto &[pLabel, pHelp] : {std::pair{"Play background music", " ENABLE   play Background MUSIC! "}, {"是否显示 HUD", "开启显示HUD选项。"}, {"隐藏 HUD", "在禅模式中隐藏 HUD。"}, {"Mute jump sounds", "Mute jump sounds in Zen Mode"}, {"Affichage ÉTENDU", "affichage étendu."}, {"使用自定义聊天气泡颜色", "聊天气泡颜色功能"}, {"Show player scoreboard after death", "Show scoreboard"}})
	{
		SCOPED_TRACE(pHelp);
		EXPECT_TRUE(ui_switch_tip::RepeatsLabel(pLabel, pHelp, true));
	}
}

TEST(SettingsSwitchTooltip, KeepsConditionsUnitsPathsAndWordBoundaries)
{
	for(const auto &[pLabel, pHelp] : {std::pair{"Show HUD", "Show HUD in Zen mode"}, {"Show HUD", "Show HUD only after 400 ms"}, {"Show HUD", "Show HUD at 50% opacity"}, {"Enable capture", "Press Ctrl+F1 to save under screenshots/"}, {"开启碰撞", "仅当服务器允许时开启碰撞"}, {"Show gameplay", "play"}, {"Enable respawn", "spawn"}, {"Show", "Enable"}})
	{
		SCOPED_TRACE(pHelp);
		EXPECT_FALSE(ui_switch_tip::RepeatsLabel(pLabel, pHelp, false));
	}
}

TEST(SettingsSwitchTooltip, UpdatedConfigHelpIsNotHiddenByAnOldAudit)
{
	const char *pHelp = "Only after a server reply";
	EXPECT_STREQ(ui_switch_tip::Fallback("qm_ui_card_rainbow_titles", pHelp, "Rainbow card titles", pHelp), pHelp);
	EXPECT_STREQ(ui_switch_tip::Fallback("qm_ui_card_rainbow_titles", pHelp, "彩虹卡片标题", "彩虹卡片标题"), "彩虹卡片标题");
	EXPECT_STREQ(ui_switch_tip::Fallback("qm_ui_card_rainbow_titles", "Settings card titles use rainbow colors", "", pHelp), pHelp);
}

class MechanismSwitchTooltip : public testing::TestWithParam<const char *>
{
};

TEST_P(MechanismSwitchTooltip, KeepsNearMatchesAndSuppressesOnlyVerbatimLocalizedHelp)
{
	const char *pCommand = GetParam();
	const char *pLabel = "Nie przewiduj innych graczy, gdy jesteś zamrożony";
	const char *pNearMatch = "Nie przewiduj innych graczy, gdy jesteś zamrożony.";
	EXPECT_STREQ(ui_switch_tip::Fallback(pCommand, "Config help", pLabel, pNearMatch), pNearMatch);
	EXPECT_STREQ(ui_switch_tip::Fallback(pCommand, "Config help", pLabel, pLabel), "");
}

static constexpr const char *s_apTask5Commands[] = {
	"cl_antiping",
	"cl_antiping_grenade",
	"cl_antiping_gunfire",
	"cl_antiping_players",
	"cl_antiping_weapons",
	"cl_background_entities",
	"cl_predict_events",
	"cl_refresh_rate",
	"qm_antiping_negative_buffer",
	"qm_auto_margin",
	"qm_auto_switch_on_unfreeze",
	"qm_auto_unspec_both_on_unfreeze",
	"qm_auto_unspec_on_unfreeze",
	"qm_axiom_quad_prediction",
	"qm_blank_asset_fallback",
	"qm_block_words_multi_replace",
	"qm_browser_favorite_expose_ip",
	"qm_chat_related_only",
	"qm_checkpoint_guide",
	"qm_fast_input",
	"qm_fast_input_others",
	"qm_fast_practice_show_main_world",
	"qm_freeze_wakeup_popup",
	"qm_ghost_view_feedback",
	"qm_gores",
	"qm_gores_auto_weapon_switch",
	"qm_gores_disable_dummy_hammer",
	"qm_gores_disable_if_weapons",
	"qm_gores_fast_input",
	"qm_gores_fast_input_others",
	"qm_gores_suppress_switch_anim",
	"qm_hide_chat_bubbles",
	"qm_hitbox_show_tee_freeze",
	"qm_hook_coll_pressed_custom",
	"qm_hud_notifications_compat_solo",
	"qm_jump_hint",
	"qm_mini_debug",
	"qm_moving_tiles_entities",
	"qm_nameplate_coord_x_align_hint",
	"qm_nameplate_coord_x_align_hint_strict",
	"qm_nameplate_effect_auto_lod",
	"qm_nameplate_free_move",
	"qm_nameplate_multi_select",
	"qm_nameplate_offsets_linked",
	"qm_net_qos",
	"qm_paused_spectator_alpha",
	"qm_paused_spectator_fade",
	"qm_prediction_margin_smooth",
	"qm_remove_anti",
	"qm_revert_hook_line",
	"qm_say_nopop",
	"qm_self_kill_enabled",
	"qm_settings_prewarm",
	"qm_show_others_ghosts",
	"qm_show_tune_zone_colors",
	"qm_solo_split_auto_team_lock",
	"qm_solo_split_link_dummy",
	"qm_swap_ghosts",
	"qm_translate_ftapi_auto_enable",
	"qm_translate_llm_enable_thinking",
	"qm_translate_outgoing_send_original",
	"qm_unpred_others_in_freeze",
	"qm_upstream_redundancy",
	"qm_voice_agc_enable",
	"qm_voice_group_global",
	"qm_voice_hear_on_spec_pos",
	"qm_voice_hear_vad",
	"qm_voice_ignore_distance",
	"qm_voice_vad_enable",
	"qm_voice_vad_threshold",
	"qm_water_hammer_highlight",
};

INSTANTIATE_TEST_SUITE_P(Task5, MechanismSwitchTooltip, testing::ValuesIn(s_apTask5Commands),
	[](const testing::TestParamInfo<const char *> &Info) { return Info.param; });

TEST(SettingsSwitchTooltip, SoloSplitShortLabelRetainsLocalizedLockConditions)
{
	const char *pHelp = "进入单刷分队模式后，本体和分身各自加入队伍时自动锁队，避免其他玩家进入。";
	EXPECT_STREQ(ui_switch_tip::Fallback("qm_solo_split_auto_team_lock", "Lock both teams immediately when entering solo split mode", "锁定队伍", pHelp), pHelp);
}
