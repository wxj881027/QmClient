/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#ifndef GAME_CLIENT_PREDICTION_ENTITIES_PLASMA_H
#define GAME_CLIENT_PREDICTION_ENTITIES_PLASMA_H

#include <game/client/laser_data.h>
#include <game/client/prediction/entity.h>

class CPlasma : public CEntity
{
	vec2 m_Core;
	bool m_Freeze;
	bool m_Explosive;
	int m_ForClientId;
	int m_EvalTick;
	int m_LifeTime;

	void Move();
	bool HitCharacter(CCharacter *pTarget);
	bool HitObstacle(CCharacter *pTarget);

public:
	CPlasma(CGameWorld *pGameWorld, int Id, const CLaserData *pData);

	bool Match(const CPlasma *pPlasma) const;
	void Read(const CLaserData *pData);
	void ShiftEvalTick(int TickDelta) { m_EvalTick += TickDelta; }
	// 分支机关炮的等离子只存在于预测世界；渲染层沿用服务器等离子同一种
	// LASERTYPE_PLASMA 数据，避免“实际冻结/爆炸，画面没有弹体”。
	CLaserData RenderData();

	void Reset();
	void Tick() override;
};

#endif // GAME_CLIENT_PREDICTION_ENTITIES_PLASMA_H
