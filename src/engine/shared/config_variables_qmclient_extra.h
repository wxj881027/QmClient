// This file can be included several times.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(Tcme, ScriptName, Def, Min, Max, Save, Desc) ;
#define MACRO_CONFIG_COL(Tcme, ScriptName, Def, Save, Desc) ;
#define MACRO_CONFIG_STR(Tcme, ScriptName, Len, Def, Save, Desc) ;
#endif

// QmClient specific variables

// Nameplate
MACRO_CONFIG_STR(QmNameplateRowOrder, qm_nameplate_row_order, 64, "keys,coords,hook,clan,name", CFGFLAG_CLIENT | CFGFLAG_SAVE, "名字板行顺序（逗号分隔：keys,coords,hook,clan,name）")

// Gores
MACRO_CONFIG_INT(QmGores, qm_gores, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用 Gores 锤枪自动切换")
MACRO_CONFIG_INT(QmGoresDisableIfWeapons, qm_gores_disable_if_weapons, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "拿到额外武器时停用 Gores 自动切换")
MACRO_CONFIG_INT(QmGoresAutoEnable, qm_gores_auto_enable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在 Gores 游戏模式中自动启用 Gores 自动切换")

// Player Stats HUD
MACRO_CONFIG_INT(QmPlayerStatsHud, qm_player_stats_hud, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用玩家统计 HUD")
MACRO_CONFIG_INT(QmPlayerStatsMapProgress, qm_player_stats_map_progress, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示地图进度条（内测中）")
MACRO_CONFIG_INT(QmPlayerStatsMapProgressStyle, qm_player_stats_map_progress_style, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "地图进度条样式（0=底部横条，1=HUD 内嵌）")
MACRO_CONFIG_COL(QmPlayerStatsMapProgressColor, qm_player_stats_map_progress_color, 0xFF24C764, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "地图进度条颜色")
MACRO_CONFIG_INT(QmPlayerStatsMapProgressWidth, qm_player_stats_map_progress_width, 28, 10, 80, CFGFLAG_CLIENT | CFGFLAG_SAVE, "地图进度条宽度（占屏幕宽度百分比）")
MACRO_CONFIG_INT(QmPlayerStatsMapProgressHeight, qm_player_stats_map_progress_height, 10, 6, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "地图进度条高度")
MACRO_CONFIG_INT(QmPlayerStatsMapProgressPosX, qm_player_stats_map_progress_pos_x, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "地图进度条水平位置（中心点，占屏幕宽度百分比）")
MACRO_CONFIG_INT(QmPlayerStatsMapProgressPosY, qm_player_stats_map_progress_pos_y, 97, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "地图进度条垂直位置（顶边，占屏幕高度百分比）")
MACRO_CONFIG_INT(QmPlayerStatsMapProgressDbgRoute, qm_player_stats_map_progress_dbg_route, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示地图进度调试点路线")
MACRO_CONFIG_INT(QmPlayerStatsResetOnJoin, qm_player_stats_reset_on_join, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "进入服务器时重置统计（0=累计统计，1=每次进服重置）")
// HUD Dynamic Island
MACRO_CONFIG_INT(QmHudIslandUseOriginalStyle, qm_hud_island_use_original_style, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD 灵动岛使用原版样式")
MACRO_CONFIG_COL(QmHudIslandBgColor, qm_hud_island_bg_color, 0x9C460E, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD 灵动岛背景颜色")
MACRO_CONFIG_INT(QmHudIslandBgOpacity, qm_hud_island_bg_opacity, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD 灵动岛背景透明度")
MACRO_CONFIG_STR(QmHudEditorLayout, qm_hud_editor_layout, 2048, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD 编辑器布局（内部使用）")

// Camera / View
MACRO_CONFIG_INT(QmCameraDrift, qm_camera_drift, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用相机漂移效果，让镜头根据移动速度产生轻微拖拽")
MACRO_CONFIG_INT(QmCameraDriftAmount, qm_camera_drift_amount, 50, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "相机漂移强度（0-200）")
MACRO_CONFIG_INT(QmCameraDriftSmoothness, qm_camera_drift_smoothness, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "相机漂移平滑度（0=瞬时，100=最平滑）")
MACRO_CONFIG_INT(QmCameraDriftReverse, qm_camera_drift_reverse, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "反转相机漂移方向")
MACRO_CONFIG_INT(QmDynamicFov, qm_dynamic_fov, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用动态视野，移动越快视野越宽")
MACRO_CONFIG_INT(QmDynamicFovAmount, qm_dynamic_fov_amount, 50, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "动态视野强度（0-200）")
MACRO_CONFIG_INT(QmDynamicFovSmoothness, qm_dynamic_fov_smoothness, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "动态视野平滑度（0=瞬时，100=最平滑）")
MACRO_CONFIG_INT(QmAspectPreset, qm_aspect_preset, 0, 0, 6, CFGFLAG_CLIENT | CFGFLAG_SAVE, "画面纵横比预设（0=关闭，1=5:4，2=4:3，3=3:2，4=16:9，5=21:9，6=自定义）")
MACRO_CONFIG_INT(QmAspectRatio, qm_aspect_ratio, 178, 100, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "自定义纵横比，按 x100 存储（例如 178=16:9，233=21:9）")

// Misc visual
MACRO_CONFIG_INT(QmJellyTee, qm_jelly_tee, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用 Q 弹 Tee 形变")
MACRO_CONFIG_INT(QmJellyTeeOthers, qm_jelly_tee_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "将 Q 弹 Tee 形变应用到其他玩家")
MACRO_CONFIG_INT(QmJellyTeeStrength, qm_jelly_tee_strength, 500, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Q 弹 Tee 形变强度")
MACRO_CONFIG_INT(QmJellyTeeDuration, qm_jelly_tee_duration, 30, 1, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Q 弹 Tee 形变持续时间")

// Skin queue
MACRO_CONFIG_INT(QmSkinQueueInterval, qm_skin_queue_interval, 60, 5, 120, CFGFLAG_CLIENT | CFGFLAG_SAVE, "皮肤队列切换间隔（秒）")
MACRO_CONFIG_INT(QmSkinQueueLength, qm_skin_queue_length, 20, 0, 1024, CFGFLAG_CLIENT | CFGFLAG_SAVE, "皮肤队列最大长度")
MACRO_CONFIG_INT(QmSkinQueueIndex, qm_skin_queue_index, 0, 0, 1024, CFGFLAG_CLIENT | CFGFLAG_SAVE, "皮肤队列当前位置")
MACRO_CONFIG_INT(QmSkinQueueRotateMap, qm_skin_queue_rotate_map, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "自动获取全图玩家皮肤并作为轮换队列")
MACRO_CONFIG_INT(QmDummySkinQueueInterval, qm_dummy_skin_queue_interval, 60, 5, 120, CFGFLAG_CLIENT | CFGFLAG_SAVE, "分身皮肤队列切换间隔（秒）")
MACRO_CONFIG_INT(QmDummySkinQueueLength, qm_dummy_skin_queue_length, 20, 0, 1024, CFGFLAG_CLIENT | CFGFLAG_SAVE, "分身皮肤队列最大长度")
MACRO_CONFIG_INT(QmDummySkinQueueIndex, qm_dummy_skin_queue_index, 0, 0, 1024, CFGFLAG_CLIENT | CFGFLAG_SAVE, "分身皮肤队列当前位置")
MACRO_CONFIG_INT(QmDummySkinQueueRotateMap, qm_dummy_skin_queue_rotate_map, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "自动获取全图玩家皮肤并作为分身轮换队列")

// Foot Particles
MACRO_CONFIG_INT(QmcFootParticles, qmc_foot_particles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "本地粒子：显示自己 Tee 身后掉落的粒子（如冻结雪花）")

MACRO_CONFIG_INT(QmPieMenuKey, qm_pie_menu_key, 25, 0, 511, CFGFLAG_CLIENT | CFGFLAG_SAVE, "饼菜单激活键（SDL 扫描码，默认 V）")

// Chat Bubble Settings
MACRO_CONFIG_INT(QmChatSaveDraft, qm_chat_save_draft, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "关闭聊天时保留未发送内容")
MACRO_CONFIG_INT(QmHideChatBubbles, qm_hide_chat_bubbles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "隐藏自己的聊天气泡（仅在远程控制台已鉴权时生效）")
MACRO_CONFIG_INT(QmChatBubble, qm_chat_bubble, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在玩家头顶显示聊天气泡")
MACRO_CONFIG_INT(QmChatBubbleDuration, qm_chat_bubble_duration, 10, 1, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "聊天气泡显示时长（秒）")
MACRO_CONFIG_INT(QmChatBubbleAlpha, qm_chat_bubble_alpha, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "聊天气泡透明度（0-100）")
MACRO_CONFIG_INT(QmChatBubbleFontSize, qm_chat_bubble_font_size, 20, 8, 32, CFGFLAG_CLIENT | CFGFLAG_SAVE, "聊天气泡字体大小")
MACRO_CONFIG_COL(QmChatBubbleBgColor, qm_chat_bubble_bg_color, 404232960, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "聊天气泡背景颜色")
MACRO_CONFIG_COL(QmChatBubbleTextColor, qm_chat_bubble_text_color, 4294967295, CFGFLAG_CLIENT | CFGFLAG_SAVE, "聊天气泡文字颜色")
MACRO_CONFIG_INT(QmChatBubbleAnimation, qm_chat_bubble_animation, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "聊天气泡消失动画（0=淡出，1=缩小，2=上滑）")

// UI Settings
MACRO_CONFIG_INT(QmUiRuntimeV2Debug, qm_ui_runtime_v2_debug, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用 UI 运行时 v2 调试日志")

// Scoreboard
MACRO_CONFIG_INT(ClScoreboardPoints, cl_scoreboard_points, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在计分板显示分数")
MACRO_CONFIG_INT(ClScoreboardSortMode, cl_scoreboard_sort_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "计分板按分数排序")

// Rainbow Name
MACRO_CONFIG_INT(QmRainbowName, qm_rainbow_name, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用自己名字的彩虹渲染")
MACRO_CONFIG_INT(QmNameplateCoordX, qm_nameplate_coord_x, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "名字板显示 X 坐标")
MACRO_CONFIG_INT(QmNameplateCoordY, qm_nameplate_coord_y, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "名字板显示 Y 坐标")
MACRO_CONFIG_INT(QmNameplateCoordsOwn, qm_nameplate_coords_own, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "名字板显示自己的坐标")
MACRO_CONFIG_INT(QmNameplateCoords, qm_nameplate_coords, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "名字板显示其他玩家坐标")

// Enhanced Laser Effects
MACRO_CONFIG_INT(QmLaserEnhanced, qm_laser_enhanced, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用增强激光特效（辉光+脉冲动画）")
MACRO_CONFIG_INT(QmLaserGlowIntensity, qm_laser_glow_intensity, 30, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "激光辉光强度（0-100）")
MACRO_CONFIG_INT(QmLaserPulseSpeed, qm_laser_pulse_speed, 100, 10, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "脉冲动画速度（百分比，100=正常）")
MACRO_CONFIG_INT(QmLaserPulseAmplitude, qm_laser_pulse_amplitude, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "脉冲振幅（0-100）")
MACRO_CONFIG_INT(QmLaserSize, qm_laser_size, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "激光粗细（百分比，100=默认）")
MACRO_CONFIG_INT(QmLaserRoundCaps, qm_laser_round_caps, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "激光端点样式（0=方角，1=圆角）")
MACRO_CONFIG_INT(QmLaserAlpha, qm_laser_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "激光透明度（0=完全透明，100=完全不透明）")

// Collision Hitbox Visualization
MACRO_CONFIG_INT(QmShowCollisionHitbox, qm_show_collision_hitbox, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示碰撞框轮廓")
MACRO_CONFIG_COL(QmCollisionHitboxColorFreeze, qm_collision_hitbox_color_freeze, 16711935, CFGFLAG_CLIENT | CFGFLAG_SAVE, "冻结碰撞框颜色")
MACRO_CONFIG_INT(QmCollisionHitboxAlpha, qm_collision_hitbox_alpha, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "碰撞框线条透明度")

// Entity overlay
MACRO_CONFIG_INT(QmEntityOverlayDeathAlpha, qm_entity_overlay_death_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "死亡图块实体覆盖透明度（0-100）")
MACRO_CONFIG_INT(QmEntityOverlayFreezeAlpha, qm_entity_overlay_freeze_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "冻结图块实体覆盖透明度（0-100）")
MACRO_CONFIG_INT(QmEntityOverlayUnfreezeAlpha, qm_entity_overlay_unfreeze_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "解冻图块实体覆盖透明度（0-100）")
MACRO_CONFIG_INT(QmEntityOverlayDeepFreezeAlpha, qm_entity_overlay_deep_freeze_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "深冻图块实体覆盖透明度（0-100）")
MACRO_CONFIG_INT(QmEntityOverlayDeepUnfreezeAlpha, qm_entity_overlay_deep_unfreeze_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "深解冻图块实体覆盖透明度（0-100）")
MACRO_CONFIG_INT(QmEntityOverlayTeleAlpha, qm_entity_overlay_tele_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "传送图块实体覆盖透明度（0-100）")
MACRO_CONFIG_INT(QmEntityOverlayTeleCheckpointAlpha, qm_entity_overlay_tele_checkpoint_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "传送检查点边界实体覆盖透明度（0-100）")
MACRO_CONFIG_INT(QmEntityOverlaySwitchAlpha, qm_entity_overlay_switch_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "开关图块实体覆盖透明度（0-100）")

// Q1menG Client Recognition
MACRO_CONFIG_INT(QmClientMarkTrail, qm_client_mark_trail, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "远程粒子：通过中心服同步并渲染其他玩家（需对方开启本地+远程）")
MACRO_CONFIG_INT(QmClientShowBadge, qm_client_show_badge, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示 Qm 标识：通过中心服识别并在名字板/计分板标记 QmClient 用户")

// Legacy QiaFen Compatibility
MACRO_CONFIG_INT(QmQiaFenEnabled, qm_qiafen_enabled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "旧恰分开关（仅兼容迁移）")
MACRO_CONFIG_INT(QmQiaFenUseDummy, qm_qiafen_use_dummy, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "旧恰分 Dummy 开关（仅兼容迁移）")
MACRO_CONFIG_STR(QmQiaFenRules, qm_qiafen_rules, 2048, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "旧恰分规则（仅兼容迁移）")
MACRO_CONFIG_STR(QmQiaFenKeywords, qm_qiafen_keywords, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "旧恰分关键词（仅兼容迁移）")
MACRO_CONFIG_INT(QmKeywordReplyEnabled, qm_keyword_reply_enabled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用关键词回复")
MACRO_CONFIG_INT(QmKeywordReplyUseDummy, qm_keyword_reply_use_dummy, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "关键词回复使用 Dummy")
MACRO_CONFIG_INT(QmKeywordReplyAutoRename, qm_keyword_reply_auto_rename, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "旧全局关键词改名开关（仅兼容迁移）")
MACRO_CONFIG_STR(QmKeywordReplyRules, qm_keyword_reply_rules, 4096, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "关键词回复规则（每行格式：[rename] [regex] 关键词=>回复）")
MACRO_CONFIG_INT(QmAutoReplyCooldown, qm_auto_reply_cooldown, 3, 0, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "自动回复冷却时间（秒）")

// Pie Menu
MACRO_CONFIG_INT(QmPieMenuEnabled, qm_pie_menu_enabled, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用饼菜单")
MACRO_CONFIG_INT(QmPieMenuMaxDistance, qm_pie_menu_max_distance, 400, 100, 2000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "最近玩家检测最大距离")
MACRO_CONFIG_INT(QmPieMenuScale, qm_pie_menu_scale, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "菜单缩放百分比")
MACRO_CONFIG_INT(QmPieMenuOpacity, qm_pie_menu_opacity, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "菜单透明度（0-100）")
MACRO_CONFIG_STR(QmPieMenuRenameQueue, qm_pie_menu_rename_queue, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "饼菜单改名名单（使用|分隔，如：璇梦1|璇梦2）")
MACRO_CONFIG_INT(QmPieMenuColorFriend, qm_pie_menu_color_friend, 0xE64D66BF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "好友选项颜色")
MACRO_CONFIG_INT(QmPieMenuColorWhisper, qm_pie_menu_color_whisper, 0x8059B3BF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "私聊选项颜色")
MACRO_CONFIG_INT(QmPieMenuColorMention, qm_pie_menu_color_mention, 0xD98033BF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "提及选项颜色")
MACRO_CONFIG_INT(QmPieMenuColorCopySkin, qm_pie_menu_color_copy_skin, 0x408CCCBF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "复制皮肤选项颜色")
MACRO_CONFIG_INT(QmPieMenuColorSwap, qm_pie_menu_color_swap, 0xCC4D4DBF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "交换选项颜色")
MACRO_CONFIG_INT(QmPieMenuColorSpectate, qm_pie_menu_color_spectate, 0x738C99BF, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "观战选项颜色")

// Repeat Message
MACRO_CONFIG_INT(QmRepeatEnabled, qm_repeat_enabled, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用复读上一条消息")
MACRO_CONFIG_INT(QmRepeatKey, qm_repeat_key, 278, 0, 512, CFGFLAG_CLIENT | CFGFLAG_SAVE, "复读快捷键（默认 Home=278）")
MACRO_CONFIG_INT(QmSayNoPop, qm_say_nopop, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "聊天输入时不显示打字表情")
MACRO_CONFIG_INT(QmHammerSwapSkin, qm_hammer_swap_skin, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "锤中目标后复制其皮肤")
MACRO_CONFIG_INT(QmRandomEmoteOnHit, qm_random_emote_on_hit, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "被锤子或榴弹击中时随机发送表情")
MACRO_CONFIG_INT(QmComboPopup, qm_combo_popup, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "2 秒内连续钩中或锤中玩家时显示连击提示")
MACRO_CONFIG_INT(QmWeaponTrajectory, qm_weapon_trajectory, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用武器弹道辅助线")
MACRO_CONFIG_INT(QmDeepflyMode, qm_deepfly_mode, 0, 0, 3, CFGFLAG_CLIENT, "Deepfly 模式（0=正常，1=DF，2=HDF，3=自定义）")

// Auto Unspec on Unfreeze
MACRO_CONFIG_INT(QmAutoUnspecOnUnfreeze, qm_auto_unspec_on_unfreeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "解冻时自动退出旁观")

// HJ大佬辅助
MACRO_CONFIG_INT(QmAutoSwitchOnUnfreeze, qm_auto_switch_on_unfreeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "本体和 Dummy 都冻结时，自动切换到先解冻的一侧")
MACRO_CONFIG_INT(QmAutoCloseChatOnUnfreeze, qm_auto_close_chat_on_unfreeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "解冻后自动关闭聊天输入框")
MACRO_CONFIG_INT(QmFreezeWakeupPopup, qm_freeze_wakeup_popup, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "本体或 Dummy 被锤醒时，在对应玩家随机的左上或右上角显示提示")

// Input Overlay
MACRO_CONFIG_INT(QmInputOverlay, qm_input_overlay, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示输入叠加")
MACRO_CONFIG_INT(QmInputOverlayScale, qm_input_overlay_scale, 20, 1, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "输入叠加比例（百分比）")
MACRO_CONFIG_INT(QmInputOverlayOpacity, qm_input_overlay_opacity, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "输入叠加不透明度（百分比）")
MACRO_CONFIG_INT(QmInputOverlayPosX, qm_input_overlay_pos_x, 71, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "输入叠加 X 位置（百分比）")
MACRO_CONFIG_INT(QmInputOverlayPosY, qm_input_overlay_pos_y, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "输入叠加 Y 位置（百分比）")

// Voice
MACRO_CONFIG_INT(RiVoiceEnable, qm_voice_enable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用语音聊天")
MACRO_CONFIG_INT(RiVoiceProtocolVersion, qm_voice_protocol_version, 3, 1, 255, CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音协议版本")
MACRO_CONFIG_STR(RiVoiceServer, qm_voice_server, 128, "42.194.185.210:9987", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音服务器地址（host:port）")
MACRO_CONFIG_STR(RiVoiceAudioBackend, qm_voice_audio_backend, 64, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音音频后端（SDL 驱动名，空为自动）")
MACRO_CONFIG_STR(RiVoiceInputDevice, qm_voice_input_device, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音输入设备（空为默认）")
MACRO_CONFIG_STR(RiVoiceOutputDevice, qm_voice_output_device, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音输出设备（空为默认）")
MACRO_CONFIG_INT(RiVoiceStereo, qm_voice_stereo, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音立体声输出")
MACRO_CONFIG_INT(RiVoiceStereoWidth, qm_voice_stereo_width, 100, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音立体声宽度（百分比）")
MACRO_CONFIG_STR(RiVoiceToken, qm_voice_token, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音房间 Token（可选）")
MACRO_CONFIG_INT(RiVoiceGroupMode, qm_voice_group_mode, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音分组模式")
MACRO_CONFIG_INT(RiVoiceFilterEnable, qm_voice_filter_enable, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用语音滤波（高通/压缩/限幅）")
MACRO_CONFIG_INT(RiVoiceNoiseSuppressEnable, qm_voice_noise_suppress_enable, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用噪声抑制")
MACRO_CONFIG_INT(RiVoiceNoiseSuppressStrength, qm_voice_noise_suppress_strength, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "噪声抑制强度（百分比）")
MACRO_CONFIG_INT(RiVoiceCompThreshold, qm_voice_comp_threshold, 20, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "压缩器阈值（百分比）")
MACRO_CONFIG_INT(RiVoiceCompRatio, qm_voice_comp_ratio, 25, 10, 80, CFGFLAG_CLIENT | CFGFLAG_SAVE, "压缩器比率（x10）")
MACRO_CONFIG_INT(RiVoiceCompAttackMs, qm_voice_comp_attack_ms, 20, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "压缩器攻击时间（毫秒）")
MACRO_CONFIG_INT(RiVoiceCompReleaseMs, qm_voice_comp_release_ms, 200, 10, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "压缩器释放时间（毫秒）")
MACRO_CONFIG_INT(RiVoiceCompMakeup, qm_voice_comp_makeup, 160, 0, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "压缩器补偿增益（百分比）")
MACRO_CONFIG_INT(RiVoiceLimiter, qm_voice_limiter, 50, 10, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "限幅器阈值（百分比）")
MACRO_CONFIG_INT(RiVoiceRadius, qm_voice_radius, 50, 1, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音距离半径（tile）")
MACRO_CONFIG_INT(RiVoiceVolume, qm_voice_volume, 100, 0, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音播放音量（百分比）")
MACRO_CONFIG_INT(RiVoiceMicVolume, qm_voice_mic_volume, 100, 0, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "麦克风音量（百分比）")
MACRO_CONFIG_INT(RiVoiceMicMute, qm_voice_mic_mute, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "静音麦克风")
MACRO_CONFIG_INT(RiVoiceTestMode, qm_voice_test_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音测试模式（0=关 1=本地 2=服务器回环）")
MACRO_CONFIG_INT(RiVoiceVadEnable, qm_voice_vad_enable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用语音激活（VAD）")
MACRO_CONFIG_INT(RiVoiceVadThreshold, qm_voice_vad_threshold, 8, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "VAD 阈值（百分比）")
MACRO_CONFIG_INT(RiVoiceVadReleaseDelayMs, qm_voice_vad_release_delay_ms, 150, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "VAD 释放延迟（毫秒）")
MACRO_CONFIG_INT(RiVoiceIgnoreDistance, qm_voice_ignore_distance, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "忽略语音距离衰减")
MACRO_CONFIG_INT(RiVoiceGroupGlobal, qm_voice_group_global, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "同组全图收听")
MACRO_CONFIG_INT(RiVoiceVisibilityMode, qm_voice_visibility_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "可见性过滤模式")
MACRO_CONFIG_INT(RiVoiceListMode, qm_voice_list_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "名单过滤模式")
MACRO_CONFIG_STR(RiVoiceWhitelist, qm_voice_whitelist, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音白名单（逗号分隔）")
MACRO_CONFIG_STR(RiVoiceBlacklist, qm_voice_blacklist, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音黑名单（逗号分隔）")
MACRO_CONFIG_STR(RiVoiceMute, qm_voice_mute, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音静音名单（逗号分隔）")
MACRO_CONFIG_INT(RiVoiceHearVad, qm_voice_hear_vad, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "接收 VAD 讲话者")
MACRO_CONFIG_STR(RiVoiceVadAllow, qm_voice_vad_allow, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "VAD 允许名单（逗号分隔）")
MACRO_CONFIG_STR(RiVoiceNameVolumes, qm_voice_name_volumes, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "按名字单独音量（name=percent）")
MACRO_CONFIG_INT(RiVoiceShowIndicator, qm_voice_show_indicator, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示语音指示图标")
MACRO_CONFIG_INT(RiVoiceIndicatorAboveSelf, qm_voice_indicator_above_self, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示自己头顶语音指示")
MACRO_CONFIG_INT(RiVoiceShowPing, qm_voice_show_ping, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示语音延迟")
MACRO_CONFIG_INT(RiVoiceDebug, qm_voice_debug, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "输出语音调试日志")
MACRO_CONFIG_INT(RiVoiceShowWhenActive, qm_voice_show_when_active, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "麦克风激活时显示提示")
MACRO_CONFIG_INT(RiVoiceOffNonActive, qm_voice_off_nonactive, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "窗口失焦时暂停语音")
MACRO_CONFIG_INT(RiVoicePttReleaseDelayMs, qm_voice_ptt_release_delay_ms, 0, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "PTT 释放延迟（毫秒）")
MACRO_CONFIG_INT(RiVoiceHearOnSpecPos, qm_voice_hear_on_spec_pos, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "旁观时按镜头中心收听")
MACRO_CONFIG_INT(RiVoiceHearPeoplesInSpectate, qm_voice_hear_peoples_in_spectate, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "接收旁观/非活跃玩家语音")
MACRO_CONFIG_INT(RiVoiceShowOverlay, qm_voice_show_overlay, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示语音叠加层")

// Streamer Mode
MACRO_CONFIG_INT(QmStreamerHideNames, qm_streamer_hide_names, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "隐藏非好友姓名/部落并显示客户端 ID")
MACRO_CONFIG_INT(QmStreamerHideSkins, qm_streamer_hide_skins, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "非好友使用默认皮肤")
MACRO_CONFIG_INT(QmStreamerScoreboardDefaultFlags, qm_streamer_scoreboard_default_flags, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在记分牌中显示默认国旗")

// Friend Online Notifications
MACRO_CONFIG_INT(QmFriendOnlineNotify, qm_friend_online_notify, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "好友上线提醒")
MACRO_CONFIG_INT(QmFriendOnlineAutoRefresh, qm_friend_online_auto_refresh, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "好友提醒自动刷新服务器列表")
MACRO_CONFIG_INT(QmFriendOnlineRefreshSeconds, qm_friend_online_refresh_seconds, 30, 5, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "好友提醒刷新间隔（秒）")
MACRO_CONFIG_INT(QmFriendEnterAutoGreet, qm_friend_enter_auto_greet, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "好友进图自动打招呼")
MACRO_CONFIG_INT(QmFriendEnterBroadcast, qm_friend_enter_broadcast, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "用大字提示好友进服")
MACRO_CONFIG_STR(QmFriendEnterBroadcastText, qm_friend_enter_broadcast_text, 128, "%s好友进入本服", CFGFLAG_CLIENT | CFGFLAG_SAVE, "好友进服大字提示文本（使用 %s 表示好友名）")
MACRO_CONFIG_STR(QmFriendEnterGreetText, qm_friend_enter_greet_text, 128, "你好啊!", CFGFLAG_CLIENT | CFGFLAG_SAVE, "好友进图自动打招呼文本")

// Block Words
MACRO_CONFIG_INT(QmBlockWordsEnabled, qm_block_words_enabled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用屏蔽词列表")
MACRO_CONFIG_INT(QmBlockWordsShowConsole, qm_block_words_show_console, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在控制台显示被屏蔽内容")
MACRO_CONFIG_COL(QmBlockWordsConsoleColor, qm_block_words_console_color, 0xFFFFFFFF, CFGFLAG_CLIENT | CFGFLAG_SAVE, "控制台中被屏蔽内容的颜色")
MACRO_CONFIG_INT(QmBlockWordsMultiReplace, qm_block_words_multi_replace, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "按原词长度替换为多个字符")
MACRO_CONFIG_INT(QmBlockWordsMode, qm_block_words_mode, 2, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "替换模式（0=Regex，1=Full，2=Both）")
MACRO_CONFIG_STR(QmBlockWordsReplacementChar, qm_block_words_replacement_char, 8, "*", CFGFLAG_CLIENT | CFGFLAG_SAVE, "屏蔽词替换字符")
MACRO_CONFIG_STR(QmBlockWordsList, qm_block_words_list, 1024, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "屏蔽词列表（使用逗号分隔）")
MACRO_CONFIG_STR(QmSidebarCardOrder, qm_sidebar_card_order, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "QmClient 侧栏模块排序")
MACRO_CONFIG_STR(QmSidebarCardCollapsed, qm_sidebar_card_collapsed, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "QmClient 侧栏模块折叠状态")
MACRO_CONFIG_STR(QmSidebarCardUsage, qm_sidebar_card_usage, 1024, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "QmClient 侧栏模块使用频率")
