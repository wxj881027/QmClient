/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "entity.h"

#include <game/collision.h>

//////////////////////////////////////////////////
// Entity
//////////////////////////////////////////////////
CEntity::CEntity(CGameWorld *pGameWorld, int ObjType, vec2 Pos, int ProximityRadius)
{
	m_pGameWorld = pGameWorld;

	m_ObjType = ObjType;
	m_Pos = Pos;
	m_ProximityRadius = ProximityRadius;

	m_MarkedForDestroy = false;
	m_Id = -1;

	m_pPrevTypeEntity = nullptr;
	m_pNextTypeEntity = nullptr;
	m_SnapTicks = -1;

	// DDRace
	m_pParent = nullptr;
	m_pChild = nullptr;
	m_DestroyTick = -1;
	m_LastRenderTick = -1;
}

CEntity::CEntity(const CEntity &Other) :
	m_pPrevTypeEntity(nullptr),
	m_pNextTypeEntity(nullptr),
	m_pGameWorld(Other.m_pGameWorld),
	m_MarkedForDestroy(Other.m_MarkedForDestroy),
	m_Id(Other.m_Id),
	m_ObjType(Other.m_ObjType),
	m_ProximityRadius(Other.m_ProximityRadius),
	m_Pos(Other.m_Pos),
	m_Number(Other.m_Number),
	m_Layer(Other.m_Layer),
	m_SnapTicks(Other.m_SnapTicks),
	m_DestroyTick(Other.m_DestroyTick),
	m_LastRenderTick(Other.m_LastRenderTick),
	m_pParent(nullptr),
	m_pChild(nullptr)
{
}

CEntity::~CEntity()
{
	if(GameWorld())
		GameWorld()->RemoveEntity(this);
}

bool CEntity::GameLayerClipped(vec2 CheckPos)
{
	return round_to_int(CheckPos.x) / 32 < -200 || round_to_int(CheckPos.x) / 32 > Collision()->GetWidth() + 200 ||
	       round_to_int(CheckPos.y) / 32 < -200 || round_to_int(CheckPos.y) / 32 > Collision()->GetHeight() + 200;
}
