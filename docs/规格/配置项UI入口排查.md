# 配置项 UI 入口排查

状态：2026-10-02 静态排查记录，包含本次头衔卡片拆分后的结果。

## 范围与判断方式

逐项提取三个配置头中带 `CFGFLAG_CLIENT` 的声明，按控制台配置名去重：

| 声明来源 | 配置数量 |
| --- | ---: |
| `src/engine/shared/config_variables_qmclient.h` | 597 |
| `src/engine/shared/config_variables_tclient.h` | 199 |
| `src/engine/shared/config_variables.h` | 465 |
| 合计 | 1261 |

包含平台条件分支中的客户端配置，不包含仅服务端配置和没有配置声明的控制台命令。数量表示配置声明范围，不是测试覆盖率。

核对配置成员的读取、写入、卡片和菜单调用，以及输入框、指针表、预设、HUD 编辑器、计分板和控制台按钮等间接入口。配置名只出现在搜索关键词、描述、测量、日志或预览中，不算可编辑 UI。下文“无入口”指没有对应的专用图形编辑入口，控制台直接输入配置名不算。

“有效”表示源码存在消费该值的业务路径，不代表本次执行过运行时验证。平台、在线状态、认证和父功能开关仍可能限制实际生效。

## 本次补齐的头衔入口

在贡献者页的“栖梦”子页，将“赞助头衔”和“头衔显示”默认并排放在首行。

| 卡片 | 内容 |
| --- | --- |
| 赞助头衔 | 兑换码、认证状态、自定义文字、昵称绑定、提交给服务器的聊天发言样式、发言预览、保存和刷新 |
| 头衔显示 | 名牌头衔总开关、头衔置于名字上方、本体和分身的本地显示、赞助聊天效果的本地显示、风格、颜色、透明度、光带、文字效果、辉光、掠光速度、波浪参数、提示方式、高级模式和头衔预览 |

本次新增两个此前确实没有控件的开关：

- `qm_show_nameplate_title`：显示名牌头衔。
- `qm_nameplate_title_above_name`：头衔显示在名字上方。

其他外观控件从原卡片迁入，配置名、保存格式和运行时语义保持原有实现。风格选择位于显示卡片，点击赞助卡的“保存”时仍会按原行为把当前风格一并提交。

两个卡片由 `src/game/client/QmUi/cards/QmCardCatalogTitle.cpp` 构建，贡献者页和全局搜索复用同一实现。分别使用稳定 ID `deck:qmclient-contributors-title` 和 `deck:qmclient-contributors-title-display`，折叠状态相互独立。旧默认排列迁到新位置；自定义排列保留。

## Qm：有效但没有专用 UI 的玩家设置

这些是后续补入口时优先考虑的配置。本次只补头衔相关入口，没有批量增加其他功能的控件。

