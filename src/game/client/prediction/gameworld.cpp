/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "gameworld.h"

#include "copy_links.h"
#include "entities/character.h"
#include "entities/door.h"
#include "entities/dragger.h"
#include "entities/laser.h"
#include "entities/pickup.h"
#include "entities/plasma.h"
#include "entities/projectile.h"
#include "entity.h"

#include <engine/shared/config.h>

#include <game/client/laser_data.h>
#include <game/client/pickup_data.h>
#include <game/client/projectile_data.h>
#include <game/collision.h>
#include <game/mapbugs.h>
#include <game/mapitems.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

//////////////////////////////////////////////////
// game world
//////////////////////////////////////////////////
CGameWorld::CGameWorld()
{
	for(auto &pFirstEntityType : m_apFirstEntityTypes)
		pFirstEntityType = nullptr;
	for(auto &pCharacter : m_apCharacters)
		pCharacter = nullptr;
	m_pCollision = nullptr;
	m_GameTick = 0;
	m_pParent = nullptr;
	m_pChild = nullptr;
	m_IsValidCopy = false;
}

CGameWorld::~CGameWorld()
{
	Clear();
	OnModified();
	QmCopyLinks::Unlink(*this);
}

void CGameWorld::Init(CCollision *pCollision, CTuningParams *pTuningList, const CMapBugs *pMapBugs)
{
	m_pCollision = pCollision;
	m_pTuningList = pTuningList;
	m_pMapBugs = pMapBugs;
}

CEntity *CGameWorld::FindFirst(int Type)
{
	return Type < 0 || Type >= NUM_ENTTYPES ? nullptr : m_apFirstEntityTypes[Type];
}

CEntity *CGameWorld::FindLast(int Type)
{
	CEntity *pLast = FindFirst(Type);
	if(pLast)
		while(pLast->TypeNext())
			pLast = pLast->TypeNext();
	return pLast;
}

int CGameWorld::FindEntities(vec2 Pos, float Radius, CEntity **ppEnts, int Max, int Type)
{
	if(Type < 0 || Type >= NUM_ENTTYPES)
		return 0;

	int Num = 0;
	for(CEntity *pEnt = m_apFirstEntityTypes[Type]; pEnt; pEnt = pEnt->m_pNextTypeEntity)
	{
		if(distance(pEnt->m_Pos, Pos) < Radius + pEnt->m_ProximityRadius)
		{
			if(ppEnts)
				ppEnts[Num] = pEnt;
			Num++;
			if(Num == Max)
				break;
		}
	}

	return Num;
}

void CGameWorld::InsertEntity(CEntity *pEnt, bool Last)
{
	pEnt->m_pGameWorld = this;
	pEnt->m_pNextTypeEntity = nullptr;
	pEnt->m_pPrevTypeEntity = nullptr;

	// insert it
	if(!Last)
	{
		if(m_apFirstEntityTypes[pEnt->m_ObjType])
			m_apFirstEntityTypes[pEnt->m_ObjType]->m_pPrevTypeEntity = pEnt;
		pEnt->m_pNextTypeEntity = m_apFirstEntityTypes[pEnt->m_ObjType];
		pEnt->m_pPrevTypeEntity = nullptr;
		m_apFirstEntityTypes[pEnt->m_ObjType] = pEnt;
	}
	else
	{
		// insert it at the end of the list
		CEntity *pLast = m_apFirstEntityTypes[pEnt->m_ObjType];
		if(pLast)
		{
			while(pLast->m_pNextTypeEntity)
				pLast = pLast->m_pNextTypeEntity;
			pLast->m_pNextTypeEntity = pEnt;
		}
		else
		{
			m_apFirstEntityTypes[pEnt->m_ObjType] = pEnt;
		}
		pEnt->m_pPrevTypeEntity = pLast;
		pEnt->m_pNextTypeEntity = nullptr;
	}

	if(pEnt->m_ObjType == ENTTYPE_CHARACTER)
	{
		auto *pChar = (CCharacter *)pEnt;
		int Id = pChar->GetCid();
		if(Id >= 0 && Id < MAX_CLIENTS)
		{
			m_apCharacters[Id] = pChar;
			m_Core.m_apCharacters[Id] = &pChar->m_Core;
		}
		pChar->SetCoreWorld(this);
	}
}

void CGameWorld::RemoveEntity(CEntity *pEnt)
{
	// not in the list
	if(!pEnt->m_pNextTypeEntity && !pEnt->m_pPrevTypeEntity && m_apFirstEntityTypes[pEnt->m_ObjType] != pEnt)
		return;

	// remove
	if(pEnt->m_pPrevTypeEntity)
		pEnt->m_pPrevTypeEntity->m_pNextTypeEntity = pEnt->m_pNextTypeEntity;
	else
		m_apFirstEntityTypes[pEnt->m_ObjType] = pEnt->m_pNextTypeEntity;
	if(pEnt->m_pNextTypeEntity)
		pEnt->m_pNextTypeEntity->m_pPrevTypeEntity = pEnt->m_pPrevTypeEntity;

	// keep list traversing valid
	if(m_pNextTraverseEntity == pEnt)
		m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;

	pEnt->m_pNextTypeEntity = nullptr;
	pEnt->m_pPrevTypeEntity = nullptr;

	if(pEnt->m_pParent && pEnt->m_pParent->m_pChild == pEnt && m_IsValidCopy && m_pParent && m_pParent->m_pChild == this)
		pEnt->m_pParent->m_DestroyTick = GameTick();
	QmCopyLinks::Unlink(*pEnt);
}

