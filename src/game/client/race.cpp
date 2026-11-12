#include "race.h"

#include <game/client/components/qmclient/race_start_detection.h>
#include <game/client/gameclient.h>
#include <game/collision.h>
#include <game/mapitems.h>

void CRaceHelper::Init(const CGameClient *pGameClient)
{
	m_pGameClient = pGameClient;

	m_aFlagIndex[TEAM_RED] = -1;
	m_aFlagIndex[TEAM_BLUE] = -1;

	const CTile *pGameTiles = m_pGameClient->Collision()->GameLayer();
	const int MapSize = m_pGameClient->Collision()->GetWidth() * m_pGameClient->Collision()->GetHeight();
	for(int Index = 0; Index < MapSize; Index++)
	{
		const int EntityIndex = pGameTiles[Index].m_Index - ENTITY_OFFSET;
		if(EntityIndex == ENTITY_FLAGSTAND_RED)
		{
			m_aFlagIndex[TEAM_RED] = Index;
			if(m_aFlagIndex[TEAM_BLUE] != -1)
				break; // Found both flags
		}
		else if(EntityIndex == ENTITY_FLAGSTAND_BLUE)
		{
			m_aFlagIndex[TEAM_BLUE] = Index;
			if(m_aFlagIndex[TEAM_RED] != -1)
				break; // Found both flags
		}
		Index += pGameTiles[Index].m_Skip;
	}
}

bool CRaceHelper::IsStart(vec2 Prev, vec2 Pos) const
{
	if(m_pGameClient->m_GameInfo.m_FlagStartsRace)
	{
		int EnemyTeam = m_pGameClient->m_aClients[m_pGameClient->m_Snap.m_LocalClientId].m_Team ^ 1;
		return m_aFlagIndex[EnemyTeam] != -1 && distance(Pos, m_pGameClient->Collision()->GetPos(m_aFlagIndex[EnemyTeam])) < 32;
	}
	else
	{
		return qm_race_start::IsTileStart(*m_pGameClient->Collision(), Prev, Pos);
	}
}