| 功能 | 配置项 | 消费位置或限制 |
| --- | --- | --- |
| 国旗加载动画 | `qm_country_flag_anim` | `components/countryflags.cpp`，国旗加载完成后的入场动画 |
| 自动保存历史数量 | `qm_auto_save_history_count` | `components/tclient/tclient.cpp`，历史保存数量，0 关闭 |
| 自动接受组队邀请 | `qm_auto_accept_team_invite` | `components/chat.cpp`，收到邀请后发送加入队伍命令 |
| 本体皮肤队列上限 | `qm_skin_queue_length` | `components/skins.cpp`，现有队列卡没有容量控件 |
| 分身皮肤队列上限 | `qm_dummy_skin_queue_length` | 同上 |
| 重复 echo 合并时间 | `qm_echo_merge_window_ms` | `components/chat.cpp`，0 关闭合并 |
| 聊天退场滑动 | `qm_chat_anim_slide_out` | `components/chat.cpp`，当前聊天动画 UI 未提供该细项 |
| 聊天淡出时长 | `qm_chat_anim_fade_duration_ms` | 同上 |
| 隐藏自己的聊天气泡 | `qm_hide_chat_bubbles` | `components/controls.cpp`，仅远程控制台认证后生效，与 Qm 聊天气泡绘制开关不同 |
| Rank ghost 方向指示 | `qm_rank_ghost_show_direction` | `components/ghost.cpp`，观看 rank ghost 成员时生效 |
| 语音测试 | `qm_voice_test_mode` | 关闭、本地回环、服务器回环 |
| 语音距离 | `qm_voice_ignore_distance` | 忽略距离衰减，与全局房间模式共同参与判定 |
| 语音可见性过滤 | `qm_voice_visibility_mode` | 发送者可见性过滤 |
| 语音名单过滤 | `qm_voice_list_mode`、`qm_voice_whitelist`、`qm_voice_blacklist`、`qm_voice_mute` | 模式、白名单、黑名单、静音名单 |
| VAD 接收限制 | `qm_voice_hear_vad`、`qm_voice_vad_allow` | 是否接收 VAD 发言和允许名单 |
| 按昵称设置音量 | `qm_voice_name_volumes` | `name=percent` 配置，由混音路径使用 |
| 窗口失焦暂停语音 | `qm_voice_off_nonactive` | 由窗口激活状态控制 |
| PTT 松开延迟 | `qm_voice_ptt_release_delay_ms` | 松开按键后的延迟关闭时间 |
| 旁观监听位置 | `qm_voice_hear_on_spec_pos` | 使用旁观镜头位置计算收听位置 |
| 接收旁观者语音 | `qm_voice_hear_peoples_in_spectate` | 参与发送者可收听判定 |

上述语音配置的业务入口位于 `src/game/client/components/qmclient/voice/voice_core.cpp`。名单字段通过配置快照传给 `AudibilityContext`，按昵称音量经 `VoiceNameVolume` 应用，不是只声明未使用。

## Qm：高级参数与诊断项没有 UI

这些参数同样没有专用编辑控件，但不宜一律作为普通玩家卡片的遗漏。

| 分类 | 配置项 | 现状 |
| --- | --- | --- |
| Windows 网络优先级 | `qm_net_qos` | `src/engine/client/client.cpp` 中的 best-effort QoS 路径 |
| UI 色彩插值 | `qm_ui_color_interpolation` | `QmAnimResolve.cpp` 使用，线性 RGB / OKLAB |
| 圆角细分 | `qm_rect_corner_segments` | `graphics_threaded.cpp` 使用，底层几何参数 |
| Ping 缓存有效期 | `qm_ping_cache_max_age_hours` | `serverbrowser_ping_cache.cpp` 使用 |
| 资源预览内存预算 | `qm_assets_preview_budget_mb_override`、`qm_assets_preview_budget_percent` | `menus_settings_assets.cpp` 只读取预算，没有控件 |
| 设置预热 | `qm_settings_prewarm` | 菜单、Tee 卡和启动逻辑只读取，没有开关入口 |
| Vulkan 增强管线 | `qm_enhanced_rendering`、`qm_enhanced_sdf`、`qm_enhanced_blur`、`qm_enhanced_msdf` | Vulkan 后端专用；不同于已有 UI 的 `qm_graphics_backend` 和 `qm_vulkan_api_version` |
| 语音协议与设备后端 | `qm_voice_protocol_version`、`qm_voice_audio_backend` | 语音握手版本和 SDL 驱动选择 |
| 实时服务连接 | `qm_websocket_url`、`qm_websocket_protocol`、`qm_websocket_heartbeat`、`qm_websocket_backoff_base_ms`、`qm_websocket_backoff_max_ms` | 连接、心跳和重连参数 |
| 举报服务配置 | `qm_report_endpoint`、`qm_report_app_id`、`qm_report_secret` | 举报 UI 读取这些服务参数，不提供编辑；此处只记录配置名 |
| 地图上传地址 | `qm_map_upload_endpoint` | `QmUi/cards/QmMapUpload.cpp` 仅显示目标服务器，没有地址输入框；旧注释所说“界面上可填”与实现不符 |
| 网易云辅助程序 | `qm_netease_hook_timeout_ms`、`qm_netease_hook_helper_path` | 心跳超时和辅助程序路径 |
| 汽水音乐辅助程序 | `qm_soda_hook_timeout_ms`、`qm_soda_hook_helper_path` | 同上 |
| 酷狗辅助程序 | `qm_kugou_hook_timeout_ms`、`qm_kugou_hook_helper_path` | 同上 |
| QQ 音乐辅助程序 | `qm_qqmusic_hook_timeout_ms`、`qm_qqmusic_hook_helper_path` | 同上 |
| 翻译并发回退值 | `qm_translate_llm_concurrency_default` | `translate_backend.cpp` 使用；供应商专用并发控件不能编辑此回退值 |
| 图形和 UI 诊断 | `qm_graphics_trace`、`qm_ui_runtime_v2_debug`、`dbg_qm_ui_dogfood` | 诊断日志、实验验证页 |
| 网络和语音诊断 | `qm_websocket_log`、`qm_voice_debug` | 日志开关 |
| 启动崩溃报告 | `qm_crash_report_on_startup` | 控制是否弹出待处理崩溃报告，没有设置开关 |