void CGameWorld::RemoveCharacter(CCharacter *pChar)
{
	int Id = pChar->GetCid();
	if(Id >= 0 && Id < MAX_CLIENTS)
	{
		// 临时匹配对象和未登记副本也会析构，只解除仍属于该对象的索引。
		if(m_apCharacters[Id] == pChar)
			m_apCharacters[Id] = nullptr;
		if(m_Core.m_apCharacters[Id] == &pChar->m_Core)
			m_Core.m_apCharacters[Id] = nullptr;
	}
}

void CGameWorld::RemoveEntities()
{
	// destroy objects marked for destruction
	for(auto *pEnt : m_apFirstEntityTypes)
		for(; pEnt;)
		{
			m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
			if(pEnt->m_MarkedForDestroy)
			{
				pEnt->Destroy();
			}
			pEnt = m_pNextTraverseEntity;
		}
}

bool CGameWorld::TrySetSwitchState(int Number, int Team, int Type, int Delay)
{
	if(Number <= 0 || static_cast<size_t>(Number) >= Switchers().size() || Team < 0 || Team >= NUM_DDRACE_TEAMS)
		return false;
	const bool Timed = Type == TILE_SWITCHTIMEDOPEN || Type == TILE_SWITCHTIMEDCLOSE;
	if(!Timed && Type != TILE_SWITCHOPEN && Type != TILE_SWITCHCLOSE)
		return false;
	int EndTick = 0;
	if(Timed)
	{
		if(Delay < 0)
			return false;
		const int64_t Deadline = static_cast<int64_t>(GameTick()) + 1 + static_cast<int64_t>(Delay) * GameTickSpeed();
		if(Deadline < std::numeric_limits<int>::min() || Deadline > std::numeric_limits<int>::max())
			return false;
		EndTick = static_cast<int>(Deadline);
	}
	// 完整期限与索引检查后才发布，失败不留下仅改变开闭状态的半次更新。
	auto &Switcher = Switchers()[Number];
	Switcher.m_aStatus[Team] = Type == TILE_SWITCHOPEN || Type == TILE_SWITCHTIMEDOPEN;
	Switcher.m_aEndTick[Team] = EndTick;
	Switcher.m_aType[Team] = Type;
	Switcher.m_aLastUpdateTick[Team] = GameTick();
	return true;
}

void CGameWorld::Tick()
{
	// update all objects
	for(int i = 0; i < NUM_ENTTYPES; i++)
	{
		// It's important to call PreTick() and Tick() after each other.
		// If we call PreTick() before, and Tick() after other entities have been processed, it causes physics changes such as a stronger shotgun or grenade.
		if(m_WorldConfig.m_NoWeakHookAndBounce && i == ENTTYPE_CHARACTER)
		{
			auto *pEnt = m_apFirstEntityTypes[i];
			for(; pEnt;)
			{
				m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
				((CCharacter *)pEnt)->PreTick();
				pEnt = m_pNextTraverseEntity;
			}
		}

		auto *pEnt = m_apFirstEntityTypes[i];
		for(; pEnt;)
		{
			m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
			pEnt->Tick();
			pEnt = m_pNextTraverseEntity;
		}
	}

	for(auto *pEnt : m_apFirstEntityTypes)
		for(; pEnt;)
		{
			m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
			pEnt->TickDeferred();
			pEnt->m_SnapTicks++;
			pEnt = m_pNextTraverseEntity;
		}

	RemoveEntities();

	// update switch state
	for(auto &Switcher : Switchers())
	{
		for(int j = 0; j < NUM_DDRACE_TEAMS; ++j)
		{
			if(Switcher.m_aEndTick[j] <= GameTick() && Switcher.m_aType[j] == TILE_SWITCHTIMEDOPEN)
			{
				Switcher.m_aStatus[j] = false;
				Switcher.m_aEndTick[j] = 0;
				Switcher.m_aType[j] = TILE_SWITCHCLOSE;
			}
			else if(Switcher.m_aEndTick[j] <= GameTick() && Switcher.m_aType[j] == TILE_SWITCHTIMEDCLOSE)
			{
				Switcher.m_aStatus[j] = true;
				Switcher.m_aEndTick[j] = 0;
				Switcher.m_aType[j] = TILE_SWITCHOPEN;
			}
		}
	}

	OnModified();
}

CCharacter *CGameWorld::IntersectCharacter(vec2 Pos0, vec2 Pos1, float Radius, vec2 &NewPos, const CCharacter *pNotThis, int CollideWith, const CCharacter *pThisOnly)
{
	return (CCharacter *)IntersectEntity(Pos0, Pos1, Radius, ENTTYPE_CHARACTER, NewPos, pNotThis, CollideWith, pThisOnly);
}

