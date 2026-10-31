// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_ddnet_background[] = {
		{Localizable("Entities background color"), nullptr, "实体层背景颜色"},
		{Localizable("Map"), nullptr, "地图"},
		{Localizable("Regular background color"), nullptr, "原图层背景颜色"},
		{Localizable("Show tiles layers from BG map"), nullptr, "显示实体层背景地图的图块层"},
		{Localizable("Use current map as background"), nullptr, "使用当前地图作为实体层背景地图"},
		{nullptr, "cl_background_color", nullptr},
		{nullptr, "cl_background_entities", nullptr},
		{nullptr, "cl_background_entities_color", nullptr},
		{nullptr, "cl_background_show_tiles_layers", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:ddnet-background", deck_ddnet_background);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_ddnet_demo[] = {
		{Localizable("Default length"), nullptr, "默认时长"},
		{Localizable("ESC replay minutes"), nullptr, "ESC 保存回放分钟数"},
		{Localizable("Enable ghost"), nullptr, "启用影子"},
		{Localizable("Enable replays"), nullptr, "启用短时回放"},
		{Localizable("Only save improvements"), nullptr, "只保存更佳记录"},
		{Localizable("Opacity"), nullptr, "不透明度"},
		{Localizable("Save ghost"), nullptr, "保存影子"},
		{Localizable("Save the best demo of each race"), nullptr, "保存每场游戏最佳记录的回放"},
		{Localizable("Show ghost"), nullptr, "显示幽灵"},
		{Localizable("Show a ghost of your best time after crossing the start line"), nullptr, "越过起跑线后显示最佳成绩的影子 Tee"},
		{nullptr, "cl_auto_race_record", nullptr},
		{nullptr, "cl_esc_replay_length_minutes", nullptr},
		{nullptr, "cl_race_ghost", nullptr},
		{nullptr, "cl_race_ghost_alpha", nullptr},
		{nullptr, "cl_race_ghost_save_best", nullptr},
		{nullptr, "cl_race_save_ghost", nullptr},
		{nullptr, "cl_race_show_ghost", nullptr},
		{nullptr, "cl_replay_length", nullptr},
		{nullptr, "cl_replays", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:ddnet-demo", deck_ddnet_demo);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_ddnet_gameplay[] = {
		{Localizable("Opacity of other teams' tees and name plates"), nullptr, "其他队伍 Tee 与名牌的不透明度"},
		{Localizable("AntiPing (latency compensation)"), nullptr, "延迟补偿（AntiPing）"},
		{Localizable("AntiPing: predict grenade path"), nullptr, "延迟补偿：预测榴弹枪路径"},
		{Localizable("AntiPing: predict other players"), nullptr, "延迟补偿：预测其他玩家"},
		{Localizable("AntiPing: predict weapons"), nullptr, "延迟补偿：预测武器"},
		{Localizable("Default zoom"), nullptr, "默认缩放"},
		{Localizable("Opacity"), nullptr, "不透明度"},
		{Localizable("Overlay entities"), nullptr, "显示实体层"},
		{Localizable("Predict events (experimental)"), nullptr, "预测事件（实验性）"},
		{Localizable("Prediction margin"), nullptr, "预测余量"},
		{Localizable("Quads are used for background decoration"), nullptr, "显示用作背景装饰的贴图"},
		{Localizable("Show background quads"), nullptr, "显示背景贴图"},
		{Localizable("Show others"), nullptr, "显示其他玩家"},
		{Localizable("Show others (own team only)"), nullptr, "仅显示同队伍"},
		{Localizable("Show text entities"), nullptr, "显示文本实体"},
		{Localizable("Size"), nullptr, "大小"},
		{Localizable("Try to predict other entities to reduce lag feeling at high latency"), nullptr, "尝试预测其他实体来降低高延迟带来的卡顿感"},
		{Localizable("Experimental: repeats outgoing UDP game packets when round-trip probes go unanswered. Upload traffic may triple; download loss and late duplicate inputs can still cause problems. Disabled with KCP."), nullptr, "上行冗余说明：探测无响应时重发 UDP 包，上传流量可能翻三倍"},
		{nullptr, "cl_antiping", nullptr},
		{nullptr, "cl_antiping_grenade", nullptr},
		{nullptr, "cl_antiping_players", nullptr},
		{nullptr, "cl_antiping_weapons", nullptr},
		{nullptr, "cl_default_zoom", nullptr},
		{nullptr, "cl_overlay_entities", nullptr},
		{Localizable("Upstream redundancy (experimental)"), nullptr, "上行冗余发送（实验性）"},
		{Localizable("Maximum packet copies"), nullptr, "最大包份数"},
		{nullptr, "qm_upstream_redundancy", nullptr},
		{nullptr, "qm_upstream_redundancy_max_copies", nullptr},
		{nullptr, "cl_predict_events", nullptr},
		{nullptr, "cl_prediction_margin", nullptr},
		{nullptr, "cl_show_others", nullptr},
		{nullptr, "cl_show_others_alpha", nullptr},
		{nullptr, "cl_showquads", nullptr},
		{nullptr, "cl_smooth_zoom_time", nullptr},
		{nullptr, "cl_text_entities", nullptr},
		{nullptr, "cl_text_entities_size", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:ddnet-gameplay", deck_ddnet_gameplay);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_ddnet_miscellaneous[] = {
		{Localizable("Chat command (e.g. showall 1)"), nullptr, "聊天指令（例如：showall 1）"},
		{Localizable("Run on join"), nullptr, "进入时执行"},
		{nullptr, "cl_run_on_join", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:ddnet-miscellaneous", deck_ddnet_miscellaneous);
}