## Qm：无消费路径、兼容项和内部状态

### 只有快照赋值的 8 个语音配置

以下字段在 `voice_core.h` 声明，并在 `voice_core.cpp` 的配置快照更新中赋值，未找到随后读取它们来改变音频或收听行为的路径：

- `qm_voice_group_mode`
- `qm_voice_filter_enable`
- `qm_voice_comp_threshold`
- `qm_voice_comp_ratio`
- `qm_voice_comp_attack_ms`
- `qm_voice_comp_release_ms`
- `qm_voice_comp_makeup`
- `qm_voice_limiter`

这些不能仅通过新增 UI 就变成有效设置；后续应先决定恢复相应处理还是废弃兼容声明。

### 兼容旧配置

| 配置项 | 处理方式 |
| --- | --- |
| `qm_macos_graphics_diagnostics` | 旧图形诊断别名，兼容到 `qm_graphics_trace` |
| `qm_realtime_websocket_url` | 迁到 `qm_websocket_url` |
| `qm_websocket_allow_insecure_tls` | 遗留选项，当前实现拒绝跳过 TLS 校验 |
| `qm_collision_hitbox_color_freeze`、`qm_collision_hitbox_alpha` | 旧碰撞线模式仍读取；当前卡片控制新 hitbox 模式和对应新参数 |
| `qm_hitbox_show_tees`、`qm_hitbox_show_weapons` | 启动时迁到细分显示开关 |
| `qm_settings_card_order`、`qm_sidebar_card_order`、`qm_sidebar_card_collapsed` | 旧卡片排列、折叠状态，交给现有迁移处理 |

### 自动维护的状态

| 配置项 | 对应操作或职责 |
| --- | --- |
| `qm_global_card_order`、`qm_settings_card_collapsed` | 卡片拖动和折叠操作自动持久化 |
| `qm_settings_card_collapse_migrated`、`qm_card_order_migrated`、`qm_card_layout_version` | 卡片迁移标记和版本 |
| `qm_ui_icon_duotone_secondary_color_migrated`、`qm_nameplate_show_scope_migrated`、`qm_translate_color_alpha_migrated`、`qm_jump_hint_defaults_migrated` | 一次性迁移标记 |
| `qm_launch_count`、`qm_sponsor_nudge_at` | 启动次数和赞助提醒进度 |
| `qm_pie_follow_name`、`qm_pie_follow_clan` | 饼菜单跨服跟随的目标状态 |
| `qm_deepfly_mode` | `binds.cpp` 根据实际绑定检测出的 DF/HDF 状态 |
| `qm_hud_editor_layout` | HUD 编辑器布局的序列化结果 |

