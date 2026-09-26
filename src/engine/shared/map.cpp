/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "map.h"

#include <base/log.h>
#include <base/mem.h>
#include <base/str.h>

#include <engine/storage.h>

#include <game/gamecore.h>
#include <game/mapitems.h>

CMap::CMap() = default;

int CMap::GetDataSize(int Index) const
{
	return m_DataFile.GetDataSize(Index);
}

void *CMap::GetData(int Index)
{
	return m_DataFile.GetData(Index);
}

void *CMap::GetDataSwapped(int Index)
{
	return m_DataFile.GetDataSwapped(Index);
}

const char *CMap::GetDataString(int Index)
{
	return m_DataFile.GetDataString(Index);
}

void CMap::UnloadData(int Index)
{
	m_DataFile.UnloadData(Index);
}

int CMap::NumData() const
{
	return m_DataFile.NumData();
}

int CMap::GetItemSize(int Index)
{
	return m_DataFile.GetItemSize(Index);
}

void *CMap::GetItem(int Index, int *pType, int *pId)
{
	return m_DataFile.GetItem(Index, pType, pId);
}

void CMap::GetType(int Type, int *pStart, int *pNum)
{
	m_DataFile.GetType(Type, pStart, pNum);
}

int CMap::FindItemIndex(int Type, int Id)
{
	return m_DataFile.FindItemIndex(Type, Id);
}

void *CMap::FindItem(int Type, int Id)
{
	return m_DataFile.FindItem(Type, Id);
}

int CMap::NumItems() const
{
	return m_DataFile.NumItems();
}