CEntity *CGameWorld::IntersectEntity(vec2 Pos0, vec2 Pos1, float Radius, int Type, vec2 &NewPos, const CEntity *pNotThis, int CollideWith, const CEntity *pThisOnly)
{
	float ClosestLen = distance(Pos0, Pos1) * 100.0f;
	CEntity *pClosest = nullptr;

	CEntity *pEntity = FindFirst(Type);
	for(; pEntity; pEntity = pEntity->TypeNext())
	{
		if(pEntity == pNotThis)
			continue;

		if(pThisOnly && pEntity != pThisOnly)
			continue;

		if(CollideWith != -1 && !pEntity->CanCollide(CollideWith))
			continue;

		vec2 IntersectPos;
		if(closest_point_on_line(Pos0, Pos1, pEntity->m_Pos, IntersectPos))
		{
			float Len = distance(pEntity->m_Pos, IntersectPos);
			if(Len < pEntity->m_ProximityRadius + Radius)
			{
				Len = distance(Pos0, IntersectPos);
				if(Len < ClosestLen)
				{
					NewPos = IntersectPos;
					ClosestLen = Len;
					pClosest = pEntity;
				}
			}
		}
	}

	return pClosest;
}

std::vector<CCharacter *> CGameWorld::IntersectedCharacters(vec2 Pos0, vec2 Pos1, float Radius, const CEntity *pNotThis)
{
	std::vector<CCharacter *> vpCharacters;
	CCharacter *pChr = (CCharacter *)FindFirst(CGameWorld::ENTTYPE_CHARACTER);
	for(; pChr; pChr = (CCharacter *)pChr->TypeNext())
	{
		if(pChr == pNotThis)
			continue;

		vec2 IntersectPos;
		if(closest_point_on_line(Pos0, Pos1, pChr->m_Pos, IntersectPos))
		{
			float Len = distance(pChr->m_Pos, IntersectPos);
			if(Len < pChr->m_ProximityRadius + Radius)
			{
				vpCharacters.push_back(pChr);
			}
		}
	}
	return vpCharacters;
}

void CGameWorld::ReleaseHooked(int ClientId)
{
	CCharacter *pChr = (CCharacter *)CGameWorld::FindFirst(CGameWorld::ENTTYPE_CHARACTER);
	for(; pChr; pChr = (CCharacter *)pChr->TypeNext())
	{
		if(pChr->Core()->HookedPlayer() == ClientId && !pChr->IsSuper())
		{
			pChr->ReleaseHook();
		}
	}
}

CEntity *CGameWorld::GetEntity(int Id, int EntityType)
{
	for(CEntity *pEnt = m_apFirstEntityTypes[EntityType]; pEnt; pEnt = pEnt->m_pNextTypeEntity)
		if(pEnt->m_Id == Id)
			return pEnt;
	return nullptr;
}

