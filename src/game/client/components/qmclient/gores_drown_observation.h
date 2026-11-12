#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_GORES_DROWN_OBSERVATION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_GORES_DROWN_OBSERVATION_H

#include "collision_position.h"
#include "gores_drown_tracker.h"

#include <game/collision.h>
#include <game/mapitems.h>

#include <string_view>

inline bool QmObserveGoresDrownPlayer(CQmGoresDrownTracker &Tracker, const CCollision &Collision, int ClientId, bool Active, std::string_view Name, std::string_view Clan, int Team, bool HasCharacter, bool Frozen, bool Hooking, vec2 Position, bool IncludeTeamZero)
{
	bool OnStart = false;
	if(Active && HasCharacter && CQmGoresDrownTracker::IsTrackedTeam(Team, IncludeTeamZero))
	{
		// 坏坐标不能进入碰撞取整，也不能被当成离开起点或新的冻结/出钩事件。
		if(!QmCollision::CanRoundPosition(Position))
			return false;
		const int Index = Collision.GetPureMapIndex(Position);
		OnStart = Collision.GetTileIndex(Index) == TILE_START || Collision.GetFrontTileIndex(Index) == TILE_START;
	}
	Tracker.Observe(ClientId, Active, Name, Clan, Team, HasCharacter, Frozen, Hooking, OnStart, IncludeTeamZero);
	return true;
}

#endif
