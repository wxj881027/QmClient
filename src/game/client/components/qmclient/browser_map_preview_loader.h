#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_MAP_PREVIEW_LOADER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_MAP_PREVIEW_LOADER_H

#include <base/mem.h>
#include <base/system.h>

#include <engine/gfx/image_loader.h>
#include <engine/shared/jobs.h>
#include <engine/shared/map.h>
#include <engine/storage.h>

#include <game/layers.h>
#include <game/map/map_render_items.h>
#include <game/mapitems.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <optional>
#include <span>
#include <vector>

// 优先普通出生点，其次队伍出生点和计时起点；不取全图平均，避免落在远处空白。
inline std::optional<vec2> QmBrowserMapPreviewFindStart(std::span<const CTile> Tiles, int Width, int Height)
{
	if(Width <= 0 || Height <= 0 || static_cast<size_t>(Width) * Height > Tiles.size())
		return std::nullopt;
	std::optional<vec2> TeamSpawn, Start;
	for(size_t Index = 0; Index < static_cast<size_t>(Width) * Height; ++Index)
	{
		const int Tile = Tiles[Index].m_Index;
		const vec2 Position((Index % Width) * 32.0f + 16.0f, (Index / Width) * 32.0f + 16.0f);
		if(Tile == ENTITY_OFFSET + ENTITY_SPAWN)
			return Position;
		if(!TeamSpawn && (Tile == ENTITY_OFFSET + ENTITY_SPAWN_RED || Tile == ENTITY_OFFSET + ENTITY_SPAWN_BLUE))
			TeamSpawn = Position;
		if(!Start && Tile == TILE_START)
			Start = Position;
	}
	return TeamSpawn ? TeamSpawn : Start;
}

// downloadedmaps 前缀枚举回调：挑出 <名>_<sha256|crc>.map 中修改时间最新的一个。
struct SMapDownloadProbeContext
{
	const char *m_pPrefix;
	char m_aBest[IO_MAX_PATH_LENGTH];
	time_t m_BestTime;
};

inline int ProbeDownloadedMap(const CFsFileInfo *pInfo, int IsDir, int DirType, void *pUser)
{
	SMapDownloadProbeContext *pProbe = static_cast<SMapDownloadProbeContext *>(pUser);
	if(IsDir || pInfo->m_TimeModified <= pProbe->m_BestTime)
		return 0;
	const int PrefixLen = str_length(pProbe->m_pPrefix);
	const int NameLen = str_length(pInfo->m_pName);
	// 严格 <前缀>_<哈希>.map：前缀后必须紧跟 '_'，防止 "Sunny" 误配 "SunnyLand_…"。
	if(NameLen < PrefixLen + 2 || str_startswith(pInfo->m_pName, pProbe->m_pPrefix) == nullptr || pInfo->m_pName[PrefixLen] != '_')
		return 0;
	if(str_endswith(pInfo->m_pName, ".map") == nullptr)
		return 0;
	pProbe->m_BestTime = pInfo->m_TimeModified;
	str_format(pProbe->m_aBest, sizeof(pProbe->m_aBest), "downloadedmaps/%s", pInfo->m_pName);
	return 0;
}

// 与 CClient::LoadMapSearch 的本地解析同序（绝不联网）：
// 1) 客户端刚加载过的地图直接用真实路径（哈希命名也能精确命中）；
// 2) maps/<名>.map 精确命中；3) maps/ 子目录搜索；
// 4) downloadedmaps/<名>_<sha256|crc>.map 前缀枚举，取修改时间最新。
inline bool ResolveLocalPreviewMapPath(IStorage *pStorage, const char *pCurrentMapPath, const char *pMapName, char *pOut, size_t OutSize)
{
	if(pCurrentMapPath[0] != '\0')
	{
		str_copy(pOut, pCurrentMapPath, OutSize);
		if(pStorage->FileExists(pOut, IStorage::TYPE_ALL_OR_ABSOLUTE))
			return true;
	}

	char aMapPath[IO_MAX_PATH_LENGTH];
	str_format(aMapPath, sizeof(aMapPath), "maps/%s.map", pMapName);
	if(pStorage->FileExists(aMapPath, IStorage::TYPE_ALL))
	{
		str_copy(pOut, aMapPath, OutSize);
		return true;
	}

	char aLeaf[IO_MAX_PATH_LENGTH];
	str_format(aLeaf, sizeof(aLeaf), "%s.map", pMapName);
	if(pStorage->FindFile(aLeaf, "maps", IStorage::TYPE_ALL, pOut, OutSize))
		return true;

	SMapDownloadProbeContext Probe;
	Probe.m_pPrefix = pMapName;
	Probe.m_aBest[0] = '\0';
	Probe.m_BestTime = 0;
	pStorage->ListDirectoryInfo(IStorage::TYPE_ALL, "downloadedmaps", ProbeDownloadedMap, &Probe);
	if(Probe.m_aBest[0] != '\0')
	{
		str_copy(pOut, Probe.m_aBest, OutSize);
		return true;
	}
	return false;
}

