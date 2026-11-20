#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_WEAPON_TRAJECTORY_GEOMETRY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_WEAPON_TRAJECTORY_GEOMETRY_H

#include "collision_position.h"

#include <engine/graphics.h>

#include <game/collision.h>
#include <game/gamecore.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

// 只约束显示采样的取整数值域，不改变共享碰撞实现或添加遍历预算。
inline bool QmWeaponTrajectoryCanTestBox(vec2 Position)
{
	const vec2 HalfSize = CCharacterCore::PhysicalSizeVec2() * 0.5f;
	return QmCollision::CanRoundPosition(Position - HalfSize) &&
	       QmCollision::CanRoundPosition(Position + HalfSize) &&
	       QmCollision::CanRoundPosition(Position + vec2(HalfSize.x, -HalfSize.y)) &&
	       QmCollision::CanRoundPosition(Position + vec2(-HalfSize.x, HalfSize.y));
}

inline bool QmTryPredictNinjaEndPosition(CCollision &Collision, vec2 Position, vec2 Direction, int MovementTicks, float Speed, vec2 Elasticity, vec2 &OutPosition) noexcept
{
	const float DirectionLength = length(Direction);
	if(!QmCollision::CanRoundPosition(Position) || !std::isfinite(DirectionLength) || DirectionLength < 0.0001f || MovementTicks < 0)
		return false;
	Direction = normalize(Direction);
	try
	{
		for(int i = 0; i < MovementTicks; ++i)
		{
			vec2 Velocity = Direction * Speed;
			const float Distance = length(Velocity);
			const vec2 Reach(std::abs(Velocity.x), std::abs(Velocity.y));
			// MoveBox会取整速度长度并探测盒角；先检查本步移动包络。
			if(!std::isfinite(Distance) || static_cast<double>(Distance) >= std::numeric_limits<int>::max() ||
				!QmWeaponTrajectoryCanTestBox(Position - Reach) || !QmWeaponTrajectoryCanTestBox(Position + Reach))
				return false;
			Collision.MoveBox(&Position, &Velocity, CCharacterCore::PhysicalSizeVec2(), Elasticity);
			if(!QmCollision::CanRoundPosition(Position))
				return false;
			const int MapIndex = Collision.GetMapIndex(Position);
			const int Teleport = Collision.IsTeleport(MapIndex);
			if(Teleport > 0 && !Collision.TeleOuts(Teleport - 1).empty())
				Position = Collision.TeleOuts(Teleport - 1)[0];
			if(!QmCollision::CanRoundPosition(Position))
				return false;
		}
		// 完整模拟后才发布；失败不留下中途位置供幽灵绘制。
		OutPosition = Position;
		return true;
	}
	catch(...)
	{
		// TeleOuts在缺少出口条目时也可能分配，失败不发布中途位置。
		return false;
	}
}

struct SQmWeaponTrajectoryScratch
{
	std::vector<vec2> m_vPoints;
	std::vector<IGraphics::CLineItem> m_vLineSegments;
	std::vector<IGraphics::CFreeformItem> m_vLineQuadSegments;

	void Reset() noexcept
	{
		m_vPoints.clear();
		m_vLineSegments.clear();
		m_vLineQuadSegments.clear();
	}

	template<typename F>
	[[nodiscard]] bool TryPrepare(F &&Prepare) noexcept
	{
		Reset();
		try
		{
			if(Prepare())
				return true;
		}
		catch(...)
		{
			// 可选轨迹准备失败时撤销前缀，后续玩家或帧可重新准备。
		}
		Reset();
		return false;
	}

	bool ReserveBounces(float BounceNum)
	{
		const float Count = BounceNum + 2.0f;
		if(!std::isfinite(Count) || static_cast<double>(Count) >= static_cast<double>(std::numeric_limits<std::size_t>::max()))
			return false;
		// 负反弹上限仍按原模拟在首撞后停止，不能转成巨大的无符号容量。
		if(Count > 0.0f)
			m_vLineSegments.reserve(static_cast<std::size_t>(Count));
		return true;
	}

	bool PrepareQuads(float HalfWidth)
	{
		m_vLineQuadSegments.clear();
		m_vLineQuadSegments.reserve(m_vLineSegments.size());
		for(const auto &Line : m_vLineSegments)
		{
			const vec2 From(Line.m_X0, Line.m_Y0);
			const vec2 To(Line.m_X1, Line.m_Y1);
			const vec2 Delta = To - From;
			const float Distance = length(Delta);
			if(!std::isfinite(Distance))
				return false;
			if(Distance < 0.0001f)
				continue;
			const vec2 Perp = normalize(vec2(-Delta.y, Delta.x));
			m_vLineQuadSegments.emplace_back(To + Perp * -HalfWidth, To + Perp * HalfWidth, From + Perp * -HalfWidth, From + Perp * HalfWidth);
		}
		return !m_vLineQuadSegments.empty();
	}
};

#endif
