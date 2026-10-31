/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#ifndef GAME_CLIENT_PREDICTION_ENTITIES_DOOR_H
#define GAME_CLIENT_PREDICTION_ENTITIES_DOOR_H

#include <game/client/prediction/entity.h>

#include <optional>

class CLaserData;

class CDoor : public CEntity
{
	vec2 m_To;
	vec2 m_Direction;
	int m_Length;
	bool m_Active;

	static int MapSide(vec2 Direction);
	int MapLength(int Side);

public:
	struct SCollisionGeometry
	{
		vec2 m_Direction{0.0f, 0.0f};
		int m_Length = 0;
	};
	// 仅准备数值合法的写入几何，不执行逐像素遍历或证明帧预算。
	static std::optional<SCollisionGeometry> CalcCollisionGeometry(vec2 Pos, vec2 To, int Side, int ConfiguredLength, ivec2 MapSize);

	CDoor(CGameWorld *pGameWorld, int Id, const CLaserData *pData);
	void ResetCollision();
	bool Match(const CDoor *pDoor) const;
	void Read(const CLaserData *pData);

	void Destroy() override;
};

#endif // GAME_CLIENT_PREDICTION_ENTITIES_DOOR_H
