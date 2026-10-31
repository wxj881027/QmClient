// 未精确映射到单一渲染函数的卡片：索引只有配置名/绑定命令，集中声明。

namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_appearance_chat_preview[] = {
		{nullptr, "cl_chat_background_color", nullptr},
		{nullptr, "cl_chat_old", nullptr},
		{nullptr, "cl_chat_width", nullptr},
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
		{nullptr, "cl_show_chat_friends", nullptr},
		{nullptr, "cl_show_chat_system", nullptr},
		{nullptr, "cl_show_chat_team_members_only", nullptr},
		{nullptr, "cl_show_ids", nullptr},
		{nullptr, "qm_chat_hide_system_prefix", nullptr},
		{nullptr, "qm_show_chat_client", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:appearance-chat-preview", deck_appearance_chat_preview);
}

namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_appearance_name_plate_preview[] = {
		{Localizable("Free move"), nullptr, "自由移动"},
		{Localizable("Preview dummy nameplate"), nullptr, "预览分身名牌"},
		{Localizable("Preview player nameplate"), nullptr, "预览玩家名牌"},
		{nullptr, "cl_dummy", nullptr},
		{nullptr, "qm_nameplate_clan_offset_x", nullptr},
		{nullptr, "qm_nameplate_clan_offset_y", nullptr},
		{nullptr, "qm_nameplate_coords_offset_x", nullptr},
		{nullptr, "qm_nameplate_coords_offset_y", nullptr},
		{nullptr, "qm_nameplate_free_move", nullptr},
		{nullptr, "qm_nameplate_free_move_x", nullptr},
		{nullptr, "qm_nameplate_free_move_y", nullptr},
		{nullptr, "qm_nameplate_hook_offset_x", nullptr},
		{nullptr, "qm_nameplate_hook_offset_y", nullptr},
		{nullptr, "qm_nameplate_keys_offset_x", nullptr},
		{nullptr, "qm_nameplate_keys_offset_y", nullptr},
		{nullptr, "qm_nameplate_name_offset_x", nullptr},
		{nullptr, "qm_nameplate_name_offset_y", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:appearance-name-plate-preview", deck_appearance_name_plate_preview);
}

namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_appearance_chat_settings[] = {
		{Localizable("Always show chat"), nullptr, "始终显示聊天框"},
		{Localizable("Auto save chat log"), nullptr, "自动保存聊天记录"},
		{Localizable("Chat background color"), nullptr, "聊天背景色"},
		{Localizable("Chat font size"), nullptr, "聊天字体大小"},
		{Localizable("Chat line spacing"), nullptr, "聊天行间隔 聊天间隔"},
		{Localizable("Chat log retention days"), nullptr, "聊天记录保留天数"},
		{Localizable("Chat width"), nullptr, "聊天框宽度"},
		{Localizable("Days"), nullptr, "天"},
		{Localizable("Save unsent chat draft"), nullptr, "保存未发送的聊天草稿"},
		{Localizable("Show chat"), nullptr, "显示聊天框"},
		{Localizable("Show names in chat in team colors"), nullptr, "以队伍颜色显示玩家昵称"},
		{Localizable("Show sponsor badges in chat"), "qm_chat_sponsor_badges", "显示聊天赞助徽章 赞助图标 头衔"},
		{Localizable("Show only chat messages from friends"), nullptr, "只显示好友聊天消息"},
		{Localizable("Show only chat messages from team members"), nullptr, "只显示队伍聊天消息"},
		{Localizable("Only show messages related to me"), "qm_chat_related_only", "只显示与我相关的消息 提及 @ 私聊"},
		{Localizable("Show the chat scrollbar on the right"), nullptr, "聊天滚动条 左侧 右侧"},
		{nullptr, "qm_chat_scrollbar_right", nullptr},
		{Localizable("Use old chat style"), nullptr, "旧版聊天框"},
		{nullptr, "cl_chat_background_color", nullptr},
		{nullptr, "cl_chat_old", nullptr},
		{nullptr, "cl_chat_size", nullptr},
		{nullptr, "cl_chat_teamcolors", nullptr},
		{nullptr, "cl_chat_width", nullptr},
		{nullptr, "cl_show_chat_friends", nullptr},
		{nullptr, "cl_show_chat_team_members_only", nullptr},
		{nullptr, "cl_showchat", nullptr},
		{nullptr, "qm_chat_line_spacing", nullptr},
		{nullptr, "qm_chat_log_auto_save", nullptr},
		{nullptr, "qm_chat_log_keep_days", nullptr},
		{nullptr, "qm_chat_save_draft", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:appearance-chat-settings", deck_appearance_chat_settings);
}

namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_tclient_auto_reply[] = {
		{Localizable("Auto reply"), nullptr, "自动回复"},
		{Localizable("Automatically reply to muted players"), nullptr, "对已屏蔽发言的玩家自动回复"},
		{Localizable("Automatically reply while the window is unfocused"), nullptr, "切屏时自动回复"},
		{Localizable("I am away from the game window"), nullptr, "我不在游戏窗口前"},
		{Localizable("I muted you"), nullptr, "我屏蔽你了"},
		{nullptr, "qm_auto_reply_minimized", nullptr},
		{nullptr, "qm_auto_reply_minimized_message", nullptr},
		{nullptr, "qm_auto_reply_muted", nullptr},
		{nullptr, "qm_auto_reply_muted_message", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("tclient:auto-reply", tclient_auto_reply);
}

namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_tclient_cursor[] = {
		{Localizable("Ingame cursor scale"), nullptr, "游戏内光标缩放"},
		{Localizable("Visual: Cursor"), nullptr, "视觉：光标"},
		{nullptr, "qm_cursor_scale", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("tclient:cursor", tclient_cursor);
}

namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_tee7_editor[] = {
		{Localizable("Are you sure that you want to delete '%s'?"), nullptr, "你确定要删除'%s'吗？"},
		{Localizable("Basic"), nullptr, "基本"},
		{Localizable("Custom"), nullptr, "自定义"},
		{Localizable("Dummy"), nullptr, "分身"},
		{Localizable("No"), nullptr, "否"},
		{Localizable("Player"), nullptr, "玩家"},
		{Localizable("Yes"), nullptr, "是"},
		{Localizable("Your skin"), nullptr, "你的皮肤"},
		{Localizable("Unable to delete skin"), nullptr, "无法删除皮肤"},
		{nullptr, "cl_dummy", nullptr},
		{nullptr, "cl_skin_filter_string", nullptr},
		{nullptr, "qm_cycle_tee_hue", nullptr},
		{nullptr, "qm_cycle_tee_hue_dummy", nullptr},
		{nullptr, "qm_cycle_tee_hue_speed", nullptr},
		{nullptr, "qm_rainbow_tees", nullptr},
		{nullptr, "qm_ui_motion_level", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:tee7-editor", deck_tee7_editor);
}

namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_controls_movement[] = {
		{Localizable("Default zoom"), nullptr, "默认缩放"},
		{Localizable("Fire"), nullptr, "开火"},
		{Localizable("Hook"), nullptr, "钩索"},
		{Localizable("Hook collisions"), nullptr, "钩子辅助线"},
		{Localizable("Jump"), nullptr, "跳跃"},
		{Localizable("Kill"), nullptr, "自杀"},
		{Localizable("Pause"), nullptr, "暂停"},
		{Localizable("Show all"), nullptr, "显示所有玩家"},
		{Localizable("Show others"), nullptr, "显示其他玩家"},
		{Localizable("Toggle dyncam"), nullptr, "动态视野开关"},
		{Localizable("Toggle ghost"), nullptr, "影子记录开关"},
		{Localizable("Zoom in"), nullptr, "放大视野"},
		{Localizable("Zoom out"), nullptr, "缩小视野"},
		{nullptr, nullptr, "+left"},
		{nullptr, nullptr, "+right"},
		{nullptr, nullptr, "+jump"},
		{nullptr, nullptr, "+fire"},
		{nullptr, nullptr, "+hook"},
		{nullptr, nullptr, "+showhookcoll"},
		{nullptr, nullptr, "say /pause"},
		{nullptr, nullptr, "kill"},
		{nullptr, nullptr, "zoom+"},
		{nullptr, nullptr, "zoom-"},
		{nullptr, nullptr, "zoom"},
		{nullptr, nullptr, "say /showothers"},
		{nullptr, nullptr, "say /showall"},
		{nullptr, nullptr, "toggle cl_dyncam 0 1"},
		{nullptr, nullptr, "toggle cl_race_show_ghost 0 1"},
	};
	QM_CARD_SEARCH_REGISTER("deck:controls-movement", deck_controls_movement);
}

namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_controls_weapon[] = {
		{Localizable("Grenade"), nullptr, "榴弹枪"},
		{Localizable("Hammer"), nullptr, "锤子"},
		{Localizable("Laser"), nullptr, "激光"},
		{Localizable("Next weapon"), nullptr, "下一个武器"},
		{Localizable("Pistol"), nullptr, "手枪"},
		{Localizable("Prev. weapon"), nullptr, "上一个武器"},
		{Localizable("Shotgun"), nullptr, "霰弹枪"},
		{Localizable("Weapon trajectory"), nullptr, "武器弹道辅助线"},
		{nullptr, nullptr, "+weapon1"},
		{nullptr, nullptr, "+weapon2"},
		{nullptr, nullptr, "+weapon3"},
		{nullptr, nullptr, "+weapon4"},
		{nullptr, nullptr, "+weapon5"},
		{nullptr, nullptr, "+showweapontrajectory"},
		{nullptr, nullptr, "+nextweapon"},
		{nullptr, nullptr, "+prevweapon"},
	};
	QM_CARD_SEARCH_REGISTER("deck:controls-weapon", deck_controls_weapon);
}

namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_controls_voting[] = {
		{Localizable("Vote no"), nullptr, "反对"},
		{Localizable("Vote yes"), nullptr, "赞成"},
		{nullptr, nullptr, "vote yes"},
		{nullptr, nullptr, "vote no"},
	};
	QM_CARD_SEARCH_REGISTER("deck:controls-voting", deck_controls_voting);
}

namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_controls_chat[] = {
		{Localizable("Chat"), nullptr, "聊天"},
		{Localizable("Chat command"), nullptr, "聊天框指令（/）"},
		{Localizable("Converse"), nullptr, "继续私聊"},
		{Localizable("Repeat message"), nullptr, "复读"},
		{Localizable("Show chat"), nullptr, "显示聊天框"},
		{Localizable("Team chat"), nullptr, "队伍聊天"},
		{Localizable("Voice chat"), nullptr, "语音聊天"},
		{nullptr, nullptr, "+show_chat; chat all"},
		{nullptr, nullptr, "+show_chat; chat team"},
		{nullptr, nullptr, "+show_chat; chat all /c "},
		{nullptr, nullptr, "+show_chat; chat all /"},
		{nullptr, nullptr, "+show_chat"},
		{nullptr, nullptr, "+qm_repeat"},
		{nullptr, nullptr, "+qm_voice_ptt"},
	};
	QM_CARD_SEARCH_REGISTER("deck:controls-chat", deck_controls_chat);
}

namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_controls_dummy[] = {
		{Localizable("Dummy Control"), nullptr, "分身控制"},
		{Localizable("Dummy copy"), nullptr, "分身同步"},
		{Localizable("Dummy fire"), nullptr, "分身开火"},
		{Localizable("Dummy hammer fly"), nullptr, "分身锤飞"},
		{Localizable("Dummy hook"), nullptr, "分身钩"},
		{Localizable("Dummy jump"), nullptr, "分身跳"},
		{Localizable("Toggle dummy"), nullptr, "切换分身"},
		{nullptr, nullptr, "toggle cl_dummy 0 1"},
		{nullptr, nullptr, "+toggle_restore cl_dummy_jump 1"},
		{nullptr, nullptr, "+toggle_restore cl_dummy_fire 1"},
		{nullptr, nullptr, "+toggle_restore cl_dummy_hook 1"},
		{nullptr, nullptr, "toggle cl_dummy_copy_moves 0 1"},
		{nullptr, nullptr, "toggle cl_dummy_hammer 0 1"},
		{nullptr, nullptr, "toggle cl_dummy_control 1 0"},
	};
	QM_CARD_SEARCH_REGISTER("deck:controls-dummy", deck_controls_dummy);
}

namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_controls_miscellaneous[] = {
		{Localizable("Active disconnect"), nullptr, "主动断开"},
		{Localizable("Console"), nullptr, "控制台"},
		{Localizable("Emoticon"), nullptr, "表情"},
		{Localizable("Fast practice"), nullptr, "快速练习"},
		{Localizable("Lock team"), nullptr, "锁定队伍"},
		{Localizable("Pie menu"), nullptr, "饼菜单"},
		{Localizable("Remote console"), nullptr, "远程控制台"},
		{Localizable("Scoreboard"), nullptr, "计分板"},
		{Localizable("Scoreboard cursor"), nullptr, "计分板光标"},
		{Localizable("Screenshot"), nullptr, "截图"},
		{Localizable("Show HUD"), nullptr, "HUD 开关"},
		{Localizable("Show entities"), nullptr, "实体层显示开关"},
		{Localizable("Spectate mode"), nullptr, "观战模式"},
		{Localizable("Spectate next"), nullptr, "旁观下一个"},
		{Localizable("Spectate previous"), nullptr, "旁观上一个"},
		{Localizable("Spectate teleport"), nullptr, "观战传送"},
		{Localizable("Statboard"), nullptr, "统计板"},
		{nullptr, "qm_spec_teleport", nullptr},
		{nullptr, "qm_timeout_disconnect", nullptr},
		{nullptr, nullptr, "+emote"},
		{nullptr, nullptr, "+spectate"},
		{nullptr, nullptr, "qm_spec_teleport"},
		{nullptr, nullptr, "spectate_next"},
		{nullptr, nullptr, "spectate_previous"},
		{nullptr, nullptr, "qm_timeout_disconnect"},
		{nullptr, nullptr, "toggle_local_console"},
		{nullptr, nullptr, "toggle_remote_console"},
		{nullptr, nullptr, "screenshot"},
		{nullptr, nullptr, "+scoreboard"},
		{nullptr, nullptr, "toggle_scoreboard_cursor"},
		{nullptr, nullptr, "+statboard"},
		{nullptr, nullptr, "+pie_menu"},
		{nullptr, nullptr, "fast_practice_toggle"},
		{nullptr, nullptr, "say /lock"},
		{nullptr, nullptr, "toggle cl_overlay_entities 0 100"},
		{nullptr, nullptr, "toggle cl_showhud 0 1"},
	};
	QM_CARD_SEARCH_REGISTER("deck:controls-miscellaneous", deck_controls_miscellaneous);
}

namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_self_kill_protection[] = {
		{Localizable("Client-side self-kill protection"), nullptr, "客户端侧自杀保护 自杀保护 自杀开关"},
		{Localizable("Enable self-kill protection"), nullptr, "自杀保护开关 拦截自杀 防止误触 K 键"},
		{Localizable("Self-kill off"), nullptr, "关闭自杀 禁止自杀 防误触"},
		{Localizable("Self-kill on"), nullptr, "开启自杀 允许自杀 解除"},
		{Localizable("Auto-enable after"), nullptr, "自动开启 开局后 自动拦截 延迟 分钟"},
		{Localizable("To prevent accidental kills while racing, toggle the switch above or use the two bind rows. To self-kill, enable first, then press K. Auto-enable counts from the moment you join a game; 0 minutes keeps it off. Server commands are not affected"), nullptr, "跑图 防误触 绑定按键 bind 按键开关"},
		{nullptr, "qm_self_kill_auto_delay", nullptr},
		{nullptr, "qm_self_kill_enabled", nullptr},
		{nullptr, "qm_self_kill_off", nullptr},
		{nullptr, "qm_self_kill_on", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:self_kill_protection", qm_self_kill_protection);
}

namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_bind_editor[] = {
		{Localizable("Active disconnect"), nullptr, "主动断开"},
		{Localizable("Bind"), nullptr, "绑定"},
		{Localizable("Chat"), nullptr, "聊天"},
		{Localizable("Command"), nullptr, "命令"},
		{Localizable("Console"), nullptr, "控制台"},
		{Localizable("Converse"), nullptr, "继续私聊"},
		{Localizable("Custom command"), nullptr, "自定义命令"},
		{Localizable("Default zoom"), nullptr, "默认缩放"},
		{Localizable("Dummy control"), nullptr, "分身控制"},
		{Localizable("Dummy copy"), nullptr, "分身同步"},
		{Localizable("Dummy fire"), nullptr, "分身开火"},
		{Localizable("Dummy hook"), nullptr, "分身钩"},
		{Localizable("Dummy jump"), nullptr, "分身跳"},
		{Localizable("Emoticon"), nullptr, "表情"},
		{Localizable("Fire"), nullptr, "开火"},
		{Localizable("Grenade"), nullptr, "榴弹枪"},
		{Localizable("Hammer"), nullptr, "锤子"},
		{Localizable("Hook"), nullptr, "钩索"},
		{Localizable("Hook collisions"), nullptr, "钩子辅助线"},
		{Localizable("Invalid command or parameters."), nullptr, "命令或参数无效。"},
		{Localizable("Jump"), nullptr, "跳跃"},
		{Localizable("Kill"), nullptr, "自杀"},
		{Localizable("Laser"), nullptr, "激光"},
		{Localizable("Next weapon"), nullptr, "下一个武器"},
		{Localizable("No results"), nullptr, "无结果"},
		{Localizable("Optional"), nullptr, "可选"},
		{Localizable("Pause"), nullptr, "暂停"},
		{Localizable("Pie menu"), nullptr, "饼菜单"},
		{Localizable("Pistol"), nullptr, "手枪"},
		{Localizable("Prev. weapon"), nullptr, "上一个武器"},
		{Localizable("Repeat message"), nullptr, "复读"},
		{Localizable("Scoreboard"), nullptr, "计分板"},
		{Localizable("Screenshot"), nullptr, "截图"},
		{Localizable("Search"), nullptr, "搜索"},
		{Localizable("Shotgun"), nullptr, "霰弹枪"},
		{Localizable("Show HUD"), nullptr, "HUD 开关"},
		{Localizable("Show all"), nullptr, "显示所有玩家"},
		{Localizable("Show chat"), nullptr, "显示聊天框"},
		{Localizable("Show entities"), nullptr, "实体层显示开关"},
		{Localizable("Show others"), nullptr, "显示其他玩家"},
		{Localizable("Spectate mode"), nullptr, "观战模式"},
		{Localizable("Spectate next"), nullptr, "旁观下一个"},
		{Localizable("Spectate previous"), nullptr, "旁观上一个"},
		{Localizable("Statboard"), nullptr, "统计板"},
		{Localizable("Team chat"), nullptr, "队伍聊天"},
		{Localizable("The command is empty, incomplete, or exceeds the local config line limit."), nullptr, "命令为空、不完整或超过本地配置行限制。"},
		{Localizable("Toggle dummy"), nullptr, "切换分身"},
		{Localizable("Toggle dyncam"), nullptr, "动态视野开关"},
		{Localizable("Toggle ghost"), nullptr, "影子记录开关"},
		{Localizable("Voice chat"), nullptr, "语音聊天"},
		{Localizable("Vote no"), nullptr, "反对"},
		{Localizable("Vote yes"), nullptr, "赞成"},
		{Localizable("Zoom in"), nullptr, "放大视野"},
		{Localizable("Zoom out"), nullptr, "缩小视野"},
		{nullptr, "qm_bind_command_editor", nullptr},
		{nullptr, "qm_bind_command_picker", nullptr},
		{nullptr, "qm_timeout_disconnect", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:bind_editor", qm_bind_editor);
}

namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_bind_overview[] = {
		{Localizable("Bind overview"), nullptr, "绑定一览"},
		{Localizable("Browse all key bindings grouped by function"), nullptr, "按功能分组查看所有键位绑定"},
		{Localizable("Movement"), nullptr, "移动"},
		{Localizable("Weapon"), nullptr, "武器"},
		{Localizable("Miscellaneous"), nullptr, "其他"},

	};
	QM_CARD_SEARCH_REGISTER("qm:bind_overview", qm_bind_overview);
}

namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_qmclient_contributors_title[] = {
		{Localizable("Chat message preview"), nullptr, "发言预览"},
		{Localizable("Chat message style"), nullptr, "发言样式"},
		{Localizable("Custom title"), nullptr, "自定义头衔"},
		{Localizable("Exact bound nickname"), nullptr, "要绑定的完整昵称"},
		{Localizable("Limit the title to the exact nickname below."), nullptr, "仅在使用下方昵称时显示头衔。"},
		{Localizable("Normal"), nullptr, "正常"},
		{Localizable("Only show with this nickname"), nullptr, "仅在使用此昵称时显示"},
		{Localizable("Permanent access; up to 4 online IP addresses"), nullptr, "永久资格，最多 4 个 IP 同时在线"},
		{Localizable("Platinum highlight"), nullptr, "铂金高光"},
		{Localizable("Redeem your sponsor code to unlock a custom title, shown in chat and on your nameplate."), nullptr, "使用赞助兑换码解锁自定义称号，称号会显示在聊天与名牌上。"},
		{Localizable("Saved with your title and shared with other QmClient players."), nullptr, "随头衔一起保存，并同步给其他 QmClient 玩家。"},
		{Localizable("See you at the next checkpoint."), nullptr, "下个检查点见。"},
		{Localizable("Sponsor code"), nullptr, "认证码"},
		{Localizable("Sponsor title"), nullptr, "赞助头衔"},
		{Localizable("Up to 6 Chinese or 12 ASCII characters"), nullptr, "最多 6 个中文或 12 个英文字符"},
		{nullptr, "qm_sponsor_chat_style", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:qmclient-contributors-title", deck_qmclient_contributors_title);
}

namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_qmclient_contributors_title_display[] = {
		{Localizable("Moon"), nullptr, "月亮"},
		{Localizable("Nameplate title size"), nullptr, "名牌头衔大小"},
		{Localizable("Show dummy title locally"), nullptr, "本机显示分身头衔"},
		{Localizable("Show main tee title locally"), nullptr, "本机显示本体头衔"},
		{Localizable("Show sponsor chat effects locally"), nullptr, "本机显示赞助者发言特效"},
		{Localizable("Show titles above player names"), nullptr, "头衔显示在名字上方"},
		{Localizable("Show titles on nameplates"), nullptr, "显示名牌头衔"},
		{Localizable("Sun"), nullptr, "太阳"},
		{Localizable("Title color"), nullptr, "头衔颜色"},
		{Localizable("Title opacity"), nullptr, "头衔不透明度"},
		{Localizable("Title preview"), nullptr, "头衔成品预览"},
		{Localizable("Title style"), nullptr, "头衔风格"},
		{Localizable("Turning this off only changes your display."), nullptr, "关闭后仅影响本机显示。"},
		{nullptr, "qm_nameplate_title_above_name", nullptr},
		{nullptr, "qm_nameplate_title_scale", nullptr},
		{nullptr, "qm_show_dummy_title", nullptr},
		{nullptr, "qm_show_main_title", nullptr},
		{nullptr, "qm_show_nameplate_title", nullptr},
		{nullptr, "qm_sponsor_chat_effects", nullptr},
		{nullptr, "qm_title_badge_color", nullptr},
		{nullptr, "qm_title_badge_style", nullptr},
		{nullptr, "qm_title_opacity", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:qmclient-contributors-title-display", deck_qmclient_contributors_title_display);
}

namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_tclient_chat_binds_kaomoji[] = {
		{Localizable("Cute:"), nullptr, "可爱："},
		{Localizable("Flip:"), nullptr, "翻转："},
		{Localizable("Kaomoji"), nullptr, "表情符号"},
		{Localizable("Lenny:"), nullptr, "Lenny ͡° ͜ʖ ͡°:"},
		{Localizable("Shrug:"), nullptr, "耸肩："},
		{Localizable("Unflip:"), nullptr, "取消翻转："},
	};
	QM_CARD_SEARCH_REGISTER("deck:tclient-chat-binds-kaomoji", deck_tclient_chat_binds_kaomoji);
}

namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_tclient_chat_binds_warlist[] = {
		{Localizable("Add [group] [clan] [reason]:"), nullptr, "添加 [组名] [战队名] [原因]"},
		{Localizable("Add [group] [name] [reason]:"), nullptr, "添加 [组名] [玩家名] [原因]"},
		{Localizable("Add team clan:"), nullptr, "添加友好战队："},
		{Localizable("Add team name:"), nullptr, "添加友好玩家："},
		{Localizable("Add war clan:"), nullptr, "添加敌对战队："},
		{Localizable("Add war name:"), nullptr, "添加敌对玩家："},
		{Localizable("Remove [group] [clan]:"), nullptr, "删除 [组名] [战队名]："},
		{Localizable("Remove [group] [name]:"), nullptr, "删除 [组名] [玩家名]："},
		{Localizable("Remove team clan:"), nullptr, "删除友好战队："},
		{Localizable("Remove team name:"), nullptr, "删除友好玩家："},
		{Localizable("Remove war clan:"), nullptr, "删除敌对战队"},
		{Localizable("Remove war name:"), nullptr, "删除敌对玩家："},
		{Localizable("Warlist"), nullptr, "敌对列表"},
	};
	QM_CARD_SEARCH_REGISTER("deck:tclient-chat-binds-warlist", deck_tclient_chat_binds_warlist);
}

namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_tclient_chat_binds_other[] = {
		{Localizable("Mute:"), nullptr, "禁言："},
		{Localizable("Other"), nullptr, "其他"},
		{Localizable("Translate ID:"), nullptr, "翻译 ID："},
		{Localizable("Translate:"), nullptr, "翻译："},
		{Localizable("Unmute:"), nullptr, "取消禁言："},
	};
	QM_CARD_SEARCH_REGISTER("deck:tclient-chat-binds-other", deck_tclient_chat_binds_other);
}

namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_controls_custom[] = {
		{Localizable("Command"), nullptr, "命令"},
		{Localizable("Custom"), nullptr, "自定义"},
	};
	QM_CARD_SEARCH_REGISTER("deck:controls-custom", deck_controls_custom);
}