void CGameWorld::CreateExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage, int ActivatedTeam, CClientMask Mask, int Id)
{
	if(Owner < 0 && m_WorldConfig.m_IsSolo && !(Weapon == WEAPON_SHOTGUN && m_WorldConfig.m_IsDDRace))
		return;

	if(m_WorldConfig.m_IsDDRace && m_WorldConfig.m_PredictDDRace)
	{
		// vanilla has different projectile physics
		CreatePredictedExplosionEvent(Pos, Id);
	}

	// deal damage
	CEntity *apEnts[MAX_CLIENTS];
	float Radius = 135.0f;
	float InnerRadius = 48.0f;
	int Num = FindEntities(Pos, Radius, apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
	for(int i = 0; i < Num; i++)
	{
		auto *pChar = static_cast<CCharacter *>(apEnts[i]);
		vec2 Diff = pChar->m_Pos - Pos;
		vec2 ForceDir(0, 1);
		float l = length(Diff);
		if(l)
			ForceDir = normalize(Diff);
		l = 1 - std::clamp((l - InnerRadius) / (Radius - InnerRadius), 0.0f, 1.0f);
		float Strength;
		CCharacter *pOwnerChar = GetCharacterById(Owner);
		if(Owner == -1 || !pOwnerChar)
			Strength = GlobalTuning()->m_ExplosionStrength;
		else
			Strength = pOwnerChar->GetTuning(pOwnerChar->GetOverriddenTuneZone())->m_ExplosionStrength;

		float Dmg = Strength * l;
		if((int)Dmg)
			if((pOwnerChar ? !pOwnerChar->GrenadeHitDisabled() : g_Config.m_SvHit || NoDamage) || Owner == pChar->GetCid())
			{
				if(Owner != -1 && !pChar->CanCollide(Owner))
					continue;
				if(Owner == -1 && ActivatedTeam != -1 && pChar->Team() != ActivatedTeam)
					continue;
				pChar->TakeDamage(ForceDir * Dmg * 2, (int)Dmg, Owner, Weapon);
				if(pOwnerChar)
				{
					pOwnerChar->AntiPingInterference(pChar->GetCid());
				}
				if(pOwnerChar ? pOwnerChar->GrenadeHitDisabled() : !g_Config.m_SvHit || NoDamage)
					break;
			}
	}
}

bool CGameWorld::IsLocalTeam(int OwnerId) const
{
	return OwnerId < 0 || m_Teams.CanCollide(m_LocalClientId, OwnerId);
}

void CGameWorld::NetObjBegin(CTeamsCore Teams, int LocalClientId)
{
	m_Teams = Teams;
	m_LocalClientId = LocalClientId;

	for(int i = 0; i < NUM_ENTTYPES; i++)
		for(CEntity *pEnt = FindFirst(i); pEnt; pEnt = pEnt->TypeNext())
		{
			pEnt->m_MarkedForDestroy = true;
			if(i == ENTTYPE_CHARACTER)
				((CCharacter *)pEnt)->m_KeepHooked = false;
		}
	OnModified();
}

void CGameWorld::NetCharAdd(int ObjId, CNetObj_Character *pCharObj, CNetObj_DDNetCharacter *pExtended, int GameTeam, bool IsLocal)
{
	if(IsLocalTeam(ObjId))
	{
		CCharacter *pChar;
		if((pChar = (CCharacter *)GetEntity(ObjId, ENTTYPE_CHARACTER)))
		{
			pChar->Read(pCharObj, pExtended, IsLocal);
			pChar->Keep();
		}
		else
		{
			pChar = new CCharacter(this, ObjId, pCharObj, pExtended);
			InsertEntity(pChar);
		}

		if(pChar)
			pChar->m_GameTeam = GameTeam;
	}
}

void CGameWorld::NetObjAdd(int ObjId, int ObjType, const void *pObjData, const CNetObj_EntityEx *pDataEx)
{
	if((ObjType == NETOBJTYPE_PROJECTILE || ObjType == NETOBJTYPE_DDRACEPROJECTILE || ObjType == NETOBJTYPE_DDNETPROJECTILE) && m_WorldConfig.m_PredictWeapons)
	{
		CProjectileData Data = ExtractProjectileInfo(ObjType, pObjData, this, pDataEx);
		if(!Data.HasValidStart() || !IsLocalTeam(Data.m_Owner))
			return;

		CProjectile NetProj = CProjectile(this, ObjId, &Data);

		if(NetProj.m_Type != WEAPON_SHOTGUN && absolute(length(NetProj.m_Direction) - 1.f) > 0.02f) // workaround to skip grenades on ball mod
			return;

		if(CProjectile *pProj = (CProjectile *)GetEntity(ObjId, ENTTYPE_PROJECTILE))
		{
			if(NetProj.Match(pProj))
			{
				pProj->Keep();
				if(pProj->m_Type == WEAPON_SHOTGUN && m_WorldConfig.m_IsDDRace)
					pProj->m_LifeSpan = NetProj.GetLifeSpan();
				return;
			}
		}
		if(!Data.m_ExtraInfo)
		{
			// try to match the newly received (unrecognized) projectile with a locally fired one
			for(CProjectile *pProj = (CProjectile *)FindFirst(CGameWorld::ENTTYPE_PROJECTILE); pProj; pProj = (CProjectile *)pProj->TypeNext())
			{
				if(pProj->m_Id == -1 && NetProj.Match(pProj))
				{
					pProj->m_Id = ObjId;
					pProj->Keep();
					return;
				}
			}
			// otherwise try to determine its owner by checking if there is only one player nearby
			if(static_cast<int64_t>(NetProj.m_StartTick) >= static_cast<int64_t>(GameTick()) - 4)
			{
				const vec2 NetPos = NetProj.m_Pos - normalize(NetProj.m_Direction) * CCharacterCore::PhysicalSize() * 0.75;
				const bool Prev = (static_cast<int64_t>(GameTick()) - NetProj.m_StartTick) > 1;
				float First = 200.0f, Second = 200.0f;
				CCharacter *pClosest = nullptr;
				for(CCharacter *pChar = (CCharacter *)FindFirst(ENTTYPE_CHARACTER); pChar; pChar = (CCharacter *)pChar->TypeNext())
				{
					float Dist = distance(Prev ? pChar->m_PrevPrevPos : pChar->m_PrevPos, NetPos);
					if(Dist < First)
					{
						Second = First;
						pClosest = pChar;
						First = Dist;
					}
					else if(Dist < Second)
					{
						Second = Dist;
					}
				}
				if(pClosest && maximum(First, 2.f) * 1.2f < Second)
					NetProj.m_Owner = pClosest->m_Id;
			}
		}
		CProjectile *pProj = new CProjectile(NetProj);
		InsertEntity(pProj);
	}
	else if((ObjType == NETOBJTYPE_PICKUP || ObjType == NETOBJTYPE_DDNETPICKUP) && m_WorldConfig.m_PredictWeapons)
	{
		CPickupData Data = ExtractPickupInfo(ObjType, pObjData, pDataEx);
		if(!Data.HasValidPosition() || (Data.m_Flags & PICKUPFLAG_NO_PREDICT))
			return;
		CPickup NetPickup = CPickup(this, ObjId, &Data);
		if(CPickup *pPickup = (CPickup *)GetEntity(ObjId, ENTTYPE_PICKUP))
		{
			if(NetPickup.Match(pPickup))
			{
				pPickup->m_Pos = NetPickup.m_Pos;
				pPickup->Keep();
				return;
			}
		}
		CEntity *pEnt = new CPickup(NetPickup);
		InsertEntity(pEnt, true);
	}
	else if(ObjType == NETOBJTYPE_LASER || ObjType == NETOBJTYPE_DDNETLASER)
	{
		CLaserData Data = ExtractLaserInfo(ObjType, pObjData, this, pDataEx);
		if(!Data.HasValidPositions() || !IsLocalTeam(Data.m_Owner) || !Data.m_Predict)
		{
			return;
		}
		if(!(Data.m_Type == LASERTYPE_DOOR ? m_WorldConfig.m_PredictTiles : m_WorldConfig.m_PredictWeapons))
			return;

		if(Data.m_Type == LASERTYPE_RIFLE || Data.m_Type == LASERTYPE_SHOTGUN || Data.m_Type < 0)
		{
			CLaser NetLaser = CLaser(this, ObjId, &Data);
			CLaser *pMatching = nullptr;
			if(CLaser *pLaser = dynamic_cast<CLaser *>(GetEntity(ObjId, ENTTYPE_LASER)))
				if(NetLaser.Match(pLaser))
					pMatching = pLaser;
			if(!pMatching)
			{
				for(CEntity *pEnt = FindFirst(CGameWorld::ENTTYPE_LASER); pEnt; pEnt = pEnt->TypeNext())
				{
					auto *const pLaser = dynamic_cast<CLaser *>(pEnt);
					if(pLaser && pLaser->m_Id == -1 && NetLaser.Match(pLaser))
					{
						pMatching = pLaser;
						pMatching->m_Id = ObjId;
						break;
					}
				}
			}
			if(pMatching)
			{
				pMatching->Keep();
				if(distance(NetLaser.m_From, NetLaser.m_Pos) < distance(pMatching->m_From, pMatching->m_Pos) - 2.f)
				{
					// if the laser stopped earlier than predicted, set the energy to 0
					pMatching->m_Energy = 0.f;
					pMatching->m_Pos = NetLaser.m_Pos;
				}
			}
		}
		else if(Data.m_Type == LASERTYPE_DRAGGER)
		{
			CDragger NetDragger = CDragger(this, ObjId, &Data);
			if(NetDragger.GetStrength() > 0)
			{
				auto *pDragger = dynamic_cast<CDragger *>(GetEntity(ObjId, ENTTYPE_DRAGGER));
				if(pDragger && NetDragger.Match(pDragger))
				{
					pDragger->Keep();
					pDragger->Read(&Data);
					return;
				}
				CEntity *pEnt = new CDragger(NetDragger);
				InsertEntity(pEnt);
			}
		}
		else if(Data.m_Type == LASERTYPE_DOOR)
		{
			CDoor NetDoor = CDoor(this, ObjId, &Data);
			auto *pDoor = dynamic_cast<CDoor *>(GetEntity(ObjId, ENTTYPE_DOOR));
			if(pDoor && NetDoor.Match(pDoor))
			{
				pDoor->Keep();
				pDoor->Read(&Data);
				return;
			}
			CDoor *pEnt = new CDoor(NetDoor);
			InsertEntity(pEnt);
		}
		else if(Data.m_Type == LASERTYPE_PLASMA)
		{
			CPlasma NetPlasma = CPlasma(this, ObjId, &Data);
			auto *pPlasma = dynamic_cast<CPlasma *>(GetEntity(ObjId, ENTTYPE_PLASMA));
			if(pPlasma && NetPlasma.Match(pPlasma))
			{
				pPlasma->Keep();
				pPlasma->Read(&Data);
				return;
			}
			CPlasma *pEnt = new CPlasma(NetPlasma);
			InsertEntity(pEnt);
		}
	}
}

void CGameWorld::ResetDoorCollision()
{
	// 只使用门的临时借用链，保留实体主链与原并列顺序，不在快照收尾分配内存。
	CDoor *pHead = nullptr;
	CDoor **ppTail = &pHead;
	for(CEntity *pEnt = FindFirst(ENTTYPE_DOOR); pEnt; pEnt = pEnt->TypeNext())
	{
		auto *pDoor = static_cast<CDoor *>(pEnt);
		*ppTail = pDoor;
		ppTail = &pDoor->m_pCollisionNext;
		*ppTail = nullptr;
	}
	const auto Less = [this](const CDoor *pLeft, const CDoor *pRight) {
		const int LeftIndex = QmCollision::CanRoundPosition(pLeft->m_Pos) ? Collision()->GetPureMapIndex(pLeft->m_Pos) : -1;
		const int RightIndex = QmCollision::CanRoundPosition(pRight->m_Pos) ? Collision()->GetPureMapIndex(pRight->m_Pos) : -1;
		if(LeftIndex != RightIndex)
			return LeftIndex < RightIndex;
		return (pLeft->m_Number > 0) < (pRight->m_Number > 0);
	};
	const auto Sort = [&Less](auto &&Self, CDoor *pList) -> CDoor * {
		if(!pList || !pList->m_pCollisionNext)
			return pList;
		CDoor *pSlow = pList;
		CDoor *pFast = pList->m_pCollisionNext;
		while(pFast && pFast->m_pCollisionNext)
		{
			pSlow = pSlow->m_pCollisionNext;
			pFast = pFast->m_pCollisionNext->m_pCollisionNext;
		}
		CDoor *pRight = pSlow->m_pCollisionNext;
		pSlow->m_pCollisionNext = nullptr;
		CDoor *pLeft = Self(Self, pList);
		pRight = Self(Self, pRight);
		CDoor *pMerged = nullptr;
		CDoor **ppNext = &pMerged;
		while(pLeft && pRight)
		{
			// 相同键先取左侧，维持原 stable_sort 的稳定性。
			CDoor *&pChosen = !Less(pRight, pLeft) ? pLeft : pRight;
			*ppNext = pChosen;
			pChosen = pChosen->m_pCollisionNext;
			ppNext = &(*ppNext)->m_pCollisionNext;
		}
		*ppNext = pLeft ? pLeft : pRight;
		return pMerged;
	};
	pHead = Sort(Sort, pHead);
	while(pHead)
	{
		CDoor *pNext = pHead->m_pCollisionNext;
		pHead->m_pCollisionNext = nullptr;
		pHead->ResetCollision();
		pHead = pNext;
	}
}

void CGameWorld::NetObjEnd()
{
	// keep predicting hooked characters, based on hook position
	for(int i = 0; i < MAX_CLIENTS; i++)
		if(CCharacter *pChar = GetCharacterById(i))
			if(!pChar->m_MarkedForDestroy)
				if(CCharacter *pHookedChar = GetCharacterById(pChar->m_Core.HookedPlayer()))
					if(pHookedChar->m_MarkedForDestroy)
					{
						pHookedChar->m_Pos = pHookedChar->m_Core.m_Pos = pChar->m_Core.m_HookPos;
						pHookedChar->ResetVelocity();
						mem_zero(&pHookedChar->m_SavedInput, sizeof(pHookedChar->m_SavedInput));
						pHookedChar->m_SavedInput.m_TargetY = -1;
						pHookedChar->m_KeepHooked = true;
						pHookedChar->m_MarkedForDestroy = false;
					}
	RemoveEntities();
	ResetDoorCollision();

	// Update character IDs and pointers
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		m_apCharacters[i] = nullptr;
		m_Core.m_apCharacters[i] = nullptr;
	}
	for(CCharacter *pChar = (CCharacter *)FindFirst(ENTTYPE_CHARACTER); pChar; pChar = (CCharacter *)pChar->TypeNext())
	{
		int Id = pChar->GetCid();
		if(Id >= 0 && Id < MAX_CLIENTS)
		{
			m_apCharacters[Id] = pChar;
			m_Core.m_apCharacters[Id] = &pChar->m_Core;
		}
	}
}