bool CMap::Load(const char *pMapName, int StorageType)
{
	IStorage *pStorage = Kernel()->RequestInterface<IStorage>();
	if(!pStorage)
		return false;

	// 避免加载失败时留下不一致的 datafile，先单独加载新 datafile
	CDataFileReader NewDataFile;
	if(!NewDataFile.Open(pStorage, pMapName, StorageType))
		return false;

	if(!ValidateMapVersion(NewDataFile))
	{
		NewDataFile.Close();
		return false;
	}

	int GroupsStart, GroupsNum, LayersStart, LayersNum;
	NewDataFile.GetType(MAPITEMTYPE_GROUP, &GroupsStart, &GroupsNum);
	NewDataFile.GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);

	// 将旧版本的地图条目替换为与最新版本兼容的条目，避免使用地图条目时做版本检查。
	// 确保存在 game 层与 game group。
	const CMapItemLayerTilemap *pGameLayer = nullptr;
	std::set<int> UsedLayerItemIndices;
	for(int GroupIndex = 0; GroupIndex < GroupsNum; GroupIndex++)
	{
		const size_t GroupItemSize = NewDataFile.GetItemSize(GroupsStart + GroupIndex);
		if(GroupItemSize < sizeof(CMapItemGroup_v1))
		{
			log_error("map/load", "Group %d is truncated (size %" PRIzu ").", GroupIndex, GroupItemSize);
			return false;
		}
		const CMapItemGroup *pGroup = static_cast<CMapItemGroup *>(NewDataFile.GetItem(GroupsStart + GroupIndex));
		if(pGroup->m_StartLayer < 0 || pGroup->m_NumLayers < 0 ||
			(int64_t)pGroup->m_StartLayer + pGroup->m_NumLayers > LayersNum)
		{
			log_error("map/load", "Group %d uses invalid layers %d to %d (the map contains %d layers).",
				GroupIndex, pGroup->m_StartLayer, pGroup->m_StartLayer + pGroup->m_NumLayers - 1, LayersNum);
			return false;
		}
		for(int LayerIndex = 0; LayerIndex < pGroup->m_NumLayers; LayerIndex++)
		{
			const int LayerItemIndex = LayersStart + pGroup->m_StartLayer + LayerIndex;
			const auto &[_, LayerUnique] = UsedLayerItemIndices.emplace(LayerItemIndex);
			if(!LayerUnique)
			{
				log_error("map/load", "Layer %d in group %d is also being used by another group.", LayerIndex, GroupIndex);
				return false;
			}
			CMapItemLayer *pLayer = static_cast<CMapItemLayer *>(NewDataFile.GetItem(LayerItemIndex));
			const size_t LayerItemSize = NewDataFile.GetItemSize(LayerItemIndex);
			if(LayerItemSize < sizeof(CMapItemLayer))
			{
				log_error("map/load", "Layer %d in group %d is truncated (size %" PRIzu ").", LayerIndex, GroupIndex, LayerItemSize);
				return false;
			}

			if(pLayer->m_Version != 0)
			{
				log_debug("map/load", "Layer %d in group %d has unused version set to %d. Resetting to 0.", LayerIndex, GroupIndex, pLayer->m_Version);
				pLayer->m_Version = 0;
			}

			if(pLayer->m_Type == LAYERTYPE_TILES)
			{
				if(!UpgradeAndValidateTilesLayerItem(NewDataFile, GroupIndex, LayerIndex, reinterpret_cast<CMapItemLayerTilemap *>(pLayer), LayerItemIndex, LayerItemSize))
				{
					return false;
				}
				// 条目可能已被替换，因此必须重新获取指针。
				const CMapItemLayerTilemap *pLayerTilemap = static_cast<CMapItemLayerTilemap *>(NewDataFile.GetItem(LayerItemIndex));
				if(pLayerTilemap->m_Flags & TILESLAYERFLAG_GAME)
				{
					pGameLayer = pLayerTilemap;
				}
			}
		}
	}
	if(pGameLayer == nullptr)
	{
		log_error("map/load", "Game layer is missing.");
		return false;
	}

	// 惰性校验数据并把压缩的 tile 层替换为未压缩版本。
	std::set<int> UsedDataIndices;
	for(int GroupIndex = 0; GroupIndex < GroupsNum; GroupIndex++)
	{
		const CMapItemGroup *pGroup = static_cast<CMapItemGroup *>(NewDataFile.GetItem(GroupsStart + GroupIndex));
		for(int LayerIndex = 0; LayerIndex < pGroup->m_NumLayers; LayerIndex++)
		{
			CMapItemLayer *pLayer = static_cast<CMapItemLayer *>(NewDataFile.GetItem(LayersStart + pGroup->m_StartLayer + LayerIndex));
			if(pLayer->m_Type == LAYERTYPE_TILES)
			{
				if(!ValidateAndUnpackTilesLayerData(NewDataFile, GroupIndex, LayerIndex, reinterpret_cast<const CMapItemLayerTilemap *>(pLayer), *pGameLayer, UsedDataIndices))
				{
					return false;
				}
			}
		}
	}

	// 立即加载并隐式校验 game 层 tile 数据，因为后续必然用到。
	// 不预加载其他数据以避免过多内存占用。
	if(NewDataFile.GetData(pGameLayer->m_Data) == nullptr)
	{
		log_error("map/load", "Game layer data is invalid.");
		return false;
	}

	// 用新 datafile 替换现有 datafile
	m_DataFile.Close();
	m_DataFile = std::move(NewDataFile);
	return true;
}

void CMap::Adopt(CMap &&Other)
{
	m_DataFile = std::move(Other.m_DataFile);
}

void CMap::Unload()
{
	m_DataFile.Close();
}

bool CMap::IsLoaded() const
{
	return m_DataFile.IsOpen();
}

IOHANDLE CMap::File() const
{
	if(!IsLoaded())
		return nullptr;
	return m_DataFile.File();
}

SHA256_DIGEST CMap::Sha256() const
{
	if(!IsLoaded())
		return SHA256_DIGEST{};
	return m_DataFile.Sha256();
}

unsigned CMap::Crc() const
{
	if(!IsLoaded())
		return 0;
	return m_DataFile.Crc();
}

int CMap::Size() const
{
	if(!IsLoaded())
		return 0;
	return m_DataFile.Size();
}

