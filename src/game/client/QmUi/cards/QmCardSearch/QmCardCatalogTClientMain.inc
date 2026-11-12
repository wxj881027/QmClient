// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_tclient_anti_latency_tools[] = {
		{Localizable("Anti Latency Tools"), nullptr, "延迟补偿工具"},
		{Localizable("Base prediction margin"), nullptr, "基础预测量"},
		{Localizable("Delay before reduction"), nullptr, "降低前延迟"},
		{Localizable("Dont predict other players if you are frozen"), nullptr, "被冻结时不预测其他玩家"},
		{Localizable("Frozen prediction margin"), nullptr, "冻结时预测量"},
		{Localizable("Maximum reduction"), nullptr, "最大降低量"},
		{Localizable("Reduce prediction while frozen"), nullptr, "冻结时降低预测量"},
		{Localizable("Use a fixed prediction margin while frozen"), nullptr, "冻结时使用固定预测量"},
		{nullptr, "cl_prediction_margin", nullptr},
		{nullptr, "qm_pred_margin_in_freeze", nullptr},
		{nullptr, "qm_pred_margin_in_freeze_amount", nullptr},
		{nullptr, "qm_remove_anti", nullptr},
		{nullptr, "qm_remove_anti_delay_ticks", nullptr},
		{nullptr, "qm_remove_anti_ticks", nullptr},
		{nullptr, "qm_unpred_others_in_freeze", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("tclient:anti-latency-tools", tclient_anti_latency_tools);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_tclient_background_draw[] = {
		{Localizable(" seconds"), nullptr, "秒"},
		{Localizable(" seconds (never)"), nullptr, "秒 (不消失)"},
		{Localizable("Background Draw"), nullptr, "背景绘制"},
		{Localizable("Color"), nullptr, "颜色"},
		{Localizable("Draw where mouse is"), nullptr, "绘制鼠标所在位置"},
		{Localizable("Stroke fade time"), nullptr, "笔画消失时间"},
		{Localizable("Width"), nullptr, "宽度"},
		{nullptr, "qm_bg_draw_color", nullptr},
		{nullptr, "qm_bg_draw_fade_time", nullptr},
		{nullptr, "qm_bg_draw_width", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("tclient:background-draw", tclient_background_draw);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_tclient_execute_on_join[] = {
		{Localizable("Delay"), nullptr, "延迟"},
		{Localizable("Execute before connecting"), nullptr, "连接前执行"},
		{Localizable("Execute on connect"), nullptr, "连接时执行"},
		{nullptr, "qm_execute_on_connect", nullptr},
		{nullptr, "qm_execute_on_join", nullptr},
		{nullptr, "qm_execute_on_join_delay", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("tclient:execute-on-join", tclient_execute_on_join);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_tclient_finish_name[] = {
		{Localizable("Attempt to change your name when near finish"), nullptr, "过终点前尝试改名"},
		{Localizable("Finish Name"), nullptr, "终点前改名"},
		{Localizable("Finish Name:"), nullptr, "恰分名称："},
		{nullptr, "qm_change_name_near_finish", nullptr},
		{nullptr, "qm_finish_name", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("tclient:finish-name", tclient_finish_name);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_tclient_font[] = {
		{Localizable("(Follow Chinese font)"), nullptr, "（跟随中文字体）"},
		{Localizable("(Follow English font)"), nullptr, "（跟随英文字体）"},
		{Localizable("CJK font weight:"), nullptr, "中日韩字体字重："},
		{Localizable("Chinese font:"), nullptr, "中文字体："},
		{Localizable("Default"), nullptr, "默认"},
		{Localizable("Download fonts from the internet instead of shipping them with the client"), nullptr, "从网络下载字体，而不是随客户端一起分发"},
		{Localizable("English font:"), nullptr, "英文字体："},
		{Localizable("Font"), nullptr, "字体"},
		{Localizable("Font store:"), nullptr, "字体商店："},
		{Localizable("Font weight:"), nullptr, "字体字重："},
		{Localizable("Icon symbols:"), nullptr, "图标符号："},
		{Localizable("Installed"), nullptr, "已安装"},
		{Localizable("Preview"), nullptr, "预览"},
		{Localizable("Japanese"), nullptr, "日语"},
		{Localizable("Korean"), nullptr, "韩语"},
		{Localizable("Chinese (Simplified)"), nullptr, "简体中文"},
		{Localizable("Chinese (Traditional)"), nullptr, "繁体中文"},
		{Localizable("Cyrillic"), nullptr, "西里尔文"},
		{Localizable("Greek"), nullptr, "希腊文"},
		{Localizable("Arabic"), nullptr, "阿拉伯文"},
		{Localizable("Font store"), nullptr, "字体商店"},
		{Localizable("Search online fonts…"), nullptr, "搜索在线字体…"},
		{Localizable("Sans-serif"), nullptr, "无衬线"},
		{Localizable("Serif"), nullptr, "衬线"},
		{Localizable("Display"), nullptr, "展示"},
		{Localizable("Handwriting"), nullptr, "手写"},
		{Localizable("Monospace"), nullptr, "等宽"},
		{Localizable("Not installed"), nullptr, "未安装"},
		{Localizable("Total fonts:"), nullptr, "字体总数："},
		{Localizable("Show system fonts"), nullptr, "显示系统字体"},
		{nullptr, "cl_text_entities_size", nullptr},
		{nullptr, "qm_custom_font", nullptr},
		{nullptr, "qm_custom_font_cjk", nullptr},
		{nullptr, "qm_custom_font_icons", nullptr},
		{nullptr, "qm_custom_font_weight", nullptr},
		{nullptr, "qm_custom_font_weight_cjk", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("tclient:font", tclient_font);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_tclient_ghost_tools[] = {
		{Localizable("Ghost tools"), nullptr, "影子工具"},
		{Localizable("Hide ghosts of frozen players"), nullptr, "隐藏冻结玩家的影子"},
		{Localizable("Predicted ghost opacity"), nullptr, "预测影子不透明度"},
		{Localizable("Render ghosts as circles"), nullptr, "将影子渲染为圆圈"},
		{Localizable("Show unpredicted ghosts for other players"), nullptr, "显示其他玩家未预测的影子"},
		{Localizable("Swap ghosts with regular players"), nullptr, "交换影子与普通玩家"},
		{Localizable("Toggle ghost key"), nullptr, "切换影子键"},
		{Localizable("Unpredicted ghost opacity"), nullptr, "未预测影子不透明度"},
		{nullptr, "qm_hide_frozen_ghosts", nullptr},
		{nullptr, "qm_pred_ghosts_alpha", nullptr},
		{nullptr, "qm_render_ghost_as_circle", nullptr},
		{nullptr, "qm_show_others_ghosts", nullptr},
		{nullptr, "qm_swap_ghosts", nullptr},
		{nullptr, "qm_unpred_ghosts_alpha", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("tclient:ghost-tools", tclient_ghost_tools);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_tclient_hud[] = {
		{Localizable("Font size"), nullptr, "字体大小"},
		{Localizable("Freeview cursor opacity"), nullptr, "自由旁观光标不透明度"},
		{Localizable("HUD"), nullptr, "HUD"},
		{Localizable("Horizontal position"), nullptr, "水平位置"},
		{Localizable("Notify when only one tee is still alive:"), nullptr, "当只剩一名存活者时提示："},
		{Localizable("Screen center line color"), nullptr, "屏幕中心线颜色"},
		{Localizable("Screen center line width"), nullptr, "屏幕中心线宽度"},
		{Localizable("Show compact vote HUD"), nullptr, "显示小型投票 HUD"},
		{Localizable("Show position and angle (mini debug)"), nullptr, "显示位置和角度（小型调试）"},
		{Localizable("Show the cursor while free spectating"), nullptr, "自由观战时显示光标"},
		{Localizable("Show the screen center line"), nullptr, "显示屏幕中心线"},
		{Localizable("Vertical position"), nullptr, "垂直位置"},
		{Localizable("You're the last one!"), nullptr, "就你一个啦！"},
		{nullptr, "qm_cursor_in_spec", nullptr},
		{nullptr, "qm_cursor_in_spec_alpha", nullptr},
		{nullptr, "qm_last_notify", nullptr},
		{nullptr, "qm_last_notify_color", nullptr},
		{nullptr, "qm_last_notify_size", nullptr},
		{nullptr, "qm_last_notify_text", nullptr},
		{nullptr, "qm_last_notify_x", nullptr},
		{nullptr, "qm_last_notify_y", nullptr},
		{nullptr, "qm_mini_debug", nullptr},
		{nullptr, "qm_mini_vote_hud", nullptr},
		{nullptr, "qm_show_center", nullptr},
		{nullptr, "qm_show_center_color", nullptr},
		{nullptr, "qm_show_center_width", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("tclient:hud", tclient_hud);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_tclient_improved_anti_ping[] = {
		{Localizable("Negative stability buffer (for Gores)"), nullptr, "负稳定性缓冲（用于 Gores）"},
		{Localizable("Optimistic prediction along stable direction"), nullptr, "沿稳定方向更乐观的预测"},
		{Localizable("Uncertainty duration"), nullptr, "不确定性持续时间"},
		{Localizable("Use new smoothing algorithm"), nullptr, "使用新的平滑算法"},
		{nullptr, "qm_antiping_improved", nullptr},
		{nullptr, "qm_antiping_negative_buffer", nullptr},
		{nullptr, "qm_antiping_stable_direction", nullptr},
		{nullptr, "qm_antiping_uncertainty_scale", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("tclient:improved-anti-ping", tclient_improved_anti_ping);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_tclient_input[] = {
		{Localizable("Amount"), nullptr, "预测量"},
		{Localizable("Auto margin"), nullptr, "自动边距"},
		{Localizable("Fast input (reduce visual latency)"), nullptr, "快速输入（降低视觉延迟）"},
		{Localizable("Fast input others"), nullptr, "快速输入其他玩家"},
		{Localizable("Input"), nullptr, "输入"},
		{Localizable("Sub-Tick aiming"), nullptr, "Sub-Tick 瞄准"},
		{nullptr, "cl_sub_tick_aiming", nullptr},
		{nullptr, "qm_auto_margin", nullptr},
		{nullptr, "qm_fast_input", nullptr},
		{nullptr, "qm_fast_input_amount", nullptr},
		{nullptr, "qm_fast_input_others", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("tclient:input", tclient_input);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_tclient_pet[] = {
		{Localizable("Pet"), nullptr, "宠物"},
		{Localizable("Pet Skin:"), nullptr, "宠物皮肤："},
		{Localizable("Pet alpha"), nullptr, "宠物透明度"},
		{Localizable("Pet size"), nullptr, "宠物大小"},
		{Localizable("Show the pet"), nullptr, "显示宠物"},
		{nullptr, "qm_pet_alpha", nullptr},
		{nullptr, "qm_pet_show", nullptr},
		{nullptr, "qm_pet_size", nullptr},
		{nullptr, "qm_pet_skin", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("tclient:pet", tclient_pet);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_tclient_player_indicator[] = {
		{Localizable("Change indicator offset based on distance to other tees"), nullptr, "根据与其他 Tee 的距离更改指示器偏移量"},
		{Localizable("Hide indicator for tees on your screen"), nullptr, "其他玩家在屏幕内时隐藏指示器"},
		{Localizable("Indicator alive color"), nullptr, "指示器存活颜色"},
		{Localizable("Indicator in freeze color"), nullptr, "指示器冻结颜色"},
		{Localizable("Indicator max distance"), nullptr, "指示器最大距离"},
		{Localizable("Indicator max offset"), nullptr, "指示器最大偏移量"},
		{Localizable("Indicator min offset"), nullptr, "指示器最小偏移量"},
		{Localizable("Indicator offset"), nullptr, "指示器偏移量"},
		{Localizable("Indicator opacity"), nullptr, "指示器不透明度"},
		{Localizable("Indicator safe color"), nullptr, "指示器安全颜色"},
		{Localizable("Indicator size"), nullptr, "显示大小"},
		{Localizable("Only show after joining a team"), nullptr, "仅在加入队伍后显示"},
		{Localizable("Player indicator"), nullptr, "玩家指示器"},
		{Localizable("Render tiny tees instead of circles"), nullptr, "渲染成迷你 Tee 而不是圆圈"},
		{Localizable("Show %s group"), nullptr, "显示 %s 分组"},
		{Localizable("Show all warlist groups"), nullptr, "显示所有敌对群组"},
		{Localizable("Show any enabled Indicators"), nullptr, "开启玩家指示器"},
		{Localizable("Show only freeze Players"), nullptr, "仅显示冻结玩家"},
		{Localizable("Use warlist colors instead of regular colors"), nullptr, "使用敌对列表颜色而不是常规颜色"},
		{Localizable("Use warlist groups for indicator"), nullptr, "使用敌对列表作为指示器"},
		{nullptr, "qm_indicator_alive", nullptr},
		{nullptr, "qm_indicator_dead", nullptr},
		{nullptr, "qm_indicator_freeze", nullptr},
		{nullptr, "qm_indicator_hide_visible_tees", nullptr},
		{nullptr, "qm_indicator_inteam", nullptr},
		{nullptr, "qm_indicator_offset", nullptr},
		{nullptr, "qm_indicator_offset_max", nullptr},
		{nullptr, "qm_indicator_opacity", nullptr},
		{nullptr, "qm_indicator_radius", nullptr},
		{nullptr, "qm_indicator_tees", nullptr},
		{nullptr, "qm_indicator_variable_distance", nullptr},
		{nullptr, "qm_indicator_variable_max_distance", nullptr},
		{nullptr, "qm_player_indicator", nullptr},
		{nullptr, "qm_player_indicator_freeze", nullptr},
		{nullptr, "qm_warlist_indicator", nullptr},
		{nullptr, "qm_warlist_indicator_all", nullptr},
		{nullptr, "qm_warlist_indicator_colors", nullptr},
		{nullptr, "qm_warlist_indicator_enemy", nullptr},
		{nullptr, "qm_warlist_indicator_team", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("tclient:player-indicator", tclient_player_indicator);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_tclient_rainbow[] = {
		{Localizable("Black"), nullptr, "黑色"},
		{Localizable("Pulse"), nullptr, "脉冲"},
		{Localizable("Rainbow"), nullptr, "彩虹"},
		{Localizable("Rainbow Tees"), nullptr, "彩虹色 Tee"},
		{Localizable("Rainbow hook"), nullptr, "彩虹色钩索"},
		{Localizable("Rainbow others"), nullptr, "将彩虹带给每个人！"},
		{Localizable("Rainbow speed"), nullptr, "颜色变化速度"},
		{Localizable("Rainbow weapons"), nullptr, "彩虹色武器"},
		{Localizable("Random"), nullptr, "随机"},
		{nullptr, "qm_rainbow_hook", nullptr},
		{nullptr, "qm_rainbow_mode", nullptr},
		{nullptr, "qm_rainbow_others", nullptr},
		{nullptr, "qm_rainbow_speed", nullptr},
		{nullptr, "qm_rainbow_tees", nullptr},
		{nullptr, "qm_rainbow_weapon", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("tclient:rainbow", tclient_rainbow);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_tclient_tee_status_bar[] = {
		{Localizable("Maximum rows"), nullptr, "最大行数"},
		{Localizable("Only show after joining a team"), nullptr, "仅在加入队伍后显示"},
		{Localizable("Show tee status bar"), nullptr, "显示 Tee 状态栏"},
		{Localizable("Show the number of frozen tees"), nullptr, "显示冻结 Tee 的数量"},
		{Localizable("Show the number of tees still alive"), nullptr, "显示剩余存活 Tee 的数量"},
		{Localizable("Tee size"), nullptr, "Tee 大小"},
		{Localizable("Tee status bar"), nullptr, "Tee 状态栏"},
		{Localizable("Use custom skins instead of the ninja tee"), nullptr, "使用自定义皮肤代替忍者 Tee"},
		{nullptr, "qm_frozen_tees_hud", nullptr},
		{nullptr, "qm_frozen_tees_hud_skins", nullptr},
		{nullptr, "qm_frozen_tees_max_rows", nullptr},
		{nullptr, "qm_frozen_tees_only_inteam", nullptr},
		{nullptr, "qm_frozen_tees_size", nullptr},
		{nullptr, "qm_frozen_tees_text", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("tclient:tee-status-bar", tclient_tee_status_bar);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_tclient_tile_outlines[] = {
		{Localizable("Deep freeze color"), nullptr, "深度冻结颜色"},
		{Localizable("Deep unfreeze color"), nullptr, "深度解冻颜色"},
		{Localizable("Freeze"), nullptr, "冻结"},
		{Localizable("Kill"), nullptr, "自杀"},
		{Localizable("Only show outlines in the entities layer"), nullptr, "仅在实体层中显示轮廓"},
		{Localizable("Outline opacity"), nullptr, "描边不透明度"},
		{Localizable("Show all enabled outlines"), nullptr, "显示所有启用的轮廓"},
		{Localizable("Solid"), nullptr, "纯色"},
		{Localizable("Solid tile outline opacity"), nullptr, "墙体轮廓不透明度"},
		{Localizable("Tele"), nullptr, "传送轮廓"},
		{Localizable("Tile outlines"), nullptr, "墙体轮廓"},
		{Localizable("Unfreeze"), nullptr, "解冻"},
		{Localizable("Width", "Outlines"), nullptr, "宽度", "Outlines"},
		{nullptr, "qm_outline", nullptr},
		{nullptr, "qm_outline_alpha", nullptr},
		{nullptr, "qm_outline_color_deep_freeze", nullptr},
		{nullptr, "qm_outline_color_deep_unfreeze", nullptr},
		{nullptr, "qm_outline_color_freeze", nullptr},
		{nullptr, "qm_outline_color_kill", nullptr},
		{nullptr, "qm_outline_color_solid", nullptr},
		{nullptr, "qm_outline_color_tele", nullptr},
		{nullptr, "qm_outline_color_unfreeze", nullptr},
		{nullptr, "qm_outline_freeze", nullptr},
		{nullptr, "qm_outline_in_entities", nullptr},
		{nullptr, "qm_outline_kill", nullptr},
		{nullptr, "qm_outline_solid", nullptr},
		{nullptr, "qm_outline_solid_alpha", nullptr},
		{nullptr, "qm_outline_tele", nullptr},
		{nullptr, "qm_outline_unfreeze", nullptr},
		{nullptr, "qm_outline_width_freeze", nullptr},
		{nullptr, "qm_outline_width_kill", nullptr},
		{nullptr, "qm_outline_width_solid", nullptr},
		{nullptr, "qm_outline_width_tele", nullptr},
		{nullptr, "qm_outline_width_unfreeze", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("tclient:tile-outlines", tclient_tile_outlines);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_tclient_visual_effects[] = {
		{Localizable("All"), nullptr, "全体"},
		{Localizable("Blue"), nullptr, "蓝色"},
		{Localizable("Enable Jelly Tee"), nullptr, "启用 Q 弹 Tee"},
		{Localizable("Fake CTF flags"), nullptr, "伪 CTF 旗帜"},
		{Localizable("Jelly duration"), nullptr, "回弹时长"},
		{Localizable("Jelly others"), nullptr, "其他玩家也使用 Q 弹 Tee"},
		{Localizable("Jelly strength"), nullptr, "Q 弹强度"},
		{Localizable("None"), nullptr, "无"},
		{Localizable("Red"), nullptr, "红色"},
		{Localizable("Self"), nullptr, "自身"},
		{Localizable("Smaller tees"), nullptr, "小尺寸 Tee"},
		{Localizable("Tiny Tee Size"), nullptr, "迷你 Tee 大小"},
		{nullptr, "qm_fake_ctf_flags", nullptr},
		{nullptr, "qm_indicator_tees_size", nullptr},
		{nullptr, "qm_jelly_tee", nullptr},
		{nullptr, "qm_jelly_tee_duration", nullptr},
		{nullptr, "qm_jelly_tee_others", nullptr},
		{nullptr, "qm_jelly_tee_strength", nullptr},
		{nullptr, "qm_tiny_tees", nullptr},
		{nullptr, "qm_tiny_tees_others", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("tclient:visual-effects", tclient_visual_effects);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_tclient_visual_nameplates[] = {
		{Localizable("Freeze stars"), nullptr, "显示冻结效果的星星动画"},
		{Localizable("Render all custom colored feet as white feet skin"), nullptr, "将所有自定义脚部颜色渲染为白色"},
		{Localizable("Show country flags in nameplates"), nullptr, "在玩家昵称旁显示国旗"},
		{Localizable("Show katana on frozen players"), nullptr, "在被冻结的玩家身上显示武士刀"},
		{Localizable("Show ping colored circle in nameplates"), nullptr, "在玩家昵称旁显示延迟颜色圆圈"},
		{Localizable("Show skin names in nameplate"), nullptr, "在玩家昵称旁显示皮肤名"},
		{Localizable("Use colored skins for frozen tees"), nullptr, "冻结 Tee 使用彩色皮肤"},
		{nullptr, "cl_freeze_stars", nullptr},
		{nullptr, "qm_color_freeze", nullptr},
		{nullptr, "qm_frozen_katana", nullptr},
		{nullptr, "qm_nameplate_country", nullptr},
		{nullptr, "qm_nameplate_ping_circle", nullptr},
		{nullptr, "qm_nameplate_skins", nullptr},
		{nullptr, "qm_white_feet", nullptr},
		{nullptr, "qm_white_feet_skin", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("tclient:visual-nameplates", tclient_visual_nameplates);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_tclient_voting[] = {
		{Localizable(" min"), nullptr, "分钟"},
		{Localizable("Auto agree vote"), nullptr, "自动同意投票"},
		{Localizable("Auto map vote"), nullptr, "自动地图投票"},
		{Localizable("Auto reject vote"), nullptr, "自动拒绝投票"},
		{Localizable("Leave empty to disable"), nullptr, "留空以禁用"},
		{Localizable("Message to send in chat:"), nullptr, "要在聊天中发送的消息："},
		{Localizable("Minimum time"), nullptr, "最短时间"},
		{Localizable("Off"), nullptr, "关"},
		{Localizable("Voting"), nullptr, "投票"},
		{nullptr, "qm_auto_vote_when_far", nullptr},
		{nullptr, "qm_auto_vote_when_far_message", nullptr},
		{nullptr, "qm_auto_vote_when_far_time", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("tclient:voting", tclient_voting);
}