// worker 只拥有本地地图与解码结果；不引用菜单、客户端状态或图形接口。
class CQmBrowserMapPreviewLoadJob : public IJob
{
	IStorage *m_pStorage;
	char m_aName[MAX_MAP_LENGTH];
	char m_aCurrentPath[IO_MAX_PATH_LENGTH];
	bool m_Sixup;

public:
	void Run() override
	{
		char aPath[IO_MAX_PATH_LENGTH];
		if(m_Cancelled.load() || !ResolveLocalPreviewMapPath(m_pStorage, m_aCurrentPath, m_aName, aPath, sizeof(aPath)) || m_Cancelled.load())
			return;
		m_pMap = std::make_unique<CMap>(m_pStorage);
		if(!m_pMap->Load(aPath, IStorage::TYPE_ALL_OR_ABSOLUTE) || m_Cancelled.load())
			return;
		m_pLayers = std::make_unique<CLayers>();
		m_pLayers->Init(m_pMap.get(), false);
		for(const auto *pLayer : {m_pLayers->GameLayer(), m_pLayers->FrontLayer()})
		{
			if(m_Start || !pLayer)
				continue;
			const int DataIndex = pLayer == m_pLayers->GameLayer() ? pLayer->m_Data : pLayer->m_Front;
			const auto *pTiles = static_cast<const CTile *>(m_pMap->GetData(DataIndex));
			const int Size = m_pMap->GetDataSize(DataIndex);
			if(pTiles && Size > 0)
				m_Start = QmBrowserMapPreviewFindStart({pTiles, static_cast<size_t>(Size) / sizeof(CTile)}, pLayer->m_Width, pLayer->m_Height);
		}
		int Start, Count;
		m_pMap->GetType(MAPITEMTYPE_IMAGE, &Start, &Count);
		Count = std::clamp<int>(Count, 0, MAX_MAPIMAGES);
		m_vImages.resize(Count);
		m_vUsage.resize(Count);
		int LayersStart, LayersCount;
		m_pMap->GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersCount);
		for(int Group = 0; Group < m_pLayers->NumGroups(); ++Group)
		{
			const auto *pGroup = m_pLayers->GetGroup(Group);
			for(int Layer = 0; Layer < pGroup->m_NumLayers; ++Layer)
			{
				const auto *pLayer = m_pLayers->GetLayer(pGroup->m_StartLayer + Layer);
				int Image = -1, Usage = 0;
				if(pLayer->m_Type == LAYERTYPE_TILES)
				{
					Image = reinterpret_cast<const CMapItemLayerTilemap *>(pLayer)->m_Image;
					Usage = 1;
				}
				else if(pLayer->m_Type == LAYERTYPE_QUADS)
				{
					const auto *pQuads = MapRenderItems::QuadLayer(*m_pMap, LayersStart + pGroup->m_StartLayer + Layer);
					if(pQuads == nullptr)
						return;
					Image = pQuads->m_Image;
					Usage = 2;
					// 四边形压缩数据也在 worker 预解压，GPU 初始化只读取准备好的数据。
					m_pMap->GetDataSwapped(pQuads->m_Data);
				}
				if(Image >= 0 && Image < Count)
					m_vUsage[Image] |= Usage;
			}
		}
		for(int Index = 0; Index < Count && !m_Cancelled.load(); ++Index)
		{
			if(m_vUsage[Index] == 0)
				continue;
			const auto ImageItem = MapRenderItems::ReadImage(*m_pMap, Start + Index);
			if(!ImageItem)
				return;
			if(ImageItem->m_Version > 1 && ImageItem->m_MustBe1 != 1)
				continue;
			const char *pName = m_pMap->GetDataString(ImageItem->m_ImageName);
			if(ImageItem->m_External)
			{
				if(pName == nullptr || pName[0] == '\0')
					continue;
				const bool Translate = m_Sixup && (!str_comp(pName, "grass_doodads") || !str_comp(pName, "grass_main") || !str_comp(pName, "winter_main") || !str_comp(pName, "generic_shadows") || !str_comp(pName, "generic_unhookable") || !str_comp(pName, "easter"));
				str_format(aPath, sizeof(aPath), "mapres/%s%s.png", pName, Translate ? "_0.7" : "");
				void *pData = nullptr;
				unsigned Size = 0;
				if(m_pStorage->ReadFile(aPath, IStorage::TYPE_ALL, &pData, &Size))
				{
					CImageLoader::LoadPng(pData, Size, aPath, m_vImages[Index]);
					free(pData);
				}
			}
			else if(ImageItem->m_Width > 0 && ImageItem->m_Height > 0)
			{
				CImageInfo &Image = m_vImages[Index];
				Image.m_Width = ImageItem->m_Width;
				Image.m_Height = ImageItem->m_Height;
				Image.m_Format = CImageInfo::FORMAT_RGBA;
				const void *pData = m_pMap->GetData(ImageItem->m_ImageData);
				size_t Size;
				if(pData && Image.DataSize(Size) && m_pMap->GetDataSize(ImageItem->m_ImageData) >= 0 && Size <= static_cast<size_t>(m_pMap->GetDataSize(ImageItem->m_ImageData)))
				{
					Image.m_pData = static_cast<uint8_t *>(malloc(Size));
					if(Image.m_pData)
						mem_copy(Image.m_pData, pData, Size);
				}
				m_pMap->UnloadData(ImageItem->m_ImageData);
			}
		}
		m_Success = !m_Cancelled.load();
	}

public:
	std::atomic<bool> m_Cancelled{false};
	std::unique_ptr<CMap> m_pMap;
	std::unique_ptr<CLayers> m_pLayers;
	std::vector<CImageInfo> m_vImages;
	std::vector<int> m_vUsage;
	bool m_Success = false;
	std::optional<vec2> m_Start;
	CQmBrowserMapPreviewLoadJob(IStorage *pStorage, const char *pName, const char *pCurrentPath, bool Sixup) : m_pStorage(pStorage), m_Sixup(Sixup)
	{
		str_copy(m_aName, pName);
		str_copy(m_aCurrentPath, pCurrentPath);
	}
	~CQmBrowserMapPreviewLoadJob() override
	{
		for(auto &Image : m_vImages)
			Image.Free();
	}
};

#endif
