#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_TICK_SHIFT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_TICK_SHIFT_H

#include <cstdint>
#include <span>

class CGameWorld;

namespace QmPractice
{
	// 暂停时只向前平移；世界及其独立攻击历史全部可表示时才一起提交。
	[[nodiscard]] bool TryShiftTicks(CGameWorld &World, int64_t Delta, std::span<int> LastAttackTicks);
}

#endif
