#include "outlines.h"

#include <base/log.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/animstate.h>
#include <game/client/components/tclient/qm_outline_neighbors.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/mapitems.h>

#include <limits>

// The order of this is the order of priority for outlines
enum
{
	OUTLINE_NONE = 0,
	OUTLINE_UNFREEZE,
	OUTLINE_DEEPUNFREEZE,
	OUTLINE_FREEZE,
	OUTLINE_DEEPFREEZE,
	OUTLINE_TELE,
	OUTLINE_KILL,
	OUTLINE_SOLID,
};

// NOLINTNEXTLINE(misc-use-internal-linkage)
enum class OutlineLayer
{
	GAME,
	FRONT,
	TELE
};

// NOLINTNEXTLINE(misc-use-internal-linkage)
class COutLineLayer
{
private:
	CMapItemLayerTilemap *GetLayer(CGameClient *pThis) const
	{
		switch(m_Type)
		{
		case OutlineLayer::GAME:
			return pThis->Layers()->GameLayer();
		case OutlineLayer::FRONT:
			return pThis->Layers()->FrontLayer();
		case OutlineLayer::TELE:
			return pThis->Layers()->TeleLayer();
		default:
			return nullptr;
		}
	}
	int GetLayerData(CGameClient *pThis) const
	{
		switch(m_Type)
		{
		case OutlineLayer::GAME:
			return pThis->Layers()->GameLayer()->m_Data;
		case OutlineLayer::FRONT:
			return pThis->Layers()->FrontLayer()->m_Front;
		case OutlineLayer::TELE:
			return pThis->Layers()->TeleLayer()->m_Tele;
		default:
			return -1;
		}
	}

public:
	const OutlineLayer m_Type;
	void GetMeta(CGameClient *pThis, ivec2 &Size) const
	{
		Size = {0, 0};
		const auto *pLayer = GetLayer(pThis);
		if(!pLayer)
			return;
		const size_t TileSize = m_Type == OutlineLayer::TELE ? sizeof(CTeleTile) : sizeof(CTile);
		const int DataSize = pThis->Layers()->Map()->GetDataSize(GetLayerData(pThis));
		if(DataSize <= 0 || (size_t)DataSize < (size_t)pLayer->m_Width * (size_t)pLayer->m_Height * TileSize)
			return;
		Size = {pLayer->m_Width, pLayer->m_Height};
	}
	void SetData(CGameClient *pThis, int *pData, const ivec2 &Size) const
	{
		const auto *pLayer = GetLayer(pThis);
		if(!pLayer)
			return;
		const auto *pTiles = (CTile *)pThis->Layers()->Map()->GetData(GetLayerData(pThis));
		if(!pTiles)
			return;
		for(int y = 0; y < pLayer->m_Height; ++y)
		{
			for(int x = 0; x < pLayer->m_Width; ++x)
			{
				const int Index = y * pLayer->m_Width + x;
				const int IndexOut = y * Size.x + x;
				if(m_Type == OutlineLayer::TELE)
				{
					const auto &Tile = ((CTeleTile *)pTiles)[Index];
					if(Tile.m_Number != 0 && Tile.m_Type != 0)
						pData[IndexOut] = OUTLINE_TELE;
				}
				else
				{
					const auto Tile = pTiles[Index].m_Index;
					if(Tile == TILE_SOLID || Tile == TILE_NOHOOK)
						pData[IndexOut] = OUTLINE_SOLID;
					else if(Tile == TILE_DFREEZE)
						pData[IndexOut] = OUTLINE_DEEPFREEZE;
					else if(Tile == TILE_FREEZE || Tile == TILE_LFREEZE)
						pData[IndexOut] = OUTLINE_FREEZE;
					else if(Tile == TILE_DUNFREEZE)
						pData[IndexOut] = OUTLINE_DEEPUNFREEZE;
					else if(Tile == TILE_UNFREEZE || Tile == TILE_LUNFREEZE)
						pData[IndexOut] = OUTLINE_UNFREEZE;
					else if(Tile == TILE_DEATH)
						pData[IndexOut] = OUTLINE_KILL;
				}
			}
		}
	}
};

// The order of this determines order of priority into the one map (tele + freeze = tele)
static constexpr COutLineLayer OUTLINE_LAYERS[] = {{OutlineLayer::TELE}, {OutlineLayer::GAME}, {OutlineLayer::FRONT}};
static constexpr size_t MAX_OUTLINE_MAP_TILES = 4096 * 4096;

