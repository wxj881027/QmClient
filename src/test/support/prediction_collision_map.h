#ifndef TEST_SUPPORT_PREDICTION_COLLISION_MAP_H
#define TEST_SUPPORT_PREDICTION_COLLISION_MAP_H

#include <engine/map.h>

#include <game/mapitems.h>

#include <array>

// 仅替换已解码地图的存储接口；分层、碰撞初始化和门写回均调用生产实现。
class CPredictionCollisionMap : public IMap
{
	CMapItemGroup m_Group{};
	CMapItemLayerTilemap m_Game{};
	CMapItemLayerTilemap m_Switch{};
	std::array<CTile, 16 * 16> m_aGameTiles{};
	std::array<CSwitchTile, 16 * 16> m_aSwitchTiles{};

public:
	CPredictionCollisionMap()
	{
		m_Group.m_Version = 3;
		m_Group.m_NumLayers = 2;
		m_Group.m_ParallaxX = m_Group.m_ParallaxY = 100;
		m_Game.m_Layer.m_Type = LAYERTYPE_TILES;
		m_Game.m_Version = 3;
		m_Game.m_Flags = TILESLAYERFLAG_GAME;
		m_Game.m_Width = m_Game.m_Height = 16;
		m_Game.m_Data = 0;
		m_Switch = m_Game;
		m_Switch.m_Flags = TILESLAYERFLAG_SWITCH;
		m_Switch.m_Data = -1;
		m_Switch.m_Switch = 1;
		m_aSwitchTiles.back().m_Type = TILE_SWITCHOPEN;
		m_aSwitchTiles.back().m_Number = 3;
	}
	void SetGameTile(int X, int Y, int Index)
	{
		m_aGameTiles[Y * 16 + X].m_Index = static_cast<unsigned char>(Index);
	}

	int GetDataSize(int Index) const override
	{
		return Index == 0 ? static_cast<int>(sizeof(m_aGameTiles)) : (Index == 1 ? static_cast<int>(sizeof(m_aSwitchTiles)) : 0);
	}
	void *GetData(int Index) override
	{
		if(Index == 0)
			return m_aGameTiles.data();
		return Index == 1 ? m_aSwitchTiles.data() : nullptr;
	}
	void *GetDataSwapped(int Index) override { return GetData(Index); }
	const char *GetDataString(int) override { return nullptr; }
	void UnloadData(int) override {}
	int NumData() const override { return 2; }
	int GetItemSize(int Index) override
	{
		return Index == 0 ? static_cast<int>(sizeof(m_Group)) : (Index == 1 || Index == 2 ? static_cast<int>(sizeof(m_Game)) : 0);
	}
	void *GetItem(int Index, int *pType = nullptr, int *pId = nullptr) override
	{
		if(pType)
			*pType = Index == 0 ? MAPITEMTYPE_GROUP : MAPITEMTYPE_LAYER;
		if(pId)
			*pId = Index == 0 ? 0 : Index - 1;
		if(Index == 0)
			return &m_Group;
		if(Index == 1)
			return &m_Game;
		return Index == 2 ? &m_Switch : nullptr;
	}
	void GetType(int Type, int *pStart, int *pNum) override
	{
		*pStart = Type == MAPITEMTYPE_LAYER ? 1 : 0;
		*pNum = Type == MAPITEMTYPE_GROUP ? 1 : (Type == MAPITEMTYPE_LAYER ? 2 : 0);
	}
	int FindItemIndex(int Type, int Id) override
	{
		if(Type == MAPITEMTYPE_GROUP && Id == 0)
			return 0;
		return Type == MAPITEMTYPE_LAYER && Id >= 0 && Id < 2 ? 1 + Id : -1;
	}
	void *FindItem(int Type, int Id) override { return GetItem(FindItemIndex(Type, Id)); }
	int NumItems() const override { return 3; }
};

#endif
