#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_WATER_HAMMER_INDICATOR_LOGIC_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_WATER_HAMMER_INDICATOR_LOGIC_H

#include <game/mapitems.h>

// 水域卡锤标记的纯判定逻辑，独立于客户端组件，便于单元测试。
inline bool QmIsWaterHammerPenaltyTile(const int Tile)
{
	return Tile == TILE_DEATH || Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE;
}

// 按角色实际冻结状态判断，避免仅接触水域边缘时提前高亮。
inline bool QmShouldMarkWaterHammer(const int FreezeEnd, const bool LiveFrozen, const bool HammerRequested, const bool FireHeld)
{
	const bool Frozen = FreezeEnd != 0 || LiveFrozen;
	return Frozen && HammerRequested && FireHeld;
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_WATER_HAMMER_INDICATOR_LOGIC_H
