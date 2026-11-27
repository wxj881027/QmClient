#ifndef GAME_CLIENT_QMUI_UISWITCHTIPPOLICY_H
#define GAME_CLIENT_QMUI_UISWITCHTIPPOLICY_H

#include <base/str.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>

namespace ui_switch_tip
{
	inline const std::string_view *AuditedSource(std::string_view Command)
	{
		struct SEntry
		{
			std::string_view m_Command;
			std::string_view m_Source;
		};
		static constexpr SEntry s_aEntries[] = {
			{"cl_auto_demo_record", "Auto record demo"},
			{"cl_auto_screenshot", "Auto-screenshot on game end"},
			{"cl_autoswitch_weapons", "Auto switch weapon on pickup"},
			{"cl_autoswitch_weapons_out_of_ammo", "Auto switch weapon on ammo depletion"},
			{"cl_chat_teamcolors", "Show player names in team color"},
			{"cl_nameplates_clan", "Show clan name on nameplate"},
			{"cl_nameplates_ids", "Show client ID"},
			{"cl_nameplates_ids_seperate_line", "Show client ID on a separate line in nametag"},
			{"cl_nameplates_teamcolors", "Show name in team color"},
			{"cl_replays", "Enable/disable replays"},
			{"cl_show_chat_friends", "Show only friend chat messages"},
			{"cl_show_chat_team_members_only", "Show only team chat messages"},
			{"cl_show_finish_messages", "Show finish messages"},
			{"cl_show_local_time_always", "Always show local time"},
			{"cl_showhud", "Show in-game HUD"},
			{"cl_showhud_ddrace", "Show in-game HUD (DDRace HUD)"},
			{"cl_showhud_dummy_actions", "Show in-game HUD (dummy controls)"},
			{"cl_showhud_healthammo", "Show in-game HUD (health + ammo)"},
			{"cl_showhud_key_status_control", "Show in-game HUD (clone control state)"},
			{"cl_showhud_key_status_hammer", "Show in-game HUD (hammer state)"},
			{"cl_showhud_key_status_reset", "Show in-game HUD (keyboard blocking status)"},
			{"cl_showhud_key_status_sync", "Show in-game HUD (dummy copy state)"},
			{"cl_showhud_player_angle", "Show in-game HUD (player aim angle)"},
			{"cl_showhud_player_position", "Show in-game HUD (player position)"},
			{"cl_showhud_player_speed", "Show in-game HUD (player speed)"},
			{"cl_showhud_score", "Show in-game HUD (score)"},
			{"cl_showhud_spectator_count", "Show in-game HUD (spectator count)"},
			{"cl_showkillmessages", "Show kill messages"},
			{"cl_skip_start_menu", "Skip start menu"},
			{"dummy7_use_custom_color_body", "Toggle use of custom body color"},
			{"dummy_use_custom_color", "Toggle custom color"},
			{"gfx_high_detail", "Show map details"},
			{"player7_use_custom_color_body", "Toggle use of custom body color"},
			{"player7_use_custom_color_feet", "Toggle use of custom color for feet"},
			{"player_use_custom_color", "Toggle use of custom colors"},
			{"qm_3d_particles", "Enable QmClient style background 3D particles"},
			{"qm_3d_particles_collide", "Background 3D particle collision"},
			{"qm_3d_particles_glow", "Enable background 3D particle glow"},
			{"qm_3d_particles_trail", "Enable background 3D particle trail"},
			{"qm_3d_particles_twinkle", "Enable background 3D particle flicker"},
			{"qm_auto_reply_muted", "Auto-reply to muted players"},
			{"qm_better_scoreboard", "Enable enhanced scoreboard presentation"},
			{"qm_change_name_near_finish", "Try to change name near finish"},
			{"qm_chat_bubble", "Show chat bubbles above players"},
			{"qm_chat_bubble_custom_colors", "Use custom chat bubble colors"},
			{"qm_chat_emoji_completion", "Show emoji completion in chat"},
			{"qm_chat_log_auto_save", "Auto-save chat log"},
			{"qm_dummy_miniview", "Show dummy mini view window"},
			{"qm_dummy_skin_queue_enabled", "Enable dummy skin queue rotation"},
			{"qm_emoticon_shadow", "Draw shadow behind emote"},
			{"qm_finish_summary", "Show finish summary in Dynamic Island"},
			{"qm_focus_mode", "Enable Zen Mode"},
			{"qm_focus_mode_hide_chat", "Hide player messages in Zen mode"},
			{"qm_focus_mode_hide_direction_indicators", "Hide direction in Zen mode"},
			{"qm_focus_mode_hide_echo", "Hide Echo messages in Zen mode"},
			{"qm_focus_mode_hide_explosion_effects", "Hide projectile effects in Zen Mode"},
			{"qm_focus_mode_hide_freeze_effects", "Hide freeze effects in Zen Mode"},
			{"qm_focus_mode_hide_guide_lines", "Hide helper lines in Zen mode"},
			{"qm_focus_mode_hide_hammer_effects", "Hide hammer effects in Zen Mode"},
			{"qm_focus_mode_hide_hud", "Hide HUD in Zen mode"},
			{"qm_focus_mode_hide_info_messages", "Hide kill and finish messages in Zen mode"},
			{"qm_focus_mode_hide_jump_effects", "Hide jump effects in Zen Mode"},
			{"qm_focus_mode_hide_kill_effects", "Hide death/respawn effects in Zen Mode"},
			{"qm_focus_mode_hide_map_progress", "Hide map progress bar in Zen mode"},
			{"qm_focus_mode_hide_muzzle_effects", "Hide weapon fire effects in Zen Mode"},
			{"qm_focus_mode_hide_nameplates", "Hide player name plates in Zen Mode"},
			{"qm_focus_mode_hide_names", "Hide player names in Zen Mode"},
			{"qm_focus_mode_hide_scoreboard", "Hide scoreboard in Zen mode"},
			{"qm_focus_mode_mute_death_sounds", "Mute death/respawn sound in Zen Mode"},
			{"qm_focus_mode_mute_hammer_sounds", "Mute hammer sound in Zen mode"},
			{"qm_focus_mode_mute_jump_sounds", "Mute jump sound in Zen Mode"},
			{"qm_friend_enter_auto_greet", "Auto greet friend on map join"},
			{"qm_friend_enter_broadcast", "Show friend join in large text"},
			{"qm_friend_online_notify", "Friend online notification"},
			{"qm_gores_drown_board_include_team0", "Include Team 0 in the Gores team drown board"},
			{"qm_gores_drown_board_show_tee", "Show Tees in the Gores team drown board"},
			{"qm_hide_join_server_info", "Do not auto-show server info on entering map"},
			{"qm_hud_island_show_team", "Show team on HUD Dynamic Island"},
			{"qm_hud_island_use_original_style", "Use original style for Dynamic Island"},
			{"qm_input_overlay", "Show input overlay"},
			{"qm_input_overlay_key_counts", "Show key press counts"},
			{"qm_mini_vote_hud", "Mini voting UI"},
			{"qm_nameplate_coord_x", "Nameplate position X"},
			{"qm_nameplate_coord_y", "Nameplate position Y"},
			{"qm_nameplate_coords", "Show others' nameplate coordinates"},
			{"qm_nameplate_coords_own", "Show own nameplate coordinates"},
			{"qm_outline_deep_freeze", "Show deep freeze tile outlines"},
			{"qm_outline_deep_unfreeze", "Show deep unfreeze tile outlines"},
			{"qm_outline_freeze", "Show freeze tile outlines"},
			{"qm_outline_kill", "Show death tile outlines"},
			{"qm_outline_nohook", "Show unhookable tile outlines"},
			{"qm_outline_solid", "Show hookable tile outlines"},
			{"qm_outline_tele", "Show teleporter tile outlines"},
			{"qm_outline_unfreeze", "Show unfreeze tile outlines"},
			{"qm_pet_show", "Show pet"},
			{"qm_pie_menu_copy_name_enabled", "Show copy name option"},
			{"qm_pie_menu_copy_skin_enabled", "Show copy skin option"},
			{"qm_pie_menu_enabled", "Enable Pie Menu"},
			{"qm_pie_menu_follow_enabled", "Show cross-server follow option"},
			{"qm_pie_menu_friend_enabled", "Show friend option"},
			{"qm_pie_menu_invite_team_enabled", "Show team invite option"},
			{"qm_pie_menu_join_team_enabled", "Show join team option"},
			{"qm_pie_menu_mention_enabled", "Show mention option"},
			{"qm_pie_menu_score_enabled", "Show player score option"},
			{"qm_pie_menu_spectate_enabled", "Show spectate option"},
			{"qm_pie_menu_swap_enabled", "Show swap option"},
			{"qm_pie_menu_whisper_enabled", "Show whisper option"},
			{"qm_player_stats_hud", "Enable player statistics panel"},
			{"qm_profile_clan", "Include clan when saving/loading config preset"},
			{"qm_profile_colors", "Include color when saving/loading config preset"},
			{"qm_profile_emote", "Include emoticon when saving/loading config preset"},
			{"qm_profile_flag", "Include country flag when saving/loading config preset"},
			{"qm_profile_name", "Include name when saving/loading config preset"},
			{"qm_profile_skin", "Include skin when saving/loading config preset"},
			{"qm_rainbow_hook", "Enable rainbow rendering for hook"},
			{"qm_rainbow_others", "Enable rainbow rendering for other players' Tees"},
			{"qm_rainbow_tees", "Enable rainbow rendering for Tee"},
			{"qm_rainbow_weapon", "Enable rainbow rendering for weapon"},
			{"qm_repeat_enabled", "Enable repeat function"},
			{"qm_scoreboard_on_death", "Show scoreboard after death"},
			{"qm_scoreboard_points", "Scoreboard score lookup"},
			{"qm_screenshot_watermark_map", "Screenshot watermark: include map name"},
			{"qm_screenshot_watermark_timestamp", "Screenshot watermark: include timestamp"},
			{"qm_show_local_time_seconds", "Include seconds in local time display"},
			{"qm_show_other_launch_emotes", "Show other players' launched emoticons"},
			{"qm_show_other_super_emotes", "Show other players' large emoticons"},
			{"qm_skin_change_transition", "Enable skin switch animation"},
			{"qm_skin_outline_local", "Show generated skin outline for local tees"},
			{"qm_skin_outline_others", "Show generated skin outline for other tees"},
			{"qm_skin_queue_enabled", "Enable skin queue rotation"},
			{"qm_skin_show_metadata", "Show skin release date and author"},
			{"qm_statusbar", "Enable status bar"},
			{"qm_statusbar_12_hour_clock", "Use 12-hour clock for local time"},
			{"qm_statusbar_labels", "Show labels on status bar items"},
			{"qm_statusbar_local_time_seconds", "Show local time seconds"},
			{"qm_tee_trail", "Enable Tee trail"},
			{"qm_tee_trail_others", "Show Tee trail for others"},
			{"qm_tooltip_animation", "Tooltip bounce animation"},
			{"qm_translate_auto", "Auto-translate incoming messages"},
			{"qm_translate_auto_outgoing", "Auto-translate outgoing messages"},
			{"qm_translate_show_advanced", "Show advanced translation options"},
			{"qm_ui_card_borders", "Show settings card borders"},
			{"qm_ui_card_rainbow_titles", "Settings card titles use rainbow colors"},
			{"qm_ui_compact_list", "Use compact list view for config variables"},
			{"qm_ui_favorite_icon_filled", "Draw the favorite star icon with the filled icon font style"},
			{"qm_ui_friend_icon_filled", "Draw the friend heart icon with the filled icon font style"},
			{"qm_ui_icon_custom_color_enabled", "Qm UI custom icon color"},
			{"qm_ui_only_modified", "Show only modified config variables"},
			{"qm_voice_enable", "Enable voice chat"},
			{"qm_voice_mic_mute", "Mute microphone"},
			{"qm_voice_show_advanced", "Show voice advanced options"},
			{"qm_voice_show_connection_status", "Show voice connection status"},
			{"qm_voice_stereo", "Voice stereo output"},
			{"qm_warlist_allow_duplicates", "Allow duplicate enemy entries"},
			{"qm_warlist_chat", "Show enemy color in chat"},
			{"qm_warlist_scoreboard", "Show enemy color in scoreboard"},
			{"qm_warlist_spectate", "Show enemy color in spectator menu"},
			{"snd_chat", "Enable chat message sound"},
			{"snd_enable", "Enable sound"},
			{"snd_enable_music", "Play background music"},
			{"snd_game", "Enable game sounds"},
			{"snd_gun", "Enable pistol sound"},
			{"snd_highlight", "Enable chat mention sound"},
			{"snd_nonactive_mute", "Mute when window inactive"},
			{"snd_servermessage", "Enable server message sound"},
			{"snd_team_chat", "Enable team chat message sound"},
		};
		const auto *pEntry = std::lower_bound(std::begin(s_aEntries), std::end(s_aEntries), Command, [](const SEntry &Entry, std::string_view Name) { return Entry.m_Command < Name; });
		return pEntry != std::end(s_aEntries) && pEntry->m_Command == Command ? &pEntry->m_Source : nullptr;
	}

