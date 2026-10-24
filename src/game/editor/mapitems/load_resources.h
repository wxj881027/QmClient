#ifndef GAME_EDITOR_MAPITEMS_LOAD_RESOURCES_H
#define GAME_EDITOR_MAPITEMS_LOAD_RESOURCES_H

#include <base/log.h>
#include <base/mem.h>

#include <game/map/map_render_items.h>
#include <game/mapitems_ex.h>

#include <cstddef>
#include <new>
#include <optional>
#include <stdexcept>
#include <vector>

namespace EditorMapLoad
{
	struct SResourceItems
	{
		std::vector<CMapItemImage_v2> m_vImages;
		std::vector<CMapItemSound> m_vSounds;
		std::vector<CMapItemAutoMapperConfig> m_vAutoMapperConfigs;
	};

	// 与实际导入共用筛选，自动映射索引按导入后的组/层序号解释。
	inline bool ImportsLayer(IMap &Map, int ItemIndex)
	{
		if(ItemIndex < 0 || ItemIndex >= Map.NumItems() || Map.GetItemSize(ItemIndex) < static_cast<int>(sizeof(CMapItemLayer)))
			return false;
		int Type;
		const auto *pLayer = static_cast<const CMapItemLayer *>(Map.GetItem(ItemIndex, &Type));
		if(pLayer == nullptr || Type != MAPITEMTYPE_LAYER)
			return false;
		if(pLayer->m_Type == LAYERTYPE_TILES || pLayer->m_Type == LAYERTYPE_QUADS)
			return true;
		const auto Sounds = MapRenderItems::ReadSoundLayer(Map, ItemIndex);
		return Sounds && Sounds->m_Version >= 1 && Sounds->m_Version <= 2;
	}

