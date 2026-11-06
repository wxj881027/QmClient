// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_camera_view[] = {
		{Localizable("Aspect ratio preset"), nullptr, "纵横比预设"},
		{Localizable("Current aspect ratio:"), nullptr, "当前宽高比："},
		{Localizable("Current aspect ratio: Show default"), nullptr, "当前宽高比：显示默认"},
		{Localizable("Custom"), nullptr, "自定义"},
		{Localizable("Custom aspect ratio"), nullptr, "自定义宽高比"},
		{Localizable("Drift direction"), nullptr, "漂移方向"},
		{Localizable("Drift intensity"), nullptr, "漂移强度"},
		{Localizable("Drift smoothness"), nullptr, "漂移平滑度"},
		{Localizable("Dynamic FOV intensity"), nullptr, "动态视野强度"},
		{Localizable("Dynamic FOV smoothness"), nullptr, "动态视野平滑度"},
		{Localizable("Enable camera drift"), nullptr, "启用镜头漂移"},
		{Localizable("Enable dynamic FOV"), nullptr, "启用动态视野"},
		{Localizable("Instant zoom reverse"), nullptr, "反向缩放立即生效"},
		{Localizable("Off"), nullptr, "关"},
		{Localizable("UI scale"), nullptr, "UI 大小"},
		{nullptr, "qm_aspect_preset", nullptr},
		{nullptr, "qm_aspect_ratio", nullptr},
		{nullptr, "qm_camera_drift", nullptr},
		{nullptr, "qm_camera_drift_amount", nullptr},
		{nullptr, "qm_camera_drift_reverse", nullptr},
		{nullptr, "qm_camera_drift_smoothness", nullptr},
		{nullptr, "qm_dynamic_fov", nullptr},
		{nullptr, "qm_dynamic_fov_amount", nullptr},
		{nullptr, "qm_dynamic_fov_smoothness", nullptr},
		{nullptr, "qm_ui_scale", nullptr},
		{nullptr, "qm_zoom_instant_reverse", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:camera_view", qm_camera_view);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_chat_bubble[] = {
		{Localizable("Animation"), nullptr, "动画效果"},
		{Localizable("Background color"), nullptr, "背景颜色"},
		{Localizable("Bounce"), nullptr, "弹跳"},
		{Localizable("Bubble opacity"), nullptr, "气泡不透明度"},
		{Localizable("Candy gradient"), nullptr, "QQ 糖果渐变"},
		{Localizable("Cloud bubble"), nullptr, "云朵 拟真 聚集 散开"},
		{Localizable("Comic dialogue"), nullptr, "漫画对白"},
		{Localizable("Dissolve"), nullptr, "消散"},
		{Localizable("Duration"), nullptr, "持续时间"},
		{Localizable("Font size"), nullptr, "字体大小"},
		{Localizable("Show chat bubbles above players"), nullptr, "在玩家头顶显示聊天气泡"},
		{Localizable("Shrink"), nullptr, "收缩"},
		{Localizable("Text color"), nullptr, "文本颜色"},
		{Localizable("Use custom colors"), nullptr, "自定义颜色"},
		{nullptr, "qm_chat_bubble", nullptr},
		{nullptr, "qm_chat_bubble_alpha", nullptr},
		{nullptr, "qm_chat_bubble_animation", nullptr},
		{nullptr, "qm_chat_bubble_bg_color", nullptr},
		{nullptr, "qm_chat_bubble_custom_colors", nullptr},
		{nullptr, "qm_chat_bubble_duration", nullptr},
		{nullptr, "qm_chat_bubble_font_size", nullptr},
		{nullptr, "qm_chat_bubble_style", nullptr},
		{nullptr, "qm_chat_bubble_text_color", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:chat_bubble", qm_chat_bubble);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_collision_hitbox[] = {
		{Localizable("All players"), nullptr, "所有人"},
		{Localizable("Freeze border color"), nullptr, "冻结边框颜色"},
		{Localizable("Freeze laser collision volume"), nullptr, "冻结激光碰撞体积"},
		{Localizable("Freeze projectile collision volume"), nullptr, "冻结子弹 冻结投射物 冻结散弹 碰撞体积"},
		{Localizable("Hammer interaction"), nullptr, "锤子交互"},
		{Localizable("Hook interaction"), nullptr, "钩索交互"},
		{Localizable("Laser / shotgun interaction"), nullptr, "激光/霰弹枪交互"},
		{Localizable("Local + Dummy"), nullptr, "本地 + 分身"},
		{Localizable("Local only"), nullptr, "仅本地"},
		{Localizable("Map danger border"), nullptr, "地图危险边界"},
		{Localizable("Opacity"), nullptr, "不透明度"},
		{Localizable("Pickup range"), nullptr, "拾取物范围"},
		{Localizable("Player range"), nullptr, "玩家范围"},
		{Localizable("Projectile / explosion range"), nullptr, "投射物/爆炸范围"},
		{Localizable("Show hitbox mode"), nullptr, "显示碰撞箱模式"},
		{Localizable("Tee collision (Tee to Tee)"), nullptr, "Tee 碰撞（Tee 与 Tee）"},
		{Localizable("Tee death probe"), nullptr, "Tee 死亡探针"},
		{Localizable("Tee freeze probe (Tee to Freeze)"), nullptr, "Tee 冻结探针（Tee 与 Freeze）"},
		{Localizable("Tee hitbox color"), nullptr, "Tee 碰撞箱颜色"},
		{Localizable("Weapon range color"), nullptr, "武器范围颜色"},
		{nullptr, "qm_hitbox_alpha", nullptr},
		{nullptr, "qm_hitbox_color_freeze", nullptr},
		{nullptr, "qm_hitbox_color_tee", nullptr},
		{nullptr, "qm_hitbox_color_weapon", nullptr},
		{nullptr, "qm_hitbox_mode", nullptr},
		{nullptr, "qm_hitbox_player_scope", nullptr},
		{nullptr, "qm_hitbox_show_freeze_lasers", nullptr},
		{nullptr, "qm_hitbox_show_freeze_projectiles", nullptr},
		{nullptr, "qm_hitbox_show_hammer", nullptr},
		{nullptr, "qm_hitbox_show_hook", nullptr},
		{nullptr, "qm_hitbox_show_lasers", nullptr},
		{nullptr, "qm_hitbox_show_map", nullptr},
		{nullptr, "qm_hitbox_show_pickups", nullptr},
		{nullptr, "qm_hitbox_show_projectiles", nullptr},
		{nullptr, "qm_hitbox_show_tee_collision", nullptr},
		{nullptr, "qm_hitbox_show_tee_death", nullptr},
		{nullptr, "qm_hitbox_show_tee_freeze", nullptr},
		{nullptr, "qm_show_collision_hitbox", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:collision_hitbox", qm_collision_hitbox);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_focus_mode[] = {
		{Localizable("Audio"), nullptr, "语音音效"},
		{Localizable("Chat"), nullptr, "聊天"},
		{Localizable("Enable Zen mode"), nullptr, "启用禅模式"},
		{Localizable("Hide Echo messages"), nullptr, "隐藏 Echo 消息"},
		{Localizable("Hide HUD"), nullptr, "隐藏 HUD"},
		{Localizable("Hide death/respawn effects"), nullptr, "隐藏死亡/重生特效"},
		{Localizable("Hide direction indicators"), nullptr, "隐藏方向"},
		{Localizable("Hide explosion effects"), nullptr, "隐藏炮击特效"},
		{Localizable("Hide freeze effects"), nullptr, "隐藏冻结特效"},
		{Localizable("Hide guide lines"), nullptr, "隐藏辅助线"},
		{Localizable("Hide hammer effects"), nullptr, "隐藏锤击特效"},
		{Localizable("Hide join/version prompts"), nullptr, "隐藏入场/版本提示"},
		{Localizable("Hide jump effects"), nullptr, "隐藏跳跃特效"},
		{Localizable("Hide kill/finish messages"), nullptr, "隐藏死亡/完成消息"},
		{Localizable("Hide map progress"), nullptr, "隐藏地图进度"},
		{Localizable("Hide nameplates"), nullptr, "隐藏名字板"},
		{Localizable("Hide names"), nullptr, "隐藏名字"},
		{Localizable("Hide player messages"), nullptr, "隐藏玩家消息"},
		{Localizable("Hide scoreboard"), nullptr, "隐藏计分板"},
		{Localizable("Hide server prompt notifications"), nullptr, "隐藏服务器提示通知"},
		{Localizable("Hide weapon muzzle flashes"), nullptr, "隐藏武器火焰"},
		{Localizable("Interface"), nullptr, "界面"},
		{Localizable("Mute death/respawn sounds"), nullptr, "静音死亡/重生"},
		{Localizable("Mute hammer sounds"), nullptr, "静音锤击"},
		{Localizable("Mute jump sounds"), nullptr, "静音跳跃"},
		{Localizable("Players"), nullptr, "玩家"},
		{Localizable("Visuals"), nullptr, "视觉"},
		{Localizable("Zen mode key"), nullptr, "禅模式按键"},
		{nullptr, "qm_focus_mode", nullptr},
		{nullptr, "qm_focus_mode_hide_chat", nullptr},
		{nullptr, "qm_focus_mode_hide_direction_indicators", nullptr},
		{nullptr, "qm_focus_mode_hide_echo", nullptr},
		{nullptr, "qm_focus_mode_hide_explosion_effects", nullptr},
		{nullptr, "qm_focus_mode_hide_freeze_effects", nullptr},
		{nullptr, "qm_focus_mode_hide_guide_lines", nullptr},
		{nullptr, "qm_focus_mode_hide_hammer_effects", nullptr},
		{nullptr, "qm_focus_mode_hide_hud", nullptr},
		{nullptr, "qm_focus_mode_hide_info_messages", nullptr},
		{nullptr, "qm_focus_mode_hide_jump_effects", nullptr},
		{nullptr, "qm_focus_mode_hide_kill_effects", nullptr},
		{nullptr, "qm_focus_mode_hide_map_progress", nullptr},
		{nullptr, "qm_focus_mode_hide_muzzle_effects", nullptr},
		{nullptr, "qm_focus_mode_hide_nameplates", nullptr},
		{nullptr, "qm_focus_mode_hide_names", nullptr},
		{nullptr, "qm_focus_mode_hide_scoreboard", nullptr},
		{nullptr, "qm_focus_mode_hide_system_info_messages", nullptr},
		{nullptr, "qm_focus_mode_hide_system_messages", nullptr},
		{nullptr, "qm_focus_mode_mute_death_sounds", nullptr},
		{nullptr, "qm_focus_mode_mute_hammer_sounds", nullptr},
		{nullptr, "qm_focus_mode_mute_jump_sounds", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:focus_mode", qm_focus_mode);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_spectator_mode[] = {
		{Localizable("Spectate mode"), nullptr, "观战模式"},
		{Localizable("Smooth free spectator camera"), nullptr, "平滑自由观战镜头"},
		{Localizable("Cinematic smoothness"), nullptr, "电影镜头平滑度"},
		{Localizable("List friends first in the spectator menu"), nullptr, "观战菜单好友优先"},
		{nullptr, "qm_cinematic_camera", nullptr},
		{nullptr, "qm_cinematic_camera_smoothness", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:spectator_mode", qm_spectator_mode);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_streamer[] = {
		{Localizable("Filter sensitive words"), nullptr, "过滤内置敏感词 屏蔽词 反规避"},
		{nullptr, "qm_streamer_filter_words", nullptr},
		{nullptr, "qm_streamer_words_file", "文件词表 读取文件 外部词库"},
		{Localizable("Replace non-friend names with ID"), nullptr, "用 ID 替换非好友名称"},
		{Localizable("Replace non-friend skins with default"), nullptr, "用默认皮肤替换非好友皮肤"},
		{Localizable("Use default flags on scoreboard"), nullptr, "在计分板上使用默认旗帜"},
		{nullptr, "qm_streamer_hide_names", nullptr},
		{nullptr, "qm_streamer_hide_skins", nullptr},
		{nullptr, "qm_streamer_scoreboard_default_flags", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:streamer", qm_streamer);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_weapon_animation[] = {
		{Localizable("All players"), nullptr, "所有人"},
		{Localizable("Animation range"), nullptr, "动画范围"},
		{Localizable("Ease in out quad"), nullptr, "缓入缓出二次"},
		{Localizable("Ease out cubic"), nullptr, "缓出三次"},
		{Localizable("Elastic back"), nullptr, "弹性回弹"},
		{Localizable("Hammer Mode"), nullptr, "锤子模式"},
		{Localizable("Linear"), nullptr, "线性"},
		{Localizable("Local"), nullptr, "本地"},
		{Localizable("Normal", "Hammer Mode"), nullptr, "正常（不旋转）", "Hammer Mode"},
		{Localizable("Play a flip animation while reloading weapons"), nullptr, "武器装填时播放翻转动画"},
		{Localizable("Rotate with cursor", "Hammer Mode"), nullptr, "随光标旋转（垂直）", "Hammer Mode"},
		{Localizable("Rotate with cursor like gun", "Hammer Mode"), nullptr, "像枪一样随光标旋转（平行）", "Hammer Mode"},
		{Localizable("Self only"), nullptr, "仅自己"},
		{Localizable("Weapon reload animation probability"), nullptr, "武器装填动画概率"},
		{Localizable("Weapon switch animation"), nullptr, "武器切换动画"},
		{Localizable("Weapon switch distance"), nullptr, "武器切换距离"},
		{Localizable("Weapon switch duration"), nullptr, "武器切换时长"},
		{Localizable("Weapon switch easing"), nullptr, "武器切换缓动"},
		{Localizable("Weapon switch rotation"), nullptr, "武器切换旋转"},
		{nullptr, "qm_hammer_rotates_with_cursor", nullptr},
		{nullptr, "qm_weapon_reload_anim", nullptr},
		{nullptr, "qm_weapon_reload_anim_probability", nullptr},
		{nullptr, "qm_weapon_switch_anim", nullptr},
		{nullptr, "qm_weapon_switch_anim_distance", nullptr},
		{nullptr, "qm_weapon_switch_anim_duration_ms", nullptr},
		{nullptr, "qm_weapon_switch_anim_easing", nullptr},
		{nullptr, "qm_weapon_switch_anim_rotation", nullptr},
		{nullptr, "qm_weapon_switch_anim_scope", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:weapon_animation", qm_weapon_animation);
}
