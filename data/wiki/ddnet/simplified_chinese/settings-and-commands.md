来源为 DDNet Wiki 贡献者。[原文修订 25455](https://wiki.ddnet.org/wiki/Special:PermanentLink/25455) · [贡献者与编辑历史](https://wiki.ddnet.org/w/index.php?title=Settings%20and%20Commands/zh&action=history) · [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)。QmClient 转换了排版并省略站点导航及媒体，补译处已标注。

## 地图设置

| 设置名称 | 说明 | 默认值 | 最小值 | 最大值 |
| --- | --- | --- | --- | --- |
| `sv_hit` | 玩家能否使用锤子/榴弹发射器/激光枪击中其他玩家 | 1 | 0 | 1 |
| `sv_endless_drag` | 无限钩索是否默认开启 | 0 | 0 | 1 |
| `sv_freeze_delay` | 玩家在触碰冻结区域后会默认冻结多少秒（适用于除开关层设置的冻结区域以及深度冻结区域之外的所有情况） | 3 | 1 | 30 |
| `sv_save_worse_scores` | 是否在已取得的较好记录的前提下保存较差记录 | 1 | 0 | 1 |
| `sv_pauseable` | 玩家是否可以使用spec指令 | 0 | 0 | 1 |
| `sv_team` | 地图组队设置（0 = 禁止组队, 1 = 推荐组队, 2 = 强制组队, 3 = 强制单人） | 1 | 0 | 3 |
| `sv_min_team_size` | 队伍的最小队员数（如果一个队伍完成地图时人数小于该值，则他们不会获得团队排名） | 2 | 1 | 64 |
| `sv_max_team_size` | 队伍的最大队员数（当一个队伍中的队员数已达到该值，会阻止其他玩家加入该队伍） | 64 | 1 | 64 |
| `sv_old_laser` | 玩家是否会被自身发射的霰弹枪激光击中，并将玩家拉向激光的反射点处（所有现代地图应将此项设置为0）。开启会导致霰弹枪激光无法击中发射者自身，并且在击中其他玩家时，其他玩家会被拉向发射者而非激光的反射点处。 | 0 | 0 | 1 |
| `sv_no_weak_hook` | 开启后会使得所有玩家使用钩索和发生碰撞时候展现出强钩特性 | 0 | 0 | 1 |
| `sv_reset_pickups` | 玩家经过起点线后是否重置其所持有的武器 | 0 | 0 | 1 |
| `sv_show_others_default` | 其他玩家默认是否可见（2为己方队伍） | 0 | 0 | 2 |
| `sv_plasma_range` | 等离子炮台侦测玩家的范围 | 700 | 1 | 99999 |
| `sv_plasma_per_sec` | 等离子炮台每秒发射的子弹数量 | 3 | 0 | 50 |
| `sv_dragger_range` | 激光钩爪侦测玩家的范围 | 700 | 1 | 99999 |
| `sv_solo_server` | 设置地图为单人模式（玩家之间除聊天外无法进行任何交互，服务器会在加载地图之前就启用该参数） | 0 | 0 | 1 |
| `sv_old_teleport_weapons` | 所有武器均可用于传送（该参数已弃用，已使用新版实体代替，请勿更改） | 0 | 0 | 1 |
| `sv_old_teleport_hook` | 钩索可穿过传送区域（该参数已弃用，已使用新版实体代替，请勿更改） | 0 | 0 | 1 |
| `sv_teleport_hold_hook` | 玩家被传送后已释放的钩索不断开 | 0 | 0 | 1 |
| `sv_teleport_lose_weapons` | 玩家被传送后重置所有武器（在竞速地图中常用） | 0 | 0 | 1 |
| `sv_deepfly` | 是否允许处于深度冻结状态的玩家在瞬时解冻时使用锤子 | 1 | 0 | 1 |
| `sv_destroy_bullets_on_death` | 玩家死亡时立即清除他们发射的弹幕 | 1 | 0 | 1 |
| `sv_destroy_lasers_on_death` | 玩家死亡时立即清除他们发射的激光 | 0 | 0 | 1 |

| 指令 | 参数 | 说明 |
| --- | --- | --- |
| `tune` | s\[调整内容\] ?i\[数值\] | 调整全地图的默认参数 |
| `toggle_tune` | s\[调整内容\] i\[数值1\] i\[数值2\] | 切换全地图的默认参数 |
| `tune_zone` | i\[区域编号\] s\[调整内容\] i\[数值\] | 调整特定编号区域的参数 |
| `tune_zone_enter` | i\[区域编号\] r\[信息\] | 进入特定区域后向玩家展示一条信息；区域编号0指向常规区域 |
| `tune_zone_leave` | i\[区域编号\] r\[信息\] | 离开特定区域后向玩家展示一条信息；区域编号0指向常规区域 |
| `mapbug` | s\[地图漏洞名称\] | 使用特定bug来开启地图兼容模式（例如：grenade-doubleexplosion@ddnet.tw） |
| `switch_open` | i\[开关编号\] | 设置默认特定开关处于未激活状态（否则处于激活状态） |

## 服务器设置

| 设置名称 | 说明 | 默认值 | 最小值 | 最大值 |
| --- | --- | --- | --- | --- |
| `password` | 进入服务器的密码 | "" |  |  |
| `events` | 启用节日模式（例如在某些节日里玩家的默认表情发生变化，或在圣诞节里获得特殊皮肤） | 1 | 0 | 1 |
| `logfile` | 记录所有输出的文件名 | "" |  |  |
| `logappend` | 每次向日志追加内容，而不是覆写 | 1 | 0 | 1 |
| `loglevel` | 日志文件的信息量 (-3 = 无, -2 = 仅错误, -1 = 警告, 0 = 信息, 1 = 调试, 2 = 跟踪) | 0 | -3 | 2 |
| `stdout_output_level` | 系统控制台的信息量 (-3 = 无, -2 = 仅错误, -1 = 警告, 0 = 信息, 1 = 调试, 2 = 跟踪) | 0 | -3 | 2 |
| `console_output_level` | 调整本地/远程控制台中信息的数量（-3 = 无，-2 = 仅错误，-1 = 警告，0 = 信息，1 = 调试，2 = 跟踪） | 0 | -3 | 2 |
| `console_enable_colors` | 启用控制台输出文本的颜色变化 | 1 | 0 | 1 |
| `sv_name` | 服务器的名字 | "unnamed server" |  |  |
| `bindaddr` | 绑定客户端/服务器的地址 | "" |  |  |
| `sv_ipv4only` | 设置是否只绑定IPv4，否则绑定所有可用接口 | 0 | 0 | 1 |
| `sv_port` | 服务器的端口 (只有位于8303-8310之间的端口才能在局域网中使用，将该项设置为0会自动寻找一个可用端口） | 0 | 0 | 0 |
| `sv_hostname` | 主机名称 (仅0.7版本有效) | "" |  |  |
| `sv_map` | 服务器使用的地图 | "Sunny Side Up" |  |  |
| `sv_max_clients` | 服务器允许的最大玩家数量 | 64 | 1 | 64 |
| `sv_max_clients_per_ip` | 服务器允许同一IP地址下的最大玩家数量 | 4 | 1 | 64 |
| `sv_high_bandwidth` | 使用高带宽模式，服务器所需的带宽增加一倍。仅在局域网中使用 | 0 | 0 | 1 |
| `sv_register` | 将服务器注册至官方服务器上，并公开服务器（可以在搜索列表中搜索到该服务器） | "1" |  |  |
| `sv_register_extra` | 发送至注册端点（主服务器）的额外标头，格式如“标头1: 值1, 标头2: 值2” | "" |  |  |
| `sv_register_url` | 用于注册至列表的主服务器网址 | "[https://master1.ddnet.org/ddnet/15/register](https://master1.ddnet.org/ddnet/15/register)" |  |  |
| `sv_rcon_password` | 一级管理员远程控制台密码（拥有全部权限） | "" |  |  |
| `sv_rcon_mod_password` | 二级管理员远程控制台密码（拥有部分权限） | "" |  |  |
| `sv_rcon_helper_password` | 三级管理员远程控制台密码（拥有部分权限） | "" |  |  |
| `sv_rcon_max_tries` | 登录远程控制台的最大尝试次数 | 30 | 0 | 100 |
| `sv_rcon_bantime` | 当登录远程控制台超过次数上限时封禁该玩家的时长（单位为分钟），0代表仅踢出该玩家 | 5 | 0 | 1440 |
| `sv_auto_demo_record` | 自动录制回放 | 0 | 0 | 1 |
| `sv_auto_demo_max` | 自动录制回放的最大数量（0表示无限制） | 10 | 0 | 1000 |
| `sv_tee_historian` | 开启后会将玩家完整的游戏流程记录在磁盘当中（注意：启用该项会占用磁盘相当大部分的空间） | 0 | 0 | 1 |
| `sv_vanilla_antispoof` | 启用原生Antispoof（反混淆） | 1 | 0 | 1 |
| `sv_dnsbl` | 启用DNSBL（域名系统黑名单） | 0 | 0 | 1 |
| `sv_dnsbl_host` | 用于IP验证的DNSBL提供者的主机名 | "" |  |  |
| `sv_dnsbl_key` | 特定DNSBL提供者的可选认证密钥 | "" |  |  |
| `sv_dnsbl_vote` | 阻止黑名单地址进行投票 | 0 | 0 | 1 |
| `sv_dnsbl_ban` | 自动封禁黑名单地址 | 0 | 0 | 1 |
| `sv_dnsbl_chat` | 禁止黑名单地址进行聊天 | 0 | 0 | 1 |
| `sv_rcon_vote` | 仅允许已认证客户端进行投票 | 0 | 0 | 1 |
| `sv_player_demo_record` | 为每名玩家自动录制回放 | 0 | 0 | 1 |
| `sv_demo_chat` | 将聊天内容添加至回放中 | 0 | 0 | 1 |
| `sv_server_info_per_second` | 每秒发送完整服务器信息响应的最大数量（0表示没有限制） | 50 | 0 | 10000 |
| `sv_van_conn_per_second` | 反混淆的限流框架（0表示没有限制） | 10 | 0 | 10000 |
| `sv_sixup` | 启用sixup连接 | 1 | 0 | 1 |
| `sv_skill_level` | 为Teeworls 0.7设置难度等级（0：休闲模式，1：普通模式，2：经济模式） | 1 | 0 | 2 |
| `debug` | Debug模式（调试模式） | 0 | 0 | 1 |
| `dbg_curl` | 调试曲率 | 0 | 0 | 1 |
| `dbg_stress` | Stress systems（仅限调试版本） | 0 | 0 | 0 |
| `http_allow_insecure` | 除安全的HTTPS协议外，允许使用不安全的HTTP协议（主要用于测试） | 0 | 0 | 1 |
| `sv_welcome` | 当然玩家加入服务器时展示的欢迎信息 | "" |  |  |
| `sv_reserved_slots` | 为特定玩家保留的人数位置数量 | 0 | 0 | 64 |
| `sv_reserved_slots_pass` | 加入为特定玩家保留的人数位置所需的密码 | "" |  |  |
| `sv_reserved_slots_auth_level` | 使用为特定玩家保留的人数位置所需的最低rcon权限级别（4 = rcon授权已禁用） | 1 | 1 | 4 |
| `sv_hit` | 玩家之间是否可以用锤子/榴弹炮/激光进行交互 | 1 | 0 | 1 |
| `sv_endless_drag` | 是否默认开启无限钩索 | 0 | 0 | 1 |
| `sv_test_cmds` | 是否启用测试指令/作弊指令（该选项仅在初始配置中生效） | 0 | 0 | 1 |
| `sv_freeze_delay` | 玩家进入冻结状态后自行解除冻结所需的时间（适用于除了开关层的冻结区域和深度冻结区域以外的所有冻结区域） | 3 | 1 | 30 |
| `sv_endless_super_hook` | 超管玩家是否开启无限钩索 | 0 | 0 | 1 |
| `sv_hide_score` | 是否公布玩家成绩 | 0 | 0 | 1 |
| `sv_save_worse_scores` | 是否在玩家已拥有更好成绩的情况下用较差成绩将其覆盖 | 1 | 0 | 1 |
| `sv_pauseable` | 玩家是否可以使用spec指令 | 0 | 0 | 1 |
| `sv_pause_messages` | 是否在玩家使用spec时向其展示信息 | 0 | 0 | 1 |
| `sv_pause_frequency` | 两次使用spec的最短时间间隔 | 1 | 0 | 9999 |
| `sv_invite` | 是否允许玩家邀请其他玩家入队 | 1 | 0 | 1 |
| `sv_invite_frequency` | 两次邀请之间的最短时间间隔 | 1 | 0 | 9999 |
| `sv_tele_others_auth_level` | 传送至其他玩家身上时需要的身份验证级别 | 1 | 1 | 3 |
| `sv_emotional_tees` | 玩家发送表情时，面部表情是否随之改变（1：改变，0：不变，-1：完全不变） | 1 | -1 | 1 |
| `sv_emoticon_ms_delay` | 玩家发送表情的最低间隔（毫秒） | 3000 | 20 | 999999999 |
| `sv_global_emoticon_ms_delay` | 玩家发送全局表情最低间隔的毫秒数（此配置需大于或等于 sv\_emoticon\_ms\_delay 才能生效） | 3000 | 20 | 999999999 |
| `sv_eye_emote_change_delay` | 两次面部表情发生变化的最短时间间隔 | 1 | 0 | 9999 |
| `sv_chat_delay` | 两次聊天的最短时间间隔 | 1 | 0 | 9999 |
| `sv_team_change_delay` | 两次玩家改变所属队伍的最短时间间隔 | 3 | 0 | 9999 |
| `sv_info_change_delay` | 两次玩家信息刷新的最短时间间隔（包括名称，皮肤，颜色），为避免彩虹tee泛滥，应将其设置为很高的数值 | 5 | 0 | 9999 |
| `sv_vote_time` | 投票的持续时间 | 25 | 1 | 9999 |
| `sv_vote_map_delay` | 两次地图投票的最短时间间隔 | 0 | 0 | 9999 |
| `sv_vote_delay` | 两次任意投票的最短时间间隔 | 3 | 0 | 9999 |
| `sv_vote_kick_delay` | 两次投票移除玩家的最短时间间隔 | 0 | 0 | 9999 |
| `sv_vote_yes_percentage` | 投票生效或失效所需要的对应投票玩家所占百分比 | 50 | 1 | 100 |
| `sv_vote_majority` | 是否开启投票的反对选项，默认开启 | 0 | 0 | 1 |
| `sv_vote_max_total` | 一次投票可参与的最多人数（0代表无限制） | 0 | 0 | 64 |
| `sv_vote_veto_time` | 玩家加入服务器多久后才可以进行地图投票（0表示禁用） | 20 | 0 | 1000 |
| `sv_kill_delay` | 两次使用kill的最短时间间隔 | 1 | 0 | 9999 |
| `sv_map_window` | 地图下载预发送的数据块数量 | 15 | 0 | 100 |
| `sv_fast_download` | 启用快速下载地图 | 1 | 0 | 1 |
| `sv_shotgun_bullet_sound` | 是否开启疯狂的霰弹枪子弹音效（？） | 0 | 0 | 1 |
| `sv_region_name` | 服务器所属区域，用于区域禁令 | "UNK" |  |  |
| `sv_sql_servername` | 插入到记录表中的SQL服务器名称 | "UNK" |  |  |
| `sv_savegames` | 是否启用save和load指令 | 1 | 0 | 1 |
| `sv_saveswapgames_delay` | 在玩家读取游戏存档或与其他玩家交换位置前的冷却时间 | 30 | 0 | 10000 |
| `sv_saveswapgames_penalty` | 玩家在读取存档或与其他玩家交换位置后所获得的惩罚时长 | 60 | 0 | 10000 |
| `sv_swap_timeout` | 连接超时后可持续存留在服务器里的最长时间 | 180 | 0 | 10000 |
| `sv_swap` | 是否启用swap指令 | 1 | 0 | 1 |
| `sv_use_sql` | 启用MySQL后端替代SQLite后端（当无法连接MySQL服务器时，sv\_sqlite\_file 仍将作为备用写入服务器使用） | 0 | 0 | 1 |
| `sv_sql_queries_delay` | 单个玩家SQL查询之间的延迟秒数 | 1 | 0 | 20 |
| `sv_sqlite_file` | 用于存储排名的文件，适用于sv\_use\_sql关闭或作为备用SQL服务器的情况 | "ddnet-server.sqlite" |  |  |
| `sv_use_upnp` | 启用UPnP支持（编译时需指定-DCONF\_UPNP=ON参数） | 0 | 0 | 1 |
| `sv_ddrace_rules` | 是否显示默认规则 | 1 | 0 | 1 |
| `sv_rules_line1` | 第一行规则 | "" |  |  |
| `sv_rules_line2` | 第二行规则 | "" |  |  |
| `sv_rules_line3` | 第三行规则 | "" |  |  |
| `sv_rules_line4` | 第四行规则 | "" |  |  |
| `sv_rules_line5` | 第五行规则 | "" |  |  |
| `sv_rules_line6` | 第六行规则 | "" |  |  |
| `sv_rules_line7` | 第七行规则 | "" |  |  |
| `sv_rules_line8` | 第八行规则 | "" |  |  |
| `sv_rules_line9` | 第九行规则 | "" |  |  |
| `sv_rules_line10` | 第十行规则 | "" |  |  |
| `sv_team` | 地图组队设置（0 = 禁止组队, 1 = 推荐组队, 2 = 强制组队, 3 = 强制单人） | 1 | 0 | 3 |
| `sv_min_team_size` | 队伍的最小人数（少于该人数的队伍完成地图不会获得队伍排名） | 2 | 1 | 64 |
| `sv_max_team_size` | 队伍的最大人数 | 64 | 1 | 64 |
| `sv_map_vote` | 是否允许使用map指令更换地图 | 1 | 0 | 1 |
| `sv_announcement_filename` | 定时公告内容文件，每条公告占一行 | "announcement.txt" |  |  |
| `sv_announcement_interval` | 每条定时公告发送的时间间隔 | 300 | 1 | 9999 |
| `sv_announcement_random` | 定时公告是否按顺序发布 | 1 | 0 | 1 |
| `sv_old_laser` | 若你射击激光霰弹，它们是否会击中你并把你拉向反弹起点（所有新地图默认为0），或者射击后无法击中自己，且会把其他人拉向射击者 | 0 | 0 | 1 |
| `sv_slash_me` | 服务器是否启用/me指令 | 0 | 0 | 1 |
| `sv_rejoin_team_0` | 完成地图后返回队伍0（仅未锁定队伍时） | 1 | 0 | 1 |
| `sv_no_weak_hook` | 是否采用替代计算方式处理世界tick，使所有玩家的钩子都表现为强钩 | 0 | 0 | 1 |
| `conn_timeout` | 网络超时时间 | 100 | 5 | 1000 |
| `conn_timeout_protection` | 网络超时保护 | 1000 | 5 | 10000 |
| `sv_reset_pickups` | 是否在通过起点时重置武器 | 0 | 0 | 1 |
| `sv_show_others` | 玩家能否使用showothers命令 | 1 | 0 | 1 |
| `sv_show_others_default` | 默认是否可见其他玩家（己方队伍为2） | 0 | 0 | 2 |
| `sv_show_all_default` | 玩家默认是否能看到所有tee | 0 | 0 | 1 |
| `sv_max_afk_time` | 玩家在几秒后变成afk状态（0 = 关闭） | 300 | 0 | 9999 |
| `sv_plasma_range` | 等离子炮台侦测玩家的范围 | 700 | 1 | 99999 |
| `sv_plasma_per_sec` | 等离子炮台每秒发射的子弹数量 | 3 | 0 | 50 |
| `sv_dragger_range` | 激光钩爪侦测玩家的范围 | 700 | 1 | 99999 |
| `sv_vote_pause` | 允许投票暂停玩家（而非转为旁观者） | 1 | 0 | 1 |
| `sv_vote_pause_time` | 玩家被投票暂停后暂停的秒数 | 10 | 0 | 360 |
| `sv_tune_reset` | 是否在更换地图时重置tune | 1 | 0 | 1 |
| `sv_reset_file` | 在地图变更或重新加载服务器时执行的文件 | "reset.cfg" |  |  |
| `sv_input_fifo` | 用作服务器控制台输入的FIFO文件（非Windows）或命名管道（Windows） | "" |  |  |
| `sv_ddrace_tune_reset` | 是否在更换地图时重置DDRace tune（sv\_hit、sv\_endless\_drag与sv\_old\_laser） | 1 | 0 | 1 |
| `sv_nameless_score` | nameless tee是否有得分 | 1 | 0 | 1 |
| `sv_time_in_broadcast_interval` | 多久更新一次广播时间 | 1 | 0 | 60 |
| `sv_default_timer_type` | 默认时间显示方式（0 = 游戏/回合计时器，1 = 广播，2 = 全部，3 = 不显示） | 0 | 0 | 3 |
| `sv_chat_initial_delay` | 首次发消息前的等待秒数 | 0 | 0 | 360 |
| `sv_chat_penalty` | 每发送一条消息，聊天分数将增加此数值，并在每刻减少1分 | 250 | 50 | 1000 |
| `sv_chat_threshold` | 若聊天分数达到此数值，玩家将会被禁言sv\_spam\_mute\_duration秒 | 1000 | 50 | 10000 |
| `sv_spam_mute_duration` | 玩家触发刷屏禁言时被禁言的秒数（0 = 关闭） | 60 | 0 | 3600 |
| `sv_shutdown_when_empty` | 当服务器无玩家时关闭服务器 | 0 | 0 | 1 |
| `sv_reload_when_empty` | 当服务器无玩家时重新加载地图（1 = 仅重新加载一次，2 = 每次服务器为空时重新加载） | 0 | 0 | 2 |
| `sv_kill_protection` | 过起点后触发防误触自杀保护的最低时间，0 - 关闭，1-9999分钟 | 20 | 0 | 9999 |
| `sv_solo_server` | 将服务器设置为单人模式（玩家间无互动，必须在加载地图前设置） | 0 | 0 | 1 |
| `sv_client_suggestion` | 当玩家使用非DDNet客户端游玩时对其显示的广播 | "Get DDNet client from DDNet.org to use all features on DDNet!" |  |  |
| `sv_client_suggestion_old` | 当玩家使用旧版DDNet客户端游玩时对其显示的广播 | "Your DDNet client is old, update it on DDNet.org!" |  |  |
| `sv_client_suggestion_bot` | 当玩家使用已知的机器人客户端时对其显示的广播 | "Your client has bots and can be remotely controlled!\\nPlease use another client like DDNet client from DDNet.org" |  |  |
| `sv_banned_versions` | 加入时会被踢出的被禁客户端的逗号分隔列表 | "" |  |  |
| `sv_netlimit` | 网络限制：客户端允许使用的最大流量（单位：kb/s） | 0 | 0 | 10000 |
| `sv_netlimit_alpha` | 网络限制：指数移动平均的Alpha平滑系数 | 50 | 1 | 100 |
| `sv_connlimit` | 连接限制：同一IP地址在指定时间段内允许建立的连接数 | 5 | 0 | 100 |
| `sv_connlimit_time` | 连接限制：IP连接计数的时间间隔 | 20 | 0 | 1000 |
| `sv_conn_logging_server` | 用于IP地址日志记录的Unix套接字服务器（仅Unix） | "" |  |  |
| `sv_warmup` | 热身时间（秒） | 0 | 0 | 0 |
| `sv_motd` | 服务器公告 | "" |  |  |
| `sv_tournament_mode` | 锦标赛模式，玩家加入服务器时默认为旁观者 | 0 | 0 | 1 |
| `sv_spamprotection` | 刷屏保护 | 1 | 0 | 1 |
| `sv_spectator_slots` | 旁观者席位预留数量 | 0 | 0 | 64 |
| `sv_inactivekick_time` | 判为挂机的等待时间（分钟） | 0 | 0 | 1000 |
| `sv_inactivekick` | 不活跃玩家的处理方式（0＝移至观察者，1＝移至空闲观察者位或踢出，2＝踢出） | 0 | 0 | 2 |
| `sv_strict_spectate_mode` | 限制旁观者模式下可获取的信息 | 0 | 0 | 1 |
| `sv_vote_spectate` | 允许通过投票将玩家移至旁观 | 1 | 0 | 1 |
| `sv_vote_spectate_rejoindelay` | 玩家被投票移至旁观后，需等待多少分钟才可重新加入 | 3 | 0 | 1000 |
| `sv_vote_kick` | 允许投票踢出玩家 | 1 | 0 | 1 |
| `sv_vote_kick_min` | 发起踢出投票所需的最低玩家数量 | 0 | 0 | 64 |
| `sv_vote_kick_bantime` | 玩家被投票踢出后封禁的秒数，设为0则仅踢出（kick） | 5 | 0 | 1440 |
| `sv_join_vote_delay` | 刚加入的玩家在发起投票或参与踢出/移至旁观投票前需要等待的秒数 | 300 | 0 | 1000 |
| `sv_old_teleport_weapons` | 传送所有武器（已弃用，请改用特殊图块） | 0 | 0 | 1 |
| `sv_old_teleport_hook` | 传送钩索（已弃用，请改用特殊图块） | 0 | 0 | 1 |
| `sv_teleport_hold_hook` | 传送后保持钩索状态 | 0 | 0 | 1 |
| `sv_teleport_lose_weapons` | 传送后卸掉武器（适用于一些竞速地图） | 0 | 0 | 1 |
| `sv_deepfly` | 允许在深度冻结后被短暂唤醒（如锤击、激光）时开火 | 1 | 0 | 1 |
| `sv_destroy_bullets_on_death` | 销毁死亡者发出的子弹 | 1 | 0 | 1 |
| `sv_destroy_lasers_on_death` | 销毁死亡者发射的激光 | 0 | 0 | 1 |
| `sv_mapupdaterate` | 64 位玩家 ID 与原版 ID 映射的更新频率 | 5 | 1 | 100 |
| `sv_server_type` | 服务器的类型（novice、moderate…） | "none" |  |  |
| `sv_send_votes_per_tick` | 每刻向客户端发送的投票选项数量 | 5 | 1 | 15 |
| `sv_rescue` | 允许使用/rescue命令，这能让玩家将自己传送出冻结（此设置仅在初始配置中生效） | 0 | 0 | 1 |
| `sv_rescue_delay` | 救援命令间隔秒数 | 1 | 0 | 1000 |
| `sv_practice` | 为队伍启用练习模式；这意味着可以使用/rescue命令，但无法获得成绩 | 1 | 0 | 1 |
| `dbg_dummies` | （仅调试构建） | 0 | 0 | 64 |

## Econ设置

| 设置（QmClient 补译） | 简介 | 默认 | 最小值 | 最大值 |
| --- | --- | --- | --- | --- |
| `ec_bindaddr` | 外部控制台地址；绑定为“localhost”以外的任何地址均有风险 | "localhost" |  |  |
| `ec_port` | 外部控制台端口 | 0 | 0 | 0 |
| `ec_password` | 外部控制台密码 | "" |  |  |
| `ec_bantime` | 外部控制台身份验证失败后被封禁的分钟数；设为0则仅断开连接 | 0 | 0 | 1440 |
| `ec_auth_timeout` | 外部控制台身份验证超时时间 | 30 | 1 | 120 |
| `ec_output_level` | 调整外部控制台中显示的日志等级 | 1 | 0 | 2 |

## 服务器指令

| 指令 | 参数 | 简介 |
| --- | --- | --- |
| `echo` | r\[文本\] | 聊天框输出文本 |
| `exec` | r\[文件名\] | 运行指定配置文件 |
| `reset` | s\[配置名\] | 重置指定配置项的值 |
| `toggle` | s\[配置项\] i\[值1\] i\[值2\] | 在两个值中切换配置项的值 |
| `access_level` | s\[指令\] ?i\[权限等级\] | 指定命令所需的权限（管理员 = 0，版主 = 1，辅助 = 2，全部 = 3） |
| `access_status` | i\[权限级别\] | 列出管理员（0）、版主（1）、助手（2）或所有人（3）可使用的命令 |
| `cmdlist` |  | 列出普通用户可使用的所有命令 |
| `ban` | ID\] ?i\[分钟\] r\[原因\] | 因"任意"原因封禁指定 IP 一段时间（分钟） |
| `ban_range` | s\[起始 IP\] s\[结束 IP\] ?i\[分钟\] r\[原因\] 封禁指定 IP 范围 可选:指定封禁时长（分钟）及原因 | 因"任意"原因封禁指定 IP 地址范围一段时间（分钟） |
| `unban` | 封禁列表条目\] | 解除指定 IP 或封禁列表条目的封禁。 |
| `unban_range` | s\[first ip\] s\[last ip\] | 解除指定 IP 地址范围的封禁。 |
| `unban_all` |  | 解除全部封禁。 |
| `bans` | ?i\[page\] | 显示封禁列表（默认从0开始，每页 20 条记录） |
| `bans_save` | s\[file\] | 保存黑名单到一个文件里 |
| `ban` | s\[ip\|id\] ?i\[minutes\] r\[reason\] | 通过 IP/客户端 ID 将玩家以"任意"理由封禁指定时长(单位每分钟) |
| `ban_region` | s\[region\] s\[ip\|id\] ?i\[minutes\] r\[reason\] | 禁止某个地区的玩家（QmClient 补译） |
| `ban_region_range` | s\[region\] s\[first ip\] s\[last ip\] ?i\[minutes\] r\[reason\] | 封禁指定区域内的 IP 段（QmClient 补译） |
| `kick` | i\[id\] ?r\[reason\] | 以"任意"理由将指定 ID 的玩家踢出 |
| `status` | ?r\[name\] | 列出包含指定名称的玩家，或列出所有玩家 |
| `shutdown` | ?r\[reason\] | 关闭（QmClient 补译） |
| `logout` |  | 退出 rcon 登录（QmClient 补译） |
| `show_ips` | ?i\[show\] | 在 rcon 命令中显示 IP 地址（1 = 打开，0 = 关闭）（QmClient 补译） |
| `record` | ?s\[file\] | 记录到一个文件 |
| `stoprecord` |  | 停止记录 |
| `reload` |  | 重新加载地图 |
| `add_sqlserver` | s\['r'\|'w'\] s\[Database\] s\[Prefix\] s\[User\] s\[Password\] s\[IP\] i\[Port\] ?i\[SetUpDatabase ?\] | 添加一个 SQL 服务器（QmClient 补译） |
| `dump_sqlservers` | s\['r'\|'w'\] | 导出全部 SQL 服务器（readservers = r, writeservers = w）（QmClient 补译） |
| `auth_add` | s\[ident\] s\[level\] r\[pw\] | 添加 rcon 键（QmClient 补译） |
| `auth_add_p` | s\[ident\] s\[level\] s\[hash\] s\[salt\] | 添加预哈希的 rcon 密钥（QmClient 补译） |
| `auth_change` | s\[ident\] s\[level\] r\[pw\] | 更新 rcon 密钥（QmClient 补译） |
| `auth_change_p` | s\[ident\] s\[level\] s\[hash\] s\[salt\] | 使用预哈希数据更新 rcon 密钥（QmClient 补译） |
| `auth_remove` | s\[ident\] | 删除 rcon 密钥（QmClient 补译） |
| `auth_list` |  | 列出所有 rcon 键（QmClient 补译） |
| `name_ban` | s\[name\] ?i\[distance\] ?i\[is\_substring\] ?r\[reason\] | 禁止某个昵称（QmClient 补译） |
| `name_unban` | s\[name\] | 解封某个昵称（QmClient 补译） |
| `name_bans` |  | 列出所有被 ban 的名字 |
| `tune` | s\[tuning\] ?i\[value\] | 将变量调到指定值，或显示当前值（QmClient 补译） |
| `toggle_tune` | s\[tuning\] i\[value 1\] i\[value 2\] | 切换调整变量（QmClient 补译） |
| `tune_reset` | ?s\[tuning\] | 将全部或单个调参变量重置为默认值（QmClient 补译） |
| `tunes` |  | 列出所有调整变量及其值（QmClient 补译） |
| `tune_zone` | i\[zone\] s\[tuning\] i\[value\] | 将区域 a 的变量调到指定值（QmClient 补译） |
| `tune_zone_dump` | i\[zone\] | 导出区域 x 的调参配置（QmClient 补译） |
| `tune_zone_reset` | ?i\[zone\] | 将指定区域的参数恢复为默认值，也可重置所有区域（QmClient 补译） |
| `tune_zone_enter` | i\[zone\] r\[message\] | 设置进入指定区域时显示的消息。普通区域用 0（QmClient 补译） |
| `tune_zone_leave` | i\[zone\] r\[message\] | 设置离开指定区域时显示的消息。普通区域用 0（QmClient 补译） |
| `mapbug` | s\[mapbug\] | 使用指定 bug 启用地图兼容模式（例如：grenade-doubleexplosion@ddnet.tw）（QmClient 补译） |
| `switch_open` | i\[switch\] | 开关是否默认禁用（否则激活）（QmClient 补译） |
| `pause_game` |  | 暂停/恢复游戏（QmClient 补译） |
| `change_map` | ?r\[map\] | 切换地图 |
| `random_map` | ?i\[stars\] | 随机地图 |
| `random_unfinished_map` | ?i\[stars\] | 随机未完成的地图 |
| `restart` | ?i\[seconds\] | 在 x 秒后重新启动（0 = 中止）（QmClient 补译） |
| `broadcast` | r\[message\] | 广播消息（QmClient 补译） |
| `say` | r\[message\] | 在聊天中说（QmClient 补译） |
| `set_team` | i\[id\] i\[team-id\] ?i\[delay in minutes\] | 将指定玩家移入目标队伍（QmClient 补译） |
| `set_team_all` | i\[team-id\] | 将所有玩家移入目标队伍（QmClient 补译） |
| `add_vote` | s\[name\] r\[command\] | 添加投票选项（QmClient 补译） |
| `remove_vote` | r\[name\] | 删除投票选项（QmClient 补译） |
| `force_vote` | s\[name\] s\[command\] ?r\[reason\] | 强制投票选项（QmClient 补译） |
| `clear_votes` |  | 清除投票选项（QmClient 补译） |
| `add_map_votes` |  | 自动为所有地图添加投票选项（QmClient 补译） |
| `vote` | r\['yes'\|'no'\] | 强制投票赞成/反对（QmClient 补译） |
| `dump_antibot` |  | 导出反机器人状态（QmClient 补译） |
| `dbg_lognetwork` |  | 记录网络（QmClient 补译） |
| `kill_pl` | v\[id\] | 让指定玩家死亡并向全服播报（QmClient 补译） |
| `totele` | i\[number\] | 将自己传送到指定编号的传送点（QmClient 补译） |
| `totelecp` | i\[number\] | 将自己传送到指定编号的检查点传送点（QmClient 补译） |
| `tele` | ?i\[id\] ?i\[id\] | 将玩家 i（或你）传送到玩家 i（或将你传送到你所看的位置）（QmClient 补译） |
| `addweapon` | i\[weapon-id\] | 给你 id 为 i 的武器（全部 = -1，锤子 = 0，枪 = 1，霰弹枪 = 2，榴弹枪 = 3，激光 = 4，忍者 = 5）（QmClient 补译） |
| `removeweapon` | i\[weapon-id\] | 从你身上移除 ID 为 i 的武器（全部 = -1，锤子 = 0，枪 = 1，霰弹枪 = 2，榴弹枪 = 3，激光 = 4，忍者 = 5）（QmClient 补译） |
| `shotgun` |  | 给你一把霰弹枪（QmClient 补译） |
| `grenade` |  | 获得榴弹发射器（QmClient 补译） |
| `laser` |  | 给予你激光枪（QmClient 补译） |
| `rifle` |  | 给予你激光枪（QmClient 补译） |
| `jetpack` |  | 给你一个喷气背包 |
| `weapons` |  | 给你所有武器 |
| `unshotgun` |  | 从你身上拿走霰弹枪（QmClient 补译） |
| `ungrenade` |  | 移除榴弹发射器（QmClient 补译） |
| `unlaser` |  | 移除你的激光枪 |
| `unrifle` |  | 移除你的激光枪 |
| `unjetpack` |  | 移除你身上的喷气背包 |
| `unweapons` |  | 移除你身上的所有武器 |
| `ninja` |  | 让你成为忍者 |
| `unninja` |  | 移除你的忍者状态 |
| `super` |  | 让你超级（QmClient 补译） |
| `unsuper` |  | 移除你的超级状态（QmClient 补译） |
| `endless_hook` |  | 给你无限钩 |
| `unendless_hook` |  | 把你的无限钩移除 |
| `solo` |  | 把你放在单人区域里面 |
| `unsolo` |  | 把你放出单人区域 |
| `deep` |  | 把你放进深度冻结 |
| `undeep` |  | 让你离开深度冻结 |
| `livefreeze` |  | 使你进入存活冻结状态（QmClient 补译） |
| `unlivefreeze` |  | 让你解除存活冻结（QmClient 补译） |
| `left` | ?i\[tiles\] | 让你向左移动 1 个方块（QmClient 补译） |
| `right` | ?i\[tiles\] | 让你向右移动 1 个方块（QmClient 补译） |
| `up` | ?i\[tiles\] | 让你向上移动 1 个方块（QmClient 补译） |
| `down` | ?i\[tiles\] | 让你向下移动 1 个方块（QmClient 补译） |
| `move` | i\[x\] i\[y\] | 移动到 x/y 编号 ii 的图块（QmClient 补译） |
| `move_raw` | i\[x\] i\[y\] | 移动到 x/y 坐标 ii 的点（QmClient 补译） |
| `force_pause` | v\[id\] i\[seconds\] | 强制 ID 为 i 的玩家暂停 i 秒（QmClient 补译） |
| `force_unpause` | v\[id\] | 将 i 的强制暂停计时器设置为 0。（QmClient 补译） |
| `set_team_ddr` | v\[id\] i\[team\] | 为指定玩家设置 DDRace 队伍（QmClient 补译） |
| `uninvite` | v\[id\] i\[team\] | 取消对玩家的队伍邀请（QmClient 补译） |
| `vote_mute` | v\[id\] i\[seconds\] ?r\[reason\] | 在指定秒数内禁止指定玩家投票（QmClient 补译） |
| `vote_unmute` | v\[id\] | 恢复指定玩家的投票资格（QmClient 补译） |
| `vote_mutes` |  | 列出当前仍在生效的投票禁令（QmClient 补译） |
| `mute` |  |  |
| `muteid` | v\[id\] i\[seconds\] ?r\[reason\] | 按玩家 ID 禁言（QmClient 补译） |
| `muteip` | s\[ip\] i\[seconds\] ?r\[reason\] | 按 IP 地址静音玩家（QmClient 补译） |
| `unmute` | i\[muteid\] |  |
| `unmuteid` | v\[id\] | 按玩家 ID 解除禁言（QmClient 补译） |
| `mutes` |  |  |
| `moderate` |  | 启用/禁用该玩家的活跃管理员模式（QmClient 补译） |
| `vote_no` |  |  |
| `save_dry` |  | 导出当前存档字符串（QmClient 补译） |
| `dump_log` | ?i\[seconds\] | 显示最后 i 秒的日志（QmClient 补译） |
| `freezehammer` | v\[id\] | 给指定玩家一把冻结锤（QmClient 补译） |
| `unfreezehammer` | v\[id\] | 移除指定玩家的冻结锤（QmClient 补译） |

