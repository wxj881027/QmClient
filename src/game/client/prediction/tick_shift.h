#ifndef GAME_CLIENT_PREDICTION_TICK_SHIFT_H
#define GAME_CLIENT_PREDICTION_TICK_SHIFT_H

#include <cstdint>
#include <initializer_list>
#include <limits>

namespace QmTickShift
{
	enum class EMode
	{
		CHECK,
		COMMIT,
	};

	inline bool CanShift(int Tick, int64_t Delta)
	{
		return Delta >= static_cast<int64_t>(std::numeric_limits<int>::min()) - Tick &&
		       Delta <= static_cast<int64_t>(std::numeric_limits<int>::max()) - Tick;
	}

	// 字段来自同一对象的不同时间戳；空指针表示该字段当前是无需平移的哨兵。
	[[nodiscard]] inline bool TryShiftFields(int64_t Delta, EMode Mode, std::initializer_list<int *> Fields)
	{
		for(const int *pTick : Fields)
			if(pTick && !CanShift(*pTick, Delta))
				return false;
		if(Mode == EMode::COMMIT)
			for(int *pTick : Fields)
				if(pTick)
					*pTick = static_cast<int>(static_cast<int64_t>(*pTick) + Delta);
		return true;
	}
}

#endif
