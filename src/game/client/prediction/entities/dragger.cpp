/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#include "dragger.h"

#include "character.h"
#include "door.h"

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/components/qmclient/collision_position.h>
#include <game/client/laser_data.h>
#include <game/collision.h>
#include <game/mapitems.h>

void CDragger::Tick()
{
	if(!QmCollision::CanRoundPosition(m_Pos))
	{
		DraggerBeamReset();
		return;
	}
	if(GameWorld()->GameTick() % (int)(GameWorld()->GameTickSpeed() * 0.15f) == 0)
	{
		Collision()->MoverSpeed(m_Pos.x, m_Pos.y, &m_Core);
		const vec2 NextPos = m_Pos + m_Core;
		if(!QmCollision::CanRoundPosition(NextPos))
		{
			DraggerBeamReset();
			return;
		}
		m_Pos = NextPos;

		LookForPlayersToDrag();
	}

	DraggerBeamTick();
}

void CDragger::LookForPlayersToDrag()
{
	// Create a list of players who are in the range of the dragger
	CEntity *apPlayersInRange[MAX_CLIENTS];
	std::fill(std::begin(apPlayersInRange), std::end(apPlayersInRange), nullptr);

	int NumPlayersInRange = GameWorld()->FindEntities(m_Pos,
		g_Config.m_SvDraggerRange - CCharacterCore::PhysicalSize(),
		apPlayersInRange, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

	// The closest player (within range) in a team is selected as the target
	int ClosestTargetId = -1;
	bool CanStillBeTeamTarget = false;
	int MinDistInTeam = 0;

	for(int i = 0; i < NumPlayersInRange; i++)
	{
		CCharacter *pTarget = static_cast<CCharacter *>(apPlayersInRange[i]);
		const int &TargetTeam = pTarget->Team();

		// Do not create a dragger beam for super player
		if(TargetTeam == pTarget->TeamsCore()->TeamSuper())
		{
			continue;
		}
		// If the dragger is disabled for the target's team, no dragger beam will be generated
		if(m_Layer == LAYER_SWITCH &&
			m_Number > 0 &&
			m_Number < (int)Switchers().size() &&
			!Switchers()[m_Number].m_aStatus[TargetTeam])
		{
			continue;
		}

		// Dragger beams can be created only for reachable, alive players
		if(!QmCollision::CanQueryLine(m_Pos, pTarget->m_Pos))
			continue;
		int IsReachable =
			m_IgnoreWalls ?
				!Collision()->IntersectNoLaserNoWalls(m_Pos, pTarget->m_Pos, nullptr, nullptr) :
				!Collision()->IntersectNoLaser(m_Pos, pTarget->m_Pos, nullptr, nullptr);
		if(IsReachable)
		{
			const int &TargetClientId = pTarget->GetCid();
			int Distance = distance(pTarget->m_Pos, m_Pos);
			if(MinDistInTeam == 0 || MinDistInTeam > Distance)
			{
				MinDistInTeam = Distance;
				ClosestTargetId = TargetClientId;
			}
			if(TargetClientId == m_TargetId)
			{
				CanStillBeTeamTarget = true;
			}
		}
	}

	// Set the closest player for each team as a target if the team does not have a target player yet
	if((m_TargetId != -1 && !CanStillBeTeamTarget) || m_TargetId == -1)
	{
		m_TargetId = ClosestTargetId;
	}
}

void CDragger::DraggerBeamReset()
{
	m_TargetId = -1;
}

void CDragger::DraggerBeamTick()
{
	CCharacter *pTarget = GameWorld()->GetCharacterById(m_TargetId);
	if(!pTarget)
	{
		DraggerBeamReset();
		return;
	}

	if(GameWorld()->GameTick() % (int)(GameWorld()->GameTickSpeed() * 0.15f) == 0)
	{
		if(m_Layer == LAYER_SWITCH &&
			m_Number > 0 &&
			m_Number < (int)Switchers().size() &&
			!Switchers()[m_Number].m_aStatus[pTarget->Team()])
		{
			DraggerBeamReset();
			return;
		}
	}

	// When the dragger can no longer reach the target player, the dragger beam dissolves
	if(distance(pTarget->m_Pos, m_Pos) >= g_Config.m_SvDraggerRange ||
		!QmCollision::CanQueryLine(m_Pos, pTarget->m_Pos) ||
		(m_IgnoreWalls ?
				Collision()->IntersectNoLaserNoWalls(m_Pos, pTarget->m_Pos, nullptr, nullptr) :
				Collision()->IntersectNoLaser(m_Pos, pTarget->m_Pos, nullptr, nullptr)))
	{
		DraggerBeamReset();
		return;
	}
	// In the center of the dragger a tee does not experience speed-up
	else if(distance(pTarget->m_Pos, m_Pos) > 28)
	{
		pTarget->AddVelocity(normalize(m_Pos - pTarget->m_Pos) * m_Strength);
	}
}

CDragger::CDragger(CGameWorld *pGameWorld, int Id, const CLaserData *pData) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_DRAGGER)
{
	m_Core = vec2(0.f, 0.f);
	m_Id = Id;
	m_TargetId = -1;
	m_Tick = std::max(1, (int)(pGameWorld->GameTickSpeed() * 0.15f));

	m_Strength = 0;
	m_IgnoreWalls = false;
	if(0 <= pData->m_Subtype && pData->m_Subtype < NUM_LASERDRAGGERTYPES)
	{
		m_IgnoreWalls = (pData->m_Subtype & 1);
		m_Strength = (pData->m_Subtype >> 1) + 1;
	}
	m_Number = pData->m_SwitchNumber;
	m_Layer = m_Number > 0 ? LAYER_SWITCH : LAYER_GAME;

	Read(pData);
	m_PlaybackStartPos = m_Pos;
	m_PlaybackTick = 0;
}

