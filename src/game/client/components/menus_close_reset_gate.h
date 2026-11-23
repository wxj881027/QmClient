#ifndef GAME_CLIENT_COMPONENTS_MENUS_CLOSE_RESET_GATE_H
#define GAME_CLIENT_COMPONENTS_MENUS_CLOSE_RESET_GATE_H

// 菜单收起时输入会在开始收起后立即交回游戏，动画只是视觉收尾。
// 关闭过程只允许重置一次游戏输入：动画收放在开始收起时重置，
// 立即收放在真正关闭时重置；收尾不能清掉动画期间新按下的展示面板。
class CMenuCloseResetGate
{
	bool m_ResetAtFinish = true;

public:
	template<typename FReset>
	void Begin(bool Online, bool Animated, FReset &&Reset)
	{
		m_ResetAtFinish = Online && !Animated;
		if(Online && Animated)
			Reset();
	}

	template<typename FReset>
	void Finish(bool Online, FReset &&Reset)
	{
		if(Online && m_ResetAtFinish)
			Reset();
		m_ResetAtFinish = true;
	}

	// 收起中途折返时取消待收尾的重置，下一次关闭重新按实际模式安排。
	void Cancel()
	{
		m_ResetAtFinish = true;
	}
};

#endif