void COutlines::OnMapLoad()
{
	m_vMapData.clear();

	// Find valid layers and size
	std::vector<const COutLineLayer *> vValidOutlineLayers;
	m_MapDataSize = {0, 0};
	for(const auto &Layer : OUTLINE_LAYERS)
	{
		ivec2 LayerSize;
		Layer.GetMeta(GameClient(), LayerSize);
		if(LayerSize.x <= 0 || LayerSize.y <= 0)
			continue;
		m_MapDataSize.x = std::max(m_MapDataSize.x, LayerSize.x);
		m_MapDataSize.y = std::max(m_MapDataSize.y, LayerSize.y);
		vValidOutlineLayers.push_back(&Layer);
	}
	if(m_MapDataSize.x <= 0 || m_MapDataSize.y <= 0)
		return;

	const size_t MapWidth = (size_t)m_MapDataSize.x;
	const size_t MapHeight = (size_t)m_MapDataSize.y;
	if(MapWidth > std::numeric_limits<size_t>::max() / MapHeight)
	{
		log_warn("outlines", "Map size overflow for outline cache: %dx%d", m_MapDataSize.x, m_MapDataSize.y);
		m_MapDataSize = {0, 0};
		return;
	}
	const size_t NumTiles = MapWidth * MapHeight;
	if(NumTiles > MAX_OUTLINE_MAP_TILES)
	{
		log_warn("outlines", "Map too large for outline cache: %dx%d", m_MapDataSize.x, m_MapDataSize.y);
		m_MapDataSize = {0, 0};
		return;
	}

	m_vMapData.assign(NumTiles, OUTLINE_NONE);

	// Do it
	for(const auto *pLayer : vValidOutlineLayers)
	{
		pLayer->SetData(GameClient(), m_vMapData.data(), m_MapDataSize);
	}
}

