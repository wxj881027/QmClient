#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_COLLISION_POSITION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_COLLISION_POSITION_H

#include <base/vmath.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

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
	inline std::optional<vec2> OffsetSnapshotPosition(ivec2 Position, ivec2 Offset)
	{
		// 显示采样先用宽整数加偏移，再转float，保留原整数量化顺序。
		const int64_t X = static_cast<int64_t>(Position.x) + Offset.x;
		const int64_t Y = static_cast<int64_t>(Position.y) + Offset.y;
		if(X < std::numeric_limits<int>::min() || X > std::numeric_limits<int>::max() ||
			Y < std::numeric_limits<int>::min() || Y > std::numeric_limits<int>::max())
			return std::nullopt;
		const vec2 Sample(static_cast<float>(X), static_cast<float>(Y));
		return CanRoundPosition(Sample) ? std::optional<vec2>(Sample) : std::nullopt;
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