bool CGameWorld::CopyWorldClean(CGameWorld *pFrom) noexcept
{
	return TryCopyWorld(pFrom, false);
}

bool CGameWorld::CopyWorld(CGameWorld *pFrom) noexcept
{
	return TryCopyWorld(pFrom, true);
}

bool CGameWorld::TryCopyWorld(CGameWorld *pFrom, bool LinkCopy) noexcept
{
	if(pFrom == this || !pFrom)
		return true;
	try
	{
		CGameWorld Candidate;
		Candidate.m_GameTick = pFrom->m_GameTick;
		Candidate.m_pCollision = pFrom->m_pCollision;
		Candidate.m_WorldConfig = pFrom->m_WorldConfig;
		Candidate.m_pTuningList = pFrom->m_pTuningList;
		Candidate.m_pMapBugs = pFrom->m_pMapBugs;
		Candidate.m_Teams = pFrom->m_Teams;
		Candidate.m_Core.m_vSwitchers = pFrom->m_Core.m_vSwitchers;
		Candidate.m_PredictedEvents = pFrom->m_PredictedEvents;
		for(int Type = 0; Type < NUM_ENTTYPES; Type++)
		{
			for(CEntity *pEnt = pFrom->FindLast(Type); pEnt; pEnt = pEnt->TypePrev())
			{
				std::unique_ptr<CEntity> pCopy;
				if(Type == ENTTYPE_PROJECTILE)
					pCopy.reset(new CProjectile(*static_cast<CProjectile *>(pEnt)));
				else if(Type == ENTTYPE_LASER)
					pCopy.reset(new CLaser(*static_cast<CLaser *>(pEnt)));
				else if(Type == ENTTYPE_DRAGGER)
					pCopy.reset(new CDragger(*static_cast<CDragger *>(pEnt)));
				else if(Type == ENTTYPE_CHARACTER)
					pCopy.reset(new CCharacter(*static_cast<CCharacter *>(pEnt)));
				else if(Type == ENTTYPE_PICKUP)
					pCopy.reset(new CPickup(*static_cast<CPickup *>(pEnt)));
				else if(Type == ENTTYPE_PLASMA)
					pCopy.reset(new CPlasma(*static_cast<CPlasma *>(pEnt)));
				if(pCopy)
				{
					// 只暂存源身份，准备阶段不建立源对象的反向引用。
					if(LinkCopy)
						pCopy->m_pParent = pEnt;
					Candidate.InsertEntity(pCopy.get());
					pCopy.release();
				}
			}
		}

		// 所有容器、实体及角色回调均已准备；此后只清理、交换和重定借用指针。
		m_IsValidCopy = false;
		OnModified();
		QmCopyLinks::Unlink(*this);
		Clear();
		m_GameTick = Candidate.m_GameTick;
		m_pCollision = Candidate.m_pCollision;
		m_WorldConfig = Candidate.m_WorldConfig;
		m_pTuningList = Candidate.m_pTuningList;
		m_pMapBugs = Candidate.m_pMapBugs;
		m_Teams = Candidate.m_Teams;
		m_Core.m_vSwitchers.swap(Candidate.m_Core.m_vSwitchers);
		m_PredictedEvents.swap(Candidate.m_PredictedEvents);
		for(int Type = 0; Type < NUM_ENTTYPES; Type++)
			m_apFirstEntityTypes[Type] = std::exchange(Candidate.m_apFirstEntityTypes[Type], nullptr);
		for(int Id = 0; Id < MAX_CLIENTS; Id++)
		{
			m_apCharacters[Id] = std::exchange(Candidate.m_apCharacters[Id], nullptr);
			m_Core.m_apCharacters[Id] = std::exchange(Candidate.m_Core.m_apCharacters[Id], nullptr);
		}
		if(LinkCopy)
		{
			if(pFrom->m_pChild)
				pFrom->m_pChild->m_IsValidCopy = false;
			QmCopyLinks::Attach(*pFrom, *this);
		}
		for(int Type = 0; Type < NUM_ENTTYPES; Type++)
			for(CEntity *pEnt = FindFirst(Type); pEnt; pEnt = pEnt->TypeNext())
			{
				pEnt->m_pGameWorld = this;
				if(Type == ENTTYPE_CHARACTER)
				{
					// 回调已捕获该角色；这里只重定核心所属，不再分配回调对象。
					static_cast<CCharacter *>(pEnt)->m_Core.SetCoreWorld(&m_Core, m_pCollision, &m_Teams);
				}
				if(LinkCopy && pEnt->m_pParent)
					QmCopyLinks::Attach(*pEnt->m_pParent, *pEnt);
			}
		m_IsValidCopy = LinkCopy;
		return true;
	}
	catch(...)
	{
		return false;
	}
}

