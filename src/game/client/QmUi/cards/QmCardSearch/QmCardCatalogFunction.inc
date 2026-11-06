// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_better_scoreboard[] = {
		{Localizable("Better scoreboard"), nullptr, "更好的计分板"},
		{Localizable("Scroll mode"), nullptr, "滚动模式 满人服务器改用固定行高计分板，滚轮翻看其余玩家"},
		{Localizable("Leave empty to show everyone"), nullptr, "留空显示所有玩家"},
		{Localizable("Scoreboard filter: only show players whose name or clan contains this text"), nullptr, "计分板筛选：匹配名字或战队"},
		{Localizable("Scoreboard point check"), nullptr, "计分板积分检查"},
		{Localizable("Scoreboard playtime"), nullptr, "计分板游玩时间 累计小时 DDStats"},
		{Localizable("Show scoreboard after death"), nullptr, "死亡后显示计分板"},
		{Localizable("Use the scoreboard cursor mode to scroll the list"), nullptr, "使用计分板光标模式滚动查看列表"},
		{Localizable("Show total playtime from DDStats instead of points in scoreboard"), nullptr, "计分板用总游戏时长替代分数"},
		{nullptr, "qm_better_scoreboard", nullptr},
		{nullptr, "qm_scoreboard_filter", nullptr},
		{nullptr, "qm_scoreboard_on_death", nullptr},
		{nullptr, "qm_scoreboard_points", nullptr},
		{nullptr, "qm_scoreboard_playtime", nullptr},
		{nullptr, "qm_scoreboard_scroll", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:better_scoreboard", qm_better_scoreboard);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_block_words[] = {
		{Localizable("Behavior"), nullptr, "行为"},
		{Localizable("Both"), nullptr, "两者"},
		{Localizable("Console color"), nullptr, "控制台颜色"},
		{Localizable("Enable word filter list"), nullptr, "启用屏蔽词列表"},
		{Localizable("Hide player messages"), nullptr, "隐藏玩家消息"},
		{Localizable("Literal"), nullptr, "字面"},
		{Localizable("Mode"), nullptr, "模式"},
		{Localizable("Regular expression"), nullptr, "正则表达式"},
		{Localizable("Replace mode"), nullptr, "替换模式"},
		{Localizable("Replacement chars"), nullptr, "替换字符"},
		{Localizable("Separate with commas"), nullptr, "用 , 分隔"},
		{Localizable("Show blocked words in console"), nullptr, "在控制台中显示被屏蔽的词语"},
		{Localizable("Use multi-char replacement based on word length"), nullptr, "根据词语长度使用多字符替换"},
		{Localizable("Word Filter"), nullptr, "屏蔽词"},
		{Localizable("Use public word filter"), nullptr, "公共屏蔽词 公共词库"},
		{Localizable("Public words: %d"), nullptr, "公共词数量：%d"},
		{Localizable("Updating public word list..."), nullptr, "正在更新公共词库..."},
		{Localizable("Public words match literal text in incoming player chat."), nullptr, "公共词按字面匹配收到的玩家聊天。"},
		{Localizable("Word list file"), nullptr, "词表文件"},
		{Localizable("%d words loaded"), nullptr, "已加载 %d 个词"},
		{Localizable("UTF-8 text, one word per line (max 2 MiB)."), nullptr, "UTF-8 文本，每行一个词（最大 2 MiB）。"},
		{nullptr, "qm_public_block_words", nullptr},
		{nullptr, "qm_block_words_action", nullptr},
		{nullptr, "qm_block_words_console_color", nullptr},
		{nullptr, "qm_block_words_enabled", nullptr},
		{nullptr, "qm_block_words_list", nullptr},
		{nullptr, "qm_block_words_file", "文件词表 读取文件 外部词库"},
		{nullptr, "qm_block_words_mode", nullptr},
		{nullptr, "qm_block_words_multi_replace", nullptr},
		{nullptr, "qm_block_words_replacement_char", nullptr},
		{nullptr, "qm_block_words_show_console", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:block_words", qm_block_words);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_emoticons[] = {
		{Localizable("Launch emote key"), nullptr, "发射表情按键"},
		{Localizable("Show other players' large emoticons"), nullptr, "显示他人的超大表情"},
		{Localizable("Show other players' launched emoticons"), nullptr, "显示他人的发射表情"},
		{nullptr, "qm_show_other_launch_emotes", nullptr},
		{nullptr, "qm_show_other_super_emotes", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:emoticons", qm_emoticons);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_favorite_maps[] = {
		{Localizable("Brutal"), nullptr, "高阶"},
		// DDmaX 系列走自己的键，中文按官方习惯叫「古典」，不再借用 Classic* 键。
		{Localizable("DDmaX"), nullptr, "古典"},
		{Localizable("DDmaX Easy"), nullptr, "古典 easy"},
		{Localizable("DDmaX Next"), nullptr, "古典 next"},
		{Localizable("DDmaX Nut"), nullptr, "古典 nut"},
		{Localizable("DDmaX Pro"), nullptr, "古典 pro"},
		{Localizable("Copied"), nullptr, "已复制"},
		{Localizable("Dummy"), nullptr, "分身"},
		{Localizable("Event"), nullptr, "活动"},
		{Localizable("Fun"), nullptr, "娱乐"},
		{Localizable("Insane"), nullptr, "疯狂"},
		{Localizable("Moderate"), nullptr, "中阶"},
		{Localizable("No favorite maps yet"), nullptr, "暂无收藏地图"},
		{Localizable("Novice"), nullptr, "简单"},
		{Localizable("Oldschool"), nullptr, "传统"},
		{Localizable("Race"), nullptr, "竞速"},
		{Localizable("Solo"), nullptr, "单人"},
		{Localizable("Unknown"), nullptr, "未知"},
	};
	QM_CARD_SEARCH_REGISTER("qm:favorite_maps", qm_favorite_maps);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_friend_notify[] = {
		{Localizable("Auto greet friends entering map"), nullptr, "自动问候进图的好友"},
		{Localizable("Auto refresh server list"), nullptr, "自动刷新服务器列表"},
		{Localizable("Auto-follow delay"), nullptr, "自动跟随延迟"},
		{Localizable("Greeting text"), nullptr, "打招呼文本"},
		{Localizable("Large text announcement for friend joining"), nullptr, "大字播报进服好友"},
		{Localizable("Large text content"), nullptr, "大字文案"},
		{Localizable("Leave empty to disable"), nullptr, "留空以禁用"},
		{Localizable("Notify when friends come online"), nullptr, "好友上线提醒"},
		{Localizable("Please use %s as friend name"), nullptr, "请使用 %s 作为好友名称"},
		{Localizable("Refresh interval"), nullptr, "刷新间隔"},
		{Localizable("Show friends' last seen dates (DDStats)"), nullptr, "好友最近在线日期 DDStats 最后上号"},
		{nullptr, "qm_friend_last_seen", nullptr},
		{nullptr, "qm_friend_auto_follow_delay", nullptr},
		{nullptr, "qm_friend_enter_auto_greet", nullptr},
		{nullptr, "qm_friend_enter_broadcast", nullptr},
		{nullptr, "qm_friend_enter_broadcast_text", nullptr},
		{nullptr, "qm_friend_enter_greet_text", nullptr},
		{nullptr, "qm_friend_online_auto_refresh", nullptr},
		{nullptr, "qm_friend_online_notify", nullptr},
		{nullptr, "qm_friend_online_refresh_seconds", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:friend_notify", qm_friend_notify);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_gores[] = {
		{Localizable("Moving block and freeze prediction (experimental)"), nullptr, "移动方块预测 移动水预测 延迟补偿"},
		{Localizable("Show moving tiles in entities"), nullptr, "在实体层显示移动方块 移动水"},
		{nullptr, "qm_axiom_quad_prediction", nullptr},
		{nullptr, "qm_moving_tiles_entities", nullptr},
		{Localizable("Auto enable in Gores mode"), nullptr, "Gores 模式自动启用"},
		{Localizable("Auto login Axiom server"), nullptr, "自动登录 Axiom 服务器账号"},
		{Localizable("Auto weapon switch"), nullptr, "自动切锤"},
		{Localizable("Auto-toggle fast input"), nullptr, "自动开关快速输入"},
		{Localizable("Auto-toggle fast input others"), nullptr, "自动开关快速输入其他玩家"},
		{Localizable("Axiom dummy password"), nullptr, "Axiom 分身密码"},
		{Localizable("Axiom main account password"), nullptr, "Axiom 主号密码"},
		{Localizable("Axiom website"), nullptr, "Axiom 官网"},
		{Localizable("Disable after picking up other weapons"), nullptr, "拿到其他武器时停用"},
		{Localizable("Enable Gores mode"), nullptr, "启用 Gores 模式"},
		{Localizable("Gores mode key"), nullptr, "Gores 模式按键"},
		{Localizable("Hide guide lines"), nullptr, "隐藏辅助线"},
		{Localizable("Skip switch animation when hammering"), nullptr, "锤击时不播放切换武器动画"},
		{Localizable("Temporarily disable dummy hammering"), nullptr, "临时禁用分身锤"},
		{Localizable("Your account is your in-game name — register it on the Axiom website. Fill in the registered main and dummy passwords. This option does not register accounts."), nullptr, "账号即游戏内名字，请先在 Axiom 官网注册。分别填入主号与分身的注册密码；不会自动注册账号。"},
		{nullptr, "qm_axiom_auto_login", nullptr},
		{nullptr, "qm_axiom_dummy_login_password", nullptr},
		{nullptr, "qm_axiom_login_password", nullptr},
		{nullptr, "qm_gores", nullptr},
		{nullptr, "qm_gores_auto_enable", nullptr},
		{nullptr, "qm_gores_auto_weapon_switch", nullptr},
		{nullptr, "qm_gores_disable_dummy_hammer", nullptr},
		{nullptr, "qm_gores_disable_if_weapons", nullptr},
		{nullptr, "qm_gores_fast_input", nullptr},
		{nullptr, "qm_gores_fast_input_others", nullptr},
		{nullptr, "qm_gores_hide_guides", nullptr},
		{nullptr, "qm_gores_suppress_switch_anim", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:gores", qm_gores);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_gores_actor[] = {
		{Localizable("Auto chat in water"), nullptr, "水中自动发言"},
		{Localizable("Chat message"), nullptr, "聊天消息"},
		{Localizable("Emoticon ID"), nullptr, "表情 ID"},
		{Localizable("Leave empty to disable"), nullptr, "留空以禁用"},
		{Localizable("Send emoticon in water"), nullptr, "水中发送表情"},
		{Localizable("Send probability"), nullptr, "发送概率"},
		{nullptr, "qm_freeze_chat_chance", nullptr},
		{nullptr, "qm_freeze_chat_emoticon", nullptr},
		{nullptr, "qm_freeze_chat_emoticon_id", nullptr},
		{nullptr, "qm_freeze_chat_enabled", nullptr},
		{nullptr, "qm_freeze_chat_message", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:gores_actor", qm_gores_actor);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_hj_assist[] = {
		{Localizable("Auto switch to the tee that got unfrozen"), nullptr, "自动切换到解冻的 Tee"},
		{Localizable("Auto team lock"), nullptr, "自动锁队"},
		{Localizable("Auto unspec on unfreeze"), nullptr, "解冻自动取消旁观"},
		{Localizable("Auto unspec both tees when either is hammered awake"), nullptr, "任一方被锤醒时两个 Tee 都取消旁观"},
		{Localizable("Automatically close the current chat after waking from freeze"), nullptr, "从冻结状态中醒来自动关闭聊天框"},
		{Localizable("Fade spectators in water"), nullptr, "虚化水中的旁观者"},
		{Localizable("Lock delay"), nullptr, "锁队延迟"},
		{Localizable("Show wake-up popup on the other tee"), nullptr, "在另一侧 Tee 显示唤醒提示"},
		{Localizable("Spectator opacity"), nullptr, "旁观者透明度"},
		{nullptr, "qm_auto_close_chat_on_unfreeze", nullptr},
		{nullptr, "qm_auto_switch_on_unfreeze", nullptr},
		{nullptr, "qm_auto_team_lock", nullptr},
		{nullptr, "qm_auto_team_lock_delay", nullptr},
		{nullptr, "qm_auto_unspec_on_unfreeze", nullptr},
		{nullptr, "qm_auto_unspec_both_on_unfreeze", nullptr},
		{nullptr, "qm_freeze_wakeup_popup", nullptr},
		{nullptr, "qm_paused_spectator_alpha", nullptr},
		{nullptr, "qm_paused_spectator_fade", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:hj_assist", qm_hj_assist);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_ime[] = {
		{Localizable("Auto manage IME while typing"), nullptr, "输入时自动管理 IME"},
		{Localizable("IME background color"), nullptr, "IME 背景颜色"},
		{Localizable("IME font size"), nullptr, "输入法字号"},
		{Localizable("IME selected text color"), nullptr, "输入法选中文字颜色"},
		{Localizable("IME selection background color"), nullptr, "输入法选中背景颜色"},
		{Localizable("IME text color"), nullptr, "输入法文字颜色"},
		{Localizable("New IME"), nullptr, "新版 IME"},
		{Localizable("IME appearance"), nullptr, "输入法外观"},
		{nullptr, "qm_ime_auto_manage", nullptr},
		{nullptr, "qm_ime_bg_color", nullptr},
		{nullptr, "qm_ime_font_size", nullptr},
		{nullptr, "qm_ime_opacity", nullptr},
		{nullptr, "qm_ime_selected_color", nullptr},
		{nullptr, "qm_ime_selected_text_color", nullptr},
		{nullptr, "qm_ime_text_color", nullptr},
		{nullptr, "qm_new_ime", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:ime", qm_ime);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_jump_hint[] = {
		{Localizable("Font size"), nullptr, "字体大小"},
		{Localizable("Horizontal position"), nullptr, "水平位置"},
		{Localizable("Position jump hint"), nullptr, "位置跳跃提示"},
		{Localizable("Text color"), nullptr, "文本颜色"},
		{Localizable("Vertical position"), nullptr, "垂直位置"},
		{nullptr, "qm_jump_hint", nullptr},
		{nullptr, "qm_jump_hint_color", nullptr},
		{nullptr, "qm_jump_hint_size", nullptr},
		{nullptr, "qm_jump_hint_x", nullptr},
		{nullptr, "qm_jump_hint_y", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:jump_hint", qm_jump_hint);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_key_binds[] = {
		{Localizable("45° Aim"), nullptr, "45°瞄准"},
		{Localizable("Active disconnect"), nullptr, "主动断开"},
		{Localizable("DF"), nullptr, "DF"},
		{Localizable("Gap aim rescue"), nullptr, "瞄缝救人"},
		{Localizable("HDF"), nullptr, "HDF"},
		{Localizable("Left jump"), nullptr, "左跳"},
		{Localizable("Right jump"), nullptr, "右跳"},
		{Localizable("Weapon Trajectory"), nullptr, "武器辅助线"},
		{nullptr, "qm_timeout_disconnect", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:key_binds", qm_key_binds);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_map_upload[] = {
		{Localizable("A player name is required."), nullptr, "玩家名不能为空。"},
		{Localizable("Could not read the map file."), nullptr, "无法读取地图文件。"},
		{Localizable("File: %s"), nullptr, "文件：%s"},
		{Localizable("Invalid map file or filename."), nullptr, "地图文件或文件名无效。"},
		{Localizable("Map exceeds the client limit of 64 MiB."), nullptr, "地图超过客户端的 64 MiB 上限。"},
		{Localizable("Map upload cancelled."), nullptr, "已取消地图上传。"},
		{Localizable("Map upload endpoint must use HTTP or HTTPS"), nullptr, "地图上传端点必须使用 HTTP 或 HTTPS"},
		{Localizable("Map upload in progress..."), nullptr, "正在上传地图……"},
		{Localizable("Map upload network error."), nullptr, "地图上传时发生网络错误。"},
		{Localizable("Map uploaded successfully."), nullptr, "地图上传成功。"},
		{Localizable("No map selected"), nullptr, "尚未选择地图"},
		{Localizable("None"), nullptr, "无"},
		{Localizable("Player: %s"), nullptr, "玩家：%s"},
		{Localizable("Select a saved .map file (client limit: 64 MiB)."), nullptr, "选择已保存的 .map 文件（客户端上限：64 MiB）。"},
		{Localizable("Target server: %s"), nullptr, "目标服务器：%s"},
		{Localizable("The server rejected the map upload."), nullptr, "服务器拒绝了地图上传。"},
		{Localizable("Unexpected map upload response."), nullptr, "地图上传接口返回了异常响应。"},
		{Localizable("Map folders"), nullptr, "地图目录"},
		{Localizable("Searching all map folders..."), nullptr, "正在搜索所有地图目录..."},
		{Localizable("Search maps and downloadedmaps, including subfolders"), nullptr, "搜索 maps 与 downloadedmaps（含子目录）"},
		{Localizable("No saved maps found"), nullptr, "未找到已保存的地图"},
		{Localizable("After uploading again, use: reload or hot_reload"), nullptr, "再次上传后使用 reload 或 hot_reload"},
		{Localizable("Press F2, log in, then use: change_map map_name"), nullptr, "按 F2 登录后使用 change_map map_name"},
		{Localizable("Upload .map files. Server and admin passwords: s."), nullptr, "上传 .map 文件；服务器与管理密码：s。"},
		{nullptr, "qm_map_upload_endpoint", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:map_upload", qm_map_upload);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_mini_features[] = {
		{Localizable("Automatic updates"), nullptr, "自动更新"},
		{Localizable("Automatically fall back to the default asset when a custom asset sprite is fully transparent; turn off to keep blank sprites invisible (e.g. to hide effects)"), nullptr, "自定义资源的贴图全透明时自动回退到默认资源；关闭后空白贴图保持不可见（可用于屏蔽特效）"},
		{Localizable("Blank asset auto fallback"), nullptr, "空白资源自动回退"},
		{Localizable("Color map tune zones by their tune zone number"), nullptr, "按 Tune Zone 编号给地图上的对应区块着色"},
		{Localizable("Combo"), nullptr, "连击"},
		{Localizable("Enable repeat"), nullptr, "启用复读"},
		{Localizable("Hide input emoticon"), nullptr, "隐藏输入表情"},
		{Localizable("Hide server information on join"), nullptr, "进图时隐藏服务器信息"},
		{Localizable("Message merging"), nullptr, "消息合并"},
		{Localizable("Random emoticon"), nullptr, "随机表情"},
		{Localizable("Short server names"), nullptr, "短服务器名称"},
		{Localizable("Show Qm badge"), nullptr, "显示 Qm 标识"},
		{Localizable("Show outdated version warning"), nullptr, "显示版本过旧提示"},
		{Localizable("Show tune zone colors"), nullptr, "显示 Tune Zone 颜色"},
		{Localizable("Sponsor reminder"), nullptr, "赞助提醒"},
		{Localizable("Show command completion in chat"), nullptr, "聊天中显示命令补全"},
		{Localizable("Chat emoji completion"), nullptr, "聊天表情补全"},
		{Localizable("Humming"), nullptr, "哼歌"},
		{Localizable("Send three consecutive lyrics to public chat at random intervals of 90 to 270 seconds, with 3 seconds between lines"), nullptr, "随机间隔向公共聊天发送三句歌词，行间间隔 3 秒"},
		{nullptr, "qm_auto_update", nullptr},
		{nullptr, "qm_blank_asset_fallback", nullptr},
		{nullptr, "qm_client_show_badge", nullptr},
		{nullptr, "qm_combo_popup", nullptr},
		{nullptr, "qm_hide_join_server_info", nullptr},
		{nullptr, "qm_message_merge", nullptr},
		{nullptr, "qm_random_emote_on_hit", nullptr},
		{nullptr, "qm_repeat_enabled", nullptr},
		{nullptr, "qm_say_nopop", nullptr},
		{nullptr, "qm_short_server_names", nullptr},
		{nullptr, "qm_show_outdated_version_warning", nullptr},
		{nullptr, "qm_show_tune_zone_colors", nullptr},
		{nullptr, "qm_sponsor_nudge", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:mini_features", qm_mini_features);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_pie_menu[] = {
		{Localizable("Click to set color"), nullptr, "点击设置颜色"},
		{Localizable("Detection distance"), nullptr, "检测距离"},
		{Localizable("Enable pie menu"), nullptr, "启用饼菜单"},
		{Localizable("Example: A|B|C,D|E (C: main, D: dummy)"), nullptr, "示例：A|B|C,D|E（C 为本体，D 为分身）"},
		{Localizable("Follow server"), nullptr, "跨服跟随"},
		{Localizable("Following: %s"), nullptr, "正在跟随：%s"},
		{Localizable("Friend"), nullptr, "好友"},
		{Localizable("Invite to team"), nullptr, "邀请组队"},
		{Localizable("Join team"), nullptr, "加入队伍"},
		{Localizable("Mention"), nullptr, "提及"},
		{Localizable("Opacity"), nullptr, "不透明度"},
		{Localizable("Option color"), nullptr, "选项颜色"},
		{Localizable("Pie menu effects"), nullptr, "饼菜单开合效果"},
		{Localizable("Pie menu highlight tint"), nullptr, "饼菜单选中色调"},
		{Localizable("Rename queue"), nullptr, "改名名单"},
		{Localizable("Spectate"), nullptr, "观战"},
		{Localizable("UI scale"), nullptr, "UI 大小"},
		{Localizable("View points"), nullptr, "查看积分"},
		{Localizable("Whisper"), nullptr, "私聊"},
		{nullptr, "qm_perf_debug", nullptr},
		{nullptr, "qm_pie_follow_name", nullptr},
		{nullptr, "qm_pie_menu_color_copy_name", nullptr},
		{nullptr, "qm_pie_menu_color_copy_skin", nullptr},
		{nullptr, "qm_pie_menu_color_follow", nullptr},
		{nullptr, "qm_pie_menu_color_friend", nullptr},
		{nullptr, "qm_pie_menu_color_invite_team", nullptr},
		{nullptr, "qm_pie_menu_color_join_team", nullptr},
		{nullptr, "qm_pie_menu_color_mention", nullptr},
		{nullptr, "qm_pie_menu_color_score", nullptr},
		{nullptr, "qm_pie_menu_color_spectate", nullptr},
		{nullptr, "qm_pie_menu_color_swap", nullptr},
		{nullptr, "qm_pie_menu_color_whisper", nullptr},
		{nullptr, "qm_pie_menu_copy_name_enabled", nullptr},
		{nullptr, "qm_pie_menu_copy_skin_enabled", nullptr},
		{nullptr, "qm_pie_menu_effects", nullptr},
		{nullptr, "qm_pie_menu_enabled", nullptr},
		{nullptr, "qm_pie_menu_follow_enabled", nullptr},
		{nullptr, "qm_pie_menu_friend_enabled", nullptr},
		{nullptr, "qm_pie_menu_invite_team_enabled", nullptr},
		{nullptr, "qm_pie_menu_join_team_enabled", nullptr},
		{nullptr, "qm_pie_menu_max_distance", nullptr},
		{nullptr, "qm_pie_menu_mention_enabled", nullptr},
		{nullptr, "qm_pie_menu_opacity", nullptr},
		{nullptr, "qm_pie_menu_rename_queue", nullptr},
		{nullptr, "qm_pie_menu_scale", nullptr},
		{nullptr, "qm_pie_menu_score_enabled", nullptr},
		{nullptr, "qm_pie_menu_selected_color", nullptr},
		{nullptr, "qm_pie_menu_spectate_enabled", nullptr},
		{nullptr, "qm_pie_menu_swap_enabled", nullptr},
		{nullptr, "qm_pie_menu_whisper_enabled", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:pie_menu", qm_pie_menu);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_qiafen[] = {
		{Localizable("Auto reply cooldown"), nullptr, "自动回复冷却"},
		{Localizable("Both sides of keyword rules must be filled"), nullptr, "关键词规则的两侧都必须填写"},
		{Localizable("Enable keyword reply"), nullptr, "启用关键词回复"},
		{Localizable("Keyword rules"), nullptr, "关键词规则"},
		{Localizable("Regex"), nullptr, "正则"},
		{Localizable("Rename"), nullptr, "重命名"},
		{Localizable("Reply content"), nullptr, "回复内容"},
		{Localizable("Reply with dummy"), nullptr, "使用分身回复"},
		{Localizable("Rule list"), nullptr, "规则列表"},
		{Localizable("Trigger limits"), nullptr, "触发限制"},
		{nullptr, "qm_auto_reply_cooldown", nullptr},
		{nullptr, "qm_keyword_reply_enabled", nullptr},
		{nullptr, "qm_keyword_reply_rules", nullptr},
		{nullptr, "qm_keyword_reply_use_dummy", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:qiafen", qm_qiafen);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_solo_split[] = {
		{Localizable("Auto team lock"), nullptr, "自动锁队"},
		{Localizable("Auto-connect dummy"), nullptr, "自动连接分身"},
		{Localizable("Solo split enter key"), nullptr, "单刷模式进队按键"},
		{Localizable("Solo split key"), nullptr, "单刷模式按键"},
		{Localizable("Solo split leave key"), nullptr, "单刷模式出队按键"},
		{Localizable("Team when leaving solo split"), nullptr, "退出单刷时的队伍"},
		{nullptr, "qm_auto_team_lock", nullptr},
		{nullptr, "qm_solo_split", nullptr},
		{nullptr, "qm_solo_split_enter", nullptr},
		{nullptr, "qm_solo_split_leave", nullptr},
		{nullptr, "qm_solo_split_link_dummy", nullptr},
		{nullptr, "qm_solo_split_restore_team", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:solo_split", qm_solo_split);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_translate[] = {
		{Localizable("API key"), nullptr, "API 密钥"},
		{Localizable("Advanced options"), nullptr, "高级选项"},
		{Localizable("Always translate"), nullptr, "始终翻译"},
		{Localizable("Auto concurrency: %d (smart default)"), nullptr, "自动并发数：%d（智能默认）"},
		{Localizable("Auto translate received messages"), nullptr, "自动翻译收到的聊天消息"},
		{Localizable("Auto translate sent messages"), nullptr, "自动翻译发送的聊天消息"},
		{Localizable("Concurrency (0 = auto)"), nullptr, "并发数（0 = 自动）"},
		{Localizable("Custom"), nullptr, "自定义"},
		{Localizable("Custom API key"), nullptr, "自定义 API 密钥"},
		{Localizable("Custom prompt template"), nullptr, "自定义提示词模板"},
		{Localizable("Custom…"), nullptr, "自定义…"},
		{Localizable("DeepSeek"), nullptr, "DeepSeek"},
		{Localizable("DeepSeek API key"), nullptr, "DeepSeek API 密钥"},
		{Localizable("Enable FTAPI auto-translate (may overload the service)"), nullptr, "启用 FTAPI 自动翻译（可能导致服务过载）"},
		{Localizable("Enable thinking mode (slower)"), nullptr, "启用思考模式（较慢）"},
		{Localizable("Endpoint"), nullptr, "端点"},
		{Localizable("Endpoint (optional)"), nullptr, "端点（可选）"},
		{Localizable("LLM provider"), nullptr, "LLM 提供商"},
		{Localizable("Leave empty to use default prompt"), nullptr, "留空使用默认提示词"},
		{Localizable("Make sure the backend supports OpenAI-compatible thinking parameters"), nullptr, "确保后端支持 OpenAI 兼容的思考参数"},
		{Localizable("Manual concurrency: %d"), nullptr, "手动并发数：%d"},
		{Localizable("Minimum match chars"), nullptr, "最少匹配字符数"},
		{Localizable("Model"), nullptr, "模型"},
		{Localizable("MyMemory needs no registration (anonymous daily quota)"), nullptr, "MyMemory 无需注册（匿名每日配额）"},
		{Localizable("OpenAI"), nullptr, "OpenAI"},
		{Localizable("OpenAI API key"), nullptr, "OpenAI API 密钥"},
		{Localizable("Original language for incoming and outgoing text"), nullptr, "接收与发送消息共用的原文语言"},
		{Localizable("Original message language"), nullptr, "原文语言"},
		{Localizable("Receive translation method"), nullptr, "接收翻译方式"},
		{Localizable("Region"), nullptr, "地域"},
		{Localizable("SecretId"), nullptr, "密钥 ID"},
		{Localizable("SecretKey"), nullptr, "密钥"},
		{Localizable("Send translation method"), nullptr, "发送翻译方式"},
		{Localizable("Target language ratio"), nullptr, "目标语言比例"},
		{Localizable("Tencent Cloud SecretId"), nullptr, "腾讯云密钥 ID"},
		{Localizable("Tencent Cloud SecretKey"), nullptr, "腾讯云密钥"},
		{Localizable("Thinking mode requires a reasoning model"), nullptr, "启用思考模式需要使用推理模型"},
		{Localizable("Translate only when needed"), nullptr, "仅在需要时翻译"},
		{Localizable("Translate only when not in target language"), nullptr, "仅在非目标语言时翻译"},
		{Localizable("Translate outgoing messages to"), nullptr, "发送消息译为"},
		{Localizable("Translate received messages to"), nullptr, "收到消息译为"},
		{Localizable("Translation service"), nullptr, "翻译服务"},
		{Localizable("Zhipu AI"), nullptr, "智谱清言"},
		{Localizable("Zhipu API key"), nullptr, "智谱 API 密钥"},
		{Localizable("Zhipu free models allow only 1 concurrent request"), nullptr, "智谱免费模型只允许 1 个并发请求"},
		{Localizable("⚠️ FTAPI is a free service. Excessive use may cause service suspension."), nullptr, "⚠️ FTAPI 是免费服务，过度使用可能导致服务暂停"},
		{Localizable("Show original text below outgoing translation"), nullptr, "发送翻译下方显示原文"},
		{nullptr, "qm_translate_auto", nullptr},
		{nullptr, "qm_translate_auto_mode", nullptr},
		{nullptr, "qm_translate_auto_outgoing", nullptr},
		{nullptr, "qm_translate_auto_outgoing_mode", nullptr},
		{nullptr, "qm_translate_backend", nullptr},
		{nullptr, "qm_translate_deepl_key", nullptr},
		{nullptr, "qm_translate_ftapi_auto_enable", nullptr},
		{nullptr, "qm_translate_libre_endpoint", nullptr},
		{nullptr, "qm_translate_libre_key", nullptr},
		{nullptr, "qm_translate_llm_concurrency", nullptr},
		{nullptr, "qm_translate_llm_enable_thinking", nullptr},
		{nullptr, "qm_translate_llm_endpoint_custom", nullptr},
		{nullptr, "qm_translate_llm_endpoint_deepseek", nullptr},
		{nullptr, "qm_translate_llm_endpoint_openai", nullptr},
		{nullptr, "qm_translate_llm_endpoint_zhipu", nullptr},
		{nullptr, "qm_translate_llm_key_custom", nullptr},
		{nullptr, "qm_translate_llm_key_deepseek", nullptr},
		{nullptr, "qm_translate_llm_key_openai", nullptr},
		{nullptr, "qm_translate_llm_key_zhipu", nullptr},
		{nullptr, "qm_translate_llm_model_custom", nullptr},
		{nullptr, "qm_translate_llm_model_deepseek", nullptr},
		{nullptr, "qm_translate_llm_model_openai", nullptr},
		{nullptr, "qm_translate_llm_model_zhipu", nullptr},
		{nullptr, "qm_translate_llm_provider", nullptr},
		{nullptr, "qm_translate_local_detect_min_chars", nullptr},
		{nullptr, "qm_translate_local_detect_ratio", nullptr},
		{nullptr, "qm_translate_outgoing_target", nullptr},
		{nullptr, "qm_translate_show_advanced", nullptr},
		{nullptr, "qm_translate_source", nullptr},
		{nullptr, "qm_translate_system_prompt", nullptr},
		{nullptr, "qm_translate_target", nullptr},
		{nullptr, "qm_translate_tc_endpoint", nullptr},
		{nullptr, "qm_translate_tc_region", nullptr},
		{nullptr, "qm_translate_tc_secret_id", nullptr},
		{nullptr, "qm_translate_tc_secret_key", nullptr},
		{Localizable("APP ID"), nullptr, "应用 ID"},
		{Localizable("Authentication"), nullptr, "认证方式"},
		{Localizable("Bearer API key"), nullptr, "Bearer API 密钥"},
		{Localizable("DeepL API quota depends on your subscription. Get an API key at deepl.com."), nullptr, "DeepL API 额度取决于订阅方案。请在 deepl.com 获取 API 密钥。"},
		{Localizable("Failed"), nullptr, "失败"},
		{Localizable("Local service without authentication"), nullptr, "本机服务，无需认证"},
		{Localizable("Send sampling and token limit parameters"), nullptr, "发送采样与 token 上限参数"},
		{Localizable("Success"), nullptr, "成功"},
		{Localizable("Testing translation..."), nullptr, "正在测试翻译…"},
		{Localizable("Thinking controls depend on the model server. Server defaults do not guarantee that thinking is disabled."), nullptr, "思考控制取决于模型服务。使用服务默认值不代表已关闭思考。"},
		{Localizable("Thinking parameters"), nullptr, "思考参数格式"},
		{Localizable("Translated text size"), nullptr, "译文字号"},
		{Localizable("Translation diagnostics"), nullptr, "翻译诊断"},
		{Localizable("Use server defaults"), nullptr, "使用服务默认值"},
		{nullptr, "qm_chat_translation_size", nullptr},
		{nullptr, "qm_translate_baidu_app_id", nullptr},
		{nullptr, "qm_translate_baidu_key", nullptr},
		{nullptr, "qm_translate_llm_custom_auth", nullptr},
		{nullptr, "qm_translate_llm_custom_parameters", nullptr},
		{nullptr, "qm_translate_llm_custom_thinking", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:translate", qm_translate);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_translate_ui[] = {
		{Localizable("Button - Disabled"), nullptr, "按钮 - 未启用"},
		{Localizable("Button - Enabled"), nullptr, "按钮 - 已启用"},
		{Localizable("Menu background"), nullptr, "菜单背景"},
		{Localizable("Menu option - normal"), nullptr, "菜单选项 - 普通"},
		{Localizable("Menu option - selected"), nullptr, "菜单选项 - 选中"},
		{nullptr, "qm_translate_btn_color_disabled", nullptr},
		{nullptr, "qm_translate_btn_color_enabled", nullptr},
		{nullptr, "qm_translate_menu_bg_color", nullptr},
		{nullptr, "qm_translate_menu_option_normal", nullptr},
		{nullptr, "qm_translate_menu_option_selected", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:translate_ui", qm_translate_ui);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_voice[] = {
		{Localizable("Advanced options"), nullptr, "高级选项"},
		{Localizable("Allow mic permission in system settings, then reopen voice"), nullptr, "需要在系统设置里允许麦克风权限，然后重新打开语音"},
		{Localizable("Audio backend init failed"), nullptr, "音频后端初始化失败"},
		{Localizable("Audio backend init failed, try switching devices and check details"), nullptr, "音频后端初始化失败，建议切换设备后重试，并查看详细原因"},
		{Localizable("Audio init failed, check details below and logs"), nullptr, "音频初始化失败，建议查看下方详细原因并结合日志排查"},
		{Localizable("Audio issue"), nullptr, "音频问题"},
		{Localizable("Auto"), nullptr, "自动"},
		{Localizable("Auto gain control for mic (AGC, experimental)"), nullptr, "自动平衡麦克风音量（AGC，实验性）"},
		{Localizable("Auto unmute when speaking"), nullptr, "自动检测说话时开麦"},
		{Localizable("Check if voice server address is reachable"), nullptr, "建议检查语音服务器地址是否可用"},
		{Localizable("Check input device, system default mic, and mic permission first"), nullptr, "建议先检查输入设备、系统默认麦克风和麦克风权限"},
		{Localizable("Check output device, confirm headphones/speakers are still online"), nullptr, "建议先检查输出设备，确认耳机或扬声器仍在线"},
		{Localizable("Confirm both are on same server, same room, and support voice"), nullptr, "建议确认双方在同服、同房间，并且对方也支持语音"},
		{Localizable("Connect to server first to establish voice network link"), nullptr, "需要先进入服务器，语音网络链路才会建立"},
		{Localizable("Connected"), nullptr, "连接成功"},
		{Localizable("Connected, no one is speaking"), nullptr, "已联通，暂时无人说话"},
		{Localizable("Connected, waiting for first ping"), nullptr, "已连接，等待首个 ping"},
		{Localizable("Current status"), nullptr, "当前状态"},
		{Localizable("Default"), nullptr, "默认"},
		{Localizable("Default input open failed"), nullptr, "默认输入打开失败"},
		{Localizable("Default output open failed"), nullptr, "默认输出打开失败"},
		{Localizable("Detailed reason"), nullptr, "详细原因"},
		{Localizable("Disconnected"), nullptr, "已断开连接"},
		{Localizable("Enable stereo positioning"), nullptr, "启用立体声定位"},
		{Localizable("Enable voice"), nullptr, "启用语音"},
		{Localizable("Full map listen in same room"), nullptr, "同房间全图收听"},
		{Localizable("Input device"), nullptr, "输入设备"},
		{Localizable("Input device not found"), nullptr, "输入设备不存在"},
		{Localizable("Input device open failed"), nullptr, "输入设备打开失败"},
		{Localizable("Input device open failed, try reconnecting headset/mic or reselecting input device"), nullptr, "输入设备打开失败，建议重插耳机/麦克风后刷新，或重新选择输入设备"},
		{Localizable("Input switch"), nullptr, "输入切换"},
		{Localizable("Leave empty to join public room"), nullptr, "留空以进入公共房间"},
		{Localizable("Left/right channel width"), nullptr, "左右声道宽度"},
		{Localizable("Local test mode"), nullptr, "本地测试模式"},
		{Localizable("Local test mode, no server needed"), nullptr, "本地测试模式，无需服务器"},
		{Localizable("Matched with callable peer"), nullptr, "已匹配到可通话对端"},
		{Localizable("Microphone"), nullptr, "麦克风"},
		{Localizable("Microphone permission denied by system"), nullptr, "麦克风权限被系统拒绝"},
		{Localizable("Microphone volume"), nullptr, "麦克风音量"},
		{Localizable("Mute microphone"), nullptr, "静音麦克风"},
		{Localizable("Muted"), nullptr, "已静音"},
		{Localizable("No audio issues detected"), nullptr, "未发现音频异常"},
		{Localizable("No callable peer found"), nullptr, "当前未发现可通话对端"},
		{Localizable("No input device available"), nullptr, "系统没有可用输入设备"},
		{Localizable("No noise reduction"), nullptr, "不降噪"},
		{Localizable("No output device available"), nullptr, "系统没有可用输出设备"},
		{Localizable("No peer"), nullptr, "暂无对端"},
		{Localizable("Noise reduction mode"), nullptr, "降噪模式"},
		{Localizable("Not connected to server"), nullptr, "未进入服务器"},
		{Localizable("Not enabled"), nullptr, "未启用"},
		{Localizable("Not open, check input device or mic permission"), nullptr, "未打开，请检查输入设备或麦克风权限"},
		{Localizable("Not open, check output device"), nullptr, "未打开，请检查输出设备"},
		{Localizable("Opened"), nullptr, "已打开"},
		{Localizable("Output device"), nullptr, "输出设备"},
		{Localizable("Output device not found"), nullptr, "输出设备不存在"},
		{Localizable("Output device open failed"), nullptr, "输出设备打开失败"},
		{Localizable("Output device open failed, try reconnecting speakers/headphones or reselecting output device"), nullptr, "输出设备打开失败，建议重插耳机/扬声器后刷新，或重新选择输出设备"},
		{Localizable("Output switch"), nullptr, "输出切换"},
		{Localizable("Parsing voice server address"), nullptr, "正在解析语音服务器地址"},
		{Localizable("Playback volume"), nullptr, "播放音量"},
		{Localizable("Please enable voice first"), nullptr, "请先启用语音"},
		{Localizable("Receiving"), nullptr, "正在接收"},
		{Localizable("Room"), nullptr, "房间"},
		{Localizable("Room password"), nullptr, "房间密码"},
		{Localizable("Send & Receive"), nullptr, "收发"},
		{Localizable("Sending and receiving"), nullptr, "正在发送和接收"},
		{Localizable("Sending locally, suggest the other party unmute or confirm they can receive"), nullptr, "本地正在发送，建议让对方开麦或确认对方是否能接收"},
		{Localizable("Sending, waiting for peer echo"), nullptr, "正在发送，等待对端回声"},
		{Localizable("Server"), nullptr, "服务器"},
		{Localizable("Server URL"), nullptr, "服务器 URL"},
		{Localizable("Show voice connection status"), nullptr, "显示语音连接状态"},
		{Localizable("Simple noise reduction"), nullptr, "简单降噪"},
		{Localizable("Simple noise reduction strength"), nullptr, "简单降噪强度"},
		{Localizable("Speaker"), nullptr, "扬声器"},
		{Localizable("Speech trigger threshold"), nullptr, "说话触发阈值"},
		{Localizable("Start here to quickly diagnose if the issue is with device, server, or room"), nullptr, "先看这里，可以快速判断卡在设备、服务器还是房间"},
		{Localizable("Status normal, check details below if still experiencing issues"), nullptr, "当前状态正常，如仍异常请查看下方详细原因"},
		{Localizable("Switch failed"), nullptr, "切换失败"},
		{Localizable("Switched to"), nullptr, "已切换"},
		{Localizable("Switching"), nullptr, "正在切换"},
		{Localizable("Switching back to default input"), nullptr, "正在切回默认输入"},
		{Localizable("Switching back to default output"), nullptr, "正在切回默认输出"},
		{Localizable("System default"), nullptr, "系统默认"},
		{Localizable("Troubleshooting suggestions"), nullptr, "建议排查"},
		{Localizable("Try reselecting input device, confirm default mic or headset mic is online"), nullptr, "建议重新选择输入设备，并确认默认麦克风或耳机麦克风仍在线"},
		{Localizable("Try reselecting output device, confirm headphones/speakers are online"), nullptr, "建议重新选择输出设备，并确认耳机或扬声器仍在线"},
		{Localizable("Try toggling voice or reconnecting to server"), nullptr, "建议重新切换语音开关或重试进入服务器"},
		{Localizable("UDP socket not open"), nullptr, "UDP 套接字未打开"},
		{Localizable("Unclassified audio issue"), nullptr, "音频异常未分类"},
		{Localizable("Unknown status"), nullptr, "未知状态"},
		{Localizable("Use default input"), nullptr, "使用默认输入"},
		{Localizable("Use default output"), nullptr, "使用默认输出"},
		{Localizable("Voice activation release delay"), nullptr, "语音激活释放延迟"},
		{Localizable("Voice bitrate"), nullptr, "语音码率"},
		{Localizable("Voice distance radius (tiles)"), nullptr, "语音距离半径（格）"},
		{Localizable("Waiting for the voice WebSocket connection"), nullptr, "正在等待语音 WebSocket 连接"},
		{Localizable("Waiting to open"), nullptr, "等待打开"},
		{Localizable("Waiting to open input device"), nullptr, "等待打开输入设备"},
		{Localizable("Waiting to open output device"), nullptr, "等待打开输出设备"},
		{nullptr, "qm_voice_agc_enable", nullptr},
		{nullptr, "qm_voice_bitrate_profile", nullptr},
		{nullptr, "qm_voice_enable", nullptr},
		{nullptr, "qm_voice_group_global", nullptr},
		{nullptr, "qm_voice_input_device", nullptr},
		{nullptr, "qm_voice_mic_mute", nullptr},
		{nullptr, "qm_voice_mic_volume", nullptr},
		{nullptr, "qm_voice_noise_suppress_enable", nullptr},
		{nullptr, "qm_voice_noise_suppress_strength", nullptr},
		{nullptr, "qm_voice_output_device", nullptr},
		{nullptr, "qm_voice_radius", nullptr},
		{nullptr, "qm_voice_server", nullptr},
		{nullptr, "qm_voice_show_advanced", nullptr},
		{nullptr, "qm_voice_show_connection_status", nullptr},
		{nullptr, "qm_voice_stereo", nullptr},
		{nullptr, "qm_voice_stereo_width", nullptr},
		{nullptr, "qm_voice_token", nullptr},
		{nullptr, "qm_voice_vad_enable", nullptr},
		{nullptr, "qm_voice_vad_release_delay_ms", nullptr},
		{nullptr, "qm_voice_vad_threshold", nullptr},
		{nullptr, "qm_voice_volume", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:voice", qm_voice);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_weapon_trajectory[] = {
		{Localizable("Always show"), nullptr, "始终显示"},
		{Localizable("Display mode"), nullptr, "显示模式"},
		{Localizable("Guide line color"), nullptr, "辅助线颜色"},
		{Localizable("Line width"), nullptr, "线宽"},
		{Localizable("Off"), nullptr, "关"},
		{Localizable("Opacity"), nullptr, "不透明度"},
		{Localizable("Pistol guide line"), nullptr, "手枪辅助线"},
		{Localizable("Predict ninja path"), nullptr, "预测忍者路径"},
		{Localizable("Show on key"), nullptr, "按键显示"},
		{nullptr, "qm_weapon_trajectory", nullptr},
		{nullptr, "qm_weapon_trajectory_alpha", nullptr},
		{nullptr, "qm_weapon_trajectory_color", nullptr},
		{nullptr, "qm_weapon_trajectory_gun", nullptr},
		{nullptr, "qm_weapon_trajectory_ninja", nullptr},
		{nullptr, "qm_weapon_trajectory_width", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:weapon_trajectory", qm_weapon_trajectory);
}
