#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_RACE_START_DETECTION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_RACE_START_DETECTION_H

#include "collision_position.h"

#include <game/collision.h>
#include <game/mapitems.h>

#include <new>
#include <stdexcept>
#include <vector>

namespace qm_race_start
{

	inline bool IsTileStart(const CCollision &Collision, vec2 Prev, vec2 Pos)
	{
		// 只在客户端起跑检测拒绝不可转换的线段；正常段沿用原扫描和空结果回退。
		if(!QmCollision::CanQueryLine(Prev, Pos))
			return false;
		try
		{
			const std::vector<int> vIndices = Collision.GetMapIndices(Prev, Pos);
			if(!vIndices.empty())
			{
				for(const int Index : vIndices)
				{
					if(Collision.GetTileIndex(Index) == TILE_START)
						return true;
					if(Collision.GetFrontTileIndex(Index) == TILE_START)
						return true;
				}
			}
			else
			{
				const int Index = Collision.GetPureMapIndex(Pos);
				if(Collision.GetTileIndex(Index) == TILE_START)
					return true;
				if(Collision.GetFrontTileIndex(Index) == TILE_START)
					return true;
			}
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
		return false;
	}

}

#endif