	// 共享 CMap 已校验组与图层基础头；在编辑器清空旧地图前补查资源尾部。
	inline std::optional<SResourceItems> ReadResourceItems(IEngineMap &Map)
	try
	{
		if(!Map.IsLoaded())
			return std::nullopt;

		SResourceItems Resources;
		int ImagesStart, ImagesNum;
		Map.GetType(MAPITEMTYPE_IMAGE, &ImagesStart, &ImagesNum);
		Resources.m_vImages.reserve(static_cast<size_t>(ImagesNum));
		for(int i = 0; i < ImagesNum; ++i)
		{
			const auto Image = MapRenderItems::ReadImage(Map, ImagesStart + i);
			if(!Image)
			{
				log_error("editor/load", "Image %d has a truncated or invalid header.", i);
				return std::nullopt;
			}
			Resources.m_vImages.push_back(*Image);
		}
		int SoundsStart, SoundsNum;
		Map.GetType(MAPITEMTYPE_SOUND, &SoundsStart, &SoundsNum);
		Resources.m_vSounds.reserve(static_cast<size_t>(SoundsNum));
		for(int i = 0; i < SoundsNum; ++i)
		{
			const auto Sound = MapRenderItems::ReadSound(Map, SoundsStart + i);
			if(!Sound)
			{
				log_error("editor/load", "Sound %d has a truncated or invalid header.", i);
				return std::nullopt;
			}
			Resources.m_vSounds.push_back(*Sound);
		}

		int GroupsStart, GroupsNum, LayersStart, LayersNum;
		Map.GetType(MAPITEMTYPE_GROUP, &GroupsStart, &GroupsNum);
		Map.GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);
		int AutoMapperStart, AutoMapperNum;
		Map.GetType(MAPITEMTYPE_AUTOMAPPER_CONFIG, &AutoMapperStart, &AutoMapperNum);
		std::vector<std::vector<bool>> vvAutoMapperTargets;
		if(AutoMapperNum > 0)
			vvAutoMapperTargets.reserve(GroupsNum);
		for(int g = 0; g < GroupsNum; ++g)
		{
			const auto *pGroup = static_cast<const CMapItemGroup_v1 *>(Map.GetItem(GroupsStart + g));
			// 与编辑器实际导入范围一致，不改变未引用或被跳过条目的处理。
			if(pGroup->m_Version < 1 || pGroup->m_Version > 3)
				continue;
			std::vector<bool> *pAutoMapperTargets = nullptr;
			if(AutoMapperNum > 0)
			{
				pAutoMapperTargets = &vvAutoMapperTargets.emplace_back();
				pAutoMapperTargets->reserve(pGroup->m_NumLayers);
			}
			for(int l = 0; l < pGroup->m_NumLayers; ++l)
			{
				const int ItemIndex = LayersStart + pGroup->m_StartLayer + l;
				const auto *pLayer = static_cast<const CMapItemLayer *>(Map.GetItem(ItemIndex));
				if(pLayer->m_Type == LAYERTYPE_QUADS)
				{
					const auto *pQuads = MapRenderItems::QuadLayer(Map, ItemIndex);
					if(pQuads == nullptr ||
						(pQuads->m_Version >= 2 && Map.GetItemSize(ItemIndex) < static_cast<int>(sizeof(CMapItemLayerQuads))))
					{
						log_error("editor/load", "Quad layer %d in group %d has a truncated header or name.", l, g);
						return std::nullopt;
					}
				}
				else if(pLayer->m_Type == LAYERTYPE_SOUNDS || pLayer->m_Type == LAYERTYPE_SOUNDS_DEPRECATED)
				{
					const auto SoundLayer = MapRenderItems::ReadSoundLayer(Map, ItemIndex);
					if(!SoundLayer ||
						(SoundLayer->m_Version >= 1 && SoundLayer->m_Version <= 2 && Map.GetItemSize(ItemIndex) < static_cast<int>(sizeof(CMapItemLayerSounds))))
					{
						log_error("editor/load", "Sound layer %d in group %d has a truncated header or name.", l, g);
						return std::nullopt;
					}
				}
				if(pAutoMapperTargets != nullptr && ImportsLayer(Map, ItemIndex))
				{
					bool IsTarget = false;
					if(pLayer->m_Type == LAYERTYPE_TILES)
					{
						const auto *pTiles = static_cast<const CMapItemLayerTilemap *>(Map.GetItem(ItemIndex));
						IsTarget = !(pTiles->m_Flags & (TILESLAYERFLAG_GAME | TILESLAYERFLAG_TELE | TILESLAYERFLAG_SPEEDUP |
										       TILESLAYERFLAG_FRONT | TILESLAYERFLAG_SWITCH | TILESLAYERFLAG_TUNE));
					}
					pAutoMapperTargets->push_back(IsTarget);
				}
			}
		}
		for(int i = 0; i < AutoMapperNum; ++i)
		{
			const int ItemIndex = AutoMapperStart + i;
			const int Size = Map.GetItemSize(ItemIndex);
			if(Size < static_cast<int>(sizeof(int)))
			{
				log_error("editor/load", "Automapper configuration %d is missing its version.", i);
				return std::nullopt;
			}
			int Type;
			const void *pData = Map.GetItem(ItemIndex, &Type);
			if(pData == nullptr || Type != MAPITEMTYPE_AUTOMAPPER_CONFIG)
				return std::nullopt;
			CMapItemAutoMapperConfig Item{};
			mem_copy(&Item, pData, sizeof(int));
			if(Item.m_Version != 1)
				continue;
			if(Size < static_cast<int>(offsetof(CMapItemAutoMapperConfig, m_LayerId)))
			{
				log_error("editor/load", "Automapper configuration %d is missing its group index.", i);
				return std::nullopt;
			}
			mem_copy(&Item, pData, offsetof(CMapItemAutoMapperConfig, m_LayerId));
			if(Item.m_GroupId < 0 || static_cast<size_t>(Item.m_GroupId) >= vvAutoMapperTargets.size())
				continue;
			if(Size < static_cast<int>(offsetof(CMapItemAutoMapperConfig, m_AutomapperConfig)))
			{
				log_error("editor/load", "Automapper configuration %d is missing its layer index.", i);
				return std::nullopt;
			}
			mem_copy(&Item, pData, offsetof(CMapItemAutoMapperConfig, m_AutomapperConfig));
			const auto &vTargets = vvAutoMapperTargets[Item.m_GroupId];
			if(Item.m_LayerId < 0 || static_cast<size_t>(Item.m_LayerId) >= vTargets.size() || !vTargets[Item.m_LayerId])
				continue;
			if(Size < static_cast<int>(sizeof(CMapItemAutoMapperConfig)))
			{
				log_error("editor/load", "Automapper configuration %d has truncated settings.", i);
				return std::nullopt;
			}
			mem_copy(&Item, pData, sizeof(Item));
			Resources.m_vAutoMapperConfigs.push_back(Item);
		}
		return Resources;
	}
	catch(const std::bad_alloc &)
	{
		log_error("editor/load", "Out of memory while preparing map resources.");
		return std::nullopt;
	}
	catch(const std::length_error &)
	{
		log_error("editor/load", "Map resources exceeded container capacity.");
		return std::nullopt;
	}
}

#endif