void COutlines::OnRender()
{
	if(m_vMapData.empty())
		return;
	if(GameClient()->m_MapLayersBackground.m_OnlineOnly && Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(!g_Config.m_ClOverlayEntities && g_Config.m_TcOutlineEntities)
		return;
	if(!g_Config.m_TcOutline)
		return;

	const float Scale = 32.0f;

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	int StartY = (int)(ScreenY0 / Scale) - 1;
	int StartX = (int)(ScreenX0 / Scale) - 1;
	int EndY = (int)(ScreenY1 / Scale) + 1;
	int EndX = (int)(ScreenX1 / Scale) + 1;
	int MaxScale = 12;
	if(EndX - StartX > Graphics()->ScreenWidth() / MaxScale || EndY - StartY > Graphics()->ScreenHeight() / MaxScale)
	{
		int EdgeX = (EndX - StartX) - (Graphics()->ScreenWidth() / MaxScale);
		StartX += EdgeX / 2;
		EndX -= EdgeX / 2;
		int EdgeY = (EndY - StartY) - (Graphics()->ScreenHeight() / MaxScale);
		StartY += EdgeY / 2;
		EndY -= EdgeY / 2;
	}

	auto GetTile = [&](int x, int y) {
		x = std::clamp(x, 0, m_MapDataSize.x - 1);
		y = std::clamp(y, 0, m_MapDataSize.y - 1);
		// 必须掩掉高位：邻接缓存把 8 邻域位写在 tile 高位（见 qm_outline_neighbors.h），
		// 不掩码会让 `GetTile(...) >= Type` 的邻域比较被缓存位污染。
		return m_vMapData[y * m_MapDataSize.x + x] & 7;
	};

	// 配置与颜色只随帧变化，不随 tile 变化。
	struct COutlineConfig
	{
		int m_Enable;
		int m_Width;
		unsigned int m_Color;
	};
	const COutlineConfig aConfigs[] = {
		{0, 0, 0},
		{g_Config.m_TcOutlineUnfreeze, g_Config.m_TcOutlineWidthUnfreeze, g_Config.m_TcOutlineColorUnfreeze},
		{g_Config.m_TcOutlineUnfreeze, g_Config.m_TcOutlineWidthUnfreeze, g_Config.m_TcOutlineColorDeepUnfreeze},
		{g_Config.m_TcOutlineFreeze, g_Config.m_TcOutlineWidthFreeze, g_Config.m_TcOutlineColorFreeze},
		{g_Config.m_TcOutlineFreeze, g_Config.m_TcOutlineWidthFreeze, g_Config.m_TcOutlineColorDeepFreeze},
		{g_Config.m_TcOutlineTele, g_Config.m_TcOutlineWidthTele, g_Config.m_TcOutlineColorTele},
		{g_Config.m_TcOutlineKill, g_Config.m_TcOutlineWidthKill, g_Config.m_TcOutlineColorKill},
		{g_Config.m_TcOutlineSolid, g_Config.m_TcOutlineWidthSolid, g_Config.m_TcOutlineColorSolid},
	};
	ColorRGBA aColors[OUTLINE_SOLID + 1];
	for(int Type = OUTLINE_NONE; Type <= OUTLINE_SOLID; ++Type)
	{
		aColors[Type] = color_cast<ColorRGBA>(ColorHSLA(aConfigs[Type].m_Color, true));
		aColors[Type].a *= g_Config.m_TcOutlineAlpha / 100.0f;
	}
	aColors[OUTLINE_SOLID].a *= g_Config.m_TcOutlineSolidAlpha / 100.0f;

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();

	for(int y = StartY; y < EndY; y++)
	{
		for(int x = StartX; x < EndX; x++)
		{
			const int Type = GetTile(x, y);
			if(Type == OUTLINE_NONE)
				continue;
			if(Type < OUTLINE_NONE || Type > OUTLINE_SOLID)
			{
				static bool s_InvalidOutlineTypeWarned = false;
				if(!s_InvalidOutlineTypeWarned)
				{
					s_InvalidOutlineTypeWarned = true;
					log_warn("outlines", "Invalid outline type %d at %d,%d on %dx%d map", Type, x, y, m_MapDataSize.x, m_MapDataSize.y);
				}
				continue;
			}
			const COutlineConfig &Config = aConfigs[Type];
			const ColorRGBA &OutlineColor = aColors[Type];
			if(!Config.m_Enable || Config.m_Width <= 0 || OutlineColor.a <= 0.0f)
				continue;
			// Find neighbours：8 邻域位缓存在 tile 高位（低三位仍是地图类型），避免逐帧重算。
			int &Tile = m_vMapData[std::clamp(y, 0, m_MapDataSize.y - 1) * m_MapDataSize.x + std::clamp(x, 0, m_MapDataSize.x - 1)];
			const int Neighbors = QmOutlineCachedNeighbors(Tile, x, y, m_MapDataSize.x, m_MapDataSize.y, GetTile);
			bool aNeighbors[8];
			for(int i = 0; i < 8; ++i)
				aNeighbors[i] = (Neighbors & (1 << i)) != 0;
			// Figure out edges
			IGraphics::CQuadItem aQuads[8];
			int NumQuads = 0;
			// Lone corners first
			if(!aNeighbors[0] && aNeighbors[1] && aNeighbors[3])
				aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale, Config.m_Width, Config.m_Width);
			if(!aNeighbors[2] && aNeighbors[1] && aNeighbors[4])
				aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale + Scale - Config.m_Width, y * Scale, Config.m_Width, Config.m_Width);
			if(!aNeighbors[5] && aNeighbors[3] && aNeighbors[6])
				aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale + Scale - Config.m_Width, Config.m_Width, Config.m_Width);
			if(!aNeighbors[7] && aNeighbors[6] && aNeighbors[4])
				aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale + Scale - Config.m_Width, y * Scale + Scale - Config.m_Width, Config.m_Width, Config.m_Width);
			// Top
			if(!aNeighbors[1])
				aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale, Scale, Config.m_Width);
			// Bottom
			if(!aNeighbors[6])
				aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale + Scale - Config.m_Width, Scale, Config.m_Width);
			// Left
			if(!aNeighbors[3])
			{
				if(aNeighbors[1] && aNeighbors[6])
					aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale, Config.m_Width, Scale);
				else if(aNeighbors[6])
					aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale + Config.m_Width, Config.m_Width, Scale - Config.m_Width);
				else if(aNeighbors[1])
					aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale, Config.m_Width, Scale - Config.m_Width);
				else
					aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale + Config.m_Width, Config.m_Width, Scale - Config.m_Width * 2.0f);
			}
			// Right
			if(!aNeighbors[4])
			{
				if(aNeighbors[1] && aNeighbors[6])
					aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale + Scale - Config.m_Width, y * Scale, Config.m_Width, Scale);
				else if(aNeighbors[6])
					aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale + Scale - Config.m_Width, y * Scale + Config.m_Width, Config.m_Width, Scale - Config.m_Width);
				else if(aNeighbors[1])
					aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale + Scale - Config.m_Width, y * Scale, Config.m_Width, Scale - Config.m_Width);
				else
					aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale + Scale - Config.m_Width, y * Scale + Config.m_Width, Config.m_Width, Scale - Config.m_Width * 2.0f);
			}
			if(NumQuads <= 0)
				continue;

			Graphics()->SetColor(OutlineColor);
			Graphics()->QuadsDrawTL(aQuads, NumQuads);
		}
	}

	Graphics()->QuadsEnd();
}
