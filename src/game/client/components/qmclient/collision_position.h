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
	inline bool CanQueryHookLine(vec2 From, vec2 To)
	{
		if(!CanQueryLine(From, To))
			return false;
		// 钩子碰撞的 ThroughOffset 还会对取整坐标加减32，需给该整数运算留余量。
		const auto HasOffsetMargin = [](vec2 Pos) {
			const auto Fits = [](float Value) {
				const float Rounded = Value > 0.0f ? Value + 0.5f : Value - 0.5f;
				return static_cast<double>(Rounded) >= static_cast<double>(std::numeric_limits<int>::min()) + 32 &&
				       static_cast<double>(Rounded) <= static_cast<double>(std::numeric_limits<int>::max()) - 32;
			};
			return Fits(Pos.x) && Fits(Pos.y);
		};
		return HasOffsetMargin(From) && HasOffsetMargin(To) && HasOffsetMargin(mix(From, To, 1.0f));
	}
}

#endif
