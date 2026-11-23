#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_INPUT_STATE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_INPUT_STATE_H

#include <generated/protocol.h>

// 输入位图与按键边沿的纯判定。
//
// 服务器只把开火计数器的最低位当作「本 tick 是否按下」；按住键产生的
// 重复按下事件不能再加一，松开事件必须改变奇偶性。这里集中这两条规则，
// 供控制台按键计数、分身自动上锤与练习输入锁共用，避免各处各写一份
// 奇偶运算。

// 一次按键事件之后的输入计数器。PressedState 为 0 表示松开，1 表示按下；
// 只在奇偶性与事件不符时递增，最后截断到输入位图宽度。
inline int QmInputCounterAfterEvent(int CurrentState, int PressedState)
{
	if((CurrentState & 1) != PressedState)
		CurrentState++;
	return CurrentState & INPUT_STATE_MASK;
}

// 生成「本 tick 按下」的计数器：保证为奇数并与旧值不同。
inline int QmPressedFireState(int FireState)
{
	return (FireState + 1) | 1;
}

// 生成「本 tick 松开」的计数器：保证为偶数；原本已松开时保持不变。
inline int QmReleasedFireState(int FireState)
{
	return (FireState + 1) & ~1;
}

#endif
