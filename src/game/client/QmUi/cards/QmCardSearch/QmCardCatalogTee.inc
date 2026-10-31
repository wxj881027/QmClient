// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_tee_glow[] = {
		{Localizable("Custom color"), nullptr, "自定义颜色"},
		{Localizable("Off"), nullptr, "关"},
		{Localizable("Rainbow"), nullptr, "彩虹"},
		{Localizable("Team 0 glow"), nullptr, "未组队发光"},
		{Localizable("Team 0 glow color"), nullptr, "未组队发光颜色"},
		{Localizable("Team tee glow"), nullptr, "队伍 Tee 外发光"},
		{Localizable("Tee color"), nullptr, "Tee 颜色"},
		{Localizable("Team tee glow intensity"), nullptr, "队伍 Tee 发光强度"},
		{Localizable("Team tee glow halo size"), nullptr, "队伍 Tee 光晕大小"},
		{nullptr, "qm_team_tee_glow", nullptr},
		{nullptr, "qm_team_tee_glow_color", nullptr},
		{nullptr, "qm_team_tee_glow_team0_mode", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:tee-glow", deck_tee_glow);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_tee_identity[] = {
		{Localizable("Body"), nullptr, "身体"},
		{Localizable("Choose default eyes when joining a server"), nullptr, "进入服务器时的默认表情"},
		{Localizable("Copy skin from dummy"), nullptr, "复制分身皮肤"},
		{Localizable("Copy skin from player"), nullptr, "复制玩家皮肤"},
		{Localizable("Create a random skin"), nullptr, "随机创造皮肤"},
		{Localizable("Custom colors"), nullptr, "自定义颜色"},
		{Localizable("Dummy"), nullptr, "分身"},
		{Localizable("Feet"), nullptr, "脚"},
		{Localizable("Player"), nullptr, "玩家"},
		{Localizable("Random Colors"), nullptr, "随机颜色"},
		{Localizable("Skin could not be found."), nullptr, "未找到皮肤。"},
		{Localizable("Skin could not be loaded due to an error. Check the local console for details."), nullptr, "加载皮肤时出错。检查本地控制台以获取详细信息。"},
		{Localizable("Skin is loading."), nullptr, "皮肤正在加载。"},
		{Localizable("This skin name cannot be used."), nullptr, "此皮肤名称不能使用。"},
		{Localizable("Your skin"), nullptr, "你的皮肤"},
		{nullptr, "cl_dummy", nullptr},
		{nullptr, "dummy7_use_custom_color_body", nullptr},
		{nullptr, "dummy7_use_custom_color_feet", nullptr},
		{nullptr, "dummy_color_body", nullptr},
		{nullptr, "dummy_color_feet", nullptr},
		{nullptr, "dummy_country", nullptr},
		{nullptr, "dummy_default_eyes", nullptr},
		{nullptr, "dummy_name", nullptr},
		{nullptr, "dummy_skin", nullptr},
		{nullptr, "dummy_use_custom_color", nullptr},
		{nullptr, "player7_use_custom_color_body", nullptr},
		{nullptr, "player7_use_custom_color_feet", nullptr},
		{nullptr, "player_color_body", nullptr},
		{nullptr, "player_color_feet", nullptr},
		{nullptr, "player_country", nullptr},
		{nullptr, "player_default_eyes", nullptr},
		{nullptr, "player_name", nullptr},
		{nullptr, "player_skin", nullptr},
		{nullptr, "player_use_custom_color", nullptr},
		{nullptr, "qm_cycle_tee_hue", nullptr},
		{nullptr, "qm_cycle_tee_hue_dummy", nullptr},
		{nullptr, "qm_cycle_tee_hue_speed", nullptr},
		{nullptr, "qm_rainbow_tees", nullptr},
		{nullptr, "qm_ui_motion_level", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:tee-identity", deck_tee_identity);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_tee_skin_list[] = {
		{Localizable("Add to queue"), nullptr, "加入队列"},
		{Localizable("All"), nullptr, "全体"},
		{Localizable("Clear recent skins"), nullptr, "清空最近使用的皮肤"},
		{Localizable("Edit skin texture"), nullptr, "编辑皮肤贴图"},
		{Localizable("Favorites"), nullptr, "收藏夹"},
		{Localizable("Name"), nullptr, "名称"},
		{Localizable("No recently used skins"), nullptr, "暂无最近使用的皮肤"},
		{Localizable("No skins match your filter criteria"), nullptr, "没有皮肤满足设置的筛选条件"},
		{Localizable("Open the directory to add custom skins"), nullptr, "打开用以添加自定义皮肤的文件夹路径"},
		{Localizable("Recent"), nullptr, "最近"},
		{Localizable("Refresh"), nullptr, "刷新"},
		{Localizable("Remove from queue"), nullptr, "从队列移除"},
		{Localizable("Reset filter"), nullptr, "重置筛选"},
		{Localizable("Skin Database"), nullptr, "皮肤数据库"},
		{Localizable("Skin could not be found."), nullptr, "未找到皮肤。"},
		{Localizable("Skin could not be loaded due to an error. Check the local console for details."), nullptr, "加载皮肤时出错。检查本地控制台以获取详细信息。"},
		{Localizable("Skin is loading."), nullptr, "皮肤正在加载。"},
		{Localizable("Skin sort"), nullptr, "皮肤排序"},
		{Localizable("This skin name cannot be used."), nullptr, "此皮肤名称不能使用。"},
		{Localizable("Time"), nullptr, "时间"},
		{nullptr, "cl_skin_filter_string", nullptr},
		{nullptr, "debug", nullptr},
		{nullptr, "dummy_color_body", nullptr},
		{nullptr, "dummy_color_feet", nullptr},
		{nullptr, "dummy_default_eyes", nullptr},
		{nullptr, "dummy_skin", nullptr},
		{nullptr, "dummy_use_custom_color", nullptr},
		{nullptr, "player_color_body", nullptr},
		{nullptr, "player_color_feet", nullptr},
		{nullptr, "player_default_eyes", nullptr},
		{nullptr, "player_skin", nullptr},
		{nullptr, "player_use_custom_color", nullptr},
		{nullptr, "qm_settings_prewarm", nullptr},
		{nullptr, "qm_skin_show_metadata", nullptr},
		{nullptr, "qm_skin_sort_mode", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:tee-skin-list", deck_tee_skin_list);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_tee_skin_options[] = {
		{Localizable("Download community skins"), nullptr, "下载社区皮肤"},
		{Localizable("Download skins"), nullptr, "下载官方皮肤"},
		{Localizable("Fat skins (DDFat)"), nullptr, "胖胖的皮肤（DDFat）"},
		{Localizable("Show skin date and author"), nullptr, "显示皮肤日期和作者"},
		{Localizable("Skin prefix"), nullptr, "皮肤名称前缀"},
		{Localizable("Vanilla skins only"), nullptr, "只显示原生皮肤"},
		{nullptr, "cl_download_community_skins", nullptr},
		{nullptr, "cl_download_skins", nullptr},
		{nullptr, "cl_fat_skins", nullptr},
		{nullptr, "cl_skin_prefix", nullptr},
		{nullptr, "cl_vanilla_skins_only", nullptr},
		{nullptr, "qm_skin_show_metadata", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:tee-skin-options", deck_tee_skin_options);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_tee_skin_queue[] = {
		{Localizable("0 disables timed rotation; skins only switch on random start events"), nullptr, "0 表示不做时间轮换；仅在随机起点事件时切换皮肤"},
		{Localizable("Apply a preset first"), nullptr, "先应用一个预设"},
		{Localizable("Apply a random skin from the queue"), nullptr, "从皮肤队列随机应用一个皮肤"},
		{Localizable("Apply a writable preset and edit first"), nullptr, "先应用一个可编辑的预设"},
		{Localizable("Apply to dummy"), nullptr, "应用到分身"},
		{Localizable("Apply to player"), nullptr, "应用到本体"},
		{Localizable("Clear current queue"), nullptr, "清空当前队列"},
		{Localizable("Custom"), nullptr, "自定义"},
		{Localizable("Default preset"), nullptr, "默认预设"},
		{Localizable("Delete this preset"), nullptr, "删除该预设"},
		{Localizable("Dummy"), nullptr, "分身"},
		{Localizable("Enable rotation"), nullptr, "启用轮换"},
		{Localizable("Enable skin queue rotation"), nullptr, "启用皮肤队列轮换"},
		{Localizable("No presets yet"), nullptr, "暂无预设"},
		{Localizable("Open rename dialog"), nullptr, "打开重命名弹窗"},
		{Localizable("Player"), nullptr, "玩家"},
		{Localizable("Preset bar"), nullptr, "预设栏"},
		{Localizable("Queue is empty"), nullptr, "队列为空"},
		{Localizable("Queue preset: %s"), nullptr, "队列预设：%s"},
		{Localizable("Random skin on map join"), nullptr, "进图随机换肤"},
		{Localizable("Remove from queue"), nullptr, "从队列移除"},
		{Localizable("Rotate all server player skins"), nullptr, "轮换服务器所有人皮肤"},
		{Localizable("Save changes back to this preset"), nullptr, "将改动保存回此预设"},
		{Localizable("Save current queue as a new preset"), nullptr, "一键保存当前队列为新预设"},
		{Localizable("Server preset"), nullptr, "服务器预设"},
		{Localizable("Start at a random queue position on every map join"), nullptr, "每次进图从队列随机位置开始轮换（分身连接、重新启用队列时同样生效）"},
		{Localizable("Switch interval"), nullptr, "切换间隔"},
		{Localizable("Shuffle skin queue"), nullptr, "打乱皮肤队列"},
		{Localizable("Select a preset, then apply it to player or dummy"), nullptr, "先选择预设，再应用到本体或分身"},
		{nullptr, "dummy_color_body", nullptr},
		{nullptr, "dummy_color_feet", nullptr},
		{nullptr, "dummy_skin", nullptr},
		{nullptr, "dummy_use_custom_color", nullptr},
		{nullptr, "player_color_body", nullptr},
		{nullptr, "player_color_feet", nullptr},
		{nullptr, "player_skin", nullptr},
		{nullptr, "player_use_custom_color", nullptr},
		{nullptr, "qm_dummy_skin_queue_enabled", nullptr},
		{nullptr, "qm_dummy_skin_queue_index", nullptr},
		{nullptr, "qm_dummy_skin_queue_interval", nullptr},
		{nullptr, "qm_dummy_skin_queue_random_join", nullptr},
		{nullptr, "qm_skin_queue_enabled", nullptr},
		{nullptr, "qm_skin_queue_index", nullptr},
		{nullptr, "qm_skin_queue_interval", nullptr},
		{nullptr, "qm_skin_queue_random_join", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:tee-skin-queue", deck_tee_skin_queue);
}
