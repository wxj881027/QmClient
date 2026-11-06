#include <base/system.h>

#include <engine/external/unordered_dense/ankerl/unordered_dense.h>
#include <engine/keys.h>

#include <game/client/components/qmclient/bind_editor.h>
#include <game/client/components/qmclient/bind_workshop.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace
{
	std::string Lower(std::string_view Text)
	{
		std::string Result(Text);
		std::transform(Result.begin(), Result.end(), Result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return Result;
	}

	bool Contains(std::string_view Haystack, std::string_view Needle)
	{
		if(Needle.empty())
			return true;
		if(Haystack.size() < Needle.size())
			return false;
		return Lower(Haystack).find(Lower(Needle)) != std::string::npos;
	}

	// 网格桶键：把二维格子折叠成一个无符号 64 位键，避免负数取模的歧义。
	uint64_t CellKey(int CellX, int CellY)
	{
		return (static_cast<uint64_t>(static_cast<uint32_t>(CellX)) << 32U) | static_cast<uint32_t>(CellY);
	}

	int CellOf(float Value)
	{
		return static_cast<int>(std::floor(Value / qm_bind_workshop::GRID_CELL_SIZE));
	}

	int CountNesting(std::string_view Command)
	{
		// 只有「bind 内再 bind」会加深控制台的嵌套层数；其它命令同帧执行。
		int Count = 0;
		const std::string Lowered = Lower(Command);
		size_t Pos = 0;
		while((Pos = Lowered.find("bind ", Pos)) != std::string::npos)
		{
			++Count;
			Pos += 5;
		}
		return Count;
	}

	std::vector<qm_bind_workshop::SPreset> MakeBuiltinPresets()
	{
		using qm_bind_workshop::EBindGrouping;
		using qm_bind_workshop::ECategory;
		using qm_bind_workshop::ESource;
		using qm_bind_workshop::SPreset;
		using qm_bind_workshop::SVariant;

		std::vector<SPreset> vPresets;
		vPresets.reserve(58);
		// 命令原文逐字符照抄社区公开写法；标题与说明是自写文案，全部走 Localize。
		// pSlot 直接给槽位，带修饰键的推荐键（ctrl+z、alt+s）因此和普通键落在同一套类型里。
		// RequiresCfg 为真表示命令只是 exec 一个 cfg 包：没有随包分发的文件，
		// 写下去的 bind 就是死键，所以这类预设不进「应用」路径（见 MakePresetPlan）。
		const auto Add = [&](const char *pId, const char *pTitle, const char *pDescription, ECategory Category, const CBindSlot &Slot, bool RequiresCfg,
					 std::vector<SVariant> vVariants, std::vector<qm_bind_workshop::SPresetParam> vParams = {},
					 EBindGrouping Grouping = EBindGrouping::EXCLUSIVE, std::vector<CBindSlot> vVariantSlots = {}) {
			SPreset Preset;
			Preset.m_Id = pId;
			Preset.m_pTitle = pTitle;
			Preset.m_pDescription = pDescription;
			Preset.m_Category = Category;
			Preset.m_Source = ESource::BUILTIN;
			Preset.m_RequiresCfg = RequiresCfg;
			Preset.m_RecommendedSlot = Slot;
			Preset.m_Grouping = Grouping;
			Preset.m_vVariantSlots = std::move(vVariantSlots);
			Preset.m_vVariants = std::move(vVariants);
			Preset.m_vParams = std::move(vParams);
			vPresets.push_back(std::move(Preset));
		};

		Add("precision_jump", "精准跳", "起跳瞬间锁住鼠标距离，松开还原。", ECategory::MOVEMENT, CBindSlot(KEY_SPACE, KeyModifier::NONE), false,
			{{"Hold", "+jump; +toggle_restore cl_mouse_max_distance 2", {}}});
		// 左右方向键与 Y 都是原生绑定（旁观切换、队伍聊天），用 ALT 修饰避开抢占：
		// 按住 ALT 的组合键是独立槽位，不会顶掉玩家原有的方向键绑定。
		Add("three_tiles_jump", "三格水跳", "cfg 包：三格水提示与左右同跳。", ECategory::MOVEMENT, CBindSlot(KEY_RIGHT, 1 << KeyModifier::ALT), true,
			{{"Right", "+right; +jump", {}},
				{"Left", "+left; +jump", {}},
				{"Hint", "exec 三格水提示.cfg", {}},
				{"Chat", "say \\\"三格水? 太简单了吧!!! , 向右:.62 .63 .66 .81, 向左:.16 .31 .34\\\"", {}}},
			{}, EBindGrouping::ACTION_SET,
			{// 右跳/左跳各是一个方向键长按，提示与喊话是另外两件事：四个键都能同时存在。
				CBindSlot(KEY_RIGHT, 1 << KeyModifier::ALT), CBindSlot(KEY_LEFT, 1 << KeyModifier::ALT),
				CBindSlot(KEY_DOWN, KeyModifier::NONE), CBindSlot(KEY_Y, 1 << KeyModifier::ALT)});
		Add("delay_jump", "延时跳跃", "cfg 包：按坐标延时起跳。", ECategory::MOVEMENT, CBindSlot(KEY_W, 1 << KeyModifier::ALT), true,
			{{"On", "exec 延时跳跃_开.cfg", {}}});
		Add("dummy_hammer_hdf", "按住分身锤", "按住时左键开火附带分身锤。", ECategory::DUMMY, CBindSlot(KEY_MOUSE_1, KeyModifier::NONE), false,
			{{"Hold", "+toggle cl_dummy_hammer 1 0", {}}});
		Add("dummy_connect", "连接分身", "一键连接分身。", ECategory::DUMMY, CBindSlot(KEY_END, KeyModifier::NONE), false,
			{{"Connect", "dummy_connect", {}}});
		Add("dummy_switch", "切换分身", "本体与分身互换，可选同时切锤。", ECategory::DUMMY, CBindSlot(KEY_X, KeyModifier::NONE), false,
			{{"Plain", "toggle cl_dummy 0 1", {}},
				{"Switch hammer", "toggle cl_dummy 0 1; +weapon1", {}}});
		Add("dummy_sync", "分身同步", "切锤并同步分身移动。", ECategory::DUMMY, CBindSlot(KEY_C, KeyModifier::NONE), false,
			{{"Sync", "+weapon1; toggle cl_dummy_copy_moves 0 1", {}}});
		Add("dummy_hook", "分身钩", "按住出分身钩，松开恢复。", ECategory::DUMMY, CBindSlot(KEY_H, KeyModifier::NONE), false,
			{{"Hold", "+toggle cl_dummy_control 1 0; +toggle cl_dummy_hook 1 0", {}},
				{"Sticky", "toggle cl_dummy_control 1 0; toggle cl_dummy_hook 1 0", {}}});
		Add("dummy_jump", "分身跳", "不切分身直接跳，按住生效。", ECategory::DUMMY, CBindSlot(KEY_M, KeyModifier::NONE), false,
			{{"Hold", "+toggle cl_dummy_control 1 0; +toggle cl_dummy_jump 1 0; +toggle cl_dummy_hammer 1 0", {}}});
		// 分身键位是「一组动作」：关分身 / 关本体 / 全关的用途各不相同，各占一个键。
		Add("dummy_keys", "分身键位", "关分身、关本体、全关三种重置。", ECategory::DUMMY, CBindSlot(KEY_UNKNOWN, KeyModifier::NONE), false,
			{{"Dummy only", "dummy_reset", {}},
				{"Both", "dummy_reset; toggle cl_dummy 0 1; dummy_reset; toggle cl_dummy 0 1", {}},
				{"Main only", "toggle cl_dummy 0 1; dummy_reset; toggle cl_dummy 0 1", {}}},
			{}, EBindGrouping::ACTION_SET,
			// K 在原生配置里是 kill：加 ALT 不要抢自杀键，L 与分号本来就空着。
			{CBindSlot(KEY_K, 1 << KeyModifier::ALT), CBindSlot(KEY_L, KeyModifier::NONE), CBindSlot(KEY_SEMICOLON, KeyModifier::NONE)});
		Add("keys_lock", "卡键", "cfg 包：开/关卡键与分身控制。", ECategory::DUMMY, CBindSlot(KEY_B, KeyModifier::NONE), true,
			{{"On", "exec 开启卡键.cfg", {}},
				{"Off", "exec 关闭卡键.cfg", {}},
				{"Reset both", "echo 卡键全关！; dummy_reset; toggle cl_dummy 0 1; dummy_reset; toggle cl_dummy 0 1", {}},
				{"Reset control", "echo 卡键全关！; +toggle cl_dummy_control 1 0; toggle cl_dummy 0 1; toggle cl_dummy 0 1", {}}});
		Add("auto_hammer", "自动换锤", "cfg 包：分身后自动切回锤子。", ECategory::DUMMY, CBindSlot(KEY_1, KeyModifier::NONE), true,
			{{"On", "exec 开启自动换锤.cfg", {}},
				{"Off", "exec 关闭自动换锤.cfg", {}}});
		Add("dummy_control", "分身控制", "cfg 包：一键开关分身控制。", ECategory::DUMMY, CBindSlot(KEY_GRAVE, KeyModifier::NONE), true,
			{{"Toggle", "exec 开启分身控制.cfg", {}}});
		Add("df_mode", "DF 模式", "左键双锤、右键出钩。", ECategory::FLYING, CBindSlot(KEY_E, KeyModifier::NONE), false,
			{{"Mode", "echo DF 模式; bind mouse1 \\\"+fire; +toggle cl_dummy_hammer 1 0\\\"; bind mouse2 +hook", {}}});
		Add("hdf_mode", "HDF 模式", "左键只出分身锤，右键出钩。", ECategory::FLYING, CBindSlot(KEY_R, KeyModifier::NONE), false,
			{{"Mode", "echo HDF 模式; bind mouse1 \\\"+toggle cl_dummy_hammer 1 0\\\"; bind mouse2 +hook", {}}});
		Add("pf_mode", "PF 模式", "右键出钩带分身锤。", ECategory::FLYING, CBindSlot(KEY_O, KeyModifier::NONE), false,
			{{"Mode", "echo PF 模式; bind mouse1 \\\"+fire\\\"; bind mouse2 \\\"+hook; +toggle cl_dummy_hammer 1 0\\\"", {}}});
		Add("normal_mode", "正常模式", "还原左右键与自动锤。", ECategory::FLYING, CBindSlot(KEY_F, KeyModifier::NONE), false,
			{{"Mode", "echo 正常模式; bind mouse1 +fire; bind mouse2 +hook; cl_dummy_hammer 0", {}}});
		Add("three_fly", "三飞模式", "左键分身锤、右键出钩。", ECategory::FLYING, CBindSlot(KEY_Y, KeyModifier::NONE), false,
			{{"Mode", "echo 3Fly 模式; bind mouse2 \\\"+hook; cl_dummy_hammer 0\\\"; bind mouse1 \\\"+toggle cl_dummy_hammer 1 0\\\"", {}},
				{"Reset variant", "echo 3Fly 模式; bind mouse1 \\\"dummy_reset; cl_dummy_hammer 1; cl_dummy_copy_moves 0\\\"; bind mouse2 \\\"dummy_reset; +hook; cl_dummy_hammer 0; cl_dummy_copy_moves 1\\\"", {}}});
		Add("three_fly_hammer", "三飞分身锤", "侧键直接出分身锤。", ECategory::FLYING, CBindSlot(KEY_MOUSE_5, KeyModifier::NONE), false,
			{{"Dummy hammer", "+toggle cl_dummy_control 1 0; +toggle cl_dummy_fire 1 0", {}},
				{"With main fire", "+toggle cl_dummy_control 1 0; +toggle cl_dummy_fire 1 0; +fire", {}},
				{"Deep fly", "dummy_reset; cl_dummy_copy_moves 0; +weapon1; +fire; +toggle cl_dummy_hammer 1 0", {}}});
		Add("number_key_hdf_df", "数字键切飞", "数字键切锤并改写左键。", ECategory::FLYING, CBindSlot(KEY_1, KeyModifier::NONE), false,
			{{"Hammer", "+weapon1;+showhookcoll; bind # \\\"toggle cl_dummy 0 1\\\";bind mouse1 \\\"+toggle cl_dummy_hammer 1 0\\\"", {}},
				{"Sync", "+weapon1;cl_dummy_restore_weapon 1;toggle cl_dummy_copy_moves 0 1;bind # \\\"toggle cl_dummy 0 1\\\";bind mouse1 \\\"+toggle cl_dummy_hammer 1 0\\\";bind @ \\\"toggle cl_dummy_copy_moves 0 1\\\"", {}}});
		// HDF 切 DF 是「一组动作」：描述就写着两键互换，HDF 与 DF 各占一个键。
		Add("hdf_df_no_stuck", "HDF 切 DF 不卡锤", "两键互换 HDF/DF，不卡锤。", ECategory::FLYING, CBindSlot(KEY_R, KeyModifier::NONE), false,
			{{"HDF", "dummy_reset; cl_dummy_copy_moves 0; +nextweapon; +fire; bind mouse1 \\\"+weapon1; +toggle cl_dummy_hammer 1 0; bind mouse1 \\\\\\\"+toggle cl_dummy_hammer 1 0\\\\\\\"\\\"", {}},
				{"DF", "+weapon1; bind mouse1 \\\"+fire; +toggle cl_dummy_hammer 1 0\\\"", {}}},
			{}, EBindGrouping::ACTION_SET,
			{CBindSlot(KEY_R, KeyModifier::NONE), CBindSlot(KEY_E, KeyModifier::NONE)});
		Add("df_hdf_switch", "切飞不卡锤", "cfg 包：长按开火也不卡锤。", ECategory::FLYING, CBindSlot(KEY_E, KeyModifier::NONE), true,
			{{"No stuck", "exec 不卡锤_DF.cfg", {}},
				{"Classic", "exec DF.cfg", {}}});
		Add("shitfly", "Shitfly 模式", "cfg 包：替代 PF 与 Hookfly。", ECategory::FLYING, CBindSlot(KEY_J, KeyModifier::NONE), true,
			{{"On", "exec Shitfly_模式_开.cfg", {}}});
		Add("hookfly", "Hookfly 模式", "cfg 包：卡键切分身后 HF。", ECategory::FLYING, CBindSlot(KEY_C, KeyModifier::NONE), true,
			{{"Body view", "exec hookfly_mode_on.cfg", {}},
				{"Main view", "exec Hookfly_模式_开.cfg", {}}});
		Add("kog_mode", "KOG 模式", "cfg 包：Kog 一键切换。", ECategory::FLYING, CBindSlot(KEY_K, KeyModifier::NONE), true,
			{{"On", "exec Kog_模式_开.cfg", {}}});
		// 侧键切枪是「一组动作」：后侧键上一把、前侧键下一把是两个独立动作，
		// 各自占一个键，一次应用要全绑上（二选一在这里没有意义）。
		Add("mouse_swift_weapons", "侧键切枪", "后侧键上一把，前侧键下一把。", ECategory::WEAPON, CBindSlot(KEY_MOUSE_4, KeyModifier::NONE), false,
			{{"Previous", "+prevweapon", {}},
				{"Next", "+nextweapon", {}}},
			{}, EBindGrouping::ACTION_SET,
			{CBindSlot(KEY_MOUSE_4, KeyModifier::NONE), CBindSlot(KEY_MOUSE_5, KeyModifier::NONE)});
		Add("quick_weapon", "按住切锤", "按住切到锤子，松开还原。", ECategory::WEAPON, CBindSlot(KEY_UNKNOWN, KeyModifier::NONE), false,
			{{"Hold", "+weapon1", {}}});
		Add("double_hammer_fire", "开火带分身锤", "左键开火同时按分身锤。", ECategory::WEAPON, CBindSlot(KEY_MOUSE_1, KeyModifier::NONE), false,
			{{"Hold", "+fire; +toggle cl_dummy_hammer 1 0", {}}});
		Add("gun_hammer_switch", "枪锤切换", "cfg 包：侧键只在枪与锤间切。", ECategory::WEAPON, CBindSlot(KEY_Z, KeyModifier::NONE), true,
			{{"On", "exec 开启枪锤切换.cfg", {}}});
		Add("weapon_master", "武器大师", "cfg 包：左键按顺序自动换枪。", ECategory::WEAPON, CBindSlot(KEY_6, KeyModifier::NONE), true,
			{{"On", "exec bind\\武器\\武器大师.txt", {}}});
		// 灵敏度这一族的目标值做成数值参数：默认取站点值，范围按 DDNet 配置项的实际
		// 取值区间夹紧（inp_mousesens 1..100000、cl_mouse_max_distance 0..5000，见
		// engine/shared/config_variables.h:480、122），这里只放出玩家真正会调的区间。
		Add("default_mouse_sense", "默认灵敏度", "设置灵敏度与鼠标范围，最小值归零。", ECategory::AIM, CBindSlot(KEY_UNKNOWN, KeyModifier::NONE), false,
			{{"Defaults", "inp_mousesens {sens}; cl_mouse_max_distance {dist}; cl_mouse_min_distance 0", {}}},
			{{"sens", "Sensitivity", 66, 1, 20000, 1}, {"dist", "Mouse distance", 350, 1, 1000, 10}});
		Add("low_sens_aim", "低灵敏度瞄准", "按住降灵敏度并显示辅助线。", ECategory::AIM, CBindSlot(KEY_S, KeyModifier::NONE), false,
			{{"Sensitivity only", "+showhookcoll; +toggle inp_mousesens 1 {sens}", {}},
				{"Full site set", "cl_show_hook_coll_own 2; +showhookcoll; +toggle inp_mousesens 1 {sens}; +toggle cl_mouse_max_distance 5000 {dist}", {}}},
			{{"sens", "Sensitivity", 66, 1, 20000, 1}, {"dist", "Mouse distance", 350, 1, 1000, 10}});
		Add("angle_45", "45 度角瞄准", "按住咬住 45 度角，松开还原。", ECategory::AIM, CBindSlot(KEY_SPACE, KeyModifier::NONE), false,
			{{"Hold", "+toggle cl_mouse_max_distance 2 {dist}; +toggle_restore inp_mousesens 1", {}}},
			{{"dist", "Mouse distance", 350, 1, 1000, 10}});
		Add("auto_3_grenade", "贴墙自动三炮", "按住贴墙，三炮打在同一处。", ECategory::AIM, CBindSlot(KEY_SPACE, KeyModifier::NONE), false,
			{{"Hold", "+toggle cl_mouse_max_distance 80 {dist}; +toggle inp_mousesens 1 {sens}", {}}},
			{{"dist", "Mouse distance", 350, 1, 1000, 10}, {"sens", "Sensitivity", 66, 1, 20000, 1}});
		Add("super_low_sens", "超级低灵敏度", "cfg 包：精度可到 0.01 度。", ECategory::AIM, CBindSlot(KEY_S, 1 << KeyModifier::ALT), true,
			{{"On", "exec 开启超级低灵敏度.cfg", {}}});
		Add("join_team", "加入队伍", "喊话并加入 101 队。", ECategory::SERVER, CBindSlot(KEY_0, KeyModifier::NONE), false,
			{{"Team 101", "echo Join team; say /team 101", {}}});
		Add("practice_mode", "练习模式", "进入服务器练习模式。", ECategory::SERVER, CBindSlot(KEY_9, KeyModifier::NONE), false,
			{{"Practice", "echo 练习模式; say /practice", {}}});
		Add("rescue", "自救", "回到上次离开墙体的位置。", ECategory::SERVER, CBindSlot(KEY_Z, 1 << KeyModifier::CTRL), false,
			{{"Rescue", "echo 自救; say /rescue", {}}});
		Add("lasttp", "回溯", "回到上一次传送的位置。", ECategory::SERVER, CBindSlot(KEY_P, KeyModifier::NONE), false,
			{{"Last teleport", "echo 回溯; say /lasttp", {}}});
		Add("teleport", "传送", "传送到视角中心。", ECategory::SERVER, CBindSlot(KEY_8, KeyModifier::NONE), false,
			{{"Teleport", "echo 传送; say /tp", {}}});
		Add("teleport_cursor", "传送到光标", "传送到鼠标光标处，Kog 常用。", ECategory::SERVER, CBindSlot(KEY_8, KeyModifier::NONE), false,
			{{"Cursor", "echo 传送; say /tc", {}}});
		Add("lock_team", "锁定队伍", "锁定队伍，防止他人加入。", ECategory::SERVER, CBindSlot(KEY_7, KeyModifier::NONE), false,
			{{"Lock", "say /lock", {}}});
		Add("invincible", "无敌模式", "开启后穿过黑水不变黑蛋。", ECategory::SERVER, CBindSlot(KEY_I, KeyModifier::NONE), false,
			{{"Invincible", "say /invincible", {}}});
		Add("team0_mode", "Team 0 模式", "队员自杀不再全队阵亡。", ECategory::SERVER, CBindSlot(KEY_I, KeyModifier::NONE), false,
			{{"Team 0", "say /team0mode", {}}});
		Add("unsolo", "出单人模式", "快速离开 solo 区域。", ECategory::SERVER, CBindSlot(KEY_I, KeyModifier::NONE), false,
			{{"Unsolo", "say /unsolo", {}}});
		// Q 是原生 +bindwheel / say /spec，V 在 DDRace 下是 say /info：都用 ALT 组合避开。
		Add("pause_spec", "旁观模式", "暂停或消失后旁观他人。", ECategory::SERVER, CBindSlot(KEY_Q, 1 << KeyModifier::ALT), false,
			{{"Pause", "say /pause", {}},
				{"Spec", "say /spec", {}}},
			{}, EBindGrouping::ACTION_SET,
			{// 暂停与旁观是两条不同的命令，同一局里两个都想要。
				CBindSlot(KEY_Q, 1 << KeyModifier::ALT), CBindSlot(KEY_V, 1 << KeyModifier::ALT)});
		Add("ninja_jetpack", "忍者飞行", "把手枪飞行换成忍者样式。", ECategory::SERVER, CBindSlot(KEY_UNKNOWN, KeyModifier::NONE), false,
			{{"Ninja", "say /ninjajetpack", {}}});
		Add("default_zoom", "恢复默认视野", "把视野改回 11。", ECategory::CAMERA, CBindSlot(KEY_MINUS, KeyModifier::NONE), false,
			{{"Zoom 11", "zoom 11", {}}});
		Add("zoom_hold", "按住缩放", "按住拉远，松开还原。", ECategory::CAMERA, CBindSlot(KEY_UNKNOWN, KeyModifier::NONE), false,
			{{"Hold", "+toggle zoom 1 0", {}},
				{"Default zoom toggle", "toggle cl_default_zoom 5 10; zoom", {}}});
		Add("mouse_fast_zoom", "滚轮快速缩放", "滚轮上恢复、下缩小。", ECategory::CAMERA, CBindSlot(KEY_MOUSE_WHEEL_UP, KeyModifier::NONE), false,
			{{"Wheel up", "zoom 11", {}},
				{"Wheel down", "zoom 5", {}}},
			{}, EBindGrouping::ACTION_SET,
			{CBindSlot(KEY_MOUSE_WHEEL_UP, KeyModifier::NONE), CBindSlot(KEY_MOUSE_WHEEL_DOWN, KeyModifier::NONE)});
		Add("mouse_zoom", "滚轮缩放", "滚轮上放大、下缩小。", ECategory::CAMERA, CBindSlot(KEY_MOUSE_WHEEL_UP, KeyModifier::NONE), false,
			{{"Zoom in", "zoom+", {}},
				{"Zoom out", "zoom-", {}}},
			{}, EBindGrouping::ACTION_SET,
			{CBindSlot(KEY_MOUSE_WHEEL_UP, KeyModifier::NONE), CBindSlot(KEY_MOUSE_WHEEL_DOWN, KeyModifier::NONE)});
		Add("no_ninja", "关闭黑蛋皮肤", "不显示忍者黑蛋，改用原皮肤。", ECategory::CAMERA, CBindSlot(KEY_UNKNOWN, KeyModifier::NONE), false,
			{{"Hide ninja", "cl_show_ninja 0", {}}});
		Add("nameplates", "关闭姓名板", "关掉姓名板、战队名与强弱钩。", ECategory::CAMERA, CBindSlot(KEY_UNKNOWN, KeyModifier::NONE), false,
			{{"Hide all", "cl_nameplates 0; cl_nameplates_clan 0; cl_nameplates_always 0; cl_nameplates_own 0; cl_nameplates_strong 0", {}}});
		Add("demo_render", "渲染录像参数", "录像帧率、画质与界面开关。", ECategory::CAMERA, CBindSlot(KEY_UNKNOWN, KeyModifier::NONE), false,
			{{"Render", "cl_video_recorder_fps 60; cl_video_crf 18; cl_video_preset 5; cl_video_show_hook_coll_other 0; cl_video_show_direction 1; cl_video_showhud 0; cl_video_showchat 0; cl_video_sound_enable 0", {}}});
		Add("change_name", "改名字", "一次改本体与分身名字。", ECategory::UTILITY, CBindSlot(KEY_6, KeyModifier::NONE), false,
			{{"Rename", "echo 改名字; player_name XXX; dummy_name XXX", {}}});
		Add("kill_protect", "防自杀", "中键自杀，F5 禁用，F7 超级自杀。", ECategory::UTILITY, CBindSlot(KEY_MOUSE_3, KeyModifier::NONE), false,
			{{"Kill", "echo Kill; kill", {}},
				{"F5 disable", "bind mouse3 \\\"echo 自救; say /rescue\\\"", {}},
				{"F6 enable", "bind mouse3 \\\"echo Kill; kill\\\"", {}},
				{"F7 super kill", "echo Super Kill; say /kill", {}}},
			{}, EBindGrouping::ACTION_SET,
			{CBindSlot(KEY_MOUSE_3, KeyModifier::NONE), CBindSlot(KEY_F5, KeyModifier::NONE), CBindSlot(KEY_F6, KeyModifier::NONE), CBindSlot(KEY_F7, KeyModifier::NONE)});
		Add("afk_emote", "关闭 AFK 表情", "不显示他人 AFK 的 ZZZ。", ECategory::UTILITY, CBindSlot(KEY_UNKNOWN, KeyModifier::NONE), false,
			{{"Hide AFK", "cl_afk_emote 0", {}}});
		Add("team_ready", "队伍就绪", "在队伍频道发送 ready。", ECategory::UTILITY, CBindSlot(KEY_UNKNOWN, KeyModifier::NONE), false,
			{{"Ready", "say_team /ready", {}}});
		return vPresets;
	}

	// ---- 内置 cfg 正文 ----
	// 站点分发的 cfg 包没有随包分发，所以按命令语义重写成等价的最小版本：只保留
	// 「按下这个键之后要发生什么」，去掉作者的配色链、多文件互相 exec 与说明文字。
	// 约束与站点 wiki / 控制台一致：分号 = 同帧执行（没有 sleep / if / 循环），
	// 单行 <= 255 字节，bind 嵌套 <= 5 层（MAX_NESTING_DEPTH）。文件之间互相 exec
	// 的只有本预设自己的另一份 cfg（开/关），UI 安装时会把一个预设的全部 cfg
	// 一起写盘，因此状态机不会指向不存在的文件。
	struct SBuiltinCfg
	{
		const char *m_pPresetId;
		const char *m_pVariantLabel;
		const char *m_pContent;
	};

	const std::vector<SBuiltinCfg> &BuiltinCfgs()
	{
		static const std::vector<SBuiltinCfg> s_vCfgs = {
			{"three_tiles_jump", "Hint", R"CFG(# QmClient built-in cfg: three-tile water jump hints
# right side: .62 .63 .66 .81 / left side: .16 .31 .34
echo 3-tile water jump: right .62 .63 .66 .81
echo 3-tile water jump: left .16 .31 .34
)CFG"},
			{"delay_jump", "On", R"CFG(# QmClient built-in cfg: delay jump (14/25 ticks in demo time, about 560 ms)
# the console has no sleep, so the direction key is extended and the jump fires with it
cl_message_client_color green
echo Delay jump ON: hold a direction, then press jump
+right; +jump
)CFG"},
			{"keys_lock", "On", R"CFG(# QmClient built-in cfg: key lock on (normal lock + sync lock)
cl_message_client_color green
echo Key lock ON
cl_dummy_resetonswitch 0
cl_dummy_control 0
bind b exec qm_bind/qm_bind_keys_lock_off.cfg
)CFG"},
			{"keys_lock", "Off", R"CFG(# QmClient built-in cfg: key lock off (lock + sync lock + dummy control)
cl_message_client_color green
echo Key lock OFF
cl_dummy_resetonswitch 1
cl_dummy_control 0
dummy_reset
bind b exec qm_bind/qm_bind_keys_lock_on.cfg
)CFG"},
			{"auto_hammer", "On", R"CFG(# QmClient built-in cfg: auto hammer on
cl_message_client_color green
echo Auto hammer ON: the dummy switches to hammer when you fire
+weapon1
cl_dummy_hammer 1
bind mouse1 "+fire; +toggle cl_dummy_hammer 1 0"
bind 1 exec qm_bind/qm_bind_auto_hammer_off.cfg
)CFG"},
			{"auto_hammer", "Off", R"CFG(# QmClient built-in cfg: auto hammer off
cl_message_client_color green
echo Auto hammer OFF: gun mode restored
cl_dummy_hammer 0
bind mouse1 +fire
bind 1 exec qm_bind/qm_bind_auto_hammer_on.cfg
)CFG"},
			{"dummy_control", "Toggle", R"CFG(# QmClient built-in cfg: one key toggles dummy control
cl_message_client_color green
echo Dummy control: press grave to toggle
bind grave toggle cl_dummy_control 0 1
)CFG"},
			{"df_hdf_switch", "No stuck", R"CFG(# QmClient built-in cfg: deep fly without stuck hammer
# fire is cleared first, so DF -> HDF -> DF never eats the first hammer
cl_message_client_color green
echo DF (no stuck) ON
bind mouse1 "dummy_reset; +fire; +toggle cl_dummy_hammer 1 0"
bind mouse2 +hook
)CFG"},
			{"df_hdf_switch", "Classic", R"CFG(# QmClient built-in cfg: classic deep fly
cl_message_client_color green
echo DF (classic) ON: press F for normal mode
bind mouse1 "+fire; +toggle cl_dummy_hammer 1 0"
bind mouse2 +hook
bind f "cl_dummy_hammer 0; bind mouse1 +fire; bind mouse2 +hook"
)CFG"},
			{"shitfly", "On", R"CFG(# QmClient built-in cfg: shitfly mode on
cl_message_client_color green
echo Shitfly ON: left hammers, right hooks with the dummy
cl_dummy_control 1
cl_dummy_copy_moves 1
bind mouse1 "+toggle cl_dummy_hammer 1 0"
bind mouse2 "+hook; +toggle cl_dummy_hook 1 0"
)CFG"},
			{"hookfly", "Body view", R"CFG(# QmClient built-in cfg: hookfly, body view
cl_message_client_color green
echo Hookfly ON (body view): shift hooks, Q/E move the dummy
cl_dummy_control 1
cl_dummy_copy_moves 0
bind shift "+hook"
bind q "+left"
bind e "+right"
)CFG"},
			{"hookfly", "Main view", R"CFG(# QmClient built-in cfg: hookfly, main view
cl_message_client_color green
echo Hookfly ON (main view): shift hooks with copy moves
cl_dummy_control 0
cl_dummy_copy_moves 1
bind shift "+hook"
)CFG"},
			{"kog_mode", "On", R"CFG(# QmClient built-in cfg: KOG mode on
cl_message_client_color green
echo KOG mode ON: right hooks and keeps the gun, left hammers
cl_dummy_restore_weapon 1
bind mouse1 "+fire; +toggle cl_dummy_hammer 1 0"
bind mouse2 "+hook"
)CFG"},
			{"gun_hammer_switch", "On", R"CFG(# QmClient built-in cfg: side buttons switch only gun and hammer
cl_message_client_color green
echo Gun/hammer switch ON
bind mouse4 +weapon1
bind mouse5 +weapon2
)CFG"},
			{"weapon_master", "On", R"CFG(# QmClient built-in cfg: weapon master
# 3/4/5 pick a starting weapon, then every left click fires it
cl_message_client_color green
echo Weapon master ON: press 3, 4 or 5, then left click
bind 3 "bind mouse1 \"+weapon3; +fire\""
bind 4 "bind mouse1 \"+weapon4; +fire\""
bind 5 "bind mouse1 \"+weapon5; +fire\""
)CFG"},
			{"super_low_sens", "On", R"CFG(# QmClient built-in cfg: super low sensitivity (0.01 degree precision)
cl_message_client_color green
echo Super low sensitivity ON: hold ALT+S to aim slowly
cl_show_hook_coll_own 2
bind alt+s "+toggle_restore inp_mousesens 1; +toggle_restore cl_mouse_max_distance 1"
)CFG"},
		};
		return s_vCfgs;
	}

	const SBuiltinCfg *FindBuiltinCfg(const qm_bind_workshop::SPreset &Preset, size_t VariantIndex)
	{
		if(VariantIndex >= Preset.m_vVariants.size())
			return nullptr;
		const char *pLabel = Preset.m_vVariants[VariantIndex].m_Label.c_str();
		for(const SBuiltinCfg &Cfg : BuiltinCfgs())
		{
			if(Preset.m_Id == Cfg.m_pPresetId && str_comp(pLabel, Cfg.m_pVariantLabel) == 0)
				return &Cfg;
		}
		return nullptr;
	}
}

namespace qm_bind_workshop
{
	const std::vector<SPreset> &BuiltinPresets()
	{
		static const std::vector<SPreset> s_vPresets = MakeBuiltinPresets();
		return s_vPresets;
	}

	const char *CategoryLabel(ECategory Category)
	{
		switch(Category)
		{
		case ECategory::ALL: return "All";
		case ECategory::MOVEMENT: return "Movement";
		case ECategory::DUMMY: return "Dummy";
		case ECategory::FLYING: return "Flying";
		case ECategory::WEAPON: return "Weapon";
		case ECategory::AIM: return "Aim";
		case ECategory::SERVER: return "Server";
		case ECategory::CAMERA: return "Camera";
		case ECategory::UTILITY: return "Utility";
		// NUM 不是可选分类：显式列出来只为让 -Wswitch 继续要求全枚举覆盖，
		// 真正的分类一个都不会落到这里。
		case ECategory::NUM: break;
		}
		return "All";
	}

	const char *ModeLabel(EMode Mode)
	{
		switch(Mode)
		{
		case EMode::ONCE: return "Execute once";
		case EMode::HOLD: return "Hold";
		case EMode::TOGGLE: return "Toggle";
		default: return "Execute once";
		}
	}

	const char *SourceLabel(ESource Source)
	{
		switch(Source)
		{
		case ESource::BUILTIN: return "Built-in";
		case ESource::IMPORTED: return "Imported";
		case ESource::USER: return "User";
		default: return "Built-in";
		}
	}

	EMode InferMode(std::string_view Command)
	{
		const std::string Lowered = Lower(Command);
		if(Lowered.find("+toggle") != std::string::npos || Lowered.find("+") != std::string::npos)
			return EMode::HOLD;
		if(Lowered.find("toggle") != std::string::npos)
			return EMode::TOGGLE;
		return EMode::ONCE;
	}

	bool MatchesQuery(const SPreset &Preset, std::string_view Query, const char *(*pLocalize)(const char *))
	{
		const std::string Title = pLocalize != nullptr ? pLocalize(Preset.m_pTitle) : Preset.m_pTitle;
		const std::string Description = pLocalize != nullptr ? pLocalize(Preset.m_pDescription) : Preset.m_pDescription;
		if(Contains(Title, Query) || Contains(Description, Query) || Contains(Preset.m_Id, Query))
			return true;
		for(const SVariant &Variant : Preset.m_vVariants)
		{
			if(Contains(Variant.m_Command, Query) || Contains(Variant.m_Label, Query))
				return true;
			for(const std::string &Tag : Variant.m_vTags)
				if(Contains(Tag, Query))
					return true;
		}
		return false;
	}

	std::vector<size_t> FilterPresets(const std::vector<SPreset> &vPresets, ECategory Category, std::string_view Query, const char *(*pLocalize)(const char *))
	{
		std::vector<size_t> vResult;
		vResult.reserve(vPresets.size());
		for(size_t Index = 0; Index < vPresets.size(); ++Index)
		{
			const SPreset &Preset = vPresets[Index];
			// 分类只在没有查询串时过滤：搜索是跨分类的，选中某个胶囊也能搜到别类的预设，
			// 清空查询后分类重新生效。
			if(Query.empty() && Category != ECategory::ALL && Preset.m_Category != Category)
				continue;
			if(!MatchesQuery(Preset, Query, pLocalize))
				continue;
			vResult.push_back(Index);
		}
		return vResult;
	}

	size_t CountCategory(const std::vector<SPreset> &vPresets, ECategory Category)
	{
		if(Category == ECategory::ALL)
			return vPresets.size();
		return static_cast<size_t>(std::count_if(vPresets.begin(), vPresets.end(), [Category](const SPreset &Preset) { return Preset.m_Category == Category; }));
	}

	// ---- 参数校验与命令生成 ----
	int ClampParamValue(const SPresetParam &Param, int Value)
	{
		// 范围写反（Max < Min）时按默认值处理，不让 UI 拿到一个随机数。
		if(Param.m_Max < Param.m_Min)
			return Param.m_Default;
		return std::clamp(Value, Param.m_Min, Param.m_Max);
	}

	int StepParamValue(const SPresetParam &Param, int Value, int Direction, int Steps)
	{
		const int Current = ClampParamValue(Param, Value);
		if(Direction == 0 || Steps == 0)
			return Current;
		const int Step = Param.m_Step != 0 ? Param.m_Step : 1;
		// 先用 64 位累加再夹紧：极端范围（1..20000、步长 1e8）也不会溢出。
		const long long Delta = static_cast<long long>(Step) * static_cast<long long>(Steps) * (Direction > 0 ? 1LL : -1LL);
		const long long Next = static_cast<long long>(Current) + Delta;
		const long long Bounded = std::clamp<long long>(Next, std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
		return ClampParamValue(Param, static_cast<int>(Bounded));
	}

	std::array<int, MAX_PRESET_PARAMS> ResolveParamValues(const SPreset &Preset, const int *pValues, size_t Count)
	{
		std::array<int, MAX_PRESET_PARAMS> aValues{};
		const size_t Num = std::min(Preset.m_vParams.size(), MAX_PRESET_PARAMS);
		for(size_t Index = 0; Index < Num; ++Index)
		{
			const SPresetParam &Param = Preset.m_vParams[Index];
			const int Requested = (pValues != nullptr && Index < Count) ? pValues[Index] : Param.m_Default;
			aValues[Index] = ClampParamValue(Param, Requested);
		}
		return aValues;
	}

	namespace
	{
		void ReplaceToken(std::string &rText, std::string_view Token, const std::string &Value)
		{
			if(Token.empty())
				return;
			size_t Pos = 0;
			while((Pos = rText.find(Token, Pos)) != std::string::npos)
			{
				rText.replace(Pos, Token.size(), Value);
				Pos += Value.size();
			}
		}
	}

	std::string ResolveVariantCommand(const SPreset &Preset, size_t VariantIndex, const int *pValues, size_t Count)
	{
		if(VariantIndex >= Preset.m_vVariants.size())
			return {};
		std::string Command = Preset.m_vVariants[VariantIndex].m_Command;
		const std::array<int, MAX_PRESET_PARAMS> aValues = ResolveParamValues(Preset, pValues, Count);
		const size_t Num = std::min(Preset.m_vParams.size(), MAX_PRESET_PARAMS);
		// 先替换命名 token（{sens} 这类）：两个参数同时出现时不会串位。
		for(size_t Index = 0; Index < Num; ++Index)
		{
			const char *pToken = Preset.m_vParams[Index].m_pToken;
			if(pToken == nullptr || pToken[0] == 0)
				continue;
			char aToken[48];
			str_format(aToken, sizeof(aToken), "{%s}", pToken);
			ReplaceToken(Command, aToken, std::to_string(aValues[Index]));
		}
		// 再按参数顺序吃掉裸写的 %d：站点命令原文里常见 `+toggle x 2 %d` 这种写法。
		for(size_t Index = 0; Index < Num; ++Index)
		{
			const size_t Pos = Command.find("%d");
			if(Pos == std::string::npos)
				break;
			Command.replace(Pos, 2, std::to_string(aValues[Index]));
		}
		return Command;
	}

	bool VariantRequiresCfg(const SVariant &Variant)
	{
		return Lower(Variant.m_Command).find("exec ") != std::string::npos;
	}

	// ---- 内置 cfg：文件名、exec 命令与正文 ----
	std::string CfgFileName(const SPreset &Preset, size_t VariantIndex)
	{
		if(VariantIndex >= Preset.m_vVariants.size())
			return {};
		// 一个预设的多个变体（开 / 关）各写一份文件，文件名必须唯一且稳定：
		// 覆盖安装不会留下旧版本的残片，exec 的相对路径也不会随 UI 变化。
		std::string Name = CFG_FOLDER;
		Name += '/';
		Name += "qm_bind_";
		Name += Preset.m_Id;
		Name += '_';
		Name += SlugifyTitle(Preset.m_vVariants[VariantIndex].m_Label);
		Name += ".cfg";
		return Name;
	}

	std::string CfgExecCommand(const SPreset &Preset, size_t VariantIndex)
	{
		const std::string FileName = CfgFileName(Preset, VariantIndex);
		if(FileName.empty())
			return {};
		return "exec " + FileName;
	}

	bool HasBuiltinCfg(const SPreset &Preset, size_t VariantIndex)
	{
		return FindBuiltinCfg(Preset, VariantIndex) != nullptr;
	}

	std::string BuildCfgContent(const SPreset &Preset, size_t VariantIndex)
	{
		const SBuiltinCfg *pCfg = FindBuiltinCfg(Preset, VariantIndex);
		if(pCfg == nullptr || pCfg->m_pContent == nullptr)
			return {};
		return pCfg->m_pContent;
	}

	size_t FindCfgViolation(std::string_view Content)
	{
		size_t LineNumber = 0;
		size_t Start = 0;
		while(true)
		{
			const size_t End = Content.find('\n', Start);
			const size_t Length = End == std::string_view::npos ? Content.size() - Start : End - Start;
			++LineNumber;
			size_t LineLength = Length;
			if(LineLength > 0 && Content[Start + LineLength - 1] == static_cast<char>(13))
				--LineLength;
			if(LineLength > CFG_MAX_LINE_BYTES)
				return LineNumber;
			if(CountNesting(Content.substr(Start, Length)) > MAX_NESTING_DEPTH)
				return LineNumber;
			if(End == std::string_view::npos)
				break;
			Start = End + 1;
		}
		return 0;
	}

	bool IsActionSet(const SPreset &Preset)
	{
		return Preset.m_Grouping == EBindGrouping::ACTION_SET;
	}

	size_t KeySlotCount(const SPreset &Preset)
	{
		if(!IsActionSet(Preset) || Preset.m_vVariants.empty())
			return 1;
		return Preset.m_vVariants.size();
	}

	CBindSlot RecommendedSlotFor(const SPreset &Preset, size_t VariantIndex)
	{
		if(VariantIndex < Preset.m_vVariantSlots.size())
			return Preset.m_vVariantSlots[VariantIndex];
		return Preset.m_RecommendedSlot;
	}

	bool SApplyPlan::HasConflicts() const
	{
		if(!m_vSkipped.empty())
			return true;
		for(const SApplyEntry &Entry : m_vEntries)
		{
			if(Entry.m_Conflicts != APPLY_CONFLICT_NONE)
				return true;
		}
		return false;
	}

	size_t SApplyPlan::ConflictCount() const
	{
		size_t Count = m_vSkipped.size();
		for(const SApplyEntry &Entry : m_vEntries)
		{
			if(Entry.m_Conflicts != APPLY_CONFLICT_NONE)
				++Count;
		}
		return Count;
	}

	SApplyPlan MakePresetPlan(const SPreset &Preset, size_t VariantIndex, const std::vector<CBindSlot> &vSlots,
		const FBoundCommand &GetBoundCommand, const int *pParamValues, size_t NumParamValues, bool CfgInstalled)
	{
		SApplyPlan Plan;
		// 哪些变体进这份计划：一组动作全部进，互斥方案只有选中的那一个。
		std::vector<size_t> vVariants;
		if(IsActionSet(Preset))
		{
			vVariants.reserve(Preset.m_vVariants.size());
			for(size_t Index = 0; Index < Preset.m_vVariants.size(); ++Index)
				vVariants.push_back(Index);
		}
		else if(VariantIndex < Preset.m_vVariants.size())
			vVariants.push_back(VariantIndex);
		if(vVariants.empty())
			return Plan;
		// cfg 门控先整体过一遍：一组动作不能只装一半，缺一份就整份计划为空。
		for(const size_t Variant : vVariants)
		{
			if(VariantRequiresCfg(Preset.m_vVariants[Variant]) && !CfgInstalled)
				return Plan;
		}
		// 互斥方案只有一个键槽；一组动作按变体下标各取各的。
		const bool PerVariantSlots = IsActionSet(Preset);
		for(const size_t Variant : vVariants)
		{
			const size_t SlotIndex = PerVariantSlots ? Variant : 0;
			CBindSlot Slot = SlotIndex < vSlots.size() ? vSlots[SlotIndex] : CBindSlot(KEY_UNKNOWN, KeyModifier::NONE);
			if(Slot.m_Key == KEY_UNKNOWN)
				Slot = RecommendedSlotFor(Preset, Variant);
			if(Slot.m_Key == KEY_UNKNOWN)
				continue; // 没有键可用：跳过这个变体，别写一条打到 KEY_UNKNOWN 的 bind
			SApplyEntry Entry;
			Entry.m_Slot = Slot;
			Entry.m_Variant = Variant;
			Entry.m_Command = VariantRequiresCfg(Preset.m_vVariants[Variant]) ?
						  CfgExecCommand(Preset, Variant) :
						  ResolveVariantCommand(Preset, Variant, pParamValues, NumParamValues);
			if(Entry.m_Command.empty())
				continue;
			// 一个键在一份计划里只写一次：先出现的那条留在写入序列里，后面的整条丢掉。
			// 这样 UI 按计划顺序逐条 Bind 就是「先写的生效」，而不是被后面那条顶掉。
			bool Duplicate = false;
			for(const SApplyEntry &Earlier : Plan.m_vEntries)
			{
				if(Earlier.m_Slot == Entry.m_Slot)
				{
					Duplicate = true;
					break;
				}
			}
			if(Duplicate)
			{
				SSkippedApplyEntry Skipped;
				Skipped.m_Slot = Entry.m_Slot;
				Skipped.m_Variant = Entry.m_Variant;
				Skipped.m_Command = std::move(Entry.m_Command);
				Plan.m_vSkipped.push_back(std::move(Skipped));
				continue;
			}
			if(GetBoundCommand)
				Entry.m_Previous = GetBoundCommand(Entry.m_Slot);
			Entry.m_Overwrite = !Entry.m_Previous.empty();
			if(Entry.m_Overwrite && Entry.m_Previous != Entry.m_Command)
				Entry.m_Conflicts |= APPLY_CONFLICT_OVERWRITE;
			Plan.m_vEntries.push_back(std::move(Entry));
		}
		return Plan;
	}

	std::string DescribePlanDiff(const SApplyPlan &Plan, const FGetKeyName &GetKeyName)
	{
		std::string Result;
		for(const SApplyEntry &Entry : Plan.m_vEntries)
		{
			char aKeyName[128] = {};
			if(GetKeyName)
				GetKeyName(Entry.m_Slot.m_Key, Entry.m_Slot.m_ModifierMask, aKeyName, sizeof(aKeyName));
			Result += aKeyName[0] != '\0' ? aKeyName : "?";
			Result += Entry.m_Overwrite ? ": " : ": + ";
			if(Entry.m_Overwrite)
				Result += Entry.m_Previous + " -> ";
			Result += Entry.m_Command;
			Result += "\n";
		}
		return Result;
	}

	void CApplyUndo::Record(const SApplyPlan &Plan)
	{
		m_vEntries.clear();
		m_vEntries.reserve(Plan.m_vEntries.size());
		for(const SApplyEntry &Entry : Plan.m_vEntries)
		{
			SRestoreEntry Restore;
			Restore.m_Slot = Entry.m_Slot;
			Restore.m_Command = Entry.m_Previous;
			Restore.m_HadBind = !Entry.m_Previous.empty();
			m_vEntries.push_back(std::move(Restore));
		}
	}

	bool CApplyUndo::Covers(const CBindSlot &Slot) const
	{
		for(const SRestoreEntry &Entry : m_vEntries)
		{
			if(Entry.m_Slot == Slot)
				return true;
		}
		return false;
	}

	size_t RestoreUndo(const CApplyUndo &Undo, const FBinder &Binder)
	{
		if(!Binder)
			return 0;
		size_t Restored = 0;
		for(const SRestoreEntry &Entry : Undo.Entries())
		{
			// 原来没绑的键写空串：空命令等于解绑那个 (键, 修饰键) 槽位。
			Binder(Entry.m_Slot, Entry.m_HadBind ? Entry.m_Command.c_str() : "");
			++Restored;
		}
		return Restored;
	}

	std::vector<SOccupancyEntry> BuildOccupancy(const FGetCommand &GetCommand, const FGetKeyName &GetKeyName)
	{
		std::vector<SOccupancyEntry> vEntries;
		std::array<int, KEY_LAST> aOccupiedModifiers{};
		aOccupiedModifiers.fill(0);
		for(int Modifier = 0; Modifier < KeyModifier::COMBINATION_COUNT; ++Modifier)
		{
			for(int Key = KEY_FIRST; Key < KEY_LAST; ++Key)
			{
				const char *pCommand = GetCommand(Key, Modifier);
				if(pCommand == nullptr || pCommand[0] == '\0')
					continue;
				if(Key >= 0 && Key < KEY_LAST)
					aOccupiedModifiers[Key] |= 1 << Modifier;
				SOccupancyEntry Entry;
				Entry.m_Slot = CBindSlot(Key, Modifier);
				char aKeyName[128] = {};
				GetKeyName(Key, Modifier, aKeyName, sizeof(aKeyName));
				Entry.m_KeyName = aKeyName;
				Entry.m_Command = pCommand;
				if(CBinds::IsReservedShortcutChord(Modifier))
					Entry.m_Flags |= OCCUPANCY_RESERVED_CHORD;
				if(std::string_view(pCommand).find(';') != std::string_view::npos)
					Entry.m_Flags |= OCCUPANCY_MULTI_ACTION;
				vEntries.push_back(std::move(Entry));
			}
		}
		for(SOccupancyEntry &Entry : vEntries)
		{
			const int Key = Entry.m_Slot.m_Key;
			if(Key < 0 || Key >= KEY_LAST)
				continue;
			const int Modifiers = aOccupiedModifiers[Key];
			// 无修饰键绑定被同键的其它修饰键组合遮蔽（保留和弦时不回落）。
			if(Entry.m_Slot.m_ModifierMask == KeyModifier::NONE && (Modifiers & ~(1 << KeyModifier::NONE)) != 0)
				Entry.m_Flags |= OCCUPANCY_SHADOWED;
			else if(Entry.m_Slot.m_ModifierMask != KeyModifier::NONE && (Modifiers & (1 << KeyModifier::NONE)) != 0 && !CBinds::AllowsUnmodifiedFallback(Key, Entry.m_Slot.m_ModifierMask))
				Entry.m_Flags |= OCCUPANCY_SHADOWED;
		}
		std::stable_sort(vEntries.begin(), vEntries.end(), [](const SOccupancyEntry &A, const SOccupancyEntry &B) {
			if(A.m_Flags != B.m_Flags)
				return A.m_Flags > B.m_Flags;
			if(A.m_Slot.m_ModifierMask != B.m_Slot.m_ModifierMask)
				return A.m_Slot.m_ModifierMask < B.m_Slot.m_ModifierMask;
			return A.m_Slot.m_Key < B.m_Slot.m_Key;
		});
		return vEntries;
	}

	const char *CompileErrorLabel(ECompileError Error)
	{
		switch(Error)
		{
		case ECompileError::MISSING_EVENT: return "Missing press event";
		case ECompileError::MULTIPLE_EVENTS: return "Only one press event is allowed";
		case ECompileError::CYCLE: return "Cycle in flow";
		case ECompileError::ORPHAN_NODE: return "Unreachable node";
		case ECompileError::EMPTY_COMMAND: return "Empty command";
		case ECompileError::NO_OUTPUT: return "No executable action";
		case ECompileError::TOO_LONG: return "Bind command line too long";
		case ECompileError::TOO_MANY_NESTING: return "Too many nested binds";
		default: return "Unknown error";
		}
	}

	const char *NodeTypeLabel(ENodeType Type)
	{
		switch(Type)
		{
		case ENodeType::EVENT_PRESS: return "Event - Press";
		case ENodeType::STATE_TOGGLE: return "State - Toggle";
		case ENodeType::STATE_TOGGLE_RESTORE: return "State - Toggle restore";
		case ENodeType::ACTION_COMMAND: return "Action - Command";
		case ENodeType::ACTION_ECHO: return "Action - Echo";
		case ENodeType::NOTE: return "Note";
		case ENodeType::GROUP: return "Group";
		default: return "Node";
		}
	}

	// 每种节点在帮助面板里的一句话说明：源 key 是英文，译文走翻译库。
	const char *NodeTypeHelp(ENodeType Type)
	{
		switch(Type)
		{
		case ENodeType::EVENT_PRESS: return "Flow starts here: when the key is pressed, the nodes after it run in order. For example Press H, then Command say go.";
		case ENodeType::STATE_TOGGLE: return "First press turns it on, the next press turns it off. For example Press K, then Toggle cl_show_hud.";
		case ENodeType::STATE_TOGGLE_RESTORE: return "Change a setting while the flow runs, then put it back.";
		case ENodeType::ACTION_COMMAND: return "Run one console or bind command.";
		case ENodeType::ACTION_ECHO: return "Print one line of text, without running it as a command.";
		case ENodeType::NOTE: return "Text for yourself only; it is never compiled into the cfg.";
		case ENodeType::GROUP: return "A frame that carries the nodes inside it; it is never compiled either. For example keep every dummy switch inside one frame.";
		default: return "Node";
		}
	}

	struct CBlueprintGraph::SGraphData
	{
		std::vector<SNode> m_vNodes;
		std::vector<SEdge> m_vEdges;
		ankerl::unordered_dense::map<int, size_t> m_IndexById;
		ankerl::unordered_dense::map<uint64_t, std::vector<int>> m_Cells;
		int m_NextId = 1;

		void RebuildIndex()
		{
			m_IndexById.clear();
			for(size_t Index = 0; Index < m_vNodes.size(); ++Index)
				m_IndexById[m_vNodes[Index].m_Id] = Index;
			RebuildCells();
		}

		void RebuildCells()
		{
			m_Cells.clear();
			for(const SNode &Node : m_vNodes)
				m_Cells[CellKey(CellOf(Node.m_X), CellOf(Node.m_Y))].push_back(Node.m_Id);
		}

		void UpdateNodeCell(int Id, float OldX, float OldY, float NewX, float NewY)
		{
			const uint64_t OldKey = CellKey(CellOf(OldX), CellOf(OldY));
			const uint64_t NewKey = CellKey(CellOf(NewX), CellOf(NewY));
			if(OldKey == NewKey)
				return;
			auto OldIt = m_Cells.find(OldKey);
			if(OldIt != m_Cells.end())
			{
				std::vector<int> &vIds = OldIt->second;
				vIds.erase(std::remove(vIds.begin(), vIds.end(), Id), vIds.end());
				if(vIds.empty())
					m_Cells.erase(OldIt);
			}
			m_Cells[NewKey].push_back(Id);
		}
	};

	CBlueprintGraph::CBlueprintGraph() :
		m_pData(std::make_unique<SGraphData>())
	{
	}

	CBlueprintGraph::~CBlueprintGraph() = default;

	CBlueprintGraph::CBlueprintGraph(CBlueprintGraph &&Other) noexcept = default;
	CBlueprintGraph &CBlueprintGraph::operator=(CBlueprintGraph &&Other) noexcept = default;

	void CBlueprintGraph::Clear()
	{
		m_pData->m_vNodes.clear();
		m_pData->m_vEdges.clear();
		m_pData->m_IndexById.clear();
		m_pData->m_Cells.clear();
		m_pData->m_NextId = 1;
	}

	CBlueprintGraph CBlueprintGraph::Clone() const
	{
		CBlueprintGraph Copy;
		*Copy.m_pData = *m_pData;
		return Copy;
	}

	void CBlueprintHistory::Record(size_t Flow, const CBlueprintGraph &Graph)
	{
		PushUndo(Flow, Graph.Clone());
		// 新编辑让「未来」失效：重做栈必须清空，否则会回到一条不存在的分支。
		m_vRedo.clear();
	}

	void CBlueprintHistory::PushUndo(size_t Flow, CBlueprintGraph Graph)
	{
		m_vUndo.push_back({Flow, std::move(Graph)});
		if(m_vUndo.size() > MAX_DEPTH)
			m_vUndo.erase(m_vUndo.begin());
	}

	void CBlueprintHistory::PushRedo(size_t Flow, CBlueprintGraph Graph)
	{
		m_vRedo.push_back({Flow, std::move(Graph)});
		if(m_vRedo.size() > MAX_DEPTH)
			m_vRedo.erase(m_vRedo.begin());
	}

	bool CBlueprintHistory::PopUndo(size_t &rFlow, CBlueprintGraph &rGraph)
	{
		if(m_vUndo.empty())
			return false;
		rFlow = m_vUndo.back().m_Flow;
		rGraph = std::move(m_vUndo.back().m_Graph);
		m_vUndo.pop_back();
		return true;
	}

	bool CBlueprintHistory::PopRedo(size_t &rFlow, CBlueprintGraph &rGraph)
	{
		if(m_vRedo.empty())
			return false;
		rFlow = m_vRedo.back().m_Flow;
		rGraph = std::move(m_vRedo.back().m_Graph);
		m_vRedo.pop_back();
		return true;
	}

	void CBlueprintHistory::Clear()
	{
		m_vUndo.clear();
		m_vRedo.clear();
	}

	int CBlueprintGraph::AddNode(SNode Node)
	{
		Node.m_Id = m_pData->m_NextId++;
		// 增量维护两张索引，避免批量建图退化成 O(n^2)。
		const size_t Index = m_pData->m_vNodes.size();
		m_pData->m_vNodes.push_back(std::move(Node));
		const SNode &Inserted = m_pData->m_vNodes[Index];
		m_pData->m_IndexById[Inserted.m_Id] = Index;
		m_pData->m_Cells[CellKey(CellOf(Inserted.m_X), CellOf(Inserted.m_Y))].push_back(Inserted.m_Id);
		return Inserted.m_Id;
	}

	bool CBlueprintGraph::RemoveNode(int Id)
	{
		const auto It = m_pData->m_IndexById.find(Id);
		if(It == m_pData->m_IndexById.end())
			return false;
		m_pData->m_vNodes.erase(m_pData->m_vNodes.begin() + static_cast<ptrdiff_t>(It->second));
		m_pData->m_vEdges.erase(std::remove_if(m_pData->m_vEdges.begin(), m_pData->m_vEdges.end(), [Id](const SEdge &Edge) { return Edge.m_From == Id || Edge.m_To == Id; }), m_pData->m_vEdges.end());
		m_pData->RebuildIndex();
		return true;
	}

	bool CBlueprintGraph::Connect(int From, int To)
	{
		if(From == To || !m_pData->m_IndexById.contains(From) || !m_pData->m_IndexById.contains(To))
			return false;
		const bool Exists = std::any_of(m_pData->m_vEdges.begin(), m_pData->m_vEdges.end(), [From, To](const SEdge &Edge) { return Edge.m_From == From && Edge.m_To == To; });
		if(Exists)
			return false;
		m_pData->m_vEdges.push_back({From, To});
		return true;
	}

	bool CBlueprintGraph::Disconnect(int From, int To)
	{
		const auto It = std::find_if(m_pData->m_vEdges.begin(), m_pData->m_vEdges.end(), [From, To](const SEdge &Edge) { return Edge.m_From == From && Edge.m_To == To; });
		if(It == m_pData->m_vEdges.end())
			return false;
		m_pData->m_vEdges.erase(It);
		return true;
	}

	bool CBlueprintGraph::MoveNode(int Id, float X, float Y)
	{
		const auto It = m_pData->m_IndexById.find(Id);
		if(It == m_pData->m_IndexById.end())
			return false;
		SNode &Node = m_pData->m_vNodes[It->second];
		const float OldX = Node.m_X;
		const float OldY = Node.m_Y;
		Node.m_X = X;
		Node.m_Y = Y;
		m_pData->UpdateNodeCell(Id, OldX, OldY, X, Y);
		return true;
	}

	bool CBlueprintGraph::SetNodeSize(int Id, float Width, float Height)
	{
		const auto It = m_pData->m_IndexById.find(Id);
		if(It == m_pData->m_IndexById.end())
			return false;
		SNode &Node = m_pData->m_vNodes[It->second];
		const float MinW = Node.m_Type == ENodeType::GROUP ? GROUP_MIN_WIDTH : NODE_WIDTH;
		const float MinH = Node.m_Type == ENodeType::GROUP ? GROUP_MIN_HEIGHT : NODE_HEIGHT;
		Node.m_W = std::max(MinW, Width);
		Node.m_H = std::max(MinH, Height);
		return true;
	}

	bool CBlueprintGraph::SetNodeCommand(int Id, std::string_view Command)
	{
		const auto It = m_pData->m_IndexById.find(Id);
		if(It == m_pData->m_IndexById.end())
			return false;
		m_pData->m_vNodes[It->second].m_Command = std::string(Command);
		return true;
	}

	bool CBlueprintGraph::SetNodeType(int Id, ENodeType Type)
	{
		const auto It = m_pData->m_IndexById.find(Id);
		if(It == m_pData->m_IndexById.end() || static_cast<int>(Type) < 0 || static_cast<int>(Type) >= static_cast<int>(ENodeType::NUM))
			return false;
		m_pData->m_vNodes[It->second].m_Type = Type;
		return true;
	}

	bool CBlueprintGraph::SetNodeComment(int Id, std::string_view Comment)
	{
		const auto It = m_pData->m_IndexById.find(Id);
		if(It == m_pData->m_IndexById.end())
			return false;
		m_pData->m_vNodes[It->second].m_Comment = std::string(Comment);
		return true;
	}

	int CBlueprintGraph::DuplicateNode(int Id, float OffsetX, float OffsetY)
	{
		const SNode *pSource = FindNode(Id);
		if(pSource == nullptr)
			return 0;
		// 复制体不继承边：连出去的边指向的是原节点的下游，原样抄一份会让两条
		// 流程同时驱动同一批节点。
		SNode Copy = *pSource;
		Copy.m_X += OffsetX;
		Copy.m_Y += OffsetY;
		return AddNode(std::move(Copy));
	}

	int CBlueprintGraph::DisconnectNode(int Id)
	{
		if(!m_pData->m_IndexById.contains(Id))
			return 0;
		const size_t Before = m_pData->m_vEdges.size();
		m_pData->m_vEdges.erase(std::remove_if(m_pData->m_vEdges.begin(), m_pData->m_vEdges.end(), [Id](const SEdge &Edge) { return Edge.m_From == Id || Edge.m_To == Id; }), m_pData->m_vEdges.end());
		return static_cast<int>(Before - m_pData->m_vEdges.size());
	}

	const std::vector<SNode> &CBlueprintGraph::Nodes() const { return m_pData->m_vNodes; }
	const std::vector<SEdge> &CBlueprintGraph::Edges() const { return m_pData->m_vEdges; }

	const SNode *CBlueprintGraph::FindNode(int Id) const
	{
		const auto It = m_pData->m_IndexById.find(Id);
		return It == m_pData->m_IndexById.end() ? nullptr : &m_pData->m_vNodes[It->second];
	}

	std::vector<int> CBlueprintGraph::TopologicalOrder(bool &rHasCycle) const
	{
		rHasCycle = false;
		const size_t Count = m_pData->m_vNodes.size();
		std::vector<int> vIndegree(Count, 0);
		std::vector<std::vector<size_t>> vAdjacency(Count);
		for(const SEdge &Edge : m_pData->m_vEdges)
		{
			const auto FromIt = m_pData->m_IndexById.find(Edge.m_From);
			const auto ToIt = m_pData->m_IndexById.find(Edge.m_To);
			if(FromIt == m_pData->m_IndexById.end() || ToIt == m_pData->m_IndexById.end())
				continue;
			vAdjacency[FromIt->second].push_back(ToIt->second);
			++vIndegree[ToIt->second];
		}
		std::deque<size_t> vQueue;
		for(size_t Index = 0; Index < Count; ++Index)
			if(vIndegree[Index] == 0)
				vQueue.push_back(Index);
		std::vector<int> vOrder;
		vOrder.reserve(Count);
		while(!vQueue.empty())
		{
			const size_t Index = vQueue.front();
			vQueue.pop_front();
			vOrder.push_back(m_pData->m_vNodes[Index].m_Id);
			for(const size_t Next : vAdjacency[Index])
				if(--vIndegree[Next] == 0)
					vQueue.push_back(Next);
		}
		rHasCycle = vOrder.size() != Count;
		return vOrder;
	}

	SCompileResult CBlueprintGraph::Compile(std::string_view KeyName) const
	{
		SCompileResult Result;
		const std::vector<SNode> &vNodes = m_pData->m_vNodes;
		std::vector<int> vEvents;
		for(const SNode &Node : vNodes)
			if(Node.m_Type == ENodeType::EVENT_PRESS)
				vEvents.push_back(Node.m_Id);
		if(vEvents.empty())
			Result.m_vErrors.push_back(ECompileError::MISSING_EVENT);
		else if(vEvents.size() > 1)
			Result.m_vErrors.push_back(ECompileError::MULTIPLE_EVENTS);

		bool HasCycle = false;
		const std::vector<int> vOrder = TopologicalOrder(HasCycle);
		if(HasCycle)
			Result.m_vErrors.push_back(ECompileError::CYCLE);

		// 从事件出发按拓扑序收集可达节点；不可达节点必须报错，否则画布上
		// 看起来连着的节点实际上不会被执行。
		std::set<int> Reachable;
		if(!vEvents.empty())
		{
			std::deque<int> vQueue{vEvents.front()};
			Reachable.insert(vEvents.front());
			while(!vQueue.empty())
			{
				const int Current = vQueue.front();
				vQueue.pop_front();
				for(const SEdge &Edge : m_pData->m_vEdges)
				{
					if(Edge.m_From != Current || Reachable.contains(Edge.m_To))
						continue;
					Reachable.insert(Edge.m_To);
					vQueue.push_back(Edge.m_To);
				}
			}
		}

		std::vector<std::string> vCommands;
		int Nesting = 0;
		for(const int Id : vOrder)
		{
			const SNode *pNode = FindNode(Id);
			if(pNode == nullptr)
				continue;
			// 注释与分组框都不产出命令。
			if(pNode->m_Type == ENodeType::NOTE || pNode->m_Type == ENodeType::GROUP)
				continue;
			if(!Reachable.contains(Id))
			{
				Result.m_vErrors.push_back(ECompileError::ORPHAN_NODE);
				continue;
			}
			if(pNode->m_Type == ENodeType::EVENT_PRESS)
				continue;
			if(pNode->m_Command.empty())
			{
				Result.m_vErrors.push_back(ECompileError::EMPTY_COMMAND);
				continue;
			}
			vCommands.push_back(pNode->m_Command);
			Nesting = std::max(Nesting, CountNesting(pNode->m_Command));
		}
		Result.m_Depth = Nesting;
		if(vCommands.empty())
			Result.m_vErrors.push_back(ECompileError::NO_OUTPUT);
		Result.m_Command = qm_bind_editor::JoinCommands(vCommands);
		if(!qm_bind_editor::FitsConfigLine(KeyName, Result.m_Command))
			Result.m_vErrors.push_back(ECompileError::TOO_LONG);
		if(Nesting > MAX_NESTING_DEPTH)
			Result.m_vErrors.push_back(ECompileError::TOO_MANY_NESTING);
		Result.m_ConfigLine = qm_bind_editor::BindCommand(KeyName, Result.m_Command);
		Result.m_Bytes = Result.m_ConfigLine.size();
		return Result;
	}

	std::vector<int> CBlueprintGraph::VisibleNodes(float MinX, float MinY, float MaxX, float MaxY) const
	{
		std::vector<int> vResult;
		const int MinCellX = CellOf(MinX - NODE_WIDTH);
		const int MaxCellX = CellOf(MaxX + NODE_WIDTH);
		const int MinCellY = CellOf(MinY - NODE_HEIGHT);
		const int MaxCellY = CellOf(MaxY + NODE_HEIGHT);
		// 只遍历视口覆盖的格子，节点总数再大也与查询成本无关。
		for(int CellX = MinCellX; CellX <= MaxCellX; ++CellX)
		{
			for(int CellY = MinCellY; CellY <= MaxCellY; ++CellY)
			{
				const auto It = m_pData->m_Cells.find(CellKey(CellX, CellY));
				if(It == m_pData->m_Cells.end())
					continue;
				for(const int Id : It->second)
				{
					const SNode *pNode = FindNode(Id);
					if(pNode == nullptr)
						continue;
					// 分组框比普通节点大，可见性判定必须按节点自己的尺寸算。
					if(pNode->m_X + pNode->m_W < MinX || pNode->m_X > MaxX || pNode->m_Y + pNode->m_H < MinY || pNode->m_Y > MaxY)
						continue;
					vResult.push_back(Id);
				}
			}
		}
		return vResult;
	}

	size_t CBlueprintGraph::IndexedCellCount() const { return m_pData->m_Cells.size(); }

	CBlueprintGraph GraphFromPreset(const SPreset &Preset, size_t VariantIndex)
	{
		CBlueprintGraph Graph;
		if(VariantIndex >= Preset.m_vVariants.size())
			return Graph;
		SNode Event;
		Event.m_Type = ENodeType::EVENT_PRESS;
		Event.m_X = 20.0f;
		Event.m_Y = 100.0f;
		Event.m_Comment = "Same-frame execution";
		const int EventId = Graph.AddNode(Event);
		const qm_bind_editor::SCommands Commands = qm_bind_editor::SplitCommands(ResolveVariantCommand(Preset, VariantIndex));
		float Y = 40.0f;
		int Previous = EventId;
		for(const std::string &Command : Commands.m_vCommands)
		{
			SNode Action;
			Action.m_Type = Command.rfind("echo", 0) == 0 ? ENodeType::ACTION_ECHO : ENodeType::ACTION_COMMAND;
			Action.m_X = 210.0f;
			Action.m_Y = Y;
			Action.m_Command = Command;
			const int Id = Graph.AddNode(Action);
			Graph.Connect(Previous, Id);
			Previous = Id;
			Y += 110.0f;
		}
		return Graph;
	}

	// ---------------------------------------------------------------------
	// 画布交互
	// ---------------------------------------------------------------------
	namespace
	{
		ECanvasPart PartAtPoint(const SNode &Node, float X, float Y, float Radius)
		{
			const float PortY = Node.m_Y + Node.m_H * 0.5f;
			// 分组框没有端口：它只是容器，不能被连线。
			if(Node.m_Type == ENodeType::GROUP)
			{
				const bool Inside = X >= Node.m_X && X <= Node.m_X + Node.m_W && Y >= Node.m_Y && Y <= Node.m_Y + Node.m_H;
				return Inside ? ECanvasPart::BODY : ECanvasPart::NONE;
			}
			if(std::fabs(Y - PortY) <= Radius)
			{
				if(std::fabs(X - Node.m_X) <= Radius)
					return ECanvasPart::IN_PORT;
				if(std::fabs(X - (Node.m_X + Node.m_W)) <= Radius)
					return ECanvasPart::OUT_PORT;
			}
			if(X >= Node.m_X && X <= Node.m_X + Node.m_W && Y >= Node.m_Y && Y <= Node.m_Y + Node.m_H)
				return ECanvasPart::BODY;
			return ECanvasPart::NONE;
		}

		int PartPriority(ECanvasPart Part)
		{
			switch(Part)
			{
			case ECanvasPart::OUT_PORT: return 3;
			case ECanvasPart::IN_PORT: return 2;
			case ECanvasPart::BODY: return 1;
			default: return 0;
			}
		}

		// 从 To 出发沿现有边能否走回 From：能走回说明新边会闭合一个环。
		// 检测放在建边之前，失败时不改图，画布上不会留下连了一半的线。
		bool WouldCreateCycle(const CBlueprintGraph &Graph, int From, int To)
		{
			std::vector<int> vStack{To};
			std::set<int> Visited;
			while(!vStack.empty())
			{
				const int Current = vStack.back();
				vStack.pop_back();
				if(Current == From)
					return true;
				if(!Visited.insert(Current).second)
					continue;
				for(const SEdge &Edge : Graph.Edges())
					if(Edge.m_From == Current)
						vStack.push_back(Edge.m_To);
			}
			return false;
		}

		bool HasEdge(const CBlueprintGraph &Graph, int From, int To)
		{
			for(const SEdge &Edge : Graph.Edges())
				if(Edge.m_From == From && Edge.m_To == To)
					return true;
			return false;
		}

		bool IsValidNodeType(ENodeType Type)
		{
			return static_cast<int>(Type) >= 0 && static_cast<int>(Type) < static_cast<int>(ENodeType::NUM);
		}
	}

	SCanvasHit HitTestCanvas(const CBlueprintGraph &Graph, float X, float Y, float Radius)
	{
		SCanvasHit Hit;
		// 只查点附近的网格桶：视口外/远处的节点根本不进入命中循环。
		const std::vector<int> vNear = Graph.VisibleNodes(X - NODE_WIDTH - Radius, Y - NODE_HEIGHT - Radius, X + NODE_WIDTH + Radius, Y + NODE_HEIGHT + Radius);
		int BestPriority = 0;
		for(const int Id : vNear)
		{
			const SNode *pNode = Graph.FindNode(Id);
			if(pNode == nullptr)
				continue;
			const int Priority = PartPriority(PartAtPoint(*pNode, X, Y, Radius));
			if(Priority == 0)
				continue;
			// 端口优先于节点体；同优先级取 ID 大的，后添加的节点盖在上面。
			if(Priority > BestPriority || (Priority == BestPriority && Id > Hit.m_NodeId))
			{
				BestPriority = Priority;
				Hit.m_NodeId = Id;
				Hit.m_Part = PartAtPoint(*pNode, X, Y, Radius);
			}
		}
		return Hit;
	}

	namespace
	{
		float DistanceToSegment(float PX, float PY, float AX, float AY, float BX, float BY)
		{
			const float DX = BX - AX;
			const float DY = BY - AY;
			const float LengthSq = DX * DX + DY * DY;
			float T = 0.0f;
			if(LengthSq > 0.0001f)
				T = std::clamp(((PX - AX) * DX + (PY - AY) * DY) / LengthSq, 0.0f, 1.0f);
			const float ClosestX = AX + DX * T;
			const float ClosestY = AY + DY * T;
			return std::sqrt((PX - ClosestX) * (PX - ClosestX) + (PY - ClosestY) * (PY - ClosestY));
		}
	}

	bool ResolveEdgePolyline(const CBlueprintGraph &Graph, int From, int To, SEdgePolyline &rOut)
	{
		const SNode *pFrom = Graph.FindNode(From);
		const SNode *pTo = Graph.FindNode(To);
		if(pFrom == nullptr || pTo == nullptr)
			return false;
		rOut.m_X0 = pFrom->m_X + pFrom->m_W;
		rOut.m_Y0 = pFrom->m_Y + pFrom->m_H * 0.5f;
		rOut.m_X1 = pTo->m_X;
		rOut.m_Y1 = pTo->m_Y + pTo->m_H * 0.5f;
		rOut.m_MidX = (rOut.m_X0 + rOut.m_X1) * 0.5f;
		return true;
	}

	std::vector<SCanvasPoint> SampleEdgeCurve(const SEdgePolyline &Line)
	{
		std::vector<SCanvasPoint> vPoints;
		vPoints.reserve(EDGE_CURVE_SEGMENTS + 1);
		// 水平出/入的控制点：dx 的六成，最少 48，太短也会有点弧度。
		const float Control = std::max(48.0f, std::fabs(Line.m_X1 - Line.m_X0) * 0.6f);
		const float P1X = Line.m_X0 + Control;
		const float P2X = Line.m_X1 - Control;
		for(int Step = 0; Step <= EDGE_CURVE_SEGMENTS; ++Step)
		{
			const float T = static_cast<float>(Step) / static_cast<float>(EDGE_CURVE_SEGMENTS);
			const float U = 1.0f - T;
			const float B0 = U * U * U;
			const float B1 = 3.0f * U * U * T;
			const float B2 = 3.0f * U * T * T;
			const float B3 = T * T * T;
			SCanvasPoint Point;
			Point.m_X = B0 * Line.m_X0 + B1 * P1X + B2 * P2X + B3 * Line.m_X1;
			Point.m_Y = B0 * Line.m_Y0 + B1 * Line.m_Y0 + B2 * Line.m_Y1 + B3 * Line.m_Y1;
			vPoints.push_back(Point);
		}
		return vPoints;
	}

	SNodeAlignment ResolveNodeAlignment(const CBlueprintGraph &Graph, int DragId, float X, float Y)
	{
		SNodeAlignment Result;
		Result.m_X = X;
		Result.m_Y = Y;
		const SNode *pDrag = Graph.FindNode(DragId);
		if(pDrag == nullptr)
			return Result;
		const float W = pDrag->m_W;
		const float H = pDrag->m_H;
		float BestX = ALIGN_SNAP_DISTANCE;
		float BestY = ALIGN_SNAP_DISTANCE;
		for(const SNode &Node : Graph.Nodes())
		{
			if(Node.m_Id == DragId)
				continue;
			// 三组候选：左/中/右 与 上/中/下 分别对齐。
			const float OurX[3] = {X, X + W * 0.5f, X + W};
			const float TheirX[3] = {Node.m_X, Node.m_X + Node.m_W * 0.5f, Node.m_X + Node.m_W};
			for(const float A : OurX)
			{
				for(const float B : TheirX)
				{
					const float Dist = std::fabs(A - B);
					// 距离相同取先找到的那组，参考线稳定落在左边/上边的对齐上。
					if(Dist < BestX)
					{
						BestX = Dist;
						Result.m_X = X + (B - A);
						Result.m_GuideX = B;
						Result.m_SnappedX = true;
					}
				}
			}
			const float OurY[3] = {Y, Y + H * 0.5f, Y + H};
			const float TheirY[3] = {Node.m_Y, Node.m_Y + Node.m_H * 0.5f, Node.m_Y + Node.m_H};
			for(const float A : OurY)
			{
				for(const float B : TheirY)
				{
					const float Dist = std::fabs(A - B);
					if(Dist < BestY)
					{
						BestY = Dist;
						Result.m_Y = Y + (B - A);
						Result.m_GuideY = B;
						Result.m_SnappedY = true;
					}
				}
			}
		}
		return Result;
	}

	SEdgeHit HitTestEdge(const CBlueprintGraph &Graph, float X, float Y, float Tolerance)
	{
		SEdgeHit Hit;
		float BestDistance = Tolerance;
		for(const SEdge &Edge : Graph.Edges())
		{
			SEdgePolyline Line;
			if(!ResolveEdgePolyline(Graph, Edge.m_From, Edge.m_To, Line))
				continue;
			// 包围盒先挡掉绝大多数边，省掉逐段点线距离。
			const float MinX = std::min(Line.m_X0, Line.m_X1) - Tolerance;
			const float MaxX = std::max(Line.m_X0, Line.m_X1) + Tolerance;
			const float MinY = std::min(Line.m_Y0, Line.m_Y1) - Tolerance;
			const float MaxY = std::max(Line.m_Y0, Line.m_Y1) + Tolerance;
			if(X < MinX || X > MaxX || Y < MinY || Y > MaxY)
				continue;
			const std::vector<SCanvasPoint> vPoints = SampleEdgeCurve(Line);
			float Distance = Tolerance;
			for(size_t Index = 1; Index < vPoints.size(); ++Index)
			{
				Distance = std::min(Distance, DistanceToSegment(X, Y, vPoints[Index - 1].m_X, vPoints[Index - 1].m_Y, vPoints[Index].m_X, vPoints[Index].m_Y));
			}
			// 距离相同取先出现的那条，结果只取决于边的注册顺序。
			if(Distance < BestDistance)
			{
				BestDistance = Distance;
				Hit.m_From = Edge.m_From;
				Hit.m_To = Edge.m_To;
			}
		}
		return Hit;
	}

	std::vector<int> NodesInRect(const CBlueprintGraph &Graph, float MinX, float MinY, float MaxX, float MaxY)
	{
		std::vector<int> vIds;
		if(MaxX < MinX)
			std::swap(MinX, MaxX);
		if(MaxY < MinY)
			std::swap(MinY, MaxY);
		// 网格桶先挡掉视口外的节点，框再大也不线性扫全图。
		for(const int Id : Graph.VisibleNodes(MinX, MinY, MaxX, MaxY))
		{
			const SNode *pNode = Graph.FindNode(Id);
			if(pNode == nullptr)
				continue;
			const bool Overlaps = pNode->m_X <= MaxX && pNode->m_X + pNode->m_W >= MinX &&
					      pNode->m_Y <= MaxY && pNode->m_Y + pNode->m_H >= MinY;
			if(Overlaps)
				vIds.push_back(Id);
		}
		std::sort(vIds.begin(), vIds.end());
		return vIds;
	}

	namespace
	{
		// 中心点落在框里算「在框内」：半个节点搭在框上不算，拖起来才符合直觉。
		bool NodeCenterInsideGroup(const SNode &Node, const SNode &Group)
		{
			const float CenterX = Node.m_X + Node.m_W * 0.5f;
			const float CenterY = Node.m_Y + Node.m_H * 0.5f;
			return CenterX >= Group.m_X && CenterX <= Group.m_X + Group.m_W &&
			       CenterY >= Group.m_Y && CenterY <= Group.m_Y + Group.m_H;
		}
	} // namespace

	int GroupOwningNode(const CBlueprintGraph &Graph, int NodeId)
	{
		const SNode *pNode = Graph.FindNode(NodeId);
		if(pNode == nullptr || pNode->m_Type == ENodeType::GROUP)
			return 0;
		int OwnerId = 0;
		float OwnerArea = 0.0f;
		for(const SNode &Group : Graph.Nodes())
		{
			if(Group.m_Type != ENodeType::GROUP || Group.m_Id == NodeId)
				continue;
			if(!NodeCenterInsideGroup(*pNode, Group))
				continue;
			// 面积小的框更具体，重叠时算它；面积相同按 Id 小的胜，
			// 归属只由几何决定，与遍历顺序无关。
			const float Area = Group.m_W * Group.m_H;
			if(OwnerId == 0 || Area < OwnerArea || (Area == OwnerArea && Group.m_Id < OwnerId))
			{
				OwnerId = Group.m_Id;
				OwnerArea = Area;
			}
		}
		return OwnerId;
	}

	std::vector<int> NodesContainedIn(const CBlueprintGraph &Graph, int GroupId)
	{
		std::vector<int> vIds;
		const SNode *pGroup = Graph.FindNode(GroupId);
		if(pGroup == nullptr || pGroup->m_Type != ENodeType::GROUP)
			return vIds;
		for(const SNode &Node : Graph.Nodes())
		{
			if(Node.m_Id == GroupId || Node.m_Type == ENodeType::GROUP)
				continue;
			// 只收自己拥有的节点：两个框重叠时，被双方同时罩住的节点只会跟一个框走，
			// 否则拖一次会把它的位置叠加两次。
			if(GroupOwningNode(Graph, Node.m_Id) != GroupId)
				continue;
			vIds.push_back(Node.m_Id);
		}
		std::sort(vIds.begin(), vIds.end());
		return vIds;
	}

	SCanvasBounds GraphBounds(const CBlueprintGraph &Graph)
	{
		SCanvasBounds Bounds;
		for(const SNode &Node : Graph.Nodes())
		{
			const float MaxX = Node.m_X + Node.m_W;
			const float MaxY = Node.m_Y + Node.m_H;
			if(!Bounds.m_HasNodes)
			{
				Bounds.m_MinX = Node.m_X;
				Bounds.m_MinY = Node.m_Y;
				Bounds.m_MaxX = MaxX;
				Bounds.m_MaxY = MaxY;
				Bounds.m_HasNodes = true;
				continue;
			}
			Bounds.m_MinX = std::min(Bounds.m_MinX, Node.m_X);
			Bounds.m_MinY = std::min(Bounds.m_MinY, Node.m_Y);
			Bounds.m_MaxX = std::max(Bounds.m_MaxX, MaxX);
			Bounds.m_MaxY = std::max(Bounds.m_MaxY, MaxY);
		}
		return Bounds;
	}

	namespace
	{
		float ClampGeometryValue(float Value, float Min, float Max)
		{
			if(!std::isfinite(Value))
				return Min;
			return std::clamp(Value, Min, Max);
		}

		// 对齐到步长的整数格：面板里 ±1 格永远落在同一套格点上，
		// 拖动拖出来的 118.37 这种值也会先归到最近的刻度再走一格。
		float SnapGeometryValue(float Value)
		{
			if(!std::isfinite(Value))
				return 0.0f;
			return std::round(Value / NODE_GEOMETRY_STEP) * NODE_GEOMETRY_STEP;
		}
	}

	void NodeGeometryMinSize(ENodeType Type, float &rMinW, float &rMinH)
	{
		rMinW = Type == ENodeType::GROUP ? GROUP_MIN_WIDTH : NODE_WIDTH;
		rMinH = Type == ENodeType::GROUP ? GROUP_MIN_HEIGHT : NODE_HEIGHT;
	}

	SNodeGeometry ReadNodeGeometry(const SNode &Node)
	{
		return SNodeGeometry{Node.m_X, Node.m_Y, Node.m_W, Node.m_H};
	}

	int NodeGeometryDisplayValue(const SNodeGeometry &Geometry, ENodeGeometryField Field)
	{
		float Value = 0.0f;
		switch(Field)
		{
		case ENodeGeometryField::X: Value = Geometry.m_X; break;
		case ENodeGeometryField::Y: Value = Geometry.m_Y; break;
		case ENodeGeometryField::WIDTH: Value = Geometry.m_W; break;
		case ENodeGeometryField::HEIGHT: Value = Geometry.m_H; break;
		default: return 0;
		}
		if(!std::isfinite(Value))
			return 0;
		return static_cast<int>(std::lround(Value));
	}

	SNodeGeometry ClampNodeGeometry(ENodeType Type, const SNodeGeometry &Geometry)
	{
		float MinW = 0.0f;
		float MinH = 0.0f;
		NodeGeometryMinSize(Type, MinW, MinH);
		SNodeGeometry Out;
		Out.m_X = ClampGeometryValue(Geometry.m_X, -NODE_GEOMETRY_LIMIT, NODE_GEOMETRY_LIMIT);
		Out.m_Y = ClampGeometryValue(Geometry.m_Y, -NODE_GEOMETRY_LIMIT, NODE_GEOMETRY_LIMIT);
		Out.m_W = ClampGeometryValue(Geometry.m_W, MinW, NODE_GEOMETRY_LIMIT);
		Out.m_H = ClampGeometryValue(Geometry.m_H, MinH, NODE_GEOMETRY_LIMIT);
		return Out;
	}

	SNodeGeometry StepNodeGeometry(ENodeType Type, const SNodeGeometry &Geometry, ENodeGeometryField Field, int Direction, int Steps)
	{
		SNodeGeometry Out = ClampNodeGeometry(Type, Geometry);
		const int StepCount = Steps > 0 ? Steps : 0;
		// Direction 为 0 只做对齐、不移动：面板在 Enabled 边界与显式对齐时都靠这个语义。
		const int Sign = Direction < 0 ? -1 : (Direction > 0 ? 1 : 0);
		const float Delta = NODE_GEOMETRY_STEP * static_cast<float>(StepCount) * static_cast<float>(Sign);
		switch(Field)
		{
		case ENodeGeometryField::X: Out.m_X = SnapGeometryValue(Out.m_X) + Delta; break;
		case ENodeGeometryField::Y: Out.m_Y = SnapGeometryValue(Out.m_Y) + Delta; break;
		case ENodeGeometryField::WIDTH: Out.m_W = SnapGeometryValue(Out.m_W) + Delta; break;
		case ENodeGeometryField::HEIGHT: Out.m_H = SnapGeometryValue(Out.m_H) + Delta; break;
		default: break;
		}
		return ClampNodeGeometry(Type, Out);
	}

	bool ApplyNodeGeometry(CBlueprintGraph &Graph, int NodeId, const SNodeGeometry &Geometry)
	{
		const SNode *pNode = Graph.FindNode(NodeId);
		if(pNode == nullptr)
			return false;
		// 先取类型：MoveNode 之后不再解引用节点指针，避免后续字段改动让它失效。
		const ENodeType Type = pNode->m_Type;
		const SNodeGeometry Clamped = ClampNodeGeometry(Type, Geometry);
		bool Applied = Graph.MoveNode(NodeId, Clamped.m_X, Clamped.m_Y);
		if(Type == ENodeType::GROUP)
			Applied = Graph.SetNodeSize(NodeId, Clamped.m_W, Clamped.m_H) && Applied;
		return Applied;
	}

	SCanvasBounds SelectionBounds(const CBlueprintGraph &Graph, const std::vector<int> &vIds)
	{
		SCanvasBounds Bounds;
		for(const int Id : vIds)
		{
			const SNode *pNode = Graph.FindNode(Id);
			if(pNode == nullptr)
				continue;
			const float MaxX = pNode->m_X + pNode->m_W;
			const float MaxY = pNode->m_Y + pNode->m_H;
			if(!Bounds.m_HasNodes)
			{
				Bounds.m_MinX = pNode->m_X;
				Bounds.m_MinY = pNode->m_Y;
				Bounds.m_MaxX = MaxX;
				Bounds.m_MaxY = MaxY;
				Bounds.m_HasNodes = true;
				continue;
			}
			Bounds.m_MinX = std::min(Bounds.m_MinX, pNode->m_X);
			Bounds.m_MinY = std::min(Bounds.m_MinY, pNode->m_Y);
			Bounds.m_MaxX = std::max(Bounds.m_MaxX, MaxX);
			Bounds.m_MaxY = std::max(Bounds.m_MaxY, MaxY);
		}
		return Bounds;
	}

	SCanvasPoint SnapToCanvasGrid(float X, float Y)
	{
		SCanvasPoint Point;
		if(!std::isfinite(X) || !std::isfinite(Y))
			return Point;
		Point.m_X = std::round(X / GRID_CELL_SIZE) * GRID_CELL_SIZE;
		Point.m_Y = std::round(Y / GRID_CELL_SIZE) * GRID_CELL_SIZE;
		return Point;
	}

	const char *LinkOutcomeLabel(ELinkOutcome Outcome)
	{
		switch(Outcome)
		{
		case ELinkOutcome::CONNECTED: return "Linked";
		case ELinkOutcome::NO_SOURCE: return "Link source is gone";
		case ELinkOutcome::NO_TARGET: return "Drop on an input port to link";
		case ELinkOutcome::SELF: return "A node cannot link to itself";
		case ELinkOutcome::DUPLICATE: return "Those nodes are already linked";
		case ELinkOutcome::CYCLE: return "That link would close a loop";
		default: return "Link failed";
		}
	}

	ELinkOutcome ResolveLinkDrop(CBlueprintGraph &Graph, int FromId, float X, float Y)
	{
		if(FromId == 0 || Graph.FindNode(FromId) == nullptr)
			return ELinkOutcome::NO_SOURCE;
		const SCanvasHit Target = HitTestCanvas(Graph, X, Y);
		if(Target.m_NodeId == 0)
			return ELinkOutcome::NO_TARGET;
		if(Target.m_NodeId == FromId)
			return ELinkOutcome::SELF;
		// 落在节点体上也接受：输入端口只有 12 个画布单位，强行要求精确点中会很难用。
		if(Target.m_Part != ECanvasPart::IN_PORT && Target.m_Part != ECanvasPart::BODY)
			return ELinkOutcome::NO_TARGET;
		if(HasEdge(Graph, FromId, Target.m_NodeId))
			return ELinkOutcome::DUPLICATE;
		if(WouldCreateCycle(Graph, FromId, Target.m_NodeId))
			return ELinkOutcome::CYCLE;
		if(!Graph.Connect(FromId, Target.m_NodeId))
			return ELinkOutcome::NO_TARGET;
		return ELinkOutcome::CONNECTED;
	}

	const std::vector<SNodeTemplate> &NodeTemplates()
	{
		static const std::vector<SNodeTemplate> s_vTemplates = {
			{"Press event", "Starts the flow when the key is pressed.", ENodeType::EVENT_PRESS, "", ""},
			{"Run command", "Any console command, for example toggle or bind.", ENodeType::ACTION_COMMAND, "toggle cl_dummy 0 1", ""},
			{"Echo", "Print a line in the console so you can see the flow fire.", ENodeType::ACTION_ECHO, "echo QmClient", ""},
			{"Toggle state", "Flip a setting between two values.", ENodeType::STATE_TOGGLE, "toggle cl_dummy_hammer 0 1", ""},
			{"Toggle restore", "Flip a setting and restore it when the key is released.", ENodeType::STATE_TOGGLE_RESTORE, "+toggle_restore inp_mousesens 1", ""},
			{"Group frame", "A frame that holds a cluster of nodes; dragging it moves its members.", ENodeType::GROUP, "", ""},
			{"Delay note", "A note node: documents that the flow has no time delay.", ENodeType::NOTE, "", "No delay: every command runs in the same frame."},
		};
		return s_vTemplates;
	}

	int AddNodeFromTemplate(CBlueprintGraph &Graph, size_t TemplateIndex, float X, float Y)
	{
		const std::vector<SNodeTemplate> &vTemplates = NodeTemplates();
		if(TemplateIndex >= vTemplates.size())
			return 0;
		const SNodeTemplate &Template = vTemplates[TemplateIndex];
		const SCanvasPoint Point = SnapToCanvasGrid(X, Y);
		SNode Node;
		Node.m_Type = Template.m_Type;
		Node.m_X = Point.m_X;
		Node.m_Y = Point.m_Y;
		Node.m_Command = Template.m_pCommand != nullptr ? Template.m_pCommand : "";
		Node.m_Comment = Template.m_pComment != nullptr ? Template.m_pComment : "";
		// 分组框用更大的默认尺寸，普通节点保持 NODE_WIDTH / NODE_HEIGHT。
		if(Node.m_Type == ENodeType::GROUP)
		{
			Node.m_W = GROUP_WIDTH;
			Node.m_H = GROUP_HEIGHT;
		}
		return Graph.AddNode(std::move(Node));
	}

	const std::vector<SCommandSnippet> &CommandSnippets()
	{
		static const std::vector<SCommandSnippet> s_vSnippets = {
			{"echo", "echo QmClient"},
			{"bind", "bind f1 \"echo nested\""},
			{"toggle", "toggle cl_dummy 0 1"},
			{"+hook", "+hook"},
			{"mouse1", "+fire"},
			{"exec", "exec qm_bind_blueprint.cfg"},
		};
		return s_vSnippets;
	}

	std::string InsertSnippet(std::string_view Existing, std::string_view Snippet)
	{
		std::string Base(Existing);
		while(!Base.empty() && (Base.back() == ' ' || Base.back() == '\t'))
			Base.pop_back();
		while(!Base.empty() && Base.back() == ';')
		{
			Base.pop_back();
			while(!Base.empty() && Base.back() == ' ')
				Base.pop_back();
		}
		if(Snippet.empty())
			return Base;
		if(Base.empty())
			return std::string(Snippet);
		// 逐段比较而不是子串查找：点两次同一个片段不该得到两条相同命令，
		// 但「echo a」也不该阻止插入「echo abc」。
		const qm_bind_editor::SCommands Split = qm_bind_editor::SplitCommands(Base);
		for(const std::string &Command : Split.m_vCommands)
		{
			if(Command == Snippet)
				return Base;
		}
		return Base + "; " + std::string(Snippet);
	}

	ENodeType CycleNodeType(ENodeType Type, int Direction)
	{
		const int Count = static_cast<int>(ENodeType::NUM);
		if(Count <= 0 || !IsValidNodeType(Type))
			return ENodeType::ACTION_COMMAND;
		int Index = static_cast<int>(Type) + (Direction >= 0 ? 1 : -1);
		Index = ((Index % Count) + Count) % Count;
		return static_cast<ENodeType>(Index);
	}

	// ---------------------------------------------------------------------
	// 多张图与 cfg 导出
	// ---------------------------------------------------------------------
	std::string SlugifyTitle(std::string_view Title)
	{
		std::string Result;
		bool LastUnderscore = false;
		for(const char Character : Title)
		{
			const unsigned char Byte = static_cast<unsigned char>(Character);
			if((Byte >= 'a' && Byte <= 'z') || (Byte >= '0' && Byte <= '9'))
			{
				Result.push_back(Character);
				LastUnderscore = false;
			}
			else if(Byte >= 'A' && Byte <= 'Z')
			{
				Result.push_back(static_cast<char>(Byte - 'A' + 'a'));
				LastUnderscore = false;
			}
			else if(!Result.empty() && !LastUnderscore)
			{
				Result.push_back('_');
				LastUnderscore = true;
			}
		}
		while(!Result.empty() && Result.back() == '_')
			Result.pop_back();
		if(Result.empty())
			Result = "flow";
		return Result;
	}

	std::string BlueprintFileName(std::string_view Title, size_t DuplicateIndex)
	{
		std::string Result = "qm_bind_blueprint_";
		Result += SlugifyTitle(Title);
		if(DuplicateIndex > 1)
		{
			Result += '_';
			Result += std::to_string(DuplicateIndex);
		}
		Result += ".cfg";
		return Result;
	}

	CFlowCollection::CFlowCollection()
	{
		m_vFlows.push_back(SFlowFile{});
		m_vFlows.front().m_Title = "Untitled flow";
		RefreshFileNames();
	}

	size_t CFlowCollection::Count() const { return m_vFlows.size(); }

	size_t CFlowCollection::ActiveIndex() const { return m_Active < m_vFlows.size() ? m_Active : 0; }

	bool CFlowCollection::SetActive(size_t Index)
	{
		if(Index >= m_vFlows.size())
			return false;
		m_Active = Index;
		return true;
	}

	const SFlowFile &CFlowCollection::Flow(size_t Index) const { return m_vFlows[Index < m_vFlows.size() ? Index : ActiveIndex()]; }

	CBlueprintGraph &CFlowCollection::Graph(size_t Index) { return m_vFlows[Index < m_vFlows.size() ? Index : ActiveIndex()].m_Graph; }

	const CBlueprintGraph &CFlowCollection::Graph(size_t Index) const { return m_vFlows[Index < m_vFlows.size() ? Index : ActiveIndex()].m_Graph; }

	std::string CFlowCollection::Title(size_t Index) const { return Index < m_vFlows.size() ? m_vFlows[Index].m_Title : std::string(); }

	std::string CFlowCollection::FileName(size_t Index) const { return Index < m_vFlows.size() ? m_vFlows[Index].m_FileName : std::string(); }

	size_t CFlowCollection::AddFlow(std::string_view Title)
	{
		SFlowFile Flow;
		Flow.m_Title = Title.empty() ? std::string("Untitled flow") : std::string(Title);
		m_vFlows.push_back(std::move(Flow));
		RefreshFileNames();
		return m_vFlows.size() - 1;
	}

	bool CFlowCollection::RemoveFlow(size_t Index)
	{
		// 至少保留一张图：标签栏不能变成空的。
		if(Index >= m_vFlows.size() || m_vFlows.size() <= 1)
			return false;
		m_vFlows.erase(m_vFlows.begin() + static_cast<ptrdiff_t>(Index));
		if(m_Active >= m_vFlows.size())
			m_Active = m_vFlows.size() - 1;
		RefreshFileNames();
		return true;
	}

	bool CFlowCollection::RenameFlow(size_t Index, std::string_view Title)
	{
		if(Index >= m_vFlows.size() || Title.empty())
			return false;
		m_vFlows[Index].m_Title = std::string(Title);
		RefreshFileNames();
		return true;
	}

	void CFlowCollection::RefreshFileNames()
	{
		for(size_t Index = 0; Index < m_vFlows.size(); ++Index)
		{
			const std::string Slug = SlugifyTitle(m_vFlows[Index].m_Title);
			size_t Suffix = 1;
			for(size_t Previous = 0; Previous < Index; ++Previous)
			{
				if(SlugifyTitle(m_vFlows[Previous].m_Title) == Slug)
					++Suffix;
			}
			m_vFlows[Index].m_FileName = BlueprintFileName(m_vFlows[Index].m_Title, Suffix);
		}
	}

	SCfgBundle CFlowCollection::BuildCfg(size_t Index, std::string_view KeyName) const
	{
		SCfgBundle Bundle;
		if(Index >= m_vFlows.size())
			return Bundle;
		const SFlowFile &Flow = m_vFlows[Index];
		Bundle.m_FileName = Flow.m_FileName;
		Bundle.m_Title = Flow.m_Title;
		Bundle.m_Content = "# QmClient bind blueprint: " + Flow.m_Title + "\n";
		Bundle.m_Content += "# Generated by the QmClient bind workshop; every command runs in the same frame.\n";
		const SCompileResult Compile = Flow.m_Graph.Compile(KeyName);
		if(!Compile.Ok())
		{
			Bundle.m_Content += "# Rejected: the flow does not validate, so no bind line was written.\n";
			for(const ECompileError Error : Compile.m_vErrors)
				Bundle.m_Content += std::string("#   ") + CompileErrorLabel(Error) + "\n";
			return Bundle;
		}
		Bundle.m_Content += "# Apply with: exec " + Flow.m_FileName + "\n";
		Bundle.m_Content += Compile.m_ConfigLine + "\n";
		Bundle.m_Ok = true;
		Bundle.m_BindLines = 1;
		return Bundle;
	}

	std::vector<SCfgBundle> CFlowCollection::BuildAllCfg(std::string_view KeyName) const
	{
		std::vector<SCfgBundle> vBundles;
		vBundles.reserve(m_vFlows.size());
		for(size_t Index = 0; Index < m_vFlows.size(); ++Index)
			vBundles.push_back(BuildCfg(Index, KeyName));
		return vBundles;
	}
}
