// This file can be included several times.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(Tcme, ScriptName, Def, Min, Max, Save, Desc) ;
#define MACRO_CONFIG_COL(Tcme, ScriptName, Def, Save, Desc) ;
#define MACRO_CONFIG_STR(Tcme, ScriptName, Len, Def, Save, Desc) ;
#endif

// Qimeng Client Specific Variables

// Nameplate
MACRO_CONFIG_STR(QmNameplateRowOrder, qm_nameplate_row_order, 64, "keys,coords,hook,clan,name", CFGFLAG_CLIENT | CFGFLAG_SAVE, "名字板行顺序（逗号分隔：keys,coords,hook,clan,name）")

// Fast Input (QmClient specific)
MACRO_CONFIG_INT(QmFastInputAmount, qm_fast_input_amount, 20, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "QmClient 快速输入的目标超前毫秒")
MACRO_CONFIG_INT(QmFastInputGuard, qm_fast_input_guard, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "QmClient 快速输入在冻结、出勾和拖拽时自动收敛")

// Fast Input (BestClient legacy - renamed to qm_)
MACRO_CONFIG_INT(QmFastInputMode, qm_fast_input_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Fast input mode (0 = fast input, 1 = delta input, 2 = gamma input)")
MACRO_CONFIG_INT(QmFastInputDeltaInput, qm_fast_input_delta_input, 0, 0, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "")
MACRO_CONFIG_INT(QmFastInputGammaInput, qm_fast_input_gamma_input, 0, 0, 600, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Gamma input amount in 0.01 ticks (UI shows 0-6.00M)")
MACRO_CONFIG_INT(QmFastInputAutoMargin, qm_fast_input_auto_margin, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically adjusts prediction margin in real time for fast input, latency and connection stability")
MACRO_CONFIG_INT(QmDeltaInputOthers, qm_delta_input_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply delta input to other tees")
MACRO_CONFIG_INT(QmGammaInputOthers, qm_gamma_input_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply gamma input to other tees")

// Gores
MACRO_CONFIG_INT(QmGores, qm_gores, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用 Gores 锤枪自动切换")
MACRO_CONFIG_INT(QmGoresDisableIfWeapons, qm_gores_disable_if_weapons, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "拿到额外武器时停用 Gores 自动切换")
MACRO_CONFIG_INT(QmGoresAutoEnable, qm_gores_auto_enable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在 Gores gametype 自动启用 Gores 自动切换")

// Player Stats HUD
MACRO_CONFIG_INT(QmPlayerStatsHud, qm_player_stats_hud, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用玩家统计HUD显示")
MACRO_CONFIG_INT(QmPlayerStatsMapProgress, qm_player_stats_map_progress, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示地图进度条（内测中）")
MACRO_CONFIG_INT(QmPlayerStatsMapProgressStyle, qm_player_stats_map_progress_style, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "地图进度条样式（0=底部进度条, 1=HUD内嵌进度条）")
MACRO_CONFIG_COL(QmPlayerStatsMapProgressColor, qm_player_stats_map_progress_color, 0xFF24C764, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "地图进度条颜色")
MACRO_CONFIG_INT(QmPlayerStatsMapProgressWidth, qm_player_stats_map_progress_width, 28, 10, 80, CFGFLAG_CLIENT | CFGFLAG_SAVE, "地图进度条宽度（占屏幕宽度百分比）")
MACRO_CONFIG_INT(QmPlayerStatsMapProgressHeight, qm_player_stats_map_progress_height, 10, 6, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "地图进度条高度")
MACRO_CONFIG_INT(QmPlayerStatsMapProgressPosX, qm_player_stats_map_progress_pos_x, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "地图进度条水平位置（中心点，占屏幕宽度百分比）")
MACRO_CONFIG_INT(QmPlayerStatsMapProgressPosY, qm_player_stats_map_progress_pos_y, 97, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "地图进度条垂直位置（顶边，占屏幕高度百分比）")
MACRO_CONFIG_INT(QmPlayerStatsMapProgressDbgRoute, qm_player_stats_map_progress_dbg_route, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在地图中显示地图进度调试点状路线")
MACRO_CONFIG_INT(QmPlayerStatsResetOnJoin, qm_player_stats_reset_on_join, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "进入服务器时重置统计数据（0=累计统计, 1=进入服务器重置）")
MACRO_CONFIG_STR(QmUnfinishedMapPlayer, qm_unfinished_map_player, 16, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "未完成图查询用玩家名")
MACRO_CONFIG_INT(QmUnfinishedMapType, qm_unfinished_map_type, 0, 0, 13, CFGFLAG_CLIENT | CFGFLAG_SAVE, "未完成图筛选类型")
MACRO_CONFIG_INT(QmUnfinishedMapAutoVote, qm_unfinished_map_auto_vote, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "未完成图自动发起投票")

// HUD Dynamic Island
MACRO_CONFIG_INT(QmHudIslandUseOriginalStyle, qm_hud_island_use_original_style, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD灵动岛使用原版样式")
MACRO_CONFIG_COL(QmHudIslandBgColor, qm_hud_island_bg_color, 0x9C460E, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD灵动岛背景颜色")
MACRO_CONFIG_INT(QmHudIslandBgOpacity, qm_hud_island_bg_opacity, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD灵动岛背景透明度")
MACRO_CONFIG_STR(QmHudEditorLayout, qm_hud_editor_layout, 2048, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD编辑器布局（内部使用）")

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
MACRO_CONFIG_INT(QmcFootParticles, qmc_foot_particles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "本地粒子：显示自己 tee 后面掉落的颗粒（如冻结雪花）")

// Hook Collision Line Color Mode
MACRO_CONFIG_INT(QmHookCollMode, qm_hookcoll_mode, 2, 1, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "钩子碰撞线颜色模式（1=武器跟随，2=默认墙跟随）")
MACRO_CONFIG_COL(QmShotgunColor, qm_shotgun_color, 1414790, CFGFLAG_CLIENT | CFGFLAG_SAVE, "武器跟随模式下散弹枪的钩子碰撞线颜色 (棕色)")
MACRO_CONFIG_COL(QmLaserColor, qm_laser_color, 11206528, CFGFLAG_CLIENT | CFGFLAG_SAVE, "武器跟随模式下激光枪的钩子碰撞线颜色 (蓝色)")
MACRO_CONFIG_COL(QmGrenadeColor, qm_grenade_color, 65407, CFGFLAG_CLIENT | CFGFLAG_SAVE, "武器跟随模式下榴弹枪的钩子碰撞线颜色 (红色)")
MACRO_CONFIG_INT(QmPieMenuKey, qm_pie_menu_key, 25, 0, 511, CFGFLAG_CLIENT | CFGFLAG_SAVE, "饼图菜单激活键（SDL扫描码，默认V）")

// Chat Bubble Settings
MACRO_CONFIG_INT(QmChatSaveDraft, qm_chat_save_draft, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "关闭聊天时保留未发送内容")
MACRO_CONFIG_INT(QmHideChatBubbles, qm_hide_chat_bubbles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "隐藏自己的聊天气泡 (Hide your own chat bubbles, only works when authed in remote console)")
MACRO_CONFIG_INT(QmChatBubble, qm_chat_bubble, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在玩家头顶显示聊天气泡 (Show chat bubbles above players)")
MACRO_CONFIG_INT(QmChatBubbleDuration, qm_chat_bubble_duration, 10, 1, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "聊天气泡显示时长（秒）(How long chat bubbles stay visible)")
MACRO_CONFIG_INT(QmChatBubbleAlpha, qm_chat_bubble_alpha, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "聊天气泡透明度 0-100 (Chat bubble transparency)")
MACRO_CONFIG_INT(QmChatBubbleFontSize, qm_chat_bubble_font_size, 20, 8, 32, CFGFLAG_CLIENT | CFGFLAG_SAVE, "聊天气泡字体大小 (Chat bubble font size)")
MACRO_CONFIG_COL(QmChatBubbleBgColor, qm_chat_bubble_bg_color, 404232960, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "聊天气泡背景颜色 (Chat bubble background color)")
MACRO_CONFIG_COL(QmChatBubbleTextColor, qm_chat_bubble_text_color, 4294967295, CFGFLAG_CLIENT | CFGFLAG_SAVE, "聊天气泡文字颜色 (Chat bubble text color)")
MACRO_CONFIG_INT(QmChatBubbleAnimation, qm_chat_bubble_animation, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "消失动画: 0=淡出 1=缩小 2=上滑 (Disappear animation: 0=fade 1=shrink 2=slide up)")

// UI Settings
MACRO_CONFIG_INT(QmUiRuntimeV2Debug, qm_ui_runtime_v2_debug, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用 UI 运行时 v2 调试日志")

// Scoreboard
MACRO_CONFIG_INT(ClScoreboardPoints, cl_scoreboard_points, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在计分板显示分数")
MACRO_CONFIG_INT(ClScoreboardSortMode, cl_scoreboard_sort_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "计分板按分数排序")
MACRO_CONFIG_INT(QmScoreboardAnimOptim, qm_scoreboard_anim_optim, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "计分板动画优化")
MACRO_CONFIG_INT(QmChatFadeOutAnim, qm_chat_fade_out_anim, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "聊天框淡出动画")
MACRO_CONFIG_INT(QmEmoticonSelectAnim, qm_emoticon_select_anim, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "表情选择动画")

// Rainbow Name
MACRO_CONFIG_INT(QmRainbowName, qm_rainbow_name, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用自己名字的彩虹色渲染")
MACRO_CONFIG_INT(QmNameplateCoordX, qm_nameplate_coord_x, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "名字牌显示坐标X")
MACRO_CONFIG_INT(QmNameplateCoordXAlignHint, qm_nameplate_coord_x_align_hint, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "名字牌X对齐提示")
MACRO_CONFIG_INT(QmNameplateCoordXAlignHintStrict, qm_nameplate_coord_x_align_hint_strict, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "名字牌X对齐提示严格模式")
MACRO_CONFIG_INT(QmNameplateCoordY, qm_nameplate_coord_y, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "名字牌显示坐标Y")
MACRO_CONFIG_INT(QmNameplateCoordsOwn, qm_nameplate_coords_own, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "名字牌显示自己坐标")
MACRO_CONFIG_INT(QmNameplateCoords, qm_nameplate_coords, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "名字牌显示他人坐标")

// Enhanced Laser Effects
MACRO_CONFIG_INT(QmLaserEnhanced, qm_laser_enhanced, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用增强激光特效（辉光+脉冲动画）")
MACRO_CONFIG_INT(QmLaserGlowIntensity, qm_laser_glow_intensity, 30, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "激光辉光强度 (0-100)")
MACRO_CONFIG_INT(QmLaserPulseSpeed, qm_laser_pulse_speed, 100, 10, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "脉冲动画速度 ( 百分比, 100=正常)")
MACRO_CONFIG_INT(QmLaserPulseAmplitude, qm_laser_pulse_amplitude, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "脉冲振幅 (0-100)")
MACRO_CONFIG_INT(QmLaserSize, qm_laser_size, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "激光大小/粗细 (百分比, 100=默认)")
MACRO_CONFIG_INT(QmLaserRoundCaps, qm_laser_round_caps, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "激光圆角端点 (0=方角, 1=圆角)")
MACRO_CONFIG_INT(QmLaserAlpha, qm_laser_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "激光半透明度 (0=完全透明, 100=不透明)")

// Collision Hitbox Visualization
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

// Q1menG Client Recognition
MACRO_CONFIG_INT(QmClientMarkTrail, qm_client_mark_trail, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "远程粒子：通过中心服同步并渲染其他玩家（需对方开启本地+远程）")
MACRO_CONFIG_INT(QmClientShowBadge, qm_client_show_badge, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示Qm标识：通过中心服识别并在名字板/计分板标记QmClient用户")

// Legacy QiaFen Compatibility
MACRO_CONFIG_INT(QmQiaFenEnabled, qm_qiafen_enabled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "旧恰分开关（仅兼容迁移）")
MACRO_CONFIG_INT(QmQiaFenUseDummy, qm_qiafen_use_dummy, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "旧恰分 Dummy 开关（仅兼容迁移）")
MACRO_CONFIG_STR(QmQiaFenRules, qm_qiafen_rules, 2048, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "旧恰分规则（仅兼容迁移）")
MACRO_CONFIG_STR(QmQiaFenKeywords, qm_qiafen_keywords, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "旧恰分关键词（仅兼容迁移）")
MACRO_CONFIG_INT(QmKeywordReplyEnabled, qm_keyword_reply_enabled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用关键词回复")
MACRO_CONFIG_INT(QmKeywordReplyUseDummy, qm_keyword_reply_use_dummy, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "关键词回复使用Dummy")
MACRO_CONFIG_INT(QmKeywordReplyAutoRename, qm_keyword_reply_auto_rename, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "旧全局关键词改名开关（仅兼容迁移）")
MACRO_CONFIG_STR(QmKeywordReplyRules, qm_keyword_reply_rules, 4096, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "关键词回复规则（每行: [rename] [regex] 关键词=>回复）")
MACRO_CONFIG_INT(QmAutoReplyCooldown, qm_auto_reply_cooldown, 3, 0, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "自动回复冷却时间（秒）")

// Pie Menu
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

// Repeat Message
MACRO_CONFIG_INT(QmRepeatEnabled, qm_repeat_enabled, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用复读功能 (Enable repeat last message)")
MACRO_CONFIG_INT(QmRepeatKey, qm_repeat_key, 278, 0, 512, CFGFLAG_CLIENT | CFGFLAG_SAVE, "复读快捷键 (Repeat key, default: Home=278)")
MACRO_CONFIG_INT(QmSayNoPop, qm_say_nopop, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "输入时不显示打字表情 (Hide typing emoticon while chatting)")
MACRO_CONFIG_INT(QmHammerSwapSkin, qm_hammer_swap_skin, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "锤人换皮肤 (Copy target skin on hammer hit)")
MACRO_CONFIG_INT(QmRandomEmoteOnHit, qm_random_emote_on_hit, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "被锤/榴弹击中时随机表情 (Random emote on hammer/grenade hit)")
MACRO_CONFIG_INT(QmWeaponTrajectory, qm_weapon_trajectory, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用武器弹道辅助线 (Enable weapon trajectory)")
MACRO_CONFIG_INT(QmDeepflyMode, qm_deepfly_mode, 0, 0, 3, CFGFLAG_CLIENT, "Deepfly模式（0=正常，1=DF，2=HDF，3=自定义）")

// Auto Unspec on Unfreeze
MACRO_CONFIG_INT(QmAutoUnspecOnUnfreeze, qm_auto_unspec_on_unfreeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "被解冻时自动取消旁观 (Auto exit spectator mode when unfrozen)")

// HJ大佬辅助
MACRO_CONFIG_INT(QmAutoSwitchOnUnfreeze, qm_auto_switch_on_unfreeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "本体和dummy都freeze时，自动切换到先解冻的那个 (Auto switch to unfrozen tee when both are frozen)")
MACRO_CONFIG_INT(QmAutoCloseChatOnUnfreeze, qm_auto_close_chat_on_unfreeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "解冻后自动关闭聊天 (Auto close chat input when unfrozen)")

// Input Overlay
MACRO_CONFIG_INT(QmInputOverlay, qm_input_overlay, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示输入覆盖")
MACRO_CONFIG_INT(QmInputOverlayScale, qm_input_overlay_scale, 20, 1, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "输入叠加比例（百分比）")
MACRO_CONFIG_INT(QmInputOverlayOpacity, qm_input_overlay_opacity, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "输入叠加不透明度（百分比）")
MACRO_CONFIG_INT(QmInputOverlayPosX, qm_input_overlay_pos_x, 71, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "输入叠加 X 位置（百分比）")
MACRO_CONFIG_INT(QmInputOverlayPosY, qm_input_overlay_pos_y, 80, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "输入叠加 Y 位置（百分比）")

// Voice (RiVoice)
MACRO_CONFIG_INT(RiVoiceEnable, ri_voice_enable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用语音聊天")
MACRO_CONFIG_INT(RiVoiceProtocolVersion, ri_voice_protocol_version, 3, 1, 255, CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音协议版本")
MACRO_CONFIG_STR(RiVoiceServer, ri_voice_server, 128, "42.194.185.210:9987", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音服务器地址 host:port")
MACRO_CONFIG_STR(RiVoiceAudioBackend, ri_voice_audio_backend, 64, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音音频后端（SDL 驱动名，空为自动）")
MACRO_CONFIG_STR(RiVoiceInputDevice, ri_voice_input_device, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音输入设备（空为默认）")
MACRO_CONFIG_STR(RiVoiceOutputDevice, ri_voice_output_device, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音输出设备（空为默认）")
MACRO_CONFIG_INT(RiVoiceStereo, ri_voice_stereo, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音立体声输出")
MACRO_CONFIG_INT(RiVoiceStereoWidth, ri_voice_stereo_width, 100, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音立体声宽度（百分比）")
MACRO_CONFIG_STR(RiVoiceToken, ri_voice_token, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音房间 Token（可选）")
MACRO_CONFIG_INT(RiVoiceGroupMode, ri_voice_group_mode, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音分组模式")
MACRO_CONFIG_INT(RiVoiceFilterEnable, ri_voice_filter_enable, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用语音滤波（高通/压缩/限幅）")
MACRO_CONFIG_INT(RiVoiceNoiseSuppressEnable, ri_voice_noise_suppress_enable, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用噪声抑制")
MACRO_CONFIG_INT(RiVoiceNoiseSuppressStrength, ri_voice_noise_suppress_strength, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "噪声抑制强度（百分比）")
MACRO_CONFIG_INT(RiVoiceCompThreshold, ri_voice_comp_threshold, 20, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "压缩器阈值（百分比）")
MACRO_CONFIG_INT(RiVoiceCompRatio, ri_voice_comp_ratio, 25, 10, 80, CFGFLAG_CLIENT | CFGFLAG_SAVE, "压缩器比率（x10）")
MACRO_CONFIG_INT(RiVoiceCompAttackMs, ri_voice_comp_attack_ms, 20, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "压缩器攻击时间（毫秒）")
MACRO_CONFIG_INT(RiVoiceCompReleaseMs, ri_voice_comp_release_ms, 200, 10, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "压缩器释放时间（毫秒）")
MACRO_CONFIG_INT(RiVoiceCompMakeup, ri_voice_comp_makeup, 160, 0, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "压缩器补偿增益（百分比）")
MACRO_CONFIG_INT(RiVoiceLimiter, ri_voice_limiter, 50, 10, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "限幅器阈值（百分比）")
MACRO_CONFIG_INT(RiVoiceRadius, ri_voice_radius, 50, 1, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音距离半径（tile）")
MACRO_CONFIG_INT(RiVoiceVolume, ri_voice_volume, 100, 0, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音播放音量（百分比）")
MACRO_CONFIG_INT(RiVoiceMicVolume, ri_voice_mic_volume, 100, 0, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "麦克风音量（百分比）")
MACRO_CONFIG_INT(RiVoiceMicMute, ri_voice_mic_mute, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "静音麦克风")
MACRO_CONFIG_INT(RiVoiceTestMode, ri_voice_test_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音测试模式（0=关 1=本地 2=服务器回环）")
MACRO_CONFIG_INT(RiVoiceVadEnable, ri_voice_vad_enable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用语音激活（VAD）")
MACRO_CONFIG_INT(RiVoiceVadThreshold, ri_voice_vad_threshold, 8, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "VAD 阈值（百分比）")
MACRO_CONFIG_INT(RiVoiceVadReleaseDelayMs, ri_voice_vad_release_delay_ms, 150, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "VAD 释放延迟（毫秒）")
MACRO_CONFIG_INT(RiVoiceIgnoreDistance, ri_voice_ignore_distance, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "忽略语音距离衰减")
MACRO_CONFIG_INT(RiVoiceGroupGlobal, ri_voice_group_global, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "同组全图收听")
MACRO_CONFIG_INT(RiVoiceVisibilityMode, ri_voice_visibility_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "可见性过滤模式")
MACRO_CONFIG_INT(RiVoiceListMode, ri_voice_list_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "名单过滤模式")
MACRO_CONFIG_STR(RiVoiceWhitelist, ri_voice_whitelist, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音白名单（逗号分隔）")
MACRO_CONFIG_STR(RiVoiceBlacklist, ri_voice_blacklist, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音黑名单（逗号分隔）")
MACRO_CONFIG_STR(RiVoiceMute, ri_voice_mute, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "语音静音名单（逗号分隔）")
MACRO_CONFIG_INT(RiVoiceHearVad, ri_voice_hear_vad, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "接收 VAD 讲话者")
MACRO_CONFIG_STR(RiVoiceVadAllow, ri_voice_vad_allow, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "VAD 允许名单（逗号分隔）")
MACRO_CONFIG_STR(RiVoiceNameVolumes, ri_voice_name_volumes, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "按名字单独音量（name=percent）")
MACRO_CONFIG_INT(RiVoiceShowIndicator, ri_voice_show_indicator, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示语音指示图标")
MACRO_CONFIG_INT(RiVoiceIndicatorAboveSelf, ri_voice_indicator_above_self, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示自己头顶语音指示")
MACRO_CONFIG_INT(RiVoiceShowPing, ri_voice_show_ping, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示语音延迟")
MACRO_CONFIG_INT(RiVoiceDebug, ri_voice_debug, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "输出语音调试日志")
MACRO_CONFIG_INT(RiVoiceShowWhenActive, ri_voice_show_when_active, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "麦克风激活时显示提示")
MACRO_CONFIG_INT(RiVoiceOffNonActive, ri_voice_off_nonactive, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "窗口失焦时暂停语音")
MACRO_CONFIG_INT(RiVoicePttReleaseDelayMs, ri_voice_ptt_release_delay_ms, 0, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "PTT 释放延迟（毫秒）")
MACRO_CONFIG_INT(RiVoiceHearOnSpecPos, ri_voice_hear_on_spec_pos, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "旁观时按镜头中心收听")
MACRO_CONFIG_INT(RiVoiceHearPeoplesInSpectate, ri_voice_hear_peoples_in_spectate, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "接收旁观/非活跃玩家语音")
MACRO_CONFIG_INT(RiVoiceShowOverlay, ri_voice_show_overlay, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示语音叠加层")

// Streamer Mode
MACRO_CONFIG_INT(QmStreamerHideNames, qm_streamer_hide_names, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "隐藏非好友姓名/部落并显示客户端 ID")
MACRO_CONFIG_INT(QmStreamerHideSkins, qm_streamer_hide_skins, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "非好友使用默认皮肤")
MACRO_CONFIG_INT(QmStreamerScoreboardDefaultFlags, qm_streamer_scoreboard_default_flags, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在记分牌中显示默认国旗")

// Friend Online Notifications
MACRO_CONFIG_INT(QmFriendOnlineNotify, qm_friend_online_notify, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "好友上线提醒")
MACRO_CONFIG_INT(QmFriendOnlineAutoRefresh, qm_friend_online_auto_refresh, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "好友提醒自动刷新服务器列表")
MACRO_CONFIG_INT(QmFriendOnlineRefreshSeconds, qm_friend_online_refresh_seconds, 30, 5, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "好友提醒刷新间隔(秒)")
MACRO_CONFIG_INT(QmFriendEnterAutoGreet, qm_friend_enter_auto_greet, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "好友进图自动打招呼")
MACRO_CONFIG_INT(QmFriendEnterBroadcast, qm_friend_enter_broadcast, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "大字显示好友进服")
MACRO_CONFIG_STR(QmFriendEnterBroadcastText, qm_friend_enter_broadcast_text, 128, "%s好友进入本服", CFGFLAG_CLIENT | CFGFLAG_SAVE, "好友进服大字提示文本（使用%s表示好友名）")
MACRO_CONFIG_STR(QmFriendEnterGreetText, qm_friend_enter_greet_text, 128, "你好啊!", CFGFLAG_CLIENT | CFGFLAG_SAVE, "好友进图自动打招呼文本")

// Block Words
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
