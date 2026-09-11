#include "QmCardRegistry.h"

#include <base/system.h>

#include <engine/shared/localization.h>

#include <game/localization.h>

#include <iterator>
#include <utility>

namespace qm_card_registry
{
	static constexpr const char *s_apTClientMainCards[] = {
		"tclient:visual-font-cursor",
		"tclient:visual-nameplates",
		"tclient:visual-effects",
		"tclient:input",
		"tclient:anti-latency-tools",
		"tclient:improved-anti-ping",
		"tclient:execute-on-join",
		"tclient:voting",
		"tclient:auto-reply",
		"tclient:player-indicator",
		"tclient:pet",
		"tclient:hud",
		"tclient:tee-status-bar",
		"tclient:tile-outlines",
		"tclient:ghost-tools",
		"tclient:rainbow",
		"tclient:tee-trails",
		"tclient:background-draw",
		"tclient:finish-name",
	};

	bool IsTClientMainCard(const char *pStableId)
	{
		if(pStableId == nullptr)
			return false;
		for(const char *pMainCard : s_apTClientMainCards)
			if(str_comp(pStableId, pMainCard) == 0)
				return true;
		return false;
	}

	bool IsTClientMainCardsLegacyLeft(const qm_card_order::CModel &Model)
	{
		int TClientCardCount = 0;
		for(int EntryIndex = 0; EntryIndex < Model.Count(); ++EntryIndex)
		{
			const qm_card_order::SEntry &Entry = Model.Entry(EntryIndex);
			if(Entry.m_pDefaultTab != nullptr && str_comp(Entry.m_pDefaultTab, "tclient") == 0)
			{
				if(!IsTClientMainCard(Entry.m_pStableId))
					return false;
				++TClientCardCount;
			}
		}
		if(TClientCardCount != static_cast<int>(std::size(s_apTClientMainCards)))
			return false;

		for(size_t Index = 0; Index < std::size(s_apTClientMainCards); ++Index)
		{
			const int EntryIndex = Model.FindByStableId(s_apTClientMainCards[Index]);
			if(EntryIndex < 0)
				return false;
			const qm_card_order::SEntry &Entry = Model.Entry(EntryIndex);
			if(Entry.m_pDefaultTab == nullptr || str_comp(Entry.m_pDefaultTab, "tclient") != 0 || Entry.m_Column != 1 || Entry.m_OrderInColumn != static_cast<int>(Index))
				return false;
		}
		return true;
	}

	bool MoveTClientMainCardsToAlternatingColumns(qm_card_order::CModel &Model)
	{
		if(!IsTClientMainCardsLegacyLeft(Model))
			return false;
		for(size_t Index = 0; Index < std::size(s_apTClientMainCards); ++Index)
			Model.MoveToTab(s_apTClientMainCards[Index], "tclient", Index % 2 == 0 ? 1 : 2, static_cast<int>(Index / 2));
		return true;
	}

