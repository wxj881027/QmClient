#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_WATER_HAMMER_INDICATOR_LOGIC_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_WATER_HAMMER_INDICATOR_LOGIC_H

#include "collision_position.h"

#include <generated/protocol.h>

#include <game/collision.h>
#include <game/gamecore.h>
#include <game/mapitems.h>

struct SQmWaterHammerInputState
{
	bool m_Received = false;
	int m_WantedWeapon = 0;
	int m_Fire = 0;
};

// 水域卡锤标记的纯判定逻辑，独立于客户端组件，便于单元测试。
inline bool QmIsWaterHammerPenaltyTile(const int Tile)
{
	return Tile == TILE_DEATH || Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE;
}

template<typename F>
inline bool QmSampleWaterHammerPenaltyArea(vec2 Position, F &&IsPenaltyAt)
{
	if(!QmCollision::CanRoundPosition(Position))
		return false;
	const float Radius = CCharacterCore::PhysicalSize() / 3.0f;
	const vec2 aSamples[] = {{0, 0}, {-Radius, -Radius}, {Radius, -Radius}, {-Radius, Radius}, {Radius, Radius}};
	for(const vec2 Offset : aSamples)
	{
		const vec2 Sample = Position + Offset;
		if(!QmCollision::CanRoundPosition(Sample))
			return false;
		if(IsPenaltyAt(Sample))
			return true;
	}
	return false;
}

inline bool QmIsWaterHammerPenaltyArea(const CCollision &Collision, vec2 Position)
{
	return QmSampleWaterHammerPenaltyArea(Position, [&Collision](vec2 Sample) {
		const int MapIndex = Collision.GetPureMapIndex(Sample);
		return QmIsWaterHammerPenaltyTile(Collision.GetTileIndex(MapIndex)) ||
		       QmIsWaterHammerPenaltyTile(Collision.GetFrontTileIndex(MapIndex)) ||
		       QmIsWaterHammerPenaltyTile(Collision.GetSwitchType(MapIndex));
	});
}

// 按角色实际冻结状态判断，避免仅接触水域边缘时提前高亮。
inline bool QmShouldMarkWaterHammer(const int FreezeEnd, const bool LiveFrozen, const bool HammerRequested, const bool FireHeld)
{
	const bool Frozen = FreezeEnd != 0 || LiveFrozen;
	return Frozen && HammerRequested && FireHeld;
}

inline bool QmShouldMarkWaterHammer(const int FreezeEnd, const bool LiveFrozen, const int CurrentWeapon, const SQmWaterHammerInputState &PreInput, const CNetObj_PlayerInput *pLocalInput)
{
	if(!pLocalInput && !PreInput.m_Received)
		return false;

	// 本地保留的分身输入优先，避免依赖切换角色后未重新广播的预输入。
	const int WantedWeapon = pLocalInput ? pLocalInput->m_WantedWeapon : PreInput.m_WantedWeapon;
	const int Fire = pLocalInput ? pLocalInput->m_Fire : PreInput.m_Fire;
	const bool HammerRequested = WantedWeapon == WEAPON_HAMMER + 1 || CurrentWeapon == WEAPON_HAMMER;
	return QmShouldMarkWaterHammer(FreezeEnd, LiveFrozen, HammerRequested, (Fire & 1) != 0);
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_WATER_HAMMER_INDICATOR_LOGIC_H