## 已有间接入口，不计入遗漏

| 配置 | UI 入口或绑定方式 |
| --- | --- |
| `qm_console_filter_mask` | 本地控制台的日志分类按钮，`console.cpp` → `SetLogFilterMask` |
| `qm_scoreboard_sort_mode` | 计分板顶部排序按钮，`scoreboard.cpp` → `DoSortButton` |
| `qm_netease_hook_enable`、`qm_soda_hook_enable`、`qm_kugou_hook_enable`、`qm_qqmusic_hook_enable`、`qm_spotify_enable` | 歌词卡遍历 `QmMusicHookRegistry`，通过成员指针写配置 |
| `qm_skin_queue_rotate_map`、`qm_dummy_skin_queue_rotate_map` | 皮肤队列中选择服务器预设，`QmCardCatalogTee.cpp` → `ApplySkinQueuePreset` |
| 本体和分身的队列启用、间隔、位置、随机加入 | 队列卡通过 `QueueEnabled`、`QueueInterval`、`QueueIndex`、`QueueRandomJoin` 引用编辑 |
| `qm_jump_hint_text` | HUD 编辑器的跳跃提示文字输入框 |
| `qm_screenshot_watermark_text` | 截图管理器的水印文字输入框 |
| `cl_message_system_gradient`、`cl_message_client_gradient`、`cl_message_highlight_gradient`、`cl_message_team_gradient`、`cl_message_gradient`、`cl_message_friend_gradient` | 聊天外观中的渐变编辑器 `DoMessageGradientLine` |
| `qm_voice_server`、`qm_voice_token`、`qm_voice_input_device`、`qm_voice_output_device` | 语音卡的输入框和设备选择器 |
| 翻译供应商、模型、地址、凭据、提示词和目标语言 | 翻译卡使用 `CLineInput`、供应商选择辅助函数绑定，并非配置未出现在 `&g_Config` 表达式中就没有 UI |
| `player7_*`、`dummy7_*` 的皮肤、部件和颜色 | 0.7 皮肤设置通过 `CSkins7::ms_apSkinNameVariables`、`ms_apSkinVariables`、`ms_apUCCVariables`、`ms_apColorVariables` 绑定 |
| `cl_snd_mute_weapon`、`cl_snd_mute_weapon_switch`、`cl_snd_mute_weapon_noammo`、`cl_snd_mute_hook`、`cl_snd_mute_movement`、`cl_snd_mute_player_state`、`cl_snd_mute_pickup`、`cl_snd_mute_flag`、`cl_snd_mute_mapsound` | 计分板音效分类面板的成员指针表 |
| `ed_align_quads`、`ed_show_quads_rect`、`ed_auto_map_reload`、`ed_layer_selector`、`ed_show_ingame_entities` | 地图编辑器选项弹窗 `src/game/editor/popups.cpp` |
| `cl_back_button_x`、`cl_back_button_y` | 返回按钮本体可拖动，`CUi::DoBackButton` 写回坐标 |
| `cl_spec_auto_sync` | 游戏菜单的相机按钮 → `CCamera::ToggleAutoSpecCamera` |
| `cl_dummy`、`cl_dummy_hammer`、`cl_dummy_copy_moves`、`cl_dummy_control`、`cl_dummy_jump`、`cl_dummy_fire`、`cl_dummy_hook` | 控制页/Bind 卡提供相应动作的按键绑定；它们属于操作状态 |
| `dbg_graphs` | HUD 调试卡的全局切换按键入口 |

## TClient：没有专用入口的有效配置

