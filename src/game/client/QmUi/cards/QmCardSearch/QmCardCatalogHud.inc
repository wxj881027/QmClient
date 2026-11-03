// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_background_3d[] = {
		{Localizable("Crescent"), nullptr, "月牙"},
		{Localizable("Cube"), nullptr, "方块"},
		{Localizable("Custom"), nullptr, "自定义"},
		{Localizable("Diamond"), nullptr, "钻石"},
		{Localizable("Enable 3D background particles"), nullptr, "启用 3D 背景粒子"},
		{Localizable("Fade in"), nullptr, "淡入"},
		{Localizable("Fade out"), nullptr, "淡出"},
		{Localizable("Glow alpha"), nullptr, "辉光透明度"},
		{Localizable("Glow offset"), nullptr, "辉光偏移"},
		{Localizable("Heart"), nullptr, "爱心"},
		{Localizable("Max size"), nullptr, "最大尺寸"},
		{Localizable("Min size"), nullptr, "最小尺寸"},
		{Localizable("Mixed"), nullptr, "混合"},
		{Localizable("Particle alpha"), nullptr, "粒子透明度"},
		{Localizable("Particle collision"), nullptr, "粒子碰撞"},
		{Localizable("Particle color"), nullptr, "粒子颜色"},
		{Localizable("Particle count"), nullptr, "粒子数量"},
		{Localizable("Particle depth"), nullptr, "粒子深度"},
		{Localizable("Particle glow"), nullptr, "粒子辉光"},
		{Localizable("Particle pulse"), nullptr, "粒子脉冲"},
		{Localizable("Particle speed"), nullptr, "粒子速度"},
		{Localizable("Particle trail"), nullptr, "粒子拖尾"},
		{Localizable("Particle twinkle"), nullptr, "粒子闪烁"},
		{Localizable("Particle type"), nullptr, "粒子类型"},
		{Localizable("Pulse speed"), nullptr, "脉冲速度"},
		{Localizable("Pulse strength"), nullptr, "脉冲强度"},
		{Localizable("Push radius"), nullptr, "推动半径"},
		{Localizable("Push strength"), nullptr, "推动强度"},
		{Localizable("Pyramid"), nullptr, "金字塔"},
		{Localizable("Random"), nullptr, "随机"},
		{Localizable("Ring"), nullptr, "圆环"},
		{Localizable("Sphere"), nullptr, "球体"},
		{Localizable("Star"), nullptr, "星形"},
		{Localizable("Trail alpha"), nullptr, "拖尾透明度"},
		{Localizable("Trail length"), nullptr, "拖尾长度"},
		{Localizable("Twinkle strength"), nullptr, "闪烁强度"},
		{Localizable("View margin"), nullptr, "视野外边距"},
		{nullptr, "qm_3d_particles", nullptr},
		{nullptr, "qm_3d_particles_alpha", nullptr},
		{nullptr, "qm_3d_particles_collide", nullptr},
		{nullptr, "qm_3d_particles_color", nullptr},
		{nullptr, "qm_3d_particles_color_mode", nullptr},
		{nullptr, "qm_3d_particles_count", nullptr},
		{nullptr, "qm_3d_particles_depth", nullptr},
		{nullptr, "qm_3d_particles_fade_in_ms", nullptr},
		{nullptr, "qm_3d_particles_fade_out_ms", nullptr},
		{nullptr, "qm_3d_particles_glow", nullptr},
		{nullptr, "qm_3d_particles_glow_alpha", nullptr},
		{nullptr, "qm_3d_particles_glow_offset", nullptr},
		{nullptr, "qm_3d_particles_pulse", nullptr},
		{nullptr, "qm_3d_particles_pulse_speed", nullptr},
		{nullptr, "qm_3d_particles_pulse_strength", nullptr},
		{nullptr, "qm_3d_particles_push_radius", nullptr},
		{nullptr, "qm_3d_particles_push_strength", nullptr},
		{nullptr, "qm_3d_particles_size_max", nullptr},
		{nullptr, "qm_3d_particles_size_min", nullptr},
		{nullptr, "qm_3d_particles_speed", nullptr},
		{nullptr, "qm_3d_particles_trail", nullptr},
		{nullptr, "qm_3d_particles_trail_alpha", nullptr},
		{nullptr, "qm_3d_particles_trail_length", nullptr},
		{nullptr, "qm_3d_particles_twinkle", nullptr},
		{nullptr, "qm_3d_particles_twinkle_strength", nullptr},
		{nullptr, "qm_3d_particles_type", nullptr},
		{nullptr, "qm_3d_particles_view_margin", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:background_3d", qm_background_3d);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_bind_status_hud[] = {
		{Localizable("Show dummy control status"), nullptr, "显示分身控制状态"},
		{Localizable("Show dummy copy status"), nullptr, "显示分身同步状态"},
		{Localizable("Show hammer status"), nullptr, "显示锤状态"},
		{Localizable("Show key stuck status"), nullptr, "显示卡键状态"},
		{nullptr, "cl_showhud_key_status_control", nullptr},
		{nullptr, "cl_showhud_key_status_hammer", nullptr},
		{nullptr, "cl_showhud_key_status_reset", nullptr},
		{nullptr, "cl_showhud_key_status_sync", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:bind_status_hud", qm_bind_status_hud);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_coords[] = {
		{Localizable("Detection time"), nullptr, "判定时间"},
		{Localizable("Show X"), nullptr, "显示 X"},
		{Localizable("Show Y"), nullptr, "显示 Y"},
		{Localizable("Show other players' coordinates"), nullptr, "显示他人坐标"},
		{Localizable("Show own coordinates"), nullptr, "显示自己坐标"},
		{Localizable("Strict mode"), nullptr, "严格模式"},
		{Localizable("X alignment color"), nullptr, "X 对齐颜色"},
		{Localizable("X alignment hint with me"), nullptr, "与我 X 对齐提示"},
		{nullptr, "qm_nameplate_coord_x", nullptr},
		{nullptr, "qm_nameplate_coord_x_align_hint", nullptr},
		{nullptr, "qm_nameplate_coord_x_align_hint_color", nullptr},
		{nullptr, "qm_nameplate_coord_x_align_hint_strict", nullptr},
		{nullptr, "qm_nameplate_coord_x_align_hint_window_ms", nullptr},
		{nullptr, "qm_nameplate_coord_y", nullptr},
		{nullptr, "qm_nameplate_coords", nullptr},
		{nullptr, "qm_nameplate_coords_own", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:coords", qm_coords);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_debug_graph[] = {
		{Localizable("Global toggle key"), nullptr, "全局开关键"},
		{Localizable("Panel opacity"), nullptr, "面板不透明度"},
		{nullptr, "qm_monitoring_hud_opacity", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:debug_graph", qm_debug_graph);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_debug_mode[] = {
		{Localizable("Debug mode"), nullptr, "调试模式"},
		{Localizable("Enable client stutter diagnostics at startup"), nullptr, "启动时启用客户端卡顿诊断"},
		{Localizable("Enable main thread and render stage performance debug logging"), nullptr, "启用主线程与渲染阶段性能调试日志"},
		{Localizable("Performance debug log threshold (ms)"), nullptr, "性能调试日志阈值（毫秒）"},
		{Localizable("Write performance debug logs to dedicated file"), nullptr, "将性能调试日志写入专用文件"},
		{nullptr, "qm_perf_debug", nullptr},
		{nullptr, "qm_perf_debug_threshold_ms", nullptr},
		{nullptr, "qm_perf_logfile", nullptr},
		{nullptr, "qm_perf_stutter_diagnostics", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:debug_mode", qm_debug_mode);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_dummy_miniview[] = {
		{Localizable("Dummy window size"), nullptr, "分身小窗大小"},
		{Localizable("Dummy window zoom"), nullptr, "分身小窗缩放"},
		{Localizable("Enable dummy window"), nullptr, "启用分身小窗"},
		{Localizable("Only show when the other Tee is not on screen"), nullptr, "仅当另一个 Tee 不在屏幕上时显示"},
		{nullptr, "qm_dummy_miniview", nullptr},
		{nullptr, "qm_dummy_miniview_auto", nullptr},
		{nullptr, "qm_dummy_miniview_size", nullptr},
		{nullptr, "qm_dummy_miniview_zoom", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:dummy_miniview", qm_dummy_miniview);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_dynamic_island[] = {
		{Localizable("Always show song title and artist"), nullptr, "始终显示歌曲名与歌手"},
		{Localizable("Background color"), nullptr, "背景颜色 灵动岛背景"},
		{Localizable("Enable hook countdown"), nullptr, "启用钩子倒计时"},
		{Localizable("Enable switch countdown"), nullptr, "启用开关倒计时"},
		{Localizable("Endless hook indicator"), nullptr, "无限钩指示样式"},
		{Localizable("Follow Tee"), nullptr, "跟随 Tee 圆环"},
		{Localizable("Hook countdown colors"), nullptr, "钩子倒计时颜色 三段色阶"},
		{Localizable("Hook countdown size"), nullptr, "钩子倒计时大小"},
		{Localizable("Infinity symbol"), nullptr, "无穷符号 ∞"},
		{Localizable("Rainbow ring"), nullptr, "彩虹圆环"},
		{Localizable("Show in Dynamic Island"), nullptr, "在灵动岛中显示"},
		{Localizable("Show team"), nullptr, "显示队伍"},
		{Localizable("Use original style"), nullptr, "使用原始样式"},
		{nullptr, "qm_hook_countdown", nullptr},
		{nullptr, "qm_hook_countdown_color", nullptr},
		{nullptr, "qm_hook_countdown_endless_style", nullptr},
		{nullptr, "qm_hook_countdown_size", nullptr},
		{nullptr, "qm_hook_countdown_urgent_color", nullptr},
		{nullptr, "qm_hook_countdown_warning_color", nullptr},
		{nullptr, "qm_hud_island_always_show_track_details", nullptr},
		{nullptr, "qm_hud_island_bg_color", nullptr},
		{nullptr, "qm_hud_island_bg_opacity", nullptr},
		{nullptr, "qm_hud_island_show_team", nullptr},
		{nullptr, "qm_hud_island_use_original_style", nullptr},
		{nullptr, "qm_switch_countdown", nullptr},
		{nullptr, "qm_switch_countdown_mode", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:dynamic_island", qm_dynamic_island);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_gores_drown_board[] = {
		{Localizable("Card opacity"), nullptr, "卡片不透明度"},
		{Localizable("Players shown"), nullptr, "显示的玩家数"},
		{Localizable("Show Gores drown board"), nullptr, "显示 Gores 落水榜"},
		{Localizable("Show player Tee"), nullptr, "显示玩家 Tee"},
		{Localizable("Include Team 0"), nullptr, "包含 0 队"},
		{nullptr, "qm_gores_drown_board", nullptr},
		{nullptr, "qm_gores_drown_board_max_players", nullptr},
		{nullptr, "qm_gores_drown_board_opacity", nullptr},
		{nullptr, "qm_gores_drown_board_show_tee", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:gores_drown_board", qm_gores_drown_board);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_hud_notifications[] = {
		{Localizable("Advanced options"), nullptr, "高级选项"},
		{Localizable("Animation duration"), nullptr, "动画持续多久"},
		{Localizable("Detect compatible solo prompts from custom servers"), nullptr, "检测自定义服务器的兼容单人区域提示"},
		{Localizable("Echo follows the original chat color"), nullptr, "Echo 跟随聊天里当时的实际颜色"},
		{Localizable("Echo text color when not inheriting chat color"), nullptr, "Echo 不跟随聊天颜色时的文字颜色"},
		{Localizable("Edge margin"), nullptr, "贴边边距"},
		{Localizable("Fade and slide"), nullptr, "淡入并滑入"},
		{Localizable("Fade only"), nullptr, "仅淡入淡出"},
		{Localizable("Join, version, rules, and help messages stay in chat instead of popups"), nullptr, "入服、版本、规则和帮助消息会留在聊天里，不弹出显示"},
		{Localizable("Max visible notifications"), nullptr, "最多同时显示几条"},
		{Localizable("No animation"), nullptr, "无动画"},
		{Localizable("Notification background"), nullptr, "通知背景"},
		{Localizable("Notification hold time"), nullptr, "每条显示多久"},
		{Localizable("Notification text size"), nullptr, "通知文字大小"},
		{Localizable("Popup animation"), nullptr, "弹出动画"},
		{Localizable("Route Echo messages to notifications"), nullptr, "Echo 消息改走通知栏"},
		{Localizable("Show basic server information"), nullptr, "显示基础服务器信息"},
		{Localizable("Show important server prompts"), nullptr, "显示重要服务器提示"},
		{Localizable("Show important server prompts as notifications"), nullptr, "重要服务器提示改走通知栏"},
		{Localizable("Show server help and usage messages"), nullptr, "显示服务器帮助和用法说明"},
		{Localizable("Show unknown server messages"), nullptr, "显示未知服务器消息"},
		{Localizable("System prompt text color"), nullptr, "系统提示文字颜色"},
		{Localizable("Use notification category filters"), nullptr, "使用通知栏分类过滤"},
		{nullptr, "qm_hud_notifications_anim_ms", nullptr},
		{nullptr, "qm_hud_notifications_anim_type", nullptr},
		{nullptr, "qm_hud_notifications_bg_color", nullptr},
		{nullptr, "qm_hud_notifications_compat_solo", nullptr},
		{nullptr, "qm_hud_notifications_echo", nullptr},
		{nullptr, "qm_hud_notifications_echo_inherit_color", nullptr},
		{nullptr, "qm_hud_notifications_echo_text_color", nullptr},
		{nullptr, "qm_hud_notifications_edge_margin", nullptr},
		{nullptr, "qm_hud_notifications_hold_ms", nullptr},
		{nullptr, "qm_hud_notifications_max_visible", nullptr},
		{nullptr, "qm_hud_notifications_show_advanced", nullptr},
		{nullptr, "qm_hud_notifications_show_basic_info", nullptr},
		{nullptr, "qm_hud_notifications_show_help_info", nullptr},
		{nullptr, "qm_hud_notifications_show_prompts", nullptr},
		{nullptr, "qm_hud_notifications_show_unknown", nullptr},
		{nullptr, "qm_hud_notifications_system", nullptr},
		{nullptr, "qm_hud_notifications_text_color", nullptr},
		{nullptr, "qm_hud_notifications_text_size", nullptr},
		{nullptr, "qm_hud_notifications_use_category_filters", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:hud_notifications", qm_hud_notifications);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_input_overlay[] = {
		{Localizable("Horizontal position"), nullptr, "水平位置"},
		{Localizable("Key press count color"), nullptr, "按键计数颜色"},
		{Localizable("Key press count font size"), nullptr, "按键计数字号"},
		{Localizable("Key press count label"), nullptr, "按键计数前缀文字"},
		{Localizable("Keyboard size"), nullptr, "键盘大小"},
		{Localizable("Mouse size"), nullptr, "鼠标大小"},
		{Localizable("Opacity"), nullptr, "不透明度"},
		{Localizable("Show inputs"), nullptr, "显示按键"},
		{Localizable("Show key press counts"), nullptr, "显示按键累计次数"},
		{Localizable("Vertical position"), nullptr, "垂直位置"},
		{nullptr, "qm_input_overlay", nullptr},
		{nullptr, "qm_input_overlay_count_color", nullptr},
		{nullptr, "qm_input_overlay_count_label", nullptr},
		{nullptr, "qm_input_overlay_count_size", nullptr},
		{nullptr, "qm_input_overlay_key_counts", nullptr},
		{nullptr, "qm_input_overlay_mouse_scale", nullptr},
		{nullptr, "qm_input_overlay_opacity", nullptr},
		{nullptr, "qm_input_overlay_pos_x", nullptr},
		{nullptr, "qm_input_overlay_pos_y", nullptr},
		{nullptr, "qm_input_overlay_scale", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:input_overlay", qm_input_overlay);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_lyrics[] = {
		{Localizable("Enable Kugou Music lyrics"), nullptr, "启用酷狗音乐歌词"},
		{Localizable("Enable lyrics"), nullptr, "启用歌词"},
		{Localizable("Enable Netease music Hook"), nullptr, "启用网易云音乐 Hook"},
		{Localizable("Enable QQ Music lyrics"), nullptr, "启用 QQ 音乐歌词"},
		{Localizable("Enable SodaMusic Hook"), nullptr, "启用汽水音乐 Hook"},
		{Localizable("Enable Spotify lyrics"), nullptr, "启用 Spotify 歌词"},
		{Localizable("Follow main island width"), nullptr, "歌词岛跟随主岛宽度 灵动岛等宽 排除眼睛附岛"},
		{Localizable("Lyrics language"), nullptr, "歌词语言 单语 双语"},
		{Localizable("Original and translation"), nullptr, "原文和译文"},
		{Localizable("Original only"), nullptr, "仅原文"},
		{Localizable("Paste sp_dc from Spotify web cookies"), nullptr, "粘贴浏览器中的 sp_dc cookie"},
		{Localizable("Separate Dynamic Island"), nullptr, "独立灵动岛 歌词附岛"},
		{Localizable("Show lyrics in Dynamic Island"), nullptr, "将歌词显示在灵动岛内"},
		{Localizable("Translation only"), nullptr, "仅译文"},
		{nullptr, "qm_kugou_hook_enable", nullptr},
		{nullptr, "qm_lyrics", nullptr},
		{nullptr, "qm_lyrics_in_media_island", nullptr},
		{nullptr, "qm_lyrics_island_follow_width", nullptr},
		{nullptr, "qm_lyrics_language", nullptr},
		{nullptr, "qm_lyrics_separate_island", nullptr},
		{nullptr, "qm_netease_hook_enable", nullptr},
		{nullptr, "qm_qqmusic_hook_enable", nullptr},
		{nullptr, "qm_soda_hook_enable", nullptr},
		{nullptr, "qm_spotify_enable", nullptr},
		{nullptr, "qm_spotify_sp_dc", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:lyrics", qm_lyrics);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_player_stats[] = {
		{Localizable("Horizontal position"), nullptr, "水平位置"},
		{Localizable("Map progress bar"), nullptr, "地图进度条"},
		{Localizable("Progress bar color"), nullptr, "进度条颜色"},
		{Localizable("Progress bar height"), nullptr, "进度条高度"},
		{Localizable("Progress bar width"), nullptr, "进度条宽度"},
		{Localizable("Reset stats when joining a server"), nullptr, "进入服务器时重置统计"},
		{Localizable("Show dotted map route debug"), nullptr, "显示地图点状路线调试"},
		{Localizable("Show player stats HUD"), nullptr, "显示玩家统计 HUD"},
		{Localizable("Progress bar location"), nullptr, "进度条位置 独立条 统计内嵌 灵动岛地图进度"},
		{Localizable("Dynamic Island"), nullptr, "灵动岛地图进度"},
		{Localizable("Standalone"), nullptr, "独立条 独立显示"},
		{Localizable("Player stats"), nullptr, "玩家统计 统计内嵌"},
		{Localizable("Vertical position"), nullptr, "垂直位置"},
		{nullptr, "qm_player_stats_hud", nullptr},
		{nullptr, "qm_player_stats_map_progress", nullptr},
		{nullptr, "qm_player_stats_map_progress_color", nullptr},
		{nullptr, "qm_player_stats_map_progress_dbg_route", nullptr},
		{nullptr, "qm_player_stats_map_progress_height", nullptr},
		{nullptr, "qm_player_stats_map_progress_pos_x", nullptr},
		{nullptr, "qm_player_stats_map_progress_pos_y", nullptr},
		{nullptr, "qm_player_stats_map_progress_style", nullptr},
		{nullptr, "qm_player_stats_map_progress_width", nullptr},
		{nullptr, "qm_player_stats_reset_on_join", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:player_stats", qm_player_stats);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_system_media_controls[] = {
		{Localizable("Enable system media control"), nullptr, "启用系统媒体控制"},
		{Localizable("Show song info in top-left corner"), nullptr, "在左上角显示歌曲信息"},
		{nullptr, "qm_smtc_enable", nullptr},
		{nullptr, "qm_smtc_show_hud", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:system_media_controls", qm_system_media_controls);
}
