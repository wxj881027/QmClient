// This file can be included several times.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(Tcme, ScriptName, Def, Min, Max, Save, Desc) ;
#define MACRO_CONFIG_COL(Tcme, ScriptName, Def, Save, Desc) ;
#define MACRO_CONFIG_STR(Tcme, ScriptName, Len, Def, Save, Desc) ;
#endif

#if defined(CONF_FAMILY_WINDOWS)
MACRO_CONFIG_INT(TcAllowAnyRes, tc_allow_any_res, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "允许缩放时使用任意游戏分辨率（Windows 上可能有问题）")
#else
MACRO_CONFIG_INT(TcAllowAnyRes, tc_allow_any_res, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "允许缩放时使用任意游戏分辨率（Windows 上可能有问题）")
#endif

MACRO_CONFIG_INT(TcShowChatClient, tc_show_chat_client, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示客户端生成的聊天消息，例如 echo")

MACRO_CONFIG_INT(TcShowFrozenText, tc_frozen_tees_text, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示队伍中冻结 Tee 的数量（0=关闭，1=显示活动 Tee，2=显示冻结 Tee）")
MACRO_CONFIG_INT(TcShowFrozenHud, tc_frozen_tees_hud, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示冻结 Tee HUD")
MACRO_CONFIG_INT(TcShowFrozenHudSkins, tc_frozen_tees_hud_skins, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在冻结 Tee HUD 中使用忍者皮肤或深色皮肤")

MACRO_CONFIG_INT(TcFrozenHudTeeSize, tc_frozen_tees_size, 15, 8, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "冻结 Tee HUD 中 Tee 图标大小（默认 15）")
MACRO_CONFIG_INT(TcFrozenMaxRows, tc_frozen_tees_max_rows, 1, 1, 6, CFGFLAG_CLIENT | CFGFLAG_SAVE, "冻结 Tee HUD 的最大行数")
MACRO_CONFIG_INT(TcFrozenHudTeamOnly, tc_frozen_tees_only_inteam, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "仅在队伍内显示冻结 Tee HUD")

MACRO_CONFIG_INT(TcNameplatePingCircle, tc_nameplate_ping_circle, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在名字板上显示 Ping 圆环")
MACRO_CONFIG_INT(TcNameplateCountry, tc_nameplate_country, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "名字板上显示国旗")
MACRO_CONFIG_INT(TcNameplateSkins, tc_nameplate_skins, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在名字板上显示皮肤名，便于查找缺失皮肤")

MACRO_CONFIG_INT(TcFakeCtfFlags, tc_fake_ctf_flags, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在玩家身上显示伪 CTF 旗帜（0=关闭，1=红旗，2=蓝旗）")

MACRO_CONFIG_INT(TcLimitMouseToScreen, tc_limit_mouse_to_screen, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "将鼠标限制在屏幕范围内")
MACRO_CONFIG_INT(TcScaleMouseDistance, tc_scale_mouse_distance, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "将最大鼠标距离缩放到 1000，以提高瞄准精度")

MACRO_CONFIG_INT(TcHammerRotatesWithCursor, tc_hammer_rotates_with_cursor, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "让锤子像其他武器一样随光标旋转")

MACRO_CONFIG_INT(TcMiniVoteHud, tc_mini_vote_hud, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "缩小投票界面")

// Anti Latency Tools
MACRO_CONFIG_INT(TcRemoveAnti, tc_remove_anti, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "冻结时减少 antiping 和玩家预测")
MACRO_CONFIG_INT(TcUnfreezeLagTicks, tc_remove_anti_ticks, 5, 0, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "最多减少的预测 tick 数")
MACRO_CONFIG_INT(TcUnfreezeLagDelayTicks, tc_remove_anti_delay_ticks, 25, 5, 150, CFGFLAG_CLIENT | CFGFLAG_SAVE, "冻结后经过多少 tick 才应用最大预测减少")

MACRO_CONFIG_INT(TcUnpredOthersInFreeze, tc_unpred_others_in_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "自己被冻结时不预测其他玩家")
MACRO_CONFIG_INT(TcPredMarginInFreeze, tc_pred_margin_in_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "冻结时启用自定义预测边距")
MACRO_CONFIG_INT(TcPredMarginInFreezeAmount, tc_pred_margin_in_freeze_amount, 15, 0, 2000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "冻结时的预测边距")

MACRO_CONFIG_INT(TcShowOthersGhosts, tc_show_others_ghosts, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在其他玩家的不可预测位置显示幽灵")
MACRO_CONFIG_INT(TcSwapGhosts, tc_swap_ghosts, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "将预测位置显示为幽灵，原始位置显示为不可预测玩家")
MACRO_CONFIG_INT(TcHideFrozenGhosts, tc_hide_frozen_ghosts, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "如果其他玩家的幽灵被冻结则隐藏")

MACRO_CONFIG_INT(TcPredGhostsAlpha, tc_pred_ghosts_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "预测幽灵透明度（0-100）")
MACRO_CONFIG_INT(TcUnpredGhostsAlpha, tc_unpred_ghosts_alpha, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "不可预测幽灵透明度（0-100）")
MACRO_CONFIG_INT(TcRenderGhostAsCircle, tc_render_ghost_as_circle, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "将幽灵渲染为圆圈而不是 Tee")

MACRO_CONFIG_INT(TcShowCenter, tc_show_center, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "绘制线条显示屏幕/点击框中心")
MACRO_CONFIG_INT(TcShowCenterWidth, tc_show_center_width, 0, 0, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "中心线宽度（需启用 tc_show_center）")
MACRO_CONFIG_COL(TcShowCenterColor, tc_show_center_color, 1694498688, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "中心线颜色（需启用 tc_show_center）") // transparent red

MACRO_CONFIG_INT(TcHookCollCursor, tc_hook_coll_cursor, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "钩子碰撞线长度跟随光标距离")

MACRO_CONFIG_INT(TcFastInput, tc_fast_input, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在下一 tick 前提前应用输入进行预测")
MACRO_CONFIG_INT(TcFastInputAmount, tc_fast_input_amount, 20, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "快速输入提前应用的毫秒数")
MACRO_CONFIG_INT(TcFastInputOthers, tc_fast_input_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "对其他 Tee 也应用快速输入")

MACRO_CONFIG_INT(TcAntiPingImproved, tc_antiping_improved, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "使用另一套 antiping 平滑算法，与 cl_antiping_smooth 不兼容")
MACRO_CONFIG_INT(TcAntiPingNegativeBuffer, tc_antiping_negative_buffer, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "对 Gores 有帮助：允许内部确定性为负值，使预测更保守")
MACRO_CONFIG_INT(TcAntiPingStableDirection, tc_antiping_stable_direction, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "沿 Tee 的稳定方向进行更乐观的预测，减少稳定下来所需的延迟")
MACRO_CONFIG_INT(TcAntiPingUncertaintyScale, tc_antiping_uncertainty_scale, 150, 25, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "按 ping 比例缩放不确定性时长（100=1.0 倍）")

MACRO_CONFIG_INT(TcColorFreeze, tc_color_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "冻结 Tee 时使用皮肤颜色")
MACRO_CONFIG_INT(TcColorFreezeDarken, tc_color_freeze_darken, 90, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "冻结时加深 Tee 颜色（0-100）")
MACRO_CONFIG_INT(TcColorFreezeFeet, tc_color_freeze_feet, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "冻结 Tee 的脚部也使用皮肤颜色")

// Revert Variables
MACRO_CONFIG_INT(TcSmoothPredictionMargin, tc_prediction_margin_smooth, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "平滑过渡预测边距（会削弱 ping 抖动修正，恢复旧行为）")
MACRO_CONFIG_INT(TcFreezeKatana, tc_frozen_katana, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在冻结玩家身上显示武士刀（恢复旧行为）")
MACRO_CONFIG_INT(TcOldTeamColors, tc_old_team_colors, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "使用旧版彩虹队伍颜色（恢复旧行为）")
MACRO_CONFIG_INT(TcRevertHookLine, tc_revert_hook_line, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "恢复旧版单段钩子碰撞线行为")

// Water Fall (Death) Auto Emoticon and Chat
MACRO_CONFIG_INT(TcWaterFallEnabled, tc_waterfall_enabled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "落水或死亡时自动发送爱心表情和聊天消息")
MACRO_CONFIG_INT(TcWaterFallEmoticon, tc_waterfall_emoticon, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "落水/死亡时发送爱心表情")
MACRO_CONFIG_STR(TcWaterFallMessage, tc_waterfall_message, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "落水/死亡时发送的聊天消息（空=无消息）")

// Freeze Auto Emoticon and Chat
MACRO_CONFIG_INT(TcFreezeChatEnabled, tc_freeze_chat_enabled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "进入冻结时自动发送表情和聊天消息")
MACRO_CONFIG_INT(TcFreezeChatEmoticon, tc_freeze_chat_emoticon, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "进入冻结状态发送表情")
MACRO_CONFIG_INT(TcFreezeChatEmoticonId, tc_freeze_chat_emoticon_id, 7, 0, 15, CFGFLAG_CLIENT | CFGFLAG_SAVE, "进入冻结状态时发送的表情 ID（0-15）")
MACRO_CONFIG_STR(TcFreezeChatMessage, tc_freeze_chat_message, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "进入冻结状态时发送的聊天消息，以逗号分隔（空=无消息）")
MACRO_CONFIG_INT(TcFreezeChatChance, tc_freeze_chat_chance, 30, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "发送冻结聊天消息的概率（0-100%）")

// Outline Variables
MACRO_CONFIG_INT(TcOutline, tc_outline, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用图块轮廓")
MACRO_CONFIG_INT(TcOutlineEntities, tc_outline_in_entities, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "仅在实体层显示轮廓")
MACRO_CONFIG_INT(TcOutlineAlpha, tc_outline_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "轮廓全局透明度")
MACRO_CONFIG_INT(TcOutlineSolidAlpha, tc_outline_solid_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "实体墙轮廓透明度")

MACRO_CONFIG_INT(TcOutlineSolid, tc_outline_solid, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示可钩/不可钩图块轮廓")
MACRO_CONFIG_INT(TcOutlineFreeze, tc_outline_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示冻结/深冻图块轮廓")
MACRO_CONFIG_INT(TcOutlineUnfreeze, tc_outline_unfreeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示解冻/深解冻图块轮廓")
MACRO_CONFIG_INT(TcOutlineKill, tc_outline_kill, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示死亡图块轮廓")
MACRO_CONFIG_INT(TcOutlineTele, tc_outline_tele, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示传送图块轮廓")

MACRO_CONFIG_INT(TcOutlineWidthSolid, tc_outline_width_solid, 2, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "可钩/不可钩图块轮廓宽度")
MACRO_CONFIG_INT(TcOutlineWidthFreeze, tc_outline_width_freeze, 2, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "冻结/深冻图块轮廓宽度")
MACRO_CONFIG_INT(TcOutlineWidthUnfreeze, tc_outline_width_unfreeze, 2, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "解冻/深解冻图块轮廓宽度")
MACRO_CONFIG_INT(TcOutlineWidthKill, tc_outline_width_kill, 2, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "死亡图块轮廓宽度")
MACRO_CONFIG_INT(TcOutlineWidthTele, tc_outline_width_tele, 2, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "传送图块轮廓宽度")

MACRO_CONFIG_COL(TcOutlineColorSolid, tc_outline_color_solid, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "可钩/不可钩图块轮廓颜色") // 255 0 0 0
MACRO_CONFIG_COL(TcOutlineColorFreeze, tc_outline_color_freeze, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "冻结图块轮廓颜色") // 255 0 0 0
MACRO_CONFIG_COL(TcOutlineColorDeepFreeze, tc_outline_color_deep_freeze, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "深冻图块轮廓颜色") // 255 0 0 0
MACRO_CONFIG_COL(TcOutlineColorUnfreeze, tc_outline_color_unfreeze, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "解冻图块轮廓颜色") // 255 0 0 0
MACRO_CONFIG_COL(TcOutlineColorDeepUnfreeze, tc_outline_color_deep_unfreeze, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "深解冻图块轮廓颜色") // 255 0 0 0
MACRO_CONFIG_COL(TcOutlineColorKill, tc_outline_color_kill, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "死亡图块轮廓颜色") // 0 0 0
MACRO_CONFIG_COL(TcOutlineColorTele, tc_outline_color_tele, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "传送图块轮廓颜色") // 255 0 0 0

// Indicator Variables
MACRO_CONFIG_COL(TcIndicatorAlive, tc_indicator_alive, 255, CFGFLAG_CLIENT | CFGFLAG_SAVE, "玩家指示器中存活 Tee 的颜色")
MACRO_CONFIG_COL(TcIndicatorFreeze, tc_indicator_freeze, 65407, CFGFLAG_CLIENT | CFGFLAG_SAVE, "玩家指示器中冻结 Tee 的颜色")
MACRO_CONFIG_COL(TcIndicatorSaved, tc_indicator_dead, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE, "玩家指示器中脱困中 Tee 的颜色")
MACRO_CONFIG_INT(TcIndicatorOffset, tc_indicator_offset, 42, 16, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "指示器偏移距离")
MACRO_CONFIG_INT(TcIndicatorOffsetMax, tc_indicator_offset_max, 100, 16, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "可变偏移时的最大偏移距离")
MACRO_CONFIG_INT(TcIndicatorVariableDistance, tc_indicator_variable_distance, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "距离越远，指示器离自己越远")
MACRO_CONFIG_INT(TcIndicatorMaxDistance, tc_indicator_variable_max_distance, 1000, 500, 7000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "可变偏移计算的最大距离")
MACRO_CONFIG_INT(TcIndicatorRadius, tc_indicator_radius, 4, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "指示器半径")
MACRO_CONFIG_INT(TcIndicatorOpacity, tc_indicator_opacity, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "指示器透明度")
MACRO_CONFIG_INT(TcPlayerIndicator, tc_player_indicator, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示其他 Tee 的径向指示器")
MACRO_CONFIG_INT(TcPlayerIndicatorFreeze, tc_player_indicator_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "仅在指示器中显示冻结 Tee")
MACRO_CONFIG_INT(TcIndicatorTeamOnly, tc_indicator_inteam, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "仅在队伍内显示指示器")
MACRO_CONFIG_INT(TcIndicatorTees, tc_indicator_tees, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "用 Tee 图标代替圆圈")
MACRO_CONFIG_INT(TcIndicatorHideVisible, tc_indicator_hide_visible_tees, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "不显示屏幕内可见的 Tee")

// Bind Wheel
MACRO_CONFIG_INT(TcResetBindWheelMouse, tc_reset_bindwheel_mouse, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "打开绑定轮时重置鼠标位置")

// Regex chat matching
MACRO_CONFIG_STR(TcRegexChatIgnore, tc_regex_chat_ignore, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "按正则表达式过滤聊天消息")

// Misc visual
MACRO_CONFIG_INT(TcWhiteFeet, tc_white_feet, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "将所有脚部渲染为纯白底色")
MACRO_CONFIG_STR(TcWhiteFeetSkin, tc_white_feet_skin, 255, "x_ninja", CFGFLAG_CLIENT | CFGFLAG_SAVE, "白脚底使用的皮肤")
MACRO_CONFIG_INT(TcMovingTilesEntities, tc_moving_tiles_entities, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在实体层显示移动图块")

MACRO_CONFIG_INT(TcMiniDebug, tc_mini_debug, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示位置和角度调试信息")

MACRO_CONFIG_INT(TcShowhudDummyPosition, tc_showhud_dummy_position, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在运动信息 HUD 中显示分身位置")
MACRO_CONFIG_INT(TcShowhudDummySpeed, tc_showhud_dummy_speed, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在运动信息 HUD 中显示分身速度")
MACRO_CONFIG_INT(TcShowhudDummyAngle, tc_showhud_dummy_angle, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在运动信息 HUD 中显示分身角度")
MACRO_CONFIG_INT(TcShowLocalTimeSeconds, tc_show_local_time_seconds, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在本地时间显示中包含秒")

MACRO_CONFIG_INT(TcNotifyWhenLast, tc_last_notify, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "仅剩一个 Tee 存活时显示提示")
MACRO_CONFIG_STR(TcNotifyWhenLastText, tc_last_notify_text, 64, "Last!", CFGFLAG_CLIENT | CFGFLAG_SAVE, "存活提醒文本")
MACRO_CONFIG_COL(TcNotifyWhenLastColor, tc_last_notify_color, 256, CFGFLAG_CLIENT | CFGFLAG_SAVE, "存活提醒颜色")
MACRO_CONFIG_INT(TcNotifyWhenLastX, tc_last_notify_x, 20, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "存活提醒水平位置（占屏幕宽度百分比）")
MACRO_CONFIG_INT(TcNotifyWhenLastY, tc_last_notify_y, 1, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "存活提醒垂直位置（占屏幕高度百分比）")
MACRO_CONFIG_INT(TcNotifyWhenLastSize, tc_last_notify_size, 10, 0, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE, "存活提醒字体大小")
MACRO_CONFIG_INT(TcJumpHint, tc_jump_hint, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "根据位置小数部分显示跳跃提示")
MACRO_CONFIG_COL(TcJumpHintColor, tc_jump_hint_color, 256, CFGFLAG_CLIENT | CFGFLAG_SAVE, "跳跃提示颜色")
MACRO_CONFIG_INT(TcJumpHintX, tc_jump_hint_x, 20, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "跳跃提示水平位置（占屏幕宽度百分比）")
MACRO_CONFIG_INT(TcJumpHintY, tc_jump_hint_y, 5, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "跳跃提示垂直位置（占屏幕高度百分比）")
MACRO_CONFIG_INT(TcJumpHintSize, tc_jump_hint_size, 10, 0, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE, "跳跃提示字体大小")

MACRO_CONFIG_INT(TcRenderCursorSpec, tc_cursor_in_spec, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "自由视角旁观时渲染准星")
MACRO_CONFIG_INT(TcRenderCursorSpecAlpha, tc_cursor_in_spec_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "自由视角旁观时准星透明度")

// MACRO_CONFIG_INT(TcRenderNameplateSpec, tc_render_nameplate_spec, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "旁观时渲染名字板")

MACRO_CONFIG_INT(TcTinyTees, tc_tiny_tees, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "缩小 Tee")
MACRO_CONFIG_INT(TcTinyTeeSize, tc_indicator_tees_size, 100, 85, 115, CFGFLAG_CLIENT | CFGFLAG_SAVE, "小 Tee 尺寸")
MACRO_CONFIG_INT(TcTinyTeesOthers, tc_tiny_tees_others, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "同时缩小其他 Tee")

MACRO_CONFIG_INT(TcCursorScale, tc_cursor_scale, 100, 0, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "游戏内准星缩放百分比（50=一半，200=两倍）")

// Profiles
MACRO_CONFIG_INT(TcProfileSkin, tc_profile_skin, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "保存/加载配置方案时包含皮肤")
MACRO_CONFIG_INT(TcProfileName, tc_profile_name, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "保存/加载配置方案时包含名字")
MACRO_CONFIG_INT(TcProfileClan, tc_profile_clan, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "保存/加载配置方案时包含部落")
MACRO_CONFIG_INT(TcProfileFlag, tc_profile_flag, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "保存/加载配置方案时包含国旗")
MACRO_CONFIG_INT(TcProfileColors, tc_profile_colors, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "保存/加载配置方案时包含颜色")
MACRO_CONFIG_INT(TcProfileEmote, tc_profile_emote, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "保存/加载配置方案时包含表情")
MACRO_CONFIG_INT(TcProfileOverwriteClanWithEmpty, tc_profile_overwrite_clan_with_empty, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "即使配置方案里的部落名为空也覆盖当前部落")

// Rainbow
MACRO_CONFIG_INT(TcRainbowTees, tc_rainbow_tees, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用 Tee 彩虹渲染")
MACRO_CONFIG_INT(TcRainbowHook, tc_rainbow_hook, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用钩子彩虹渲染")
MACRO_CONFIG_INT(TcRainbowWeapon, tc_rainbow_weapon, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用武器彩虹渲染")

MACRO_CONFIG_INT(TcRainbowOthers, tc_rainbow_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "对其他玩家也启用 Tee 彩虹渲染")
MACRO_CONFIG_INT(TcRainbowMode, tc_rainbow_mode, 1, 1, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "彩虹模式（1=彩虹，2=脉冲，3=暗色，4=随机）")
MACRO_CONFIG_INT(TcRainbowSpeed, tc_rainbow_speed, 100, 0, 10000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "彩虹变化速度百分比（50=半速，200=双倍）")

// War List
MACRO_CONFIG_INT(TcWarList, tc_warlist, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用敌对列表视觉标记")
MACRO_CONFIG_INT(TcWarListShowClan, tc_warlist_show_clan_if_war, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "敌对关系时在名字板显示部落")
MACRO_CONFIG_INT(TcWarListReason, tc_warlist_reason, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示敌对原因")
MACRO_CONFIG_INT(TcWarListChat, tc_warlist_chat, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在聊天中显示敌对颜色")
MACRO_CONFIG_INT(TcWarListScoreboard, tc_warlist_scoreboard, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在计分板中显示敌对颜色")
MACRO_CONFIG_INT(TcWarListAllowDuplicates, tc_warlist_allow_duplicates, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "允许重复的敌对条目")
MACRO_CONFIG_INT(TcWarListSpectate, tc_warlist_spectate, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在旁观菜单中显示敌对颜色")

MACRO_CONFIG_INT(TcWarListIndicator, tc_warlist_indicator, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "用敌对列表驱动玩家指示器")
MACRO_CONFIG_INT(TcWarListIndicatorColors, tc_warlist_indicator_colors, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "使用敌对颜色而不是冻结颜色")
MACRO_CONFIG_INT(TcWarListIndicatorAll, tc_warlist_indicator_all, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示所有敌对分组")
MACRO_CONFIG_INT(TcWarListIndicatorEnemy, tc_warlist_indicator_enemy, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示敌对组玩家")
MACRO_CONFIG_INT(TcWarListIndicatorTeam, tc_warlist_indicator_team, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示队伍组玩家")

// Status Bar
MACRO_CONFIG_INT(TcStatusBar, tc_statusbar, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用状态栏")

MACRO_CONFIG_INT(TcStatusBar12HourClock, tc_statusbar_12_hour_clock, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "本地时间使用 12 小时制")
MACRO_CONFIG_INT(TcStatusBarLocalTimeSeconds, tc_statusbar_local_time_seconds, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示本地时间秒数")
MACRO_CONFIG_INT(TcStatusBarHeight, tc_statusbar_height, 8, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "状态栏高度")

MACRO_CONFIG_COL(TcStatusBarColor, tc_statusbar_color, 3221225472, CFGFLAG_CLIENT | CFGFLAG_SAVE, "状态栏背景颜色")
MACRO_CONFIG_COL(TcStatusBarTextColor, tc_statusbar_text_color, 4278190335, CFGFLAG_CLIENT | CFGFLAG_SAVE, "状态栏文字颜色")
MACRO_CONFIG_INT(TcStatusBarAlpha, tc_statusbar_alpha, 75, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "状态栏背景透明度")
MACRO_CONFIG_INT(TcStatusBarTextAlpha, tc_statusbar_text_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "状态栏文字透明度")

MACRO_CONFIG_INT(TcStatusBarLabels, tc_statusbar_labels, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在状态栏条目上显示标签")
MACRO_CONFIG_STR(TcStatusBarScheme, tc_statusbar_scheme, 128, "ac pf r", CFGFLAG_CLIENT | CFGFLAG_SAVE, "状态栏项目显示顺序")

// Trails
MACRO_CONFIG_INT(TcTeeTrail, tc_tee_trail, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用 Tee 轨迹")
MACRO_CONFIG_INT(TcTeeTrailOthers, tc_tee_trail_others, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "为其他玩家显示 Tee 轨迹")
MACRO_CONFIG_INT(TcTeeTrailWidth, tc_tee_trail_width, 15, 0, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "轨迹宽度")
MACRO_CONFIG_INT(TcTeeTrailLength, tc_tee_trail_length, 25, 5, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "轨迹长度")
MACRO_CONFIG_INT(TcTeeTrailAlpha, tc_tee_trail_alpha, 80, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "轨迹透明度")
MACRO_CONFIG_COL(TcTeeTrailColor, tc_tee_trail_color, 255, CFGFLAG_CLIENT | CFGFLAG_SAVE, "轨迹颜色")
MACRO_CONFIG_INT(TcTeeTrailTaper, tc_tee_trail_taper, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "轨迹末端渐细")
MACRO_CONFIG_INT(TcTeeTrailFade, tc_tee_trail_fade, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "沿轨迹长度逐渐淡出透明度")
MACRO_CONFIG_INT(TcTeeTrailColorMode, tc_tee_trail_color_mode, 1, 1, 5, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tee 轨迹颜色模式（1=纯色，2=当前 Tee 颜色，3=彩虹，4=按 Tee 速度着色，5=随机）")

// Chat Reply
MACRO_CONFIG_INT(TcAutoReplyMuted, tc_auto_reply_muted, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "自动回复已静音玩家的消息")
MACRO_CONFIG_STR(TcAutoReplyMutedMessage, tc_auto_reply_muted_message, 128, "I have muted you", CFGFLAG_CLIENT | CFGFLAG_SAVE, "回复静音玩家的消息")
MACRO_CONFIG_INT(TcAutoReplyMinimized, tc_auto_reply_minimized, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "游戏最小化时自动回复")
MACRO_CONFIG_STR(TcAutoReplyMinimizedMessage, tc_auto_reply_minimized_message, 128, "I am not tabbed in", CFGFLAG_CLIENT | CFGFLAG_SAVE, "游戏最小化时回复的消息")

// Voting
MACRO_CONFIG_INT(TcAutoVoteWhenFar, tc_auto_vote_when_far, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "离地图过远时自动投反对票")
MACRO_CONFIG_STR(TcAutoVoteWhenFarMessage, tc_auto_vote_when_far_message, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "自动投反对票时发送的消息，留空则不发送")
MACRO_CONFIG_INT(TcAutoVoteWhenFarTime, tc_auto_vote_when_far_time, 5, 0, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "触发自动投反对票前的等待时间")

// Font
MACRO_CONFIG_STR(TcCustomFont, tc_custom_font, 255, "DejaVu Sans", CFGFLAG_CLIENT | CFGFLAG_SAVE, "自定义字体")

// Bg Draw
MACRO_CONFIG_INT(TcBgDrawWidth, tc_bg_draw_width, 5, 1, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE, "背景描边宽度")
MACRO_CONFIG_INT(TcBgDrawFadeTime, tc_bg_draw_fade_time, 0, 0, 600, CFGFLAG_CLIENT | CFGFLAG_SAVE, "描边保留时间（0=永不消失）")
MACRO_CONFIG_INT(TcBgDrawMaxItems, tc_bg_draw_max_items, 128, 0, 2048, CFGFLAG_CLIENT | CFGFLAG_SAVE, "最多保留的描边条目数")
MACRO_CONFIG_COL(TcBgDrawColor, tc_bg_draw_color, 14024576, CFGFLAG_CLIENT | CFGFLAG_SAVE, "背景描边颜色")
MACRO_CONFIG_INT(TcBgDrawAutoSaveLoad, tc_bg_draw_auto_save_load, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "自动保存并加载背景描线")

// Animations
MACRO_CONFIG_INT(TcAnimateWheelTime, tc_animate_wheel_time, 350, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "表情轮和绑定轮动画时长（毫秒，0=无动画，1000=1 秒）")

// Pets
MACRO_CONFIG_INT(TcPetShow, tc_pet_show, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示宠物")
MACRO_CONFIG_STR(TcPetSkin, tc_pet_skin, 24, "twinbop", CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_INSENSITIVE, "宠物皮肤")
MACRO_CONFIG_INT(TcPetSize, tc_pet_size, 60, 10, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "宠物相对普通玩家的大小比例")
MACRO_CONFIG_INT(TcPetAlpha, tc_pet_alpha, 90, 10, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "宠物透明度（100=完全不透明，50=半透明）")

// Change name near finish
MACRO_CONFIG_INT(TcChangeNameNearFinish, tc_change_name_near_finish, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "接近终点时尝试改名")
MACRO_CONFIG_STR(TcFinishName, tc_finish_name, 16, "nameless tee", CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_INSENSITIVE, "接近终点时要改成的名字（启用 tc_change_name_near_finish 时生效）")

// Flags
MACRO_CONFIG_INT(TcTClientSettingsTabs, tc_tclient_settings_tabs, 0, 0, 65536, CFGFLAG_CLIENT | CFGFLAG_SAVE, "用于禁用设置标签页的位标志")

// Volleyball
MACRO_CONFIG_INT(TcVolleyBallBetterBall, tc_volleyball_better_ball, 1, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "让排球模式中的冻结玩家看起来更像排球（0=禁用，1=仅排球地图，2=始终）")
MACRO_CONFIG_STR(TcVolleyBallBetterBallSkin, tc_volleyball_better_ball_skin, 24, "beachball", CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_INSENSITIVE, "排球外观使用的玩家皮肤")

// Mod
MACRO_CONFIG_INT(TcShowPlayerHitBoxes, tc_show_player_hit_boxes, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示玩家命中框（1=仅预测，2=预测和未预测）")

// Legacy Chat Bubble Settings (tc_ prefix)
MACRO_CONFIG_INT(TcHideChatBubblesLegacy, tc_hide_chat_bubbles, 0, 0, 1, CFGFLAG_CLIENT, "旧版聊天气泡设置")
MACRO_CONFIG_INT(TcChatBubbleLegacy, tc_chat_bubble, 1, 0, 1, CFGFLAG_CLIENT, "旧版聊天气泡设置")
MACRO_CONFIG_INT(TcChatBubbleDurationLegacy, tc_chat_bubble_duration, 10, 1, 30, CFGFLAG_CLIENT, "旧版聊天气泡设置")
MACRO_CONFIG_INT(TcChatBubbleAlphaLegacy, tc_chat_bubble_alpha, 80, 0, 100, CFGFLAG_CLIENT, "旧版聊天气泡设置")
MACRO_CONFIG_INT(TcChatBubbleFontSizeLegacy, tc_chat_bubble_font_size, 20, 8, 32, CFGFLAG_CLIENT, "旧版聊天气泡设置")
MACRO_CONFIG_COL(TcChatBubbleBgColorLegacy, tc_chat_bubble_bg_color, 404232960, CFGFLAG_CLIENT | CFGFLAG_COLALPHA, "旧版聊天气泡设置")
MACRO_CONFIG_COL(TcChatBubbleTextColorLegacy, tc_chat_bubble_text_color, 4294967295, CFGFLAG_CLIENT, "旧版聊天气泡设置")
MACRO_CONFIG_INT(TcChatBubbleAnimationLegacy, tc_chat_bubble_animation, 0, 0, 2, CFGFLAG_CLIENT, "旧版聊天气泡设置")

MACRO_CONFIG_INT(TcModWeapon, tc_mod_weapon, 0, 0, 1, CFGFLAG_CLIENT, "指向某人并射击时执行命令（默认 kill，仅在远程控制台已鉴权时生效）")
MACRO_CONFIG_STR(TcModWeaponCommand, tc_mod_weapon_command, 256, "rcon kill_pl", CFGFLAG_CLIENT | CFGFLAG_SAVE, "tc_mod_weapon 执行的命令，目标 id 会附加在命令末尾")

// Run on join
MACRO_CONFIG_STR(TcExecuteOnConnect, tc_execute_on_connect, 100, "Run a console command before connect", CFGFLAG_CLIENT | CFGFLAG_SAVE, "连接服务器前执行的控制台命令")
MACRO_CONFIG_STR(TcExecuteOnJoin, tc_execute_on_join, 100, "Run a console command on join", CFGFLAG_CLIENT | CFGFLAG_SAVE, "进入服务器后执行的控制台命令")
MACRO_CONFIG_INT(TcExecuteOnJoinDelay, tc_execute_on_join_delay, 2, 7, 50000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "执行 tc_execute_on_join 前的延迟（毫秒）")

// Custom Communities
MACRO_CONFIG_STR(TcCustomCommunitiesUrl, tc_custom_communities_url, 256, "https://raw.githubusercontent.com/SollyBunny/ddnet-custom-communities/refs/heads/main/custom-communities-ddnet-info.json", CFGFLAG_CLIENT | CFGFLAG_SAVE, "自定义社区列表的获取 URL（必须是 https，留空则禁用）")

// Discord RPC
MACRO_CONFIG_INT(TcDiscordRPC, tc_discord_rpc, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用 Discord RPC（需要重启）") // broken

// Sidebar
MACRO_CONFIG_INT(TcSidebarEnable, tc_sidebar_enable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用侧边栏")
MACRO_CONFIG_INT(TcSidebarWidth, tc_sidebar_width, 200, 100, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "侧边栏宽度（像素）")
MACRO_CONFIG_INT(TcSidebarOpacity, tc_sidebar_opacity, 75, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "侧边栏背景不透明度（0-100）")
MACRO_CONFIG_INT(TcSidebarPosition, tc_sidebar_position, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "侧边栏位置（0=左，1=右，2=自定义）")
MACRO_CONFIG_INT(TcSidebarShowInGame, tc_sidebar_show_in_game, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "游戏中显示侧边栏")
MACRO_CONFIG_INT(TcSidebarShowInMenu, tc_sidebar_show_in_menu, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在菜单中显示侧边栏")
MACRO_CONFIG_INT(TcSidebarShowInSpec, tc_sidebar_show_in_spec, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "旁观模式下显示侧边栏")

MACRO_CONFIG_INT(TcSidebarShowFPS, tc_sidebar_show_fps, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在侧边栏中显示 FPS")
MACRO_CONFIG_INT(TcSidebarShowPing, tc_sidebar_show_ping, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在侧边栏中显示 ping")
MACRO_CONFIG_INT(TcSidebarShowSpeed, tc_sidebar_show_speed, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在侧边栏中显示玩家速度")
MACRO_CONFIG_INT(TcSidebarShowPosition, tc_sidebar_show_position, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在侧边栏中显示玩家位置")
MACRO_CONFIG_INT(TcSidebarShowTime, tc_sidebar_show_time, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在侧边栏中显示本地时间")
MACRO_CONFIG_INT(TcSidebarShowServerInfo, tc_sidebar_show_server_info, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在侧边栏中显示服务器信息")

// UI Settings
MACRO_CONFIG_INT(TcUiShowTClient, tc_ui_show_tclient, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示 TClient 配置变量")
MACRO_CONFIG_INT(TcUiShowDDNet, tc_ui_show_ddnet, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示 DDNet 配置变量")
MACRO_CONFIG_INT(TcUiCompactList, tc_ui_compact_list, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "对配置变量使用紧凑列表视图")
MACRO_CONFIG_INT(TcUiOnlyModified, tc_ui_only_modified, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "只显示修改的配置变量")
MACRO_CONFIG_INT(QmPerfDebug, qm_perf_debug, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用主线程与渲染阶段性能调试日志")
MACRO_CONFIG_INT(QmPerfDebugThresholdMs, qm_perf_debug_threshold_ms, 20, 1, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "性能调试日志阈值（毫秒）")
MACRO_CONFIG_INT(QmUiRuntimeV2Debug, qm_ui_runtime_v2_debug, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用 UI 运行时 v2 调试日志")

// Scoreboard
MACRO_CONFIG_INT(ClScoreboardPoints, cl_scoreboard_points, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在记分牌中显示分数列（从 DDNet API 获取）")
MACRO_CONFIG_INT(ClScoreboardSortMode, cl_scoreboard_sort_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "记分牌排序模式（0=分数，1=分）")
MACRO_CONFIG_INT(QmScoreboardAnimOptim, qm_scoreboard_anim_optim, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "计分板动画优化")
MACRO_CONFIG_INT(QmChatFadeOutAnim, qm_chat_fade_out_anim, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "聊天框淡出动画")
MACRO_CONFIG_INT(QmEmoticonSelectAnim, qm_emoticon_select_anim, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "表情选择动画")

// Rainbow Name / 彩虹名字
MACRO_CONFIG_INT(QmRainbowName, qm_rainbow_name, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用自己名字的彩虹色渲染")
MACRO_CONFIG_INT(QmNameplateCoordX, qm_nameplate_coord_x, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "名字牌显示坐标X")
MACRO_CONFIG_INT(QmNameplateCoordXAlignHint, qm_nameplate_coord_x_align_hint, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "名字牌X对齐提示")
MACRO_CONFIG_INT(QmNameplateCoordXAlignHintStrict, qm_nameplate_coord_x_align_hint_strict, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "名字牌X对齐提示严格模式")
MACRO_CONFIG_INT(QmNameplateCoordY, qm_nameplate_coord_y, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "名字牌显示坐标Y")
MACRO_CONFIG_INT(QmNameplateCoordsOwn, qm_nameplate_coords_own, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "名字牌显示自己坐标")
MACRO_CONFIG_INT(QmNameplateCoords, qm_nameplate_coords, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "名字牌显示他人坐标")

// Enhanced Laser Effects (Glow + Pulse) / 增强激光效果（辉光+脉冲）
MACRO_CONFIG_INT(QmLaserEnhanced, qm_laser_enhanced, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用增强激光特效（辉光+脉冲动画）")
MACRO_CONFIG_INT(QmLaserGlowIntensity, qm_laser_glow_intensity, 30, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "激光辉光强度 (0-100)")
MACRO_CONFIG_INT(QmLaserPulseSpeed, qm_laser_pulse_speed, 100, 10, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "脉冲动画速度 ( 百分比, 100=正常)")
MACRO_CONFIG_INT(QmLaserPulseAmplitude, qm_laser_pulse_amplitude, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "脉冲振幅 (0-100)")
MACRO_CONFIG_INT(QmLaserSize, qm_laser_size, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "激光大小/粗细 (百分比, 100=默认)")
MACRO_CONFIG_INT(QmLaserRoundCaps, qm_laser_round_caps, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "激光圆角端点 (0=方角, 1=圆角)")
MACRO_CONFIG_INT(QmLaserAlpha, qm_laser_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "激光半透明度 (0=完全透明, 100=不透明)")

// Collision Hitbox Visualization / 碰撞体积可视化
MACRO_CONFIG_INT(QmShowCollisionHitbox, qm_show_collision_hitbox, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示碰撞体积边框 (Show collision hitbox outlines)")
MACRO_CONFIG_COL(QmCollisionHitboxColorFreeze, qm_collision_hitbox_color_freeze, 16711935, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Freeze碰撞边框颜色 (Freeze collision box color)")
MACRO_CONFIG_INT(QmCollisionHitboxAlpha, qm_collision_hitbox_alpha, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "碰撞体积线条透明度 (Collision hitbox line alpha)")

// Entity overlay
MACRO_CONFIG_INT(QmEntityOverlayDeathAlpha, qm_entity_overlay_death_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "覆盖死亡图块的实体 alpha (0-100)")
MACRO_CONFIG_INT(QmEntityOverlayFreezeAlpha, qm_entity_overlay_freeze_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "覆盖冻结图块的实体 alpha (0-100)")
MACRO_CONFIG_INT(QmEntityOverlayUnfreezeAlpha, qm_entity_overlay_unfreeze_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "覆盖实体 alpha 以解冻图块 (0-100)")
MACRO_CONFIG_INT(QmEntityOverlayDeepFreezeAlpha, qm_entity_overlay_deep_freeze_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "深度冻结图块的叠加实体 alpha (0-100)")
MACRO_CONFIG_INT(QmEntityOverlayDeepUnfreezeAlpha, qm_entity_overlay_deep_unfreeze_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "深度解冻图块的叠加实体 alpha (0-100)")
MACRO_CONFIG_INT(QmEntityOverlayTeleAlpha, qm_entity_overlay_tele_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Teletile 的覆盖实体 alpha (0-100)")
MACRO_CONFIG_INT(QmEntityOverlayTeleCheckpointAlpha, qm_entity_overlay_tele_checkpoint_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "CP点边界的覆盖实体 alpha (0-100)")
MACRO_CONFIG_INT(QmEntityOverlaySwitchAlpha, qm_entity_overlay_switch_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "开关图块的覆盖实体 alpha (0-100)")

// Q1menG Client Recognition / Q1menG客户端识别
MACRO_CONFIG_INT(QmClientMarkTrail, qm_client_mark_trail, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "远程粒子：通过中心服同步并渲染其他玩家（需对方开启本地+远程）")
MACRO_CONFIG_INT(QmClientShowBadge, qm_client_show_badge, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示Qm标识：通过中心服识别并在名字板/计分板标记QmClient用户")

// Legacy QiaFen Compatibility / 旧恰分兼容配置
MACRO_CONFIG_INT(QmQiaFenEnabled, qm_qiafen_enabled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "旧恰分开关（仅兼容迁移）")
MACRO_CONFIG_INT(QmQiaFenUseDummy, qm_qiafen_use_dummy, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "旧恰分 Dummy 开关（仅兼容迁移）")
MACRO_CONFIG_STR(QmQiaFenRules, qm_qiafen_rules, 2048, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "旧恰分规则（仅兼容迁移）")
MACRO_CONFIG_STR(QmQiaFenKeywords, qm_qiafen_keywords, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "旧恰分关键词（仅兼容迁移）")
MACRO_CONFIG_INT(QmKeywordReplyEnabled, qm_keyword_reply_enabled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用关键词回复")
MACRO_CONFIG_INT(QmKeywordReplyUseDummy, qm_keyword_reply_use_dummy, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "关键词回复使用Dummy")
MACRO_CONFIG_INT(QmKeywordReplyAutoRename, qm_keyword_reply_auto_rename, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "旧全局关键词改名开关（仅兼容迁移）")
MACRO_CONFIG_STR(QmKeywordReplyRules, qm_keyword_reply_rules, 4096, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "关键词回复规则（每行: [rename] [regex] 关键词=>回复）")
MACRO_CONFIG_INT(QmAutoReplyCooldown, qm_auto_reply_cooldown, 3, 0, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "自动回复冷却时间（秒）")

// Pie Menu / 饼菜单
MACRO_CONFIG_INT(QmPieMenuEnabled, qm_pie_menu_enabled, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用饼菜单 (Enable pie menu for player interactions)")
MACRO_CONFIG_INT(QmPieMenuMaxDistance, qm_pie_menu_max_distance, 400, 100, 2000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "玩家检测最大距离 (Maximum detection distance for nearest player)")
MACRO_CONFIG_INT(QmPieMenuScale, qm_pie_menu_scale, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "UI大小百分比 (UI scale percentage)")
MACRO_CONFIG_INT(QmPieMenuOpacity, qm_pie_menu_opacity, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "菜单不透明度 (Menu opacity 0-100)")
MACRO_CONFIG_STR(QmPieMenuRenameQueue, qm_pie_menu_rename_queue, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "饼菜单改名名单（使用|分隔，如：璇梦1|璇梦2）")
MACRO_CONFIG_INT(QmPieMenuColorFriend, qm_pie_menu_color_friend, 0xE64D66BF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "好友选项颜色")
MACRO_CONFIG_INT(QmPieMenuColorWhisper, qm_pie_menu_color_whisper, 0x8059B3BF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "私聊选项颜色")
MACRO_CONFIG_INT(QmPieMenuColorMention, qm_pie_menu_color_mention, 0xD98033BF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "提及选项颜色")
MACRO_CONFIG_INT(QmPieMenuColorCopySkin, qm_pie_menu_color_copy_skin, 0x408CCCBF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "复制皮肤选项颜色")
MACRO_CONFIG_INT(QmPieMenuColorSwap, qm_pie_menu_color_swap, 0xCC4D4DBF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "交换选项颜色")
MACRO_CONFIG_INT(QmPieMenuColorSpectate, qm_pie_menu_color_spectate, 0x738C99BF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "观战选项颜色")

// Repeat Message / 复读功能
MACRO_CONFIG_INT(QmRepeatEnabled, qm_repeat_enabled, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用复读功能 (Enable repeat last message)")
MACRO_CONFIG_INT(QmRepeatKey, qm_repeat_key, 278, 0, 512, CFGFLAG_CLIENT | CFGFLAG_SAVE, "复读快捷键 (Repeat key, default: Home=278)")
MACRO_CONFIG_INT(QmSayNoPop, qm_say_nopop, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "输入时不显示打字表情 (Hide typing emoticon while chatting)")
MACRO_CONFIG_INT(QmHammerSwapSkin, qm_hammer_swap_skin, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "锤人换皮肤 (Copy target skin on hammer hit)")
MACRO_CONFIG_INT(QmRandomEmoteOnHit, qm_random_emote_on_hit, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "被锤/榴弹击中时随机表情 (Random emote on hammer/grenade hit)")
MACRO_CONFIG_INT(QmWeaponTrajectory, qm_weapon_trajectory, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用武器弹道辅助线 (Enable weapon trajectory)")
MACRO_CONFIG_INT(QmDeepflyMode, qm_deepfly_mode, 0, 0, 3, CFGFLAG_CLIENT, "Deepfly模式（0=正常，1=DF，2=HDF，3=自定义）")

// Auto Unspec on Unfreeze / 解冻自动取消旁观
MACRO_CONFIG_INT(QmAutoUnspecOnUnfreeze, qm_auto_unspec_on_unfreeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "被解冻时自动取消旁观 (Auto exit spectator mode when unfrozen)")

// HJ大佬辅助 - 自动切换到解冻的Tee
MACRO_CONFIG_INT(QmAutoSwitchOnUnfreeze, qm_auto_switch_on_unfreeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "本体和dummy都freeze时，自动切换到先解冻的那个 (Auto switch to unfrozen tee when both are frozen)")
// HJ大佬辅助 - 解冻后自动关闭聊天
MACRO_CONFIG_INT(QmAutoCloseChatOnUnfreeze, qm_auto_close_chat_on_unfreeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "解冻后自动关闭聊天 (Auto close chat input when unfrozen)")

// Input Overlay
MACRO_CONFIG_INT(QmInputOverlay, qm_input_overlay, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示输入覆盖")
MACRO_CONFIG_INT(QmInputOverlayScale, qm_input_overlay_scale, 20, 1, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "输入叠加比例（百分比）")
MACRO_CONFIG_INT(QmInputOverlayOpacity, qm_input_overlay_opacity, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "输入叠加不透明度（百分比）")
MACRO_CONFIG_INT(QmInputOverlayPosX, qm_input_overlay_pos_x, 71, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "输入叠加 X 位置（百分比）")
MACRO_CONFIG_INT(QmInputOverlayPosY, qm_input_overlay_pos_y, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "输入叠加 Y 位置（百分比）")

// Voice
MACRO_CONFIG_INT(QmVoiceEnable, qm_voice_enable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用语音聊天")
MACRO_CONFIG_INT(QmVoiceProtocolVersion, qm_voice_protocol_version, 3, 1, 255, CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音协议版本")
MACRO_CONFIG_STR(QmVoiceServer, qm_voice_server, 128, "42.194.185.210:9987", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音服务器地址 host:port")
MACRO_CONFIG_STR(QmVoiceAudioBackend, qm_voice_audio_backend, 64, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音音频后端（SDL 驱动名，空为自动）")
MACRO_CONFIG_STR(QmVoiceInputDevice, qm_voice_input_device, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音输入设备（空为默认）")
MACRO_CONFIG_STR(QmVoiceOutputDevice, qm_voice_output_device, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音输出设备（空为默认）")
MACRO_CONFIG_INT(QmVoiceStereo, qm_voice_stereo, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音立体声输出")
MACRO_CONFIG_INT(QmVoiceStereoWidth, qm_voice_stereo_width, 100, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音立体声宽度（百分比）")
MACRO_CONFIG_STR(QmVoiceToken, qm_voice_token, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音房间 Token（可选）")
MACRO_CONFIG_INT(QmVoiceGroupMode, qm_voice_group_mode, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音分组模式")
MACRO_CONFIG_INT(QmVoiceFilterEnable, qm_voice_filter_enable, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用语音滤波（高通/压缩/限幅）")
MACRO_CONFIG_INT(QmVoiceNoiseSuppressEnable, qm_voice_noise_suppress_enable, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用噪声抑制")
MACRO_CONFIG_INT(QmVoiceNoiseSuppressStrength, qm_voice_noise_suppress_strength, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "噪声抑制强度（百分比）")
MACRO_CONFIG_INT(QmVoiceCompThreshold, qm_voice_comp_threshold, 20, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "压缩器阈值（百分比）")
MACRO_CONFIG_INT(QmVoiceCompRatio, qm_voice_comp_ratio, 25, 10, 80, CFGFLAG_CLIENT | CFGFLAG_SAVE, "压缩器比率（x10）")
MACRO_CONFIG_INT(QmVoiceCompAttackMs, qm_voice_comp_attack_ms, 20, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "压缩器攻击时间（毫秒）")
MACRO_CONFIG_INT(QmVoiceCompReleaseMs, qm_voice_comp_release_ms, 200, 10, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "压缩器释放时间（毫秒）")
MACRO_CONFIG_INT(QmVoiceCompMakeup, qm_voice_comp_makeup, 160, 0, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "压缩器补偿增益（百分比）")
MACRO_CONFIG_INT(QmVoiceLimiter, qm_voice_limiter, 50, 10, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "限幅器阈值（百分比）")
MACRO_CONFIG_INT(QmVoiceRadius, qm_voice_radius, 50, 1, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音距离半径（tile）")
MACRO_CONFIG_INT(QmVoiceVolume, qm_voice_volume, 100, 0, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音播放音量（百分比）")
MACRO_CONFIG_INT(QmVoiceMicVolume, qm_voice_mic_volume, 100, 0, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "麦克风音量（百分比）")
MACRO_CONFIG_INT(QmVoiceMicMute, qm_voice_mic_mute, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "静音麦克风")
MACRO_CONFIG_INT(QmVoiceTestMode, qm_voice_test_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音测试模式（0=关 1=本地 2=服务器回环）")
MACRO_CONFIG_INT(QmVoiceVadEnable, qm_voice_vad_enable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用语音激活（VAD）")
MACRO_CONFIG_INT(QmVoiceVadThreshold, qm_voice_vad_threshold, 8, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "VAD 阈值（百分比）")
MACRO_CONFIG_INT(QmVoiceVadReleaseDelayMs, qm_voice_vad_release_delay_ms, 150, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "VAD 释放延迟（毫秒）")
MACRO_CONFIG_INT(QmVoiceIgnoreDistance, qm_voice_ignore_distance, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "忽略语音距离衰减")
MACRO_CONFIG_INT(QmVoiceGroupGlobal, qm_voice_group_global, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "同组全图收听")
MACRO_CONFIG_INT(QmVoiceVisibilityMode, qm_voice_visibility_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "可见性过滤模式")
MACRO_CONFIG_INT(QmVoiceListMode, qm_voice_list_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "名单过滤模式")
MACRO_CONFIG_STR(QmVoiceWhitelist, qm_voice_whitelist, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音白名单（逗号分隔）")
MACRO_CONFIG_STR(QmVoiceBlacklist, qm_voice_blacklist, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音黑名单（逗号分隔）")
MACRO_CONFIG_STR(QmVoiceMute, qm_voice_mute, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音静音名单（逗号分隔）")
MACRO_CONFIG_INT(QmVoiceHearVad, qm_voice_hear_vad, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "接收 VAD 讲话者")
MACRO_CONFIG_STR(QmVoiceVadAllow, qm_voice_vad_allow, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "VAD 允许名单（逗号分隔）")
MACRO_CONFIG_STR(QmVoiceNameVolumes, qm_voice_name_volumes, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "按名字单独音量（name=percent）")
MACRO_CONFIG_INT(QmVoiceShowIndicator, qm_voice_show_indicator, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示语音指示图标")
MACRO_CONFIG_INT(QmVoiceIndicatorAboveSelf, qm_voice_indicator_above_self, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示自己头顶语音指示")
MACRO_CONFIG_INT(QmVoiceShowPing, qm_voice_show_ping, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示语音延迟")
MACRO_CONFIG_INT(QmVoiceDebug, qm_voice_debug, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "输出语音调试日志")
MACRO_CONFIG_INT(QmVoiceOffNonActive, qm_voice_off_nonactive, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "窗口失焦时暂停语音")
MACRO_CONFIG_INT(QmVoicePttReleaseDelayMs, qm_voice_ptt_release_delay_ms, 0, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "PTT 释放延迟（毫秒）")
MACRO_CONFIG_INT(QmVoiceHearOnSpecPos, qm_voice_hear_on_spec_pos, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "旁观时按镜头中心收听")
MACRO_CONFIG_INT(QmVoiceHearPeoplesInSpectate, qm_voice_hear_peoples_in_spectate, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "接收旁观/非活跃玩家语音")

// Streamer Mode
MACRO_CONFIG_INT(QmStreamerHideNames, qm_streamer_hide_names, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "隐藏非好友姓名/部落并显示客户端 ID")
MACRO_CONFIG_INT(QmStreamerHideSkins, qm_streamer_hide_skins, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "非好友使用默认皮肤")
MACRO_CONFIG_INT(QmStreamerScoreboardDefaultFlags, qm_streamer_scoreboard_default_flags, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在记分牌中显示默认国旗")

// Friend Online Notifications / 好友上线提醒
MACRO_CONFIG_INT(QmFriendOnlineNotify, qm_friend_online_notify, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "好友上线提醒")
MACRO_CONFIG_INT(QmFriendOnlineAutoRefresh, qm_friend_online_auto_refresh, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "好友提醒自动刷新服务器列表")
MACRO_CONFIG_INT(QmFriendOnlineRefreshSeconds, qm_friend_online_refresh_seconds, 30, 5, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "好友提醒刷新间隔(秒)")
MACRO_CONFIG_INT(QmFriendEnterAutoGreet, qm_friend_enter_auto_greet, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "好友进图自动打招呼")
MACRO_CONFIG_INT(QmFriendEnterBroadcast, qm_friend_enter_broadcast, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "大字显示好友进服")
MACRO_CONFIG_STR(QmFriendEnterBroadcastText, qm_friend_enter_broadcast_text, 128, "%s好友进入本服", CFGFLAG_CLIENT | CFGFLAG_SAVE, "好友进服大字提示文本（使用%s表示好友名）")
MACRO_CONFIG_STR(QmFriendEnterGreetText, qm_friend_enter_greet_text, 128, "你好啊!", CFGFLAG_CLIENT | CFGFLAG_SAVE, "好友进图自动打招呼文本")

// Block Words / 屏蔽词
MACRO_CONFIG_INT(QmBlockWordsEnabled, qm_block_words_enabled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用屏蔽词列表")
MACRO_CONFIG_INT(QmBlockWordsShowConsole, qm_block_words_show_console, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "控制台显示被屏蔽词")
MACRO_CONFIG_COL(QmBlockWordsConsoleColor, qm_block_words_console_color, 0xFFFFFFFF, CFGFLAG_CLIENT | CFGFLAG_SAVE, "被屏蔽词控制台颜色")
MACRO_CONFIG_INT(QmBlockWordsMultiReplace, qm_block_words_multi_replace, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "按屏蔽词长度多字符替换")
MACRO_CONFIG_INT(QmBlockWordsMode, qm_block_words_mode, 2, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "替换方式 0=Regex 1=Full 2=Both")
MACRO_CONFIG_STR(QmBlockWordsReplacementChar, qm_block_words_replacement_char, 8, "*", CFGFLAG_CLIENT | CFGFLAG_SAVE, "屏蔽词替换字符")
MACRO_CONFIG_STR(QmBlockWordsList, qm_block_words_list, 1024, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "屏蔽词列表（用,分隔）")
MACRO_CONFIG_STR(QmSidebarCardOrder, qm_sidebar_card_order, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "栖梦侧栏模块排序")
MACRO_CONFIG_STR(QmSidebarCardCollapsed, qm_sidebar_card_collapsed, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "栖梦侧栏模块折叠状态")
MACRO_CONFIG_STR(QmSidebarCardUsage, qm_sidebar_card_usage, 1024, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "栖梦侧栏模块使用频率")