	inline std::u32string Normalize(const char *pText)
	{
		std::u32string Text;
		while(*pText)
		{
			const int Code = str_utf8_decode(&pText);
			if(str_utf8_isspace(Code))
			{
				if(!Text.empty() && Text.back() != U' ')
					Text.push_back(U' ');
			}
			else
				Text.push_back(str_utf8_tolower_codepoint(Code));
		}
		return Text;
	}

	inline std::u32string_view Core(std::u32string_view Text, bool Zen)
	{
		constexpr std::u32string_view Edges = U" .,:;!?。．，、：；！？";
		Text.remove_prefix(std::min(Text.size(), Text.find_first_not_of(Edges)));
		Text = Text.substr(0, Text.find_last_not_of(Edges) + 1);
		if(Zen && Text.ends_with(U" in zen mode"))
			Text.remove_suffix(std::u32string_view(U" in zen mode").size());
		for(;;)
		{
			const auto Size = Text.size();
			if(Zen)
				for(std::u32string_view Prefix : {U"在禅模式中", U"在禅模式下", U"禅模式中"})
					if(Text.starts_with(Prefix))
						Text.remove_prefix(Prefix.size());
			for(std::u32string_view Prefix : {U"enable ", U"show ", U"use ", U"play ", U"draw ", U"allow ", U"toggle ", U"是否", U"启用", U"开启", U"打开", U"显示", U"使用", U"允许", U"播放", U"绘制", U" "})
				if(Text.starts_with(Prefix))
					Text.remove_prefix(Prefix.size());
			for(std::u32string_view Suffix : {U"功能", U"选项", U" "})
				if(Text.ends_with(Suffix))
					Text.remove_suffix(Suffix.size());
			if(Text.size() == Size)
				return Text;
		}
	}

