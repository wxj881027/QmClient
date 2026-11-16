#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MOBILE_BACK_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MOBILE_BACK_H

// 手机端返回栈：按“最上面一层先关”的顺序决定这一次返回该做什么。
// 单独抽出来是为了能直接用真值表测，不必启动客户端。
enum class EQmMobileBackTarget
{
	NONE,
	// 打开的 CUi 弹层
	POPUP,
	CHAT,
	SCOREBOARD,
	// 旁观者列表等旁观界面
	SPECTATOR,
	// 关闭菜单
	MENU_CLOSE,
	// 空栈：打开菜单（等价桌面 ESC）
	MENU_OPEN,
	// 菜单里短时间内第二次返回：退出确认
	QUIT,
};

inline EQmMobileBackTarget QmMobileBackTarget(bool PopupOpen, bool ChatActive, bool ScoreboardActive, bool SpectatorActive, bool MenuActive, bool QuickSecond)
{
	if(PopupOpen)
		return EQmMobileBackTarget::POPUP;
	if(ChatActive)
		return EQmMobileBackTarget::CHAT;
	if(ScoreboardActive)
		return EQmMobileBackTarget::SCOREBOARD;
	if(SpectatorActive)
		return EQmMobileBackTarget::SPECTATOR;
	if(MenuActive)
		return QuickSecond ? EQmMobileBackTarget::QUIT : EQmMobileBackTarget::MENU_CLOSE;
	return QuickSecond ? EQmMobileBackTarget::QUIT : EQmMobileBackTarget::MENU_OPEN;
}

// 两次返回算“连续两次”的窗口：太短会误触，太长会盖住正常返回节奏。
constexpr int QM_MOBILE_BACK_QUIT_WINDOW_MS = 800;

#endif