CEntity *CGameWorld::FindMatch(int ObjId, int ObjType, const void *pObjData)
{
	switch(ObjType)
	{
	case NETOBJTYPE_CHARACTER:
	{
		CCharacter *pEnt = (CCharacter *)GetEntity(ObjId, ENTTYPE_CHARACTER);
		if(pEnt && CCharacter(this, ObjId, (CNetObj_Character *)pObjData).Match(pEnt))
		{
			return pEnt;
		}
		return nullptr;
	}
	case NETOBJTYPE_PROJECTILE:
	case NETOBJTYPE_DDRACEPROJECTILE:
	case NETOBJTYPE_DDNETPROJECTILE:
	{
		CProjectileData Data = ExtractProjectileInfo(ObjType, pObjData, this, nullptr);
		if(!Data.HasValidStart())
			return nullptr;
		CProjectile *pEnt = (CProjectile *)GetEntity(ObjId, ENTTYPE_PROJECTILE);
		if(pEnt && CProjectile(this, ObjId, &Data).Match(pEnt))
		{
			return pEnt;
		}
		return nullptr;
	}
	case NETOBJTYPE_LASER:
	case NETOBJTYPE_DDNETLASER:
	{
		CLaserData Data = ExtractLaserInfo(ObjType, pObjData, this, nullptr);
		if(!Data.HasValidPositions())
			return nullptr;
		if(Data.m_Type == LASERTYPE_RIFLE || Data.m_Type == LASERTYPE_SHOTGUN)
		{
			CLaser *pEnt = (CLaser *)GetEntity(ObjId, ENTTYPE_LASER);
			if(pEnt && CLaser(this, ObjId, &Data).Match(pEnt))
			{
				return pEnt;
			}
		}
		else if(Data.m_Type == LASERTYPE_DRAGGER)
		{
			CDragger *pEnt = (CDragger *)GetEntity(ObjId, ENTTYPE_DRAGGER);
			if(pEnt && CDragger(this, ObjId, &Data).Match(pEnt))
			{
				return pEnt;
			}
		}
		else if(Data.m_Type == LASERTYPE_DOOR)
		{
			CDoor *pEnt = (CDoor *)GetEntity(ObjId, ENTTYPE_DOOR);
			if(pEnt && CDoor(this, ObjId, &Data).Match(pEnt))
			{
				return pEnt;
			}
		}
		else if(Data.m_Type == LASERTYPE_PLASMA)
		{
			CPlasma *pEnt = (CPlasma *)GetEntity(ObjId, ENTTYPE_PLASMA);
			if(pEnt && CPlasma(this, ObjId, &Data).Match(pEnt))
			{
				return pEnt;
			}
		}
		return nullptr;
	}
	case NETOBJTYPE_PICKUP:
	case NETOBJTYPE_DDNETPICKUP:
	{
		CPickupData Data = ExtractPickupInfo(ObjType, pObjData, nullptr);
		if(!Data.HasValidPosition())
			return nullptr;
		CPickup *pEnt = (CPickup *)GetEntity(ObjId, ENTTYPE_PICKUP);
		if(pEnt && CPickup(this, ObjId, &Data).Match(pEnt))
		{
			return pEnt;
		}
		return nullptr;
	}
	}
	return nullptr;
}

