#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_MENU_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_MENU_H

#include <engine/shared/localization.h>

// 快速练习 / 普通练习菜单的内置条目。名称在渲染时用 Localize() 取译文；
// 命令按控制台语法执行，聊天指令统一走 "say /xxx"，与玩家手动输入走同一条路径。
namespace qm_practice_menu
{
	struct SEntry
	{
		const char *m_pName;
		const char *m_pCommand;
	};

	// 快速练习：客户端分支世界里的动作 + 常用指令。
	inline constexpr SEntry s_aFastPracticeEntries[] = {
		{Localizable("Enter fast practice"), "fast_practice_enter"},
		{Localizable("Exit fast practice"), "fast_practice_exit"},
		{Localizable("Rewind practice world"), "practice_restart"},
		{Localizable("Set practice respawn point"), "practice_set_checkpoint"},
		{Localizable("Return to respawn point"), "practice_restore_checkpoint"},
		{Localizable("Save practice line"), "practice_save_ghost"},
		{Localizable("Start recording"), "practice_record_start"},
		{Localizable("Pause recording"), "practice_record_pause"},
		{Localizable("Stop and save recording"), "practice_record_stop"},
		{Localizable("Rescue"), "say /r"},
		{Localizable("Back"), "say /back"},
		{Localizable("Teleport to cursor"), "say /tc"},
		{Localizable("Invincible"), "say /invincible"},
		{Localizable("Enter solo mode"), "say /solo"},
		{Localizable("Exit solo mode"), "say /unsolo"},
		{Localizable("Get all weapons"), "say /weapons"},
		{Localizable("Remove all weapons"), "say /unweapons"},
	};

	// 普通练习模式：服务器 practice 队伍可用的无参指令。
	inline constexpr SEntry s_aPracticeEntries[] = {
		{Localizable("Rescue"), "say /r"},
		{Localizable("Back"), "say /back"},
		{Localizable("Rescue mode"), "say /rescuemode"},
		{Localizable("Enter solo mode"), "say /solo"},
		{Localizable("Exit solo mode"), "say /unsolo"},
		{Localizable("Deep freeze"), "say /deep"},
		{Localizable("Undo deep freeze"), "say /undeep"},
		{Localizable("Get all weapons"), "say /weapons"},
		{Localizable("Remove all weapons"), "say /unweapons"},
		{Localizable("Shotgun"), "say /shotgun"},
		{Localizable("Grenade"), "say /grenade"},
		{Localizable("Laser"), "say /laser"},
		{Localizable("Rifle"), "say /rifle"},
		{Localizable("Jetpack"), "say /jetpack"},
		{Localizable("Ninja"), "say /ninja"},
		{Localizable("Endless hook"), "say /endless"},
	};
} // namespace qm_practice_menu

#endif
