// 请抬头享受阳光｜日子很好 我很我---------致咩子
// This file can be included several times.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(ConfigName, ScriptName, Def, Min, Max, Save, Desc) ;
#define MACRO_CONFIG_COL(ConfigName, ScriptName, Def, Save, Desc) ;
#define MACRO_CONFIG_STR(ConfigName, ScriptName, Len, Def, Save, Desc) ;
#endif

// QmClient specific variables - 栖梦客户端配置项

// Log / 日志
MACRO_CONFIG_INT(QmSteamAutoLaunch, qm_steam_auto_launch, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically launch Steam when the client is started externally")
MACRO_CONFIG_INT(QmConsoleFilterMask, qm_console_filter_mask, 15, 0, 15, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Local console log category filter mask (bit flags)")
MACRO_CONFIG_INT(QmPerfDebug, qm_perf_debug, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable main thread and render stage performance debug logging")
MACRO_CONFIG_INT(QmPerfLogfile, qm_perf_logfile, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Write performance debug logs to dedicated file")
MACRO_CONFIG_INT(QmPerfDebugThresholdMs, qm_perf_debug_threshold_ms, 4, 1, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Performance debug log threshold (ms)")
MACRO_CONFIG_INT(QmPerfStutterDiagnostics, qm_perf_stutter_diagnostics, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable client stutter diagnostics at startup")
MACRO_CONFIG_INT(QmGraphicsTrace, qm_graphics_trace, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Graphics trace level: 0=off, 1=periodic summary, 2=summary and slow frames, 3=detailed backend trace")
MACRO_CONFIG_INT(QmMacosGraphicsDiagnostics, qm_macos_graphics_diagnostics, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Deprecated compatibility alias for qm_graphics_trace (macOS signposts)")
MACRO_CONFIG_INT(QmGraphicsMode, qm_graphics_mode, 1, -1, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Graphics mode: -1=use backend setting, 0=compatibility, 1=performance (default)")
MACRO_CONFIG_INT(QmVulkanApiVersion, qm_vulkan_api_version, 14, 11, 14, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Vulkan API version: 14=prefer 1.4 then 1.3 then 1.1, 13=prefer 1.3 then 1.1, 11=force 1.1")
MACRO_CONFIG_INT(QmNetQos, qm_net_qos, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Prioritize outgoing game traffic on Windows (best effort)")
MACRO_CONFIG_INT(QmAssetsPreviewBudgetMbOverride, qm_assets_preview_budget_mb_override, 0, 0, 16384, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Resource preview VRAM budget override (MB, 0=auto)")
MACRO_CONFIG_INT(QmAssetsPreviewBudgetPercent, qm_assets_preview_budget_percent, 8, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Resource preview VRAM budget percentage (based on device local VRAM budget)")
MACRO_CONFIG_INT(QmUiRuntimeV2Debug, qm_ui_runtime_v2_debug, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable UI runtime v2 debug logging")
MACRO_CONFIG_INT(QmUiMotionLevel, qm_ui_motion_level, 2, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "QmUi animation intensity: 0=Off, 1=Reduced, 2=Full")
MACRO_CONFIG_INT(QmUiScale, qm_ui_scale, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Global UI size percentage")
MACRO_CONFIG_INT(QmUiListEntryAnimations, qm_ui_list_entry_animations, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Animate lists when entering a page")
MACRO_CONFIG_INT(QmUiCardHeightAnimations, qm_ui_card_height_animations, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Animate settings card expand and collapse height changes")
MACRO_CONFIG_INT(QmUiCardReflowAnimations, qm_ui_card_reflow_animations, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Animate settings card reorder and layout reflow")
MACRO_CONFIG_INT(QmExtraAnimations, qm_extra_animations, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Animate chat box, emote selector, scoreboard, and spectate selection")
MACRO_CONFIG_INT(QmUiCardRainbowTitles, qm_ui_card_rainbow_titles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Settings card titles use rainbow colors")
MACRO_CONFIG_INT(QmUiCardBorders, qm_ui_card_borders, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show settings card borders")
MACRO_CONFIG_COL(QmUiCardBorderColor, qm_ui_card_border_color, 0x1AFFFFFF, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Settings card border color")
MACRO_CONFIG_COL(QmUiCardColor, qm_ui_card_color, 0x000000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Settings card background color")
MACRO_CONFIG_INT(QmUiCardOpacity, qm_ui_card_opacity, 30, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Settings card background transparency")
MACRO_CONFIG_INT(QmUiIconColor, qm_ui_icon_color, 1, 1, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Qm UI icon color: 1=White, 2=Black, 3=Custom, 4=Rainbow")
MACRO_CONFIG_COL(QmUiIconCustomColor, qm_ui_icon_custom_color, 0xFFFFFF, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Qm UI custom icon color")
MACRO_CONFIG_COL(QmUiIconDuotoneSecondaryColor, qm_ui_icon_duotone_secondary_color, 0xFFFFFF, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Qm UI duotone secondary icon color")
MACRO_CONFIG_INT(QmUiIconWeight, qm_ui_icon_weight, 1, 0, 5, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Qm UI icon style: 0=Regular, 1=Bold, 2=Thin, 3=Fill, 4=Light, 5=Duotone")
MACRO_CONFIG_INT(QmUiColorInterpolation, qm_ui_color_interpolation, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "QmUi color animation interpolation: 0=sRGB linear, 1=OKLAB perceptually uniform")
MACRO_CONFIG_INT(QmRectCornerSegments, qm_rect_corner_segments, 16, 8, 48, CFGFLAG_CLIENT | CFGFLAG_SAVE, "UI rounded corner segments (even numbers recommended)")
MACRO_CONFIG_STR(QmGlobalCardOrder, qm_global_card_order, 8000, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Global card ordering (format: stableId|tab|col|order; semicolon-separated)")
MACRO_CONFIG_INT(QmCardOrderMigrated, qm_card_order_migrated, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Global card ordering old config migration completed flag")
MACRO_CONFIG_INT(QmCardLayoutVersion, qm_card_layout_version, 0, 0, 9, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Settings card default layout migration version")
MACRO_CONFIG_STR(QmSettingsCardOrder, qm_settings_card_order, 2048, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tclient settings card ordering (format: id:col:order; semicolon-separated)")
MACRO_CONFIG_INT(DbgQmUiDogfood, dbg_qm_ui_dogfood, 0, 0, 1, CFGFLAG_CLIENT, "Show feat-003 shared UI primitives dogfood page (takes over QmClient settings page, for visual verification of 11 primitives + spring/easing animations)")
MACRO_CONFIG_INT(QmNewUi, qm_new_ui, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable new settings page UI")
MACRO_CONFIG_COL(QmUiColor, qm_ui_color, 0x000000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Interface surface color")
MACRO_CONFIG_COL(QmUiFocusColor, qm_ui_focus_color, 0x97FFA6, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keyboard focus ring color")
MACRO_CONFIG_COL(QmUiAccentColor, qm_ui_accent_color, 0x8FDDAD, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Interface accent color")
MACRO_CONFIG_COL(QmUiSelectedColor, qm_ui_selected_color, 0x8FDDAD, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Selected item color")
MACRO_CONFIG_COL(QmMapBrowserColor, qm_map_browser_color, 0x000000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Map browser surface color")
MACRO_CONFIG_COL(QmScoreboardColor, qm_scoreboard_color, 0x000000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Scoreboard surface color")
MACRO_CONFIG_INT(QmUiOpacity, qm_ui_opacity, 30, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Interface transparency")
MACRO_CONFIG_INT(QmMapBrowserOpacity, qm_map_browser_opacity, 30, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Map browser transparency")
MACRO_CONFIG_INT(QmScoreboardOpacity, qm_scoreboard_opacity, 30, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Scoreboard transparency")
MACRO_CONFIG_INT(QmShowOutdatedVersionWarning, qm_show_outdated_version_warning, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show outdated version warning")
MACRO_CONFIG_INT(QmAutoUpdate, qm_auto_update, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically check for stable updates and install them on exit")
MACRO_CONFIG_INT(QmImeAutoManage, qm_ime_auto_manage, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto enable/disable IME on text focus")
MACRO_CONFIG_INT(QmNewIme, qm_new_ime, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable new IME candidate bar")
MACRO_CONFIG_INT(QmAutoSaveHistoryCount, qm_auto_save_history_count, 100, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-save history count (0=disable)")
MACRO_CONFIG_INT(QmShortServerNames, qm_short_server_names, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show short server names in browser")
MACRO_CONFIG_INT(QmPingCacheMaxAgeHours, qm_ping_cache_max_age_hours, 72, 0, 8760, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Discard cached server pings older than this many hours (0 = never expire)")
MACRO_CONFIG_INT(QmSkinSortMode, qm_skin_sort_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Skin list sort mode (0=Name, 1=Release date)")
MACRO_CONFIG_INT(QmSkinShowMetadata, qm_skin_show_metadata, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show skin release date and author")
MACRO_CONFIG_INT(QmSkinOutlineLocal, qm_skin_outline_local, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show generated skin outline for local tees")
MACRO_CONFIG_INT(QmSkinOutlineOthers, qm_skin_outline_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show generated skin outline for other tees")
MACRO_CONFIG_COL(QmSkinOutlineColor, qm_skin_outline_color, 0xFFFFFFFF, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Generated skin outline color")
MACRO_CONFIG_INT(QmSkinOutlineWidth, qm_skin_outline_width, 2, 1, 6, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Generated skin outline width")
MACRO_CONFIG_INT(QmSkinOutlineAlpha, qm_skin_outline_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Generated skin outline opacity")

// Report / 举报
MACRO_CONFIG_STR(QmReportEndpoint, qm_report_endpoint, 128, "http://124.222.146.111:8790", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Report service URL")
MACRO_CONFIG_STR(QmReportAppId, qm_report_app_id, 128, "desktop", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Report service App ID")
MACRO_CONFIG_STR(QmReportSecret, qm_report_secret, 128, "SsF-7wLdC9dO-RCb5sGieLII9gVW0v5lPpiK6zitUNo", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Report service signing key")

// UI / 界面
MACRO_CONFIG_INT(QmGaussianBlur, qm_gaussian_blur, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable backdrop blur for translucent interface and selected HUD backgrounds")
MACRO_CONFIG_INT(QmBlurMode, qm_blur_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Backdrop blur algorithm: 0=Gaussian, 1=Kawase, 2=Dual Kawase")

// QmVulkan 扩展总开关：0=关（纯净 Vulkan + 几何/CPU 兜底），1=自动（失败/设备丢失回退），2=强制开
MACRO_CONFIG_INT(QmEnhancedRendering, qm_enhanced_rendering, 1, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Qm enhanced rendering: 0=Off pure Vulkan, 1=Auto fallback, 2=Force on")
MACRO_CONFIG_INT(QmEnhancedSdf, qm_enhanced_sdf, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use SDF pipelines for Dynamic Island / rounded rects when enhanced rendering is active")
MACRO_CONFIG_INT(QmEnhancedBlur, qm_enhanced_blur, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use Gaussian blur pipeline when enhanced rendering is active")
MACRO_CONFIG_INT(QmEnhancedMsdf, qm_enhanced_msdf, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use MSDF icon pipeline when enhanced rendering is active")

// Sponsor nudge / 赞助提醒
MACRO_CONFIG_INT(QmLaunchCount, qm_launch_count, 0, 0, 1000000000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Accumulated client launch count (used by the sponsor reminder)")
MACRO_CONFIG_INT(QmSponsorNudgeAt, qm_sponsor_nudge_at, 14, 1, 100000000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Launch count threshold for the next sponsor reminder")
MACRO_CONFIG_INT(QmSponsorNudge, qm_sponsor_nudge, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show the occasional sponsor reminder in the main menu")

// Scoreboard / 计分板
MACRO_CONFIG_INT(QmScoreboardPoints, qm_scoreboard_points, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Scoreboard score lookup")
MACRO_CONFIG_INT(QmScoreboardSortMode, qm_scoreboard_sort_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Scoreboard sort mode (0=Score, 1=Points)")
MACRO_CONFIG_INT(QmScoreboardOnDeath, qm_scoreboard_on_death, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show scoreboard after death")
MACRO_CONFIG_INT(QmScoreboardScroll, qm_scoreboard_scroll, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Fixed-size scoreboard rows with mouse wheel scrolling for crowded servers")
MACRO_CONFIG_STR(QmScoreboardFilter, qm_scoreboard_filter, 32, "", CFGFLAG_CLIENT, "Scoreboard filter: only show players whose name or clan contains this text")
MACRO_CONFIG_INT(QmBetterScoreboard, qm_better_scoreboard, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable enhanced scoreboard presentation")
MACRO_CONFIG_INT(QmHideJoinServerInfo, qm_hide_join_server_info, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Do not auto-show server info on entering map")

// Chat / 聊天
MACRO_CONFIG_STR(ClMessageSystemGradient, cl_message_system_gradient, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "System message text gradient color")
MACRO_CONFIG_STR(ClMessageClientGradient, cl_message_client_gradient, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Client message text gradient color")
MACRO_CONFIG_STR(ClMessageHighlightGradient, cl_message_highlight_gradient, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Highlight message text gradient color")
MACRO_CONFIG_STR(ClMessageTeamGradient, cl_message_team_gradient, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Team message text gradient color")
MACRO_CONFIG_STR(ClMessageGradient, cl_message_gradient, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Normal message text gradient color")
MACRO_CONFIG_STR(ClMessageFriendGradient, cl_message_friend_gradient, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Friend message text gradient color")
MACRO_CONFIG_INT(QmChatLogAutoSave, qm_chat_log_auto_save, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-save chat log")
MACRO_CONFIG_INT(QmChatLogKeepDays, qm_chat_log_keep_days, 30, 0, 3650, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat log retention days")

MACRO_CONFIG_INT(QmNameplateCoordX, qm_nameplate_coord_x, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Nameplate position X")
MACRO_CONFIG_INT(QmNameplateCoordXAlignHint, qm_nameplate_coord_x_align_hint, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Nameplate X alignment hint with local player")
MACRO_CONFIG_INT(QmNameplateCoordXAlignHintStrict, qm_nameplate_coord_x_align_hint_strict, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Nameplate X alignment hint strict mode")
MACRO_CONFIG_INT(QmNameplateCoordXAlignHintWindowMs, qm_nameplate_coord_x_align_hint_window_ms, 1000, 100, 3000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Nameplate X alignment hint duration window (ms)")
MACRO_CONFIG_COL(QmNameplateCoordXAlignHintColor, qm_nameplate_coord_x_align_hint_color, 0x21FF99, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Nameplate X alignment hint highlight color")
MACRO_CONFIG_INT(QmNameplateCoordY, qm_nameplate_coord_y, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Nameplate position Y")
MACRO_CONFIG_INT(QmNameplateCoordsOwn, qm_nameplate_coords_own, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show own nameplate coordinates")
MACRO_CONFIG_INT(QmNameplateCoords, qm_nameplate_coords, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show others' nameplate coordinates")

// Enhanced Laser Effects (Glow + Pulse) / 增强激光效果（辉光+脉冲）
MACRO_CONFIG_INT(QmLaserEnhanced, qm_laser_enhanced, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable enhanced laser effect (glow + pulse animation)")
MACRO_CONFIG_INT(QmLaserGlowIntensity, qm_laser_glow_intensity, 30, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Laser glow intensity (0-100)")
MACRO_CONFIG_INT(QmLaserPulseSpeed, qm_laser_pulse_speed, 100, 10, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Pulse animation speed (percentage, 100=normal)")
MACRO_CONFIG_INT(QmLaserPulseAmplitude, qm_laser_pulse_amplitude, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Pulse Amplitude (0-100)")
MACRO_CONFIG_INT(QmLaserSize, qm_laser_size, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Laser Size/Thickness (percentage, 100=default)")
MACRO_CONFIG_INT(QmLaserRoundCaps, qm_laser_round_caps, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Laser Rounded Ends (0=square, 1=round)")
MACRO_CONFIG_INT(QmLaserAlpha, qm_laser_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Laser Opacity (0=transparent, 100=opaque)")

// Collision Hitbox Visualization / 碰撞体积可视化
MACRO_CONFIG_INT(QmShowCollisionHitbox, qm_show_collision_hitbox, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Collision Borders")
MACRO_CONFIG_COL(QmCollisionHitboxColorFreeze, qm_collision_hitbox_color_freeze, 16711935, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Freeze Collision Border Color")
MACRO_CONFIG_INT(QmCollisionHitboxAlpha, qm_collision_hitbox_alpha, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Collision Line Opacity")
MACRO_CONFIG_INT(QmHitboxMode, qm_hitbox_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hitbox Mode Override")
MACRO_CONFIG_INT(QmHitboxShowMap, qm_hitbox_show_map, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Danger Tiles in Hitbox Mode")
MACRO_CONFIG_INT(QmHitboxShowTees, qm_hitbox_show_tees, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Tee Collision in Hitbox Mode")
MACRO_CONFIG_INT(QmHitboxShowPickups, qm_hitbox_show_pickups, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Pickup Ranges in Hitbox Mode")
MACRO_CONFIG_INT(QmHitboxShowWeapons, qm_hitbox_show_weapons, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Weapon Interactions in Hitbox Mode")
// 语义化碰撞箱显示开关。旧的 ShowTees/ShowWeapons 保留用于配置迁移。
MACRO_CONFIG_INT(QmHitboxShowTeeCollision, qm_hitbox_show_tee_collision, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tee collision (Tee to Tee)")
MACRO_CONFIG_INT(QmHitboxShowTeeFreeze, qm_hitbox_show_tee_freeze, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tee freeze probe (Tee to Freeze)")
MACRO_CONFIG_INT(QmHitboxShowTeeDeath, qm_hitbox_show_tee_death, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tee death probe")
MACRO_CONFIG_INT(QmHitboxShowHammer, qm_hitbox_show_hammer, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hammer interaction")
MACRO_CONFIG_INT(QmHitboxShowProjectiles, qm_hitbox_show_projectiles, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Projectile / explosion range")
MACRO_CONFIG_INT(QmHitboxShowLasers, qm_hitbox_show_lasers, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Laser / shotgun interaction")
MACRO_CONFIG_INT(QmHitboxShowFreezeLasers, qm_hitbox_show_freeze_lasers, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Freeze laser collision volume")
MACRO_CONFIG_INT(QmHitboxShowHook, qm_hitbox_show_hook, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hook interaction")
MACRO_CONFIG_INT(QmHitboxAlpha, qm_hitbox_alpha, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hitbox Mode Global Opacity")
MACRO_CONFIG_INT(QmHitboxPlayerScope, qm_hitbox_player_scope, 2, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hitbox Mode Player Scope: 0=local, 1=local+clone, 2=all")
MACRO_CONFIG_COL(QmHitboxColorFreeze, qm_hitbox_color_freeze, 16711935, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hitbox Mode Freeze Border Color")
MACRO_CONFIG_COL(QmHitboxColorTee, qm_hitbox_color_tee, 65535, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hitbox Mode Tee Collision Color")
MACRO_CONFIG_COL(QmHitboxColorWeapon, qm_hitbox_color_weapon, 16776960, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hitbox Mode Weapon Interaction Color")

// Entity Overlay / 实体叠加
MACRO_CONFIG_INT(QmEntityOverlayDeathAlpha, qm_entity_overlay_death_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Override Death Tile Entity Alpha (0-100)")
MACRO_CONFIG_INT(QmEntityOverlayFreezeAlpha, qm_entity_overlay_freeze_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Override Freeze Tile Entity Alpha (0-100)")
MACRO_CONFIG_INT(QmEntityOverlayUnfreezeAlpha, qm_entity_overlay_unfreeze_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Override Entity Alpha for Unfreeze Tiles (0-100)")
MACRO_CONFIG_INT(QmEntityOverlayDeepFreezeAlpha, qm_entity_overlay_deep_freeze_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Depth freeze tile overlay alpha (0-100)")
MACRO_CONFIG_INT(QmEntityOverlayDeepUnfreezeAlpha, qm_entity_overlay_deep_unfreeze_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Depth unfreeze tile overlay alpha (0-100)")
MACRO_CONFIG_INT(QmEntityOverlayTeleAlpha, qm_entity_overlay_tele_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Teletile overlay alpha (0-100)")
MACRO_CONFIG_INT(QmEntityOverlayTeleCheckpointAlpha, qm_entity_overlay_tele_checkpoint_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "CP border overlay alpha (0-100)")
MACRO_CONFIG_INT(QmEntityOverlaySwitchAlpha, qm_entity_overlay_switch_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Switch tile overlay alpha (0-100)")

// Q1menG Client Recognition / Q1menG客户端识别
MACRO_CONFIG_INT(QmClientShowBadge, qm_client_show_badge, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Qm badge: identify via central server and mark QmClient users on nameplate/scoreboard")

// Sponsor title appearance / 赞助头衔外观

// Fast Input / 快速输入
MACRO_CONFIG_INT(QmAutoMargin, qm_auto_margin, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto adjust prediction margin (fixed base margin when off)")

// Keyword Reply / 关键词回复
MACRO_CONFIG_INT(QmKeywordReplyEnabled, qm_keyword_reply_enabled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable keyword reply")
MACRO_CONFIG_INT(QmKeywordReplyUseDummy, qm_keyword_reply_use_dummy, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keyword reply use Dummy")
MACRO_CONFIG_STR(QmKeywordReplyRules, qm_keyword_reply_rules, 4096, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keyword reply rules (each line: [rename] [regex] keyword=>reply)")
MACRO_CONFIG_INT(QmAutoReplyCooldown, qm_auto_reply_cooldown, 3, 0, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto reply cooldown (seconds)")

// Pie Menu / 饼菜单
MACRO_CONFIG_INT(QmPieMenuEnabled, qm_pie_menu_enabled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable Pie Menu")
MACRO_CONFIG_INT(QmPieMenuMaxDistance, qm_pie_menu_max_distance, 400, 100, 2000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Max player detection distance")
MACRO_CONFIG_INT(QmPieMenuScale, qm_pie_menu_scale, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "UI size percentage")
MACRO_CONFIG_INT(QmPieMenuOpacity, qm_pie_menu_opacity, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Menu opacity (0-100)")
MACRO_CONFIG_STR(QmPieMenuRenameQueue, qm_pie_menu_rename_queue, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Pie menu rename list (separate with |, e.g., name1|name2)")
MACRO_CONFIG_INT(QmPieMenuColorFriend, qm_pie_menu_color_friend, 0xE64D66BF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Friend option color")
MACRO_CONFIG_INT(QmPieMenuColorWhisper, qm_pie_menu_color_whisper, 0x8059B3BF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Whisper option color")
MACRO_CONFIG_INT(QmPieMenuColorMention, qm_pie_menu_color_mention, 0xD98033BF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Mention option color")
MACRO_CONFIG_INT(QmPieMenuColorCopySkin, qm_pie_menu_color_copy_skin, 0x408CCCBF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Copy skin option color")
MACRO_CONFIG_INT(QmPieMenuColorSwap, qm_pie_menu_color_swap, 0xCC4D4DBF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Swap option color")
MACRO_CONFIG_INT(QmPieMenuColorSpectate, qm_pie_menu_color_spectate, 0x738C99BF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Spectate option color")

// Repeat Message / 复读功能
MACRO_CONFIG_INT(QmRepeatEnabled, qm_repeat_enabled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable repeat function")
MACRO_CONFIG_INT(QmSayNoPop, qm_say_nopop, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide typing emoji during input")
MACRO_CONFIG_INT(QmHammerSwapSkin, qm_hammer_swap_skin, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Switch skin on hammer")
MACRO_CONFIG_INT(QmSkinChangeTransition, qm_skin_change_transition, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable skin switch animation")
MACRO_CONFIG_INT(QmSkinChangeTransitionScope, qm_skin_change_transition_scope, 1, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Skin switch animation range: 0=Self, 1=Local+Dummy, 2=All players")
MACRO_CONFIG_INT(QmSkinChangeTransitionType, qm_skin_change_transition_type, 0, 0, 6, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Skin switch animation type (0=Ghost pop, 1=Soft fade, 2=Slide left, 3=Rotate pop, 4=Light dark switch, 5=Glitch shake, 6=Elastic scale)")
MACRO_CONFIG_INT(QmSkinChangeTransitionMs, qm_skin_change_transition_ms, 500, 0, 2000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Skin switch animation duration (ms, 0=No animation)")
MACRO_CONFIG_INT(QmSkinChangeTransitionEasing, qm_skin_change_transition_easing, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Skin switch animation easing mode")
MACRO_CONFIG_INT(QmSkinChangeTransitionIntensity, qm_skin_change_transition_intensity, 100, 0, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Skin switch animation intensity percentage")
MACRO_CONFIG_INT(QmCycleTeeHue, qm_cycle_tee_hue, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Cycle custom Tee hue for main")
MACRO_CONFIG_INT(QmCycleTeeHueDummy, qm_cycle_tee_hue_dummy, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Cycle custom Tee hue for dummy simultaneously")
MACRO_CONFIG_INT(QmCycleTeeHueSpeed, qm_cycle_tee_hue_speed, 72, 0, 360, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Cycle custom Tee hue speed (deg/s)")
MACRO_CONFIG_INT(QmTeamTeeGlow, qm_team_tee_glow, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Draw a glow around tees colored by their race team")
MACRO_CONFIG_INT(QmTeamTeeGlowTeam0Mode, qm_team_tee_glow_team0_mode, 1, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Team 0 (unteamed) tee glow mode: 0=Off, 1=Tee color, 2=Custom color, 3=Rainbow")
MACRO_CONFIG_COL(QmTeamTeeGlowColor, qm_team_tee_glow_color, 0xFFFFFFFF, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Team 0 tee glow custom color")
MACRO_CONFIG_INT(QmRandomEmoteOnHit, qm_random_emote_on_hit, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Random emote when hit by hammer/grenade")
MACRO_CONFIG_INT(QmEmoticonShadow, qm_emoticon_shadow, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Draw shadow behind emote")
MACRO_CONFIG_INT(QmShowOtherSuperEmotes, qm_show_other_super_emotes, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show other players' large emoticons")
MACRO_CONFIG_INT(QmShowOtherLaunchEmotes, qm_show_other_launch_emotes, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show other players' launched emoticons")
MACRO_CONFIG_INT(QmTitleAdvanced, qm_title_advanced, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show advanced title settings (collapsing keeps configured effects)")
MACRO_CONFIG_INT(QmTitleColorMode, qm_title_color_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Bracketed title color: 0 = follow server, 1 = single color, 2 = rainbow")
MACRO_CONFIG_COL(QmTitleColor, qm_title_color, 0xFFFFFF, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Bracketed title single color")
MACRO_CONFIG_INT(QmTitleOpacity, qm_title_opacity, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Bracketed title opacity (0-100)")
MACRO_CONFIG_INT(QmTitleStyleEnabled, qm_title_style_enabled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use animated title style (overrides qm_title_color_mode)")
MACRO_CONFIG_STR(QmTitleStyle, qm_title_style, 32, "exotic_rainbow", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Animated title style id")
MACRO_CONFIG_INT(QmTitleBobAmplitude, qm_title_bob_amplitude, 4, 0, 12, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Title per-character vertical bob amplitude in pixels (0 = off)")
MACRO_CONFIG_INT(QmTitlePhase, qm_title_phase, 20, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Title per-character phase per pixel in 1/1000 px (0 = style default, 20 = visible light band)")
MACRO_CONFIG_INT(QmTitleBloom, qm_title_bloom, 1, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Classic title glow: 0 = off, 1 = subtle (6 draws), 2 = full Calamity (16 draws)")
MACRO_CONFIG_INT(QmTitleEffect, qm_title_effect, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Title spatial effect: 0 = polished, 1 = solid, 2 = classic Calamity, 3 = off")
MACRO_CONFIG_INT(QmTitleShimmerSpeed, qm_title_shimmer_speed, 60, 0, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Title light sweep speed in 1/100 row per second (0 = off)")
MACRO_CONFIG_INT(QmTitleBobWavelength, qm_title_bob_wavelength, 320, 16, 1024, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Title bob wavelength in pixels")
MACRO_CONFIG_INT(QmTitleBobSpeed, qm_title_bob_speed, 150, 0, 2000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Title bob angular speed in 1/100 rad/s")
MACRO_CONFIG_INT(QmTitleBobPixelSnap, qm_title_bob_pixel_snap, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Snap title bob offset to whole pixels (sharper glyphs, choppier motion)")
MACRO_CONFIG_INT(QmWeaponTrajectory, qm_weapon_trajectory, 1, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Weapon trajectory helper mode (0=Off, 1=On key, 2=Always)")
MACRO_CONFIG_INT(QmWeaponTrajectoryGun, qm_weapon_trajectory_gun, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Pistol guide line")
MACRO_CONFIG_INT(QmWeaponTrajectoryNinja, qm_weapon_trajectory_ninja, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Predict ninja path")
MACRO_CONFIG_COL(QmWeaponTrajectoryColor, qm_weapon_trajectory_color, 16750899, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Weapon trajectory helper color")
MACRO_CONFIG_INT(QmWeaponTrajectoryWidth, qm_weapon_trajectory_width, 2, 1, 10, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Weapon trajectory helper width")
MACRO_CONFIG_INT(QmWeaponTrajectoryAlpha, qm_weapon_trajectory_alpha, 70, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Weapon trajectory helper alpha")
MACRO_CONFIG_INT(QmWeaponSwitchAnim, qm_weapon_switch_anim, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Play slide-in rotation animation when switching weapon")
MACRO_CONFIG_INT(QmWeaponSwitchAnimScope, qm_weapon_switch_anim_scope, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Weapon switch animation scope: 0=Self, 1=Local+dummy, 2=All players")
MACRO_CONFIG_INT(QmWeaponSwitchAnimDurationMs, qm_weapon_switch_anim_duration_ms, 300, 50, 2000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Weapon switch animation duration (ms)")
MACRO_CONFIG_INT(QmWeaponSwitchAnimDistance, qm_weapon_switch_anim_distance, 40, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Weapon switch animation slide distance")
MACRO_CONFIG_INT(QmWeaponSwitchAnimRotation, qm_weapon_switch_anim_rotation, 360, 0, 1440, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Weapon switch animation rotation angle")
MACRO_CONFIG_INT(QmWeaponSwitchAnimEasing, qm_weapon_switch_anim_easing, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Weapon switch animation easing mode")
MACRO_CONFIG_INT(QmWeaponReloadAnim, qm_weapon_reload_anim, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Play a flip animation while reloading weapons")
MACRO_CONFIG_INT(QmWeaponReloadAnimProbability, qm_weapon_reload_anim_probability, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Weapon reload animation probability")
MACRO_CONFIG_INT(QmDeepflyMode, qm_deepfly_mode, 0, 0, 3, CFGFLAG_CLIENT, "Deepfly mode (0=Normal, 1=DF, 2=HDF, 3=Custom)")

// Auto Unspec on Unfreeze / 解冻自动取消旁观
MACRO_CONFIG_INT(QmAutoUnspecOnUnfreeze, qm_auto_unspec_on_unfreeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-unspec when unfrozen")

// Auto-Switch on Unfreeze / HJ大佬辅助 - 自动切换到解冻的Tee
MACRO_CONFIG_INT(QmAutoSwitchOnUnfreeze, qm_auto_switch_on_unfreeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-switch to first unfrozen when both frozen")
MACRO_CONFIG_INT(QmAutoCloseChatOnUnfreeze, qm_auto_close_chat_on_unfreeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-close chat after unfreeze")
MACRO_CONFIG_INT(QmFreezeWakeupPopup, qm_freeze_wakeup_popup, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show hint at random top-left/right of player when main or dummy is hammered awake")
MACRO_CONFIG_INT(QmAutoTeamLock, qm_auto_team_lock, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-lock after joining lockable team")
MACRO_CONFIG_INT(QmAutoTeamLockDelay, qm_auto_team_lock_delay, 5, 0, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-lock delay (seconds)")

// Input Overlay / 输入叠加
MACRO_CONFIG_INT(QmInputOverlay, qm_input_overlay, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show input overlay")
MACRO_CONFIG_INT(QmInputOverlayScale, qm_input_overlay_scale, 20, 1, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Input overlay keyboard scale (percent)")
MACRO_CONFIG_INT(QmInputOverlayMouseScale, qm_input_overlay_mouse_scale, 20, 1, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Input overlay mouse scale (percent)")
MACRO_CONFIG_INT(QmInputOverlayOpacity, qm_input_overlay_opacity, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Input overlay opacity (percent)")
MACRO_CONFIG_INT(QmInputOverlayPosX, qm_input_overlay_pos_x, 71, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Input overlay X position (percent)")
MACRO_CONFIG_INT(QmInputOverlayPosY, qm_input_overlay_pos_y, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Input overlay Y position (percent)")

// Notification Bar / 通知栏
MACRO_CONFIG_INT(QmHudNotificationsSystem, qm_hud_notifications_system, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Notification bar overrides server system messages (except version info)")
MACRO_CONFIG_INT(QmHudNotificationsEcho, qm_hud_notifications_echo, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Notification bar overrides echo messages")
MACRO_CONFIG_INT(QmHudNotificationsShowAdvanced, qm_hud_notifications_show_advanced, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show notification bar advanced settings")
MACRO_CONFIG_INT(QmHudNotificationsUseCategoryFilters, qm_hud_notifications_use_category_filters, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use notification bar category filter")
MACRO_CONFIG_INT(QmHudNotificationsShowBasicInfo, qm_hud_notifications_show_basic_info, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Notification bar shows basic server info")
MACRO_CONFIG_INT(QmHudNotificationsShowHelpInfo, qm_hud_notifications_show_help_info, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Notification bar shows server help")
MACRO_CONFIG_INT(QmHudNotificationsShowPrompts, qm_hud_notifications_show_prompts, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Notification bar shows important server hints")
MACRO_CONFIG_INT(QmHudNotificationsShowUnknown, qm_hud_notifications_show_unknown, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Notification bar shows unknown server messages")
MACRO_CONFIG_INT(QmHudNotificationsCompatSolo, qm_hud_notifications_compat_solo, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Compatible with single-player area hints from other servers")
MACRO_CONFIG_COL(QmHudNotificationsBgColor, qm_hud_notifications_bg_color, 0x99000000, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Notification bar background color")
MACRO_CONFIG_COL(QmHudNotificationsTextColor, qm_hud_notifications_text_color, 0xFFFFFFFF, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Notification bar system message text color")
MACRO_CONFIG_INT(QmHudNotificationsEchoInheritColor, qm_hud_notifications_echo_inherit_color, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Notification bar echo inherits chat echo color")
MACRO_CONFIG_COL(QmHudNotificationsEchoTextColor, qm_hud_notifications_echo_text_color, 0xFF92FFFF, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Notification echo overlay text color")
MACRO_CONFIG_INT(QmHudNotificationsTextSize, qm_hud_notifications_text_size, 8, 1, 24, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Notification text size")
MACRO_CONFIG_INT(QmHudNotificationsHoldMs, qm_hud_notifications_hold_ms, 2500, 500, 10000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Notification display duration (ms)")
MACRO_CONFIG_INT(QmHudNotificationsAnimType, qm_hud_notifications_anim_type, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Notification animation type (0=Fade+Slide, 1=Fade only, 2=No animation)")
MACRO_CONFIG_INT(QmHudNotificationsAnimMs, qm_hud_notifications_anim_ms, 220, 0, 2000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Notification animation duration (ms)")
MACRO_CONFIG_INT(QmHudNotificationsMaxVisible, qm_hud_notifications_max_visible, 3, 1, 8, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Maximum notification count")
MACRO_CONFIG_INT(QmHudNotificationsEdgeMargin, qm_hud_notifications_edge_margin, 8, 0, 32, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Margin when notification is docked")

MACRO_CONFIG_INT(QmMonitoringHudOpacity, qm_monitoring_hud_opacity, 66, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Debug graph panel opacity (percent)")

// Voice / 语音
MACRO_CONFIG_INT(QmVoiceEnable, qm_voice_enable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable voice chat")
MACRO_CONFIG_INT(QmVoiceAgcEnable, qm_voice_agc_enable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatic gain control (0=off 1=on)")
MACRO_CONFIG_INT(QmVoiceProtocolVersion, qm_voice_protocol_version, 3, 1, 255, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice protocol version")
MACRO_CONFIG_STR(QmVoiceServer, qm_voice_server, 256, "wss://qmclient.icu/ws/voice", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice server URL (ws:// or wss://)")
MACRO_CONFIG_STR(QmRealtimeWebsocketUrl, qm_realtime_websocket_url, 512, "", CFGFLAG_CLIENT, "Legacy realtime WebSocket URL (migrated to qm_websocket_url)")
MACRO_CONFIG_STR(QmWebSocketUrl, qm_websocket_url, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dedicated WebSocket endpoint (empty uses wss://qmclient.icu/ws)")
MACRO_CONFIG_STR(QmWebSocketProtocol, qm_websocket_protocol, 64, "qmclient-json", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Sec-WebSocket-Protocol sent during the realtime handshake")
MACRO_CONFIG_INT(QmWebSocketHeartbeat, qm_websocket_heartbeat, 15, 0, 600, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Realtime channel heartbeat interval (seconds, 0 uses default)")
MACRO_CONFIG_INT(QmWebSocketBackoffBaseMs, qm_websocket_backoff_base_ms, 1000, 100, 60000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Realtime channel reconnect backoff base (ms)")
MACRO_CONFIG_INT(QmWebSocketBackoffMaxMs, qm_websocket_backoff_max_ms, 60000, 1000, 600000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Realtime channel reconnect backoff cap (ms)")
MACRO_CONFIG_INT(QmWebSocketLog, qm_websocket_log, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Log realtime WebSocket channel events")
MACRO_CONFIG_INT(QmWebSocketAllowInsecureTls, qm_websocket_allow_insecure_tls, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Legacy option: connections that skip TLS verification are rejected")
MACRO_CONFIG_STR(QmMapUploadEndpoint, qm_map_upload_endpoint, 512, "https://shengyan.art/api/upload", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Map upload endpoint (empty disables map upload)")
MACRO_CONFIG_STR(QmVoiceAudioBackend, qm_voice_audio_backend, 64, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice audio backend (SDL driver name, empty=auto)")
MACRO_CONFIG_STR(QmVoiceInputDevice, qm_voice_input_device, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice input device (empty=default)")
MACRO_CONFIG_STR(QmVoiceOutputDevice, qm_voice_output_device, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice output device (empty=default)")
MACRO_CONFIG_INT(QmVoiceStereo, qm_voice_stereo, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice stereo output")
MACRO_CONFIG_INT(QmVoiceStereoWidth, qm_voice_stereo_width, 100, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice stereo width (percent)")
MACRO_CONFIG_STR(QmVoiceToken, qm_voice_token, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice room token (optional)")
MACRO_CONFIG_INT(QmVoiceGroupMode, qm_voice_group_mode, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice group mode")
MACRO_CONFIG_INT(QmVoiceFilterEnable, qm_voice_filter_enable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable voice filtering (highpass/compressor/limiter)")
MACRO_CONFIG_INT(QmVoiceBitrateProfile, qm_voice_bitrate_profile, 0, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice bitrate (0=auto 1=24kbps 2=32kbps 3=48kbps 4=64kbps)")
MACRO_CONFIG_INT(QmVoiceNoiseSuppressEnable, qm_voice_noise_suppress_enable, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Noise suppression mode (0=off 1=simple 2=RNNoise)")
MACRO_CONFIG_INT(QmVoiceNoiseSuppressStrength, qm_voice_noise_suppress_strength, 35, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Noise suppression strength (percent)")
MACRO_CONFIG_INT(QmVoiceCompThreshold, qm_voice_comp_threshold, 24, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Compressor threshold (percent)")
MACRO_CONFIG_INT(QmVoiceCompRatio, qm_voice_comp_ratio, 20, 10, 80, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Compressor ratio (x10)")
MACRO_CONFIG_INT(QmVoiceCompAttackMs, qm_voice_comp_attack_ms, 12, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Compressor attack time (ms)")
MACRO_CONFIG_INT(QmVoiceCompReleaseMs, qm_voice_comp_release_ms, 140, 10, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Compressor release time (ms)")
MACRO_CONFIG_INT(QmVoiceCompMakeup, qm_voice_comp_makeup, 125, 0, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Compressor makeup gain (percent)")
MACRO_CONFIG_INT(QmVoiceLimiter, qm_voice_limiter, 92, 10, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Limiter threshold (percent)")
MACRO_CONFIG_INT(QmVoiceRadius, qm_voice_radius, 50, 1, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice distance radius (Tiles)")
MACRO_CONFIG_INT(QmVoiceVolume, qm_voice_volume, 100, 0, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice playback volume (percent)")
MACRO_CONFIG_INT(QmVoiceMicVolume, qm_voice_mic_volume, 100, 0, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Microphone volume (percent)")
MACRO_CONFIG_INT(QmVoiceMicMute, qm_voice_mic_mute, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Mute microphone")
MACRO_CONFIG_INT(QmVoiceShowConnectionStatus, qm_voice_show_connection_status, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show voice connection status")
MACRO_CONFIG_INT(QmVoiceShowAdvanced, qm_voice_show_advanced, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show voice advanced options")
MACRO_CONFIG_INT(QmVoiceTestMode, qm_voice_test_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice test mode (0=Off 1=Local 2=Server Loopback)")
MACRO_CONFIG_INT(QmVoiceVadEnable, qm_voice_vad_enable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable voice activation (VAD)")
MACRO_CONFIG_INT(QmVoiceVadThreshold, qm_voice_vad_threshold, 8, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "VAD threshold (percent)")
MACRO_CONFIG_INT(QmVoiceVadReleaseDelayMs, qm_voice_vad_release_delay_ms, 150, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "VAD release delay (ms)")
MACRO_CONFIG_INT(QmVoiceIgnoreDistance, qm_voice_ignore_distance, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Ignore voice distance attenuation")
MACRO_CONFIG_INT(QmVoiceGroupGlobal, qm_voice_group_global, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Same group full map listening")
MACRO_CONFIG_INT(QmVoiceVisibilityMode, qm_voice_visibility_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Visibility filter mode")
MACRO_CONFIG_INT(QmVoiceListMode, qm_voice_list_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "List filter mode")
MACRO_CONFIG_STR(QmVoiceWhitelist, qm_voice_whitelist, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice whitelist (comma-separated)")
MACRO_CONFIG_STR(QmVoiceBlacklist, qm_voice_blacklist, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice blacklist (comma-separated)")
MACRO_CONFIG_STR(QmVoiceMute, qm_voice_mute, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice mute list (comma-separated)")
MACRO_CONFIG_INT(QmVoiceHearVad, qm_voice_hear_vad, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Receive VAD speakers")
MACRO_CONFIG_STR(QmVoiceVadAllow, qm_voice_vad_allow, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "VAD allow list (comma-separated)")
MACRO_CONFIG_STR(QmVoiceNameVolumes, qm_voice_name_volumes, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Per-name volume (name=percent)")
MACRO_CONFIG_INT(QmVoiceDebug, qm_voice_debug, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Output voice debug log")
MACRO_CONFIG_INT(QmVoiceOffNonActive, qm_voice_off_nonactive, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Pause voice when window unfocused")
MACRO_CONFIG_INT(QmVoicePttReleaseDelayMs, qm_voice_ptt_release_delay_ms, 0, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "PTT release delay (ms)")
MACRO_CONFIG_INT(QmVoiceHearOnSpecPos, qm_voice_hear_on_spec_pos, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Listen from camera center when spectating")
MACRO_CONFIG_INT(QmVoiceHearPeoplesInSpectate, qm_voice_hear_peoples_in_spectate, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Receive voice from spectators/inactive players")

// Streamer Mode / 主播模式
MACRO_CONFIG_INT(QmStreamerHideNames, qm_streamer_hide_names, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide non-friend names/clans and show client ID")
MACRO_CONFIG_INT(QmStreamerHideSkins, qm_streamer_hide_skins, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Non-friends use default skin")
MACRO_CONFIG_INT(QmStreamerScoreboardDefaultFlags, qm_streamer_scoreboard_default_flags, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show default flag in scoreboard")

// 好友上线 / 进服提醒（默认文案为英文 source key，运行时 Localize）
MACRO_CONFIG_INT(QmFriendOnlineNotify, qm_friend_online_notify, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Friend online notification")
MACRO_CONFIG_INT(QmFriendOnlineAutoRefresh, qm_friend_online_auto_refresh, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-refresh server list for friend notifications")
MACRO_CONFIG_INT(QmFriendOnlineRefreshSeconds, qm_friend_online_refresh_seconds, 30, 5, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Friend notification refresh interval (sec)")
MACRO_CONFIG_INT(QmFriendEnterAutoGreet, qm_friend_enter_auto_greet, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto greet friend on map join")
MACRO_CONFIG_INT(QmFriendEnterBroadcast, qm_friend_enter_broadcast, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show friend join in large text")
MACRO_CONFIG_STR(QmFriendEnterBroadcastText, qm_friend_enter_broadcast_text, 128, "%s joined this server", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Large text for friend join (use %s for friend name)")
MACRO_CONFIG_STR(QmFriendEnterGreetText, qm_friend_enter_greet_text, 128, "Hi!", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto greet friend text")

// Block Words / 屏蔽词
MACRO_CONFIG_INT(QmBlockWordsEnabled, qm_block_words_enabled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable word filter list")
MACRO_CONFIG_INT(QmWarListBlockEnemyChat, qm_warlist_block_enemy_chat, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide chat messages from players marked as enemies")
MACRO_CONFIG_INT(QmBlockWordsShowConsole, qm_block_words_show_console, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show filtered words in console")
MACRO_CONFIG_COL(QmBlockWordsConsoleColor, qm_block_words_console_color, 0xFFFFFFFF, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Filtered word console color")
MACRO_CONFIG_INT(QmBlockWordsMultiReplace, qm_block_words_multi_replace, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Replace censored words with length-based chars")
MACRO_CONFIG_INT(QmBlockWordsAction, qm_block_words_action, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Word filter action: 0=replace matching words, 1=hide entire message")
MACRO_CONFIG_INT(QmBlockWordsMode, qm_block_words_mode, 2, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Replace mode 0=Regex 1=Full 2=Both")
MACRO_CONFIG_STR(QmBlockWordsReplacementChar, qm_block_words_replacement_char, 8, "*", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Censor replacement character")
MACRO_CONFIG_STR(QmBlockWordsList, qm_block_words_list, 1024, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Censor word list (comma-separated)")
MACRO_CONFIG_STR(QmSidebarCardOrder, qm_sidebar_card_order, 2048, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "QmClient sidebar module order")
MACRO_CONFIG_STR(QmSidebarCardCollapsed, qm_sidebar_card_collapsed, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "QmClient sidebar module collapse state")

// Nameplate - 名字版
MACRO_CONFIG_INT(QmNameplateFreeMove, qm_nameplate_free_move, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Name plate elements freely movable within bounds")
MACRO_CONFIG_INT(QmNameplateFreeMoveX, qm_nameplate_free_move_x, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Name plate elements allow free X movement")
MACRO_CONFIG_INT(QmNameplateFreeMoveY, qm_nameplate_free_move_y, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Name plate elements allow free Y movement")
MACRO_CONFIG_INT(QmNameplateKeysOffsetX, qm_nameplate_keys_offset_x, 0, -300, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Name plate key line X offset")
MACRO_CONFIG_INT(QmNameplateKeysOffsetY, qm_nameplate_keys_offset_y, 0, -300, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Name plate key line Y offset")
MACRO_CONFIG_INT(QmNameplateCoordsOffsetX, qm_nameplate_coords_offset_x, 0, -300, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Name plate coord line X offset")
MACRO_CONFIG_INT(QmNameplateCoordsOffsetY, qm_nameplate_coords_offset_y, 0, -300, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Name plate coord line Y offset")
MACRO_CONFIG_INT(QmNameplateHookOffsetX, qm_nameplate_hook_offset_x, 0, -300, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Name plate strength line X offset")
MACRO_CONFIG_INT(QmNameplateHookOffsetY, qm_nameplate_hook_offset_y, 0, -300, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Name plate strength line Y offset")
MACRO_CONFIG_INT(QmNameplateHookStrongWeakScope, qm_nameplate_hook_strong_weak_scope, 1, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Nameplate hook strength icon scope (0=Own 1=Others 2=Strong hook 3=Weak hook 4=All players)")
MACRO_CONFIG_INT(QmNameplateShowScope, qm_nameplate_show_scope, 5, 0, 5, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Nameplate nickname scope (0=None 1=Current character 2=Own characters 3=Other players 4=Other players and own non-current characters 5=All)")
MACRO_CONFIG_INT(QmNameplateShowScopeMigrated, qm_nameplate_show_scope_migrated, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Nameplate nickname scope migration completed flag")
MACRO_CONFIG_COL(QmNameplateStrongHookColor, qm_nameplate_strong_hook_color, 6401973, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Nameplate strong hook icon color")
MACRO_CONFIG_COL(QmNameplateWeakHookColor, qm_nameplate_weak_hook_color, 41131, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Nameplate weak hook icon color")
MACRO_CONFIG_INT(QmNameplateTextEffects, qm_nameplate_text_effects, 1, 0, 15, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Nameplate text effects (1=Border 2=Gradient 4=Rainbow 8=Glow)")
MACRO_CONFIG_COL(QmNameplateTextBorderColor, qm_nameplate_text_border_color, 0x80000000, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Nameplate text border color")
MACRO_CONFIG_INT(QmNameplateTextBorderRange, qm_nameplate_text_border_range, 1, 1, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Nameplate text border range")
MACRO_CONFIG_COL(QmNameplateTextGradientColor, qm_nameplate_text_gradient_color, 0xFFFFFFFF, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Nameplate text gradient color")
MACRO_CONFIG_COL(QmNameplateTextGlowColor, qm_nameplate_text_glow_color, 0x664CC6FF, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Nameplate text glow color")
MACRO_CONFIG_INT(QmNameplateTextGlowRange, qm_nameplate_text_glow_range, 4, 1, 12, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Nameplate text glow range")
MACRO_CONFIG_INT(QmNameplateEffectAutoLod, qm_nameplate_effect_auto_lod, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Nameplate text effect auto LOD (0=Off 1=On: trim outer effect layers when crowded)")
MACRO_CONFIG_INT(QmNameplateEffectLodThreshold, qm_nameplate_effect_lod_threshold, 20, 4, 64, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Nameplate count that still keeps full text effect quality; beyond it effect layers scale down by count (higher=keep more)")
MACRO_CONFIG_INT(QmNameplateTextPlayingScope, qm_nameplate_text_playing_scope, 5, 0, 5, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Nameplate text effects playing scope (0=Off 1=Own 2=Others 3=Friends 4=Own and friends 5=All players)")
MACRO_CONFIG_INT(QmNameplateTextSpectateScope, qm_nameplate_text_spectate_scope, 1, 0, 5, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Nameplate text effects spectate scope (0=Off 1=Spectated player 2=Others 3=Friends 4=Spectated player and friends 5=All players)")
// Demo 预览与视频导出共用的独立显示选项。
MACRO_CONFIG_INT(QmDemoShowDirection, qm_demo_show_direction, 1, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Demo key presses (0=Off 1=Others 2=All 3=Own)")
MACRO_CONFIG_INT(QmDemoShowStrongWeak, qm_demo_show_strong_weak, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Demo hook strength (0=Off 1=Icon 2=Icon and number)")
MACRO_CONFIG_INT(QmDemoStrongWeakScope, qm_demo_strong_weak_scope, 4, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Demo hook strength scope (0=Own 1=Others 2=Strong 3=Weak 4=All)")
MACRO_CONFIG_INT(QmDemoShowHud, qm_demo_show_hud, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show in-game HUD in demo preview and video")
MACRO_CONFIG_INT(QmDemoShowChat, qm_demo_show_chat, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show chat in demo preview and video")
MACRO_CONFIG_INT(QmNameplateTextDemoMode, qm_nameplate_text_demo_mode, 1, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Nameplate text effects demo mode (0=Off 1=Smart 2=Manual target 3=Manual scope)")
MACRO_CONFIG_INT(QmNameplateTextDemoTarget, qm_nameplate_text_demo_target, -1, -1, 63, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Nameplate text effects demo manual target client ID (-1=None)")
MACRO_CONFIG_INT(QmNameplateClanOffsetX, qm_nameplate_clan_offset_x, 0, -300, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Name plate clan line X offset")
MACRO_CONFIG_INT(QmNameplateClanOffsetY, qm_nameplate_clan_offset_y, 0, -300, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Name plate clan line Y offset")
MACRO_CONFIG_INT(QmNameplateNameOffsetX, qm_nameplate_name_offset_x, 0, -300, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Name plate name line X offset")
MACRO_CONFIG_INT(QmNameplateNameOffsetY, qm_nameplate_name_offset_y, 0, -300, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Name plate name line Y offset")

// Gores Mode - Gores 模式
MACRO_CONFIG_INT(QmGores, qm_gores, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable Gores mode (King of Gores helper tools)")
MACRO_CONFIG_INT(QmGoresAutoWeaponSwitch, qm_gores_auto_weapon_switch, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically switch hammer/gun in Gores mode")
MACRO_CONFIG_INT(QmGoresDisableIfWeapons, qm_gores_disable_if_weapons, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Disable Gores auto-switch when picking up extra weapon")
MACRO_CONFIG_INT(QmGoresAutoEnable, qm_gores_auto_enable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically enable Gores auto-switch in Gores game mode")
MACRO_CONFIG_INT(QmGoresFastInput, qm_gores_fast_input, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable fast input in Gores mode")
MACRO_CONFIG_INT(QmGoresFastInputOthers, qm_gores_fast_input_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable fast input for other players in Gores mode")
MACRO_CONFIG_INT(QmGoresHideGuides, qm_gores_hide_guides, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide helper lines in Gores mode")
MACRO_CONFIG_INT(QmGoresDisableDummyHammer, qm_gores_disable_dummy_hammer, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Temporarily disable dummy hammering in Gores mode")
MACRO_CONFIG_INT(QmGoresSuppressSwitchAnim, qm_gores_suppress_switch_anim, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Skip the weapon switch animation for hammer switches in Gores mode")

// Zen Mode - 禅模式
MACRO_CONFIG_INT(QmFocusMode, qm_focus_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable Zen Mode")
MACRO_CONFIG_INT(QmFocusModeHideNames, qm_focus_mode_hide_names, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide player names in Zen Mode")
MACRO_CONFIG_INT(QmFocusModeHideNameplates, qm_focus_mode_hide_nameplates, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide player name plates in Zen Mode")
MACRO_CONFIG_INT(QmFocusModeHideJumpEffects, qm_focus_mode_hide_jump_effects, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide jump effects in Zen Mode")
MACRO_CONFIG_INT(QmFocusModeHideKillEffects, qm_focus_mode_hide_kill_effects, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide death/respawn effects in Zen Mode")
MACRO_CONFIG_INT(QmFocusModeHideExplosionEffects, qm_focus_mode_hide_explosion_effects, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide projectile effects in Zen Mode")
MACRO_CONFIG_INT(QmFocusModeHideFreezeEffects, qm_focus_mode_hide_freeze_effects, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide freeze effects in Zen Mode")
MACRO_CONFIG_INT(QmFocusModeHideHammerEffects, qm_focus_mode_hide_hammer_effects, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide hammer effects in Zen Mode")
MACRO_CONFIG_INT(QmFocusModeHideMuzzleEffects, qm_focus_mode_hide_muzzle_effects, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide weapon fire effects in Zen Mode")
MACRO_CONFIG_INT(QmFocusModeMuteJumpSounds, qm_focus_mode_mute_jump_sounds, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Mute jump sound in Zen Mode")
MACRO_CONFIG_INT(QmFocusModeMuteDeathSounds, qm_focus_mode_mute_death_sounds, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Mute death/respawn sound in Zen Mode")
MACRO_CONFIG_INT(QmFocusModeMuteHammerSounds, qm_focus_mode_mute_hammer_sounds, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Mute hammer sound in Zen mode")
MACRO_CONFIG_INT(QmFocusModeHideHud, qm_focus_mode_hide_hud, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide HUD in Zen mode")
MACRO_CONFIG_INT(QmFocusModeHideChat, qm_focus_mode_hide_chat, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide player messages in Zen mode")
MACRO_CONFIG_INT(QmFocusModeHideSystemInfoMessages, qm_focus_mode_hide_system_info_messages, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide basic system info (join, version, rules) in Zen mode")
MACRO_CONFIG_INT(QmFocusModeHideSystemMessages, qm_focus_mode_hide_system_messages, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide server tip notifications (including notification bar) in Zen mode")
MACRO_CONFIG_INT(QmFocusModeHideEcho, qm_focus_mode_hide_echo, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide Echo messages in Zen mode")
MACRO_CONFIG_INT(QmFocusModeHideMapProgress, qm_focus_mode_hide_map_progress, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide map progress bar in Zen mode")
MACRO_CONFIG_INT(QmFocusModeHideInfoMessages, qm_focus_mode_hide_info_messages, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide kill and finish messages in Zen mode")
MACRO_CONFIG_INT(QmFocusModeHideScoreboard, qm_focus_mode_hide_scoreboard, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide scoreboard in Zen mode")
MACRO_CONFIG_INT(QmFocusModeHideDirectionIndicators, qm_focus_mode_hide_direction_indicators, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide direction in Zen mode")
MACRO_CONFIG_INT(QmFocusModeHideGuideLines, qm_focus_mode_hide_guide_lines, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide helper lines in Zen mode")
MACRO_CONFIG_INT(QmAxiomAutoLogin, qm_axiom_auto_login, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-login after entering Axiom community server")
MACRO_CONFIG_STR(QmAxiomLoginPassword, qm_axiom_login_password, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Password for Axiom main account auto-login")
MACRO_CONFIG_STR(QmAxiomDummyLoginPassword, qm_axiom_dummy_login_password, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Password for Axiom alt account auto-login")

// Player Stats HUD - 玩家统计面板
MACRO_CONFIG_INT(QmPlayerStatsHud, qm_player_stats_hud, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable player statistics panel")
MACRO_CONFIG_INT(QmPlayerStatsMapProgress, qm_player_stats_map_progress, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show map progress bar (beta)")
MACRO_CONFIG_INT(QmPlayerStatsMapProgressStyle, qm_player_stats_map_progress_style, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Map progress bar style (0=bottom bar, 1=HUD overlay)")
MACRO_CONFIG_COL(QmPlayerStatsMapProgressColor, qm_player_stats_map_progress_color, 0xFF24C764, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Map progress bar color")
MACRO_CONFIG_INT(QmPlayerStatsMapProgressWidth, qm_player_stats_map_progress_width, 28, 10, 80, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Map progress bar width (percentage of screen width)")
MACRO_CONFIG_INT(QmPlayerStatsMapProgressHeight, qm_player_stats_map_progress_height, 10, 6, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Map progress bar height")
MACRO_CONFIG_INT(QmPlayerStatsMapProgressPosX, qm_player_stats_map_progress_pos_x, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Map progress bar horizontal position (center, % of screen width)")
MACRO_CONFIG_INT(QmPlayerStatsMapProgressPosY, qm_player_stats_map_progress_pos_y, 97, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Map progress bar vertical position (top edge, % of screen height)")
MACRO_CONFIG_INT(QmPlayerStatsMapProgressDbgRoute, qm_player_stats_map_progress_dbg_route, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show map progress test point route")
MACRO_CONFIG_INT(QmPlayerStatsResetOnJoin, qm_player_stats_reset_on_join, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reset stats on server join (0=persistent, 1=reset on join)")

// Switch Countdown - 开关倒计时
MACRO_CONFIG_INT(QmSwitchCountdown, qm_switch_countdown, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable switch countdown")
MACRO_CONFIG_INT(QmSwitchCountdownMode, qm_switch_countdown_mode, 1, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Switch countdown position (0=follow Tee, 1=Dynamic Island, 2=both)")

// Hook Countdown - 钩子倒计时（蓝色环，跟随 Tee）
MACRO_CONFIG_INT(QmHookCountdown, qm_hook_countdown, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable hook countdown")

// HUD Dynamic Island - 灵动岛/HUD 编辑器
MACRO_CONFIG_INT(QmHudIslandUseOriginalStyle, qm_hud_island_use_original_style, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use original style for Dynamic Island")
MACRO_CONFIG_INT(QmHudIslandShowTeam, qm_hud_island_show_team, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show team on HUD Dynamic Island")
MACRO_CONFIG_COL(QmHudIslandBgColor, qm_hud_island_bg_color, 0x9C460E, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dynamic Island background color")
MACRO_CONFIG_INT(QmHudIslandBgOpacity, qm_hud_island_bg_opacity, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dynamic Island background alpha")
MACRO_CONFIG_STR(QmHudEditorLayout, qm_hud_editor_layout, 2048, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD editor layout")

// Camera / View - 相机、视野
MACRO_CONFIG_INT(QmCameraDrift, qm_camera_drift, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable camera drift effect, slightly drags camera based on speed")
MACRO_CONFIG_INT(QmCameraDriftAmount, qm_camera_drift_amount, 50, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Camera drift intensity (0-200)")
MACRO_CONFIG_INT(QmCameraDriftSmoothness, qm_camera_drift_smoothness, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Camera drift smoothness (0=instant, 100=smoothest)")
MACRO_CONFIG_INT(QmCameraDriftReverse, qm_camera_drift_reverse, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Invert camera drift direction")
MACRO_CONFIG_INT(QmDynamicFov, qm_dynamic_fov, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable dynamic FOV, wider view at higher speed")
MACRO_CONFIG_INT(QmDynamicFovAmount, qm_dynamic_fov_amount, 50, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dynamic FOV intensity (0-200)")
MACRO_CONFIG_INT(QmDynamicFovSmoothness, qm_dynamic_fov_smoothness, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dynamic FOV smoothness (0=instant, 100=smoothest)")
MACRO_CONFIG_INT(QmCinematicCamera, qm_cinematic_camera, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable smooth cinematic camera while free spectating")
MACRO_CONFIG_INT(QmZoomInstantReverse, qm_zoom_instant_reverse, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reverse zoom direction instantly when the opposite zoom key is pressed (0=keep original smooth zoom)")
MACRO_CONFIG_INT(QmCrashReportOnStartup, qm_crash_report_on_startup, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show pending crash reports in a window at startup (0 = keep them on disk and log a line instead)")
MACRO_CONFIG_INT(QmAspectPreset, qm_aspect_preset, 0, 0, 6, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Aspect ratio preset (0=off, 1=5:4, 2=4:3, 3=3:2, 4=16:9, 5=21:9, 6=custom)")
MACRO_CONFIG_INT(QmAspectRatio, qm_aspect_ratio, 178, 100, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Custom aspect ratio, stored as x100 (e.g. 178=16:9, 233=21:9)")

// Misc visual - 其他视觉效果
MACRO_CONFIG_INT(QmJellyTee, qm_jelly_tee, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable squishy Tee deformation")
MACRO_CONFIG_INT(QmJellyTeeOthers, qm_jelly_tee_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply squishy Tee deformation to other players")
MACRO_CONFIG_INT(QmJellyTeeStrength, qm_jelly_tee_strength, 500, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Squishy Tee deformation intensity")
MACRO_CONFIG_INT(QmJellyTeeDuration, qm_jelly_tee_duration, 30, 1, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Squishy Tee deformation duration")
MACRO_CONFIG_INT(Qm3DParticles, qm_3d_particles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable QmClient style background 3D particles")
MACRO_CONFIG_INT(Qm3DParticlesType, qm_3d_particles_type, 1, 1, 9, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background 3D particle type: 1=Cube, 2=Heart, 3=Mix, 4=Sphere, 5=Pyramid, 6=Diamond, 7=Torus, 8=Star, 9=Crescent")
MACRO_CONFIG_INT(Qm3DParticlesCount, qm_3d_particles_count, 45, 1, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background 3D particle count")
MACRO_CONFIG_INT(Qm3DParticlesSpeed, qm_3d_particles_speed, 18, 1, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background 3D particle base speed")
MACRO_CONFIG_INT(Qm3DParticlesSizeMin, qm_3d_particles_size_min, 4, 2, 64, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background 3D particle minimum size")
MACRO_CONFIG_INT(Qm3DParticlesSizeMax, qm_3d_particles_size_max, 10, 2, 64, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background 3D particle maximum size")
MACRO_CONFIG_INT(Qm3DParticlesDepth, qm_3d_particles_depth, 300, 10, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background 3D particle depth range")
MACRO_CONFIG_INT(Qm3DParticlesViewMargin, qm_3d_particles_view_margin, 120, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background 3D particle margin outside view")
MACRO_CONFIG_INT(Qm3DParticlesAlpha, qm_3d_particles_alpha, 30, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background 3D particle opacity")
MACRO_CONFIG_INT(Qm3DParticlesFadeInMs, qm_3d_particles_fade_in_ms, 400, 1, 5000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background 3D particle fade-in time")
MACRO_CONFIG_INT(Qm3DParticlesFadeOutMs, qm_3d_particles_fade_out_ms, 400, 1, 5000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background 3D particle fade-out time")
MACRO_CONFIG_INT(Qm3DParticlesCollide, qm_3d_particles_collide, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background 3D particle collision")
MACRO_CONFIG_INT(Qm3DParticlesPushRadius, qm_3d_particles_push_radius, 120, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Player push radius for background 3D particles")
MACRO_CONFIG_INT(Qm3DParticlesPushStrength, qm_3d_particles_push_strength, 120, 0, 2000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Player push strength for background 3D particles")
MACRO_CONFIG_INT(Qm3DParticlesColorMode, qm_3d_particles_color_mode, 1, 1, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background 3D particle color mode: 1=Custom, 2=Random")
MACRO_CONFIG_COL(Qm3DParticlesColor, qm_3d_particles_color, 0xFF8FB89E, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Background 3D particle custom color")
MACRO_CONFIG_INT(Qm3DParticlesGlow, qm_3d_particles_glow, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable background 3D particle glow")
MACRO_CONFIG_INT(Qm3DParticlesGlowAlpha, qm_3d_particles_glow_alpha, 35, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background 3D particle glow opacity")
MACRO_CONFIG_INT(Qm3DParticlesGlowOffset, qm_3d_particles_glow_offset, 2, 1, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background 3D particle glow offset")
MACRO_CONFIG_INT(Qm3DParticlesTrail, qm_3d_particles_trail, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable background 3D particle trail")
MACRO_CONFIG_INT(Qm3DParticlesTrailLength, qm_3d_particles_trail_length, 4, 2, 6, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background 3D particle trail length")
MACRO_CONFIG_INT(Qm3DParticlesTrailAlpha, qm_3d_particles_trail_alpha, 45, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background 3D particle trail opacity")
MACRO_CONFIG_INT(Qm3DParticlesPulse, qm_3d_particles_pulse, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable background 3D particle pulse scaling")
MACRO_CONFIG_INT(Qm3DParticlesPulseStrength, qm_3d_particles_pulse_strength, 15, 0, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background 3D particle pulse intensity")
MACRO_CONFIG_INT(Qm3DParticlesPulseSpeed, qm_3d_particles_pulse_speed, 100, 10, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background 3D particle pulse speed")
MACRO_CONFIG_INT(Qm3DParticlesTwinkle, qm_3d_particles_twinkle, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable background 3D particle flicker")
MACRO_CONFIG_INT(Qm3DParticlesTwinkleStrength, qm_3d_particles_twinkle_strength, 35, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Background 3D particle flicker intensity")
MACRO_CONFIG_INT(QmShowTuneZoneColors, qm_show_tune_zone_colors, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Color map tune zones by their tune zone number")
MACRO_CONFIG_INT(QmBlankAssetFallback, qm_blank_asset_fallback, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically fall back to the default asset when a custom asset sprite is fully transparent; turn off to keep blank sprites invisible (e.g. to hide effects)")
MACRO_CONFIG_INT(QmShowSpectatorGhosts, qm_show_spectator_ghosts, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show semi-transparent ghost tees for other players who are spectating")
MACRO_CONFIG_INT(QmSpectatorGhostAlpha, qm_spectator_ghost_alpha, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Opacity of the ghost tees shown for players who are spectating (0 = fully transparent)")

// Skin queue - 皮肤队列
MACRO_CONFIG_INT(QmSkinQueueEnabled, qm_skin_queue_enabled, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable skin queue rotation")
MACRO_CONFIG_INT(QmSkinQueueInterval, qm_skin_queue_interval, 600, 0, 120000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Skin queue switch interval (ms, 0=no timed rotation, random start only)")
MACRO_CONFIG_INT(QmSkinQueueLength, qm_skin_queue_length, 20, 0, 1024, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Skin queue max length")
MACRO_CONFIG_INT(QmSkinQueueIndex, qm_skin_queue_index, 0, 0, 1024, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Skin queue current position")
MACRO_CONFIG_INT(QmSkinQueueRotateMap, qm_skin_queue_rotate_map, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-fetch all players' skins for rotation queue")
MACRO_CONFIG_INT(QmSkinQueueRandomJoin, qm_skin_queue_random_join, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Start skin queue from a random position when entering a map")
MACRO_CONFIG_INT(QmDummySkinQueueEnabled, qm_dummy_skin_queue_enabled, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable dummy skin queue rotation")
MACRO_CONFIG_INT(QmDummySkinQueueInterval, qm_dummy_skin_queue_interval, 600, 0, 120000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dummy skin queue switch interval (ms, 0=no timed rotation, random start only)")
MACRO_CONFIG_INT(QmDummySkinQueueLength, qm_dummy_skin_queue_length, 20, 0, 1024, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dummy skin queue max length")
MACRO_CONFIG_INT(QmDummySkinQueueIndex, qm_dummy_skin_queue_index, 0, 0, 1024, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dummy skin queue current position")
MACRO_CONFIG_INT(QmDummySkinQueueRotateMap, qm_dummy_skin_queue_rotate_map, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-fetch all players' skins for dummy rotation queue")
MACRO_CONFIG_INT(QmDummySkinQueueRandomJoin, qm_dummy_skin_queue_random_join, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Start dummy skin queue from a random position when entering a map")

// Settings performance - 性能
MACRO_CONFIG_INT(QmSettingsPrewarm, qm_settings_prewarm, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Pre-warm settings page on startup and menu idle")

// Chat Bubble Settings - 聊天气泡
MACRO_CONFIG_INT(QmChatSaveDraft, qm_chat_save_draft, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keep unsent message on chat close")
MACRO_CONFIG_INT(QmMessageMerge, qm_message_merge, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Merge consecutive identical player messages within 2 seconds")
// echo 合并的滑动窗口：同一段 echo 文本在窗口内连续重复时只保留一次并计数。
// 该行为始终生效，不受 qm_message_merge 影响；设为 0 关闭合并。
MACRO_CONFIG_INT(QmEchoMergeWindowMs, qm_echo_merge_window_ms, 2000, 0, 60000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Merge consecutive identical echo messages within this window in milliseconds (0 disables merging)")
MACRO_CONFIG_INT(QmChatHideSystemPrefix, qm_chat_hide_system_prefix, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide the *** prefix before server chat messages")
MACRO_CONFIG_INT(QmChatAnimSlideOut, qm_chat_anim_slide_out, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable left swipe offset on chat fade")
MACRO_CONFIG_INT(QmChatAnimFadeDurationMs, qm_chat_anim_fade_duration_ms, 300, 0, 2000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat fade smooth time (milliseconds)")
MACRO_CONFIG_INT(QmHideChatBubbles, qm_hide_chat_bubbles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide own chat bubble (only when authenticated in remote console)")
MACRO_CONFIG_INT(QmChatBubble, qm_chat_bubble, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show chat bubbles above players")
MACRO_CONFIG_INT(QmChatBubbleDuration, qm_chat_bubble_duration, 10, 1, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat bubble display duration (seconds)")
MACRO_CONFIG_INT(QmChatBubbleAlpha, qm_chat_bubble_alpha, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat bubble transparency (0-100)")
MACRO_CONFIG_INT(QmChatBubbleFontSize, qm_chat_bubble_font_size, 20, 8, 32, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat bubble font size")
MACRO_CONFIG_COL(QmChatBubbleBgColor, qm_chat_bubble_bg_color, 404232960, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Chat bubble background color")
MACRO_CONFIG_COL(QmChatBubbleTextColor, qm_chat_bubble_text_color, 4294967295, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat bubble text color")
MACRO_CONFIG_INT(QmChatBubbleAnimation, qm_chat_bubble_animation, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat bubble disappear animation (0=Fade, 1=Shrink, 2=Slide up)")

MACRO_CONFIG_INT(QmComboPopup, qm_combo_popup, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show combo hint when hooking or hammering player within 2 seconds")

// Dummy Mini View - 分身小窗
MACRO_CONFIG_INT(QmDummyMiniView, qm_dummy_miniview, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show dummy mini view window")
MACRO_CONFIG_INT(QmDummyMiniViewAuto, qm_dummy_miniview_auto, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show dummy mini view only when other tee leaves current view")
MACRO_CONFIG_INT(QmDummyMiniViewSize, qm_dummy_miniview_size, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dummy mini view size (percent)")
MACRO_CONFIG_INT(QmDummyMiniViewZoom, qm_dummy_miniview_zoom, 100, 10, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dummy mini view zoom (percent)")

// System Media Controls - 系统媒体控件
MACRO_CONFIG_INT(QmSmtcEnable, qm_smtc_enable, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable system media transport control integration")
MACRO_CONFIG_INT(QmRankGhostShowDirection, qm_rank_ghost_show_direction, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show direction key indicators above rank ghost members in view mode")
MACRO_CONFIG_INT(QmSmtcShowHud, qm_smtc_show_hud, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show system media info on HUD")
MACRO_CONFIG_INT(QmNeteaseHookEnable, qm_netease_hook_enable, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable Netease Music hook integration")
MACRO_CONFIG_INT(QmNeteaseHookTimeoutMs, qm_netease_hook_timeout_ms, 1500, 250, 10000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Netease hook heartbeat timeout (milliseconds)")
MACRO_CONFIG_STR(QmNeteaseHookHelperPath, qm_netease_hook_helper_path, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Netease hook helper path (empty=beside QmClient)")

// 网易云歌词展示开关。关闭展示时后台桥接仍可继续采集并维护当前歌曲状态。
MACRO_CONFIG_INT(QmLyrics, qm_lyrics, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable Netease lyric integration")
MACRO_CONFIG_INT(QmLyricsInMediaIsland, qm_lyrics_in_media_island, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show current Netease lyric in Media Island")

// 汽水音乐 Hook 集成(共享同一套歌词展示开关)。
// 与网易云 Hook 互斥:默认关闭,用户在 Lyrics 设置里切换。
MACRO_CONFIG_INT(QmSodaHookEnable, qm_soda_hook_enable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable SodaMusic hook integration")
MACRO_CONFIG_INT(QmSodaHookTimeoutMs, qm_soda_hook_timeout_ms, 1500, 250, 10000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "SodaMusic hook heartbeat timeout (milliseconds)")
MACRO_CONFIG_STR(QmSodaHookHelperPath, qm_soda_hook_helper_path, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "SodaMusic hook helper path (empty=beside QmClient)")

// 酷狗与 QQ 音乐 Hook 的启用开关与采集参数(共用独立采集 helper)。
// 启用位同时被卡片目录的歌词卡用于计算高度与重测版本
// (QmCardCatalogHud.cpp:59,86,248,250,均为度量/布局用途,不渲染开关)。
// 超时与 helper 路径供集成层按当前来源选择(见 SyncHookConfiguration)。
// 与既有 Hook 同样互斥:默认关闭,用户在 Lyrics 设置里切换。
MACRO_CONFIG_INT(QmKugouHookEnable, qm_kugou_hook_enable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable Kugou Music hook integration")
MACRO_CONFIG_INT(QmKugouHookTimeoutMs, qm_kugou_hook_timeout_ms, 1500, 250, 10000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Kugou hook heartbeat timeout (milliseconds)")
MACRO_CONFIG_STR(QmKugouHookHelperPath, qm_kugou_hook_helper_path, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Kugou hook helper path (empty=beside QmClient)")
MACRO_CONFIG_INT(QmQQMusicHookEnable, qm_qqmusic_hook_enable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable QQ Music hook integration")
MACRO_CONFIG_INT(QmQQMusicHookTimeoutMs, qm_qqmusic_hook_timeout_ms, 1500, 250, 10000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "QQ Music hook heartbeat timeout (milliseconds)")
MACRO_CONFIG_STR(QmQQMusicHookHelperPath, qm_qqmusic_hook_helper_path, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "QQ Music hook helper path (empty=beside QmClient)")

// Spotify 歌词链路(纯网络:sp_dc → TOTP token → color-lyrics,LRCLIB 兜底)。
// 共享同一套歌词展示开关(qm_lyrics / qm_lyrics_in_media_island)。
MACRO_CONFIG_INT(QmSpotifyEnable, qm_spotify_enable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable Spotify lyric integration")
MACRO_CONFIG_STR(QmSpotifySpDc, qm_spotify_sp_dc, 1024, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Spotify sp_dc cookie (from browser DevTools, long-lived)")

// Translate - 翻译模块
MACRO_CONFIG_STR(QmTranslateBackend, qm_translate_backend, 32, "llm", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Translation backend (llm/tencentcloud/libretranslate/ftapi)")
MACRO_CONFIG_STR(QmTranslateTarget, qm_translate_target, 16, "zh", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Target language code (e.g. zh, en, ja, zh-TW)")
MACRO_CONFIG_INT(QmTranslateAuto, qm_translate_auto, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-translate incoming messages")
MACRO_CONFIG_INT(QmTranslateLocalDetectMinChars, qm_translate_local_detect_min_chars, 2, 1, 12, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Minimum characters for local target language detection")
MACRO_CONFIG_INT(QmTranslateLocalDetectRatio, qm_translate_local_detect_ratio, 75, 50, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Ratio threshold for local target language detection")
MACRO_CONFIG_INT(QmTranslateFtapiAutoEnable, qm_translate_ftapi_auto_enable, 0, 0, 1,
	CFGFLAG_CLIENT | CFGFLAG_SAVE,
	"Allow FTAPI auto-translate (may cause overload)")

// Translate - LLM API (OpenAI 兼容，默认智谱AI预设)
MACRO_CONFIG_INT(QmTranslateLlmProvider, qm_translate_llm_provider, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "LLM Provider (0=ZhipuAI, 1=DeepSeek, 2=OpenAI, 3=Custom)")

// 各 Provider 的模型配置（切换 Provider 时自动切换对应模型）
MACRO_CONFIG_STR(QmTranslateLlmModelZhipu, qm_translate_llm_model_zhipu, 32, "glm-4.5-flash", CFGFLAG_CLIENT | CFGFLAG_SAVE, "ZhipuAI model name")
MACRO_CONFIG_STR(QmTranslateLlmModelDeepseek, qm_translate_llm_model_deepseek, 32, "deepseek-chat", CFGFLAG_CLIENT | CFGFLAG_SAVE, "DeepSeek model name")
MACRO_CONFIG_STR(QmTranslateLlmModelOpenai, qm_translate_llm_model_openai, 32, "gpt-4o-mini", CFGFLAG_CLIENT | CFGFLAG_SAVE, "OpenAI model name")
MACRO_CONFIG_STR(QmTranslateLlmModelCustom, qm_translate_llm_model_custom, 32, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Custom Provider model name")

// 各 Provider 的端点配置（留空使用默认端点）
MACRO_CONFIG_STR(QmTranslateLlmEndpointZhipu, qm_translate_llm_endpoint_zhipu, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "ZhipuAI endpoint (leave empty for default)")
MACRO_CONFIG_STR(QmTranslateLlmEndpointDeepseek, qm_translate_llm_endpoint_deepseek, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "DeepSeek endpoint (leave empty for default)")
MACRO_CONFIG_STR(QmTranslateLlmEndpointOpenai, qm_translate_llm_endpoint_openai, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "OpenAI endpoint (leave empty for default)")
MACRO_CONFIG_STR(QmTranslateLlmEndpointCustom, qm_translate_llm_endpoint_custom, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Custom Provider endpoint")

// 各 Provider 的 API Key
MACRO_CONFIG_STR(QmTranslateLlmKeyZhipu, qm_translate_llm_key_zhipu, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "ZhipuAI API Key")
MACRO_CONFIG_STR(QmTranslateLlmKeyDeepseek, qm_translate_llm_key_deepseek, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "DeepSeek API Key")
MACRO_CONFIG_STR(QmTranslateLlmKeyOpenai, qm_translate_llm_key_openai, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "OpenAI API Key")
MACRO_CONFIG_STR(QmTranslateLlmKeyCustom, qm_translate_llm_key_custom, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Custom Provider API Key")

MACRO_CONFIG_INT(QmTranslateLlmConcurrency, qm_translate_llm_concurrency, 0, 0, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "LLM translation concurrency (0=auto)")
MACRO_CONFIG_INT(QmTranslateLlmConcurrencyDefault, qm_translate_llm_concurrency_default, 3, 1, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "LLM translation default concurrency (smart adjustment)")
MACRO_CONFIG_INT(QmTranslateLlmEnableThinking, qm_translate_llm_enable_thinking, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable LLM thinking mode (may increase response time)")
MACRO_CONFIG_STR(QmTranslateSystemPrompt, qm_translate_system_prompt, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Custom translation prompt (overrides built-in)")

// Translate - Source/Target Language - 目标语言
MACRO_CONFIG_STR(QmTranslateSource, qm_translate_source, 16, "auto", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Source language code for translation (auto=auto-detect)")

// Translate - Auto Outgoing - 自动翻译发送消息
MACRO_CONFIG_INT(QmTranslateAutoOutgoing, qm_translate_auto_outgoing, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-translate outgoing messages")
MACRO_CONFIG_INT(QmTranslateAutoOutgoingMode, qm_translate_auto_outgoing_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-translation mode (0=Trigger on common source languages only, 1=Always translate)")
MACRO_CONFIG_STR(QmTranslateOutgoingTarget, qm_translate_outgoing_target, 16, "en", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Outgoing translation target language code")

// Translate - Tencent Cloud - 腾讯云
MACRO_CONFIG_STR(QmTranslateTcEndpoint, qm_translate_tc_endpoint, 256, "https://tmt.tencentcloudapi.com/", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tencent Cloud Translation endpoint")
MACRO_CONFIG_STR(QmTranslateTcSecretId, qm_translate_tc_secret_id, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tencent Cloud Translation SecretId")
MACRO_CONFIG_STR(QmTranslateTcSecretKey, qm_translate_tc_secret_key, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tencent Cloud Translation SecretKey")
MACRO_CONFIG_STR(QmTranslateTcRegion, qm_translate_tc_region, 32, "ap-guangzhou", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tencent Cloud Translation region")

// Translate - LibreTranslate
MACRO_CONFIG_STR(QmTranslateLibreEndpoint, qm_translate_libre_endpoint, 256, "http://localhost:5000", CFGFLAG_CLIENT | CFGFLAG_SAVE, "LibreTranslate endpoint")
MACRO_CONFIG_STR(QmTranslateLibreKey, qm_translate_libre_key, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "LibreTranslate API Key")

// Translate Button Colors - 翻译按钮自定义颜色
MACRO_CONFIG_INT(QmTranslateColorAlphaMigrated, qm_translate_color_alpha_migrated, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Translation color alpha migration completed flag")
MACRO_CONFIG_COL(QmTranslateBtnColorDisabled, qm_translate_btn_color_disabled, 0xD1000029, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Translation button cancel color")
MACRO_CONFIG_COL(QmTranslateBtnColorEnabled, qm_translate_btn_color_enabled, 0xE69E5E86, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Translation button active color")
MACRO_CONFIG_COL(QmTranslateMenuBgColor, qm_translate_menu_bg_color, 0xF200001F, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Translation menu background color")
MACRO_CONFIG_COL(QmTranslateMenuOptionSelected, qm_translate_menu_option_selected, 0xE69E5E86, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Translation menu selected color")
MACRO_CONFIG_COL(QmTranslateMenuOptionNormal, qm_translate_menu_option_normal, 0xE6000033, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Translation menu normal color")

// Jump Hint / 跳跃提示 - 根据位置小数部分显示跳跃速查表（由 tc_jump_hint 迁移而来）
MACRO_CONFIG_INT(QmJumpHint, qm_jump_hint, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show jump hint based on fractional part of position")
MACRO_CONFIG_INT(QmJumpHintDefaultsMigrated, qm_jump_hint_defaults_migrated, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Jump hint defaults migration completed flag")
MACRO_CONFIG_STR(QmJumpHintText, qm_jump_hint_text, 512, "三格边缘跳:\\n左起跳: .34|.31|.16\\n左二段跳: .41|.28|.25|.13\\n右起跳: .63|.66|.81\\n右二段跳: .56|.69|.72|.84", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Jump hint text (use \\n for newline)")
MACRO_CONFIG_COL(QmJumpHintColor, qm_jump_hint_color, 255, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Jump hint color")
MACRO_CONFIG_INT(QmJumpHintX, qm_jump_hint_x, 20, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Jump hint horizontal position (% of screen width)")
MACRO_CONFIG_INT(QmJumpHintY, qm_jump_hint_y, 5, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Jump hint vertical position (% of screen height)")
MACRO_CONFIG_INT(QmJumpHintSize, qm_jump_hint_size, 10, 0, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Jump hint font size")

// Friends - 好友
MACRO_CONFIG_INT(QmFriendAutoFollowDelay, qm_friend_auto_follow_delay, 3, 0, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-follow friend server switch delay (seconds)")
