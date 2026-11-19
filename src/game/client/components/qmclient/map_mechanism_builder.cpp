#include "map_mechanism_builder.h"

#include <game/client/laser_data.h>
#include <game/client/pickup_data.h>
#include <game/client/prediction/entities/door.h>
#include <game/client/prediction/entities/dragger.h>
#include <game/client/prediction/entities/laser.h>
#include <game/client/prediction/entities/pickup.h>
#include <game/client/prediction/entities/practice_light.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <cmath>

namespace
{
	// 与服务端 IGameController::OnEntity 的八个相邻方向一致。
	constexpr int s_aSideOffsetX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
	constexpr int s_aSideOffsetY[8] = {1, 1, 0, -1, -1, -1, 0, 1};

	qm_map_mechanisms::SSnapshotLaser *FindNearestSnapshotLaser(
		std::vector<qm_map_mechanisms::SSnapshotLaser> &vSnapshots,
		int Type, vec2 Hub, int Number, float MaxDistance)
	{
		qm_map_mechanisms::SSnapshotLaser *pBest = nullptr;
		float BestDistance = MaxDistance;
		for(qm_map_mechanisms::SSnapshotLaser &Snapshot : vSnapshots)
		{
			if(Snapshot.m_Used || Snapshot.m_Type != Type || Snapshot.m_SwitchNumber != Number)
				continue;
			const float Distance = distance(Snapshot.m_Hub, Hub);
			if(Distance > BestDistance)
				continue;
			BestDistance = Distance;
			pBest = &Snapshot;
		}
		if(pBest != nullptr)
			pBest->m_Used = true;
		return pBest;
	}

	qm_map_mechanisms::SSnapshotLaser *FindSnapshotLight(
		std::vector<qm_map_mechanisms::SSnapshotLaser> &vSnapshots,
		vec2 Hub, int Number, vec2 Direction)
	{
		qm_map_mechanisms::SSnapshotLaser *pBest = nullptr;
		float BestDistance = 512.0f;
		float BestDirection = -2.0f;
		for(qm_map_mechanisms::SSnapshotLaser &Snapshot : vSnapshots)
		{
			if(Snapshot.m_Used || Snapshot.m_Type != LASERTYPE_FREEZE || Snapshot.m_SwitchNumber != Number)
				continue;
			const vec2 SnapshotDirection = Snapshot.m_From - Snapshot.m_Hub;
			if(length(SnapshotDirection) < 0.5f)
				continue;
			const float DirectionScore = dot(normalize(SnapshotDirection), Direction);
			const float SnapshotDistance = distance(Snapshot.m_Hub, Hub);
			if(DirectionScore <= 0.0f || SnapshotDistance > 512.0f)
				continue;
			if(SnapshotDistance > BestDistance + 0.5f ||
				(absolute(SnapshotDistance - BestDistance) <= 0.5f && DirectionScore <= BestDirection))
				continue;
			BestDistance = SnapshotDistance;
			BestDirection = DirectionScore;
			pBest = &Snapshot;
		}
		if(pBest != nullptr)
			pBest->m_Used = true;
		return pBest;
	}

	qm_map_mechanisms::SSnapshotLaser *FindSnapshotDoor(
		std::vector<qm_map_mechanisms::SSnapshotLaser> &vSnapshots,
		vec2 Hub, int Number, vec2 Direction)
	{
		qm_map_mechanisms::SSnapshotLaser *pBest = nullptr;
		float BestDistance = 512.0f;
		float BestDirection = 0.9f;
		for(qm_map_mechanisms::SSnapshotLaser &Snapshot : vSnapshots)
		{
			if(Snapshot.m_Used || Snapshot.m_Type != LASERTYPE_DOOR || Snapshot.m_SwitchNumber != Number)
				continue;
			const vec2 SnapshotDirection = Snapshot.m_From - Snapshot.m_Hub;
			if(length(SnapshotDirection) < 0.5f)
				continue;
			const float DirectionScore = dot(normalize(SnapshotDirection), Direction);
			const float SnapshotDistance = distance(Snapshot.m_Hub, Hub);
			if(DirectionScore < BestDirection || SnapshotDistance > 512.0f)
				continue;
			if(SnapshotDistance > BestDistance + 0.5f ||
				(absolute(SnapshotDistance - BestDistance) <= 0.5f && DirectionScore <= BestDirection))
				continue;
			BestDistance = SnapshotDistance;
			BestDirection = DirectionScore;
			pBest = &Snapshot;
		}
		if(pBest != nullptr)
			pBest->m_Used = true;
		return pBest;
	}
}

