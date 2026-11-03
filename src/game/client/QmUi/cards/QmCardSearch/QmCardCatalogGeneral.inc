// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_general_client[] = {
		{Localizable("Config directory"), nullptr, "配置目录（cfg）"},
		{Localizable("Hz", "Hertz"), nullptr, "Hz", "Hertz"},
		{Localizable("Save power by lowering update rate (higher input latency)"), nullptr, "降低刷新率以节约功耗（增加输入延迟）"},
		{Localizable("Saves file"), nullptr, "存档密码文件"},
		{Localizable("Settings file"), nullptr, "设置文件"},
		{Localizable("Skip the main menu"), nullptr, "跳过主菜单"},
		{Localizable("Themes directory"), nullptr, "主题目录"},
		{Localizable("Update Rate"), nullptr, "客户端刷新率"},
		{nullptr, "cl_menu_map", nullptr},
		{nullptr, "cl_refresh_rate", nullptr},
		{nullptr, "cl_skip_start_menu", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:general-client", deck_general_client);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_general_game[] = {
		{Localizable("Dynamic Camera"), nullptr, "动态视野"},
		{Localizable("Smooth Dynamic Camera"), nullptr, "平滑动态视野"},
		{Localizable("Switch weapon on pickup"), nullptr, "拾取武器时自动装备"},
		{Localizable("Switch weapon when out of ammo"), nullptr, "子弹打空时自动切换武器"},
		{nullptr, "cl_autoswitch_weapons", nullptr},
		{nullptr, "cl_autoswitch_weapons_out_of_ammo", nullptr},
		{nullptr, "cl_dyncam", nullptr},
		{nullptr, "cl_dyncam_smoothness", nullptr},
		{nullptr, "cl_dyncam_stabilizing", nullptr},
		{nullptr, "cl_mouse_followfactor", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:general-game", deck_general_game);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_general_language[] = {
		{Localizable("Language"), nullptr, "语言"},
		{nullptr, "cl_languagefile", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:general-language", deck_general_language);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_general_recording[] = {
#if defined(CONF_FAMILY_WINDOWS)
		{Localizable("Automatically copy manual screenshots to clipboard"), "qm_screenshot_clipboard", "手动截图 自动复制 剪贴板 剪切板 F10 clipboard screenshot"},
#endif
		{Localizable("Automatically add a watermark to screenshots"), nullptr, "截图自动水印 时间戳 地图名 自定义文本"},
		{Localizable("Custom watermark text"), nullptr, "截图水印文本"},
		{Localizable("Timestamp"), nullptr, "截图时间戳"},
		{Localizable("Map name"), nullptr, "截图地图名"},
		{Localizable("Bottom left"), nullptr, "左下"},
		{Localizable("Bottom right"), nullptr, "右下"},
		{Localizable("Top left"), nullptr, "左上"},
		{Localizable("Top right"), nullptr, "右上"},
		{Localizable("Leave empty to use the default directory. Relative paths use the config directory."), nullptr, "留空则使用默认目录；相对路径基于配置目录。"},
		{Localizable("Demos directory"), nullptr, "录像目录"},
		{Localizable("Videos directory"), nullptr, "视频目录"},
		{nullptr, "qm_screenshot_watermark", nullptr},
		{nullptr, "qm_screenshot_watermark_timestamp", nullptr},
		{nullptr, "qm_screenshot_watermark_map", nullptr},
		{nullptr, "qm_screenshot_watermark_position", nullptr},
		{nullptr, "qm_screenshot_watermark_text", nullptr},
		{Localizable("Automatically create statboard csv"), nullptr, "自动将统计板导出为 csv 文件"},
		{Localizable("Automatically record demos"), nullptr, "自动录制回放"},
		{Localizable("Automatically take game over screenshot"), nullptr, "自动对游戏结束画面截图"},
		{Localizable("Automatically take statboard screenshot"), nullptr, "自动对统计板截图"},
		{Localizable("Max CSVs"), nullptr, "最大 CSV 数"},
		{Localizable("Max Screenshots"), nullptr, "最大截图数"},
		{Localizable("Max demos"), nullptr, "最大回放数"},
		{Localizable("Demo directory"), "qm_demo_directory", "回放保存目录 路径 demos"},
		{Localizable("Video directory"), "qm_video_directory", "视频导出目录 路径 videos"},
		{Localizable("Video quality CRF"), nullptr, "渲染质量 画质 crf"},
		{nullptr, "cl_video_crf", nullptr},
		{Localizable("Recorder FPS"), nullptr, "录制帧率 录像帧率 帧率 fps"},
		{nullptr, "cl_video_recorder_fps", nullptr},
		{Localizable("Render preset"), nullptr, "渲染预设 编码预设 preset"},
		{nullptr, "cl_video_x264_preset", nullptr},
		{Localizable("Screenshot directory"), "qm_screenshot_directory", "截图保存目录 路径 screenshots"},
		{nullptr, "cl_auto_csv", nullptr},
		{nullptr, "cl_auto_csv_max", nullptr},
		{nullptr, "cl_auto_demo_max", nullptr},
		{nullptr, "cl_auto_demo_record", nullptr},
		{nullptr, "cl_auto_screenshot", nullptr},
		{nullptr, "cl_auto_screenshot_max", nullptr},
		{nullptr, "cl_auto_statboard_screenshot", nullptr},
		{nullptr, "cl_auto_statboard_screenshot_max", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:general-recording", deck_general_recording);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_tclient_info_files[] = {
		{Localizable("Configuration packages"), nullptr, "配置包 打包 qmconfig"},
		{Localizable("Import configuration package"), nullptr, "导入 配置 恢复 bind cfg"},
		{Localizable("Chat Binds"), nullptr, "聊天绑定"},
		{Localizable("Profiles"), nullptr, "配置文件"},
		{Localizable("QmClient Settings"), nullptr, "QmClient 设置"},
		{Localizable("War List"), nullptr, "敌对列表"},
		{Localizable("Settings, binds, profiles, chat binds and the war list are included. Select additional cfg files below."), nullptr, "包含设置、绑定、配置档、聊天绑定与敌对列表；可在下方额外选择 cfg 文件。"},
		{Localizable("Copy a .qmconfig file into the package folder, then refresh."), nullptr, "把 .qmconfig 复制到配置包目录后刷新。"},
		{Localizable("Remove"), nullptr, "移除"},
		{Localizable("Complete restore replaces the listed files. A backup is saved before importing. The client will restart."), nullptr, "完整恢复会替换列出的文件；导入前自动备份，客户端将重启。"},
		{Localizable("Replace settings, binds and the listed cfg files, then restart? The current configuration will be backed up first."), nullptr, "替换设置、绑定和列出的 cfg 文件并重启？当前配置会先备份。"},
		{Localizable("Select a configuration package to preview its contents."), nullptr, "选择一个配置包以预览内容。"},
};
	QM_CARD_SEARCH_REGISTER("deck:tclient-info-files", deck_tclient_info_files);
}
