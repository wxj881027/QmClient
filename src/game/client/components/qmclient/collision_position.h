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
	inline bool CanQueryLine(vec2 From, vec2 To)
	{
		// 仅约束端点、实际 mix 终点与 int 遍历计数；不代表该步数已满足帧预算。
		if(!CanRoundPosition(From) || !CanRoundPosition(To) || !CanRoundPosition(mix(From, To, 1.0f)))
			return false;
		const float Steps = distance(From, To) + 1.0f;
		return std::isfinite(Steps) && static_cast<double>(Steps) < std::numeric_limits<int>::max();
	}

}

#endif