bool CMap::ExtractTiles(CTile *pDest, size_t DestSize, const CTile *pSrc, size_t SrcSize)
{
	size_t DestIndex = 0;
	size_t SrcIndex = 0;
	while(DestIndex < DestSize && SrcIndex < SrcSize)
	{
		if(pSrc[SrcIndex].m_MustBe0 != 0)
		{
			log_error("map/load", "Tile layer data contains non-zero padding value %d at index %" PRIzu ".",
				pSrc[SrcIndex].m_MustBe0, SrcIndex);
			return false;
		}
		for(unsigned Counter = 0; Counter <= pSrc[SrcIndex].m_Skip && DestIndex < DestSize; Counter++)
		{
			pDest[DestIndex].m_Index = pSrc[SrcIndex].m_Index;
			pDest[DestIndex].m_Flags = pSrc[SrcIndex].m_Flags;
			pDest[DestIndex].m_Skip = 0;
			pDest[DestIndex].m_MustBe0 = 0;
			DestIndex++;
		}
		SrcIndex++;
	}
	if(DestIndex != DestSize)
	{
		log_error("map/load", "Tile layer data is truncated (got %" PRIzu ", wanted %" PRIzu ").",
			DestIndex, DestSize);
		return false;
	}
	if(SrcIndex != SrcSize)
	{
		log_error("map/load", "Too much tile layer data (read %" PRIzu ", total %" PRIzu ").",
			SrcIndex, SrcSize);
		return false;
	}
	return true;
}

bool CMap::ValidateMapVersion(CDataFileReader &NewDataFile)
{
	const int VersionItemIndex = NewDataFile.FindItemIndex(MAPITEMTYPE_VERSION, 0);
	if(VersionItemIndex < 0)
	{
		log_error("map/load", "Map version item is missing.");
		return false;
	}
	const size_t VersionItemSize = NewDataFile.GetItemSize(VersionItemIndex);
	if(VersionItemSize < sizeof(CMapItemVersion))
	{
		log_error("map/load", "Map version item is truncated (size %" PRIzu ").", VersionItemSize);
		return false;
	}
	const CMapItemVersion *pVersionItem = static_cast<CMapItemVersion *>(NewDataFile.GetItem(VersionItemIndex));
	if(pVersionItem->m_Version != 1)
	{
		log_error("map/load", "Map version %d is not supported.", pVersionItem->m_Version);
		return false;
	}
	return true;
}

static bool AtMostOneBitSet(int Flags)
{
	// https://graphics.stanford.edu/~seander/bithacks.html#DetermineIfPowerOf2
	return (Flags & (Flags - 1)) == 0;
}