void CDragger::Read(const CLaserData *pData)
{
	m_Pos = pData->m_From;
	m_TargetId = pData->m_Owner;
}

void CDragger::SetPlaybackTick(int Tick)
{
	if(Tick < m_PlaybackTick)
	{
		// 拖动回退：回到地图初始位置后重放到目标 tick。
		m_Pos = m_PlaybackStartPos;
		m_Core = vec2(0.f, 0.f);
		m_PlaybackTick = 0;
		m_TargetId = -1;
	}
	while(m_PlaybackTick + m_Tick <= Tick)
	{
		m_PlaybackTick += m_Tick;
		Collision()->MoverSpeed(m_Pos.x, m_Pos.y, &m_Core);
		m_Pos += m_Core;
	}
}

bool CDragger::Match(CDragger *pDragger)
{
	return pDragger->m_Strength == m_Strength && pDragger->m_Number == m_Number && pDragger->m_IgnoreWalls == m_IgnoreWalls;
}

std::optional<CLaserData> CDragger::RenderData(int Team)
{
	const auto &vSwitchers = GameWorld()->Switchers();
	const bool IsSwitchLayer = m_Layer == LAYER_SWITCH;
	const bool SwitchStatus = IsSwitchLayer && m_Number > 0 && m_Number < (int)vSwitchers.size() &&
				  Team >= 0 && Team < NUM_DDRACE_TEAMS && vSwitchers[m_Number].m_aStatus[Team];
	if(!QmPracticeSwitchActive(IsSwitchLayer, m_Number, Team, (int)vSwitchers.size(), SwitchStatus))
		return std::nullopt;

	CCharacter *pTarget = m_TargetId >= 0 ? GameWorld()->GetCharacterById(m_TargetId) : nullptr;
	CLaserData Data = {};
	Data.m_From = m_Pos;
	Data.m_To = pTarget != nullptr ? pTarget->m_Pos : m_Pos;
	Data.m_StartTick = GameWorld()->GameTick();
	Data.m_ExtraInfo = true;
	Data.m_Owner = pTarget != nullptr ? pTarget->GetCid() : -1;
	Data.m_Type = LASERTYPE_DRAGGER;
	Data.m_SwitchNumber = m_Number;
	Data.m_Subtype = QmPracticeDraggerSubtype(m_Strength, m_IgnoreWalls);
	Data.m_Predict = false;
	Data.m_TuneZone = GameWorld()->m_WorldConfig.m_UseTuneZones ? Collision()->IsTune(Collision()->GetMapIndex(m_Pos)) : 0;
	return Data;
}
