// This file can be included several times.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(ConfigName, ScriptName, Def, Min, Max, Save, Desc) ;
#define MACRO_CONFIG_COL(ConfigName, ScriptName, Def, Save, Desc) ;
#define MACRO_CONFIG_STR(ConfigName, ScriptName, Len, Def, Save, Desc) ;
#endif

// QmClient specific variables

// Nameplate
MACRO_CONFIG_STR(QmNameplateRowOrder, qm_nameplate_row_order, 64, "keys,coords,hook,clan,name", CFGFLAG_CLIENT | CFGFLAG_SAVE, "名字板行顺序（逗号分隔：keys,coords,hook,clan,name）")

// Gores
MACRO_CONFIG_INT(QmGores, qm_gores, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用 Gores 锤枪自动切换")
MACRO_CONFIG_INT(QmGoresDisableIfWeapons, qm_gores_disable_if_weapons, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "拿到额外武器时停用 Gores 自动切换")
MACRO_CONFIG_INT(QmGoresAutoEnable, qm_gores_auto_enable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在 Gores 游戏模式中自动启用 Gores 自动切换")
MACRO_CONFIG_INT(QmGoresFastInput, qm_gores_fast_input, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Gores 模式下启用快速输入")
MACRO_CONFIG_INT(QmGoresFastInputOthers, qm_gores_fast_input_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Gores 模式下对其他玩家也启用快速输入")
MACRO_CONFIG_INT(QmGoresHideGuides, qm_gores_hide_guides, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Gores 模式下隐藏辅助线")
MACRO_CONFIG_INT(QmAxiomAutoLogin, qm_axiom_auto_login, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "进入 Axiom 社区服务器后自动登录")
MACRO_CONFIG_STR(QmAxiomLoginPassword, qm_axiom_login_password, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Axiom 服务器自动登录密码")

// Focus Mode (Zen Mode)
MACRO_CONFIG_INT(QmFocusMode, qm_focus_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用专注模式（Zen Mode）")
MACRO_CONFIG_INT(QmFocusModeHideNames, qm_focus_mode_hide_names, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "专注模式下隐藏玩家名字")
MACRO_CONFIG_INT(QmFocusModeHideEffects, qm_focus_mode_hide_effects, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "专注模式下隐藏视觉特效")
MACRO_CONFIG_INT(QmFocusModeHideHud, qm_focus_mode_hide_hud, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "专注模式下隐藏 HUD")
MACRO_CONFIG_INT(QmFocusModeHideChat, qm_focus_mode_hide_chat, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "专注模式下隐藏聊天")
MACRO_CONFIG_INT(QmFocusModeHideEcho, qm_focus_mode_hide_echo, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "专注模式下隐藏 echo 消息")
MACRO_CONFIG_INT(QmFocusModeHideUI, qm_focus_mode_hide_ui, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "专注模式下隐藏非必要 UI 元素")
MACRO_CONFIG_INT(QmFocusModeHideScoreboard, qm_focus_mode_hide_scoreboard, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "专注模式下隐藏计分板")
MACRO_CONFIG_INT(QmFocusModeHideOverheadIndicators, qm_focus_mode_hide_overhead_indicators, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "专注模式下隐藏头顶方向键和强弱钩提示")

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
MACRO_CONFIG_INT(QmFootParticles, qm_foot_particles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "本地粒子：显示自己 Tee 身后掉落的粒子（如冻结雪花）")

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

MACRO_CONFIG_INT(QmComboPopup, qm_combo_popup, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "2 秒内连续钩中或锤中玩家时显示连击提示")

// HJ大佬辅助
MACRO_CONFIG_INT(QmFreezeWakeupPopup, qm_freeze_wakeup_popup, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "本体或 Dummy 被锤醒时，在对应玩家随机的左上或右上角显示提示")

MACRO_CONFIG_INT(QmAutoTeamLock, qm_auto_team_lock, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "加入可锁定队伍后自动锁定")
MACRO_CONFIG_INT(QmAutoTeamLockDelay, qm_auto_team_lock_delay, 5, 0, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "自动锁定延迟（秒）")

// Translate - General
MACRO_CONFIG_STR(QmTranslateBackend, qm_translate_backend, 32, "llm", CFGFLAG_CLIENT | CFGFLAG_SAVE, "翻译后端（llm/tencentcloud/libretranslate/ftapi）")
MACRO_CONFIG_STR(QmTranslateTarget, qm_translate_target, 16, "zh", CFGFLAG_CLIENT | CFGFLAG_SAVE, "翻译目标语言代码（如 zh、en、ja、zh-TW）")
MACRO_CONFIG_INT(QmTranslateAuto, qm_translate_auto, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "自动翻译入站消息")
MACRO_CONFIG_INT(QmTranslateLocalDetectMinChars, qm_translate_local_detect_min_chars, 2, 1, 12, CFGFLAG_CLIENT | CFGFLAG_SAVE, "本地目标语言识别最小字符数")
MACRO_CONFIG_INT(QmTranslateLocalDetectRatio, qm_translate_local_detect_ratio, 75, 50, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "本地目标语言识别占比阈值")
MACRO_CONFIG_INT(QmTranslateFtapiAutoEnable, qm_translate_ftapi_auto_enable, 0, 0, 1,
    CFGFLAG_CLIENT | CFGFLAG_SAVE,
    "允许 FTAPI 自动翻译（可能导致过载）")

// Translate - LLM API (OpenAI 兼容，默认智谱AI预设)
MACRO_CONFIG_INT(QmTranslateLlmProvider, qm_translate_llm_provider, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "LLM Provider (0=ZhipuAI, 1=DeepSeek, 2=OpenAI, 3=Custom)")

// 各 Provider 的模型配置（切换 Provider 时自动切换对应模型）
MACRO_CONFIG_STR(QmTranslateLlmModelZhipu, qm_translate_llm_model_zhipu, 32, "glm-4.5-flash", CFGFLAG_CLIENT | CFGFLAG_SAVE, "智谱AI 模型名称")
MACRO_CONFIG_STR(QmTranslateLlmModelDeepseek, qm_translate_llm_model_deepseek, 32, "deepseek-chat", CFGFLAG_CLIENT | CFGFLAG_SAVE, "DeepSeek 模型名称")
MACRO_CONFIG_STR(QmTranslateLlmModelOpenai, qm_translate_llm_model_openai, 32, "gpt-4o-mini", CFGFLAG_CLIENT | CFGFLAG_SAVE, "OpenAI 模型名称")
MACRO_CONFIG_STR(QmTranslateLlmModelCustom, qm_translate_llm_model_custom, 32, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "自定义 Provider 模型名称")

// 各 Provider 的端点配置（留空使用默认端点）
MACRO_CONFIG_STR(QmTranslateLlmEndpointZhipu, qm_translate_llm_endpoint_zhipu, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "智谱AI 端点（留空使用默认）")
MACRO_CONFIG_STR(QmTranslateLlmEndpointDeepseek, qm_translate_llm_endpoint_deepseek, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "DeepSeek 端点（留空使用默认）")
MACRO_CONFIG_STR(QmTranslateLlmEndpointOpenai, qm_translate_llm_endpoint_openai, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "OpenAI 端点（留空使用默认）")
MACRO_CONFIG_STR(QmTranslateLlmEndpointCustom, qm_translate_llm_endpoint_custom, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "自定义 Provider 端点")

// 各 Provider 的 API Key
MACRO_CONFIG_STR(QmTranslateLlmKeyZhipu, qm_translate_llm_key_zhipu, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "智谱AI API Key")
MACRO_CONFIG_STR(QmTranslateLlmKeyDeepseek, qm_translate_llm_key_deepseek, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "DeepSeek API Key")
MACRO_CONFIG_STR(QmTranslateLlmKeyOpenai, qm_translate_llm_key_openai, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "OpenAI API Key")
MACRO_CONFIG_STR(QmTranslateLlmKeyCustom, qm_translate_llm_key_custom, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "自定义 Provider API Key")

MACRO_CONFIG_INT(QmTranslateLlmConcurrency, qm_translate_llm_concurrency, 1, 1, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "LLM 翻译并发数")
MACRO_CONFIG_INT(QmTranslateLlmConcurrencyDefault, qm_translate_llm_concurrency_default, 3, 1, 20,
    CFGFLAG_CLIENT | CFGFLAG_SAVE,
    "LLM 翻译默认并发数（智能调整）")
MACRO_CONFIG_INT(QmTranslateLlmEnableThinking, qm_translate_llm_enable_thinking, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用 LLM 思考模式（可能增加响应时间）")
MACRO_CONFIG_STR(QmTranslateSystemPrompt, qm_translate_system_prompt, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "自定义翻译提示词（覆盖内置提示词）")

// Dummy Mini View
MACRO_CONFIG_INT(QmDummyMiniView, qm_dummy_miniview, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "显示分身迷你视图窗口")
MACRO_CONFIG_INT(QmDummyMiniViewAuto, qm_dummy_miniview_auto, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "仅在另一只 tee 离开当前视角时显示分身迷你视图")
MACRO_CONFIG_INT(QmDummyMiniViewSize, qm_dummy_miniview_size, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "分身迷你视图大小（百分比）")
MACRO_CONFIG_INT(QmDummyMiniViewZoom, qm_dummy_miniview_zoom, 100, 10, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "分身迷你视图缩放（百分比）")

// System Media Controls
MACRO_CONFIG_INT(QmSmtcEnable, qm_smtc_enable, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用系统媒体传输控制集成")
MACRO_CONFIG_INT(QmSmtcShowHud, qm_smtc_show_hud, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "在 HUD 上显示系统媒体信息")

// Translate - Source/Target Language
MACRO_CONFIG_STR(QmTranslateSource, qm_translate_source, 16, "auto", CFGFLAG_CLIENT | CFGFLAG_SAVE, "翻译源语言代码（auto=自动检测）")

// Translate - Auto Outgoing
MACRO_CONFIG_INT(QmTranslateAutoOutgoing, qm_translate_auto_outgoing, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "自动翻译发送的消息")
MACRO_CONFIG_INT(QmTranslateAutoOutgoingMode, qm_translate_auto_outgoing_mode, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "自动翻译模式 (0=仅常见源语言输入时触发, 1=始终翻译)")
MACRO_CONFIG_STR(QmTranslateOutgoingTarget, qm_translate_outgoing_target, 16, "en", CFGFLAG_CLIENT | CFGFLAG_SAVE, "出站翻译目标语言代码")

// Translate - Tencent Cloud
MACRO_CONFIG_STR(QmTranslateTcEndpoint, qm_translate_tc_endpoint, 256, "https://tmt.tencentcloudapi.com/", CFGFLAG_CLIENT | CFGFLAG_SAVE, "腾讯云翻译端点")
MACRO_CONFIG_STR(QmTranslateTcSecretId, qm_translate_tc_secret_id, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "腾讯云翻译 SecretId")
MACRO_CONFIG_STR(QmTranslateTcSecretKey, qm_translate_tc_secret_key, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "腾讯云翻译 SecretKey")
MACRO_CONFIG_STR(QmTranslateTcRegion, qm_translate_tc_region, 32, "ap-guangzhou", CFGFLAG_CLIENT | CFGFLAG_SAVE, "腾讯云翻译地域")

// Translate - LibreTranslate
MACRO_CONFIG_STR(QmTranslateLibreEndpoint, qm_translate_libre_endpoint, 256, "http://localhost:5000", CFGFLAG_CLIENT | CFGFLAG_SAVE, "LibreTranslate 端点")
MACRO_CONFIG_STR(QmTranslateLibreKey, qm_translate_libre_key, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "LibreTranslate API Key")

MACRO_CONFIG_INT(QmSpeedrunTimer, qm_speedrun_timer, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "速通倒计时器")
MACRO_CONFIG_INT(QmSpeedrunTimerTime, qm_speedrun_timer_time, 0, 0, 9999, CFGFLAG_CLIENT | CFGFLAG_SAVE, "速通计时器时间（MMSS 格式，兼容旧版）")
MACRO_CONFIG_INT(QmSpeedrunTimerHours, qm_speedrun_timer_hours, 0, 0, 99, CFGFLAG_CLIENT | CFGFLAG_SAVE, "速通计时器小时")
MACRO_CONFIG_INT(QmSpeedrunTimerMinutes, qm_speedrun_timer_minutes, 0, 0, 59, CFGFLAG_CLIENT | CFGFLAG_SAVE, "速通计时器分钟")
MACRO_CONFIG_INT(QmSpeedrunTimerSeconds, qm_speedrun_timer_seconds, 0, 0, 59, CFGFLAG_CLIENT | CFGFLAG_SAVE, "速通计时器秒")
MACRO_CONFIG_INT(QmSpeedrunTimerMilliseconds, qm_speedrun_timer_milliseconds, 0, 0, 999, CFGFLAG_CLIENT | CFGFLAG_SAVE, "速通计时器毫秒")
MACRO_CONFIG_INT(QmSpeedrunTimerAutoDisable, qm_speedrun_timer_auto_disable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "时间耗尽后自动禁用速通计时器")

// Translate UI Colors - 翻译按钮自定义颜色
MACRO_CONFIG_COL(QmTranslateBtnColorDisabled, qm_translate_btn_color_disabled, 0xD1000029, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Translate button color when disabled")
MACRO_CONFIG_COL(QmTranslateBtnColorEnabled, qm_translate_btn_color_enabled, 0xE69E5E86, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Translate button color when enabled")
MACRO_CONFIG_COL(QmTranslateMenuBgColor, qm_translate_menu_bg_color, 0xF200001F, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Translate menu background color")
MACRO_CONFIG_COL(QmTranslateMenuOptionSelected, qm_translate_menu_option_selected, 0xE69E5E86, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Translate menu selected option color")
MACRO_CONFIG_COL(QmTranslateMenuOptionNormal, qm_translate_menu_option_normal, 0xE6000033, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Translate menu normal option color")
