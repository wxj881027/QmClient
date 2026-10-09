// 冻结投射物与普通轨迹、爆炸范围的独立显示策略。
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_COLLISION_HITBOX_PROJECTILE_LOGIC_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_COLLISION_HITBOX_PROJECTILE_LOGIC_H

#include <generated/protocol.h>

#include <game/client/projectile_data.h>
#include <game/gamecore.h>

struct SQmProjectileHitboxDisplay
{
	bool m_ShowTrail;
	bool m_ShowExplosion;
	float m_FreezeRadius;
};

inline SQmProjectileHitboxDisplay ResolveQmProjectileHitboxDisplay(const CProjectileData &Data, bool ShowProjectiles, bool ShowFreezeProjectiles, bool FreezeActive)
{
	// 冻结子弹以 1 单位半径查找角色；圆圈显示角色中心进入后会被冻结的范围。
	const float FreezeRadius = ShowFreezeProjectiles && Data.m_Freeze && FreezeActive ? CCharacterCore::PhysicalSize() + 1.0f : 0.0f;
	return {ShowProjectiles, ShowProjectiles && (Data.m_Type == WEAPON_GRENADE || Data.m_Explosive), FreezeRadius};
}

#endif
