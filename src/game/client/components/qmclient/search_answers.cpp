#include "search_answers.h"

#include <base/str.h>

#include <algorithm>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace qm_search_answers
{

	const std::vector<SAnswer> &Answers()
	{
		// 参考作者的 Q&A.md，按当前卡片和命令整理；不在运行时读取或修改原文。
		// 首版只维护中文。新增条目同时补充口语别名和真实卡片 ID。
		static const std::vector<SAnswer> s_vAnswers = {
			{"scoreboard-cursor", "计分板怎么呼出鼠标？",
				"在控制设置的“杂项”中绑定“计分板鼠标”，打开计分板后按绑定键切换鼠标。也可以在 F1 控制台绑定 toggle_scoreboard_cursor。示例中的 key 需要替换成你想用的按键。",
				{"计分板鼠标", "计分板光标", "计分板呼出鼠标", "toggle_scoreboard_cursor"},
				{"deck:controls-miscellaneous"}, "bind key toggle_scoreboard_cursor"},
			{"hitbox", "身上的十字架怎么关？",
				"打开下方的“碰撞箱”卡片，关闭显示碰撞箱的选项。这里控制角色碰撞范围及武器交互的可视化。",
				{"身上十字架", "十字架", "碰撞箱", "碰撞体积", "hitbox"}, {"qm:collision_hitbox"}},
			{"notifications", "右边黑黑的气泡怎么调整？",
				"右侧弹出的系统提示可以在“通知栏”卡片中调整。关闭“重要服务器提示显示为通知”和“Echo 消息改走通知栏”可停用这两类通知，也可以调整外观和过滤条件。",
				{"右边黑黑气泡", "右边气泡", "黑色气泡", "黑黑气泡", "右侧通知", "通知栏", "系统提示"}, {"qm:hud_notifications"}},
			{"screenshots", "截图保存在哪里？",
				"截图保存在客户端数据目录下的 screenshots 文件夹。点击“打开目录”会打开当前客户端实际使用的位置；使用便携配置或自定义存储路径时也以此位置为准。",
				{"截图", "截屏", "screenshots"}, {}, nullptr, "screenshots"},
			{"performance", "客户端卡顿应该怎么排查？",
				"在“调试模式”卡片中启用性能日志，重现卡顿后收集数据目录 dumps/QmClient_Perf 下新生成的日志。反馈时附上客户端版本、触发操作和当时开启的功能。完成收集后可以关闭调试模式。",
				{"卡顿", "卡卡", "掉帧", "性能日志", "性能排查", "调试模式", "帧数低", "fps低"}, {"qm:debug_mode"}, nullptr, "dumps/QmClient_Perf"},
			{"bind-status", "HUD 的卡键和分身状态在哪里关？",
				"在“DDRace HUD Pro”卡片中关闭不需要的卡键、卡锤、分身控制或分身同步状态显示。这些选项控制 HUD 提示。",
				{"卡键状态", "卡锤状态", "分身状态", "分身同步状态", "hud卡键", "ddracehudpro"}, {"qm:bind_status_hud"}},
			{"repeat", "复读怎么用？",
				"先在“梦的百宝箱”卡片中启用复读，再到“控制”的“聊天”卡片绑定复读按键。连续按两次绑定键触发复读，请避免刷屏。",
				{"复读", "复读按键", "repeat"}, {"qm:mini_features", "deck:controls-chat"}},
			{"trajectory", "武器辅助线在哪里开？",
				"在“武器”控制卡片中绑定武器弹道辅助线按键，再用“武器弹道辅助线”卡片调整显示。下面的示例将武器辅助线与钩子辅助线绑定到同一个键，使用前把 key 换成自己的键位。",
				{"武器辅助线", "武器弹道", "武器弹道辅助线", "辅助线绑定", "+showweapontrajectory"}, {"qm:weapon_trajectory", "deck:controls-weapon"}, "bind key \"+showweapontrajectory;+showhookcoll\""},
			{"game-sound", "游戏没有声音怎么办？",
				"先在“声音”设置中确认已启用声音，再检查主音量和游戏音量。若只有切到后台时没有声音，检查后台静音选项；还可以确认系统音量混合器没有将客户端静音。",
				{"游戏没声音", "游戏没有声音", "游戏声音", "游戏音量", "主音量", "没声音", "没有声音"}, {"deck:sound-toggle", "deck:sound-volume"}},
			{"voice-sound", "语音没有声音怎么办？",
				"在“语音”卡片中检查启用状态、输入和输出设备、音量及静音设置。使用卡片中的状态信息和本地测试区分麦克风采集、播放与连接问题；使用按住说话时也要检查对应绑定。",
				{"语音没声音", "语音没有声音", "语音听不到", "听不到队友", "麦克风", "没声音", "没有声音"}, {"qm:voice"}},
			{"music-sound", "音乐没有声音怎么办？",
				"先确认音乐播放器本身能播放声音。客户端的歌曲信息和歌词显示不会代替播放器输出声音。如果指的是游戏内菜单音乐，请检查声音设置中的音乐音量。",
				{"音乐没声音", "音乐没有声音", "音乐听不到", "音乐音量", "没声音", "没有声音"}, {"deck:sound-volume"}},
			{"voice-distance", "同一语音房间走远后声音变小怎么办？",
				"在“语音”卡片的距离设置中开启“同房间全图收听”，让同房间语音不受地图距离限制。“立体声定位”和距离半径也在该卡片中调整。",
				{"语音距离", "语音衰减", "走远声音变小", "同房间全图收听", "全图收听", "qm_voice_group_global"}, {"qm:voice"}, "qm_voice_group_global 1"},
			{"lyrics", "放歌时灵动岛为什么没有歌词？",
				"首先确保你使用的是网易云、酷狗、QQ 音乐、汽水或 Spotify 中的任意一种。酷狗和 QQ 音乐用户请注意：我们只支持特定版本，可以从群内下载。网易云用户请检查是否打开了 SMTC；汽水和 Spotify 用户请私聊群主。",
				{"歌词", "灵动岛歌词", "没有歌词", "不显示歌词", "歌名", "歌曲信息", "smtc", "网易云"}, {"qm:lyrics", "qm:dynamic_island"}},
			{"spectator-eye", "旁观的小眼睛去哪里了？",
				"启用灵动岛时，旁观相关提示会显示在顶部灵动岛中。可以在“灵动岛”卡片调整显示，或关闭灵动岛使用原有 HUD 样式。",
				{"旁观眼睛", "旁观小眼睛", "小眼睛", "旁观提示"}, {"qm:dynamic_island"}},
			{"spectate-key", "旁观模式选人的按键是什么？",
				"在“控制”的“杂项”卡片中绑定“旁观模式”。也可以使用下面的 +spectate 绑定命令；把 key 替换为想使用的按键。",
				{"旁观选人", "旁观模式按键", "旁观按键", "+spectate"}, {"deck:controls-miscellaneous"}, "bind key \"+spectate\""},
			{"server-info", "服务器信息不显示怎么办？",
				"在“梦的百宝箱”卡片中关闭“进图时隐藏服务器信息”。如果手动将 cl_motd_time 设为了 0，可以改成 10 或其他非零值。",
				{"服务器信息", "进图信息", "motd", "cl_motd_time"}, {"qm:mini_features"}, "cl_motd_time 10"},
			{"title-backup", "重装电脑前如何备份赞助头衔？",
				"备份客户端数据目录 qmclient 下的 title_token.txt，并在新环境的数据目录中放回同一相对位置。点击“打开目录”可定位到该文件所在目录。这个文件包含头衔凭据，请自行保管，不要发到群里。",
				{"备份头衔", "头衔备份", "重装头衔", "title_token.txt", "头衔丢失"}, {"deck:qmclient-contributors-title"}, nullptr, "qmclient"},
			{"translation", "聊天翻译怎么用？",
				"在“翻译”卡片中选择翻译服务和目标语言，并开启自动翻译接收消息。需要凭据的服务要先填写相应配置，再用卡片中的测试按钮检查服务。接收翻译、发送翻译和译文显示可以分别调整。",
				{"翻译", "聊天翻译", "翻译服务", "翻译测试"}, {"qm:translate"}},
			{"sponsor", "如何赞助？我能得到什么？",
				"在“贡献者 → 栖梦 → 赞助支持”中显示赞助二维码。赞助后可联系群主确认记录和兑换码；兑换后可设置显示在聊天与名牌中的自定义头衔，并选择铂金高亮、流体等发言样式。具体发放事宜请与群主确认，下方卡片用于兑换和设置头衔。",
				{"赞助", "赞助支持", "赞助权益", "赞助有什么", "怎么赞助", "如何赞助", "赞助能得到什么", "赞助有什么好处"}, {"deck:qmclient-contributors-title"}},
			{"missing-credit", "贡献者名单里面没有我怎么办？",
				"先确认名单已加载，赞助者名单可以点击重新加载。如果赞助后仍没有名字，请带上赞助记录截图和希望显示的名字私信群主。代码、翻译或素材贡献未署名时，请附上相关 PR、提交或作品说明联系维护者，方便核对和补录。",
				{"贡献者名单没有我", "贡献者名单里面没有我", "赞助名单没有我", "赞助者名单没有我", "名单漏了", "补署名", "贡献没有署名"}, {}},
			{"octagonal-aim", "八角定位的 bind 是什么？",
				"bind <key> \"+toggle cl_mouse_max_distance 2 330; +toggle inp_mousesens 1 114\"，其中 <key> 替换成你所需要的键位，330 改成你的最大鼠标距离，默认 400，114 改成你自己的游戏内灵敏度。",
				{"八角定位", "八角定位bind", "八角定位绑定", "八角", "45度瞄准", "45°瞄准", "45度定位"}, {"qm:bind_editor", "deck:controls-mouse"}, "bind <key> \"+toggle cl_mouse_max_distance 2 330; +toggle inp_mousesens 1 114\""},
			{"held-keys", "卡键是什么？",
				"卡键是让角色的某项输入持续保持按下状态，常见的有卡方向、卡跳、卡锤和卡钩。具体行为取决于绑定命令及分身控制设置。想解除时，先关闭对应的持续输入或分身开关，并检查相关按键绑定；HUD 的卡键状态开关只控制提示显示。",
				{"卡键", "卡键是什么意思", "什么是卡键", "卡锤", "卡钩", "卡方向", "持续按键"}, {"deck:controls-movement", "deck:controls-dummy", "qm:bind_status_hud"}},
			{"crash", "游戏崩溃了怎么办？",
				"记录客户端版本、崩溃时间、所在地图和崩溃前的操作。打开数据目录 dumps/QmClient_Crash，保留同一时间生成的报告并交给维护者；Windows 上可能包含 .RTP、.dmp 和文本报告。没有生成报告时也可以反馈现象。若客户端无法启动，可通过系统文件管理器进入数据目录取文件。",
				{"游戏崩溃", "游戏崩溃了", "客户端崩溃", "崩溃", "闪退", "崩溃报告", "crash"}, {}, nullptr, "dumps/QmClient_Crash"},
			{"steam-start", "怎么启动 QmClient 的同时拉起 Steam？",
				"在下方“Steam 集成”卡片中开启“外部启动客户端时自动启动 Steam”，下次启动 QmClient 时生效。也可以在 F1 输入下面的命令后正常退出并重新启动客户端。此开关默认关闭，关闭后不再自动拉起 Steam；Steam 程序路径留空时会自动检测。",
				{"启动qmclient的同时拉起steam", "同时拉起steam", "自动启动steam", "拉起steam", "qm_steam_auto_launch", "steam自动启动", "联动steam"}, {"qm:steam"}, "qm_steam_auto_launch 1"},
			{"bug-report", "有 BUG 怎么办？",
				"先确认客户端版本，并检查最新版是否仍有相同问题。反馈时说明操作步骤、预期结果、实际结果、发生频率及相关设置，并附截图或录屏。通过“贡献者 → 栖梦”的官方社区入口联系维护者；崩溃附崩溃报告，卡顿附性能日志，不要只发一句“有 BUG”。",
				{"有bug", "bug", "bug反馈", "反馈bug", "反馈问题", "报告问题", "功能异常"}, {}},
			{"hammer-double-sound", "为什么锤人的声音有两遍？",
				"你需要关闭预测事件。在下方 DDNet 设置卡片中关闭“预测事件”，也可以在 F1 控制台输入 cl_predict_events 0。",
				{"锤人的声音有两遍", "锤人声音两遍", "锤子声音重复", "锤人双重声音", "锤人两次声音", "锤人重音", "锤子音效两遍"}, {"deck:ddnet-gameplay"}, "cl_predict_events 0"},

			// 经作者确认的常用设置与资源问答。
			{"hook-countdown", "钩子倒计时怎么开？",
				"在“灵动岛”卡片中，先关闭“使用原始样式”，再开启“启用钩子倒计时”。钩住玩家后，钩链中间会显示倒计时圆环；无限钩会显示对应标记。",
				{"钩子倒计时", "钩索倒计时", "钩子计时", "钩链倒计时", "钩子圆环", "qm_hook_countdown"}, {"qm:dynamic_island"}, "qm_hook_countdown 1"},
			{"skin-colors", "怎么给皮肤调色？",
				"进入“设置 → Tee → Tee”，开启玩家信息下方的“自定义颜色”，就可以分别调整皮肤的身体和脚部颜色。",
				{"给皮肤调色", "皮肤调色", "皮肤颜色", "身体颜色", "脚部颜色", "自定义颜色", "皮肤改色"}, {"deck:tee-identity"}},
			{"audio-packs", "音效有什么用、怎么使用、在哪里下载？",
				"音频包可以替换游戏中的声音，例如跳跃、锤击和钩子音效。加入 QQ 群 1076765929 获取，入群答案为“栖梦QimenG”。下载后解压，将音频包文件夹放进“设置 → 声音 → 音频包 → 音频包目录”，刷新列表并选中即可使用。",
				{"音效", "音效有啥用", "音效有什么用", "音效怎么使用", "音效怎么用", "音效在哪下", "音效下载", "音频包", "音效包", "音效包放哪里", "替换音效", "下载音效", "音效群"}, {"deck:sound-audio-pack"}, nullptr, "audio"},
			{"chat-export", "导出的聊天记录在哪里？",
				"在客户端数据目录的 qmclient/chat_log 文件夹中。可以点击下方“打开目录”直接查看，也可以点 F1 控制台的“配置目录”打开数据目录，再进入该文件夹。手动导出会生成 TXT、HTML 和 PNG 文件。",
				{"导出的聊天记录", "聊天记录导出", "导出聊天记录", "导出聊天", "聊天记录保存位置", "聊天记录文件夹", "聊天导出目录", "聊天记录在哪里", "聊天记录在哪", "聊天记录"}, {}, nullptr, "qmclient/chat_log"},
			{"tee-status-bar", "顶部一排皮肤头像怎么关掉？",
				"在“设置 → TClient”的“Tee 状态栏”卡片中，关闭“显示 Tee 状态栏”。也可以在 F1 输入 qm_frozen_tees_hud 0。",
				{"顶部头像", "上面一排皮肤", "顶部一排皮肤", "上面一排头像", "皮肤栏", "头像栏", "队友头像", "玩家头像栏", "Tee状态栏", "冻结状态栏", "这一类皮肤", "qm_frozen_tees_hud"}, {"tclient:tee-status-bar"}, "qm_frozen_tees_hud 0"},
			{"player-ids", "名字前面的数字可以关掉吗？",
				"可以。名字前面的数字是玩家 ID。在“设置 → 游戏界面 → HUD”中，关闭“显示客户端 ID（计分板、聊天、观战）”。也可以在 F1 输入 cl_show_ids 0。",
				{"名字前面的数字", "聊天数字", "玩家编号", "玩家ID", "名字前面的冒号", "关闭ID", "聊天ID", "聊天名字前面的数字", "客户端ID", "cl_show_ids"}, {"deck:appearance-hud-ddrace"}, "cl_show_ids 0"},
			{"super-emote", "可以一直发大表情的指令是什么？",
				"在 F1 输入 super_emote 0 可以直接发大表情，0 是表情编号，可换成 0～15。想随时按键发送，可以使用下面的绑定，将 key 换成自己的按键。每按一次发送一个大表情，无需蓄力。",
				{"大表情", "发大表情", "大表情指令", "大表情命令", "一直发大表情", "大表情绑定", "无需蓄力", "不蓄力发大表情", "super_emote"}, {"qm:emoticons"}, "bind key \"super_emote 0\""},

			// 原版 DDNet 的常用操作。
			{"player-name", "怎么修改游戏名字和战队？",
				"在下方“玩家”卡片中修改名字和战队。注意当前编辑的是本体还是分身，两者可以分别设置；使用赞助头衔的昵称绑定时，改名后也要检查头衔绑定的名字是否一致。",
				{"改名", "修改名字", "游戏名字", "改战队", "修改战队", "分身名字", "player_name"}, {"deck:player-identity"}},
			{"skins", "怎么更换角色皮肤？",
				"在 Tee 设置的皮肤列表中选择皮肤，并确认当前编辑的是本体还是分身。普通自定义皮肤可放入当前数据目录的 skins 文件夹，然后刷新皮肤列表；0.7 皮肤使用独立的皮肤编辑入口。",
				{"换皮肤", "更换皮肤", "角色皮肤", "皮肤放哪里", "自定义皮肤", "皮肤列表"}, {"deck:tee-skin-list"}, nullptr, "skins"},
			{"dummy", "如何连接和控制分身？",
				"进入服务器后可以在菜单连接分身，也可在 F1 输入 dummy_connect。下方“分身”控制卡片可绑定切换本体/分身、同步动作和分身锤等操作；断开分身的命令是 dummy_disconnect。分身能否进入仍受服务器人数和连接限制影响。",
				{"分身", "连接分身", "控制分身", "分身同步", "切换分身", "dummy_connect", "dummy_disconnect"}, {"deck:controls-dummy"}, "dummy_connect"},
			{"mouse-sensitivity", "鼠标灵敏度在哪里调？",
				"在“控制”的“鼠标”卡片中调整游戏内灵敏度和准心距离。若只有按住某个绑定键时灵敏度变化，请同时检查该键是否绑定了临时灵敏度命令，例如缝隙救人或 45° 瞄准。",
				{"鼠标灵敏度", "灵敏度", "鼠标太快", "鼠标太慢", "准心距离", "inp_mousesens"}, {"deck:controls-mouse"}},
			{"entities", "怎么开启实体层？",
				"在下方“实体层调整”卡片中调整实体层叠加比例和各类实体的显示。也可以在“控制 → 杂项”绑定“显示实体层”；下面的绑定会在 0 和 100 之间切换，使用前将 key 换成自己的键位。",
				{"实体层", "显示实体层", "地图实体", "实体层透明度", "cl_overlay_entities"}, {"qm:entity_overlay", "deck:controls-miscellaneous"}, "bind key \"toggle cl_overlay_entities 0 100\""},
			{"demo-recording", "怎么录制和查看游戏回放？",
				"进入服务器后，在 F1 输入 record 开始录制，输入 stoprecord 停止。录制文件保存在数据目录 demos 中，可从主菜单的“回放”入口查看。下方录制卡片可设置自动录制；Demo 是游戏回放文件，不是可直接上传的视频。",
				{"录制回放", "游戏回放", "录像", "录demo", "demo", "自动录制", "回放文件", "stoprecord"}, {"deck:general-recording", "deck:ddnet-demo"}, "record", "demos"},
			{"hook-line", "钩子辅助线怎么开？",
				"在控制设置中绑定“钩子碰撞线”，按住绑定键即可显示。下方“钩子碰撞线”卡片可以调整外观；也可以复制 +showhookcoll 的绑定示例，将 key 换成自己的按键。",
				{"钩子辅助线", "钩线", "钩子碰撞线", "钩索辅助线", "+showhookcoll"}, {"deck:controls-movement", "deck:appearance-hook-collision-main"}, "bind key \"+showhookcoll\""},
			{"team-chat", "怎么发队伍聊天？",
				"在“控制 → 聊天”卡片中分别绑定公共聊天和队伍聊天。队伍聊天发送给当前队伍，不等于给某个玩家私聊；下方示例用于打开队伍聊天输入框，key 需要替换为自己的按键。",
				{"队伍聊天", "队内聊天", "团队聊天", "公共聊天", "聊天按键"}, {"deck:controls-chat"}, "bind key \"+show_chat; chat team\""},
			{"bind-basics", "bind 命令怎么用？",
				"在 F1 控制台使用 bind 键名 \"命令\"。示例中的 key 是占位文字，使用前需要换成真实按键；重新绑定同一个键会替换原绑定。多条命令可以放在同一对引号中，用英文分号分隔。也可用下方绑定编辑器查看和修改。",
				{"bind", "bind怎么用", "按键绑定", "绑定命令", "key替换", "bind里的key", "组合绑定"}, {"qm:bind_editor", "qm:bind_overview"}, "bind key \"+jump\""},
			{"unbind", "怎么取消一个按键绑定？",
				"可以在控制设置中清除对应绑定，或在 F1 输入 unbind key，并把 key 换成要清除的按键。只取消一个键时不要使用 unbindall，它会清空全部绑定。重新使用该键前，可以在绑定编辑器中设置需要的命令。",
				{"取消绑定", "删除绑定", "解除绑定", "清除绑定", "unbind"}, {"qm:bind_editor", "qm:bind_overview"}, "unbind key"},

			// TClient 的常用工具。
			{"tc-bind-wheel", "TC 绑定轮盘怎么用？",
				"在“绑定轮盘”卡片中填写名称和控制台命令，添加条目，再绑定轮盘按键。打开轮盘后按界面提示选择条目；这里填写的是控制台命令，发送聊天消息时应使用 say 开头。下面的 +bindwheel 示例用于绑定轮盘入口。",
				{"绑定轮盘", "轮盘", "tc轮盘", "tc绑定轮盘", "tclient轮盘", "bindwheel", "+bindwheel"}, {"deck:tclient-bind-wheel-editor"}, "bind key \"+bindwheel\""},
			{"tc-warlist", "TC 敌对列表怎么用？",
				"在“敌对列表 / War List”卡片中启用列表，添加玩家或战队条目，并选择分组和颜色。聊天、计分板和旁观选择中的颜色可分别控制；屏蔽敌对聊天与完全屏蔽敌对玩家也是独立选项，请按需要开启。",
				{"敌对列表", "战争列表", "warlist", "tc敌对列表", "拉黑玩家", "敌对颜色"}, {"deck:tclient-warlist"}},
			{"tc-profiles", "TC 配置方案能保存什么？",
				"“配置方案 / Profiles”用于保存和切换身份等预设。保存或加载前检查名字、战队、皮肤、颜色、国旗和表情等选项，以及当前目标是本体还是分身。页面还提供语录、资源和轮盘相关入口，不应把单个身份预设当成整个客户端配置的备份。",
				{"配置方案", "身份预设", "保存身份", "切换身份", "profiles", "tc配置", "tc配置方案"}, {"deck:tclient-profiles"}},
			{"tc-status-bar", "TC 状态栏怎么开关和调整？",
				"在“状态栏”卡片中开关显示，并调整高度、背景、文字及时间格式。状态方案和条目内容可以在预览卡片中设置；Tee 身旁的状态条与这条状态栏使用不同设置。",
				{"状态栏", "tc状态栏", "tclient状态栏", "状态栏高度", "底部状态栏"}, {"deck:tclient-status-bar-settings", "deck:tclient-status-bar-preview"}},
			{"tc-rainbow", "TC 彩虹颜色在哪里设置？",
				"在下方“彩虹”卡片中分别选择 Tee、钩索、武器以及其他玩家的彩虹效果，并调整颜色变化速度。不需要某类效果时关闭对应项即可。",
				{"彩虹", "彩虹颜色", "彩虹钩子", "彩虹皮肤", "tc彩虹", "rainbow"}, {"tclient:rainbow"}},
			{"tc-font", "怎么更换客户端字体？",
				"在下方“字体”卡片中选择字体和字重，也可以打开字体商店。若中文和英文字体表现不同，请分别检查普通字体与中日韩字体设置。调整后观察文字显示，再按需要修改字重。",
				{"字体", "换字体", "中文字体", "字体商店", "字体字重", "tc字体", "tclient字体"}, {"tclient:font"}},

			// QmClient 的其他常用功能。
			{"chat-bubble", "头顶的聊天气泡怎么关？",
				"在“游戏界面 → 聊天”的“聊天气泡”卡片中关闭聊天气泡，也可以调整样式、文字大小、持续时间和位置。这张卡片控制玩家头顶的聊天内容；右侧系统通知在“通知栏”卡片中设置。",
				{"聊天气泡", "头顶气泡", "头顶聊天", "漫画气泡", "气泡"}, {"qm:chat_bubble"}},
			{"friend-code", "好友列表怎么备份或分享？",
				"在“好友提醒”卡片中复制好友码，保存到自己保管的位置。导入时先粘贴并查看预览，默认合并到现有好友列表；选择替换会覆盖现有好友及分组等信息。好友码包含名字、备注和分组，只发给你希望分享的人。",
				{"好友码", "备份好友", "好友备份", "分享好友", "导入好友", "导出好友", "好友列表备份"}, {"qm:friend_notify"}},
			{"self-kill", "怎么防止误按自杀键？",
				"在“客户端侧自杀保护”卡片中设置保护，可选择进图后自动启用保护的延迟。需要主动自杀时先确认保护是否开启；该功能针对客户端主动发出的自杀操作，不能阻止地图中的死亡。",
				{"自杀保护", "防误杀", "误按自杀", "自杀键", "自杀不了", "按k没反应"}, {"qm:self_kill_protection"}},
			{"coordinates", "怎么显示坐标和对齐提示？",
				"在“坐标”卡片中选择显示自己、其他玩家以及 X、Y 坐标。需要对齐提示时开启“与我 X 对齐提示”，再按需要设置严格模式和检测时间。",
				{"坐标", "对齐提示", "x对齐", "自己坐标", "他人坐标"}, {"qm:coords"}},
			{"steam-path", "自动启动时找不到 Steam 怎么办？",
				"先确认已安装 Steam。在“Steam 集成”卡片中点击重新检测；仍找不到时填写 Steam 客户端程序的完整路径，Windows 上通常是 steam.exe。路径中不要附加启动参数。填写无效路径时会回退到自动检测，下次启动 QmClient 时再尝试拉起 Steam。",
				{"找不到steam", "steam路径", "steam没启动", "steam启动失败", "qm_steam_client_path"}, {"qm:steam"}},
			{"steam-playtime", "Steam 为什么没有记录游戏时长？",
				"先确认 Steam 已启动并登录，再检查它是否显示 DDNet 正在运行。自动拉起 Steam 不保证 Steam 接口初始化成功；如果启动时还没登录完成，可登录后重新启动 QmClient。仍未关联时检查安装包的 Steam 组件是否完整，并把客户端版本和启动方式反馈给维护者。",
				{"steam时长", "steam游戏时长", "steam不计时", "steam没记录时间", "steam不显示游戏中"}, {"qm:steam"}},
		};
		return s_vAnswers;
	}

	namespace
	{
		std::string Normalize(const char *pText)
		{
			std::string Result;
			while(*pText)
			{
				int Codepoint = str_utf8_decode(&pText);
				if(Codepoint < 0)
					return {};
				if(Codepoint >= 0xff01 && Codepoint <= 0xff5e)
					Codepoint -= 0xfee0;
				if(Codepoint >= 'A' && Codepoint <= 'Z')
					Codepoint += 'a' - 'A';
				if(Codepoint <= ' ' || Codepoint == 0x3000 || Codepoint == 0x3001 || Codepoint == 0x3002 ||
					Codepoint == '?' || Codepoint == '!' || Codepoint == ',' || Codepoint == ':' || Codepoint == ';' ||
					Codepoint == '"' || Codepoint == '\'' || Codepoint == 0x201c || Codepoint == 0x201d)
					continue;
				char aEncoded[4];
				const int Length = str_utf8_encode(aEncoded, Codepoint);
				Result.append(aEncoded, Length);
			}
			// 只去掉询问和操作措辞；保留“没有”“不显示”等能区分故障的内容。
			for(const char *pWord : {"开启", "关闭", "打开", "关掉", "调整", "请问", "我想", "能不能", "可不可以", "应该", "怎么办", "如何用", "怎么用", "怎么关", "怎么开", "如何", "为什么", "为啥", "怎么", "在哪里关", "在哪里开", "在哪关", "在哪开", "在哪里", "在哪", "哪里", "是什么", "一下", "我的", "这个", "那个", "的", "我", "呀", "啊", "呢", "吗"})
			{
				const size_t Length = str_length(pWord);
				for(size_t Pos = Result.find(pWord); Pos != std::string::npos; Pos = Result.find(pWord, Pos))
					Result.erase(Pos, Length);
			}
			return Result;
		}

		struct SIndexedAnswer
		{
			const SAnswer *m_pAnswer;
			std::vector<std::string> m_vPhrases;
		};

		const std::vector<SIndexedAnswer> &Index()
		{
			static const auto s_vIndex = [] {
				std::vector<SIndexedAnswer> vIndex;
				for(const SAnswer &Answer : Answers())
				{
					SIndexedAnswer Entry{&Answer, {Normalize(Answer.m_pQuestion)}};
					for(const char *pAlias : Answer.m_vAliases)
						Entry.m_vPhrases.push_back(Normalize(pAlias));
					vIndex.push_back(std::move(Entry));
				}
				return vIndex;
			}();
			return s_vIndex;
		}
	}

	SResult Search(const char *pQuery)
	{
		SResult Result;
		if(pQuery == nullptr)
			return Result;
		// 先按既有字节上限截断扫描，超长输入不再先遍历整个 C 字符串。
		size_t Length = 0;
		while(Length <= 512 && pQuery[Length] != '\0')
			++Length;
		if(Length > 512 || !str_utf8_check(pQuery))
			return Result;
		const std::string Query = Normalize(pQuery);
		if(Query.empty())
			return Result;
		const auto Codepoints = std::count_if(Query.begin(), Query.end(), [](unsigned char Character) { return (Character & 0xc0) != 0x80; });
		struct SCandidate
		{
			const SAnswer *m_pAnswer;
			int m_Score;
		};
		std::vector<SCandidate> vCandidates;
		for(const auto &Entry : Index())
		{
			int Score = 0;
			for(const std::string &Phrase : Entry.m_vPhrases)
			{
				if(Phrase.empty())
					continue;
				if(Query == Phrase)
					Score = 100;
				else if(Query.find(Phrase) != std::string::npos)
					Score = std::max(Score, static_cast<int>(80 * Phrase.size() / Query.size()));
				else if(Codepoints >= 2 && Query.size() >= 3 && Phrase.find(Query) != std::string::npos)
					Score = std::max(Score, static_cast<int>(60 * Query.size() / Phrase.size()));
			}
			if(Score > 0)
				vCandidates.push_back({Entry.m_pAnswer, Score});
		}
		std::stable_sort(vCandidates.begin(), vCandidates.end(), [](const auto &Left, const auto &Right) { return Left.m_Score > Right.m_Score; });
		// 只有唯一完整匹配才直接答复；部分匹配或同分歧义交给玩家选择。
		if(!vCandidates.empty() && vCandidates.front().m_Score == 100 && (vCandidates.size() == 1 || vCandidates[1].m_Score < 100))
			Result.m_pAnswer = vCandidates.front().m_pAnswer;
		for(size_t Index = 0; Index < std::min(size_t{5}, vCandidates.size()); ++Index)
			Result.m_vCandidates.push_back(vCandidates[Index].m_pAnswer);
		return Result;
	}

	bool TrySearch(const char *pQuery, SResult &Out)
	{
		try
		{
			SResult Candidate = Search(pQuery);
			Out.m_vCandidates.swap(Candidate.m_vCandidates);
			std::swap(Out.m_pAnswer, Candidate.m_pAnswer);
			return true;
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
	}
}
