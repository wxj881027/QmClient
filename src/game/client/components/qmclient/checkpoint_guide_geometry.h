#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHECKPOINT_GUIDE_GEOMETRY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHECKPOINT_GUIDE_GEOMETRY_H

#include <base/vmath.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace QmCheckpointGuide
{
	inline constexpr float MOVE_RESTRICTION_DISTANCE = 18.0f;

	// 与现有 round_to_int 的浮点加减顺序一致，检查通过后才允许进入碰撞转换。
	inline bool IsSafeCollisionCoordinate(float Value)
	{
		if(!std::isfinite(Value))
			return false;
		const float Rounded = Value > 0.0f ? Value + 0.5f : Value - 0.5f;
		return std::isfinite(Rounded) && static_cast<double>(Rounded) >= std::numeric_limits<int>::min() && static_cast<double>(Rounded) <= std::numeric_limits<int>::max();
	}

	inline bool IsSafeCollisionPosition(vec2 Position, float ProbeDistance = 0.0f)
	{
		if(!std::isfinite(ProbeDistance) || ProbeDistance < 0.0f || ProbeDistance > 32.0f)
			return false;
		return IsSafeCollisionCoordinate(Position.x) && IsSafeCollisionCoordinate(Position.y) &&
		       IsSafeCollisionCoordinate(Position.x + ProbeDistance) && IsSafeCollisionCoordinate(Position.x - ProbeDistance) &&
		       IsSafeCollisionCoordinate(Position.y + ProbeDistance) && IsSafeCollisionCoordinate(Position.y - ProbeDistance);
	}

	inline std::optional<vec2> TileCenter(int Width, int Height, int Index)
	{
		const int64_t Size = static_cast<int64_t>(Width) * Height;
		if(Width <= 0 || Height <= 0 || Size > std::numeric_limits<int>::max() || Index < 0 || Index >= Size)
			return std::nullopt;
		// 格子总数能用 int 表示，不代表格子中心乘 32 后仍能表示。
		const int64_t X = static_cast<int64_t>(Index % Width) * 32 + 16;
		const int64_t Y = static_cast<int64_t>(Index / Width) * 32 + 16;
		const vec2 Center(static_cast<float>(X), static_cast<float>(Y));
		return IsSafeCollisionPosition(Center, MOVE_RESTRICTION_DISTANCE) ? std::make_optional(Center) : std::nullopt;
	}
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_CHECKPOINT_GUIDE_GEOMETRY_H
