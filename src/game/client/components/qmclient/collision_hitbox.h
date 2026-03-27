#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_COLLISION_HITBOX_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_COLLISION_HITBOX_H

#include <game/client/component.h>

class CTile;
class CTeleTile;

// 碰撞体积类型枚举
enum ECollisionHitboxType
{
	HITBOX_NONE = 0,
	HITBOX_SOLID,
	HITBOX_FREEZE,
	HITBOX_DFREEZE,
	HITBOX_DEATH,
};

class CCollisionHitbox : public CComponent
{
private:
	ivec2 m_MapDataSize;
	int *m_pMapData = nullptr;

	void RenderTileHitboxes();
	void RenderTeeHitboxes();
	void RenderPickupHitboxes();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnMapLoad() override;
	void OnRender() override;
	~CCollisionHitbox() override { delete[] m_pMapData; }
};

#endif
