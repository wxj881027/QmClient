/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#ifndef GAME_CLIENT_PREDICTION_ENTITIES_DOOR_H
#define GAME_CLIENT_PREDICTION_ENTITIES_DOOR_H

#include <game/client/prediction/entity.h>

class CLaserData;

class CDoor : public CEntity
{
	vec2 m_To;
	vec2 m_Direction;
	int m_Length;
	bool m_Active;

	int MapSide() const;
	int MapLength(int Side);

public:
	CDoor(CGameWorld *pGameWorld, int Id, const CLaserData *pData);
	// 副本只复制几何，不接管共享 CCollision 上的门碰撞端点：世界树里
	// 同一扇门同时存在于多个 CGameWorld 副本，只有权威世界负责销毁清理。
	CDoor(const CDoor &Other);
	void ResetCollision();
	bool Match(const CDoor *pDoor) const;
	void Read(const CLaserData *pData);

	void Destroy() override;
};

#endif // GAME_CLIENT_PREDICTION_ENTITIES_DOOR_H