	ETClientMainCardsMigrationResult MigrateTClientMainCardsToAlternatingColumns(qm_card_order::CModel &Model, char *pSerialized, const int SerializedSize)
	{
		if(!IsTClientMainCardsLegacyLeft(Model))
		{
			if(!Model.IsDirty())
				return ETClientMainCardsMigrationResult::NOT_LEGACY;
			return Model.Serialize(pSerialized, SerializedSize) ? ETClientMainCardsMigrationResult::PERSISTED_DIRTY : ETClientMainCardsMigrationResult::PERSIST_FAILED;
		}

		std::vector<qm_card_order::SEntry> vCandidateEntries;
		vCandidateEntries.reserve(Model.Count());
		for(int EntryIndex = 0; EntryIndex < Model.Count(); ++EntryIndex)
			vCandidateEntries.push_back(Model.Entry(EntryIndex));
		qm_card_order::CModel Candidate;
		Candidate.SetEntries(std::move(vCandidateEntries));
		MoveTClientMainCardsToAlternatingColumns(Candidate);
		if(!Candidate.Serialize(pSerialized, SerializedSize))
			return ETClientMainCardsMigrationResult::PERSIST_FAILED;

		MoveTClientMainCardsToAlternatingColumns(Model);
		return ETClientMainCardsMigrationResult::MIGRATED;
	}
	static const std::vector<SCardDefault> &DefaultsTable()
	{
		// clang-format off
		static const std::vector<SCardDefault> s_aDefaults = {
			// === 栖梦侧栏模块（39）· qm:<key>（显式默认值齐全，来源 s_aQmModuleDefaults）===
			{"qm:info", "visual", ECardColumn::Full, 0, "QmClient", "qmclient info", "QmClient information and project links"},
			{"qm:chat_bubble", "visual", ECardColumn::Left, 0, "Chat bubble", "消息气泡 liaotian qipao chat bubble typing 预览 yulan 镜头缩放 suofang 持续时间 chixu 透明度 touming 字体大小 ziti 最大宽度 kuandu 垂直偏移 pianyi 圆角 yuanjiao visual", "Show chat messages above players"},
			{"qm:camera_view", "visual", ECardColumn::Right, 0, "Camera view", "镜头 jingtou camera drift 漂移 piaoyi dynamic fov 动态视野 dongtai shiye 纵横比 zonghengbi aspect ratio preset 预设 yushe 自定义 zidinyi 视野视角 shijiao visual", "Adjust game camera and FOV settings"},
			{"qm:skin_transition", "visual", ECardColumn::Left, 1, "Skin transition", "皮肤切换 pifu qiehuan skin transition 换皮 huanpi 动画 donghua 开关 kaiguan 类型 leixing 时长 shichang 强度 qiangdu easing 缓动 huandong 锤中偷皮 chuizhong toupi 故障 guzhang glitch 抖动 doudong 弹性 tanxing elastic Tee外观 tee waiguan 循环色调 xunhuan sediao hue 速度 sudu 分身 fenshen dummy visual", "Configure hammer skin steal and skin transition animations"},
			{"qm:focus_mode", "visual", ECardColumn::Left, 2, "Focus mode", "禅模式 zhuanzhi moshi focus mode zen mode 隐藏 yincang hud 名字 mingzi 特效 texiao 计分板 jifenban 沉浸 chenjing 无干扰 wuganrao 聊天 liaotian chat 非必要UI visual", "Hide UI for focused gameplay"},
			{"qm:weapon_animation", "visual", ECardColumn::Right, 1, "Weapon animation", "武器动画 wuqi donghua weapon animation 切换武器动画 qiehuan wuqi donghua weapon switch animation 装填动画 zhuangtian donghua reload animation 概率 gailv probability 滑入 huaru 旋转 xuanzhuan visual", "Play a slide-in rotation animation when switching weapons"},
			{"qm:entity_overlay", "visual", ECardColumn::Right, 2, "Entity overlay", "实体层颜色 shiti ceng yanse 实体层 shiti entity overlay 死亡透明度 siwang 冻结透明度 dongjie 解冻透明度 jiedong 深度冻结 shendu dongjie 深度解冻 shendu jiedong 传送透明度 chuansong cp点透明度 cp checkpoint 开关透明度 kaiguan 叠层透明度 dieceng visual", "Adjust opacity of entity layers"},
			{"qm:collision_hitbox", "visual", ECardColumn::Right, 5, "Collision hitbox", "碰撞箱模式 pengzhuangxiang moshi 碰撞体积可视化 pengzhuang tiji keshihua collision hitbox hitbox mode 显示碰撞 武器交互 透明度 visual", "Show collision and weapon interaction"},
			{"qm:streamer", "visual", ECardColumn::Left, 10, "Streamer mode", "主播模式 zhubo moshi 直播 zhibo 隐私 yinsi 非好友昵称改id feihaoyou nicheng id 非好友皮肤默认 pifu moren 计分板默认国旗 guoqi visual", "Protect names and skins while streaming"},
			{"qm:translate_ui", "function", ECardColumn::Left, 15, "Translate UI", "fanyi ui 颜色 yanse color 按钮 anniu button 菜单 caidan menu rgba 自定义 zidingyi custom function", "Customize translate button and menu colors"},
			{"qm:gores_actor", "function", ECardColumn::Left, 3, "Gores actor", "gores 演员 actor 掉水 diaoshui 自动发言 zidong fayan 表情 biaoqing 表情id emoticon 发送概率 gaolv function", "Auto chat when dying in water"},
			{"qm:gores", "function", ECardColumn::Left, 4, "Gores", "gores kog king of gores 锤枪切换 chuichang qiehuan 自动切枪 zidong qieqiang 自动切锤 zidong qiechui gun hammer prevweapon fire 开火后切锤 kaihuo qiechui 拿到其他武器停用 快速输入 kuaisu shuru fast input 快速输入其他玩家 function", "Gores auto weapon switch"},
			{"qm:key_binds", "function", ECardColumn::Left, 5, "Key binds", "按键绑定 anjian bangding bind 快捷键 kuaijiejian 常用绑定 changyong bangding 武器辅助线 fuzhuxian 异常断开 yichang duankai timeout disconnect function", "Common key bindings"},
			{"qm:mini_features", "function", ECardColumn::Left, 6, "Mini features", "梦的小功能 meng xiaogongneng 粒子拖尾 lizi tuowei 远程粒子 yuancheng lizi 计分板查分 chafen 聊天框淡出 liaotian danchu 表情选择 biaoqing xuanze 动画优化 donghua youhua 复读 fudu 锤人换皮 chuiren huanpi 随机表情 suiji biaoqing 连击 lianji combo 说话不弹表情 shuo hua biaoqing 本地彩虹名字 caihong mingzi 计分板Qm标识 qm biaoshi scoreboard badge 更新 gengxin 版本 banben 过旧 guojiu 提示 tishi outdated version warning 新版UI xinban ui settings page shezhi yemian 新版IME xinban ime 输入法 shurufa 候选栏 houxuanlan 自动管理 zidong guanli 进程优先级 jincheng youxianji 协作制图 xiezuo zhitu 多人制图 duoren zhitu function", "Configure Dream-only convenience features"},
			{"qm:jump_hint", "function", ECardColumn::Left, 7, "Jump hint", "位置跳跃提示 tiaoyue tishi jump hint position edge jump color yanse 颜色 horizontal position shuiping weizhi vertical position chuizhi weizhi font size ziti function", "Customize the position jump hint"},
			{"qm:weapon_trajectory", "function", ECardColumn::Left, 8, "Weapon trajectory", "武器辅助线 wuqi fuzhuxian weapon trajectory 弹道辅助线 dandao fuzhuxian 手枪辅助线 shouqiang fuzhuxian pistol guide line 预测忍者路径 yuce renzhe lujing predict ninja path 线宽 xian kuan 透明度 toumingdu 始终显示 shizhong xianshi 按键显示 anjian xianshi function", "Show grenade and laser trajectory preview"},
			{"qm:coords", "hud", ECardColumn::Left, 9, "Coordinates", "显示坐标 xianshi zuobiao coords position 自己坐标 ziji 他人坐标 taren 显示x xianshi x 显示y xianshi y 对齐提示 duiqi tishi 严格对齐 yange duiqi hud", "Show coordinates above players"},
			{"qm:friend_notify", "function", ECardColumn::Left, 11, "Friend notify", "好友提醒 haoyou tixing 好友上线 shangxian 自动刷新 zidong shuaxin 服务器列表 fuwuqi liebiao 刷新间隔 jiange 进图打招呼 jintu dazhaohu 大字显示 dazi xianshi function", "Friend online and join notifications"},
			{"qm:block_words", "function", ECardColumn::Left, 12, "Block words", "屏蔽词 pingbici block words 控制台显示 kongzhitai 启用列表 qiyong liebiao 按词长替换 cichang tihuan 多字符替换 duozifu tihuan function", "Chat word filtering"},
			{"qm:qiafen", "function", ECardColumn::Left, 13, "Keyword reply", "关键词回复 guanjianci huifu 自动回复 zidong huifu 冷却 lengque dummy 发言 fayan 规则 guize 改名 gaiming 自动改名 zidong gaiming keyword reply qiafen function", "Configure keyword-based automatic replies"}, // UI 名 keyword_reply，以持久化 key qiafen 为权威
			{"qm:translate", "function", ECardColumn::Left, 14, "Translate", "翻译 fanyi translate 腾讯云 tengxunyun 智谱AI zhipuai 大模型 LLM 自动翻译 zidong fanyi 主动翻译 zhudong fanyi [ru] 目标语言 mubiao yuyan 端点 duandian endpoint 地域 diyu region secret id key api key 密钥 秘钥 凭证 glm-4.5-flash glm-4-flash 模型 model 中文跳过 zhongwen tiaoguo 服务器消息跳过 function", "Chat translation settings"},
			{"qm:pie_menu", "function", ECardColumn::Left, 16, "Pie menu", "饼菜单 bingcaidan pie menu 启用 qiyong ui大小 daxiao 不透明度 butouming 检测距离 jiance juli 改名名单 gaiming mingdan function", "Quick action menu for players"},
			{"qm:favorite_maps", "function", ECardColumn::Right, 6, "Favorite maps", "收藏地图 shoucang ditu favorite maps 地图管理 ditu guanli 收藏 shoucang 取消收藏 quxiao shoucang function", "Your favorite map manager"},
			{"qm:hj_assist", "function", ECardColumn::Right, 7, "HJ assist", "hj辅助 hj fuzhu 解冻辅助 jiedong fuzhu 自动取消旁观 quxiao pangguan 自动切换 qiehuan tee 自动关闭聊天 guanbi liaotian function", "Configure HJ unfreeze assistance"},
			{"qm:player_stats", "hud", ECardColumn::Right, 4, "Player stats", "玩家统计 wanjia tongji player stats gores hud 显示统计 xianshi tongji 进服重置 jinfu chongzhi", "Player stats and info display"},
			{"qm:speedrun_timer", "hud", ECardColumn::Right, 8, "Speedrun timer", "速通计时器 sutong jishiqi speedrun timer 倒计时 daojishi 倒数 daoshu 小时 xiaoshi 分钟 fenzhong 秒 miao 毫秒 haomiao 自动关闭 zidong guanbi hud", "Speedrun countdown timer"},
			{"qm:debug_graph", "hud", ECardColumn::Right, 9, "Debug graph", "调试图表 tiaoshi tubiao debug graph monitoring hud 不透明度 touming 透明度 面板 mianban 快捷键 kuaijiejian 按键 anjian", "Debug performance graph panel"},
			{"qm:input_overlay", "hud", ECardColumn::Right, 10, "Input overlay", "按键显示 anjian xianshi input overlay 按键叠加 anjian diejia 大小 daxiao 不透明度 butouming 水平位置 shuiping weizhi 垂直位置 chuizhi weizhi hud", "Configure the input overlay display"},
			{"qm:hud_notifications", "hud", ECardColumn::Right, 11, "HUD notifications", "通知栏 tongzhi lan notification toast echo 系统提示 xitong tishi 黑名单 heimingdan 右侧 youce 动画 donghua 背景 beijing 文字 wenzi hud", "Show server prompts and Echo messages as popups"},
			{"qm:voice", "hud", ECardColumn::Right, 12, "Voice", "语音 yuyin voice chat 麦克风 maikefeng mic 静音 jingyin 音量 yinliang 语音激活 vad 阈值 yuzhi 释放延迟 shifang yanchi 服务器 fuwuqi token 叠加层 diejiaceng 按住说话 ptt push to talk 全图收听 quantu 衰减 shuijian 距离 juli 半径 banjing 测试 ceshi 本地 bendi 回环 huihuan 设备 shebei 输入 shuru 左右声道定位 左右 zuoyou 声道 shengdao 立体声 stereo 高级 gaoji advanced hud", "Voice chat settings and diagnostics"},
			{"qm:dummy_miniview", "hud", ECardColumn::Right, 13, "Dummy mini view", "分身小窗 fenshen xiaochuang dummy mini view 预览 yulan 缩放 suofang 小窗大小 daxiao 离开视角 offscreen 自动显示 zidong xianshi hud", "Show a small view of the dummy"},
			{"qm:dynamic_island", "hud", ECardColumn::Right, 14, "Dynamic island", "灵动岛 lld lingdongdao dynamic island hud 顶部 dingbu 背景 beijing 颜色 yanse 透明度 touming 黑底 heidi 原版 yuanban 默认 moren classic old style", "Configure HUD island appearance"},
			{"qm:system_media_controls", "hud", ECardColumn::Right, 15, "System media controls", "系统媒体控制 xitong meiti kongzhi smtc media controls 启用系统媒体 qiyong 显示歌曲信息 gequ xinxi 上一个 shangyige 播放暂停 bofang zanting 下一个 xiayige hud", "Expose playback controls to the operating system"},
			{"qm:lyrics", "hud", ECardColumn::Right, 16, "Lyrics", "歌词 geci lyrics 来源 laiyuan source 网易云 wangyi netease 汽水 qishui soda 显示 xianshi 灵动岛 lingdongdao hud", "Configure lyrics sources and display"},
			{"qm:background_3d", "hud", ECardColumn::Right, 17, "3D background", "3d背景 3d beijing background particles 粒子 lizi 方块 fangkuai cube 爱心 aixin heart 球体 qiuti sphere 金字塔 jinzita pyramid 钻石 zuanshi diamond 圆环 yuanhuan ring 星形 xingxing star 月牙 yueya crescent 混合 hunhe mixed 数量 shuliang 速度 sudu 尺寸 chicun 深度 shendu 透明度 touming 颜色 yanse 随机 suiji 自定义 zidingyi 辉光 huiguang 拖尾 tuowei trail 脉冲 maichong pulse 闪烁 shanshuo twinkle 推动 tuidong 碰撞 pengzhuang 淡入 danru 淡出 danchu hud", "Configure background 3D particle effects"},
			{"qm:debug_mode", "hud", ECardColumn::Right, 19, "Debug mode", "调试模式 tiaoshi moshi debug mode 性能日志 xingneng rizhi perf log 性能调试 xingneng tiaoshi 日志文件 rizhi wenjian 采样阈值 caiyang yuzhi threshold 卡顿诊断 kadun zhenduan stutter diagnostics hud", "Enable performance debug logging and diagnostics"},
			{"qm:bind_status_hud", "hud", ECardColumn::Right, 20, "DDRace HUD Pro", "bind status hud 分身状态 fenshen zhuangtai 卡键 kajian 锤子 chuizi 分身控制 fenshen kongzhi 分身同步 fenshen tongbu 同步 tongbu ddrace hud pro", "Dummy key/hammer/control/copy status switches"},
			{"qm:nameplate_text", "hud", ECardColumn::Right, 18, "Nameplate text", "nameplate text hud 名字 mingzi 名牌 mingpai 文字 wenzi", "Customize additional nameplate text"}, // 数据债：原无 tab 归属，B1 补 hud
			{"qm:laser", "visual", ECardColumn::Right, 3, "Laser", "激光设置 jiguang laser 增强特效 zengqiang texiao 辉光强度 huiguang qiangdu 激光大小 daxiao 半透明 bantouming 圆角端点 yuanjiao duandian 脉冲速度 maichong sudu 脉冲幅度 maichong fudu visual", "Customize laser shape and effects"}, // 数据债：原无 tab 归属，B1 补 visual

			// === Tclient section（19）· tclient:<name>（id 不变；column/order 按当前 section 顺序显式化）===
			{"tclient:visual-font-cursor", "tclient", ECardColumn::Left, 0, "Font cursor", "font cursor tclient visual", "Choose the menu font and cursor appearance"},
			{"tclient:visual-nameplates", "tclient", ECardColumn::Right, 0, "Nameplates", "nameplates tclient visual", "Adjust player nameplate details and visibility"},
			{"tclient:visual-effects", "tclient", ECardColumn::Left, 1, "Visual effects", "visual effects tclient", "Tune additional world and player effects"},
			{"tclient:input", "tclient", ECardColumn::Right, 1, "Input", "input tclient", "Configure input helpers and cursor behavior"},
			{"tclient:anti-latency-tools", "tclient", ECardColumn::Left, 2, "Anti latency tools", "anti latency tools tclient", "Control latency compensation tools"},
			{"tclient:improved-anti-ping", "tclient", ECardColumn::Right, 2, "Improved anti ping", "improved anti ping tclient", "Fine-tune improved anti-ping prediction"},
			{"tclient:execute-on-join", "tclient", ECardColumn::Left, 3, "Execute on join", "execute on join tclient", "Run selected commands after joining a server"},
			{"tclient:voting", "tclient", ECardColumn::Right, 3, "Voting", "voting tclient", "Customize vote display and interaction"},
			{"tclient:auto-reply", "tclient", ECardColumn::Left, 4, "Auto reply", "auto reply tclient", "Define automatic chat reply rules"},
			{"tclient:player-indicator", "tclient", ECardColumn::Right, 4, "Player indicator", "player indicator tclient", "Highlight selected players in the game world"},
			{"tclient:pet", "tclient", ECardColumn::Left, 5, "Pet", "pet tclient", "Configure the companion appearance and movement"},
			{"tclient:hud", "tclient", ECardColumn::Right, 5, "HUD", "hud tclient", "Adjust TClient heads-up display elements"},
			{"tclient:tee-status-bar", "tclient", ECardColumn::Left, 6, "Tee status bar", "tee status bar tclient", "Build the status text shown above Tees"},
			{"tclient:tile-outlines", "tclient", ECardColumn::Right, 6, "Tile outlines", "tile outlines tclient", "Show configurable outlines around map tiles"},
			{"tclient:ghost-tools", "tclient", ECardColumn::Left, 7, "Ghost tools", "ghost tools tclient", "Configure ghost recording and playback tools"},
			{"tclient:rainbow", "tclient", ECardColumn::Right, 7, "Rainbow", "rainbow tclient", "Customize animated rainbow colors"},
			{"tclient:tee-trails", "tclient", ECardColumn::Left, 8, "Tee trails", "tee trails tclient", "Adjust trails rendered behind Tees"},
			{"tclient:background-draw", "tclient", ECardColumn::Right, 8, "Background draw", "background draw tclient", "Control custom background drawing"},
			{"tclient:finish-name", "tclient", ECardColumn::Left, 9, "Finish name", "finish name tclient", "Format player names after a finish"},

			// === 设置 deck · deck:<page>-<card>（原无持久化；tab=归属页/子页，column/order 按运行时卡片顺序显式化）===
			{"deck:qmclient-contributors-community", "qmclient-contributors", ECardColumn::Left, 0, "QmClient Community", "community links qmclient", "Find QmClient communities and project links"},
			{"deck:qmclient-contributors-title", "qmclient-contributors", ECardColumn::Left, 1, Localizable("Sponsor title"), "sponsor title code authentication nickname", Localizable("Redeem your code and customize your title")},
			{"deck:qmclient-contributors-sponsors", "qmclient-contributors", ECardColumn::Right, 0, "Sponsor support", "sponsor support qmclient", "View the people supporting QmClient development"},
			{"deck:global-search-input", "global-search", ECardColumn::Full, 0, "Feature Search", "global search feature cards", "Search settings by title, feature, or keyword"},
			{"deck:global-search-results", "global-search", ECardColumn::Full, 1, "Search", "global search result cards", "Open a matching settings card directly"},
			{"deck:general-game", "general", ECardColumn::Left, 0, Localizable("Game"), "general game camera weapon", "Configure camera, weapon, and gameplay defaults"},
			{"deck:general-language", "general", ECardColumn::Right, 0, Localizable("Language"), "general language localization", "Choose the language used by the client"},
			{"deck:general-client", "general", ECardColumn::Left, 1, Localizable("Client"), "general client theme files", "Manage client theme and menu preferences"},
			{"deck:general-recording", "general", ECardColumn::Right, 1, Localizable("Demo"), "general demo screenshot csv recording", "Automate demos, screenshots, and match exports"},
			{"deck:player-identity", "player", ECardColumn::Left, 0, Localizable("Player"), "player dummy name clan identity", "Edit player and dummy identity information"},
			{"deck:player-country", "player", ECardColumn::Right, 0, Localizable("Choose country flag"), "player dummy country flag", "Select the country flag for each player"},
			{"deck:tee-identity", "tee", ECardColumn::Left, 0, "Player preview", "tee player dummy identity preview", "Preview player and dummy appearance"},
			{"deck:tee-skin-options", "tee", ECardColumn::Right, 0, "Skin options", "tee skin colors eyes options prefix", "Configure skin colors, eyes, and filters"},
			{"deck:tee-skin-list", "tee", ECardColumn::Full, 0, "Skin search", "tee skins search filter list", "Search skins and manage the skin queue"},
			{"deck:tee7-editor", "tee7", ECardColumn::Full, 0, "Skin", "tee sixup skin editor", "Edit individual Tee 7 skin parts"},
			{"deck:graphics-display", "graphics", ECardColumn::Left, 0, Localizable("Graphics display"), "graphics display monitor window", "Window and monitor"},
			{"deck:graphics-visual", "graphics", ECardColumn::Left, 1, Localizable("Visual"), "graphics visual rendering card appearance settings card border corner segments rainbow title", "Rendering options"},
			{"deck:graphics-icons", "graphics", ECardColumn::Left, 2, Localizable("Icons"), "graphics UI icon color white black weight regular bold phosphor", "Configure UI icon appearance"},
			{"deck:graphics-modes", "graphics", ECardColumn::Right, 0, Localizable("Display modes"), "display modes graphics resolutions", "Available resolutions"},
			{"deck:graphics-interaction", "graphics", ECardColumn::Right, 1, Localizable("Interface animations"), "interface ui motion animations focus rainbow", "Motion and focus feedback"},
			{"deck:sound-toggle", "sound", ECardColumn::Left, 0, "Sound", "sound toggle audio", "Enable audio output and choose a device"},
			{"deck:sound-volume", "sound", ECardColumn::Left, 1, "Volume", "volume sound audio", "Balance master, music, and game volume"},
			{"deck:sound-audio-pack", "sound", ECardColumn::Right, 0, "Audio packs", "audio pack audio packs sound", "Browse and activate custom audio packs"},
			{"deck:ddnet-demo", "ddnet", ECardColumn::Left, 0, "Demo", "demo ddnet", "Configure DDNet demo recording behavior"},
			{"deck:ddnet-gameplay", "ddnet", ECardColumn::Left, 1, "Gameplay", "gameplay ddnet", "Adjust DDNet gameplay indicators and assists"},
			{"deck:ddnet-background", "ddnet", ECardColumn::Right, 0, "Background", "background ddnet", "Choose the menu map and background colors"},
			{"deck:ddnet-miscellaneous", "ddnet", ECardColumn::Right, 1, "Miscellaneous", "miscellaneous ddnet", "Configure remaining DDNet client preferences"},
			{"deck:tclient-bind-wheel-editor", "tclient-bind-wheel", ECardColumn::Left, 0, "Bind Wheel", "bind wheel tclient", "Assign commands and labels to wheel slots"},
			{"deck:tclient-bind-wheel-preview", "tclient-bind-wheel", ECardColumn::Right, 0, "Preview", "preview bind wheel tclient", "Preview the bind wheel layout and selection"},
			{"deck:tclient-status-bar-settings", "tclient-status-bar", ECardColumn::Left, 0, "Status Bar", "status bar tclient", "Adjust status bar position and text style"},
			{"deck:tclient-status-bar-preview", "tclient-status-bar", ECardColumn::Left, 1, "Preview", "preview status bar tclient", "Preview the composed status bar output"},
			{"deck:tclient-chat-binds-kaomoji", "tclient-chat-binds", ECardColumn::Left, 0, "Kaomoji", "chat binds kaomoji tclient", "Manage kaomoji chat shortcuts"},
			{"deck:tclient-chat-binds-warlist", "tclient-chat-binds", ECardColumn::Right, 0, "Warlist", "chat binds warlist tclient", "Manage hostile-list chat shortcuts"},
			{"deck:tclient-chat-binds-other", "tclient-chat-binds", ECardColumn::Left, 1, "Other", "chat binds other tclient", "Manage additional custom chat shortcuts"},
			{"deck:tclient-warlist", "tclient-warlist", ECardColumn::Full, 0, "War List", "enemy hostile war list entries groups players settings tclient", "Manage hostile players, groups, and display rules"},
			{"deck:tclient-info-links", "tclient-info", ECardColumn::Left, 0, "TClient Links", "tclient links discord website github support", "Open TClient community and source links"},
			{"deck:tclient-info-files", "tclient-info", ECardColumn::Left, 1, "Config Files", "config files settings profiles war list chat binds", "Open TClient configuration file locations"},
			{"deck:tclient-info-developers", "tclient-info", ECardColumn::Right, 0, "TClient Developers", "tclient developers tater sollybunny pebox teero chillerdragon", "View the developers and contributors"},
			{"deck:tclient-info-tabs", "tclient-info", ECardColumn::Right, 1, "Hide Settings Tabs", "hide settings tabs bind wheel war list chat binds status bar", "Choose which TClient settings tabs are visible"},
			{"deck:tclient-profiles-actions", "tclient-profiles", ECardColumn::Left, 0, "Profiles", "profiles save load delete", "Load, save, overwrite, or delete profiles"},
			{"deck:tclient-profiles-options", "tclient-profiles", ECardColumn::Right, 0, "Profile Options", "profiles options dummy colors fields", "Select which player fields a profile stores"},
			{"deck:tclient-profiles-list", "tclient-profiles", ECardColumn::Left, 1, "Saved Profiles", "profiles saved list", "Browse profiles saved on this device"},
			{"deck:tclient-configs-actions", "tclient-configs", ECardColumn::Full, 0, "Configuration", "configs changes filters variables values reset", "Review and reset changed configuration values"},
			{"deck:appearance-hud-main", "appearance-hud", ECardColumn::Left, 0, "HUD", "appearance hud main", "Configure health, ammo, timer, and race HUD"},
			{"deck:appearance-hud-ddrace", "appearance-hud", ECardColumn::Right, 0, "DDRace HUD", "appearance ddrace hud", "Adjust DDRace-specific HUD indicators"},
			{"deck:appearance-chat-settings", "appearance-chat", ECardColumn::Left, 0, "Chat", "appearance chat settings", "Configure chat position, size, and visibility"},
			{"deck:appearance-chat-messages", "appearance-chat", ECardColumn::Right, 0, "Messages", "appearance chat messages", "Style chat names, text, and message colors"},
			{"deck:appearance-chat-preview", "appearance-chat", ECardColumn::Left, 1, "Preview", "appearance chat preview", "Preview the current chat appearance"},
			{"deck:appearance-name-plate-settings", "appearance-name-plate", ECardColumn::Left, 0, "Name Plate", "appearance name plate settings", "Configure nameplate text, badges, and visibility"},
			{"deck:appearance-name-plate-preview", "appearance-name-plate", ECardColumn::Right, 0, "Preview", "appearance name plate preview", "Preview player and dummy nameplates"},
			{"deck:appearance-hook-collision-main", "appearance-hook-collision", ECardColumn::Left, 0, "Hook collision line", "appearance hook collision line", "Configure hook range and collision guide lines"},
			{"deck:appearance-hook-collision-preview", "appearance-hook-collision", ECardColumn::Right, 0, "Preview", "appearance hook collision preview", "Preview hook collision colors and line styles"},
			{"deck:appearance-info-messages", "appearance-info-messages", ECardColumn::Left, 0, "Info Messages", "appearance info messages", "Style kill, finish, and system messages"},
			{"deck:appearance-laser-enhanced", "appearance-laser", ECardColumn::Left, 0, "Laser settings", "appearance laser enhanced settings", "Configure laser shape, glow, and animation"},
			{"deck:appearance-laser-colors", "appearance-laser", ECardColumn::Left, 1, "Laser colors", "appearance laser colors weapons entities", "Choose colors for weapons and entity lasers"},
			{"deck:appearance-laser-preview", "appearance-laser", ECardColumn::Right, 0, "Preview", "appearance laser preview", "Preview laser bodies, outlines, and impacts"},
			{"deck:controls-mouse", "controls", ECardColumn::Left, 0, "Mouse", "controls mouse sensitivity", "Adjust mouse sensitivity and cursor limits"},
			{"deck:controls-controller", "controls", ECardColumn::Left, 1, "Controller", "controls controller joystick", "Enable and calibrate controller input"},
			{"deck:controls-movement", "controls", ECardColumn::Left, 2, "Movement", "controls movement binds", "Bind movement, jump, hook, and fire actions"},
			{"deck:controls-weapon", "controls", ECardColumn::Left, 3, "Weapon", "controls weapon binds", "Bind weapon selection and switching actions"},
			{"deck:controls-voting", "controls", ECardColumn::Right, 0, "Voting", "controls voting binds", "Bind voting and server browser actions"},
			{"deck:controls-chat", "controls", ECardColumn::Right, 1, "Chat", "controls chat binds", "Bind chat, team chat, and history actions"},
			{"deck:controls-dummy", "controls", ECardColumn::Right, 2, "Dummy", "controls dummy binds", "Bind dummy control and copy actions"},
			{"deck:controls-miscellaneous", "controls", ECardColumn::Right, 3, "Miscellaneous", "controls miscellaneous binds", "Bind scoreboard, emote, and console actions"},
			{"deck:controls-custom", "controls", ECardColumn::Right, 4, "Custom", "controls custom binds", "Review and edit custom key bindings"},
		};
		// clang-format on
		return s_aDefaults;
	}