static bool EnsureTileLayerProperties(int GroupIndex, int LayerIndex, CMapItemLayerTilemap &LayerTilemap)
{
	if(LayerTilemap.m_Width < 2)
	{
		log_error("map/load", "Tile layer %d in group %d has invalid width %d.",
			LayerIndex, GroupIndex, LayerTilemap.m_Width);
		return false;
	}

	if(LayerTilemap.m_Height < 2)
	{
		log_error("map/load", "Tile layer %d in group %d has invalid height %d.",
			LayerIndex, GroupIndex, LayerTilemap.m_Height);
		return false;
	}

	const auto &&EnsureValidName = [&](const char *pExpectedName) {
		char aCurrentName[sizeof(LayerTilemap.m_aName)];
		if(!IntsToStr(LayerTilemap.m_aName, std::size(LayerTilemap.m_aName), aCurrentName, std::size(aCurrentName)))
		{
			log_error("map/load", "Tile layer %d in group %d has invalid name.",
				LayerIndex, GroupIndex);
			return false;
		}
		else if(pExpectedName != nullptr && str_comp(aCurrentName, pExpectedName) != 0)
		{
			log_debug("map/load", "Physics tile layer %d in group %d has unexpected name '%s'. Resetting to '%s'.",
				LayerIndex, GroupIndex, aCurrentName, pExpectedName);
			StrToInts(LayerTilemap.m_aName, std::size(LayerTilemap.m_aName), pExpectedName);
		}
		return true;
	};

	const auto &&EnsureDefaultColor = [&]() {
		const CColor DefaultColor = CColor{255, 255, 255, 255};
		if(LayerTilemap.m_Color != DefaultColor)
		{
			log_debug("map/load", "Physics tile layer %d in group %d has unexpected color (%d, %d, %d, %d). Resetting to default.",
				LayerIndex, GroupIndex, LayerTilemap.m_Color.r, LayerTilemap.m_Color.g, LayerTilemap.m_Color.b, LayerTilemap.m_Color.a);
			LayerTilemap.m_Color = DefaultColor;
		}
	};

	const auto &&EnsureNoDetailFlag = [&]() {
		if(LayerTilemap.m_Layer.m_Flags & LAYERFLAG_DETAIL)
		{
			log_debug("map/load", "Physics tile layer %d in group %d has detail flag set. Resetting to non-detail.",
				LayerIndex, GroupIndex);
			LayerTilemap.m_Layer.m_Flags &= ~LAYERFLAG_DETAIL;
		}
	};

	if(LayerTilemap.m_Flags & TILESLAYERFLAG_GAME)
	{
		if(!EnsureValidName("Game"))
		{
			return false;
		}
		EnsureDefaultColor();
		EnsureNoDetailFlag();
	}
	else if(LayerTilemap.m_Flags & TILESLAYERFLAG_TELE)
	{
		if(!EnsureValidName("Tele"))
		{
			return false;
		}
		EnsureDefaultColor();
		EnsureNoDetailFlag();
	}
	else if(LayerTilemap.m_Flags & TILESLAYERFLAG_SPEEDUP)
	{
		if(!EnsureValidName("Speedup"))
		{
			return false;
		}
		EnsureDefaultColor();
		EnsureNoDetailFlag();
	}
	else if(LayerTilemap.m_Flags & TILESLAYERFLAG_FRONT)
	{
		if(!EnsureValidName("Front"))
		{
			return false;
		}
		EnsureDefaultColor();
		EnsureNoDetailFlag();
	}
	else if(LayerTilemap.m_Flags & TILESLAYERFLAG_SWITCH)
	{
		if(!EnsureValidName("Switch"))
		{
			return false;
		}
		EnsureDefaultColor();
		EnsureNoDetailFlag();
	}
	else if(LayerTilemap.m_Flags & TILESLAYERFLAG_TUNE)
	{
		if(!EnsureValidName("Tune"))
		{
			return false;
		}
		EnsureDefaultColor();
		EnsureNoDetailFlag();
	}
	else
	{
		if(!EnsureValidName(nullptr))
		{
			return false;
		}

		if(!in_range(LayerTilemap.m_Color.r, 0, 255) ||
			!in_range(LayerTilemap.m_Color.g, 0, 255) ||
			!in_range(LayerTilemap.m_Color.b, 0, 255) ||
			!in_range(LayerTilemap.m_Color.a, 0, 255))
		{
			log_error("map/load", "Tile layer %d in group %d has invalid color (%d, %d, %d, %d).",
				LayerIndex, GroupIndex, LayerTilemap.m_Color.r, LayerTilemap.m_Color.g, LayerTilemap.m_Color.b, LayerTilemap.m_Color.a);
			return false;
		}
	}

	const auto &&EnsureUnsetPhysicsData = [&](int TilesLayerFlag, int *pDataIndex, const char *pName) {
		if((LayerTilemap.m_Flags & TilesLayerFlag) == 0 && *pDataIndex != -1)
		{
			log_debug("map/load", "Tile layer %d in group %d has unused %s data index %d. Resetting to -1.",
				LayerIndex, GroupIndex, pName, *pDataIndex);
			*pDataIndex = -1;
		}
	};
	EnsureUnsetPhysicsData(TILESLAYERFLAG_TELE, &LayerTilemap.m_Tele, "tele");
	EnsureUnsetPhysicsData(TILESLAYERFLAG_SPEEDUP, &LayerTilemap.m_Speedup, "speedup");
	EnsureUnsetPhysicsData(TILESLAYERFLAG_FRONT, &LayerTilemap.m_Front, "front");
	EnsureUnsetPhysicsData(TILESLAYERFLAG_SWITCH, &LayerTilemap.m_Switch, "switch");
	EnsureUnsetPhysicsData(TILESLAYERFLAG_TUNE, &LayerTilemap.m_Tune, "tune");

	return true;
}

