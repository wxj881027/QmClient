#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PLAYBACK_SHORTCUTS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PLAYBACK_SHORTCUTS_H

#include <engine/keys.h>

// 回放播放器（demo 与 Rank 1 查看模式共用）的键盘快捷键清单。
// 这里是唯一的登记处：菜单侧的快捷键处理只认表里的键，绑定层也按同一张表
// 让路。两处各写一份清单时，新增快捷键很容易只改一边，结果就是回放按键
// 漏进在线游戏（默认 k = kill，按 K 暂停会顺手杀掉自己的 tee）。
// 新增快捷键时先在这里登记，菜单侧再调用同一个键；漏登记会直接失效，
// 不会静默漏进绑定层。
inline bool QmPlaybackShortcutKey(int Key)
{
	switch(Key)
	{
	case KEY_P:
	case KEY_UP:
	case KEY_DOWN:
	case KEY_SPACE:
	case KEY_RETURN:
	case KEY_KP_ENTER:
	case KEY_K:
	case KEY_LEFT:
	case KEY_J:
	case KEY_RIGHT:
	case KEY_L:
	case KEY_0:
	case KEY_1:
	case KEY_2:
	case KEY_3:
	case KEY_4:
	case KEY_5:
	case KEY_6:
	case KEY_7:
	case KEY_8:
	case KEY_9:
	case KEY_HOME:
	case KEY_END:
	case KEY_PERIOD:
	case KEY_COMMA:
	case KEY_C:
	case KEY_MOUSE_WHEEL_UP:
	case KEY_MOUSE_WHEEL_DOWN:
		return true;
	default:
		return false;
	}
}

#endif