	inline bool RepeatsLabel(const char *pLabel, const char *pHelp, bool Zen)
	{
		const auto LabelText = Normalize(pLabel);
		const auto HelpText = Normalize(pHelp);
		const auto Label = Core(LabelText, Zen);
		const auto Help = Core(HelpText, Zen);
		if(Help.empty())
			return false;
		const auto IsHan = [](char32_t Code) { return Code >= 0x3400 && Code <= 0x9fff; };
		for(auto Pos = Label.find(Help); Pos != decltype(Label)::npos; Pos = Label.find(Help, Pos + 1))
		{
			const auto End = Pos + Help.size();
			if((Pos == 0 || Label[Pos - 1] == U' ' || IsHan(Label[Pos - 1]) || IsHan(Help.front())) &&
				(End == Label.size() || Label[End] == U' ' || IsHan(Label[End]) || IsHan(Help.back())))
				return true;
		}
		return false;
	}

	inline const char *Fallback(std::string_view Command, const char *pSource, const char *pLabel, const char *pHelp)
	{
		if(pSource == nullptr || pLabel == nullptr || *pLabel == 0 || pHelp == nullptr || *pHelp == 0)
			return pHelp;
		static constexpr std::string_view s_aMechanismHelp[] = {
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
			"qm_humming",
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
			"qm_singing",
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
			"qm_world_lyrics",
		};
		if(std::binary_search(std::begin(s_aMechanismHelp), std::end(s_aMechanismHelp), Command))
			return str_comp(pLabel, pHelp) == 0 ? "" : pHelp;
		if(const auto *pAudited = AuditedSource(Command))
			return *pAudited == pSource ? "" : pHelp;
		return RepeatsLabel(pLabel, pHelp, Command.starts_with("qm_focus_mode_")) ? "" : pHelp;
	}
}

#endif
