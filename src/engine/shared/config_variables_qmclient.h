// 请抬头享受阳光｜日子很好 我很我---------致咩子
// This file can be included several times.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(ConfigName, ScriptName, Def, Min, Max, Save, Desc) ;
#define MACRO_CONFIG_COL(ConfigName, ScriptName, Def, Save, Desc) ;
#define MACRO_CONFIG_STR(ConfigName, ScriptName, Len, Def, Save, Desc) ;
#endif

// QmClient specific variables - 栖梦客户端配置项

// 往返探针无法区分上下行丢包；仅作为默认关闭的灰测功能。
MACRO_CONFIG_INT(QmUpstreamRedundancy, qm_upstream_redundancy, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Experimental: repeats outgoing UDP game packets when round-trip probes go unanswered. Upload traffic may triple; download loss and late duplicate inputs can still cause problems. Disabled with KCP.")
MACRO_CONFIG_INT(QmUpstreamRedundancyMaxCopies, qm_upstream_redundancy_max_copies, 3, 2, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Maximum number of copies per outgoing UDP game packet")

// 地图编辑器
MACRO_CONFIG_INT(QmEditorCollabFollowPlacement, qm_editor_collab_follow_placement, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Follow remote placements in the collaborative editor")

// Log / 日志
MACRO_CONFIG_INT(QmSteamAutoLaunch, qm_steam_auto_launch, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically launch Steam when the client is started externally, so Steam can track your playtime")
MACRO_CONFIG_STR(QmSteamClientPath, qm_steam_client_path, 1024, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Steam client path (leave empty to detect automatically)")
MACRO_CONFIG_INT(QmConsoleFilterMask, qm_console_filter_mask, 15, 0, 15, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Local console log category filter mask (bit flags)")
MACRO_CONFIG_INT(QmConsoleFontSize, qm_console_font_size, 10, 1, 24, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Console font size")
// 0=现代终端 1=经典 PowerShell 2=自定义；3 起为 Windows Terminal 内置配色
//（顺序见 console_color_schemes.h，只在末尾追加，不重排）。
MACRO_CONFIG_INT(QmConsoleColorScheme, qm_console_color_scheme, 0, 0, 18, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Console color scheme: 0=modern terminal, 1=classic PowerShell, 2=custom, 3+=Windows Terminal schemes")
MACRO_CONFIG_INT(QmConsoleOpacity, qm_console_opacity, 96, 20, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Console background opacity")
MACRO_CONFIG_INT(QmConsoleHighlightCommands, qm_console_highlight_commands, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Highlight console commands")
MACRO_CONFIG_COL(QmConsoleBackgroundColor, qm_console_background_color, 0x00000C, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Console background color")
MACRO_CONFIG_COL(QmConsoleTextColor, qm_console_text_color, 0x0000CC, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Console text color")
MACRO_CONFIG_COL(QmConsoleCommandColor, qm_console_command_color, 0x26DFCF, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Console command color")
MACRO_CONFIG_COL(QmConsoleParameterColor, qm_console_parameter_color, 0x80969C, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Console parameter color")
MACRO_CONFIG_COL(QmConsoleStringColor, qm_console_string_color, 0x4E6AB9, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Console string color")
MACRO_CONFIG_COL(QmConsoleNumberColor, qm_console_number_color, 0xCB8DC5, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Console number color")
MACRO_CONFIG_COL(QmConsoleLinkColor, qm_console_link_color, 0x95FFB6, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Console link color")
MACRO_CONFIG_COL(QmConsoleSearchColor, qm_console_search_color, 0x07DAB2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Console search match color")
MACRO_CONFIG_COL(QmConsoleSearchSelectedColor, qm_console_search_selected_color, 0x26DFCF, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Console selected search match color")
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
MACRO_CONFIG_INT(QmDefaultsProfileVersion, qm_defaults_profile_version, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Default appearance profile migration version")
MACRO_CONFIG_STR(QmNewsReadRelease, qm_news_read_release, 64, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Last viewed client release notes")
MACRO_CONFIG_INT(QmNewsReadVersion, qm_news_read_version, -1, -1, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Last viewed announcement version")
MACRO_CONFIG_STR(QmNewsReadContentId, qm_news_read_content_id, 65, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Last viewed announcement content revision")
MACRO_CONFIG_INT(QmUiListEntryAnimations, qm_ui_list_entry_animations, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Animate lists when entering a page")
MACRO_CONFIG_INT(QmUiCardHeightAnimations, qm_ui_card_height_animations, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Animate settings card expand and collapse height changes")
MACRO_CONFIG_INT(QmUiCardReflowAnimations, qm_ui_card_reflow_animations, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Animate settings card reorder and layout reflow")
MACRO_CONFIG_INT(QmExtraAnimations, qm_extra_animations, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Animate chat box, emote selector, scoreboard, and spectate selection")
MACRO_CONFIG_INT(QmUiScrollLegacyWheel, qm_ui_scroll_legacy_wheel, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Restore the original wheel scroll feel: each tick restarts the smooth scroll without inheriting velocity")
MACRO_CONFIG_INT(QmUiScrollStep, qm_ui_scroll_step, 10, 2, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Wheel scroll step in UI pixels per tick; settings card pages scroll 12x this per tick")
MACRO_CONFIG_INT(QmCountryFlagAnim, qm_country_flag_anim, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable bounce-in entrance animation when country flags finish loading")
MACRO_CONFIG_INT(QmUiCardRainbowTitles, qm_ui_card_rainbow_titles, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Settings card titles use rainbow colors")
MACRO_CONFIG_INT(QmUiCardBorders, qm_ui_card_borders, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show settings card borders")
MACRO_CONFIG_COL(QmUiCardBorderColor, qm_ui_card_border_color, 0x1AFFFFFF, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Settings card border color")
MACRO_CONFIG_COL(QmUiCardColor, qm_ui_card_color, 0xFF00FF, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Settings card background color")
MACRO_CONFIG_INT(QmUiCardOpacity, qm_ui_card_opacity, 13, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Settings card background transparency")
MACRO_CONFIG_INT(QmUiIconColor, qm_ui_icon_color, 1, 1, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Qm UI icon color: 1=White, 2=Black, 3=Custom, 4=Rainbow")
MACRO_CONFIG_INT(QmUiIconCustomColorEnabled, qm_ui_icon_custom_color_enabled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Qm UI custom icon color")
MACRO_CONFIG_COL(QmUiIconCustomColor, qm_ui_icon_custom_color, 0xFFFFFF, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Qm UI custom icon color")
MACRO_CONFIG_COL(QmUiFriendIconColor, qm_ui_friend_icon_color, 0x00D1AB, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Friend icon color")
MACRO_CONFIG_COL(QmUiFavoriteIconColor, qm_ui_favorite_icon_color, 0x21FFA6, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Favorite icon color")
MACRO_CONFIG_INT(QmUiFriendIconFilled, qm_ui_friend_icon_filled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Draw the friend heart icon with the filled icon font style")
MACRO_CONFIG_INT(QmUiFavoriteIconFilled, qm_ui_favorite_icon_filled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Draw the favorite star icon with the filled icon font style")
MACRO_CONFIG_INT(QmUiIconWeight, qm_ui_icon_weight, 1, 0, 5, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Qm UI icon style: 0=Regular, 1=Bold, 2=Light (legacy), 3=Fill, 4=Light, 5=Bold (legacy)")
MACRO_CONFIG_INT(QmHudIslandSignalColorMode, qm_hud_island_signal_color_mode, 1, 1, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dynamic island connection icon color: 1=Quality tier, 2=Follow global icon color, 3=Custom")
MACRO_CONFIG_COL(QmHudIslandSignalColor, qm_hud_island_signal_color, 0x00D1AB, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dynamic island connection icon custom color")
MACRO_CONFIG_INT(QmUiColorInterpolation, qm_ui_color_interpolation, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "QmUi color animation interpolation: 0=sRGB linear, 1=OKLAB perceptually uniform")
MACRO_CONFIG_INT(QmRectCornerSegments, qm_rect_corner_segments, 16, 8, 48, CFGFLAG_CLIENT | CFGFLAG_SAVE, "UI rounded corner segments (even numbers recommended)")
MACRO_CONFIG_STR(QmGlobalCardOrder, qm_global_card_order, 8000, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Global card ordering (format: stableId|tab|col|order; semicolon-separated)")
MACRO_CONFIG_STR(QmSettingsCardCollapsed, qm_settings_card_collapsed, 8000, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Settings card states: stableId for collapsed, !stableId for expanded, separated by semicolons")
MACRO_CONFIG_STR(QmSettingsCardWidths, qm_settings_card_widths, 8000, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Settings card widths: stableId for full width, !stableId for half width, separated by semicolons")
MACRO_CONFIG_INT(QmSettingsCardCollapseMigrated, qm_settings_card_collapse_migrated, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Legacy settings card collapse states migrated")
MACRO_CONFIG_INT(QmCardOrderMigrated, qm_card_order_migrated, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Global card ordering old config migration completed flag")
MACRO_CONFIG_INT(QmCardLayoutVersion, qm_card_layout_version, 0, 0, 13, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Settings card default layout migration version")
MACRO_CONFIG_STR(QmSettingsCardOrder, qm_settings_card_order, 2048, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tclient settings card ordering (format: id:col:order; semicolon-separated)")
MACRO_CONFIG_INT(DbgQmUiDogfood, dbg_qm_ui_dogfood, 0, 0, 1, CFGFLAG_CLIENT, "Show feat-003 shared UI primitives dogfood page (takes over QmClient settings page, for visual verification of 11 primitives + spring/easing animations)")
MACRO_CONFIG_COL(QmUiColor, qm_ui_color, 0x000000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Interface surface color")
MACRO_CONFIG_COL(QmUiFocusColor, qm_ui_focus_color, 0x97FFA6, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keyboard focus ring color")
MACRO_CONFIG_COL(QmUiAccentColor, qm_ui_accent_color, 0x5D00FF, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Interface accent color")
MACRO_CONFIG_INT(QmUiAccentOpacity, qm_ui_accent_opacity, 24, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Interface accent color opacity percentage")
MACRO_CONFIG_COL(QmUiSelectedColor, qm_ui_selected_color, 0x8F061D, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Selected item color")
MACRO_CONFIG_COL(QmScoreboardColor, qm_scoreboard_color, 0x000000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Scoreboard surface color")
MACRO_CONFIG_INT(QmUiOpacity, qm_ui_opacity, 12, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Interface transparency")
MACRO_CONFIG_INT(QmColorPickerShape, qm_color_picker_shape, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Color picker saturation/value area shape: 0=square, 1=wheel")
MACRO_CONFIG_STR(QmColorPickerPalette, qm_color_picker_palette, 4096, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Color picker saved swatches (format: name|#hex entries separated by semicolons)")
MACRO_CONFIG_COL(QmTooltipBackgroundColor, qm_tooltip_background_color, 0xD9000066, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Tooltip background color")
MACRO_CONFIG_COL(QmTooltipTextColor, qm_tooltip_text_color, 0xFF0000FF, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Tooltip text color")
MACRO_CONFIG_INT(QmTooltipFontSize, qm_tooltip_font_size, 14, 10, 24, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tooltip font size")
MACRO_CONFIG_INT(QmTooltipAnimation, qm_tooltip_animation, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tooltip bounce animation")
MACRO_CONFIG_COL(QmUiDropdownColor, qm_ui_dropdown_color, 0x000000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Button background color")
MACRO_CONFIG_INT(QmUiDropdownOpacity, qm_ui_dropdown_opacity, 16, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Button background opacity percentage")
MACRO_CONFIG_COL(QmUiInputColor, qm_ui_input_color, 0x0000FF, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Input background color")
MACRO_CONFIG_INT(QmUiInputOpacity, qm_ui_input_opacity, 16, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Input background opacity percentage")
MACRO_CONFIG_COL(QmUiDropdownListColor, qm_ui_dropdown_list_color, 0x0000FF, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Expanded dropdown background color")
MACRO_CONFIG_INT(QmUiDropdownListOpacity, qm_ui_dropdown_list_opacity, 12, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Expanded dropdown background opacity percentage")
MACRO_CONFIG_INT(QmUiTextColorMode, qm_ui_text_color_mode, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Text color mode: 0 automatic, 1 white, 2 black, 3 custom")
MACRO_CONFIG_COL(QmUiTextCustomColor, qm_ui_text_custom_color, 0x0000FF, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Custom text color")
MACRO_CONFIG_COL(QmUiPopupColor, qm_ui_popup_color, 0x0000FF, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Secondary menu background color")
MACRO_CONFIG_INT(QmUiPopupOpacity, qm_ui_popup_opacity, 20, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Secondary menu background opacity percentage")
MACRO_CONFIG_INT(QmMapBrowserEmptyOnly, qm_map_browser_empty_only, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Map browser: show empty servers only")
MACRO_CONFIG_INT(QmMapBrowserFavoriteOnly, qm_map_browser_favorite_only, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Map browser: show favorite maps only")
MACRO_CONFIG_INT(QmMapBrowserStarMask, qm_map_browser_star_mask, 0, 0, 62, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Map browser difficulty star filter bitmask (bits 1-5)")
// 留空沿用原目录；自定义路径只影响之后开始保存的文件。
MACRO_CONFIG_STR(QmDemoDirectory, qm_demo_directory, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Demo directory")
MACRO_CONFIG_STR(QmVideoDirectory, qm_video_directory, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Video directory")
MACRO_CONFIG_STR(QmScreenshotDirectory, qm_screenshot_directory, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Screenshot directory")
// Demo 导出转换管线：固定转换线程数 + 有界帧队列（反压不丢帧），替代按 CPU 核数分配线程与全尺寸缓冲。
MACRO_CONFIG_INT(QmVideoConvertThreads, qm_video_convert_threads, 3, 1, 8, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Number of video pixel-format conversion threads for demo export (2-4 recommended)")
MACRO_CONFIG_INT(QmVideoQueueFrames, qm_video_queue_frames, 8, 2, 32, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Bounded capture frame queue depth for demo export (higher values use more memory)")
// 分段渲染（M3）：0 = 整段导出；>0 时按该秒数切片，每段独立封装并记录进度，中断后可跳过已完成片段续渲。
MACRO_CONFIG_INT(QmVideoSegmentSeconds, qm_video_segment_seconds, 300, 0, 3600, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Demo export segment length in seconds (0 = single file, 300 = 5 minutes)")
MACRO_CONFIG_INT(QmVideoSegmentWarmup, qm_video_segment_warmup, 100, 0, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Demo ticks played without recording before each segment to restore effects/chat state (50 ticks = 1 second)")
// 编码器选择（M4）：空 = 默认软件编码器（libx264）；否则为 FFmpeg 编码器名（如 h264_nvenc），必须经运行时探测可用。
MACRO_CONFIG_STR(QmVideoEncoder, qm_video_encoder, 64, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Video encoder for demo export (empty = default software encoder, e.g. h264_nvenc)")
// 统一质量值（M4）：0-51（CRF/CQ/QP 同刻度，越大质量越低）；-1 = 沿用 cl_video_crf。按所选编码器家族映射到原生参数。
MACRO_CONFIG_INT(QmVideoQuality, qm_video_quality, -1, -1, 51, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Unified export quality (CRF/CQ/QP scale 0-51, -1 = use cl_video_crf); mapped to each encoder's native parameter")
// 导出预设（M6）：0=自定义，1=日常分享(libx264/mp4)，2=高效分享(hevc/mp4，优先硬件)，
// 3=剪辑(全帧内 H.264/mp4)，4=交换格式(ProRes/mov)，5=最高画质(全帧内 H.264 4:4:4/mp4)，
// 6=最快速度(硬件优先 H.264/mp4)。
// 剪辑预设用全帧内 H.264：实测同画质下体积约为 ProRes 422 HQ 的 1/17，随机定位等价（两者都全内帧）。
// 最高画质在剪辑之上把质量压到 14（肉眼无损档）；最快速度用 h264 家族别名（无硬件时回落软件）+ 更高 CQ。
// DNxHR 与 FFV1 已移除（用户明确不需要归档格式）。项目尚未发布，不做旧索引迁移。
MACRO_CONFIG_INT(QmVideoPreset, qm_video_preset, 0, 0, 6, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Export preset: 0=custom, 1=daily(libx264/mp4), 2=efficient share(hevc/mp4), 3=editing(all-intra H.264/mp4), 4=exchange(ProRes/mov), 5=best quality(all-intra H.264 4:4:4/mp4), 6=fastest(hardware h264/mp4)")
// 色度/位深偏好（M6.5）：0=自动（按编码器家族），1=4:2:0，2=4:2:2，3=4:4:4。
// 渲染读回是 8bit RGBA，提升位深不产生新信息；4:4:4 提高的是色度采样密度（细线、彩色小字、描边）。
MACRO_CONFIG_INT(QmVideoPixelFormat, qm_video_pixfmt, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chroma sampling preference for export: 0=auto, 1=4:2:0, 2=4:2:2, 3=4:4:4 (higher keeps thin colored details)")
// ProRes profile：0=Proxy 1=LT 2=422 3=HQ 4=4444 5=4444XQ；-1=按像素格式自动（444 用 4444，其余 HQ）。
MACRO_CONFIG_INT(QmVideoProresProfile, qm_video_prores_profile, -1, -1, 5, CFGFLAG_CLIENT | CFGFLAG_SAVE, "ProRes profile override: -1=auto (4444 for 4:4:4, HQ otherwise), 0=Proxy, 1=LT, 2=422, 3=HQ, 4=4444, 5=4444XQ")
// 输出容器（M6）：自定义预设使用，mp4/mov/mkv；后缀与 libavformat 容器按文件名匹配。
MACRO_CONFIG_STR(QmVideoContainer, qm_video_container, 8, "mp4", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Custom export container: mp4, mov or mkv")
// 关键帧间隔（M4）：编码器 GOP；0 = 用编码器默认，1 = 全帧内（每帧都是关键帧，剪辑任意切点）。
// 分段渲染每段独立编码器实例，段首帧恒为 IDR，不受此值影响。
// NVENC 拒绝 gop_size=1，会被降到 2（隔帧关键帧）；需要真正全帧内时请选软件编码器。
MACRO_CONFIG_INT(QmVideoGop, qm_video_gop, 60, 0, 600, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keyframe interval (GOP) in frames for demo export (0 = encoder default, 1 = all-intra, 60 = recommended)")
// 输出分辨率（M7 超采样）：0 = 跟随窗口渲染分辨率。小于渲染分辨率时以渲染分辨率绘制再经
// swscale LANCZOS 下采样（例如 2560x1440 窗口导出 1280x720 = 2x 超采样，改善 2D 边缘锯齿）；
// 大于窗口时按输出尺寸离屏渲染（真分辨率导出，输出宽高比即为画面宽高比）。
MACRO_CONFIG_INT(QmVideoWidth, qm_video_width, 0, 0, 7680, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Export output width in pixels (0 = follow window render size; smaller than window = supersampled downscale)")
MACRO_CONFIG_INT(QmVideoHeight, qm_video_height, 0, 0, 4320, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Export output height in pixels (0 = follow window render size; smaller than window = supersampled downscale)")
// 导出抗锯齿（与全局 gfx_fsaa_samples 分家）：-1 = 跟随全局（读窗口帧缓冲，窗口开 AA 导出就带 AA）；
// 0 = 关（即使窗口开了 FSAA，导出也强制单采样）；2/4/8 = 显式档位，整段录制画进离屏 MSAA 目标，
// 后端不支持或像素预算超限时自动降档。输出大于窗口时必然离屏渲染，跟随全局则按全局档位采样。
MACRO_CONFIG_INT(QmVideoFsaa, qm_video_fsaa, -1, -1, 8, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Anti-aliasing for video export: -1 = follow gfx_fsaa_samples, 0 = off, 2/4/8 = multisampled offscreen rendering")

MACRO_CONFIG_INT(QmScreenshotClipboard, qm_screenshot_clipboard, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically copy manual screenshots to clipboard")
MACRO_CONFIG_INT(QmScreenshotWatermark, qm_screenshot_watermark, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically add a watermark to screenshots")
MACRO_CONFIG_INT(QmScreenshotWatermarkTimestamp, qm_screenshot_watermark_timestamp, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Screenshot watermark: include timestamp")
MACRO_CONFIG_INT(QmScreenshotWatermarkMap, qm_screenshot_watermark_map, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Screenshot watermark: include map name")
MACRO_CONFIG_INT(QmScreenshotWatermarkPosition, qm_screenshot_watermark_position, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Screenshot watermark position: 0=bottom-left, 1=bottom-right, 2=top-left, 3=top-right")
MACRO_CONFIG_STR(QmScreenshotWatermarkText, qm_screenshot_watermark_text, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Screenshot watermark custom text")
MACRO_CONFIG_INT(QmScoreboardOpacity, qm_scoreboard_opacity, 16, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Scoreboard transparency")
MACRO_CONFIG_INT(QmShowOutdatedVersionWarning, qm_show_outdated_version_warning, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show outdated version warning")
MACRO_CONFIG_STR(QmUpdateRecentSource, qm_update_recent_source, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Last verified approved update source")
MACRO_CONFIG_INT(QmAutoUpdate, qm_auto_update, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically check for stable updates and install them on exit")
MACRO_CONFIG_INT(QmImeAutoManage, qm_ime_auto_manage, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto enable/disable IME on text focus")
MACRO_CONFIG_INT(QmNewIme, qm_new_ime, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable new IME candidate bar")
MACRO_CONFIG_COL(QmImeBgColor, qm_ime_bg_color, 0x1C1C1E, CFGFLAG_CLIENT | CFGFLAG_SAVE, "New IME candidate bar background color")
MACRO_CONFIG_INT(QmImeOpacity, qm_ime_opacity, 96, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "New IME candidate bar opacity")
MACRO_CONFIG_COL(QmImeTextColor, qm_ime_text_color, 0xF50000FF, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "IME text color")
MACRO_CONFIG_COL(QmImeSelectedTextColor, qm_ime_selected_text_color, 0xFF98D68E, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "IME selected text color")
MACRO_CONFIG_COL(QmImeSelectedColor, qm_ime_selected_color, 0x4799FFA1, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "IME selection background color")
MACRO_CONFIG_INT(QmImeFontSize, qm_ime_font_size, 100, 75, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME font size percentage")
MACRO_CONFIG_STR(QmImeBgGradient, qm_ime_bg_gradient, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient colors")
MACRO_CONFIG_INT(QmImeBgGradientType, qm_ime_bg_gradient_type, 0, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient type (0=linear, 1=radial, 2=angular, 3=reflected, 4=diamond)")
MACRO_CONFIG_INT(QmImeBgGradientAngle, qm_ime_bg_gradient_angle, 0, 0, 360, CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient angle in degrees")
MACRO_CONFIG_INT(QmImeBgGradientCenterX, qm_ime_bg_gradient_center_x, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient horizontal position percentage")
MACRO_CONFIG_INT(QmImeBgGradientCenterY, qm_ime_bg_gradient_center_y, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient vertical position percentage")
MACRO_CONFIG_INT(QmImeBgGradientRange, qm_ime_bg_gradient_range, 100, 10, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient range percentage")
MACRO_CONFIG_INT(QmImeBgGradientReverse, qm_ime_bg_gradient_reverse, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reverse IME gradient colors")
MACRO_CONFIG_STR(QmImeTextGradient, qm_ime_text_gradient, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient colors")
MACRO_CONFIG_INT(QmImeTextGradientType, qm_ime_text_gradient_type, 0, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient type (0=linear, 1=radial, 2=angular, 3=reflected, 4=diamond)")
MACRO_CONFIG_INT(QmImeTextGradientAngle, qm_ime_text_gradient_angle, 0, 0, 360, CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient angle in degrees")
MACRO_CONFIG_INT(QmImeTextGradientCenterX, qm_ime_text_gradient_center_x, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient horizontal position percentage")
MACRO_CONFIG_INT(QmImeTextGradientCenterY, qm_ime_text_gradient_center_y, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient vertical position percentage")
MACRO_CONFIG_INT(QmImeTextGradientRange, qm_ime_text_gradient_range, 100, 10, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient range percentage")
MACRO_CONFIG_INT(QmImeTextGradientReverse, qm_ime_text_gradient_reverse, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reverse IME gradient colors")
MACRO_CONFIG_STR(QmImeSelectedTextGradient, qm_ime_selected_text_gradient, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient colors")
MACRO_CONFIG_INT(QmImeSelectedTextGradientType, qm_ime_selected_text_gradient_type, 0, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient type (0=linear, 1=radial, 2=angular, 3=reflected, 4=diamond)")
MACRO_CONFIG_INT(QmImeSelectedTextGradientAngle, qm_ime_selected_text_gradient_angle, 0, 0, 360, CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient angle in degrees")
MACRO_CONFIG_INT(QmImeSelectedTextGradientCenterX, qm_ime_selected_text_gradient_center_x, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient horizontal position percentage")
MACRO_CONFIG_INT(QmImeSelectedTextGradientCenterY, qm_ime_selected_text_gradient_center_y, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient vertical position percentage")
MACRO_CONFIG_INT(QmImeSelectedTextGradientRange, qm_ime_selected_text_gradient_range, 100, 10, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient range percentage")
MACRO_CONFIG_INT(QmImeSelectedTextGradientReverse, qm_ime_selected_text_gradient_reverse, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reverse IME gradient colors")
MACRO_CONFIG_STR(QmImeSelectedGradient, qm_ime_selected_gradient, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient colors")
MACRO_CONFIG_INT(QmImeSelectedGradientType, qm_ime_selected_gradient_type, 0, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient type (0=linear, 1=radial, 2=angular, 3=reflected, 4=diamond)")
MACRO_CONFIG_INT(QmImeSelectedGradientAngle, qm_ime_selected_gradient_angle, 0, 0, 360, CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient angle in degrees")
MACRO_CONFIG_INT(QmImeSelectedGradientCenterX, qm_ime_selected_gradient_center_x, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient horizontal position percentage")
MACRO_CONFIG_INT(QmImeSelectedGradientCenterY, qm_ime_selected_gradient_center_y, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient vertical position percentage")
MACRO_CONFIG_INT(QmImeSelectedGradientRange, qm_ime_selected_gradient_range, 100, 10, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "IME gradient range percentage")
MACRO_CONFIG_INT(QmImeSelectedGradientReverse, qm_ime_selected_gradient_reverse, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reverse IME gradient colors")
MACRO_CONFIG_INT(QmAutoSaveHistoryCount, qm_auto_save_history_count, 100, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-save history count (0=disable)")
MACRO_CONFIG_INT(QmShortServerNames, qm_short_server_names, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show short server names in browser (applies to China community servers)")
MACRO_CONFIG_INT(QmPingCacheMaxAgeHours, qm_ping_cache_max_age_hours, 72, 0, 8760, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Discard cached server pings older than this many hours (0 = never expire)")
MACRO_CONFIG_INT(QmSavesAutoRemoveUsed, qm_saves_auto_remove_used, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically remove a local save record after it has been loaded successfully (0 = keep and mark as used, 1 = remove)")
MACRO_CONFIG_INT(QmSkinSortMode, qm_skin_sort_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Skin list sort mode (0=Name, 1=Release date)")
MACRO_CONFIG_INT(QmSkinShowMetadata, qm_skin_show_metadata, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show skin release date and author")
MACRO_CONFIG_INT(QmSkinOutlineLocal, qm_skin_outline_local, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show generated skin outline for local tees")
MACRO_CONFIG_INT(QmSkinOutlineOthers, qm_skin_outline_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show generated skin outline for other tees")
MACRO_CONFIG_COL(QmSkinOutlineColor, qm_skin_outline_color, 0xFFFFFFFF, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Generated skin outline color")
MACRO_CONFIG_INT(QmSkinOutlineWidth, qm_skin_outline_width, 2, 1, 6, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Generated skin outline width")
MACRO_CONFIG_INT(QmSkinOutlineAlpha, qm_skin_outline_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Generated skin outline opacity")
MACRO_CONFIG_INT(QmWaterHammerHighlight, qm_water_hammer_highlight, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Highlight teammates holding hammer in death or freeze areas")

// QmVulkan 扩展：SDF 常驻，不再提供关闭 SDF/增强管线的配置。
MACRO_CONFIG_INT(QmEnhancedProceduralRing, qm_enhanced_procedural_ring, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use procedural ring pipeline")

// UI / 界面
MACRO_CONFIG_INT(QmGaussianBlur, qm_gaussian_blur, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable backdrop blur for translucent interface and selected HUD backgrounds")
MACRO_CONFIG_INT(QmBlurMode, qm_blur_mode, 2, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Backdrop blur algorithm: 0=Gaussian, 1=Kawase, 2=Dual Kawase")

// Sponsor nudge / 赞助提醒
MACRO_CONFIG_INT(QmLaunchCount, qm_launch_count, 0, 0, 1000000000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Accumulated client launch count (used by the sponsor reminder)")
MACRO_CONFIG_INT(QmSponsorNudgeAt, qm_sponsor_nudge_at, 14, 1, 100000000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Launch count threshold for the next sponsor reminder")
MACRO_CONFIG_INT(QmSponsorNudge, qm_sponsor_nudge, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show the occasional sponsor reminder in the main menu")

// Scoreboard / 计分板
MACRO_CONFIG_INT(QmScoreboardPoints, qm_scoreboard_points, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Scoreboard score lookup")
MACRO_CONFIG_INT(QmScoreboardPlaytime, qm_scoreboard_playtime, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show total playtime from DDStats instead of points in scoreboard")
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
MACRO_CONFIG_INT(QmChatSystemGradientType, qm_chat_system_gradient_type, 0, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient type (0=linear, 1=radial, 2=angular, 3=reflected, 4=diamond)")
MACRO_CONFIG_INT(QmChatSystemGradientAngle, qm_chat_system_gradient_angle, 0, 0, 360, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient angle in degrees")
MACRO_CONFIG_INT(QmChatSystemGradientCenterX, qm_chat_system_gradient_center_x, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient horizontal position percentage")
MACRO_CONFIG_INT(QmChatSystemGradientCenterY, qm_chat_system_gradient_center_y, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient vertical position percentage")
MACRO_CONFIG_INT(QmChatSystemGradientRange, qm_chat_system_gradient_range, 100, 10, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient range percentage")
MACRO_CONFIG_INT(QmChatSystemGradientReverse, qm_chat_system_gradient_reverse, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reverse chat gradient colors")
MACRO_CONFIG_INT(QmChatClientGradientType, qm_chat_client_gradient_type, 0, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient type (0=linear, 1=radial, 2=angular, 3=reflected, 4=diamond)")
MACRO_CONFIG_INT(QmChatClientGradientAngle, qm_chat_client_gradient_angle, 0, 0, 360, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient angle in degrees")
MACRO_CONFIG_INT(QmChatClientGradientCenterX, qm_chat_client_gradient_center_x, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient horizontal position percentage")
MACRO_CONFIG_INT(QmChatClientGradientCenterY, qm_chat_client_gradient_center_y, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient vertical position percentage")
MACRO_CONFIG_INT(QmChatClientGradientRange, qm_chat_client_gradient_range, 100, 10, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient range percentage")
MACRO_CONFIG_INT(QmChatClientGradientReverse, qm_chat_client_gradient_reverse, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reverse chat gradient colors")
MACRO_CONFIG_INT(QmChatHighlightGradientType, qm_chat_highlight_gradient_type, 0, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient type (0=linear, 1=radial, 2=angular, 3=reflected, 4=diamond)")
MACRO_CONFIG_INT(QmChatHighlightGradientAngle, qm_chat_highlight_gradient_angle, 0, 0, 360, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient angle in degrees")
MACRO_CONFIG_INT(QmChatHighlightGradientCenterX, qm_chat_highlight_gradient_center_x, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient horizontal position percentage")
MACRO_CONFIG_INT(QmChatHighlightGradientCenterY, qm_chat_highlight_gradient_center_y, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient vertical position percentage")
MACRO_CONFIG_INT(QmChatHighlightGradientRange, qm_chat_highlight_gradient_range, 100, 10, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient range percentage")
MACRO_CONFIG_INT(QmChatHighlightGradientReverse, qm_chat_highlight_gradient_reverse, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reverse chat gradient colors")
MACRO_CONFIG_INT(QmChatTeamGradientType, qm_chat_team_gradient_type, 0, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient type (0=linear, 1=radial, 2=angular, 3=reflected, 4=diamond)")
MACRO_CONFIG_INT(QmChatTeamGradientAngle, qm_chat_team_gradient_angle, 0, 0, 360, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient angle in degrees")
MACRO_CONFIG_INT(QmChatTeamGradientCenterX, qm_chat_team_gradient_center_x, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient horizontal position percentage")
MACRO_CONFIG_INT(QmChatTeamGradientCenterY, qm_chat_team_gradient_center_y, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient vertical position percentage")
MACRO_CONFIG_INT(QmChatTeamGradientRange, qm_chat_team_gradient_range, 100, 10, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient range percentage")
MACRO_CONFIG_INT(QmChatTeamGradientReverse, qm_chat_team_gradient_reverse, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reverse chat gradient colors")
MACRO_CONFIG_INT(QmChatFriendGradientType, qm_chat_friend_gradient_type, 0, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient type (0=linear, 1=radial, 2=angular, 3=reflected, 4=diamond)")
MACRO_CONFIG_INT(QmChatFriendGradientAngle, qm_chat_friend_gradient_angle, 0, 0, 360, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient angle in degrees")
MACRO_CONFIG_INT(QmChatFriendGradientCenterX, qm_chat_friend_gradient_center_x, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient horizontal position percentage")
MACRO_CONFIG_INT(QmChatFriendGradientCenterY, qm_chat_friend_gradient_center_y, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient vertical position percentage")
MACRO_CONFIG_INT(QmChatFriendGradientRange, qm_chat_friend_gradient_range, 100, 10, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient range percentage")
MACRO_CONFIG_INT(QmChatFriendGradientReverse, qm_chat_friend_gradient_reverse, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reverse chat gradient colors")
MACRO_CONFIG_INT(QmChatNormalGradientType, qm_chat_normal_gradient_type, 0, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient type (0=linear, 1=radial, 2=angular, 3=reflected, 4=diamond)")
MACRO_CONFIG_INT(QmChatNormalGradientAngle, qm_chat_normal_gradient_angle, 0, 0, 360, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient angle in degrees")
MACRO_CONFIG_INT(QmChatNormalGradientCenterX, qm_chat_normal_gradient_center_x, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient horizontal position percentage")
MACRO_CONFIG_INT(QmChatNormalGradientCenterY, qm_chat_normal_gradient_center_y, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient vertical position percentage")
MACRO_CONFIG_INT(QmChatNormalGradientRange, qm_chat_normal_gradient_range, 100, 10, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat gradient range percentage")
MACRO_CONFIG_INT(QmChatNormalGradientReverse, qm_chat_normal_gradient_reverse, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reverse chat gradient colors")
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
MACRO_CONFIG_INT(QmHitboxShowFreezeProjectiles, qm_hitbox_show_freeze_projectiles, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Freeze projectile collision volume")
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
MACRO_CONFIG_INT(QmPieMenuEffects, qm_pie_menu_effects, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Pie menu opening and closing effects")
MACRO_CONFIG_COL(QmPieMenuSelectedColor, qm_pie_menu_selected_color, 0x0099FFA1, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Pie menu highlight tint (alpha controls tint strength)")
MACRO_CONFIG_INT(QmPieMenuOpacity, qm_pie_menu_opacity, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Menu opacity (0-100)")
MACRO_CONFIG_STR(QmPieMenuRenameQueue, qm_pie_menu_rename_queue, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Pie menu rename list (| separates entries; main,dummy renames both)")
MACRO_CONFIG_INT(QmPieMenuColorFriend, qm_pie_menu_color_friend, 0xE64D66BF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Friend option color")
MACRO_CONFIG_INT(QmPieMenuColorWhisper, qm_pie_menu_color_whisper, 0x8059B3BF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Whisper option color")
MACRO_CONFIG_INT(QmPieMenuColorMention, qm_pie_menu_color_mention, 0xD98033BF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Mention option color")
MACRO_CONFIG_INT(QmPieMenuColorCopySkin, qm_pie_menu_color_copy_skin, 0x408CCCBF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Copy skin option color")
MACRO_CONFIG_INT(QmPieMenuColorSwap, qm_pie_menu_color_swap, 0xCC4D4DBF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Swap option color")
MACRO_CONFIG_INT(QmPieMenuColorSpectate, qm_pie_menu_color_spectate, 0x738C99BF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Spectate option color")
MACRO_CONFIG_INT(QmPieMenuFriendEnabled, qm_pie_menu_friend_enabled, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show friend option")
MACRO_CONFIG_INT(QmPieMenuWhisperEnabled, qm_pie_menu_whisper_enabled, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show whisper option")
MACRO_CONFIG_INT(QmPieMenuMentionEnabled, qm_pie_menu_mention_enabled, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show mention option")
MACRO_CONFIG_INT(QmPieMenuCopySkinEnabled, qm_pie_menu_copy_skin_enabled, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show copy skin option")
MACRO_CONFIG_INT(QmPieMenuSwapEnabled, qm_pie_menu_swap_enabled, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show swap option")
MACRO_CONFIG_INT(QmPieMenuSpectateEnabled, qm_pie_menu_spectate_enabled, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show spectate option")
MACRO_CONFIG_INT(QmPieMenuInviteTeamEnabled, qm_pie_menu_invite_team_enabled, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show team invite option")
MACRO_CONFIG_INT(QmPieMenuJoinTeamEnabled, qm_pie_menu_join_team_enabled, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show join team option")
MACRO_CONFIG_INT(QmPieMenuFollowEnabled, qm_pie_menu_follow_enabled, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show cross-server follow option")
MACRO_CONFIG_INT(QmPieMenuScoreEnabled, qm_pie_menu_score_enabled, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show player score option")
MACRO_CONFIG_INT(QmPieMenuCopyNameEnabled, qm_pie_menu_copy_name_enabled, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show copy name option")
MACRO_CONFIG_STR(QmPieFollowName, qm_pie_follow_name, 64, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Player to follow across servers until cancelled; empty disables following")
MACRO_CONFIG_STR(QmPieFollowClan, qm_pie_follow_clan, 64, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Clan of the player followed from the pie menu")
MACRO_CONFIG_INT(QmPieMenuColorInviteTeam, qm_pie_menu_color_invite_team, 0xE67A4DBF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Invite team option color")
MACRO_CONFIG_INT(QmPieMenuColorJoinTeam, qm_pie_menu_color_join_team, 0x4D9BE6BF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Join team option color")
MACRO_CONFIG_INT(QmPieMenuColorFollow, qm_pie_menu_color_follow, 0x4DBF80BF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Follow option color")
MACRO_CONFIG_INT(QmPieMenuColorScore, qm_pie_menu_color_score, 0xB06BE6BF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Score option color")
MACRO_CONFIG_INT(QmPieMenuColorCopyName, qm_pie_menu_color_copy_name, 0xBF808099, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Copy name option color")

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
MACRO_CONFIG_COL(QmTeamTeeGlowColor, qm_team_tee_glow_color, 0xFFFFFFFF, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Team 0 tee glow custom color")
MACRO_CONFIG_INT(QmTeamTeeGlowStrength, qm_team_tee_glow_strength, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Team tee glow intensity in percent")
MACRO_CONFIG_INT(QmTeamTeeGlowSize, qm_team_tee_glow_size, 40, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Team tee glow halo size in percent")
MACRO_CONFIG_INT(QmRandomEmoteOnHit, qm_random_emote_on_hit, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Random emote when hit by hammer/grenade")
MACRO_CONFIG_INT(QmEmoticonShadow, qm_emoticon_shadow, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Draw shadow behind emote")
MACRO_CONFIG_INT(QmEmoticonProjectileDuration, qm_emoticon_projectile_duration, 5, 1, 10, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Emoticon projectile duration")
MACRO_CONFIG_INT(QmShowOtherSuperEmotes, qm_show_other_super_emotes, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show other players' large emoticons")
MACRO_CONFIG_INT(QmShowOtherLaunchEmotes, qm_show_other_launch_emotes, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show other players' launched emoticons")
MACRO_CONFIG_INT(QmTitleBadgeStyle, qm_title_badge_style, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Sponsor title badge: 0 = moon, 1 = sun")
MACRO_CONFIG_COL(QmTitleBadgeColor, qm_title_badge_color, 0xC3638E, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Overall sponsor title badge color")
MACRO_CONFIG_INT(QmTitleInstantHints, qm_title_instant_hints, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show title setting hints immediately in small text")
MACRO_CONFIG_INT(QmTitleColorMode, qm_title_color_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Bracketed title color: 0 = follow server, 1 = single color, 2 = rainbow")
MACRO_CONFIG_INT(QmTitleOpacity, qm_title_opacity, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Sponsor title badge opacity (0-100)")
MACRO_CONFIG_INT(QmTitleStyleEnabled, qm_title_style_enabled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use animated title style (overrides qm_title_color_mode)")
MACRO_CONFIG_STR(QmTitleStyle, qm_title_style, 32, "exotic_rainbow", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Animated title style id")
MACRO_CONFIG_INT(QmSponsorChatStyle, qm_sponsor_chat_style, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Sponsor chat style draft: 0=Normal, 1=Platinum highlight")
MACRO_CONFIG_INT(QmSponsorChatEffects, qm_sponsor_chat_effects, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show synchronized sponsor chat styles")
MACRO_CONFIG_INT(QmChatSponsorBadges, qm_chat_sponsor_badges, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show sponsor badges in chat")
MACRO_CONFIG_INT(QmTitleMotionMode, qm_title_motion_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Title motion: 0 = vertical wave, 1 = pop wave")
MACRO_CONFIG_INT(QmTitleBobAmplitude, qm_title_bob_amplitude, 1, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Title wave strength (0-3, 0 = off)")
MACRO_CONFIG_INT(QmTitlePhase, qm_title_phase, 20, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Title per-character phase per pixel in 1/1000 px (0 = style default, 20 = visible light band)")
MACRO_CONFIG_INT(QmTitleEffect, qm_title_effect, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Title spatial effect: 0 = polished, 1 = solid, 2 = classic Calamity, 3 = off")
MACRO_CONFIG_INT(QmTitleShimmerSpeed, qm_title_shimmer_speed, 60, 0, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Title light sweep speed in 1/100 row per second (0 = off)")
MACRO_CONFIG_INT(QmTitleBobWavelength, qm_title_bob_wavelength, 320, 16, 1024, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Title bob wavelength in pixels")
MACRO_CONFIG_INT(QmTitleBobSpeed, qm_title_bob_speed, 150, 0, 2000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Title bob angular speed in 1/100 rad/s")
MACRO_CONFIG_INT(QmTitleBobPixelSnap, qm_title_bob_pixel_snap, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Snap title bob offset to whole pixels (sharper glyphs, choppier motion)")
MACRO_CONFIG_INT(QmShowNameplateTitle, qm_show_nameplate_title, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show QmClient title in player nameplate (0 = hide)")
MACRO_CONFIG_INT(QmNameplateTitleScale, qm_nameplate_title_scale, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Nameplate title size")
MACRO_CONFIG_INT(QmShowMainTitle, qm_show_main_title, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show main tee title locally")
MACRO_CONFIG_INT(QmShowDummyTitle, qm_show_dummy_title, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show dummy title locally")
MACRO_CONFIG_INT(QmNameplateTitleAboveName, qm_nameplate_title_above_name, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Place title on a separate line above the player name instead of inline")
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
MACRO_CONFIG_INT(QmAutoUnspecBothOnUnfreeze, qm_auto_unspec_both_on_unfreeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Exit spectating on both tees when either is hammered awake by another player")

// Auto-Switch on Unfreeze / HJ大佬辅助 - 自动切换到解冻的Tee
MACRO_CONFIG_INT(QmAutoSwitchOnUnfreeze, qm_auto_switch_on_unfreeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-switch to first unfrozen when both frozen")
MACRO_CONFIG_INT(QmAutoCloseChatOnUnfreeze, qm_auto_close_chat_on_unfreeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-close chat after unfreeze")
MACRO_CONFIG_INT(QmFreezeWakeupPopup, qm_freeze_wakeup_popup, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show hint at random top-left/right of player when main or dummy is hammered awake")
MACRO_CONFIG_INT(QmAutoTeamLock, qm_auto_team_lock, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-lock after joining lockable team")
MACRO_CONFIG_INT(QmAutoTeamLockDelay, qm_auto_team_lock_delay, 5, 0, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-lock delay (seconds)")
MACRO_CONFIG_INT(QmAutoAcceptTeamInvite, qm_auto_accept_team_invite, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-accept team invites by sending /team <n>")
MACRO_CONFIG_INT(QmSpectatorFriendsFirst, qm_spectator_friends_first, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "List friends first in the spectator menu")
// HJ 大佬辅助：淡化水中的旁观实体，保留既有配置键。
MACRO_CONFIG_INT(QmPausedSpectatorFade, qm_paused_spectator_fade, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Fade spectators in water")
MACRO_CONFIG_INT(QmPausedSpectatorAlpha, qm_paused_spectator_alpha, 40, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Spectator opacity (percent)")

// Input Overlay / 输入叠加
MACRO_CONFIG_INT(QmInputOverlay, qm_input_overlay, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show input overlay")
MACRO_CONFIG_INT(QmInputOverlayKeyCounts, qm_input_overlay_key_counts, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show key press counts")
MACRO_CONFIG_COL(QmInputOverlayCountColor, qm_input_overlay_count_color, 0xFF0000FF, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Key press count color")
MACRO_CONFIG_INT(QmInputOverlayCountSize, qm_input_overlay_count_size, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Key press count font size percentage")
MACRO_CONFIG_STR(QmInputOverlayCountLabel, qm_input_overlay_count_label, 32, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Text before each key press count")
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
MACRO_CONFIG_INT(QmSoloSplitRestoreTeam, qm_solo_split_restore_team, 0, 0, 63, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Team used when leaving solo split mode")
MACRO_CONFIG_INT(QmSoloSplitLinkDummy, qm_solo_split_link_dummy, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Connect dummy automatically for solo split mode")
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
MACRO_CONFIG_STR(QmStreamerWordsFile, qm_streamer_words_file, 1024, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Streamer word list file")
MACRO_CONFIG_INT(QmStreamerFilterWords, qm_streamer_filter_words, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Filter sensitive words")
MACRO_CONFIG_INT(QmStreamerHideNames, qm_streamer_hide_names, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide non-friend names/clans and show client ID")
MACRO_CONFIG_INT(QmStreamerHideSkins, qm_streamer_hide_skins, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Non-friends use default skin")
MACRO_CONFIG_INT(QmStreamerScoreboardDefaultFlags, qm_streamer_scoreboard_default_flags, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show default flag in scoreboard")

// 好友状态 / 上线 / 进服提醒（默认文案为英文 source key，运行时 Localize）
MACRO_CONFIG_INT(QmFriendLastSeen, qm_friend_last_seen, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show last seen dates for offline friends using DDStats (updated daily, matched by player name)")
MACRO_CONFIG_INT(QmFriendOnlineNotify, qm_friend_online_notify, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Friend online notification")
MACRO_CONFIG_INT(QmFriendOnlineAutoRefresh, qm_friend_online_auto_refresh, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-refresh server list for friend notifications")
MACRO_CONFIG_INT(QmFriendOnlineRefreshSeconds, qm_friend_online_refresh_seconds, 30, 5, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Friend notification refresh interval (sec)")
MACRO_CONFIG_INT(QmFriendEnterAutoGreet, qm_friend_enter_auto_greet, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto greet friend on map join")
MACRO_CONFIG_INT(QmFriendEnterBroadcast, qm_friend_enter_broadcast, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show friend join in large text")
MACRO_CONFIG_STR(QmFriendEnterBroadcastText, qm_friend_enter_broadcast_text, 128, "%s joined this server", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Large text for friend join (use %s for friend name)")
MACRO_CONFIG_STR(QmFriendEnterGreetText, qm_friend_enter_greet_text, 128, "Hi!", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto greet friend text")

// Block Words / 屏蔽词
MACRO_CONFIG_INT(QmPublicBlockWords, qm_public_block_words, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use public word filter")
MACRO_CONFIG_INT(QmBlockWordsEnabled, qm_block_words_enabled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable word filter list")
MACRO_CONFIG_INT(QmWarListBlockEnemyChat, qm_warlist_block_enemy_chat, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide chat messages from players marked as enemies")
MACRO_CONFIG_INT(QmWarListBlockEnemies, qm_warlist_block_enemies, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide enemy chat, replace enemy names with client IDs and use default skins; keep local players visible")
MACRO_CONFIG_INT(QmBlockWordsShowConsole, qm_block_words_show_console, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show filtered words in console")
MACRO_CONFIG_COL(QmBlockWordsConsoleColor, qm_block_words_console_color, 0xFFFFFFFF, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Filtered word console color")
MACRO_CONFIG_INT(QmBlockWordsMultiReplace, qm_block_words_multi_replace, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Replace censored words with length-based chars")
MACRO_CONFIG_INT(QmBlockWordsAction, qm_block_words_action, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Word filter action: 0=replace matching words, 1=hide entire message")
MACRO_CONFIG_INT(QmBlockWordsMode, qm_block_words_mode, 2, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Replace mode 0=Regex 1=Full 2=Both")
MACRO_CONFIG_STR(QmBlockWordsReplacementChar, qm_block_words_replacement_char, 8, "*", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Censor replacement character")
// 大词表由聊天组件分段写入同一 cfg，不能按一个超长命令保存。
MACRO_CONFIG_STR(QmBlockWordsList, qm_block_words_list, 65536, "", CFGFLAG_CLIENT, "Censor word list (comma-separated)")
MACRO_CONFIG_STR(QmBlockWordsFile, qm_block_words_file, 1024, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat word list file")
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
MACRO_CONFIG_INT(QmNameplateAdvanced, qm_nameplate_advanced, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show advanced nameplate settings without changing effect values")
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
// 仅渲染 demo：导出时隐藏 HUD、聊天、旁观信息与键鼠/钩索提示，但仍允许切视角和调整速度。
MACRO_CONFIG_INT(QmVideoRenderDemoOnly, qm_video_render_demo_only, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render demo content only during video export (hide HUD, chat and spectator overlays)")
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
MACRO_CONFIG_INT(QmAxiomAutoLogin, qm_axiom_auto_login, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-login after entering Axiom community server")
MACRO_CONFIG_INT(QmAxiomQuadPrediction, qm_axiom_quad_prediction, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Predict moving blocks and freeze quads on servers using Axiom quad rules (experimental)")
MACRO_CONFIG_STR(QmAxiomLoginPassword, qm_axiom_login_password, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Password for Axiom main account auto-login")
MACRO_CONFIG_STR(QmAxiomDummyLoginPassword, qm_axiom_dummy_login_password, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Password for Axiom alt account auto-login")

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

// Player Stats HUD - 玩家统计面板
MACRO_CONFIG_INT(QmPlayerStatsHud, qm_player_stats_hud, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable player statistics panel")
MACRO_CONFIG_INT(QmPlayerStatsMapProgress, qm_player_stats_map_progress, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show map progress bar (beta)")
MACRO_CONFIG_INT(QmPlayerStatsMapProgressStyle, qm_player_stats_map_progress_style, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Map progress bar location (0=standalone, 1=player stats, 2=Dynamic Island)")
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

// Hook Countdown - 钩链中点的普通钩倒计时与无限钩标记
MACRO_CONFIG_INT(QmHookCountdown, qm_hook_countdown, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable hook countdown")
MACRO_CONFIG_INT(QmHookCountdownSize, qm_hook_countdown_size, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hook countdown size percentage")
MACRO_CONFIG_INT(QmHookCountdownEndlessStyle, qm_hook_countdown_endless_style, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Endless hook indicator style (0=infinity symbol, 1=rainbow ring)")
MACRO_CONFIG_COL(QmHookCountdownColor, qm_hook_countdown_color, 0x0092FF99, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hook countdown ring color")
MACRO_CONFIG_COL(QmHookCountdownWarningColor, qm_hook_countdown_warning_color, 0x001BFF93, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hook countdown warning color")
MACRO_CONFIG_COL(QmHookCountdownUrgentColor, qm_hook_countdown_urgent_color, 0x0000FF9F, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hook countdown urgent color")

// Hook Collision Pressed Colors - 钩索辅助线按下后颜色（开关 + 四类自定义；开=四色始终生效，无回退）
// 默认按下色 = 对应原色配置默认值做 HSL 提亮（L + 0.25，H/S 不变，alpha 置 FF）得到的高亮变体。
// 数值推导：
//   qm_hook_coll_pressed_no_coll   : cl_hook_coll_color_no_coll      0x0000FF7F (H=00 S=FF L=7F 红) → L=BF → 0xFF00FFBF
//   qm_hook_coll_pressed_hookable  : cl_hook_coll_color_hookable_coll 0x0061AFB5 (H=61 S=AF L=B5 绿) → L=F5 → 0xFF61AFF5
//   qm_hook_coll_pressed_tee       : cl_hook_coll_color_tee_coll     0x002AFF7F (H=2A S=FF L=7F 黄) → L=BF → 0xFF2AFFBF
//   qm_hook_coll_pressed_tip       : cl_hook_coll_tip_color          0x002BFF80 (H=2B S=FF L=80)    → L=C0 → 0xFF2BFFC0
MACRO_CONFIG_INT(QmHookCollPressedCustom, qm_hook_coll_pressed_custom, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use custom colors for the pressed hook collision line (0 = always invert)")
MACRO_CONFIG_COL(QmHookCollPressedNoColl, qm_hook_coll_pressed_no_coll, 0xFF00FFBF, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Pressed hook collision line color when not aiming at anything hookable")
MACRO_CONFIG_COL(QmHookCollPressedHookable, qm_hook_coll_pressed_hookable, 0xFF61AFF5, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Pressed hook collision line color when hitting something hookable")
MACRO_CONFIG_COL(QmHookCollPressedTee, qm_hook_coll_pressed_tee, 0xFF2AFFBF, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Pressed hook collision line color when hitting a tee")
MACRO_CONFIG_COL(QmHookCollPressedTip, qm_hook_coll_pressed_tip, 0xFF2BFFC0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Pressed hook line tip color")

// HUD Dynamic Island - 灵动岛/HUD 编辑器
MACRO_CONFIG_INT(QmHudIslandUseOriginalStyle, qm_hud_island_use_original_style, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use original style for Dynamic Island")
MACRO_CONFIG_INT(QmHudIslandShowTeam, qm_hud_island_show_team, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show team on HUD Dynamic Island")
MACRO_CONFIG_INT(QmHudIslandAlwaysShowTrackDetails, qm_hud_island_always_show_track_details, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Always show song title and artist")
MACRO_CONFIG_COL(QmHudIslandBgColor, qm_hud_island_bg_color, 0x9C460E, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dynamic Island background color")
MACRO_CONFIG_INT(QmHudIslandBgOpacity, qm_hud_island_bg_opacity, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dynamic Island background alpha")
MACRO_CONFIG_STR(QmHudEditorLayout, qm_hud_editor_layout, 2048, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD editor layout")

// Gores 组队落水榜
MACRO_CONFIG_INT(QmGoresDrownBoard, qm_gores_drown_board, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show the Gores team drown board")
MACRO_CONFIG_INT(QmGoresDrownBoardIncludeTeam0, qm_gores_drown_board_include_team0, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Include Team 0 in the Gores team drown board")
MACRO_CONFIG_INT(QmGoresDrownBoardMaxPlayers, qm_gores_drown_board_max_players, 8, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Maximum players shown in the Gores team drown board")
MACRO_CONFIG_INT(QmGoresDrownBoardOpacity, qm_gores_drown_board_opacity, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Gores team drown board opacity")
MACRO_CONFIG_INT(QmGoresDrownBoardShowTee, qm_gores_drown_board_show_tee, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Tees in the Gores team drown board")

// Camera / View - 相机、视野
MACRO_CONFIG_INT(QmCameraDrift, qm_camera_drift, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable camera drift effect, slightly drags camera based on speed")
MACRO_CONFIG_INT(QmCameraDriftAmount, qm_camera_drift_amount, 50, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Camera drift intensity (0-200)")
MACRO_CONFIG_INT(QmCameraDriftSmoothness, qm_camera_drift_smoothness, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Camera drift smoothness (0=instant, 100=smoothest)")
MACRO_CONFIG_INT(QmCameraDriftReverse, qm_camera_drift_reverse, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Invert camera drift direction")
MACRO_CONFIG_INT(QmDynamicFov, qm_dynamic_fov, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable dynamic FOV, wider view at higher speed")
MACRO_CONFIG_INT(QmDynamicFovAmount, qm_dynamic_fov_amount, 50, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dynamic FOV intensity (0-200)")
MACRO_CONFIG_INT(QmDynamicFovSmoothness, qm_dynamic_fov_smoothness, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dynamic FOV smoothness (0=instant, 100=smoothest)")
MACRO_CONFIG_INT(QmCinematicCamera, qm_cinematic_camera, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Smooth the mouse-driven camera in live and demo free spectating")
MACRO_CONFIG_INT(QmCinematicCameraSmoothness, qm_cinematic_camera_smoothness, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Cinematic camera smoothness (0=instant, 100=smoothest)")
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
MACRO_CONFIG_INT(QmChatScrollbarRight, qm_chat_scrollbar_right, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show the chat scrollbar on the right")
MACRO_CONFIG_INT(QmChatRelatedOnly, qm_chat_related_only, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Only show mentions, whispers and your own chat messages regardless of friend or team filters; keep system messages and respect muted players")
MACRO_CONFIG_INT(QmChatSaveDraft, qm_chat_save_draft, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keep unsent message on chat close")
MACRO_CONFIG_INT(QmChatCommandCompletion, qm_chat_command_completion, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show command completion in chat")
MACRO_CONFIG_INT(QmChatEmojiCompletion, qm_chat_emoji_completion, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show emoji completion in chat")
MACRO_CONFIG_COL(QmChatCompletionBgColor, qm_chat_completion_bg_color, 0x1C1C1E, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat completion background color")
MACRO_CONFIG_INT(QmChatCompletionOpacity, qm_chat_completion_opacity, 96, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat completion background opacity")
MACRO_CONFIG_COL(QmChatCompletionTextColor, qm_chat_completion_text_color, 0xF50000FF, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Chat completion text color")
MACRO_CONFIG_COL(QmChatCompletionSelectedTextColor, qm_chat_completion_selected_text_color, 0xFF98D68E, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Chat completion selected text color")
MACRO_CONFIG_COL(QmChatCompletionSelectedColor, qm_chat_completion_selected_color, 0x4799FFA1, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Chat completion selection background color")
MACRO_CONFIG_INT(QmMessageMerge, qm_message_merge, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Merge consecutive identical player messages within 2 seconds")
// echo 合并的滑动窗口：同一段 echo 文本在窗口内连续重复时只保留一次并计数。
// 该行为始终生效，不受 qm_message_merge 影响；设为 0 关闭合并。
MACRO_CONFIG_INT(QmEchoMergeWindowMs, qm_echo_merge_window_ms, 2000, 0, 60000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Merge consecutive identical echo messages within this window in milliseconds (0 disables merging)")
MACRO_CONFIG_INT(QmChatHideSystemPrefix, qm_chat_hide_system_prefix, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide the *** prefix before server chat messages")
MACRO_CONFIG_INT(QmChatLineSpacing, qm_chat_line_spacing, 0, 0, 32, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Extra vertical spacing between chat lines in pixels (0 = default)")
MACRO_CONFIG_INT(QmChatAnimSlideOut, qm_chat_anim_slide_out, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable left swipe offset on chat fade")
MACRO_CONFIG_INT(QmChatAnimFadeDurationMs, qm_chat_anim_fade_duration_ms, 300, 0, 2000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat fade smooth time (milliseconds)")
MACRO_CONFIG_INT(QmHideChatBubbles, qm_hide_chat_bubbles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide own chat bubble (only when authenticated in remote console)")
MACRO_CONFIG_INT(QmChatBubble, qm_chat_bubble, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show chat bubbles above players")
MACRO_CONFIG_INT(QmChatBubbleStyle, qm_chat_bubble_style, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat bubble style (0=Comic dialogue, 1=Candy gradient, 2=Cloud bubble)")
MACRO_CONFIG_INT(QmChatBubbleCustomColors, qm_chat_bubble_custom_colors, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use custom chat bubble colors")
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

MACRO_CONFIG_INT(QmLyricsLanguage, qm_lyrics_language, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Lyrics language (0=Original, 1=Translation, 2=Original and translation)")
MACRO_CONFIG_INT(QmLyricsSeparateIsland, qm_lyrics_separate_island, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show lyrics in a separate island below Dynamic Island")
MACRO_CONFIG_INT(QmLyricsIslandFollowWidth, qm_lyrics_island_follow_width, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Match lyrics island width to the main Dynamic Island, excluding satellites")
MACRO_CONFIG_INT(QmHumming, qm_humming, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Send three consecutive lyrics to public chat at random intervals of 30 to 90 seconds, with 3 seconds between lines")

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
MACRO_CONFIG_STR(QmTranslateBackend, qm_translate_backend, 32, "mymemory", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Translation service")
MACRO_CONFIG_INT(QmTranslateShowAdvanced, qm_translate_show_advanced, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show advanced translation options")
MACRO_CONFIG_STR(QmTranslateTarget, qm_translate_target, 16, "zh", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Target language code (e.g. zh, en, ja, zh-TW)")
MACRO_CONFIG_INT(QmTranslateAuto, qm_translate_auto, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-translate incoming messages")
MACRO_CONFIG_INT(QmChatTranslationSize, qm_chat_translation_size, 80, 50, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Translated chat text size as a percentage of the original text (50-100)")
MACRO_CONFIG_INT(QmTranslateAutoMode, qm_translate_auto_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Incoming auto-translation mode (0=Only translate when not in target language, 1=Always translate)")
MACRO_CONFIG_INT(QmTranslateLocalDetectMinChars, qm_translate_local_detect_min_chars, 2, 1, 12, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Minimum characters for local target language detection")
MACRO_CONFIG_INT(QmTranslateLocalDetectRatio, qm_translate_local_detect_ratio, 75, 50, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Ratio threshold for local target language detection")
MACRO_CONFIG_INT(QmTranslateFtapiAutoEnable, qm_translate_ftapi_auto_enable, 0, 0, 1,
	CFGFLAG_CLIENT | CFGFLAG_SAVE,
	"Allow FTAPI auto-translate (may cause overload)")

// Translate - LLM API (OpenAI 兼容，默认智谱AI预设)
MACRO_CONFIG_INT(QmTranslateLlmProvider, qm_translate_llm_provider, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "LLM Provider (0=ZhipuAI, 1=DeepSeek, 2=OpenAI, 3=Custom)")

// 各 Provider 的模型配置（切换 Provider 时自动切换对应模型）
MACRO_CONFIG_STR(QmTranslateLlmModelZhipu, qm_translate_llm_model_zhipu, 32, "glm-4.7-flash", CFGFLAG_CLIENT | CFGFLAG_SAVE, "ZhipuAI model name")
MACRO_CONFIG_STR(QmTranslateLlmModelDeepseek, qm_translate_llm_model_deepseek, 32, "deepseek-flash", CFGFLAG_CLIENT | CFGFLAG_SAVE, "DeepSeek model name")
MACRO_CONFIG_STR(QmTranslateLlmModelOpenai, qm_translate_llm_model_openai, 32, "gpt-5.6-luna", CFGFLAG_CLIENT | CFGFLAG_SAVE, "OpenAI model name")
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
MACRO_CONFIG_INT(QmTranslateLlmCustomAuth, qm_translate_llm_custom_auth, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Authentication")
MACRO_CONFIG_INT(QmTranslateLlmCustomThinking, qm_translate_llm_custom_thinking, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Thinking parameters")
MACRO_CONFIG_INT(QmTranslateLlmCustomParameters, qm_translate_llm_custom_parameters, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Send sampling and token limit parameters")
MACRO_CONFIG_INT(QmTranslateLlmEnableThinking, qm_translate_llm_enable_thinking, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable LLM thinking mode (may increase response time)")
MACRO_CONFIG_STR(QmTranslateSystemPrompt, qm_translate_system_prompt, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Custom translation prompt (overrides built-in)")

// Translate - Source/Target Language - 目标语言
MACRO_CONFIG_STR(QmTranslateSource, qm_translate_source, 16, "auto", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Source language code for translation (auto=auto-detect)")

// Translate - Auto Outgoing - 自动翻译发送消息
MACRO_CONFIG_INT(QmTranslateAutoOutgoing, qm_translate_auto_outgoing, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-translate outgoing messages")
// 保存键沿用旧版本名称；现在只控制本地原文附注，不发送原文。
MACRO_CONFIG_INT(QmTranslateOutgoingShowOriginal, qm_translate_outgoing_send_original, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show the original text locally below translated outgoing messages")
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

// Translate - Baidu - 凭据只由用户配置，不内置公共密钥
MACRO_CONFIG_STR(QmTranslateBaiduAppId, qm_translate_baidu_app_id, 64, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "APP ID")
MACRO_CONFIG_STR(QmTranslateBaiduKey, qm_translate_baidu_key, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "API key")

// Translate - DeepL - 额度与重置周期由实际订阅决定
MACRO_CONFIG_STR(QmTranslateDeeplKey, qm_translate_deepl_key, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "DeepL API Key (free keys end with :fx)")

// Translate Button Colors - 翻译按钮自定义颜色
MACRO_CONFIG_INT(QmTranslateColorAlphaMigrated, qm_translate_color_alpha_migrated, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Translation color alpha migration completed flag")
MACRO_CONFIG_COL(QmTranslateBtnColorDisabled, qm_translate_btn_color_disabled, 0xD1000029, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Translation button cancel color")
MACRO_CONFIG_COL(QmTranslateBtnColorEnabled, qm_translate_btn_color_enabled, 0xE69E5E86, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Translation button active color")
MACRO_CONFIG_COL(QmTranslateMenuBgColor, qm_translate_menu_bg_color, 0xF200001F, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Translation menu background color")
MACRO_CONFIG_COL(QmTranslateMenuOptionSelected, qm_translate_menu_option_selected, 0xE69E5E86, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Translation menu selected color")
MACRO_CONFIG_COL(QmTranslateMenuOptionNormal, qm_translate_menu_option_normal, 0xE6000033, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Translation menu normal color")

// Jump Hint / 跳跃提示 - 根据位置小数部分显示跳跃速查表（由 qm_jump_hint 迁移而来）
MACRO_CONFIG_INT(QmJumpHint, qm_jump_hint, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show jump hint based on fractional part of position")
MACRO_CONFIG_INT(QmJumpHintDefaultsMigrated, qm_jump_hint_defaults_migrated, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Jump hint defaults migration completed flag")
MACRO_CONFIG_STR(QmJumpHintText, qm_jump_hint_text, 512, "三格边缘跳:\\n左起跳: .34|.31|.16\\n左二段跳: .41|.28|.25|.13\\n右起跳: .63|.66|.81\\n右二段跳: .56|.69|.72|.84", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Jump hint text (use \\n for newline)")
MACRO_CONFIG_COL(QmJumpHintColor, qm_jump_hint_color, 255, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Jump hint color")
MACRO_CONFIG_INT(QmJumpHintX, qm_jump_hint_x, 20, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Jump hint horizontal position (% of screen width)")
MACRO_CONFIG_INT(QmJumpHintY, qm_jump_hint_y, 5, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Jump hint vertical position (% of screen height)")
MACRO_CONFIG_INT(QmJumpHintSize, qm_jump_hint_size, 10, 0, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Jump hint font size")

// Friends - 好友
MACRO_CONFIG_INT(QmFriendAutoFollowDelay, qm_friend_auto_follow_delay, 3, 0, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-follow friend server switch delay (seconds)")

// 合影渲染界面的选项记住上次选择：这三个值原本只存在内存里，重启就回到默认，
// 玩家每次都要重选。0=png 1=webp / 1-4 倍 / 实体层开关。
MACRO_CONFIG_INT(QmPoseOutput, qm_pose_output, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Pose render output format (0 = png, 1 = webp)")
MACRO_CONFIG_INT(QmPoseCaptureScale, qm_pose_capture_scale, 1, 1, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Pose render supersampling scale (1-4)")
MACRO_CONFIG_INT(QmPoseEntityLayer, qm_pose_entity_layer, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Pose render fills the background with the entity layer")
MACRO_CONFIG_INT(QmPoseSharpen, qm_pose_sharpen, 0, 0, 60, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Pose render unsharp mask strength (0 = off)")

// 合影超采样的像素预算（百万像素）。0 = 不限制。
// 默认 40 MP：必须容得下 4K 窗口的 2 倍（3840x2160x2 = 33.2 MP）——
// 早期写死 33 MP（3300 万，比 3318 万略小）会把 4K 用户的所有倍数都压成 1 倍，
// 表现为「选 1 倍还是 4 倍都一样模糊」。
// 4 倍 4K 是 133 MP（颜色+深度+读回各 530 MB，PNG 编码几十秒，实测会触发
// 「客户端卡死」看门狗），需要显式调高，不给默认。
MACRO_CONFIG_INT(QmPoseCaptureMaxMegapixels, qm_pose_capture_max_megapixels, 40, 0, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Pose capture pixel budget in megapixels (0 = unlimited)")

// Self-kill protection / 自杀保护 - 仅拦截客户端主动自杀（K 键 / kill 命令），服务端命令不受影响。
// 默认关闭：需要玩家自己按下绑定键（qm_self_kill_off / qm_self_kill_on）或设置卡开关才会拦截，
// 不会"自己打开"。
MACRO_CONFIG_INT(QmSelfKillEnabled, qm_self_kill_enabled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Block client-initiated self-kill (K key / kill command)")
// 进图后自动开启保护的延迟（分钟）。0 = 不自动开启，保持配置里的默认状态。
// 自动开启只改本局运行时状态，不写回 qm_self_kill_enabled。
MACRO_CONFIG_INT(QmSelfKillAutoDelay, qm_self_kill_auto_delay, 0, 0, 60, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-enable self-kill protection this many minutes after joining a game (0 = never)")
