/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "layers.h"

#include "mapitems.h"

#include <engine/map.h>

CLayers::CLayers()
{
	Unload();
}

void CLayers::Init(IMap *pMap, bool GameOnly)
{
	Unload();

	m_pMap = pMap;
	m_pMap->GetType(MAPITEMTYPE_GROUP, &m_GroupsStart, &m_GroupsNum);
	m_pMap->GetType(MAPITEMTYPE_LAYER, &m_LayersStart, &m_LayersNum);

	for(int GroupIndex = 0; GroupIndex < NumGroups(); GroupIndex++)
	{
		CMapItemGroup *pGroup = GetGroup(GroupIndex);
		for(int LayerIndex = 0; LayerIndex < pGroup->m_NumLayers; LayerIndex++)
		{
			CMapItemLayer *pLayer = GetLayer(pGroup->m_StartLayer + LayerIndex);
			if(pLayer->m_Type != LAYERTYPE_TILES)
				continue;

			CMapItemLayerTilemap *pTilemap = reinterpret_cast<CMapItemLayerTilemap *>(pLayer);
			bool IsEntities = false;

			if(pTilemap->m_Flags & TILESLAYERFLAG_GAME)
			{
				m_pGameLayer = pTilemap;
				m_pGameGroup = pGroup;

				// make sure the game group has standard settings
				m_pGameGroup->m_OffsetX = 0;
				m_pGameGroup->m_OffsetY = 0;
				m_pGameGroup->m_ParallaxX = 100;
				m_pGameGroup->m_ParallaxY = 100;

				if(m_pGameGroup->m_Version >= 2)
				{
					m_pGameGroup->m_UseClipping = 0;
					m_pGameGroup->m_ClipX = 0;
					m_pGameGroup->m_ClipY = 0;
					m_pGameGroup->m_ClipW = 0;
					m_pGameGroup->m_ClipH = 0;
				}

				IsEntities = true;
			}

			if(!GameOnly)
			{
				if(pTilemap->m_Flags & TILESLAYERFLAG_TELE)
				{
					m_pTeleLayer = pTilemap;
					IsEntities = true;
				}

				if(pTilemap->m_Flags & TILESLAYERFLAG_SPEEDUP)
				{
					m_pSpeedupLayer = pTilemap;
					IsEntities = true;
				}

				if(pTilemap->m_Flags & TILESLAYERFLAG_FRONT)
				{
					m_pFrontLayer = pTilemap;
					IsEntities = true;
				}

				if(pTilemap->m_Flags & TILESLAYERFLAG_SWITCH)
				{
					m_pSwitchLayer = pTilemap;
					IsEntities = true;
				}

				if(pTilemap->m_Flags & TILESLAYERFLAG_TUNE)
				{
					m_pTuneLayer = pTilemap;
					IsEntities = true;
				}
			}

			if(IsEntities)
			{
				// Ensure default color for entities layers
				pTilemap->m_Color = CColor(255, 255, 255, 255);
			}
		}
	}

	InitTilemapSkip();
}

void CLayers::Unload()
{
	m_GroupsNum = 0;
	m_GroupsStart = 0;
	m_LayersNum = 0;
	m_LayersStart = 0;

	m_pGameGroup = nullptr;
	m_pGameLayer = nullptr;
	m_pMap = nullptr;

	m_pTeleLayer = nullptr;
	m_pSpeedupLayer = nullptr;
	m_pFrontLayer = nullptr;
	m_pSwitchLayer = nullptr;
	m_pTuneLayer = nullptr;
}

void CLayers::InitTilemapSkip()
{
	for(int GroupIndex = 0; GroupIndex < NumGroups(); GroupIndex++)
	{
		const CMapItemGroup *pGroup = GetGroup(GroupIndex);
		for(int LayerIndex = 0; LayerIndex < pGroup->m_NumLayers; LayerIndex++)
		{
			const CMapItemLayer *pLayer = GetLayer(pGroup->m_StartLayer + LayerIndex);
			if(pLayer->m_Type != LAYERTYPE_TILES)
				continue;

			const CMapItemLayerTilemap *pTilemap = reinterpret_cast<const CMapItemLayerTilemap *>(pLayer);
			// 除 game 层外的物理层把 tile 存在各自的数据索引里，其 m_Data 既不使用也不在地图加载时校验。
			if((pTilemap->m_Flags & (TILESLAYERFLAG_TELE | TILESLAYERFLAG_SPEEDUP | TILESLAYERFLAG_FRONT | TILESLAYERFLAG_SWITCH | TILESLAYERFLAG_TUNE)) != 0)
				continue;

			CTile *pTiles = static_cast<CTile *>(m_pMap->GetData(pTilemap->m_Data));
			if(pTiles == nullptr || pTilemap->m_Width < 0 || pTilemap->m_Height < 0 ||
				(int64_t)pTilemap->m_Width * pTilemap->m_Height > m_pMap->GetDataSize(pTilemap->m_Data) / (int)sizeof(CTile))
			{
				continue;
			}

			for(int y = 0; y < pTilemap->m_Height; y++)
			{
				for(int x = 1; x < pTilemap->m_Width;)
				{
					int SkippedX;
					for(SkippedX = 1; x + SkippedX < pTilemap->m_Width && SkippedX < 255; SkippedX++)
					{
						if(pTiles[y * pTilemap->m_Width + x + SkippedX].m_Index)
							break;
					}

					pTiles[y * pTilemap->m_Width + x].m_Skip = SkippedX - 1;
					x += SkippedX;
				}
			}
		}
	}
}

CMapItemGroup *CLayers::GetGroup(int Index) const
{
	return static_cast<CMapItemGroup *>(m_pMap->GetItem(m_GroupsStart + Index));
}

CMapItemLayer *CLayers::GetLayer(int Index) const
{
	return static_cast<CMapItemLayer *>(m_pMap->GetItem(m_LayersStart + Index));
}