void CGameWorld::OnModified() const
{
	if(m_pChild)
		m_pChild->m_IsValidCopy = false;
}

void CGameWorld::Clear()
{
	// delete all entities
	for(auto &pFirstEntityType : m_apFirstEntityTypes)
		while(pFirstEntityType)
		{
			CEntity *pEntity = pFirstEntityType;
			CEntity *pNextEntity = pEntity->m_pNextTypeEntity;
			RemoveEntity(pEntity);
			delete pEntity; // NOLINT(clang-analyzer-cplusplus.NewDelete)
			pFirstEntityType = pNextEntity;
		}
}

bool CGameWorld::EmulateBug(int Bug) const
{
	return m_pMapBugs->Contains(Bug);
}

bool CGameWorld::TryAppendPredictedEvent(const CPredictedEvent &Event) noexcept
{
	if(!Event.HasValidPosition())
		return false;
	try
	{
		m_PredictedEvents.push_back(Event);
		return true;
	}
	catch(...)
	{
		// 事件是值数据；扩容失败只丢弃本次记录，保留已有队列供后续帧使用。
		return false;
	}
}

void CGameWorld::CreatePredictedEvent(const CPredictedEvent &NewEvent)
{
	if(!NewEvent.HasValidPosition() || !g_Config.m_ClPredictEvents || !m_WorldConfig.m_PredictEvents)
		return;

	// prediction is ran multiple times per tick, check if event already exists
	const auto It = std::find_if(
		m_PredictedEvents.begin(),
		m_PredictedEvents.end(),
		[NewEvent](const CPredictedEvent &Event) { return QmPredictedEventMatchesForCreation(Event, NewEvent); });

	if(It == m_PredictedEvents.end())
	{
		(void)TryAppendPredictedEvent(NewEvent);
	}
	else if(NewEvent.m_EventId == NETEVENTTYPE_SOUNDWORLD && !It->m_Handled)
	{
		// 预测校正位置时保留同一声音身份，同时更新尚未播放事件的
		// 空间化位置。已播放事件保留原位置，确保服务器确认仍能匹配。
		It->m_Pos = NewEvent.m_Pos;
	}
}