namespace qm_map_mechanisms
{
	SBuildCounts Build(CGameWorld &World, CCollision *pCollision, int &NextEntityId, const SBuildOptions &Options)
	{
		SBuildCounts Counts;
		if(pCollision == nullptr)
			return Counts;

		const CTile *pGame = pCollision->GameLayer();
		const CTile *pFront = pCollision->FrontLayer();
		const CSwitchTile *pSwitch = pCollision->SwitchLayer();
		if(pGame == nullptr)
			return Counts;

		const int Width = pCollision->GetWidth();
		const int Height = pCollision->GetHeight();
		for(int y = 0; y < Height; ++y)
		{
			for(int x = 0; x < Width; ++x)
			{
				const int TileIndex = y * Width + x;
				const vec2 Pos(x * 32.0f + 16.0f, y * 32.0f + 16.0f);

				const auto HandleEntity = [&](int EntityIndex, int Layer, int Number, int TileFlags) {
					int PickupType = -1;
					int PickupSubtype = 0;
					if(EntityIndex == ENTITY_ARMOR_1)
						PickupType = POWERUP_ARMOR;
					else if(EntityIndex == ENTITY_ARMOR_SHOTGUN)
						PickupType = POWERUP_ARMOR_SHOTGUN;
					else if(EntityIndex == ENTITY_ARMOR_GRENADE)
						PickupType = POWERUP_ARMOR_GRENADE;
					else if(EntityIndex == ENTITY_ARMOR_NINJA)
						PickupType = POWERUP_ARMOR_NINJA;
					else if(EntityIndex == ENTITY_ARMOR_LASER)
						PickupType = POWERUP_ARMOR_LASER;
					else if(EntityIndex == ENTITY_HEALTH_1)
						PickupType = POWERUP_FREEZE;
					else if(EntityIndex == ENTITY_WEAPON_SHOTGUN)
					{
						PickupType = POWERUP_WEAPON;
						PickupSubtype = WEAPON_SHOTGUN;
					}
					else if(EntityIndex == ENTITY_WEAPON_GRENADE)
					{
						PickupType = POWERUP_WEAPON;
						PickupSubtype = WEAPON_GRENADE;
					}
					else if(EntityIndex == ENTITY_WEAPON_LASER)
					{
						PickupType = POWERUP_WEAPON;
						PickupSubtype = WEAPON_LASER;
					}
					else if(EntityIndex == ENTITY_POWERUP_NINJA)
					{
						PickupType = POWERUP_NINJA;
						PickupSubtype = WEAPON_NINJA;
					}

					if(PickupType != -1)
					{
						if(!Options.m_BuildPickups)
							return;
						CPickupData Data;
						Data.m_Pos = Pos;
						Data.m_Type = PickupType;
						Data.m_Subtype = PickupSubtype;
						Data.m_SwitchNumber = Number;
						Data.m_Flags = 0;
						if(TileFlags & TILEFLAG_XFLIP)
							Data.m_Flags |= PICKUPFLAG_XFLIP;
						if(TileFlags & TILEFLAG_YFLIP)
							Data.m_Flags |= PICKUPFLAG_YFLIP;
						if(TileFlags & TILEFLAG_ROTATE)
							Data.m_Flags |= PICKUPFLAG_ROTATE;
						World.InsertEntity(new CPickup(&World, NextEntityId++, &Data), true);
						++Counts.m_Pickups;
						return;
					}

					if(EntityIndex == ENTITY_DOOR)
					{
						for(int Side = 0; Side < 8; ++Side)
						{
							const int Neighbor = pCollision->Entity(x + s_aSideOffsetX[Side], y + s_aSideOffsetY[Side], Layer);
							if(Neighbor < ENTITY_LASER_SHORT || Neighbor > ENTITY_LASER_LONG)
								continue;
							const int Length = 32 * 3 + 32 * (Neighbor - ENTITY_LASER_SHORT) * 3;
							const vec2 Dir(std::sin(pi / 4 * Side), std::cos(pi / 4 * Side));
							vec2 To = Pos + Dir * Length;
							pCollision->IntersectNoLaser(Pos, To, &To, nullptr);
							vec2 DoorHub = Pos;
							if(Options.m_pSnapshots != nullptr)
							{
								if(const SSnapshotLaser *pSnapshot = FindSnapshotDoor(
									   *Options.m_pSnapshots, Pos, Number, Dir))
								{
									DoorHub = pSnapshot->m_Hub;
									To = pSnapshot->m_From;
								}
							}
							CLaserData Data = {};
							Data.m_From = To;
							Data.m_To = DoorHub;
							Data.m_StartTick = -1;
							Data.m_ExtraInfo = true;
							Data.m_Owner = -1;
							Data.m_Type = LASERTYPE_DOOR;
							Data.m_SwitchNumber = Number;
							auto *pDoor = new CDoor(&World, NextEntityId++, &Data);
							if(!Options.m_RegisterDoorCollision)
								pDoor->PrepareRenderGeometry();
							World.InsertEntity(pDoor);
							++Counts.m_Doors;
						}
						return;
					}

					if(EntityIndex >= ENTITY_LASER_FAST_CCW && EntityIndex <= ENTITY_LASER_FAST_CW)
					{
						int aSides[8];
						int aModifiers[8];
						for(int Side = 0; Side < 8; ++Side)
						{
							aSides[Side] = pCollision->Entity(x + s_aSideOffsetX[Side], y + s_aSideOffsetY[Side], Layer);
							aModifiers[Side] = pCollision->Entity(x + 2 * s_aSideOffsetX[Side], y + 2 * s_aSideOffsetY[Side], Layer);
						}
						int Ind = EntityIndex - ENTITY_LASER_STOP;
						int DirectionSign = -1;
						if(Ind < 0)
						{
							Ind = -Ind;
							DirectionSign = 1;
						}
						else if(Ind == 0)
							DirectionSign = 0;
						float AngularSpeed = 0.0f;
						if(Ind == 1)
							AngularSpeed = pi / 360;
						else if(Ind == 2)
							AngularSpeed = pi / 180;
						else if(Ind == 3)
							AngularSpeed = pi / 90;
						AngularSpeed *= DirectionSign;
						for(int Side = 0; Side < 8; ++Side)
						{
							if(aSides[Side] < ENTITY_LASER_SHORT || aSides[Side] > ENTITY_LASER_LONG)
								continue;
							const int Length = 32 * 3 + 32 * (aSides[Side] - ENTITY_LASER_SHORT) * 3;
							const SSnapshotLaser *pSnapshot = nullptr;
							if(Options.m_InheritSnapshotLights && Options.m_pSnapshots != nullptr)
								pSnapshot = FindSnapshotLight(*Options.m_pSnapshots, Pos, Number,
									vec2(std::sin(pi / 4 * Side), std::cos(pi / 4 * Side)));
							const vec2 LightPos = pSnapshot != nullptr ? pSnapshot->m_Hub : Pos;
							auto *pLight = new CPracticeLight(&World, LightPos, pi / 4 * Side, Length, Layer, Number);
							bool FromSnapshot = false;
							if(pSnapshot != nullptr)
								FromSnapshot = pLight->InitFromSnapshot(pSnapshot->m_Hub, pSnapshot->m_From, pSnapshot->m_StartTick);
							pLight->m_AngularSpeed = AngularSpeed;
							if(aModifiers[Side] >= ENTITY_LASER_C_SLOW && aModifiers[Side] <= ENTITY_LASER_C_FAST)
							{
								pLight->m_Speed = 1 + (aModifiers[Side] - ENTITY_LASER_C_SLOW) * 2;
								if(!FromSnapshot)
									pLight->m_CurveLength = pLight->m_Length;
							}
							else if(aModifiers[Side] >= ENTITY_LASER_O_SLOW && aModifiers[Side] <= ENTITY_LASER_O_FAST)
							{
								pLight->m_Speed = 1 + (aModifiers[Side] - ENTITY_LASER_O_SLOW) * 2;
								if(!FromSnapshot)
									pLight->m_CurveLength = 0;
							}
							else if(!FromSnapshot)
								pLight->m_CurveLength = pLight->m_Length;
							if(!FromSnapshot)
								pLight->CapturePhaseOrigin();
							pLight->RefreshGeometry();
							++Counts.m_Lights;
						}
						return;
					}

					int Strength = 0;
					bool IgnoreWalls = false;
					if(EntityIndex >= ENTITY_DRAGGER_WEAK && EntityIndex <= ENTITY_DRAGGER_STRONG)
						Strength = EntityIndex - ENTITY_DRAGGER_WEAK + 1;
					else if(EntityIndex >= ENTITY_DRAGGER_WEAK_NW && EntityIndex <= ENTITY_DRAGGER_STRONG_NW)
					{
						Strength = EntityIndex - ENTITY_DRAGGER_WEAK_NW + 1;
						IgnoreWalls = true;
					}
					if(Strength <= 0)
						return;

					CLaserData Data = {};
					Data.m_From = Pos;
					if(Options.m_pSnapshots != nullptr)
					{
						if(const SSnapshotLaser *pSnapshot = FindNearestSnapshotLaser(
							   *Options.m_pSnapshots, LASERTYPE_DRAGGER, Pos, Number, 512.0f))
							Data.m_From = pSnapshot->m_Hub;
					}
					Data.m_To = Pos;
					Data.m_StartTick = -1;
					Data.m_ExtraInfo = true;
					Data.m_Owner = -1;
					Data.m_Type = LASERTYPE_DRAGGER;
					Data.m_SwitchNumber = Number;
					Data.m_Subtype = ((Strength - 1) << 1) | (IgnoreWalls ? 1 : 0);
					World.InsertEntity(new CDragger(&World, NextEntityId++, &Data));
					++Counts.m_Draggers;
				};

				if(pGame[TileIndex].m_Index >= ENTITY_OFFSET)
					HandleEntity(pGame[TileIndex].m_Index - ENTITY_OFFSET, LAYER_GAME, 0, pGame[TileIndex].m_Flags);
				if(pFront && pFront[TileIndex].m_Index >= ENTITY_OFFSET)
					HandleEntity(pFront[TileIndex].m_Index - ENTITY_OFFSET, LAYER_FRONT, 0, pFront[TileIndex].m_Flags);
				if(pSwitch && pSwitch[TileIndex].m_Type >= ENTITY_OFFSET)
					HandleEntity(pSwitch[TileIndex].m_Type - ENTITY_OFFSET, LAYER_SWITCH, pSwitch[TileIndex].m_Number, pSwitch[TileIndex].m_Flags);
			}
		}

		// 门碰撞只在可玩分支世界注册；影子世界只留渲染几何。
		if(Options.m_RegisterDoorCollision)
			World.ResetDoorCollision();
		return Counts;
	}

	void AdvanceMechanisms(CGameWorld &World, int Tick, int RenderTick)
	{
		for(CEntity *pEnt = World.FindFirst(CGameWorld::ENTTYPE_LIGHT); pEnt; pEnt = pEnt->TypeNext())
		{
			auto *pLight = dynamic_cast<CPracticeLight *>(pEnt);
			if(pLight != nullptr)
				pLight->SetPhaseTick(Tick, RenderTick);
		}
		for(CEntity *pEnt = World.FindFirst(CGameWorld::ENTTYPE_DRAGGER); pEnt; pEnt = pEnt->TypeNext())
		{
			auto *pDragger = dynamic_cast<CDragger *>(pEnt);
			if(pDragger != nullptr)
				pDragger->SetPlaybackTick(Tick);
		}
	}
}