## 聊天框指令

| 指令 | 参数 | 说明 |
| --- | --- | --- |
| `credits` |  | 显示DDNet模组的制作者名单 |
| `rules` |  | 显示服务器规则 |
| `emote` | ?s\[表情名称\] i\[持续秒数\] | 设置Tee的眼部表情 |
| `eyeemote` | ?s\['on'\|'off'\|'toggle'\] | 打开/关闭标准眼睛表情的使用，eyeemote s，其中 s = on 表示打开，off 表示关闭，toggle 表示切换，不显示当前状态（QmClient 补译） |
| `settings` | ?s\[configname\] | 显示这个服务器的游玩信息 |
| `help` | ?r\[command\] | 显示命令 r 的帮助，如果留空则显示一般帮助（QmClient 补译） |
| `info` |  | 显示此服务器的信息（QmClient 补译） |
| `list` | ?s\[filter\] | 列出在线玩家，可选不区分大小写的子串筛选（QmClient 补译） |
| `me` | r\[message\] | 像 IRC 中的同名指令一样，发送 `/me says hi` 后会显示 `<你的名字> says hi`（QmClient 补译） |
| `w` | s\[player name\] r\[message\] | 向某人发送私聊消息（QmClient 补译） |
| `whisper` | s\[player name\] r\[message\] | 向某人发送私聊消息（QmClient 补译） |
| `c` | r\[message\] | 与上一个私聊对象继续对话（私聊）（QmClient 补译） |
| `converse` | r\[message\] | 与上一个私聊对象继续对话（私聊）（QmClient 补译） |
| `pause` | ?r\[player name\] | 切换暂停（QmClient 补译） |
| `spec` | ?r\[player name\] | 切换旁观（如果不可用，则表现为 /pause）（QmClient 补译） |
| `pausevoted` |  | 切换当前投票玩家的暂停状态（QmClient 补译） |
| `specvoted` |  | 切换当前投票玩家的旁观（QmClient 补译） |
| `dnd` |  | 切换请勿打扰（无聊天和服务器消息）（QmClient 补译） |
| `mapinfo` | ?r\[map\] | 显示有关名称为 r 的地图的信息（默认为当前地图）（QmClient 补译） |
| `timeout` | ?s\[code\] | 设置超时保护码（QmClient 补译） |
| `practice` | ?i\['0'\|'1'\] | 为当前队伍开启练习功能（此处仅指 `/rescue`）。本轮成绩不会进入排行榜（QmClient 补译） |
| `swap` | ?r\[player name\] | 请求与另一名队友交换 Tee（QmClient 补译） |
| `save` | ?r\[code\] | 将队伍保存为代码 r（QmClient 补译） |
| `load` | ?r\[code\] | 使用代码 r 载入；可用 /load 查看已有存档（QmClient 补译） |
| `map` | ?r\[map\] | 按名称对地图进行投票（QmClient 补译） |
| `rankteam` | ?r\[player name\] | 显示名称为 r 的玩家队伍排名（默认你的队伍排名）（QmClient 补译） |
| `teamrank` | ?r\[player name\] | 显示名称为 r 的玩家队伍排名（默认你的队伍排名）（QmClient 补译） |
| `rank` | ?r\[player name\] | 显示名称为 r 的玩家排名（默认你的排名）（QmClient 补译） |
| `top5team` | ?s\[player name\] ?i\[rank to start with\] | 显示天梯队伍排名（或玩家排名）从第 i 名开始的 5 条（默认 1，-1 为最差）（QmClient 补译） |
| `teamtop5` | ?s\[player name\] ?i\[rank to start with\] | 显示天梯队伍排名（或玩家排名）从第 i 名开始的 5 条（默认 1，-1 为最差）（QmClient 补译） |
| `top` | ?i\[rank to start with\] | 显示全局与区域天梯从第 i 名开始的前列排名（默认 1，-1 为最差）（QmClient 补译） |
| `top5` | ?i\[rank to start with\] | 显示全局与区域天梯从第 i 名开始的前列排名（默认 1，-1 为最差）（QmClient 补译） |
| `times` | ?s\[player name\] ?i\[number of times to skip\] | /times ?s?i 显示服务器最近 5 次成绩，或名称以 s 开头玩家从第 i 条开始的成绩（i 默认 1，-1 表示第一条）（QmClient 补译） |
| `points` | ?r\[player name\] | 显示名称以 r 开头玩家的全局积分（默认你的排名）（QmClient 补译） |
| `top5points` | ?i\[number\] | 显示全局积分榜从第 i 名开始的 5 条（默认 1）（QmClient 补译） |
| `timecp` | ?r\[player name\] | 根据其他玩家设置你的检查点（QmClient 补译） |
| `team` | ?i\[id\] | 让你加入队伍 i（留空则显示你的队伍）（QmClient 补译） |
| `lock` | ?i\['0'\|'1'\] | 锁定队伍后，其他玩家无法加入，任一队员死亡都会让全队重来。`/lock 0` 解锁，`/lock 1` 锁定（QmClient 补译） |
| `unlock` |  | 解锁一个队伍 |
| `invite` | r\[player name\] | 邀请某人加入锁定的队伍（QmClient 补译） |
| `showothers` | ?i\['0'\|'1'\|'2'\] | 是否显示其他队伍玩家（默认关闭），可选 i = 0 关闭，i = 1 打开，i = 2 仅本队（QmClient 补译） |
| `showall` | ?i\['0'\|'1'\] | 是否显示任意距离的玩家（默认关闭），可选 i = 0 为关闭，否则为打开（QmClient 补译） |
| `specteam` | ?i\['0'\|'1'\] | 旁观时是否显示其他队伍玩家（默认打开），可选 i = 0 关闭，否则为打开（QmClient 补译） |
| `ninjajetpack` | ?i\['0'\|'1'\] | 是否使用忍者喷气背包。让喷气背包看起来更酷（QmClient 补译） |
| `saytime` | ?r\[player name\] | 私聊发送某人在当前竞速中的时间（默认发送你的时间）（QmClient 补译） |
| `saytimeall` |  | 公开发送你当前竞速时间给所有人（QmClient 补译） |
| `time` |  | 在广播中私聊显示你当前竞速时间（QmClient 补译） |
| `timer` | ?s\['gametimer'\|'broadcast'\|'both'\|'none'\|'cycle'\] | 在广播或游戏/回合计时器中显示时间的个人设置，计时器 s，其中 s = broadcast 为广播，gametimer 为游戏/回合计时器，cycle 为循环，both 为两者都显示，none 为不显示计时器，留空则显示当前状态（QmClient 补译） |
| `r` |  | 将自己传送出冻结区（服务器需开启 `sv_rescue 1`）（QmClient 补译） |
| `rescue` |  | 将自己传送出冻结区（服务器需开启 `sv_rescue 1`）（QmClient 补译） |
| `tp` | ?r\[player name\] | 传送到指定玩家的位置。省略玩家名时，传送到当前观战的位置（QmClient 补译） |
| `lasttp` |  | 回到上一次传送的目的地（QmClient 补译） |
| `teleport` | ?r\[player name\] | 传送到指定玩家的位置。省略玩家名时，传送到当前观战的位置（QmClient 补译） |
| `unsolo` |  | 把你放出单人区域 |
| `undeep` |  | 让你解除深度冻结（QmClient 补译） |
| `kill` |  | 在长局 kill 保护时自杀（普通自杀请用 f1 + kill）（QmClient 补译） |

