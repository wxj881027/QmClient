// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_appearance_name_plate_hook_strength[] = {
		{Localizable("All"), nullptr, "全体"},
		{Localizable("Hook strength scope"), nullptr, "钩索强度范围"},
		{Localizable("Others"), nullptr, "他人"},
		{Localizable("Self"), nullptr, "自身"},
		{Localizable("Show hook strength icon indicator"), nullptr, "显示钩索强度图标"},
		{Localizable("Show hook strength number indicator"), nullptr, "显示钩索强度数值"},
		{Localizable("Size of hook strength icon and number indicator"), nullptr, "钩索强度图标与数值的大小"},
		{Localizable("Strong hook"), nullptr, "强钩"},
		{Localizable("Strong hook color"), nullptr, "强钩颜色"},
		{Localizable("Weak hook"), nullptr, "弱钩"},
		{Localizable("Weak hook color"), nullptr, "弱钩颜色"},
		{nullptr, "cl_nameplates_strong", nullptr},
		{nullptr, "cl_nameplates_strong_size", nullptr},
		{nullptr, "qm_nameplate_hook_strong_weak_scope", nullptr},
		{nullptr, "qm_nameplate_strong_hook_color", nullptr},
		{nullptr, "qm_nameplate_weak_hook_color", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:appearance-name-plate-hook-strength", deck_appearance_name_plate_hook_strength);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_appearance_name_plate_key_presses[] = {
		{Localizable("All", "Show players' key presses"), nullptr, "全体", "Show players' key presses"},
		{Localizable("None", "Show players' key presses"), nullptr, "禁用", "Show players' key presses"},
		{Localizable("Others", "Show players' key presses"), nullptr, "他人", "Show players' key presses"},
		{Localizable("Own", "Show players' key presses"), nullptr, "自身", "Show players' key presses"},
		{Localizable("Show players' key presses"), nullptr, "显示按键"},
		{Localizable("Size of key press icons"), nullptr, "按键显示图标大小"},
		{nullptr, "cl_direction_size", nullptr},
		{nullptr, "cl_show_direction", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:appearance-name-plate-key-presses", deck_appearance_name_plate_key_presses);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_appearance_name_plate_settings[] = {
		{Localizable("All", "Show name plates"), nullptr, "全体", "Show name plates"},
		{Localizable("Clan plates size"), nullptr, "战队名称大小"},
		{Localizable("Client IDs size"), nullptr, "客户端 ID 大小"},
		{Localizable("Coords size"), nullptr, "坐标大小"},
		{Localizable("Current", "Show name plates"), nullptr, "当前角色", "Show name plates"},
		{Localizable("Name plates offset"), nullptr, "昵称距离"},
		{Localizable("Name plates size"), nullptr, "昵称大小"},
		{Localizable("None", "Show name plates"), nullptr, "无", "Show name plates"},
		{Localizable("Others", "Show name plates"), nullptr, "他人", "Show name plates"},
		{Localizable("Others and own", "Show name plates"), nullptr, "他人与自己的其他角色", "Show name plates"},
		{Localizable("Own characters", "Show name plates"), nullptr, "自己的角色", "Show name plates"},
		{Localizable("Show clan above name plates"), nullptr, "显示战队名称"},
		{Localizable("Show client IDs in name plates"), nullptr, "显示客户端 ID"},
		{Localizable("Show client IDs on a separate line"), nullptr, "客户端 ID 换行显示"},
		{Localizable("Show friend icon in name plates"), nullptr, "在昵称中显示好友图标"},
		{Localizable("Show name plates"), nullptr, "显示昵称"},
		{Localizable("Use team colors for name plates"), nullptr, "用队伍颜色显示昵称"},
		{nullptr, "cl_nameplates_clan", nullptr},
		{nullptr, "cl_nameplates_clan_size", nullptr},
		{nullptr, "cl_nameplates_coords_size", nullptr},
		{nullptr, "cl_nameplates_friendmark", nullptr},
		{nullptr, "cl_nameplates_ids", nullptr},
		{nullptr, "cl_nameplates_ids_seperate_line", nullptr},
		{nullptr, "cl_nameplates_ids_size", nullptr},
		{nullptr, "cl_nameplates_offset", nullptr},
		{nullptr, "cl_nameplates_size", nullptr},
		{nullptr, "cl_nameplates_teamcolors", nullptr},
		{nullptr, "qm_nameplate_show_scope", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:appearance-name-plate-settings", deck_appearance_name_plate_settings);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_appearance_name_plate_text[] = {
		{Localizable("All players"), nullptr, "所有人"},
		{Localizable("Automatic effect LOD when crowded"), nullptr, "同屏拥挤时自动降低特效"},
		{Localizable("Border"), nullptr, "边框"},
		{Localizable("Border color"), nullptr, "边框颜色"},
		{Localizable("Border range"), nullptr, "边框范围"},
		{Localizable("Demo effects"), nullptr, "Demo 特效"},
		{Localizable("Demo target"), nullptr, "Demo 目标"},
		{Localizable("Friends only"), nullptr, "仅限好友"},
		{Localizable("Full quality nameplate count"), nullptr, "保持完整质量的名牌数量上限"},
		{Localizable("Glow"), nullptr, "辉光"},
		{Localizable("Glow color"), nullptr, "辉光颜色"},
		{Localizable("Glow range"), nullptr, "辉光范围"},
		{Localizable("Gradient"), nullptr, "渐变"},
		{Localizable("Gradient color"), nullptr, "渐变颜色"},
		{Localizable("Manual scope"), nullptr, "手动范围"},
		{Localizable("Manual target"), nullptr, "手动目标"},
		{Localizable("None"), nullptr, "无"},
		{Localizable("Off"), nullptr, "关"},
		{Localizable("Others only"), nullptr, "仅他人"},
		{Localizable("Playing effects"), nullptr, "播放效果"},
		{Localizable("Rainbow"), nullptr, "彩虹"},
		{Localizable("Self and friends"), nullptr, "自己和好友"},
		{Localizable("Self only"), nullptr, "仅自己"},
		{Localizable("Show advanced options"), nullptr, "显示高级选项"},
		{Localizable("Smart"), nullptr, "智能"},
		{Localizable("Spectate effects"), nullptr, "观战特效"},
		{Localizable("Spectated player"), nullptr, "被观战玩家"},
		{Localizable("Spectated player and friends"), nullptr, "被观战的玩家与好友"},
		{nullptr, "qm_nameplate_advanced", nullptr},
		{nullptr, "qm_nameplate_effect_auto_lod", nullptr},
		{nullptr, "qm_nameplate_effect_lod_threshold", nullptr},
		{nullptr, "qm_nameplate_text_border_color", nullptr},
		{nullptr, "qm_nameplate_text_border_range", nullptr},
		{nullptr, "qm_nameplate_text_demo_mode", nullptr},
		{nullptr, "qm_nameplate_text_demo_target", nullptr},
		{nullptr, "qm_nameplate_text_effects", nullptr},
		{nullptr, "qm_nameplate_text_glow_color", nullptr},
		{nullptr, "qm_nameplate_text_glow_range", nullptr},
		{nullptr, "qm_nameplate_text_gradient_color", nullptr},
		{nullptr, "qm_nameplate_text_playing_scope", nullptr},
		{nullptr, "qm_nameplate_text_spectate_scope", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:appearance-name-plate-text", deck_appearance_name_plate_text);
}