	const std::vector<SCardDefault> &Defaults()
	{
		return DefaultsTable();
	}

	const SCardDefault *FindByStableId(const char *pStableId)
	{
		if(pStableId == nullptr)
			return nullptr;
		for(const SCardDefault &D : DefaultsTable())
		{
			if(str_comp(D.m_pStableId, pStableId) == 0)
				return &D;
		}
		return nullptr;
	}

	const char *ResolveDescriptionKey(const SCardDefault &Default)
	{
		if(Default.m_pDescription != nullptr && Default.m_pDescription[0] != '\0')
			return Default.m_pDescription;
		const char *pTab = Default.m_pDefaultTab;
		if(pTab == nullptr)
			return Localizable("Configure these settings");
		if(str_comp(pTab, "visual") == 0)
			return Localizable("Customize QmClient visuals and effects");
		if(str_comp(pTab, "function") == 0)
			return Localizable("Configure QmClient gameplay helpers");
		if(str_comp(pTab, "hud") == 0)
			return Localizable("Configure QmClient HUD elements");
		if(str_comp(pTab, "general") == 0)
			return Localizable("Configure game and client preferences");
		if(str_comp(pTab, "player") == 0)
			return Localizable("Configure player identity and country");
		if(str_comp(pTab, "tee") == 0 || str_comp(pTab, "tee7") == 0)
			return Localizable("Configure Tee appearance and skins");
		if(str_comp(pTab, "graphics") == 0)
			return Localizable("Configure display and rendering");
		if(str_comp(pTab, "sound") == 0)
			return Localizable("Configure sound and audio packs");
		if(str_comp(pTab, "ddnet") == 0)
			return Localizable("Configure DDNet gameplay preferences");
		if(str_comp(pTab, "controls") == 0)
			return Localizable("Configure controls and key bindings");
		if(str_startswith(pTab, "appearance-") != nullptr)
			return Localizable("Customize interface appearance and previews");
		if(str_comp(pTab, "tclient") == 0 || str_startswith(pTab, "tclient-") != nullptr)
			return Localizable("Configure TClient tools and profiles");
		if(str_comp(pTab, "qmclient-contributors") == 0)
			return Localizable("QmClient community and project information");
		if(str_comp(pTab, "global-search") == 0)
			return Localizable("Search and navigate settings");
		return Localizable("Configure these settings");
	}