## 客户端设置

| 设置 | 简介 | 默认 | 最小值 | 最大值 |
| --- | --- | --- | --- | --- |
| `cl_predict` | 预测本地角色的移动。（QmClient 补译） | 1 | 0 | 1 |
| `cl_predict_dummy` | 预测分身的移动。（QmClient 补译） | 1 | 0 | 1 |
| `cl_antiping_limit` | 设置延迟补偿的限制值。0 为关闭。（QmClient 补译） | 0 | 0 | 200 |
| `cl_antiping` | 启用延迟补偿（AntiPing），使用更积极的预测方式。（QmClient 补译） | 0 | 0 | 1 |
| `cl_antiping_players` | 更积极地预测其他玩家的移动。仅在 cl\_antiping 为 1 时生效。（QmClient 补译） | 1 | 0 | 1 |
| `cl_antiping_grenade` | 预测榴弹轨迹。仅在 cl\_antiping 为 1 时生效。（QmClient 补译） | 1 | 0 | 1 |
| `cl_antiping_weapons` | 预测武器投射物。仅在 cl\_antiping 为 1 时生效。（QmClient 补译） | 1 | 0 | 1 |
| `cl_antiping_smooth` | 让其他玩家的移动预测更平滑。（QmClient 补译） | 0 | 0 | 1 |
| `cl_antiping_gunfire` | 预测开火并显示预测的武器物理效果。需要 cl\_antiping\_grenade 1 和 cl\_antiping\_weapons 1。（QmClient 补译） | 1 | 0 | 1 |
| `cl_prediction_margin` | 预测余量，单位为毫秒。增加余量会带来额外延迟，但能减轻 ping 突然变化导致的卡顿。（QmClient 补译） | 10 | 1 | 2000 |
| `cl_nameplates` | 显示昵称（QmClient 补译） | 1 | 0 | 1 |
| `cl_afk_emote` | 在挂机玩家旁显示 zzz 表情。（QmClient 补译） | 1 | 0 | 1 |
| `cl_nameplates_always` | 始终显示名牌，无论玩家离得多远。（QmClient 补译） | 1 | 0 | 1 |
| `cl_nameplates_teamcolors` | 用队伍颜色显示昵称（QmClient 补译） | 1 | 0 | 1 |
| `cl_nameplates_size` | 名牌大小，范围为 0% 至 100%。（QmClient 补译） | 50 | 0 | 100 |
| `cl_nameplates_clan` | 在名牌中显示战队。（QmClient 补译） | 0 | 0 | 1 |
| `cl_nameplates_clan_size` | 名牌中战队名称的大小，范围为 0% 至 100%。（QmClient 补译） | 30 | 0 | 100 |
| `cl_nameplates_ids` | 在名牌中显示玩家 ID。（QmClient 补译） | 0 | 0 | 1 |
| `cl_nameplates_own` | 显示自己的名牌，录制回放时可用。（QmClient 补译） | 0 | 0 | 1 |
| `cl_nameplates_friendmark` | 在好友名牌中显示标记（♥）。（QmClient 补译） | 0 | 0 | 1 |
| `cl_nameplates_strong` | 在名牌中显示强弱钩。0 为关闭，1 为图标，2 为图标和数字。（QmClient 补译） | 0 | 0 | 2 |
| `cl_text_entities` | 绘制实体块的文字信息。（QmClient 补译） | 1 | 0 | 1 |
| `cl_text_entities_size` | 实体块文字的大小，范围为 1% 至 100%。（QmClient 补译） | 100 | 1 | 100 |
| `cl_authed_player_color` | 计分板上已认证玩家的名字颜色。（QmClient 补译） | 5898211 |  |  |
| `cl_same_clan_color` | 计分板上同战队玩家的战队名称颜色。（QmClient 补译） | 5898211 |  |  |
| `cl_enable_ping_color` | 是否在计分板上用颜色区分 ping。（QmClient 补译） | 1 | 0 | 1 |
| `cl_autoswitch_weapons` | 在捡起武器时自动切换 | 1 | 0 | 1 |
| `cl_autoswitch_weapons_out_of_ammo` | 弹药耗尽时自动切换武器。（QmClient 补译） | 0 | 0 | 1 |
| `cl_showhud` | 显示游戏内 HUD | 1 | 0 | 1 |
| `cl_showhud_healthammo` | 在游戏 HUD 中显示生命值和弹药。（QmClient 补译） | 1 | 0 | 1 |
| `cl_showhud_score` | 显示游戏内 HUD（分数） | 1 | 0 | 1 |
| `cl_showhud_timer` | 显示游戏内 HUD（计时器） | 1 | 0 | 1 |
| `cl_showhud_dummy_actions` | 显示游戏内 HUD（分身动作） | 1 | 0 | 1 |
| `cl_showhud_player_position` | 显示游戏内 HUD（玩家位置） | 0 | 0 | 1 |
| `cl_showhud_player_speed` | 显示游戏内 HUD（玩家速度） | 0 | 0 | 1 |
| `cl_showhud_player_angle` | 在游戏 HUD 中显示玩家瞄准角度。（QmClient 补译） | 0 | 0 | 1 |
| `cl_showhud_ddrace` | 显示游戏内 HUD（DDRace HUD） | 1 | 0 | 1 |
| `cl_showhud_jumps_indicator` | 在游戏 HUD 中显示可用和已用的跳跃次数。（QmClient 补译） | 1 | 0 | 1 |
| `cl_show_freeze_bars` | 在冻结玩家下方显示冻结进度条，提示距离解冻还剩多久。（QmClient 补译） | 1 | 0 | 1 |
| `cl_freezebars_alpha_inside_freeze` | 玩家处于冻结区域内时，冻结进度条的不透明度。0 为不可见，100 为完全可见。（QmClient 补译） | 0 | 0 | 100 |
| `cl_showrecord` | 显示旧版 DDRace 客户端样式的纪录。（QmClient 补译） | 0 | 0 | 1 |
| `cl_shownotifications` | 其他玩家在聊天中提到你时发出通知。（QmClient 补译） | 1 | 0 | 1 |
| `cl_showemotes` | 显示 Tee 表情（QmClient 补译） | 1 | 0 | 1 |
| `cl_showchat` | 显示聊天。设为 2 时始终显示大聊天区。（QmClient 补译） | 1 | 0 | 2 |
| `cl_show_chat_friends` | 只显示来自朋友的消息 | 0 | 0 | 1 |
| `cl_show_chat_system` | 显示服务器消息 | 1 | 0 | 1 |
| `cl_showkillmessages` | 显示击杀信息 | 1 | 0 | 1 |
| `cl_show_votes_after_voting` | 在投票时显示投票窗口 | 0 | 0 | 1 |
| `cl_show_local_time_always` | 总是显示本地时间 | 0 | 0 | 1 |
| `cl_showfps` | 在游戏中显示 FPS 计数器 | 0 | 0 | 1 |
| `cl_showpred` | 在游戏中显示预测时间，单位为毫秒。（QmClient 补译） | 0 | 0 | 1 |
| `cl_eye_wheel` | 在表情轮盘中同时显示眼睛表情。（QmClient 补译） | 1 | 0 | 1 |
| `cl_eye_duration` | 设置眼睛表情的持续时间。（QmClient 补译） | 999999 | 1 | 999999 |
| `cl_airjumpindicator` | 显示空中跳跃指示器。（QmClient 补译） | 1 | 0 | 1 |
| `cl_threadsoundloading` | 在线程中加载音频文件。（QmClient 补译） | 1 | 0 | 1 |
| `cl_warning_teambalance` | 关于队伍平衡的警告（QmClient 补译） | 1 | 0 | 1 |
| `cl_mouse_deadzone` | 鼠标光标移动多少后，镜头才开始跟随。（QmClient 补译） | 0 | 0 | 3000 |
| `cl_mouse_followfactor` | 镜头跟随鼠标光标的幅度系数。（QmClient 补译） | 0 | 0 | 200 |
| `cl_mouse_max_distance` | 最小鼠标指针距离 | 400 | 0 | 5000 |
| `cl_mouse_min_distance` | 最大鼠标指针距离 | 0 | 0 | 5000 |
| `cl_dyncam` | 启用动态镜头。（QmClient 补译） | 0 | 0 | 1 |
| `cl_dyncam_max_distance` | 最大动态相机光标距离（QmClient 补译） | 1000 | 0 | 2000 |
| `cl_dyncam_min_distance` | 最小动态相机光标距离（QmClient 补译） | 0 | 0 | 2000 |
| `cl_dyncam_mousesens` | 启用动态镜头时使用的鼠标灵敏度。（QmClient 补译） | 0 | 0 | 100000 |
| `cl_dyncam_deadzone` | 鼠标光标移动多少后，动态镜头才开始跟随。（QmClient 补译） | 300 | 1 | 1300 |
| `cl_dyncam_follow_factor` | 动态镜头跟随鼠标光标的幅度系数。（QmClient 补译） | 60 | 0 | 200 |
| `cl_dyncam_smoothness` | 镜头移动的平滑程度。0 为立即移动，100 为缓慢且平滑地移动。（QmClient 补译） | 0 | 0 | 100 |
| `cl_dyncam_stabilizing` | 鼠标光标快速移动时的镜头减速幅度。值过高可能导致镜头跟随滞后。（QmClient 补译） | 0 | 0 | 100 |
| `cl_multiview_sensitivity` | 多视角模式下，镜头移向目标位置的速度。值越高，移动越快。（QmClient 补译） | 100 | 0 | 200 |
| `cl_multiview_zoom_smoothness` | 多视角模式下的缩放过渡时间，单位为毫秒。值越高，过渡越慢。（QmClient 补译） | 1300 | 50 | 5000 |
| `ed_autosave_interval` | 每隔多少分钟把当前编辑的地图副本自动保存到 auto 文件夹。0 为关闭自动保存。（QmClient 补译） | 10 | 0 | 240 |
| `ed_autosave_max` | 同一地图名保留的自动存档数量上限。0 表示不限数量。（QmClient 补译） | 10 | 0 | 1000 |
| `ed_smooth_zoom_time` | 地图编辑器中平滑缩放动画的时长，单位为毫秒。0 为关闭动画。（QmClient 补译） | 250 | 0 | 5000 |
| `ed_limit_max_zoom_level` | 是否限制地图编辑器中的最大放大级别。0 为不限制。（QmClient 补译） | 1 | 0 | 1 |
| `ed_zoom_target` | 以鼠标当前指向的位置为中心缩放。（QmClient 补译） | 0 | 0 | 1 |
| `ed_showkeys` | 显示按下的按键 | 0 | 0 | 1 |
| `cl_show_welcome` | 客户端首次启动时显示欢迎消息。（QmClient 补译） | 1 | 0 | 1 |
| `cl_motd_time` | 服务器公告的显示时长。（QmClient 补译） | 10 | 0 | 100 |
| `cl_map_download_url` | 用于下载地图的 URL，可以以 http:// 或 https:// 开头。（QmClient 补译） | "[https://maps.ddnet.org](https://maps.ddnet.org)" |  |  |
| `cl_map_download_connect_timeout_ms` | 通过 HTTP 下载地图时，建立连接的超时时间，单位为毫秒。0 为禁用此超时。（QmClient 补译） | 2000 | 0 | 100000 |
| `cl_map_download_low_speed_limit` | 通过 HTTP 下载地图时的最低下载速度，单位为字节每秒。0 为禁用此限制。（QmClient 补译） | 4000 | 0 | 100000 |
| `cl_map_download_low_speed_time` | 通过 HTTP 下载地图时，速度低于限制后允许持续的时间。0 为禁用此限制。（QmClient 补译） | 3 | 0 | 100000 |
| `cl_languagefile` | 文件使用的是什么语言 | "" |  |  |
| `cl_skin_download_url` | 用于下载皮肤的 URL。（QmClient 补译） | "[https://skins.ddnet.org/skin/](https://skins.ddnet.org/skin/)" |  |  |
| `cl_skin_community_download_url` | 用于下载社区皮肤的 URL。（QmClient 补译） | "[https://skins.ddnet.org/skin/community/](https://skins.ddnet.org/skin/community/)" |  |  |
| `cl_vanilla_skins_only` | 只显示在原版《Teeworlds》发布的皮肤 | 0 | 0 | 1 |
| `cl_download_skins` | 在需要时从 cl\_skin\_download\_url 下载皮肤。（QmClient 补译） | 1 | 0 | 1 |
| `cl_download_community_skins` | 允许下载社区制作的皮肤。启用后使用 cl\_skin\_community\_download\_url 作为下载地址。（QmClient 补译） | 0 | 0 | 1 |
| `cl_auto_statboard_screenshot` | 比赛结束后自动截取统计面板。（QmClient 补译） | 0 | 0 | 1 |
| `cl_auto_statboard_screenshot_max` | 自动生成的统计面板截图保留上限。0 表示不限数量。（QmClient 补译） | 10 | 0 | 1000 |
| `cl_default_zoom` | 默认缩放等级 | 10 | 0 | 20 |
| `cl_smooth_zoom_time` | 游戏内平滑缩放动画的时长，单位为毫秒。0 为关闭动画。（QmClient 补译） | 250 | 0 | 5000 |
| `cl_limit_max_zoom_level` | 是否限制游戏内的最大放大级别。0 为不限制。（QmClient 补译） | 1 | 0 | 1 |
| `player_use_custom_color` | 切换角色是否使用自定义颜色。（QmClient 补译） | 0 | 0 | 1 |
| `player_color_body` | 玩家身体颜色 | 65408 |  |  |
| `player_color_feet` | 玩家脚颜色（QmClient 补译） | 65408 |  |  |
| `player_skin` | 玩家皮肤 | "default" |  |  |
| `player_default_eyes` | 角色进入服务器时的眼睛表情。0 为正常，1 为痛苦，2 为开心，3 为惊讶，4 为生气，5 为眨眼。（QmClient 补译） | 0 | 0 | 5 |
| `cl_skin_prefix` | 用具有指定前缀的皮肤替换原皮肤，例如 kitty 或 santa。（QmClient 补译） | "" |  |  |
| `cl_fat_skins` | 启用胖胖的皮肤 | 0 | 0 | 1 |
| `ui_page` | 界面页面（QmClient 补译） | 9 | 6 | 10 |
| `ui_settings_page` | 界面设置页面（QmClient 补译） | 0 | 0 | 9 |
| `ui_toolbox_page` | 工具箱页面（QmClient 补译） | 0 | 0 | 2 |
| `ui_server_address` | 界面中使用的服务器地址。（QmClient 补译） | "localhost:8303" |  |  |
| `ui_mousesens` | 菜单及地图编辑器中的鼠标灵敏度。（QmClient 补译） | 200 | 1 | 100000 |
| `ui_controller_sens` | 菜单及地图编辑器中的手柄灵敏度。（QmClient 补译） | 100 | 1 | 100000 |
| `ui_smooth_scroll_time` | 菜单及地图编辑器中平滑滚动动画的时长，单位为毫秒。0 为关闭动画。（QmClient 补译） | 500 | 0 | 5000 |
| `ui_color` | 界面颜色（QmClient 补译） | 0xE4A046AF |  |  |
| `ui_colorize_ping` | 用颜色突出显示 ping。（QmClient 补译） | 1 | 0 | 1 |
| `ui_colorize_gametype` | 高亮游戏类型 | 1 | 0 | 1 |
| `ui_close_window_after_changing_setting` | 在改变设置之后关闭窗口 | 1 | 0 | 1 |
| `ui_unread_news` | 记录是否有未读公告。（QmClient 补译） | 0 | 0 | 1 |
| `gfx_noclip` | 禁用裁剪（QmClient 补译） | 0 | 0 | 1 |
| `dummy_name` | 分身的名字 | "" |  |  |
| `dummy_clan` | 分身的队伍 | "" |  |  |
| `dummy_country` | 分身的国家 | -1 | -1 | 1000 |
| `dummy_use_custom_color` | 切换分身是否使用自定义颜色。（QmClient 补译） | 0 | 0 | 1 |
| `dummy_color_body` | 分身身体颜色 | 65408 |  |  |
| `dummy_color_feet` | 分身脚颜色（QmClient 补译） | 65408 |  |  |
| `dummy_skin` | 分身皮肤 | "default" |  |  |
| `dummy_default_eyes` | 分身进入服务器时的眼睛表情。0 为正常，1 为痛苦，2 为开心，3 为惊讶，4 为生气，5 为眨眼。（QmClient 补译） | 0 | 0 | 5 |
| `cl_dummy` | 0 - 玩家 / 1 - 分身 | 0 | 0 | 1 |
| `cl_dummy_hammer` | 控制分身是否持续挥锤用于锤飞。（QmClient 补译） | 0 | 0 | 1 |
| `cl_dummy_resetonswitch` | 切换分身时是否松开已按住的键。0 为不松开，1 为松开分身的按键，2 为松开主角色的按键。（QmClient 补译） | 0 | 0 | 2 |
| `cl_dummy_restore_weapon` | 锤飞结束后，分身是否切回之前的武器。（QmClient 补译） | 1 | 0 | 1 |
| `cl_dummy_copy_moves` | 分身是否同步主角色的操作。（QmClient 补译） | 0 | 0 | 1 |
| `cl_dummy_control` | 是否允许同时控制主角色和分身，并通过 cl\_dummy\_jump、cl\_dummy\_fire 和 cl\_dummy\_hook 控制分身的动作。（QmClient 补译） | 0 | 0 | 1 |
| `cl_dummy_jump` | 控制分身跳跃，需要将 cl\_dummy\_control 设为 1。（QmClient 补译） | 0 | 0 | 1 |
| `cl_dummy_fire` | 控制分身开火，需要将 cl\_dummy\_control 设为 1。（QmClient 补译） | 0 | 0 | 1 |
| `cl_dummy_hook` | 控制分身出钩，需要将 cl\_dummy\_control 设为 1。（QmClient 补译） | 0 | 0 | 1 |
| `cl_show_start_menu_images` | 显示初始界面的图像 | 1 | 0 | 1 |
| `cl_skip_start_menu` | 跳过初始界面 | 0 | 0 | 1 |
| `cl_video_pausewithdemo` | 在回放暂停的同时暂停视频渲染 | 1 | 0 | 1 |
| `cl_video_showhud` | 在渲染视频时显示游戏内 HUD | 0 | 0 | 1 |
| `cl_video_showchat` | 在渲染视频时显示聊天界面 | 1 | 0 | 1 |
| `cl_video_sound_enable` | 渲染视频时加入游戏声音。（QmClient 补译） | 1 | 0 | 1 |
| `cl_video_show_hook_coll_other` | 渲染视频时显示其他玩家的钩子辅助线（QmClient 补译） | 0 | 0 | 1 |
| `cl_video_show_direction` | 渲染视频时显示玩家的按键状态。1 只显示其他玩家，2 也显示自己的按键状态。（QmClient 补译） | 0 | 0 | 2 |
| `cl_video_crf` | 使用 libx264 编码视频时的 CRF。0 为最高画质，51 为最低画质。（QmClient 补译） | 18 | 0 | 51 |
| `cl_video_preset` | 使用 libx264 编码视频时的预设。默认 5（medium），0 为最快的 ultrafast，9 为最慢的 placebo，通常不建议使用。（QmClient 补译） | 5 | 0 | 9 |
| `dbg_tuning` | 显示影响自身角色的调校参数。0 为关闭，1 为只显示有改动的参数，2 为显示全部参数。（QmClient 补译） | 0 | 0 | 2 |
| `player_name` | 玩家的名称 | "" |  |  |
| `player_clan` | 玩家的战队 | "" |  |  |
| `player_country` | 玩家的国家 | -1 | -1 | 1000 |
| `password` | 连接服务器时使用的密码。（QmClient 补译） | "" |  |  |
| `events` | 启用节日活动效果，例如某些节日的服务器眼睛表情和客户端的圣诞皮肤。（QmClient 补译） | 1 | 0 | 1 |
| `steam_name` | 上次读取到的 Steam 个人资料名称。（QmClient 补译） | "" |  |  |
| `logfile` | 用于记录全部输出的日志文件名。（QmClient 补译） | "" |  |  |
| `logappend` | 将日志追加到现有文件，开启后每次启动都会保留之前的记录。（QmClient 补译） | 1 | 0 | 1 |
| `loglevel` | 设置日志文件的输出级别。-3 不输出，-2 仅错误，-1 为警告，0 为信息，1 为调试，2 为跟踪。（QmClient 补译） | 0 | -3 | 2 |
| `stdout_output_level` | 设置系统控制台的输出级别。-3 不输出，-2 仅错误，-1 为警告，0 为信息，1 为调试，2 为跟踪。（QmClient 补译） | 0 | -3 | 2 |
| `console_output_level` | 设置本地和远程控制台的输出级别。-3 不输出，-2 仅错误，-1 为警告，0 为信息，1 为调试，2 为跟踪。（QmClient 补译） | 0 | -3 | 2 |
| `console_enable_colors` | 在控制台输出中启用颜色显示 | 1 | 0 | 1 |
| `cl_save_settings` | 在退出时写入设置文件 | 1 | 0 | 1 |
| `cl_refresh_rate` | 游戏更新频率，单位为 Hz。（QmClient 补译） | 0 | 0 | 10000 |
| `cl_refresh_rate_inactive` | 游戏窗口失去焦点时的游戏更新频率，单位为 Hz。（QmClient 补译） | 120 | 0 | 10000 |
| `cl_editor` | 打开地图编辑器 | 0 | 0 | 1 |
| `cl_editor_dilate` | 自动扩张嵌入图片透明边缘处的像素颜色。（QmClient 补译） | 1 | 0 | 1 |
| `cl_skin_filter_string` | 用于筛选皮肤的字符串。（QmClient 补译） | "" |  |  |
| `cl_auto_demo_record` | 自动录制回放（QmClient 补译） | 1 | 0 | 1 |
| `cl_auto_demo_on_connect` | 启用自动录制时，仅在连接服务器时开始录制新的回放。（QmClient 补译） | 0 | 0 | 1 |
| `cl_auto_demo_max` | 自动录制回放的保留上限。0 表示不限数量。（QmClient 补译） | 10 | 0 | 1000 |
| `cl_auto_screenshot` | 自动对游戏结束画面截图（QmClient 补译） | 0 | 0 | 1 |
| `cl_auto_screenshot_max` | 自动截图的保留上限。0 表示不限数量。（QmClient 补译） | 10 | 0 | 1000 |
| `cl_auto_csv` | 在比赛结束时自动生成 CSV 文件。（QmClient 补译） | 0 | 0 | 1 |
| `cl_auto_csv_max` | 自动生成的 CSV 文件保留上限。0 表示不限数量。（QmClient 补译） | 10 | 0 | 1000 |
| `cl_show_broadcasts` | 在游戏中显示广播消息。（QmClient 补译） | 1 | 0 | 1 |
| `cl_print_broadcasts` | 把广播消息输出到控制台。（QmClient 补译） | 1 | 0 | 1 |
| `cl_print_motd` | 把服务器公告输出到控制台。（QmClient 补译） | 1 | 0 | 1 |
| `cl_friends_ignore_clan` | 在搜索好友时忽略战队名称 | 1 | 0 | 1 |
| `cl_assets_entities` | 用于实体层的资源包。（QmClient 补译） | "default" |  |  |
| `cl_asset_game` | 用于游戏贴图的资源包。（QmClient 补译） | "default" |  |  |
| `cl_asset_emoticons` | 用于表情的资源包。（QmClient 补译） | "default" |  |  |
| `cl_asset_particles` | 用于粒子效果的资源包。（QmClient 补译） | "default" |  |  |
| `cl_asset_hud` | 用于 HUD 的资源包。（QmClient 补译） | "default" |  |  |
| `cl_asset_extras` | 用于 Teeworlds 原版以外的额外游戏贴图的资源包。（QmClient 补译） | "default" |  |  |
| `br_filter_string` | 服务器浏览器的搜索筛选词。（QmClient 补译） | "Novice" |  |  |
| `br_exclude_string` | 服务器浏览器的排除关键词。（QmClient 补译） | "" |  |  |
| `br_filter_full` | 在服务器浏览器中隐藏已满的服务器。（QmClient 补译） | 0 | 0 | 1 |
| `br_filter_empty` | 在服务器浏览器中隐藏空服务器。（QmClient 补译） | 0 | 0 | 1 |
| `br_filter_spectators` | 统计服务器玩家人数时排除观战者。（QmClient 补译） | 0 | 0 | 1 |
| `br_filter_friends` | 按服务器内是否有好友筛选。（QmClient 补译） | 0 | 0 | 1 |
| `br_filter_country` | 隐藏没有指定国家或地区玩家的服务器。（QmClient 补译） | 0 | 0 | 1 |
| `br_filter_country_index` | 服务器浏览器按玩家国旗筛选时使用的国家或地区索引。（QmClient 补译） | -1 | -1 | 999 |
| `br_filter_pw` | 在浏览器中筛选掉受密码保护的服务器（QmClient 补译） | 0 | 0 | 1 |
| `br_filter_gametype` | 用于筛选服务器的游戏模式。（QmClient 补译） | "" |  |  |
| `br_filter_gametype_strict` | 严格匹配（QmClient 补译） | 0 | 0 | 1 |
| `br_filter_connecting_players` | 不计连接中（QmClient 补译） | 1 | 0 | 1 |
| `br_filter_serveraddress` | 要筛选的服务器地址（QmClient 补译） | "" |  |  |
| `br_filter_unfinished_map` | 仅显示未完成地图的服务器（QmClient 补译） | 0 | 0 | 1 |
| `br_filter_exclude_countries` | 按国家或地区排除 DDNet 服务器。（QmClient 补译） | "" |  |  |
| `br_filter_exclude_types` | 按游戏类型（模组）排除 DDNet 服务器。（QmClient 补译） | "" |  |  |
| `br_indicate_finished` | 显示你是否已完成 DDNet 地图（会将你的玩家名传输至 info.ddnet.org/info）（QmClient 补译） | 1 | 0 | 1 |
| `br_location` | 覆盖 Ping 估计位置，可用：auto、af、as、as:cn、eu、na、oc、sa（自动、非洲、亚洲、中国、欧洲、北美、大洋洲/澳大利亚、南美洲）（QmClient 补译） | "auto" |  |  |
| `br_cached_best_serverinfo_url` | 不要设置此变量；请在 settings\_ddnet.cfg 旁创建 ddnet-serverlist-urls.cfg，以指定所有可用的服务器列表 URL（QmClient 补译） | "" |  |  |
| `br_filter_exclude_countries_kog` | 按国家或地区排除 KoG 服务器。（QmClient 补译） | "" |  |  |
| `br_filter_exclude_types_kog` | 按游戏类型（模组）排除 KoG 服务器。（QmClient 补译） | "" |  |  |
| `br_sort` | 服务器浏览器的排序列。（QmClient 补译） | 4 | 0 | 256 |
| `br_sort_order` | 服务器浏览器的排序方向。（QmClient 补译） | 2 | 0 | 2 |
| `br_max_requests` | 刷新服务器浏览器时并发请求的数量。（QmClient 补译） | 100 | 0 | 1000 |
| `br_demo_sort` | 回放浏览器的排序列。（QmClient 补译） | 0 | 0 | 3 |
| `br_demo_sort_order` | 回放浏览器的排序方向。（QmClient 补译） | 0 | 0 | 1 |
| `br_demo_fetch_info` | 刷新时是否自动读取回放信息。（QmClient 补译） | 0 | 0 | 1 |
| `snd_buffer_size` | 音效缓冲区大小 | 512 | 128 | 32768 |
| `snd_rate` | 音频混音采样率。（QmClient 补译） | 48000 | 5512 | 384000 |
| `snd_enable` | 启用音效 | 1 | 0 | 1 |
| `snd_enable_music` | 播放背景音乐 | 0 | 0 | 1 |
| `snd_volume` | 音效音量 | 30 | 0 | 100 |
| `snd_chat_volume` | 聊天音效音量 | 30 | 0 | 100 |
| `snd_game_volume` | 游戏音效音量 | 30 | 0 | 100 |
| `snd_ambient_volume` | 地图音效音量 | 30 | 0 | 100 |
| `snd_background_music_volume` | 背景音乐音量 | 30 | 0 | 100 |
| `snd_nonactive_mute` | 游戏窗口失去焦点时静音。（QmClient 补译） | 0 | 0 | 1 |
| `snd_game` | 启用游戏音效 | 1 | 0 | 1 |
| `snd_gun` | 启用枪声 | 1 | 0 | 1 |
| `snd_long_pain` | 启用长版痛苦音效，例如冻结时按下开火键发出的叫声。（QmClient 补译） | 1 | 0 | 1 |
| `snd_chat` | 启用聊天消息提示音（QmClient 补译） | 1 | 0 | 1 |
| `snd_team_chat` | 启用团队聊天音效 | 1 | 0 | 1 |
| `snd_servermessage` | 启用服务器消息音效 | 1 | 0 | 1 |
| `snd_highlight` | 启用高亮聊天音效 | 1 | 0 | 1 |
| `gfx_screen` | 屏幕编号 | 0 | 0 | 15 |
| `gfx_screen_width` | 屏幕分辨率的宽 | 0 | 0 | 0 |
| `gfx_screen_height` | 屏幕分辨率的高 | 0 | 0 | 0 |
| `gfx_screen_refresh_rate` | 显示器刷新率。（QmClient 补译） | 0 | 0 | 0 |
| `gfx_desktop_width` | 用于检测显示器变化的桌面分辨率宽度，通常无需手动修改。（QmClient 补译） | 0 | 0 | 0 |
| `gfx_desktop_height` | 用于检测显示器变化的桌面分辨率高度，通常无需手动修改。（QmClient 补译） | 0 | 0 | 0 |
| `gfx_borderless` | 启用无边框窗口。请勿与全屏同时使用。（QmClient 补译） | 1 | 0 | 1 |
| `gfx_fullscreen` | 设置全屏模式。0 为窗口模式，1 为独占全屏，2 为桌面全屏，3 为窗口化全屏。（QmClient 补译） | 0 | 0 | 3 |
| `gfx_highdpi` | 启用高dpi | 1 | 0 | 1 |
| `gfx_color_depth` | 帧缓冲的颜色位深，仅适用于全屏。（QmClient 补译） | 24 | 16 | 24 |
| `gfx_vsync` | 启用垂直同步，可能会增加延迟。（QmClient 补译） | 0 | 0 | 1 |
| `gfx_display_all_video_modes` | 显示所有视频模式 | 0 | 0 | 1 |
| `gfx_high_detail` | 启用高细节绘制。（QmClient 补译） | 1 | 0 | 1 |
| `gfx_fsaa_samples` | 全屏抗锯齿（FSAA）的采样数，可能会增加延迟。（QmClient 补译） | 0 | 0 | 64 |
| `gfx_refresh_rate` | 屏幕刷新率。（QmClient 补译） | 0 | 0 | 10000 |
| `gfx_backgroundrender` | 窗口在后台时仍渲染画面（QmClient 补译） | 1 | 0 | 1 |
| `gfx_text_overlay` | 限制地图编辑器或实体层的文字叠加绘制。值越大，绘制的细节越少，速度越快。（QmClient 补译） | 10 | 1 | 100 |
| `gfx_asyncrender_old` | 如果游戏更新时上一帧仍在渲染，则跳过当前帧的渲染。（QmClient 补译） | 1 | 0 | 1 |
| `gfx_quad_as_triangle` | 用三角形绘制四边形，可修复部分 GPU 上的四边形着色问题。（QmClient 补译） | 0 | 0 | 1 |
| `inp_mousesens` | 鼠标灵敏度 | 200 | 1 | 100000 |
| `inp_translated_keys` | 解析按键前，先按当前键盘布局转换键值（QmClient 补译） | 0 | 0 | 1 |
| `inp_ignored_modifiers` | 要忽略的键盘修饰键掩码（QmClient 补译） | 0 | 0 | 65536 |
| `inp_ime_native_ui` | 使用输入法自带的界面。全屏时输入法可能无法使用，修改后需重启（QmClient 补译） | 0 | 0 | 1 |
| `inp_controller_enable` | 启用控制器 | 0 | 0 | 1 |
| `inp_controller_guid` | 当前手柄的唯一标识符（GUID）（QmClient 补译） | "" |  |  |
| `inp_controller_absolute` | 在游戏中启用手柄绝对瞄准（QmClient 补译） | 0 | 0 | 1 |
| `inp_controller_sens` | 游戏内控制器灵敏度 | 100 | 1 | 100000 |
| `inp_controller_x` | 控制光标横向移动的手柄轴（QmClient 补译） | 0 | 0 | 12 |
| `inp_controller_y` | 控制光标纵向移动的手柄轴（QmClient 补译） | 1 | 0 | 12 |
| `inp_controller_tolerance` | 用于过滤手柄轴轻微抖动的容差（QmClient 补译） | 5 | 0 | 50 |
| `cl_port` | 客户端连接服务器使用的端口（0 随机选择，1024 或更大为手动指定）。修改后需重启（QmClient 补译） | 0 | 0 | 65535 |
| `cl_dummy_port` | 分身连接服务器使用的端口（0 随机选择，1024 或更大为手动指定）。修改后需重启（QmClient 补译） | 0 | 0 | 65535 |
| `cl_contact_port` | 查询服务器信息使用的端口（0 随机选择，1024 或更大为手动指定）。修改后需重启（QmClient 补译） | 0 | 0 | 65535 |
| `bindaddr` | 客户端或服务器要绑定的本地地址（QmClient 补译） | "" |  |  |
| `debug` | 调试模式 | 0 | 0 | 1 |
| `dbg_curl` | 启用 curl 调试输出（QmClient 补译） | 0 | 0 | 1 |
| `dbg_graphs` | 显示性能图表（QmClient 补译） | 0 | 0 | 1 |
| `dbg_gfx` | 显卡支持时，显示图形库的警告和错误（0 = 关闭，1 = 最少，2 = 影响性能的问题，3 = 详细，4 = 全部）（QmClient 补译） | 0 | 0 | 4 |
| `dbg_stress` | 对系统进行压力测试（仅调试构建）（QmClient 补译） | 0 | 0 | 0 |
| `dbg_stress_server` | 要进行压力测试的服务器（仅调试构建）（QmClient 补译） | "localhost" |  |  |
| `http_allow_insecure` | 允许使用 HTTP，并继续支持 HTTPS，主要用于测试（QmClient 补译） | 0 | 0 | 1 |
| `cl_race_binds_set` | 记录已应用到哪个版本的 DDRace 默认绑定，由客户端自动更新（QmClient 补译） | 0 | 0 | 1 |
| `cl_reconnect_timeout` | 连接超时后，等待多少秒再重连（0 = 关闭）（QmClient 补译） | 120 | 0 | 600 |
| `cl_reconnect_full` | 服务器已满时，等待多少秒再重连（0 = 关闭）（QmClient 补译） | 5 | 0 | 600 |
| `cl_message_system_color` | 系统消息颜色 | 2817983 |  |  |
| `cl_message_client_color` | 客户端消息颜色 | 9633471 |  |  |
| `cl_message_highlight_color` | 高亮消息颜色 | 65471 |  |  |
| `cl_message_team_color` | 团队消息颜色 | 5636050 |  |  |
| `cl_message_color` | 消息颜色 | 255 |  |  |
| `cl_laser_rifle_inner_color` | 激光枪光束的内部颜色（QmClient 补译） | 11206591 |  |  |
| `cl_laser_rifle_outline_color` | 激光枪光束的轮廓颜色（QmClient 补译） | 11176233 |  |  |
| `cl_laser_sg_inner_color` | 霰弹枪光束的内部颜色（QmClient 补译） | 1467241 |  |  |
| `cl_laser_sg_outline_color` | 霰弹枪光束的轮廓颜色（QmClient 补译） | 1866773 |  |  |
| `cl_laser_door_inner_color` | 激光门的内部颜色（QmClient 补译） | 7701379 |  |  |
| `cl_laser_door_outline_color` | 激光门的轮廓颜色（QmClient 补译） | 7667473 |  |  |
| `cl_laser_freeze_inner_color` | 冻结激光的内部颜色（QmClient 补译） | 12001153 |  |  |
| `cl_laser_freeze_outline_color` | 冻结激光的轮廓颜色（QmClient 补译） | 11613223 |  |  |
| `cl_kill_message_normal_color` | 默认击杀消息颜色 | 255 |  |  |
| `cl_kill_message_highlight_color` | 高亮击杀消息颜色 | 255 |  |  |
| `cl_message_friend` | 为好友的聊天消息使用专属颜色并显示爱心标记（QmClient 补译） | 1 | 0 | 1 |
| `cl_message_friend_color` | 朋友消息颜色 | 65425 |  |  |
| `conn_timeout` | 网络超时 | 100 | 5 | 1000 |
| `cl_show_ids` | 在计分板上显示客户端 ID（QmClient 补译） | 0 | 0 | 1 |
| `cl_scoreboard_on_death` | 死亡后显示计分板（QmClient 补译） | 1 | 0 | 1 |
| `cl_auto_race_record` | 保存每场游戏最佳记录的回放（QmClient 补译） | 1 | 0 | 1 |
| `cl_replays` | 启用/禁用重播（QmClient 补译） | 0 | 0 | 1 |
| `cl_replay_length` | 设置即时回放的默认时长（QmClient 补译） | 30 | 10 | 0 |
| `cl_race_record_server_control` | 允许服务器启动竞速录像（QmClient 补译） | 1 | 0 | 1 |
| `cl_demo_name` | 将玩家名称写入录像文件（QmClient 补译） | 1 | 0 | 1 |
| `cl_demo_assume_race` | 按竞速录像处理录像文件（QmClient 补译） | 1 | 0 | 1 |
| `cl_race_ghost` | 启用幽灵 | 1 | 0 | 1 |
| `cl_race_ghost_server_control` | 允许服务器启动竞速影子（QmClient 补译） | 1 | 0 | 1 |
| `cl_race_show_ghost` | 显示幽灵 | 1 | 0 | 1 |
| `cl_race_save_ghost` | 保存幽灵 | 1 | 0 | 1 |
| `cl_ddrace_scoreboard` | 启用 DDRace 计分板 | 1 | 0 | 1 |
| `cl_show_others` | 显示其他队伍的玩家（2 = 只显示自己的队伍）（QmClient 补译） | 0 | 0 | 2 |
| `cl_show_others_alpha` | 其他队伍玩家的可见度（0 = 完全透明，100 = 完全不透明）（QmClient 补译） | 40 | 0 | 100 |
| `cl_overlay_entities` | 按设定的不透明度叠加地图实体图层（QmClient 补译） | 0 | 0 | 100 |
| `cl_showquads` | 显示地图中的四边形，供制图或排查性能问题使用（QmClient 补译） | 1 | 0 | 1 |
| `cl_background_color` | 背景颜色 | 128 |  |  |
| `cl_background_entities_color` | 背景（实体）颜色 | 128 |  |  |
| `cl_background_entities` | 背景（实体） | "" |  |  |
| `cl_run_on_join` | 进入服务器时执行的命令（QmClient 补译） | "" |  |  |
| `cl_menu_map` | 菜单使用的背景地图（QmClient 补译） | "auto" |  |  |
| `cl_rotation_radius` | 菜单相机旋转半径（QmClient 补译） | 30 | 1 | 500 |
| `cl_rotation_speed` | 菜单镜头旋转一周所用的秒数（QmClient 补译） | 40 | 1 | 120 |
| `cl_camera_speed` | 菜单相机速度 | 5 | 1 | 40 |
| `cl_background_show_tiles_layers` | 使用自定义背景（实体模式）时绘制图块图层（QmClient 补译） | 0 | 0 | 1 |
| `cl_unpredicted_shadow` | 显示未经预测的 Tee 残影（0 = 关闭，1 = 开启，-1 = 连调试模式也关闭）（QmClient 补译） | 0 | -1 | 1 |
| `cl_predict_freeze` | 预测冻结区域（0 = 关闭，1 = 开启，2 = 部分预测，允许在冻结区内少量移动）（QmClient 补译） | 1 | 0 | 2 |
| `cl_show_ninja` | 显示忍者皮肤（QmClient 补译） | 1 | 0 | 1 |
| `cl_show_hook_coll_other` | 显示其他玩家的钩索碰撞线（2 = 始终显示）（QmClient 补译） | 1 | 0 | 2 |
| `cl_show_hook_coll_own` | 显示自己的钩索碰撞线（2 = 始终显示）（QmClient 补译） | 1 | 0 | 2 |
| `cl_hook_coll_size` | 钩索碰撞线的粗细（QmClient 补译） | 0 | 0 | 20 |
| `cl_hook_coll_alpha` | 钩索碰撞线的不透明度（0 = 完全透明，100 = 完全不透明）（QmClient 补译） | 100 | 0 | 100 |
| `cl_hook_coll_color_no_coll` | 钩索碰撞线未命中时的颜色（QmClient 补译） | 65407 |  |  |
| `cl_hook_coll_color_hookable_coll` | 钩索碰撞线命中可钩图块时的颜色（QmClient 补译） | 6401973 |  |  |
| `cl_hook_coll_color_tee_coll` | 钩索碰撞线命中 Tee 时的颜色（QmClient 补译） | 2817919 |  |  |
| `cl_chat_teamcolors` | 以队伍颜色显示玩家昵称（QmClient 补译） | 0 | 0 | 1 |
| `cl_chat_reset` | 按 Esc 时重置聊天输入（QmClient 补译） | 1 | 0 | 1 |
| `cl_chat_old` | 老式聊天样式：没有 tee，没有背景 | 0 | 0 | 1 |
| `cl_show_direction` | 显示按键输入（1 = 其他玩家，2 = 也显示自己的输入）（QmClient 补译） | 1 | 0 | 2 |
| `cl_old_gun_position` | 像 Teeworlds 0.6.1 及更早版本一样，让 Tee 把手枪举高一点（QmClient 补译） | 0 | 0 | 1 |
| `cl_confirm_disconnect_time` | 游玩达到多少分钟后，断开连接前弹出确认框（-1 = 关闭，0 = 始终确认）（QmClient 补译） | 20 | -1 | 1440 |
| `cl_confirm_quit_time` | 游玩达到多少分钟后，退出客户端前弹出确认框（-1 = 关闭，0 = 始终确认）（QmClient 补译） | 20 | -1 | 1440 |
| `cl_timeout_code` | 使用的超时代码 | "" |  |  |
| `cl_dummy_timeout_code` | 分身使用的超时代码 | "" |  |  |
| `cl_timeout_seed` | 超时种子 | "" |  |  |
| `cl_input_fifo` | 接受客户端控制台输入的 FIFO 文件（非 Windows）或命名管道（Windows）（QmClient 补译） | "" |  |  |
| `cl_config_version` | 配置版本号，供新版客户端修正旧配置中的问题（QmClient 补译） | 0 | 0 | 0 |
| `cl_demo_slice_begin` | 录像裁切的起点标记（QmClient 补译） | -1 | 0 | 0 |
| `cl_demo_slice_end` | 录像裁切的终点标记（QmClient 补译） | -1 | 0 | 0 |
| `cl_demo_show_speed` | 播放速度变动时显示速度指示器（QmClient 补译） | 0 | 0 | 1 |
| `cl_demo_show_pause` | 暂停或继续播放时显示状态指示器（QmClient 补译） | 1 | 0 | 1 |
| `cl_demo_keyboard_shortcuts` | 在回放播放器中启用键盘快捷键 | 1 | 0 | 1 |
| `gfx_gl_major` | 图形库的主版本号（QmClient 补译） | 1 | 1 | 10 |
| `gfx_gl_minor` | 图形库的次版本号（QmClient 补译） | 3 | 0 | 10 |
| `gfx_gl_patch` | 图形库的补丁版本号（QmClient 补译） | 0 | 0 | 10 |
| `gfx_gl_texture_lod_bias` | 图形库纹理采样的细节层级（LOD）偏移量，按实际值的 1000 倍存储（QmClient 补译） | -500 | -15000 | 15000 |
| `gfx_3d_texture_analysis_ran` | 是否已检查 3D 纹理和 2D 纹理数组的采样能否正常工作（QmClient 补译） | 0 | 0 | 1 |
| `gfx_3d_texture_analysis_renderer` | 运行纹理采样检查时使用的渲染器（QmClient 补译） | "" |  |  |
| `gfx_3d_texture_analysis_version` | 运行纹理采样检查时使用的版本（QmClient 补译） | "" |  |  |
| `gfx_gpu_name` | 指定图形后端使用的显卡名称，需后端支持（QmClient 补译） | "auto" |  |  |
| `gfx_backend` | 要使用的图形后端，例如 OpenGL 或 Vulkan（QmClient 补译） | "OpenGL" |  |  |
| `gfx_render_thread_count` | 图形后端用于渲染的线程数，后端可以忽略此值（QmClient 补译） | 3 | 0 | 0 |
| `gfx_driver_is_blocked` | 为 1 时表示当前驱动处于已阻止的错误状态（QmClient 补译） | 0 | 0 | 1 |
| `cl_video_recorder_fps` | 将录像导出为视频时的帧率（QmClient 补译） | 60 | 1 | 1000 |

