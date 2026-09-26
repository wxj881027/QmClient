// This file can be included several times.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(Tcme, ScriptName, Def, Min, Max, Save, Desc) ;
#define MACRO_CONFIG_COL(Tcme, ScriptName, Def, Save, Desc) ;
#define MACRO_CONFIG_STR(Tcme, ScriptName, Len, Def, Save, Desc) ;
#endif

#if defined(CONF_FAMILY_WINDOWS)
MACRO_CONFIG_INT(TcAllowAnyRes, tc_allow_any_res, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Allow arbitrary game resolution when scaling (may cause issues on Windows)")
#else
MACRO_CONFIG_INT(TcAllowAnyRes, tc_allow_any_res, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Allow arbitrary game resolution when scaling (may cause issues on Windows)")
#endif

MACRO_CONFIG_INT(TcShowChatClient, tc_show_chat_client, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show client-generated chat messages, e.g., echo")

MACRO_CONFIG_INT(TcShowFrozenText, tc_frozen_tees_text, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show number of frozen Tees in team (0=Off, 1=Show active Tee, 2=Show frozen Tee)")
MACRO_CONFIG_INT(TcShowFrozenHud, tc_frozen_tees_hud, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show frozen Tee HUD")
MACRO_CONFIG_INT(TcShowFrozenHudSkins, tc_frozen_tees_hud_skins, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use ninja or dark skin in frozen Tee HUD")

MACRO_CONFIG_INT(TcFrozenHudTeeSize, tc_frozen_tees_size, 15, 8, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tee icon size in frozen Tee HUD (default 15)")
MACRO_CONFIG_INT(TcFrozenMaxRows, tc_frozen_tees_max_rows, 1, 1, 6, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Max rows in frozen Tee HUD")
MACRO_CONFIG_INT(TcFrozenHudTeamOnly, tc_frozen_tees_only_inteam, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show frozen Tee HUD only in team")

MACRO_CONFIG_INT(TcNameplatePingCircle, tc_nameplate_ping_circle, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show ping ring on name plate")
MACRO_CONFIG_INT(TcNameplateCountry, tc_nameplate_country, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show flag on name plate")
MACRO_CONFIG_INT(TcNameplateSkins, tc_nameplate_skins, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show skin name on name plate to find missing skins")

MACRO_CONFIG_INT(TcFakeCtfFlags, tc_fake_ctf_flags, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show dummy CTF flags on players (0=Off, 1=Red flag, 2=Blue flag)")

MACRO_CONFIG_INT(TcLimitMouseToScreen, tc_limit_mouse_to_screen, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Clamp mouse to screen")
MACRO_CONFIG_INT(TcScaleMouseDistance, tc_scale_mouse_distance, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Scale max mouse distance to 1000 for better aim")

MACRO_CONFIG_INT(TcHammerRotatesWithCursor, tc_hammer_rotates_with_cursor, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Rotate hammer with cursor like other weapons")

MACRO_CONFIG_INT(TcMiniVoteHud, tc_mini_vote_hud, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Mini voting UI")

// Anti Latency Tools
MACRO_CONFIG_INT(TcRemoveAnti, tc_remove_anti, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reduce antiping and player prediction when frozen")
MACRO_CONFIG_INT(TcUnfreezeLagTicks, tc_remove_anti_ticks, 5, 0, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Max prediction ticks reduced")
MACRO_CONFIG_INT(TcUnfreezeLagDelayTicks, tc_remove_anti_delay_ticks, 25, 5, 150, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Ticks after freeze before applying max prediction reduction")

MACRO_CONFIG_INT(TcUnpredOthersInFreeze, tc_unpred_others_in_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Do not predict other players when frozen")
MACRO_CONFIG_INT(TcPredMarginInFreeze, tc_pred_margin_in_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable custom prediction margin when frozen")
MACRO_CONFIG_INT(TcPredMarginInFreezeAmount, tc_pred_margin_in_freeze_amount, 15, 0, 2000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Prediction margin when frozen")

MACRO_CONFIG_INT(TcShowOthersGhosts, tc_show_others_ghosts, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show ghost at unpredictable position of other players")
MACRO_CONFIG_INT(TcSwapGhosts, tc_swap_ghosts, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show predicted position as ghost, original as unpredictable player")
MACRO_CONFIG_INT(TcHideFrozenGhosts, tc_hide_frozen_ghosts, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide other players' ghosts when frozen")

MACRO_CONFIG_INT(TcPredGhostsAlpha, tc_pred_ghosts_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Prediction ghost opacity (0-100)")
MACRO_CONFIG_INT(TcUnpredGhostsAlpha, tc_unpred_ghosts_alpha, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Unpredictable ghost opacity (0-100)")
MACRO_CONFIG_INT(TcRenderGhostAsCircle, tc_render_ghost_as_circle, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render ghosts as circles instead of Tee")

MACRO_CONFIG_INT(TcShowCenter, tc_show_center, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Draw lines showing screen/clickbox center")
MACRO_CONFIG_INT(TcShowCenterWidth, tc_show_center_width, 0, 0, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Center line width (requires tc_show_center)")
MACRO_CONFIG_COL(TcShowCenterColor, tc_show_center_color, 1694498688, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Center line color (requires tc_show_center)") // transparent red

MACRO_CONFIG_INT(TcHookCollCursor, tc_hook_coll_cursor, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hook collision line length follows cursor distance")

MACRO_CONFIG_INT(TcFastInput, tc_fast_input, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply input early before next tick for prediction")
MACRO_CONFIG_INT(TcFastInputAmount, tc_fast_input_amount, 20, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Milliseconds to apply quick input early")
MACRO_CONFIG_INT(TcFastInputOthers, tc_fast_input_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply quick input to other Tees as well")

MACRO_CONFIG_INT(TcAntiPingImproved, tc_antiping_improved, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use alternative antiping smoothing, incompatible with cl_antiping_smooth")
MACRO_CONFIG_INT(TcAntiPingNegativeBuffer, tc_antiping_negative_buffer, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Helpful for Gores: allow negative internal certainty for more conservative prediction")
MACRO_CONFIG_INT(TcAntiPingStableDirection, tc_antiping_stable_direction, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "More optimistic prediction along Tee's stable direction, reduces delay to stabilize")
MACRO_CONFIG_INT(TcAntiPingUncertaintyScale, tc_antiping_uncertainty_scale, 150, 25, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Scale uncertainty duration by ping (100=1.0x)")

MACRO_CONFIG_INT(TcColorFreeze, tc_color_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use skin color when Tee is frozen")
MACRO_CONFIG_INT(TcColorFreezeDarken, tc_color_freeze_darken, 90, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Darken Tee color when frozen (0-100)")
MACRO_CONFIG_INT(TcColorFreezeFeet, tc_color_freeze_feet, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use skin color for frozen Tee's feet")

// Revert Variables
MACRO_CONFIG_INT(TcSmoothPredictionMargin, tc_prediction_margin_smooth, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Smooth prediction margin (weakens ping jitter correction, restores old behavior)")
MACRO_CONFIG_INT(TcFreezeKatana, tc_frozen_katana, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show katana on frozen players (restores old behavior)")
MACRO_CONFIG_INT(TcOldTeamColors, tc_old_team_colors, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use old rainbow team colors (restores old behavior)")
MACRO_CONFIG_INT(TcRevertHookLine, tc_revert_hook_line, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Restore old single-segment hook collision line behavior")

// Freeze Auto Emoticon and Chat
MACRO_CONFIG_INT(TcFreezeChatEnabled, tc_freeze_chat_enabled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-send emote and chat message when entering freeze")
MACRO_CONFIG_INT(TcFreezeChatEmoticon, tc_freeze_chat_emoticon, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Send emote when entering freeze")
MACRO_CONFIG_INT(TcFreezeChatEmoticonId, tc_freeze_chat_emoticon_id, 7, 0, 15, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Emote ID to send when entering freeze (0-15)")
MACRO_CONFIG_STR(TcFreezeChatMessage, tc_freeze_chat_message, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat messages to send when entering freeze, comma-separated (empty=none)")
MACRO_CONFIG_INT(TcFreezeChatChance, tc_freeze_chat_chance, 30, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Probability of sending freeze chat message (0-100%)")

// Outline Variables
MACRO_CONFIG_INT(TcOutline, tc_outline, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable tile outlines")
MACRO_CONFIG_INT(TcOutlineEntities, tc_outline_in_entities, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show outlines only on entity layer")
MACRO_CONFIG_INT(TcOutlineAlpha, tc_outline_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Outline global opacity")
MACRO_CONFIG_INT(TcOutlineSolidAlpha, tc_outline_solid_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Entity wall outline opacity")

MACRO_CONFIG_INT(TcOutlineSolid, tc_outline_solid, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show hookable/unhookable tile outlines")
MACRO_CONFIG_INT(TcOutlineFreeze, tc_outline_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show freeze/deep freeze tile outlines")
MACRO_CONFIG_INT(TcOutlineUnfreeze, tc_outline_unfreeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show unfreeze/deep unfreeze tile outlines")
MACRO_CONFIG_INT(TcOutlineKill, tc_outline_kill, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show death tile outlines")
MACRO_CONFIG_INT(TcOutlineTele, tc_outline_tele, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show teleporter tile outlines")

MACRO_CONFIG_INT(TcOutlineWidthSolid, tc_outline_width_solid, 2, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hookable/unhookable tile outline width")
MACRO_CONFIG_INT(TcOutlineWidthFreeze, tc_outline_width_freeze, 2, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Freeze/Deep freeze tile outline width")
MACRO_CONFIG_INT(TcOutlineWidthUnfreeze, tc_outline_width_unfreeze, 2, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Unfreeze/Deep unfreeze tile outline width")
MACRO_CONFIG_INT(TcOutlineWidthKill, tc_outline_width_kill, 2, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Death tile outline width")
MACRO_CONFIG_INT(TcOutlineWidthTele, tc_outline_width_tele, 2, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Teleport tile outline width")

MACRO_CONFIG_COL(TcOutlineColorSolid, tc_outline_color_solid, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Hookable/Unhookable tile outline color") // 255 0 0 0
MACRO_CONFIG_COL(TcOutlineColorFreeze, tc_outline_color_freeze, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Freeze tile outline color") // 255 0 0 0
MACRO_CONFIG_COL(TcOutlineColorDeepFreeze, tc_outline_color_deep_freeze, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Deep freeze tile outline color") // 255 0 0 0
MACRO_CONFIG_COL(TcOutlineColorUnfreeze, tc_outline_color_unfreeze, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Unfreeze tile outline color") // 255 0 0 0
MACRO_CONFIG_COL(TcOutlineColorDeepUnfreeze, tc_outline_color_deep_unfreeze, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Deep unfreeze tile outline color") // 255 0 0 0
MACRO_CONFIG_COL(TcOutlineColorKill, tc_outline_color_kill, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Death tile outline color") // 0 0 0
MACRO_CONFIG_COL(TcOutlineColorTele, tc_outline_color_tele, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Teleport tile outline color") // 255 0 0 0

// Indicator Variables
MACRO_CONFIG_COL(TcIndicatorAlive, tc_indicator_alive, 255, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Player indicator alive Tee color")
MACRO_CONFIG_COL(TcIndicatorFreeze, tc_indicator_freeze, 65407, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Player indicator frozen Tee color")
MACRO_CONFIG_COL(TcIndicatorSaved, tc_indicator_dead, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Player indicator unfreezing Tee color")
MACRO_CONFIG_INT(TcIndicatorOffset, tc_indicator_offset, 42, 16, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Indicator offset distance")
MACRO_CONFIG_INT(TcIndicatorOffsetMax, tc_indicator_offset_max, 100, 16, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Max offset distance for variable offset")
MACRO_CONFIG_INT(TcIndicatorVariableDistance, tc_indicator_variable_distance, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Indicator moves farther away as distance increases")
MACRO_CONFIG_INT(TcIndicatorMaxDistance, tc_indicator_variable_max_distance, 1000, 500, 7000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Max distance for variable offset calculation")
MACRO_CONFIG_INT(TcIndicatorRadius, tc_indicator_radius, 4, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Indicator radius")
MACRO_CONFIG_INT(TcIndicatorOpacity, tc_indicator_opacity, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Indicator transparency")
MACRO_CONFIG_INT(TcPlayerIndicator, tc_player_indicator, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show radial indicators of other Tees")
MACRO_CONFIG_INT(TcPlayerIndicatorFreeze, tc_player_indicator_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show only frozen Tees in indicators")
MACRO_CONFIG_INT(TcIndicatorTeamOnly, tc_indicator_inteam, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show indicators only within team")
MACRO_CONFIG_INT(TcIndicatorTees, tc_indicator_tees, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use Tee icons instead of circles")
MACRO_CONFIG_INT(TcIndicatorHideVisible, tc_indicator_hide_visible_tees, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Do not show Tees visible on screen")

// Bind Wheel
MACRO_CONFIG_INT(TcResetBindWheelMouse, tc_reset_bindwheel_mouse, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reset mouse position when opening bind wheel")

// Regex chat matching
MACRO_CONFIG_STR(TcRegexChatIgnore, tc_regex_chat_ignore, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Filter chat messages by regular expression")

// Misc visual
MACRO_CONFIG_INT(TcWhiteFeet, tc_white_feet, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render all feet as solid white base")
MACRO_CONFIG_STR(TcWhiteFeetSkin, tc_white_feet_skin, 255, "x_ninja", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Skin used for white feet base")
MACRO_CONFIG_INT(TcMovingTilesEntities, tc_moving_tiles_entities, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show moving tiles in entity layer")

MACRO_CONFIG_INT(TcMiniDebug, tc_mini_debug, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show position and angle debug info")

MACRO_CONFIG_INT(TcShowhudDummyPosition, tc_showhud_dummy_position, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show dummy position in movement info HUD")
MACRO_CONFIG_INT(TcShowhudDummySpeed, tc_showhud_dummy_speed, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show dummy velocity in movement info HUD")
MACRO_CONFIG_INT(TcShowhudDummyAngle, tc_showhud_dummy_angle, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show dummy angle in movement info HUD")
MACRO_CONFIG_INT(TcShowLocalTimeSeconds, tc_show_local_time_seconds, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Include seconds in local time display")

MACRO_CONFIG_INT(TcNotifyWhenLast, tc_last_notify, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show notification when only one Tee alive")
MACRO_CONFIG_STR(TcNotifyWhenLastText, tc_last_notify_text, 64, "Last!", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Alive notification text")
MACRO_CONFIG_COL(TcNotifyWhenLastColor, tc_last_notify_color, 256, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Alive notification color")
MACRO_CONFIG_INT(TcNotifyWhenLastX, tc_last_notify_x, 20, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Alive notification horizontal position (percentage of screen width)")
MACRO_CONFIG_INT(TcNotifyWhenLastY, tc_last_notify_y, 1, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Alive notification vertical position (percentage of screen height)")
MACRO_CONFIG_INT(TcNotifyWhenLastSize, tc_last_notify_size, 10, 0, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Alive notification font size")
MACRO_CONFIG_INT(TcJumpHintLegacy, tc_jump_hint, 0, 0, 1, CFGFLAG_CLIENT, "Deprecated: jump hint toggle, migrated to qm_jump_hint on startup")
MACRO_CONFIG_STR(TcJumpHintTextLegacy, tc_jump_hint_text, 512, "", CFGFLAG_CLIENT, "Deprecated: jump hint text, migrated to qm_jump_hint_text on startup (use \\n for newline)")
MACRO_CONFIG_COL(TcJumpHintColorLegacy, tc_jump_hint_color, 255, CFGFLAG_CLIENT, "Deprecated: jump hint color, migrated to qm_jump_hint_color on startup")
MACRO_CONFIG_INT(TcJumpHintXLegacy, tc_jump_hint_x, 20, 0, 100, CFGFLAG_CLIENT, "Deprecated: jump hint X position, migrated to qm_jump_hint_x on startup")
MACRO_CONFIG_INT(TcJumpHintYLegacy, tc_jump_hint_y, 5, 0, 100, CFGFLAG_CLIENT, "Deprecated: jump hint Y position, migrated to qm_jump_hint_y on startup")
MACRO_CONFIG_INT(TcJumpHintSizeLegacy, tc_jump_hint_size, 10, 0, 50, CFGFLAG_CLIENT, "Deprecated: jump hint font size, migrated to qm_jump_hint_size on startup")

MACRO_CONFIG_INT(TcRenderCursorSpec, tc_cursor_in_spec, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render crosshair in free spectate mode")
MACRO_CONFIG_INT(TcRenderCursorSpecAlpha, tc_cursor_in_spec_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Crosshair opacity in free spectate")

// MACRO_CONFIG_INT(TcRenderNameplateSpec, tc_render_nameplate_spec, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "旁观时渲染名字板")

MACRO_CONFIG_INT(TcTinyTees, tc_tiny_tees, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Scale down Tee")
MACRO_CONFIG_INT(TcTinyTeeSize, tc_indicator_tees_size, 100, 85, 115, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Small Tee size")
MACRO_CONFIG_INT(TcTinyTeesOthers, tc_tiny_tees_others, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Scale down other Tees as well")

MACRO_CONFIG_INT(TcCursorScale, tc_cursor_scale, 100, 0, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "In-game crosshair scale percent (50=half, 200=double)")

// Profiles
MACRO_CONFIG_INT(TcProfileSkin, tc_profile_skin, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Include skin when saving/loading config preset")
MACRO_CONFIG_INT(TcProfileName, tc_profile_name, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Include name when saving/loading config preset")
MACRO_CONFIG_INT(TcProfileClan, tc_profile_clan, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Include clan when saving/loading config preset")
MACRO_CONFIG_INT(TcProfileFlag, tc_profile_flag, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Include country flag when saving/loading config preset")
MACRO_CONFIG_INT(TcProfileColors, tc_profile_colors, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Include color when saving/loading config preset")
MACRO_CONFIG_INT(TcProfileEmote, tc_profile_emote, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Include emoticon when saving/loading config preset")
MACRO_CONFIG_INT(TcProfileOverwriteClanWithEmpty, tc_profile_overwrite_clan_with_empty, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Override current clan even if preset clan name is empty")

// Rainbow
MACRO_CONFIG_INT(TcRainbowTees, tc_rainbow_tees, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable rainbow rendering for Tee")
MACRO_CONFIG_INT(TcRainbowHook, tc_rainbow_hook, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable rainbow rendering for hook")
MACRO_CONFIG_INT(TcRainbowWeapon, tc_rainbow_weapon, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable rainbow rendering for weapon")

MACRO_CONFIG_INT(TcRainbowOthers, tc_rainbow_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable rainbow rendering for other players' Tees")
MACRO_CONFIG_INT(TcRainbowMode, tc_rainbow_mode, 1, 1, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Rainbow mode (1=Rainbow, 2=Pulse, 3=Dark, 4=Random)")
MACRO_CONFIG_INT(TcRainbowSpeed, tc_rainbow_speed, 100, 0, 10000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Rainbow speed percent (50=half speed, 200=double)")

// War List
MACRO_CONFIG_INT(TcWarList, tc_warlist, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable visual markers for enemy list")
MACRO_CONFIG_INT(TcWarListShowClan, tc_warlist_show_clan_if_war, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show clan on nameplates for enemies")
MACRO_CONFIG_INT(TcWarListReason, tc_warlist_reason, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show enemy reason")
MACRO_CONFIG_INT(TcWarListChat, tc_warlist_chat, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show enemy color in chat")
MACRO_CONFIG_INT(TcWarListScoreboard, tc_warlist_scoreboard, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show enemy color in scoreboard")
MACRO_CONFIG_INT(TcWarListAllowDuplicates, tc_warlist_allow_duplicates, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Allow duplicate enemy entries")
MACRO_CONFIG_INT(TcWarListSpectate, tc_warlist_spectate, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show enemy color in spectator menu")

MACRO_CONFIG_INT(TcWarListIndicator, tc_warlist_indicator, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use enemy list to drive player indicators")
MACRO_CONFIG_INT(TcWarListIndicatorColors, tc_warlist_indicator_colors, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use enemy color instead of frozen color")
MACRO_CONFIG_INT(TcWarListIndicatorAll, tc_warlist_indicator_all, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show all enemy groups")
MACRO_CONFIG_INT(TcWarListIndicatorEnemy, tc_warlist_indicator_enemy, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show enemy group players")
MACRO_CONFIG_INT(TcWarListIndicatorTeam, tc_warlist_indicator_team, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show team group players")

// Status Bar
MACRO_CONFIG_INT(TcStatusBar, tc_statusbar, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable status bar")

MACRO_CONFIG_INT(TcStatusBar12HourClock, tc_statusbar_12_hour_clock, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use 12-hour clock for local time")
MACRO_CONFIG_INT(TcStatusBarLocalTimeSeconds, tc_statusbar_local_time_seconds, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show local time seconds")
MACRO_CONFIG_INT(TcStatusBarHeight, tc_statusbar_height, 8, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Status bar height")

MACRO_CONFIG_COL(TcStatusBarColor, tc_statusbar_color, 3221225472, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Status bar background color")
MACRO_CONFIG_COL(TcStatusBarTextColor, tc_statusbar_text_color, 4278190335, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Status bar text color")
MACRO_CONFIG_INT(TcStatusBarAlpha, tc_statusbar_alpha, 75, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Status bar background opacity")
MACRO_CONFIG_INT(TcStatusBarTextAlpha, tc_statusbar_text_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Status bar text opacity")

MACRO_CONFIG_INT(TcStatusBarLabels, tc_statusbar_labels, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show labels on status bar items")
MACRO_CONFIG_STR(TcStatusBarScheme, tc_statusbar_scheme, 129, "ac pf r", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Status bar item order")

// Trails
MACRO_CONFIG_INT(TcTeeTrail, tc_tee_trail, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable Tee trail")
MACRO_CONFIG_INT(TcTeeTrailOthers, tc_tee_trail_others, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Tee trail for others")
MACRO_CONFIG_INT(TcTeeTrailWidth, tc_tee_trail_width, 15, 0, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Trail width")
MACRO_CONFIG_INT(TcTeeTrailLength, tc_tee_trail_length, 25, 5, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Trail length")
MACRO_CONFIG_INT(TcTeeTrailAlpha, tc_tee_trail_alpha, 80, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Trail opacity")
MACRO_CONFIG_COL(TcTeeTrailColor, tc_tee_trail_color, 255, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Trail color")
MACRO_CONFIG_INT(TcTeeTrailTaper, tc_tee_trail_taper, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Taper trail ends")
MACRO_CONFIG_INT(TcTeeTrailFade, tc_tee_trail_fade, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Fade opacity along trail length")
MACRO_CONFIG_INT(TcTeeTrailColorMode, tc_tee_trail_color_mode, 1, 1, 5, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tee trail color mode (1=Solid, 2=Current Tee color, 3=Rainbow, 4=Tee speed, 5=Random)")
MACRO_CONFIG_INT(TcTeeTrailStyle, tc_tee_trail_style, 0, 0, 5, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tee trail style (0=Original, 1=Cursed flame, 2=Violet bolt, 3=Spirit light, 4=Void shadow, 5=Golden grace)")
MACRO_CONFIG_INT(TcTeeTrailStyleColors, tc_tee_trail_style_colors, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use the selected tee trail style palette")

// Chat Reply
MACRO_CONFIG_INT(TcAutoReplyMuted, tc_auto_reply_muted, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-reply to muted players")
MACRO_CONFIG_STR(TcAutoReplyMutedMessage, tc_auto_reply_muted_message, 128, "I have muted you", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reply to messages from muted players")
MACRO_CONFIG_INT(TcAutoReplyMinimized, tc_auto_reply_minimized, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-reply when game minimized")
MACRO_CONFIG_STR(TcAutoReplyMinimizedMessage, tc_auto_reply_minimized_message, 128, "I am not tabbed in", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Message to reply when game minimized")

// Voting
MACRO_CONFIG_INT(TcAutoVoteWhenFar, tc_auto_vote_when_far, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto vote on map change after min duration (0=Off, 1=Oppose, 2=Agree)")
MACRO_CONFIG_STR(TcAutoVoteWhenFarMessage, tc_auto_vote_when_far_message, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Message to send on auto vote, leave empty to not send")
MACRO_CONFIG_INT(TcAutoVoteWhenFarTime, tc_auto_vote_when_far_time, 5, 0, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Wait time before triggering auto vote")

// Font
MACRO_CONFIG_STR(TcCustomFont, tc_custom_font, 255, "DejaVu Sans", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Custom font")
MACRO_CONFIG_INT(TcCustomFontWeight, tc_custom_font_weight, 400, 100, 900, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Variable custom font weight")

// Bg Draw
MACRO_CONFIG_INT(TcBgDrawWidth, tc_bg_draw_width, 5, 1, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background stroke width")
MACRO_CONFIG_INT(TcBgDrawFadeTime, tc_bg_draw_fade_time, 0, 0, 600, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Stroke retention time (0=Never disappear)")
MACRO_CONFIG_INT(TcBgDrawMaxItems, tc_bg_draw_max_items, 128, 0, 2048, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Max stroke entries to keep")
MACRO_CONFIG_COL(TcBgDrawColor, tc_bg_draw_color, 14024576, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background stroke color")
MACRO_CONFIG_INT(TcBgDrawAutoSaveLoad, tc_bg_draw_auto_save_load, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto save and load background strokes")

// Animations
MACRO_CONFIG_INT(TcAnimateWheelTime, tc_animate_wheel_time, 350, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Emote and binding wheel animation duration (ms, 0=No animation, 1000=1 sec)")

// Pets
MACRO_CONFIG_INT(TcPetShow, tc_pet_show, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show pet")
MACRO_CONFIG_STR(TcPetSkin, tc_pet_skin, 24, "twinbop", CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_INSENSITIVE, "Pet skin")
MACRO_CONFIG_INT(TcPetSize, tc_pet_size, 60, 10, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Pet size relative to normal player")
MACRO_CONFIG_INT(TcPetAlpha, tc_pet_alpha, 90, 10, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Pet opacity (100=Opaque, 50=Semi-transparent)")

// Change name near finish
MACRO_CONFIG_INT(TcChangeNameNearFinish, tc_change_name_near_finish, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Try to change name near finish")
MACRO_CONFIG_STR(TcFinishName, tc_finish_name, 16, "nameless tee", CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_INSENSITIVE, "Name to change to near finish (when tc_change_name_near_finish enabled)")

// Flags
MACRO_CONFIG_INT(TcTClientSettingsTabs, tc_tclient_settings_tabs, 0, 0, 65536, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Bit flags for disabling settings tabs")

// Volleyball
MACRO_CONFIG_INT(TcVolleyBallBetterBall, tc_volleyball_better_ball, 1, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Make frozen players look more like volleyballs (0=Disabled, 1=Only volleyball maps, 2=Always)")
MACRO_CONFIG_STR(TcVolleyBallBetterBallSkin, tc_volleyball_better_ball_skin, 24, "beachball", CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_INSENSITIVE, "Player skin used for volleyball appearance")

// Mod
MACRO_CONFIG_INT(TcShowPlayerHitBoxes, tc_show_player_hit_boxes, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show player hitboxes (1=predicted only, 2=predicted and unpredicted)")

MACRO_CONFIG_INT(TcModWeapon, tc_mod_weapon, 0, 0, 1, CFGFLAG_CLIENT, "Command executed when pointing at someone and shooting (default kill, only works when remote console is authenticated)")
MACRO_CONFIG_STR(TcModWeaponCommand, tc_mod_weapon_command, 256, "rcon kill_pl", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Command executed by tc_mod_weapon, target id appended at end")

// Run on join
MACRO_CONFIG_STR(TcExecuteOnConnect, tc_execute_on_connect, 100, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "")
MACRO_CONFIG_STR(TcExecuteOnJoin, tc_execute_on_join, 100, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "")
MACRO_CONFIG_INT(TcExecuteOnJoinDelay, tc_execute_on_join_delay, 7, 7, 50000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tick delay before executing tc_execute_on_join")

// Custom Communities
MACRO_CONFIG_STR(TcCustomCommunitiesUrl, tc_custom_communities_url, 256, "https://raw.githubusercontent.com/SollyBunny/ddnet-custom-communities/refs/heads/main/custom-communities-ddnet-info.json", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Custom community list fetch URL (must be https, leave empty to disable)")

// Discord RPC
MACRO_CONFIG_INT(TcDiscordRPC, tc_discord_rpc, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable Discord RPC (requires restart)") // broken

// UI Settings
MACRO_CONFIG_INT(TcUiShowTClient, tc_ui_show_tclient, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show TClient config variables")
MACRO_CONFIG_INT(TcUiShowDDNet, tc_ui_show_ddnet, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show DDNet config variables")
MACRO_CONFIG_INT(TcUiShowQm, tc_ui_show_qm, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show QmClient config variables")
MACRO_CONFIG_INT(TcUiCompactList, tc_ui_compact_list, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use compact list view for config variables")
MACRO_CONFIG_INT(TcUiOnlyModified, tc_ui_only_modified, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show only modified config variables")
