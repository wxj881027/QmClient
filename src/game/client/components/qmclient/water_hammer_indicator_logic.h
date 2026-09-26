#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_WATER_HAMMER_INDICATOR_LOGIC_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_WATER_HAMMER_INDICATOR_LOGIC_H

#include <game/mapitems.h>

// 水域卡锤标记的纯判定逻辑，独立于客户端组件，便于单元测试。
inline bool QmIsWaterHammerPenaltyTile(const int Tile)
{
	return Tile == TILE_DEATH || Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE;
}

inline bool QmShouldMarkWaterHammer(const bool InPenaltyArea, const bool HammerRequested, const bool FireHeld)
{
	return InPenaltyArea && HammerRequested && FireHeld;
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_WATER_HAMMER_INDICATOR_LOGIC_H
