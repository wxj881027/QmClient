// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_appearance_chat_messages[] = {
		{Localizable("Client message"), nullptr, "客户端消息"},
		{Localizable("Friend heart"), nullptr, "好友爱心"},
		{Localizable("Friend message"), nullptr, "好友消息"},
		{Localizable("Hide system message prefix"), nullptr, "隐藏系统消息前缀"},
		{Localizable("Highlighted message"), nullptr, "被提及消息"},
		{Localizable("Normal message"), nullptr, "普通消息"},
		{Localizable("System message"), nullptr, "系统消息"},
		{Localizable("Team message"), nullptr, "队伍消息"},
		{nullptr, "cl_message_client_color", nullptr},
		{nullptr, "cl_message_client_gradient", nullptr},
		{nullptr, "cl_message_color", nullptr},
		{nullptr, "cl_message_friend", nullptr},
		{nullptr, "cl_message_friend_color", nullptr},
		{nullptr, "cl_message_friend_gradient", nullptr},
		{nullptr, "cl_message_friend_heart_color", nullptr},
		{nullptr, "cl_message_gradient", nullptr},
		{nullptr, "cl_message_highlight_color", nullptr},
		{nullptr, "cl_message_highlight_gradient", nullptr},
		{nullptr, "cl_message_system_color", nullptr},
		{nullptr, "cl_message_system_gradient", nullptr},
		{nullptr, "cl_message_team_color", nullptr},
		{nullptr, "cl_message_team_gradient", nullptr},
		{nullptr, "cl_show_chat_system", nullptr},
		{nullptr, "qm_chat_hide_system_prefix", nullptr},
		{nullptr, "qm_show_chat_client", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:appearance-chat-messages", deck_appearance_chat_messages);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_appearance_hook_collision_main[] = {
		{Localizable("A Tee"), nullptr, "其他玩家"},
		{Localizable("Always show other players' hook collision lines"), nullptr, "始终显示其他玩家的钩子辅助线"},
		{Localizable("Always show own player's hook collision line"), nullptr, "始终显示自己的钩子辅助线"},
		{Localizable("Colors of the hook collision line, in case of a possible collision with:"), nullptr, "钩子辅助线颜色（瞄准以下目标时）："},
		{Localizable("Hook collision line opacity"), nullptr, "钩子辅助线不透明度"},
		{Localizable("Hook line tip"), nullptr, "辅助线提示"},
		{Localizable("Nothing hookable"), nullptr, "未瞄准可钩物体"},
		{Localizable("Pressed hook line color; always used when custom pressed colors are enabled"), nullptr, "按下后颜色；开启自定义按下后颜色时始终生效"},
		{Localizable("Pressed: A Tee"), nullptr, "按下后：其他玩家"},
		{Localizable("Pressed: Hook line tip"), nullptr, "按下后：辅助线提示"},
		{Localizable("Pressed: Nothing hookable"), nullptr, "按下后：未瞄准可钩物体"},
		{Localizable("Pressed: Something hookable"), nullptr, "按下后：可钩物体"},
		{Localizable("Use the custom colors below while the hook key is held"), nullptr, "按住钩子键时使用下方的自定义颜色"},
		{Localizable("Show other players' hook collision lines"), nullptr, "显示其他玩家的钩子辅助线"},
		{Localizable("Show own player's hook collision line"), nullptr, "显示自己的钩子辅助线"},
		{Localizable("Something hookable"), nullptr, "可钩物体"},
		{Localizable("Width of others' hook collision line"), nullptr, "其他玩家的钩子辅助线线宽度"},
		{Localizable("Width of your own hook collision line"), nullptr, "自己的钩子辅助线线宽度"},
		{Localizable("Your movements are not taken into account when calculating the line colors"), nullptr, "计算线条颜色时不考虑你的移动"},
		{Localizable("Custom pressed hook line colors"), nullptr, "自定义按下钩线颜色"},
		{nullptr, "cl_hook_coll_alpha", nullptr},
		{nullptr, "cl_hook_coll_color_hookable_coll", nullptr},
		{nullptr, "cl_hook_coll_color_no_coll", nullptr},
		{nullptr, "cl_hook_coll_color_tee_coll", nullptr},
		{nullptr, "cl_hook_coll_size", nullptr},
		{nullptr, "cl_hook_coll_size_other", nullptr},
		{nullptr, "cl_hook_coll_tip_color", nullptr},
		{nullptr, "cl_show_hook_coll_other", nullptr},
		{nullptr, "cl_show_hook_coll_own", nullptr},
		{nullptr, "qm_hook_coll_pressed_custom", nullptr},
		{nullptr, "qm_hook_coll_pressed_hookable", nullptr},
		{nullptr, "qm_hook_coll_pressed_no_coll", nullptr},
		{nullptr, "qm_hook_coll_pressed_tee", nullptr},
		{nullptr, "qm_hook_coll_pressed_tip", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:appearance-hook-collision-main", deck_appearance_hook_collision_main);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_appearance_hook_collision_preview[] = {
		{Localizable("Preview 'Hook collisions' being pressed"), nullptr, "预览按下时的钩子辅助线"},
		{nullptr, "cl_hook_coll_alpha", nullptr},
		{nullptr, "cl_hook_coll_color_hookable_coll", nullptr},
		{nullptr, "cl_hook_coll_color_no_coll", nullptr},
		{nullptr, "cl_hook_coll_color_tee_coll", nullptr},
		{nullptr, "cl_hook_coll_size", nullptr},
		{nullptr, "cl_hook_coll_size_other", nullptr},
		{nullptr, "cl_hook_coll_tip_color", nullptr},
		{nullptr, "dummy_color_body", nullptr},
		{nullptr, "dummy_color_feet", nullptr},
		{nullptr, "dummy_skin", nullptr},
		{nullptr, "dummy_use_custom_color", nullptr},
		{nullptr, "player_color_body", nullptr},
		{nullptr, "player_color_feet", nullptr},
		{nullptr, "player_skin", nullptr},
		{nullptr, "player_use_custom_color", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:appearance-hook-collision-preview", deck_appearance_hook_collision_preview);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_appearance_hud_ddrace[] = {
		{Localizable("HUD rainbow colors"), nullptr, "HUD 彩虹色"},
		{Localizable("Opacity of freeze bars inside freeze"), nullptr, "解冻进度条不透明度"},
		{Localizable("Show DDRace HUD"), nullptr, "显示 DDRace HUD"},
		{Localizable("Show client IDs (scoreboard, chat, spectating)"), nullptr, "显示客户端 ID（计分板、聊天、观战）"},
		{Localizable("Show dummy actions"), nullptr, "显示分身状态"},
		{Localizable("Show freeze bars"), nullptr, "显示解冻进度条"},
		{Localizable("Show jumps indicator"), nullptr, "显示可跳跃次数"},
		{Localizable("Show number of spectators"), nullptr, "显示旁观者人数"},
		{Localizable("Show player position"), nullptr, "显示玩家坐标"},
		{Localizable("Show player speed"), nullptr, "显示玩家速度"},
		{Localizable("Show player target angle"), nullptr, "显示玩家瞄准角度"},
		{nullptr, "cl_freezebars_alpha_inside_freeze", nullptr},
		{nullptr, "cl_hud_rainbow_colors", nullptr},
		{nullptr, "cl_show_freeze_bars", nullptr},
		{nullptr, "cl_show_ids", nullptr},
		{nullptr, "cl_showhud_ddrace", nullptr},
		{nullptr, "cl_showhud_dummy_actions", nullptr},
		{nullptr, "cl_showhud_jumps_indicator", nullptr},
		{nullptr, "cl_showhud_player_angle", nullptr},
		{nullptr, "cl_showhud_player_position", nullptr},
		{nullptr, "cl_showhud_player_speed", nullptr},
		{nullptr, "cl_showhud_spectator_count", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:appearance-hud-ddrace", deck_appearance_hud_ddrace);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_appearance_hud_main[] = {
		{Localizable("Authed name color in scoreboard"), nullptr, "管理权限者名字在计分板上的颜色"},
		{Localizable("Clan color in friends list"), nullptr, "好友列表中战队成员条目颜色"},
		{Localizable("Friend color in friends list"), nullptr, "好友列表中好友条目颜色"},
		{Localizable("Same clan color in scoreboard"), nullptr, "相同战队在计分板上的颜色"},
		{Localizable("Scoreboard"), nullptr, "计分板"},
		{Localizable("Show health, shields and ammo"), nullptr, "显示生命、护甲与弹药栏"},
		{Localizable("Show ingame HUD"), nullptr, "显示 HUD"},
		{Localizable("Show local time always"), nullptr, "始终显示本地时间"},
		{Localizable("Show score"), nullptr, "显示分数"},
		{Localizable("Show spectator cursor"), nullptr, "显示旁观者光标"},
		{Localizable("Show votes window after voting"), nullptr, "投票后不关闭投票窗口"},
		{nullptr, "cl_authed_player_color", nullptr},
		{nullptr, "cl_friends_list_clan_color", nullptr},
		{nullptr, "cl_friends_list_friend_color", nullptr},
		{nullptr, "cl_same_clan_color", nullptr},
		{nullptr, "cl_show_local_time_always", nullptr},
		{nullptr, "cl_show_votes_after_voting", nullptr},
		{nullptr, "cl_showhud", nullptr},
		{nullptr, "cl_showhud_healthammo", nullptr},
		{nullptr, "cl_showhud_score", nullptr},
		{nullptr, "cl_spec_cursor", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:appearance-hud-main", deck_appearance_hud_main);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_appearance_info_messages[] = {
		{Localizable("Highlight Color"), nullptr, "相关信息颜色"},
		{Localizable("Normal Color"), nullptr, "非相关信息颜色"},
		{Localizable("Show finish messages"), nullptr, "显示完成消息"},
		{Localizable("Show kill messages"), nullptr, "显示击杀消息"},
		{nullptr, "cl_kill_message_highlight_color", nullptr},
		{nullptr, "cl_kill_message_normal_color", nullptr},
		{nullptr, "cl_show_finish_messages", nullptr},
		{nullptr, "cl_showkillmessages", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:appearance-info-messages", deck_appearance_info_messages);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_appearance_laser_colors[] = {
		{Localizable("Door Laser Inner Color"), nullptr, "激光门实心颜色"},
		{Localizable("Door Laser Outline Color"), nullptr, "激光门外框颜色"},
		{Localizable("Dragger Inner Color"), nullptr, "光索实心颜色"},
		{Localizable("Dragger Outline Color"), nullptr, "光索外框颜色"},
		{Localizable("Entities"), nullptr, "实体"},
		{Localizable("Freeze Laser Inner Color"), nullptr, "冻结激光内色"},
		{Localizable("Freeze Laser Outline Color"), nullptr, "冻结激光外框色"},
		{Localizable("Rifle Laser Inner Color"), nullptr, "激光枪弹道实心颜色"},
		{Localizable("Rifle Laser Outline Color"), nullptr, "激光枪弹道外框颜色"},
		{Localizable("Shotgun Laser Inner Color"), nullptr, "霰弹枪弹道实心颜色"},
		{Localizable("Shotgun Laser Outline Color"), nullptr, "霰弹枪弹道外框颜色"},
		{Localizable("Weapons"), nullptr, "武器"},
		{nullptr, "cl_laser_door_inner_color", nullptr},
		{nullptr, "cl_laser_door_outline_color", nullptr},
		{nullptr, "cl_laser_dragger_inner_color", nullptr},
		{nullptr, "cl_laser_dragger_outline_color", nullptr},
		{nullptr, "cl_laser_freeze_inner_color", nullptr},
		{nullptr, "cl_laser_freeze_outline_color", nullptr},
		{nullptr, "cl_laser_rifle_inner_color", nullptr},
		{nullptr, "cl_laser_rifle_outline_color", nullptr},
		{nullptr, "cl_laser_sg_inner_color", nullptr},
		{nullptr, "cl_laser_sg_outline_color", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:appearance-laser-colors", deck_appearance_laser_colors);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_appearance_laser_enhanced[] = {
		{Localizable("Glow intensity"), nullptr, "辉光强度"},
		{Localizable("Laser effect enhancement"), nullptr, "激光效果增强"},
		{Localizable("Laser size"), nullptr, "激光大小"},
		{Localizable("Opacity"), nullptr, "不透明度"},
		{Localizable("Pulse amplitude"), nullptr, "脉冲幅度"},
		{Localizable("Pulse speed"), nullptr, "脉冲速度"},
		{Localizable("Rounded caps"), nullptr, "圆角端点"},
		{nullptr, "qm_laser_alpha", nullptr},
		{nullptr, "qm_laser_enhanced", nullptr},
		{nullptr, "qm_laser_glow_intensity", nullptr},
		{nullptr, "qm_laser_pulse_amplitude", nullptr},
		{nullptr, "qm_laser_pulse_speed", nullptr},
		{nullptr, "qm_laser_round_caps", nullptr},
		{nullptr, "qm_laser_size", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:appearance-laser-enhanced", deck_appearance_laser_enhanced);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_appearance_laser_preview[] = {
		{nullptr, "cl_laser_door_inner_color", nullptr},
		{nullptr, "cl_laser_door_outline_color", nullptr},
		{nullptr, "cl_laser_dragger_inner_color", nullptr},
		{nullptr, "cl_laser_dragger_outline_color", nullptr},
		{nullptr, "cl_laser_freeze_inner_color", nullptr},
		{nullptr, "cl_laser_freeze_outline_color", nullptr},
		{nullptr, "cl_laser_rifle_inner_color", nullptr},
		{nullptr, "cl_laser_rifle_outline_color", nullptr},
		{nullptr, "cl_laser_sg_inner_color", nullptr},
		{nullptr, "cl_laser_sg_outline_color", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:appearance-laser-preview", deck_appearance_laser_preview);
}