| 功能 | 配置项 |
| --- | --- |
| 任意分辨率 | `tc_allow_any_res` |
| 鼠标范围与距离 | `tc_limit_mouse_to_screen`、`tc_scale_mouse_distance` |
| 冻结外观 | `tc_color_freeze_darken`、`tc_color_freeze_feet` |
| 预测平滑 | `tc_prediction_margin_smooth` |
| 旧队伍颜色、钩线行为 | `tc_old_team_colors`、`tc_revert_hook_line` |
| 聊天正则过滤 | `tc_regex_chat_ignore` |
| 分身运动信息 | `tc_showhud_dummy_position`、`tc_showhud_dummy_speed`、`tc_showhud_dummy_angle` |
| 时钟秒数 | `tc_show_local_time_seconds` |
| 名牌显示敌对原因 | `tc_warlist_reason` |
| 背景绘制容量与自动保存 | `tc_bg_draw_max_items`、`tc_bg_draw_auto_save_load` |
| 轮盘动画时间 | `tc_animate_wheel_time` |
| 排球外观 | `tc_volleyball_better_ball`、`tc_volleyball_better_ball_skin` |
| 玩家碰撞框 | `tc_show_player_hit_boxes` |
| 管理员瞄准指令 | `tc_mod_weapon`、`tc_mod_weapon_command`，依赖远程控制台认证 |
| 自定义社区数据源 | `tc_custom_communities_url` |
| Discord 状态 | `tc_discord_rpc`，依赖构建中的 Discord 支持 |

这些配置分别由 `controls.cpp`、`players.cpp`、`nameplates.cpp`、`hud.cpp`、`components/tclient/` 和 `src/engine/client/` 消费。

另有 7 个旧项不应直接补普通 UI：`tc_jump_hint`、`tc_jump_hint_text`、`tc_jump_hint_color`、`tc_jump_hint_x`、`tc_jump_hint_y`、`tc_jump_hint_size` 已在 `gameclient.cpp` 迁至 Qm 跳跃提示；`tc_hook_coll_cursor` 的使用代码已注释，当前没有有效消费路径。

## DDNet：没有专用入口的配置

以下保留上游名称；不少属于高级控制台参数。按消费路径分组列出，不建议为了有 UI 而全部塞入常规设置。

### 显示、输入与体验

| 分类 | 配置项 |
| --- | --- |
| 名牌、表情、皮肤外观 | `cl_nameplates_always`、`cl_afk_emote`、`cl_showemotes`、`cl_eye_wheel`、`cl_eye_duration`、`cl_airjumpindicator`、`cl_show_ninja`、`cl_old_gun_position` |
| 流媒体隐私与延迟颜色 | `cl_streamer_mode`、`cl_enable_ping_color` |
| HUD 计时和比赛信息 | `cl_showhud_timer`、`cl_showhud_time_cp_diff`、`cl_showhud_spectator`、`cl_showrecord`、`cl_warning_teambalance` |
| HUD 动画与诊断显示 | `cl_hud_animations`、`cl_hud_animation_speed`、`cl_showpred`、`cl_show_packet_loss` |
| 通知、广播、MOTD | `cl_shownotifications`、`cl_show_broadcasts`、`cl_print_broadcasts`、`cl_print_motd`、`cl_motd_time` |
| 聊天与好友匹配 | `cl_chat_reset`、`cl_friends_ignore_clan` |
| 镜头与缩放 | `cl_mouse_deadzone`、`cl_dyncam_max_distance`、`cl_dyncam_min_distance`、`cl_dyncam_mousesens`、`cl_dyncam_deadzone`、`cl_dyncam_follow_factor`、`cl_multiview_sensitivity`、`cl_multiview_zoom_smoothness`、`cl_smooth_spectating_time`、`cl_smooth_zoom_time`、`cl_limit_max_zoom_level` |
| 旁观鼠标操作 | `cl_spectator_mouseclicks` |
| 分身输入与武器恢复 | `cl_dummy_resetonswitch`、`cl_dummy_restore_weapon` |
| 触屏总开关、返回按钮显示 | `cl_touch_controls`、`cl_back_button`；已启用触屏后的布局编辑不等于这两个开关的编辑入口 |
| 滚动、浏览器着色和菜单行为 | `ui_smooth_scroll_time`、`ui_colorize_ping`、`ui_colorize_gametype`、`ui_close_window_after_changing_setting`、`cl_show_start_menu_images` |
| 旧 UI 基色 | `ui_color`，旧绘制路径仍读取；不同于截图中的 `qm_ui_color` |
| 节日效果 | `events` |
| 旧预测影子 | `cl_unpredicted_shadow`、`cl_unpredicted_shadow_alpha` |