bool CGameWorld::CheckPredictedEventHandled(const CPredictedEvent &CheckEvent, bool *pUnplayedMatch)
{
	// 网络确认事件没有实体 Id，只能在有限 tick 窗口内按类型、附加信息和位置匹配。
	return QmCheckPredictedEventHandled(m_PredictedEvents, CheckEvent, pUnplayedMatch);
}

bool CGameWorld::CheckPredictedHammerHitHandled(const CPredictedEvent &CheckEvent, bool *pUnplayedMatch)
{
	return QmCheckPredictedHammerHitHandled(m_PredictedEvents, CheckEvent, pUnplayedMatch);
}

bool CGameWorld::CheckPredictedHammerHitHandledLoose(int AttackerId, vec2 Pos, int Tick)
{
	return QmCheckPredictedHammerHitHandledLoose(m_PredictedEvents, AttackerId, Pos, Tick);
}

void CGameWorld::CreatePredictedSound(vec2 Pos, int SoundId, int Id)
{
	if(!g_Config.m_SndEnable)
		return;

	if(g_Config.m_DbgPredictEvents)
		dbg_msg("pred_event", "create sound=%d id=%d tick=%d pos=%.1f,%.1f", SoundId, Id, GameTick(), Pos.x, Pos.y);

	CPredictedEvent Event(NETEVENTTYPE_SOUNDWORLD, Pos, Id, GameTick(), SoundId);
	CreatePredictedEvent(Event);
}

// QmClient: 见 gameworld.h 声明处注释。屏障事件带实体 Id（本地角色），
// 因此在 QmCheckPredictedEventHandled 中不受"无 Id 事件只屏蔽 1 tick"的
// 限制，能覆盖服务端确认晚到数 tick 的情况；同位置 3 秒内的同声快照
// 事件至多被吞一次，属可接受的权衡（与预测钩子声音的既有行为一致）。
void CGameWorld::CreateHandledPredictedSound(vec2 Pos, int SoundId, int Id)
{
	if(!g_Config.m_SndEnable)
		return;

	CPredictedEvent Event(NETEVENTTYPE_SOUNDWORLD, Pos, Id, GameTick(), SoundId);
	if(!Event.HasValidPosition())
		return;
	Event.m_Handled = true;
	const auto It = std::find_if(
		m_PredictedEvents.begin(),
		m_PredictedEvents.end(),
		[Event](const CPredictedEvent &Existing) { return QmPredictedEventMatchesForCreation(Existing, Event); });
	if(It == m_PredictedEvents.end())
		(void)TryAppendPredictedEvent(Event);
}

void CGameWorld::CreatePredictedExplosionEvent(vec2 Pos, int Id)
{
	CPredictedEvent Event(NETEVENTTYPE_EXPLOSION, Pos, Id, GameTick());
	CreatePredictedEvent(Event);
}

void CGameWorld::CreatePredictedHammerHitEvent(vec2 Pos, int Id, int TargetId)
{
	if(!g_Config.m_ClPredictEvents || !m_WorldConfig.m_PredictEvents)
		return;

	CPredictedEvent Event(NETEVENTTYPE_HAMMERHIT, Pos, Id, GameTick(), TargetId);
	if(!Event.HasValidPosition())
		return;
	// 预测在同一 tick 内可能重复运行，位置修正不应把同一次命中拆成多个粒子事件。
	const auto It = std::find_if(
		m_PredictedEvents.begin(),
		m_PredictedEvents.end(),
		[Event](const CPredictedEvent &Existing) { return QmPredictedHammerHitMatchesForCreation(Existing, Event); });
	if(It != m_PredictedEvents.end())
	{
		// 命中身份由攻击者、目标和 tick 决定；仅在粒子尚未播放时
		// 更新预测校正位置，已播放事件保留原位置以便服务器确认匹配。
		if(!It->m_Handled)
			It->m_Pos = Event.m_Pos;
		return;
	}
	CreatePredictedEvent(Event);
}

void CGameWorld::CreatePredictedDamageIndEvent(vec2 Pos, float Angle, int Amount, int Id)
{
	float a = 3 * pi / 2 + Angle;
	float s = a - pi / 3;
	float e = a + pi / 3;
	for(int i = 0; i < Amount; i++)
	{
		float f = mix(s, e, (i + 1) / (float)(Amount + 1));

		CPredictedEvent Event(NETEVENTTYPE_DAMAGEIND, Pos, Id, GameTick(), (int)(f * 256.0f));
		CreatePredictedEvent(Event);
	}
}