	const char *ResolveLocalizedDescription(const SCardDefault &Default)
	{
		return Localize(ResolveDescriptionKey(Default));
	}

	const char *ResolveLocalizedDescription(const char *pStableId)
	{
		const SCardDefault *pDefault = FindByStableId(pStableId);
		return pDefault != nullptr ? ResolveLocalizedDescription(*pDefault) : nullptr;
	}

	namespace
	{
		bool SearchTextMatches(const char *pText, const char *pQuery)
		{
			return pText != nullptr && pText[0] != '\0' && str_utf8_find_nocase(pText, pQuery) != nullptr;
		}
	}

	SCardNavigationTarget ResolveCardNavigationTarget(const SCardDefault &Default, const qm_card_order::CModel &Model)
	{
		const int StateIndex = Default.m_pStableId != nullptr ? Model.FindByStableId(Default.m_pStableId) : -1;
		const char *pCurrentTab = StateIndex >= 0 ? Model.Entry(StateIndex).m_pDefaultTab : nullptr;
		if(pCurrentTab == nullptr)
			pCurrentTab = Default.m_pDefaultTab;
		return {pCurrentTab, Default.m_pStableId};
	}

	std::vector<SCardSearchResult> SearchCards(const char *pQuery, const qm_card_order::CModel &Model)
	{
		std::vector<SCardSearchResult> vResults;
		if(pQuery == nullptr || pQuery[0] == '\0')
			return vResults;

		const std::vector<SCardDefault> &vDefaults = DefaultsTable();
		vResults.reserve(vDefaults.size());
		for(const SCardDefault &Default : vDefaults)
		{
			if(Default.m_pStableId == nullptr)
				continue;
			const char *pLocalizedTitle = Default.m_pTitle != nullptr ? Localize(Default.m_pTitle) : "";
			const char *pLocalizedDescription = Default.m_pDescription != nullptr ? Localize(Default.m_pDescription) : "";
			if(!SearchTextMatches(pLocalizedTitle, pQuery) &&
				!SearchTextMatches(pLocalizedDescription, pQuery) &&
				!SearchTextMatches(Default.m_pSearchKeywords, pQuery))
				continue;

			SCardSearchResult Result;
			Result.m_pStableId = Default.m_pStableId;
			Result.m_Title = pLocalizedTitle;
			Result.m_Description = pLocalizedDescription;
			Result.m_Target = ResolveCardNavigationTarget(Default, Model);
			vResults.push_back(std::move(Result));
		}
		return vResults;
	}