### 预测、网络、资源与后端

| 分类 | 配置项 |
| --- | --- |
| 预测细项 | `cl_predict`、`cl_predict_dummy`、`cl_antiping_limit`、`cl_antiping_percent`、`cl_antiping_smooth`、`cl_antiping_gunfire`、`cl_antiping_preinput`、`cl_predict_freeze` |
| 地图下载 | `cl_map_download_url`、`cl_map_download_connect_timeout_ms`、`cl_map_download_low_speed_limit`、`cl_map_download_low_speed_time` |
| 皮肤缓存与下载源 | `cl_skins_loaded_max`、`cl_skin_download_url`、`cl_skin_community_download_url` |
| 浏览器请求 | `br_location`、`br_max_requests` |
| 连接端口和绑定地址 | `cl_port`、`cl_dummy_port`、`cl_contact_port`、`bindaddr` |
| 断线重连 | `cl_reconnect_timeout`、`cl_reconnect_full`、`conn_timeout`、`conn_resend_requests_per_second` |
| 超时保护身份 | `cl_timeout_code`、`cl_dummy_timeout_code`、`cl_timeout_seed`，由连接路径生成或使用，也允许控制台覆盖 |
| 声音后端 | `cl_threadsoundloading`、`snd_buffer_size`、`snd_rate` |
| 图形调度 | `gfx_asyncrender_old`、`gfx_quad_as_triangle`、`gfx_gl_texture_lod_bias`、`gfx_render_thread_count`、`gfx_display_all_video_modes` |
| 后台刷新 | `cl_refresh_rate_inactive`、`gfx_backgroundrender` |
| 输入后端 | `inp_translated_keys`、`inp_ignored_modifiers`、`inp_ime_native_ui` |
| 地图文字细节 | `gfx_text_overlay`，仍由 `src/game/map/render_map.cpp` 消费 |
| 配置落盘 | `cl_save_settings`，控制退出时是否保存 |

### 回放、编辑器与诊断

| 分类 | 配置项 |
| --- | --- |
| 视频录制行为 | `cl_video_pausewithdemo`、`cl_video_show_hook_coll_other`、`cl_video_show_direction`、`cl_video_show_important_alerts` |
| 视频编码 | `cl_video_crf`、`cl_video_preset`、`cl_video_recorder_fps` |
| 回放提示 | `cl_demo_show_speed`、`cl_demo_show_pause` |
| 自动录像、比赛记录与 ghost | `cl_auto_demo_on_connect`、`cl_race_record_server_control`、`cl_demo_name`、`cl_race_ghost_server_control`、`cl_race_ghost_strict_map` |
| 菜单背景 | `cl_background_video_fps`、`cl_rotation_radius`、`cl_rotation_speed`、`cl_camera_speed` |
| 编辑器自动保存 | `ed_autosave_interval`、`ed_autosave_max` |
| 编辑器缩放 | `ed_smooth_zoom_time`、`ed_limit_max_zoom_level`、`ed_zoom_target` |
| 编辑器按键、文字和撤销容量 | `ed_showkeys`、`cl_text_entities_editor`、`cl_editor_max_history` |
| 日志 | `logfile`、`logappend`、`loglevel`、`stdout_output_level`、`console_output_level`、`console_enable_colors` |
| 调试 | `debug`、`dbg_tuning`、`dbg_http`、`dbg_gfx`、`dbg_render_group_clips`、`dbg_render_quad_clips`、`dbg_render_cluster_clips`、`dbg_render_tile_clips`、`dbg_predict_events`、`gfx_noclip` |
| 测试与自动化接口 | `http_allow_insecure`、`cl_input_fifo` |

