// This file can be included several times.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(Tcme, ScriptName, Def, Min, Max, Save, Desc) ;
#define MACRO_CONFIG_COL(Tcme, ScriptName, Def, Save, Desc) ;
#define MACRO_CONFIG_STR(Tcme, ScriptName, Len, Def, Save, Desc) ;
#endif

#if defined(CONF_FAMILY_WINDOWS)
MACRO_CONFIG_INT(QmAllowAnyRes, qm_allow_any_res, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Allow arbitrary game resolution when scaling (may cause issues on Windows)")
#else
MACRO_CONFIG_INT(QmAllowAnyRes, qm_allow_any_res, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Allow arbitrary game resolution when scaling (may cause issues on Windows)")
#endif

MACRO_CONFIG_INT(QmShowChatClient, qm_show_chat_client, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show client-generated chat messages, e.g., echo")

MACRO_CONFIG_INT(QmShowFrozenText, qm_frozen_tees_text, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show number of frozen Tees in team (0=Off, 1=Show active Tee, 2=Show frozen Tee)")
MACRO_CONFIG_INT(QmShowFrozenHud, qm_frozen_tees_hud, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show frozen Tee HUD")
MACRO_CONFIG_INT(QmShowFrozenHudSkins, qm_frozen_tees_hud_skins, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use ninja or dark skin in frozen Tee HUD")

MACRO_CONFIG_INT(QmFrozenHudTeeSize, qm_frozen_tees_size, 15, 8, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tee icon size in frozen Tee HUD (default 15)")
MACRO_CONFIG_INT(QmFrozenMaxRows, qm_frozen_tees_max_rows, 1, 1, 6, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Max rows in frozen Tee HUD")
MACRO_CONFIG_INT(QmFrozenHudTeamOnly, qm_frozen_tees_only_inteam, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show frozen Tee HUD only in team")

MACRO_CONFIG_INT(QmNameplatePingCircle, qm_nameplate_ping_circle, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show ping ring on name plate")
MACRO_CONFIG_INT(QmNameplateCountry, qm_nameplate_country, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show flag on name plate")
MACRO_CONFIG_INT(QmNameplateSkins, qm_nameplate_skins, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show skin name on name plate to find missing skins")

MACRO_CONFIG_INT(QmFakeCtfFlags, qm_fake_ctf_flags, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show dummy CTF flags on players (0=Off, 1=Red flag, 2=Blue flag)")

MACRO_CONFIG_INT(QmLimitMouseToScreen, qm_limit_mouse_to_screen, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Clamp mouse to screen")
MACRO_CONFIG_INT(QmScaleMouseDistance, qm_scale_mouse_distance, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Scale max mouse distance to 1000 for better aim")

MACRO_CONFIG_INT(QmHammerRotatesWithCursor, qm_hammer_rotates_with_cursor, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Rotate hammer with cursor like other weapons")

MACRO_CONFIG_INT(QmMiniVoteHud, qm_mini_vote_hud, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Mini voting UI")

// Anti Latency Tools
MACRO_CONFIG_INT(QmRemoveAnti, qm_remove_anti, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reduce antiping and player prediction when frozen")
MACRO_CONFIG_INT(QmUnfreezeLagTicks, qm_remove_anti_ticks, 5, 0, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Max prediction ticks reduced")
MACRO_CONFIG_INT(QmUnfreezeLagDelayTicks, qm_remove_anti_delay_ticks, 25, 5, 150, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Ticks after freeze before applying max prediction reduction")

MACRO_CONFIG_INT(QmUnpredOthersInFreeze, qm_unpred_others_in_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Do not predict other players when frozen")
MACRO_CONFIG_INT(QmPredMarginInFreeze, qm_pred_margin_in_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable custom prediction margin when frozen")
MACRO_CONFIG_INT(QmPredMarginInFreezeAmount, qm_pred_margin_in_freeze_amount, 15, 0, 2000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Prediction margin when frozen")

MACRO_CONFIG_INT(QmShowOthersGhosts, qm_show_others_ghosts, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show ghost at unpredictable position of other players")
MACRO_CONFIG_INT(QmSwapGhosts, qm_swap_ghosts, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show predicted position as ghost, original as unpredictable player")
MACRO_CONFIG_INT(QmHideFrozenGhosts, qm_hide_frozen_ghosts, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide other players' ghosts when frozen")

MACRO_CONFIG_INT(QmPredGhostsAlpha, qm_pred_ghosts_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Prediction ghost opacity (0-100)")
MACRO_CONFIG_INT(QmUnpredGhostsAlpha, qm_unpred_ghosts_alpha, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Unpredictable ghost opacity (0-100)")
MACRO_CONFIG_INT(QmRenderGhostAsCircle, qm_render_ghost_as_circle, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render ghosts as circles instead of Tee")

MACRO_CONFIG_INT(QmShowCenter, qm_show_center, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Draw lines showing screen/clickbox center")
MACRO_CONFIG_INT(QmShowCenterWidth, qm_show_center_width, 0, 0, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Center line width (requires qm_show_center)")
MACRO_CONFIG_COL(QmShowCenterColor, qm_show_center_color, 1694498688, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Center line color (requires qm_show_center)") // transparent red

MACRO_CONFIG_INT(QmHookCollCursor, qm_hook_coll_cursor, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hook collision line length follows cursor distance")

MACRO_CONFIG_INT(QmFastInput, qm_fast_input, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply input early before next tick for prediction")
MACRO_CONFIG_INT(QmFastInputAmount, qm_fast_input_amount, 20, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Milliseconds to apply quick input early")
MACRO_CONFIG_INT(QmFastInputOthers, qm_fast_input_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply quick input to other Tees as well")

MACRO_CONFIG_INT(QmAntiPingImproved, qm_antiping_improved, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use alternative antiping smoothing, incompatible with cl_antiping_smooth")
MACRO_CONFIG_INT(QmAntiPingNegativeBuffer, qm_antiping_negative_buffer, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Helpful for Gores: allow negative internal certainty for more conservative prediction")
MACRO_CONFIG_INT(QmAntiPingStableDirection, qm_antiping_stable_direction, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "More optimistic prediction along Tee's stable direction, reduces delay to stabilize")
MACRO_CONFIG_INT(QmAntiPingUncertaintyScale, qm_antiping_uncertainty_scale, 150, 25, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Scale uncertainty duration by ping (100=1.0x)")

MACRO_CONFIG_INT(QmColorFreeze, qm_color_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use skin color when Tee is frozen")
MACRO_CONFIG_INT(QmColorFreezeDarken, qm_color_freeze_darken, 90, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Darken Tee color when frozen (0-100)")
MACRO_CONFIG_INT(QmColorFreezeFeet, qm_color_freeze_feet, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use skin color for frozen Tee's feet")

// Revert Variables
MACRO_CONFIG_INT(QmSmoothPredictionMargin, qm_prediction_margin_smooth, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Smooth prediction margin (weakens ping jitter correction, restores old behavior)")
MACRO_CONFIG_INT(QmFreezeKatana, qm_frozen_katana, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show katana on frozen players (restores old behavior)")
MACRO_CONFIG_INT(QmOldTeamColors, qm_old_team_colors, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use old rainbow team colors (restores old behavior)")
MACRO_CONFIG_INT(QmRevertHookLine, qm_revert_hook_line, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Restore old single-segment hook collision line behavior")

// Freeze Auto Emoticon and Chat
MACRO_CONFIG_INT(QmFreezeChatEnabled, qm_freeze_chat_enabled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-send emote and chat message when entering freeze")
MACRO_CONFIG_INT(QmFreezeChatEmoticon, qm_freeze_chat_emoticon, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Send emote when entering freeze")
MACRO_CONFIG_INT(QmFreezeChatEmoticonId, qm_freeze_chat_emoticon_id, 7, 0, 15, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Emote ID to send when entering freeze (0-15)")
MACRO_CONFIG_STR(QmFreezeChatMessage, qm_freeze_chat_message, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat messages to send when entering freeze, comma-separated (empty=none)")
MACRO_CONFIG_INT(QmFreezeChatChance, qm_freeze_chat_chance, 30, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Probability of sending freeze chat message (0-100%)")

// Outline Variables
MACRO_CONFIG_INT(QmOutline, qm_outline, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable tile outlines")
MACRO_CONFIG_INT(QmOutlineEntities, qm_outline_in_entities, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show outlines only on entity layer")
MACRO_CONFIG_INT(QmOutlineAlpha, qm_outline_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Outline global opacity")
MACRO_CONFIG_INT(QmOutlineSolidAlpha, qm_outline_solid_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Entity wall outline opacity")

MACRO_CONFIG_INT(QmOutlineSolid, qm_outline_solid, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show hookable/unhookable tile outlines")
MACRO_CONFIG_INT(QmOutlineFreeze, qm_outline_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show freeze/deep freeze tile outlines")
MACRO_CONFIG_INT(QmOutlineUnfreeze, qm_outline_unfreeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show unfreeze/deep unfreeze tile outlines")
MACRO_CONFIG_INT(QmOutlineKill, qm_outline_kill, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show death tile outlines")
MACRO_CONFIG_INT(QmOutlineTele, qm_outline_tele, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show teleporter tile outlines")

MACRO_CONFIG_INT(QmOutlineWidthSolid, qm_outline_width_solid, 2, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hookable/unhookable tile outline width")
MACRO_CONFIG_INT(QmOutlineWidthFreeze, qm_outline_width_freeze, 2, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Freeze/Deep freeze tile outline width")
MACRO_CONFIG_INT(QmOutlineWidthUnfreeze, qm_outline_width_unfreeze, 2, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Unfreeze/Deep unfreeze tile outline width")
MACRO_CONFIG_INT(QmOutlineWidthKill, qm_outline_width_kill, 2, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Death tile outline width")
MACRO_CONFIG_INT(QmOutlineWidthTele, qm_outline_width_tele, 2, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Teleport tile outline width")

MACRO_CONFIG_COL(QmOutlineColorSolid, qm_outline_color_solid, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Hookable/Unhookable tile outline color") // 255 0 0 0
MACRO_CONFIG_COL(QmOutlineColorFreeze, qm_outline_color_freeze, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Freeze tile outline color") // 255 0 0 0
MACRO_CONFIG_COL(QmOutlineColorDeepFreeze, qm_outline_color_deep_freeze, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Deep freeze tile outline color") // 255 0 0 0
MACRO_CONFIG_COL(QmOutlineColorUnfreeze, qm_outline_color_unfreeze, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Unfreeze tile outline color") // 255 0 0 0
MACRO_CONFIG_COL(QmOutlineColorDeepUnfreeze, qm_outline_color_deep_unfreeze, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Deep unfreeze tile outline color") // 255 0 0 0
MACRO_CONFIG_COL(QmOutlineColorKill, qm_outline_color_kill, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Death tile outline color") // 0 0 0
MACRO_CONFIG_COL(QmOutlineColorTele, qm_outline_color_tele, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Teleport tile outline color") // 255 0 0 0

// Indicator Variables
MACRO_CONFIG_COL(QmIndicatorAlive, qm_indicator_alive, 255, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Player indicator alive Tee color")
MACRO_CONFIG_COL(QmIndicatorFreeze, qm_indicator_freeze, 65407, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Player indicator frozen Tee color")
MACRO_CONFIG_COL(QmIndicatorSaved, qm_indicator_dead, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Player indicator unfreezing Tee color")
MACRO_CONFIG_INT(QmIndicatorOffset, qm_indicator_offset, 42, 16, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Indicator offset distance")
MACRO_CONFIG_INT(QmIndicatorOffsetMax, qm_indicator_offset_max, 100, 16, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Max offset distance for variable offset")
MACRO_CONFIG_INT(QmIndicatorVariableDistance, qm_indicator_variable_distance, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Indicator moves farther away as distance increases")
MACRO_CONFIG_INT(QmIndicatorMaxDistance, qm_indicator_variable_max_distance, 1000, 500, 7000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Max distance for variable offset calculation")
MACRO_CONFIG_INT(QmIndicatorRadius, qm_indicator_radius, 4, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Indicator radius")
MACRO_CONFIG_INT(QmIndicatorOpacity, qm_indicator_opacity, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Indicator transparency")
MACRO_CONFIG_INT(QmPlayerIndicator, qm_player_indicator, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show radial indicators of other Tees")
MACRO_CONFIG_INT(QmPlayerIndicatorFreeze, qm_player_indicator_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show only frozen Tees in indicators")
MACRO_CONFIG_INT(QmIndicatorTeamOnly, qm_indicator_inteam, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show indicators only within team")
MACRO_CONFIG_INT(QmIndicatorTees, qm_indicator_tees, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use Tee icons instead of circles")
MACRO_CONFIG_INT(QmIndicatorHideVisible, qm_indicator_hide_visible_tees, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Do not show Tees visible on screen")

// Bind Wheel
MACRO_CONFIG_INT(QmResetBindWheelMouse, qm_reset_bindwheel_mouse, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reset mouse position when opening bind wheel")

// Regex chat matching
MACRO_CONFIG_STR(QmRegexChatIgnore, qm_regex_chat_ignore, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Filter chat messages by regular expression")

// Misc visual
MACRO_CONFIG_INT(QmWhiteFeet, qm_white_feet, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render all feet as solid white base")
MACRO_CONFIG_STR(QmWhiteFeetSkin, qm_white_feet_skin, 255, "x_ninja", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Skin used for white feet base")
MACRO_CONFIG_INT(QmMovingTilesEntities, qm_moving_tiles_entities, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show moving tiles in entity layer")

MACRO_CONFIG_INT(QmMiniDebug, qm_mini_debug, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show position and angle debug info")

MACRO_CONFIG_INT(QmShowhudDummyPosition, qm_showhud_dummy_position, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show dummy position in movement info HUD")
MACRO_CONFIG_INT(QmShowhudDummySpeed, qm_showhud_dummy_speed, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show dummy velocity in movement info HUD")
MACRO_CONFIG_INT(QmShowhudDummyAngle, qm_showhud_dummy_angle, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show dummy angle in movement info HUD")
MACRO_CONFIG_INT(QmShowLocalTimeSeconds, qm_show_local_time_seconds, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Include seconds in local time display")

MACRO_CONFIG_INT(QmNotifyWhenLast, qm_last_notify, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show notification when only one Tee alive")
MACRO_CONFIG_STR(QmNotifyWhenLastText, qm_last_notify_text, 64, "Last!", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Alive notification text")
MACRO_CONFIG_COL(QmNotifyWhenLastColor, qm_last_notify_color, 256, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Alive notification color")
MACRO_CONFIG_INT(QmNotifyWhenLastX, qm_last_notify_x, 20, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Alive notification horizontal position (percentage of screen width)")
MACRO_CONFIG_INT(QmNotifyWhenLastY, qm_last_notify_y, 1, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Alive notification vertical position (percentage of screen height)")
MACRO_CONFIG_INT(QmNotifyWhenLastSize, qm_last_notify_size, 10, 0, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Alive notification font size")

MACRO_CONFIG_INT(QmRenderCursorSpec, qm_cursor_in_spec, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render crosshair in free spectate mode")
MACRO_CONFIG_INT(QmRenderCursorSpecAlpha, qm_cursor_in_spec_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Crosshair opacity in free spectate")

// MACRO_CONFIG_INT(QmRenderNameplateSpec, qm_render_nameplate_spec, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "旁观时渲染名字板")

MACRO_CONFIG_INT(QmTinyTees, qm_tiny_tees, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Scale down Tee")
MACRO_CONFIG_INT(QmTinyTeeSize, qm_indicator_tees_size, 100, 85, 115, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Small Tee size")
MACRO_CONFIG_INT(QmTinyTeesOthers, qm_tiny_tees_others, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Scale down other Tees as well")

MACRO_CONFIG_INT(QmCursorScale, qm_cursor_scale, 100, 0, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "In-game crosshair scale percent (50=half, 200=double)")

// Profiles
MACRO_CONFIG_INT(QmProfileSkin, qm_profile_skin, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Include skin when saving/loading config preset")
MACRO_CONFIG_INT(QmProfileName, qm_profile_name, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Include name when saving/loading config preset")
MACRO_CONFIG_INT(QmProfileClan, qm_profile_clan, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Include clan when saving/loading config preset")
MACRO_CONFIG_INT(QmProfileFlag, qm_profile_flag, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Include country flag when saving/loading config preset")
MACRO_CONFIG_INT(QmProfileColors, qm_profile_colors, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Include color when saving/loading config preset")
MACRO_CONFIG_INT(QmProfileEmote, qm_profile_emote, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Include emoticon when saving/loading config preset")
MACRO_CONFIG_INT(QmProfileOverwriteClanWithEmpty, qm_profile_overwrite_clan_with_empty, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Override current clan even if preset clan name is empty")

// Rainbow
MACRO_CONFIG_INT(QmRainbowTees, qm_rainbow_tees, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable rainbow rendering for Tee")
MACRO_CONFIG_INT(QmRainbowHook, qm_rainbow_hook, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable rainbow rendering for hook")
MACRO_CONFIG_INT(QmRainbowWeapon, qm_rainbow_weapon, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable rainbow rendering for weapon")

MACRO_CONFIG_INT(QmRainbowOthers, qm_rainbow_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable rainbow rendering for other players' Tees")
MACRO_CONFIG_INT(QmRainbowMode, qm_rainbow_mode, 1, 1, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Rainbow mode (1=Rainbow, 2=Pulse, 3=Dark, 4=Random)")
MACRO_CONFIG_INT(QmRainbowSpeed, qm_rainbow_speed, 100, 0, 10000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Rainbow speed percent (50=half speed, 200=double)")

// War List
MACRO_CONFIG_INT(QmWarList, qm_warlist, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable visual markers for enemy list")
MACRO_CONFIG_INT(QmWarListShowClan, qm_warlist_show_clan_if_war, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show clan on nameplates for enemies")
MACRO_CONFIG_INT(QmWarListReason, qm_warlist_reason, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show enemy reason")
MACRO_CONFIG_INT(QmWarListChat, qm_warlist_chat, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show enemy color in chat")
MACRO_CONFIG_INT(QmWarListScoreboard, qm_warlist_scoreboard, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show enemy color in scoreboard")
MACRO_CONFIG_INT(QmWarListAllowDuplicates, qm_warlist_allow_duplicates, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Allow duplicate enemy entries")
MACRO_CONFIG_INT(QmWarListSpectate, qm_warlist_spectate, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show enemy color in spectator menu")

MACRO_CONFIG_INT(QmWarListIndicator, qm_warlist_indicator, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use enemy list to drive player indicators")
MACRO_CONFIG_INT(QmWarListIndicatorColors, qm_warlist_indicator_colors, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use enemy color instead of frozen color")
MACRO_CONFIG_INT(QmWarListIndicatorAll, qm_warlist_indicator_all, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show all enemy groups")
MACRO_CONFIG_INT(QmWarListIndicatorEnemy, qm_warlist_indicator_enemy, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show enemy group players")
MACRO_CONFIG_INT(QmWarListIndicatorTeam, qm_warlist_indicator_team, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show team group players")

// Status Bar
MACRO_CONFIG_INT(QmStatusBar, qm_statusbar, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable status bar")

MACRO_CONFIG_INT(QmStatusBar12HourClock, qm_statusbar_12_hour_clock, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use 12-hour clock for local time")
MACRO_CONFIG_INT(QmStatusBarLocalTimeSeconds, qm_statusbar_local_time_seconds, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show local time seconds")
MACRO_CONFIG_INT(QmStatusBarHeight, qm_statusbar_height, 8, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Status bar height")

MACRO_CONFIG_COL(QmStatusBarColor, qm_statusbar_color, 3221225472, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Status bar background color")
MACRO_CONFIG_COL(QmStatusBarTextColor, qm_statusbar_text_color, 4278190335, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Status bar text color")
MACRO_CONFIG_INT(QmStatusBarAlpha, qm_statusbar_alpha, 75, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Status bar background opacity")
MACRO_CONFIG_INT(QmStatusBarTextAlpha, qm_statusbar_text_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Status bar text opacity")

MACRO_CONFIG_INT(QmStatusBarLabels, qm_statusbar_labels, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show labels on status bar items")
MACRO_CONFIG_STR(QmStatusBarScheme, qm_statusbar_scheme, 129, "ac pf r", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Status bar item order")

// Trails
MACRO_CONFIG_INT(QmTeeTrail, qm_tee_trail, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable Tee trail")
MACRO_CONFIG_INT(QmTeeTrailOthers, qm_tee_trail_others, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Tee trail for others")
MACRO_CONFIG_INT(QmTeeTrailWidth, qm_tee_trail_width, 15, 0, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Trail width")
MACRO_CONFIG_INT(QmTeeTrailLength, qm_tee_trail_length, 25, 5, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Trail length")
MACRO_CONFIG_INT(QmTeeTrailAlpha, qm_tee_trail_alpha, 80, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Trail opacity")
MACRO_CONFIG_COL(QmTeeTrailColor, qm_tee_trail_color, 255, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Trail color")
MACRO_CONFIG_INT(QmTeeTrailTaper, qm_tee_trail_taper, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Taper trail ends")
MACRO_CONFIG_INT(QmTeeTrailFade, qm_tee_trail_fade, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Fade opacity along trail length")
MACRO_CONFIG_INT(QmTeeTrailColorMode, qm_tee_trail_color_mode, 1, 1, 5, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tee trail color mode (1=Solid, 2=Current Tee color, 3=Rainbow, 4=Tee speed, 5=Random)")
// 继续接受旧配置的 4、5，实际渲染和菜单统一解析为漫画、魔法。
MACRO_CONFIG_INT(QmTeeTrailStyle, qm_tee_trail_style, 0, 0, 5, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tee trail style (0=Original, 1=Manga, 2=Magic, 3=Pixel, 4=Legacy manga, 5=Legacy magic)")
MACRO_CONFIG_INT(QmTeeTrailStyleColors, qm_tee_trail_style_colors, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use the selected tee trail style palette")

// Chat Reply
MACRO_CONFIG_INT(QmAutoReplyMuted, qm_auto_reply_muted, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-reply to muted players")
MACRO_CONFIG_STR(QmAutoReplyMutedMessage, qm_auto_reply_muted_message, 128, "I have muted you", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reply to messages from muted players")
MACRO_CONFIG_INT(QmAutoReplyMinimized, qm_auto_reply_minimized, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-reply when game minimized")
MACRO_CONFIG_STR(QmAutoReplyMinimizedMessage, qm_auto_reply_minimized_message, 128, "I am not tabbed in", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Message to reply when game minimized")

// Voting
MACRO_CONFIG_INT(QmAutoVoteWhenFar, qm_auto_vote_when_far, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto vote on map change after min duration (0=Off, 1=Oppose, 2=Agree)")
MACRO_CONFIG_STR(QmAutoVoteWhenFarMessage, qm_auto_vote_when_far_message, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Message to send on auto vote, leave empty to not send")
MACRO_CONFIG_INT(QmAutoVoteWhenFarTime, qm_auto_vote_when_far_time, 5, 0, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Wait time before triggering auto vote")

// Font
MACRO_CONFIG_STR(QmCustomFont, qm_custom_font, 255, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Custom font for Latin text (family, or family + style)")
// 字重有效范围按所选可变字体的 wght 轴钳制（轴上限可达 1000），此处只做安全网。
MACRO_CONFIG_INT(QmCustomFontWeight, qm_custom_font_weight, 400, 1, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Variable custom font weight for Latin text")
MACRO_CONFIG_STR(QmCustomFontCjk, qm_custom_font_cjk, 255, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Custom font for Chinese/CJK text (family, or family + style); empty = follow the Latin custom font")
MACRO_CONFIG_INT(QmCustomFontWeightCjk, qm_custom_font_weight_cjk, 400, 1, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Variable custom font weight for Chinese/CJK text")
MACRO_CONFIG_STR(QmCustomFontIcons, qm_custom_font_icons, 255, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Custom font for icon/symbol glyphs; empty = follow CJK or Latin font")

// Bg Draw
MACRO_CONFIG_INT(QmBgDrawWidth, qm_bg_draw_width, 5, 1, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background stroke width")
MACRO_CONFIG_INT(QmBgDrawFadeTime, qm_bg_draw_fade_time, 0, 0, 600, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Stroke retention time (0=Never disappear)")
MACRO_CONFIG_INT(QmBgDrawMaxItems, qm_bg_draw_max_items, 128, 0, 2048, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Max stroke entries to keep")
MACRO_CONFIG_COL(QmBgDrawColor, qm_bg_draw_color, 14024576, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background stroke color")
MACRO_CONFIG_INT(QmBgDrawAutoSaveLoad, qm_bg_draw_auto_save_load, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto save and load background strokes")

// Animations
MACRO_CONFIG_INT(QmAnimateWheelTime, qm_animate_wheel_time, 350, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Emote and binding wheel animation duration (ms, 0=No animation, 1000=1 sec)")

// Pets
MACRO_CONFIG_INT(QmPetShow, qm_pet_show, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show pet")
MACRO_CONFIG_STR(QmPetSkin, qm_pet_skin, 24, "twinbop", CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_INSENSITIVE, "Pet skin")
MACRO_CONFIG_INT(QmPetSize, qm_pet_size, 60, 10, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Pet size relative to normal player")
MACRO_CONFIG_INT(QmPetAlpha, qm_pet_alpha, 90, 10, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Pet opacity (100=Opaque, 50=Semi-transparent)")

// Change name near finish
MACRO_CONFIG_INT(QmChangeNameNearFinish, qm_change_name_near_finish, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Try to change name near finish")
MACRO_CONFIG_STR(QmFinishName, qm_finish_name, 16, "nameless tee", CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_INSENSITIVE, "Name to change to near finish (when qm_change_name_near_finish enabled)")

// Volleyball
MACRO_CONFIG_INT(QmVolleyBallBetterBall, qm_volleyball_better_ball, 1, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Make frozen players look more like volleyballs (0=Disabled, 1=Only volleyball maps, 2=Always)")
MACRO_CONFIG_STR(QmVolleyBallBetterBallSkin, qm_volleyball_better_ball_skin, 24, "beachball", CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_INSENSITIVE, "Player skin used for volleyball appearance")

// Mod
MACRO_CONFIG_INT(QmShowPlayerHitBoxes, qm_show_player_hit_boxes, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show player hitboxes (1=predicted only, 2=predicted and unpredicted)")

MACRO_CONFIG_INT(QmModWeapon, qm_mod_weapon, 0, 0, 1, CFGFLAG_CLIENT, "Command executed when pointing at someone and shooting (default kill, only works when remote console is authenticated)")
MACRO_CONFIG_STR(QmModWeaponCommand, qm_mod_weapon_command, 256, "rcon kill_pl", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Command executed by qm_mod_weapon, target id appended at end")

// Run on join
MACRO_CONFIG_STR(QmExecuteOnConnect, qm_execute_on_connect, 100, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "")
MACRO_CONFIG_STR(QmExecuteOnJoin, qm_execute_on_join, 100, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "")
MACRO_CONFIG_INT(QmExecuteOnJoinDelay, qm_execute_on_join_delay, 7, 7, 50000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tick delay before executing qm_execute_on_join")

// Custom Communities
MACRO_CONFIG_STR(QmCustomCommunitiesUrl, qm_custom_communities_url, 256, "https://raw.githubusercontent.com/SollyBunny/ddnet-custom-communities/refs/heads/main/custom-communities-ddnet-info.json", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Custom community list fetch URL (must be https, leave empty to disable)")

// Discord RPC
MACRO_CONFIG_INT(QmDiscordRPC, qm_discord_rpc, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable Discord RPC (requires restart)") // broken

// UI Settings
MACRO_CONFIG_INT(QmUiShowTClient, qm_ui_show_tclient, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show TClient config variables")
MACRO_CONFIG_INT(QmUiShowDDNet, qm_ui_show_ddnet, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show DDNet config variables")
MACRO_CONFIG_INT(QmUiShowQm, qm_ui_show_qm, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show QmClient config variables")
MACRO_CONFIG_INT(QmUiCompactList, qm_ui_compact_list, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use compact list view for config variables")
MACRO_CONFIG_INT(QmUiOnlyModified, qm_ui_only_modified, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show only modified config variables")