bool CMap::UpgradeAndValidateTilesLayerItem(
	CDataFileReader &NewDataFile, int GroupIndex, int LayerIndex,
	CMapItemLayerTilemap_v2 *pLayerTilemapBase, int LayerItemIndex, size_t LayerItemSize)
{
	if(LayerItemSize < sizeof(CMapItemLayerTilemap_v2))
	{
		log_error("map/load", "Tile layer %d in group %d is truncated (size %" PRIzu ").",
			LayerIndex, GroupIndex, LayerItemSize);
		return false;
	}

	if(!in_range(pLayerTilemapBase->m_Version, 2, 4))
	{
		log_error("map/load", "Tile layer %d in group %d has unsupported version %d.",
			LayerIndex, GroupIndex, pLayerTilemapBase->m_Version);
		return false;
	}

	if(!AtMostOneBitSet(pLayerTilemapBase->m_Flags & (TILESLAYERFLAG_GAME | TILESLAYERFLAG_TELE | TILESLAYERFLAG_SPEEDUP | TILESLAYERFLAG_FRONT | TILESLAYERFLAG_SWITCH | TILESLAYERFLAG_TUNE)))
	{
		log_error("map/load", "Tile layer %d in group %d has invalid combination of flags %d. At most one physics tile layer flag can be set.",
			LayerIndex, GroupIndex, pLayerTilemapBase->m_Flags);
		return false;
	}

	const auto &&UnpackPhysicsLayerDataIndex = [&](int TilesLayerFlag, int *pTargetDataIndex, const int *pSourceDataIndex, const CMapItemLayerTilemap_v2 *pSourceTileLayer, const char *pName) {
		// 旧地图可能只包含物理 tile 数据索引的前缀而不递增版本，因此必须逐个检查每个数据索引的大小。
		if(LayerItemSize < reinterpret_cast<const uint8_t *>(pSourceDataIndex) - reinterpret_cast<const uint8_t *>(pSourceTileLayer) + sizeof(*pSourceDataIndex))
		{
			if(pSourceTileLayer->m_Flags & TilesLayerFlag)
			{
				log_error("map/load", "%s layer %d in group %d is truncated (version %d, size %" PRIzu ").",
					pName, LayerIndex, GroupIndex, pSourceTileLayer->m_Version, LayerItemSize);
				return false;
			}
			*pTargetDataIndex = -1;
		}
		else
		{
			*pTargetDataIndex = *pSourceDataIndex;
		}
		return true;
	};

	if(pLayerTilemapBase->m_Version == 2)
	{
		const CMapItemLayerTilemap_v2Legacy *pLayerTilemapLegacy = static_cast<const CMapItemLayerTilemap_v2Legacy *>(pLayerTilemapBase);
		CMapItemLayerTilemap OverriddenLayerTilemap;
		mem_copy(&OverriddenLayerTilemap, pLayerTilemapLegacy, sizeof(CMapItemLayerTilemap_v2));

		// 版本 2 条目没有图层名，默认为空字符串。物理层名称稍后统一修正。
		StrToInts(OverriddenLayerTilemap.m_aName, std::size(OverriddenLayerTilemap.m_aName), "");

		if(!UnpackPhysicsLayerDataIndex(TILESLAYERFLAG_TELE, &OverriddenLayerTilemap.m_Tele, &pLayerTilemapLegacy->m_Tele, pLayerTilemapBase, "Tele") ||
			!UnpackPhysicsLayerDataIndex(TILESLAYERFLAG_SPEEDUP, &OverriddenLayerTilemap.m_Speedup, &pLayerTilemapLegacy->m_Speedup, pLayerTilemapBase, "Speedup") ||
			!UnpackPhysicsLayerDataIndex(TILESLAYERFLAG_FRONT, &OverriddenLayerTilemap.m_Front, &pLayerTilemapLegacy->m_Front, pLayerTilemapBase, "Front") ||
			!UnpackPhysicsLayerDataIndex(TILESLAYERFLAG_SWITCH, &OverriddenLayerTilemap.m_Switch, &pLayerTilemapLegacy->m_Switch, pLayerTilemapBase, "Switch") ||
			!UnpackPhysicsLayerDataIndex(TILESLAYERFLAG_TUNE, &OverriddenLayerTilemap.m_Tune, &pLayerTilemapLegacy->m_Tune, pLayerTilemapBase, "Tune"))
		{
			return false;
		}
		if(!EnsureTileLayerProperties(GroupIndex, LayerIndex, OverriddenLayerTilemap))
		{
			return false;
		}
		if(!NewDataFile.OverrideItemData(LayerItemIndex, &OverriddenLayerTilemap, sizeof(OverriddenLayerTilemap)))
		{
			return false;
		}
	}
	else if(LayerItemSize < sizeof(CMapItemLayerTilemap_v3Teeworlds))
	{
		// 版本 3 与 4 条目只允许截断 DDRace 追加的物理层数据索引，图层名必须完整。
		log_error("map/load", "Tile layer %d in group %d is truncated (version %d, size %" PRIzu ").",
			LayerIndex, GroupIndex, pLayerTilemapBase->m_Version, LayerItemSize);
		return false;
	}
	else if(LayerItemSize < sizeof(CMapItemLayerTilemap))
	{
		const CMapItemLayerTilemap *pLayerTilemapLegacy = static_cast<const CMapItemLayerTilemap *>(pLayerTilemapBase);
		CMapItemLayerTilemap OverriddenLayerTilemap;
		mem_copy(&OverriddenLayerTilemap, pLayerTilemapLegacy, sizeof(CMapItemLayerTilemap_v3Teeworlds));

		if(!UnpackPhysicsLayerDataIndex(TILESLAYERFLAG_TELE, &OverriddenLayerTilemap.m_Tele, &pLayerTilemapLegacy->m_Tele, pLayerTilemapBase, "Tele") ||
			!UnpackPhysicsLayerDataIndex(TILESLAYERFLAG_SPEEDUP, &OverriddenLayerTilemap.m_Speedup, &pLayerTilemapLegacy->m_Speedup, pLayerTilemapBase, "Speedup") ||
			!UnpackPhysicsLayerDataIndex(TILESLAYERFLAG_FRONT, &OverriddenLayerTilemap.m_Front, &pLayerTilemapLegacy->m_Front, pLayerTilemapBase, "Front") ||
			!UnpackPhysicsLayerDataIndex(TILESLAYERFLAG_SWITCH, &OverriddenLayerTilemap.m_Switch, &pLayerTilemapLegacy->m_Switch, pLayerTilemapBase, "Switch") ||
			!UnpackPhysicsLayerDataIndex(TILESLAYERFLAG_TUNE, &OverriddenLayerTilemap.m_Tune, &pLayerTilemapLegacy->m_Tune, pLayerTilemapBase, "Tune"))
		{
			return false;
		}
		if(!EnsureTileLayerProperties(GroupIndex, LayerIndex, OverriddenLayerTilemap))
		{
			return false;
		}
		if(!NewDataFile.OverrideItemData(LayerItemIndex, &OverriddenLayerTilemap, sizeof(OverriddenLayerTilemap)))
		{
			return false;
		}
	}
	else // 最新版本，整个 CMapItemLayerTilemap 可用
	{
		if(!EnsureTileLayerProperties(GroupIndex, LayerIndex, *static_cast<CMapItemLayerTilemap *>(pLayerTilemapBase)))
		{
			return false;
		}
	}

	return true;
}