	const char *MigrateLegacyKey(const char *pLegacyKey)
	{
		if(pLegacyKey == nullptr)
			return nullptr;
		// 从注册表派生：构造 stableId="qm:"+legacyKey 查表（DRY，不硬编码 key 列表）。
		// UI 名（如 keyword_reply）不在注册表，构造后查不到→nullptr，天然以持久化 key 为权威。
		char aStableId[128];
		str_format(aStableId, sizeof(aStableId), "qm:%s", pLegacyKey);
		const SCardDefault *D = FindByStableId(aStableId);
		return D != nullptr ? D->m_pStableId : nullptr;
	}

	std::vector<qm_card_order::SEntry> BuildDefaultEntries()
	{
		std::vector<qm_card_order::SEntry> vEntries;
		const std::vector<SCardDefault> &vDefaults = DefaultsTable();
		vEntries.reserve(vDefaults.size());
		for(const SCardDefault &Default : vDefaults)
		{
			int Column = 1;
			if(Default.m_DefaultColumn == ECardColumn::Full)
				Column = 0;
			else if(Default.m_DefaultColumn == ECardColumn::Right)
				Column = 2;
			vEntries.push_back({Default.m_pStableId, Default.m_pDefaultTab, Column, Default.m_DefaultOrder});
		}
		return vEntries;
	}
} // namespace qm_card_registry