### 上游兼容与内部项

- `cl_nameplates`、`cl_nameplates_own`：新名牌显示范围使用 `qm_nameplate_show_scope`，旧值保留作迁移/兼容，不再作为两个独立 UI 开关。
- `cl_menu_panel_color`、`cl_menu_panel_opacity`、`cl_menu_panel_elevated_opacity`、`cl_settings_tabbar_opacity`：未找到有效消费路径，属于保留声明。
- `steam_name`：Steam 资料状态。
- `br_cached_best_serverinfo_url`：服务器列表 URL 缓存，不是可选数据源编辑器。
- `cl_show_welcome`、`cl_race_binds_set`、`cl_config_version`：欢迎页、默认绑定和配置迁移状态。
- `gfx_3d_texture_analysis_ran`、`gfx_3d_texture_analysis_renderer`、`gfx_3d_texture_analysis_version`：图形兼容性分析结果。
- `cl_friends_category_expanded`：好友分组展开状态，通过 UI 操作持久化。

## 截图对应的发布默认值

依据用户保存后的 `qmclient/settings.cfg`，读取时间为 2026-10-02，文件最后写入时间为本地 13:30:31。只提取截图所属视觉设置，没有复制个人凭据、路径或其他偏好。

颜色是项目的打包 HSL/HSLA 配置值，不是 RGB 十六进制。各层透明效果还会与实际背景混合，截图取色不能替代这些原始值。

| 项目 | 配置与默认值 |
| --- | --- |
| 界面表面 | `qm_ui_color = 0x000000`、`qm_ui_opacity = 12` |
| 界面强调色 | `qm_ui_accent_color = 0x5DFE54` |
| 选中项颜色 | `qm_ui_selected_color = 0x8F061D` |
| 设置卡片背景 | `qm_ui_card_color = 0xFF00FF`、`qm_ui_card_opacity = 13` |
| 按钮背景 | `qm_ui_dropdown_color = 0x000000`、`qm_ui_dropdown_opacity = 16` |
| 输入框背景 | `qm_ui_input_color = 0x0000FF`、`qm_ui_input_opacity = 16` |
| 展开列表背景 | `qm_ui_dropdown_list_color = 0x0000FF`、`qm_ui_dropdown_list_opacity = 12` |
| 二级菜单背景 | `qm_ui_popup_color = 0x0000FF`、`qm_ui_popup_opacity = 20` |
| 文本颜色模式 | `qm_ui_text_color_mode = 0`，自动 |
| 焦点环颜色 | `qm_ui_focus_color = 0x97FFA6` |
| 计分板表面 | `qm_scoreboard_color = 0x000000`、`qm_scoreboard_opacity = 16` |
| 背景模糊 | `qm_gaussian_blur = 1`、`qm_blur_mode = 2`，双重 Kawase |
| 卡片边框 | `qm_ui_card_borders = 0`，边框颜色仍为 `qm_ui_card_border_color = 0x1AFFFFFF` |
| 彩虹标题 | `qm_ui_card_rainbow_titles = 1` |

默认值写在 `src/engine/shared/config_variables_qmclient.h`。配置文件明确保存的值继续覆盖默认值；缺省项和恢复默认操作使用这些新值。

## 验证情况

- 已补充头衔卡片的注册/检索协作、布局迁移重载、自定义排列保留和折叠状态测试，并更新受新默认排列影响的既有测试。
- 已从翻译 TOML 生成新增文案的语言文件。
- 本记录基于源码静态审阅，未编译、未执行测试或 gate、未启动客户端进行视觉验证。缩放、滚动、预览和真实搜索交互仍需后续运行时验证。
