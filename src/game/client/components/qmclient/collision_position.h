#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_COLLISION_POSITION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_COLLISION_POSITION_H

#include <base/vmath.h>

#include <cmath>
#include <limits>

namespace QmCollision
{
	inline bool CanRoundPosition(vec2 Pos)
	{
		const auto CanRound = [](float Value) {
			// 与碰撞入口的round_to_int保持相同浮点舍入，转换前检查真实int边界。
			const float Rounded = Value > 0.0f ? Value + 0.5f : Value - 0.5f;
			return std::isfinite(Rounded) &&
			       static_cast<double>(Rounded) >= std::numeric_limits<int>::min() &&
			       static_cast<double>(Rounded) <= std::numeric_limits<int>::max();
		};
		return CanRound(Pos.x) && CanRound(Pos.y);
	}
}

#endif
