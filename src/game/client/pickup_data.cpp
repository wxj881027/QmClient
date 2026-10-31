/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "pickup_data.h"

#include <engine/shared/snapshot.h>

#include <generated/protocol.h>

#include <game/client/components/qmclient/collision_position.h>
#include <game/collision.h>

bool CPickupData::HasValidPosition() const
{
	return QmCollision::CanRoundPosition(m_Pos);
}

bool CPickupData::TryFillNetObj(CNetObj_Pickup &Out) const
{
	if(!HasValidPosition())
		return false;
	Out.m_X = static_cast<int>(m_Pos.x);
	Out.m_Y = static_cast<int>(m_Pos.y);
	Out.m_Type = m_Type;
	Out.m_Subtype = m_Subtype;
	return true;
}

CPickupData ExtractPickupInfo(int NetObjType, const void *pData, const CNetObj_EntityEx *pEntEx)
{
	if(NetObjType == NETOBJTYPE_DDNETPICKUP)
	{
		return ExtractPickupInfoDDNet((CNetObj_DDNetPickup *)pData);
	}

	CNetObj_Pickup *pPickup = (CNetObj_Pickup *)pData;

	CPickupData Result;

	Result.m_Pos.x = pPickup->m_X;
	Result.m_Pos.y = pPickup->m_Y;
	Result.m_Type = pPickup->m_Type;
	Result.m_Subtype = pPickup->m_Subtype;
	Result.m_SwitchNumber = pEntEx ? pEntEx->m_SwitchNumber : 0;

	return Result;
}

CPickupData ExtractPickupInfoDDNet(const CNetObj_DDNetPickup *pPickup)
{
	CPickupData Result;

	Result.m_Pos.x = pPickup->m_X;
	Result.m_Pos.y = pPickup->m_Y;
	Result.m_Type = pPickup->m_Type;
	Result.m_Subtype = pPickup->m_Subtype;
	Result.m_SwitchNumber = pPickup->m_SwitchNumber;
	Result.m_Flags = pPickup->m_Flags;

	return Result;
}