bool CMap::ValidateAndUnpackTilesLayerData(CDataFileReader &NewDataFile, int GroupIndex, int LayerIndex, const CMapItemLayerTilemap *pLayerTilemap, const CMapItemLayerTilemap &GameLayer, std::set<int> &UsedDataIndices)
{
	size_t TileSize;
	int DataIndex;
	int LayerType;
	if(pLayerTilemap->m_Flags & TILESLAYERFLAG_GAME)
	{
		TileSize = sizeof(CTile);
		DataIndex = pLayerTilemap->m_Data;
		LayerType = LAYERTYPE_GAME;
	}
	else if(pLayerTilemap->m_Flags & TILESLAYERFLAG_TELE)
	{
		TileSize = sizeof(CTeleTile);
		DataIndex = pLayerTilemap->m_Tele;
		LayerType = LAYERTYPE_TELE;
	}
	else if(pLayerTilemap->m_Flags & TILESLAYERFLAG_SPEEDUP)
	{
		TileSize = sizeof(CSpeedupTile);
		DataIndex = pLayerTilemap->m_Speedup;
		LayerType = LAYERTYPE_SPEEDUP;
	}
	else if(pLayerTilemap->m_Flags & TILESLAYERFLAG_FRONT)
	{
		TileSize = sizeof(CTile);
		DataIndex = pLayerTilemap->m_Front;
		LayerType = LAYERTYPE_FRONT;
	}
	else if(pLayerTilemap->m_Flags & TILESLAYERFLAG_SWITCH)
	{
		TileSize = sizeof(CSwitchTile);
		DataIndex = pLayerTilemap->m_Switch;
		LayerType = LAYERTYPE_SWITCH;
	}
	else if(pLayerTilemap->m_Flags & TILESLAYERFLAG_TUNE)
	{
		TileSize = sizeof(CTuneTile);
		DataIndex = pLayerTilemap->m_Tune;
		LayerType = LAYERTYPE_TUNE;
	}
	else
	{
		TileSize = sizeof(CTile);
		DataIndex = pLayerTilemap->m_Data;
		LayerType = LAYERTYPE_TILES;
	}

	if(DataIndex < 0 || DataIndex >= NewDataFile.NumData())
	{
		log_error("map/load", "Tile data index %d of layer %d in group %d is invalid.", DataIndex, LayerIndex, GroupIndex);
		return false;
	}

	const auto &[_, DataUnique] = UsedDataIndices.emplace(DataIndex);
	if(!DataUnique)
	{
		log_error("map/load", "Tile data index %d of layer %d in group %d is not unique.", DataIndex, LayerIndex, GroupIndex);
		return false;
	}

	const size_t TilemapCount = (size_t)pLayerTilemap->m_Width * pLayerTilemap->m_Height;
	const size_t TilemapSize = TilemapCount * TileSize;

	if(((int)TilemapCount / pLayerTilemap->m_Width != pLayerTilemap->m_Height) || (TilemapSize / TileSize != TilemapCount))
	{
		log_error("map/load", "Tile layer %d in group %d is too big (%d * %d * %" PRIzu " causes an integer overflow).",
			LayerIndex, GroupIndex, pLayerTilemap->m_Width, pLayerTilemap->m_Height, TileSize);
		return false;
	}

	// 碰撞检测使用 game 层的大小来访问所有物理层数据，
	// 因此物理层必须包含至少与 game 层一样多的 tile。
	if(LayerType != LAYERTYPE_TILES && LayerType != LAYERTYPE_GAME &&
		TilemapCount < (size_t)GameLayer.m_Width * GameLayer.m_Height)
	{
		log_error("map/load", "Physics layer %d in group %d is smaller than the game layer (%d * %d < %d * %d).",
			LayerIndex, GroupIndex, pLayerTilemap->m_Width, pLayerTilemap->m_Height, GameLayer.m_Width, GameLayer.m_Height);
		return false;
	}

	NewDataFile.AddDataProcessor(DataIndex, [pLayerTilemap, TileSize, LayerType, GroupIndex, LayerIndex, TilemapCount, TilemapSize](void *pData, size_t Size) -> std::pair<void *, size_t> {
		const size_t SavedTilesSize = Size / TileSize;
		if(pLayerTilemap->m_Version >= 4)
		{
			// 该版本的 CMapItemLayerTilemap 只会被上游 Teeworlds 写入地图。
			// 此版本 tile 层的数据必须按 CTile::m_Skip 值重复 tile 来解包。
			if(LayerType != LAYERTYPE_TILES && LayerType != LAYERTYPE_GAME)
			{
				log_error("map/load", "Layer %d in group %d uses tileskip but this is only supported for tiles and game layers.",
					LayerIndex, GroupIndex);
				free(pData);
				return std::make_pair(nullptr, 0);
			}
			CTile *pTiles = static_cast<CTile *>(malloc(TilemapSize));
			if(pTiles == nullptr)
			{
				log_error("map/load", "Failed to allocate memory for layer %d in group %d (size %d * %d).",
					LayerIndex, GroupIndex, pLayerTilemap->m_Width, pLayerTilemap->m_Height);
				free(pData);
				return std::make_pair(nullptr, 0);
			}
			else if(!ExtractTiles(pTiles, (size_t)pLayerTilemap->m_Width * pLayerTilemap->m_Height, static_cast<const CTile *>(pData), SavedTilesSize))
			{
				log_error("map/load", "Failed to extract tiles of layer %d in group %d.",
					LayerIndex, GroupIndex);
				free(pTiles);
				free(pData);
				return std::make_pair(nullptr, 0);
			}
			free(pData);
			return std::make_pair(pTiles, TilemapSize);
		}
		else if(SavedTilesSize < TilemapCount)
		{
			log_error("map/load", "Tile data of layer %d in group %d is truncated (got %" PRIzu ", wanted %" PRIzu ").",
				LayerIndex, GroupIndex, SavedTilesSize, TilemapCount);
			free(pData);
			return std::make_pair(nullptr, 0);
		}
		else if(LayerType == LAYERTYPE_TILES || LayerType == LAYERTYPE_GAME || LayerType == LAYERTYPE_FRONT)
		{
			const CTile *pTileData = static_cast<const CTile *>(pData);
			for(size_t TileIndex = 0; TileIndex < TilemapCount; ++TileIndex)
			{
				if(pTileData[TileIndex].m_Skip != 0)
				{
					log_error("map/load", "Tile data of layer %d in group %d contains non-zero skip value %d at index %" PRIzu " but version %d does not use tileskip.",
						LayerIndex, GroupIndex, pTileData[TileIndex].m_Skip, TileIndex, pLayerTilemap->m_Version);
					free(pData);
					return std::make_pair(nullptr, 0);
				}
				if(pTileData[TileIndex].m_MustBe0 != 0)
				{
					log_error("map/load", "Tile data of layer %d in group %d contains non-zero padding value %d at index %" PRIzu ".",
						LayerIndex, GroupIndex, pTileData[TileIndex].m_MustBe0, TileIndex);
					free(pData);
					return std::make_pair(nullptr, 0);
				}
			}
		}
		else if(LayerType == LAYERTYPE_SPEEDUP)
		{
			const CSpeedupTile *pSpeedupData = static_cast<const CSpeedupTile *>(pData);
			for(size_t TileIndex = 0; TileIndex < TilemapCount; ++TileIndex)
			{
				if(pSpeedupData[TileIndex].m_MustBe0 != 0)
				{
					log_error("map/load", "Speedup tile data of layer %d in group %d contains non-zero padding value %d at index %" PRIzu ".",
						LayerIndex, GroupIndex, pSpeedupData[TileIndex].m_MustBe0, TileIndex);
					free(pData);
					return std::make_pair(nullptr, 0);
				}
			}
		}
		return std::make_pair(pData, Size);
	});

	return true;
}

extern IEngineMap *CreateEngineMap() { return new CMap; }