## 客户端指令

| 指令 | 参数 | 描述 |
| --- | --- | --- |
| `exec` | r\[文件名\] | 运行指定文件 |
| `reset` | s\[客户端参数名\] | 设为默认值 |
| `toggle` | s\[客户端参数名\] i\[值1\] i\[值2\] | 按下时如为值2则切换为值1，否则切换为值2 |
| `+toggle` | s\[客户端参数名\] i\[值1\] i\[值2\] | 按下时切换为值1，松开时切换为值2 |
| `toggle_local_console` |  | 切换本地控制台（QmClient 补译） |
| `toggle_remote_console` |  | 切换远程控制台（QmClient 补译） |
| `clear_local_console` |  | 清空本地控制台 |
| `clear_remote_console` |  | 清空远程控制台 |
| `dump_local_console` |  | 将本地控制台内容写入文本文件 |
| `dump_remote_console` |  | 将远程控制台内容写入文本文件 |
| `console_page_up` |  | 控制台上一页 |
| `console_page_down` |  | 控制台下一页 |
| `callvote` | s\['kick'\|'spectate'\|'option'\] s\[id\|option text\] ?r\[reason\] | 发起投票（没人会在这里用吧） |
| `vote` | r\['yes'\|'no'\] | 投票赞成/反对（QmClient 补译） |
| `+emote` |  | 打开表情选择器 |
| `emote` | i\[emote-id\] | 使用表情 |
| `+spectate` |  | 打开旁观者模式选择器 |
| `spectate` | i\[spectator-id\] | 切换到旁观者模式 |
| `spectate_next` |  | 旁观下一个玩家 |
| `spectate_previous` |  | 旁观上一个玩家 |
| `spectate_closest` |  | 旁观最近的玩家（QmClient 补译） |
| `spectate_multiview` | i\[id\] | 添加/移除要专门旁观的客户端 ID（-1 重置）（QmClient 补译） |
| `zoom+` |  | 放大视角（QmClient 补译） |
| `zoom-` |  | 缩小视角（QmClient 补译） |
| `zoom` | ?i | 改变缩放 |
| `set_view` | i\[x\]i\[y\] | 将相机位置设置为地图中的 x 和 y（QmClient 补译） |
| `goto_switch` | i\[number\]?i\[offset\] | 查看给定编号找到的开关（在偏移处）（QmClient 补译） |
| `goto_tele` | i\[number\]?i\[offset\] | 查看给定编号找到的传送点（在偏移处）（QmClient 补译） |
| `say` | r\[message\] | 在聊天中发言 |
| `say_team` | r\[message\] | 在队伍聊天中说（QmClient 补译） |
| `chat` | s\['team'\|'all'\] ?r\[message\] | 启用与所有人/队伍聊天模式（QmClient 补译） |
| `+show_chat` |  | 显示聊天 |
| `echo` | r\[message\] | 在聊天窗口中回显文本 |
| `+left` |  | 左移 |
| `+right` |  | 右移 |
| `+jump` |  | 跳 |
| `+hook` |  | 勾 |
| `+fire` |  | 开火 |
| `+showhookcoll` |  | 显示勾索碰撞 |
| `+weapon1` |  | 切换到锤子 |
| `+weapon2` |  | 切换到枪 |
| `+weapon3` |  | 切换到霰弹枪（QmClient 补译） |
| `+weapon4` |  | 切换到榴弹枪（QmClient 补译） |
| `+weapon5` |  | 切换到激光（QmClient 补译） |
| `+nextweapon` |  | 切换到下一个武器 |
| `+prevweapon` |  | 切换到上一个武器 |
| `add_foe` | s\[name\] ?s\[clan\] | 添加一个敌对玩家（QmClient 补译） |
| `remove_foe` | s\[name\] ?s\[clan\] | 移除一个敌对玩家（QmClient 补译） |
| `foes` |  | 列出敌对玩家（QmClient 补译） |
| `add_friend` | s\[name\] ?s\[clan\] | 添加一个好友 |
| `remove_friend` | s\[name\] ?s\[clan\] | 移除一个好友 |
| `friends` |  | 好友列表 |
| `team` | i\[team-id\] | 切换团队 |
| `kill` |  | 自杀并重生 |
| `tune_zone` | i\[zone\] s\[tuning\] i\[value\] | 将变量的值调整为…… |
| `dummy_connect` |  | 连接分身 |
| `dummy_disconnect` |  | 断开分身连接 |
| `dummy_reset` |  | 重置分身操作 |
| `quit` |  | 退出客户端 |
| `exit` |  | 退出客户端 |
| `restart` |  | 重启客户端 |
| `minimize` |  | 最小化客户端 |
| `connect` | r\[主机名\|ip\] | 连接指定主机/IP |
| `disconnect` |  | 断开连接 |
| `ping` |  | 测试与当前服务器的网络延迟 |
| `screenshot` |  | 截屏 |
| `start_video` |  | （在回放播放器中）开始记录视频 |
| `stop_video` |  | （在回放播放器中）停止记录视频 |
| `rcon` | r\[远程控制台指令\] | 向远程控制台发送指定命令 |
| `rcon_auth` | r\[密码\] | 登录远程控制台 |
| `rcon_login` | s\[用户名称\] r\[密码\] | 使用用户名登录远程控制台 |
| `play` | r\[文件\] | 播放指定的回放文件 |
| `record` | ?r\[文件\] | 记录回放到文件 |
| `stoprecord` |  | 停止录制回放 |
| `add_demomarker` |  | 添加Demo时间轴标记 |
| `begin_favorite_group` |  | 在 \`add\_favorite\` 之前使用此项，以便对收藏项进行分组。最后需调用 \`end\_favorite\_group\`。 |
| `end_favorite_group` |  | 在 \`add\_favorite\` 之后使用此功能，以便对收藏项进行分组。请以 \`begin\_favorite\_group\` 作为起始。 |
| `add_favorite` | s\[域名\|IP\] ?s\['allow\_ping'\] | 收藏服务器 |
| `remove_favorite` | r\[主机名\|IP\] | 将服务器从收藏列表中移除 |
| `demo_slice_start` |  |  |
| `demo_slice_end` |  |  |
| `demo_play` |  | 开始播放回放 |
| `demo_speed` | i\[速度\] | 设置回放播放速度 |
| `save_replay` | ?i\[长度\] s\[文件名称\] | 录制过去指定秒数的画面并保存短时回放 |
| `benchmark_quit` | i\[秒数\] r\[文件\] | 记录指定秒数内的帧时间并写入文件，随后退出 |

## 参数调整

| 参数 | 描述 | 默认值 |
| --- | --- | --- |
| `ground_control_speed` | Tee仅用方向键能在地面上达到的最大速度 | 10.0 |
| `ground_control_accel` | 在地面走动时的加速度 | 2.0 |
| `ground_friction` | 地面摩擦 | 0.5 |
| `ground_jump_impulse` | 地面跳跃冲量 | 13.2 |
| `air_jump_impulse` | 空中跳跃冲量 | 12.0 |
| `air_control_speed` | Tee仅用方向键能在空中达到的最大速度 | 5.0 |
| `air_control_accel` | 在空中走动时的加速度 | 1.5 |
| `air_friction` | 空气摩擦 | 0.95 |
| `hook_length` | 钩索长度 | 380.0 |
| `hook_fire_speed` | 钩索弹出速度 | 80.0 |
| `hook_drag_accel` | 钩索拖拽加速度 | 3.0 |
| `hook_drag_speed` | 钩索拖拽最大速度 | 15.0 |
| `gravity` | 重力 | 0.5 |
| `velramp_start` | 速度坡度起始 | 550 |
| `velramp_range` | 速度坡度范围 | 2000 |
| `velramp_curvature` | 速度坡度曲率 | 1.4 |
| `gun_curvature` | 手枪子弹曲率 | 1.25\[Vanilla\] 0.0\[DDRace\] |
| `gun_speed` | 手枪子弹速度 | 2200.0 |
| `gun_lifetime` | 手枪子弹存在时长 | 2.0 |
| `shotgun_curvature` | 霰弹枪子弹曲率 | 1.25 |
| `shotgun_speed` | 霰弹枪子弹速度 | 2750.0 |
| `shotgun_speeddiff` | （未使用）霰弹枪子弹间的速度差异 | 0.8 |
| `shotgun_lifetime` | （未使用）霰弹枪子弹存在时长 | 0.20 |
| `grenade_curvature` | 榴弹曲率 | 7.0 |
| `grenade_speed` | 榴弹速度 | 1000.0 |
| `grenade_lifetime` | 榴弹存在时长 | 2.0 |
| `laser_reach` | 激光最大长度 | 800.0 |
| `laser_bounce_delay` | 激光反射延迟 | 150 |
| `laser_bounce_num` | 激光最大反射次数 | 1000 |
| `laser_bounce_cost` | 激光每次反射后扣除的长度 | 0 |
| `laser_damage` | （未使用）激光伤害 | 5 |
| `player_collision` | 开启玩家间碰撞 | 1 |
| `player_hooking` | 开启玩家间使用钩索 | 1 |
| `jetpack_strength` | 喷气背包推力 | 400.0 |
| `shotgun_strength` | 霰弹枪吸引强度 | 10.0 |
| `explosion_strength` | 爆炸强度（例如榴弹） | 6.0 |
| `hammer_strength` | 锤击强度 | 1.0 |
| `hook_duration` | 钩索抓取玩家的时长 | 1.25 |
| `hammer_fire_delay` | 最短锤击间隔时间（锤了个寂寞时） | 125 |
| `gun_fire_delay` | 最短手枪发射间隔时间 | 125 |
| `shotgun_fire_delay` | 最短霰弹发射间隔时间 | 500 |
| `grenade_fire_delay` | 最短榴弹发射间隔时间 | 500 |
| `laser_fire_delay` | 最短激光发射间隔时间 | 800 |
| `ninja_fire_delay` | 最短忍者刀冲击间隔时间 | 800 |
| `hammer_hit_fire_delay` | 最短锤击间隔时间 | 320 |
| `ground_elasticity_x` | 墙壁弹性 | 0 |
| `ground_elasticity_y` | 地面/天花板弹性 | 0 |
