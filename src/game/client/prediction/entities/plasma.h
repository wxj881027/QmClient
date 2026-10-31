/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#ifndef GAME_CLIENT_PREDICTION_ENTITIES_PLASMA_H
#define GAME_CLIENT_PREDICTION_ENTITIES_PLASMA_H

#include <game/client/components/qmclient/collision_position.h>
#include <game/client/prediction/entity.h>

class CLaserData;

class CPlasma : public CEntity
{
	vec2 m_Core;
	bool m_Freeze;
	bool m_Explosive;
	int m_ForClientId;
	int m_EvalTick;
	int m_LifeTime;

	bool HitCharacter(CCharacter *pTarget);
	bool HitObstacle(CCharacter *pTarget);

public:
	// 下一位置与下一次查询线段均准备完整后再提交，失败不改变当前位置和速度。
	static bool TryAdvance(vec2 &Position, vec2 &Velocity)
	{
		const vec2 NextPosition = Position + Velocity;
		const vec2 NextVelocity = Velocity * 1.1f;
		if(!QmCollision::CanRoundPosition(Position) || !QmCollision::CanRoundPosition(NextPosition) || !QmCollision::CanRoundPosition(NextPosition + NextVelocity))
			return false;
		Position = NextPosition;
		Velocity = NextVelocity;
		return true;
	}

	CPlasma(CGameWorld *pGameWorld, int Id, const CLaserData *pData);

	bool Match(const CPlasma *pPlasma) const;
	void Read(const CLaserData *pData);
	void ShiftEvalTick(int TickDelta) { m_EvalTick += TickDelta; }

	void Reset();
	void Tick() override;
};

#endif // GAME_CLIENT_PREDICTION_ENTITIES_PLASMA_H
