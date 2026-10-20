#ifndef GAME_EDITOR_MAPITEMS_LOAD_RESOURCES_H
#define GAME_EDITOR_MAPITEMS_LOAD_RESOURCES_H

#include <base/log.h>

#include <game/map/map_render_items.h>

#include <optional>
#include <vector>

namespace EditorMapLoad
{
	struct SResourceItems
	{
		std::vector<CMapItemImage_v2> m_vImages;
		std::vector<CMapItemSound> m_vSounds;
	};

	// 共享 CMap 已校验组与图层基础头；在编辑器清空旧地图前补查资源尾部。
	inline std::optional<SResourceItems> ReadResourceItems(IEngineMap &Map)
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
		for(int g = 0; g < GroupsNum; ++g)
		{
			const auto *pGroup = static_cast<const CMapItemGroup_v1 *>(Map.GetItem(GroupsStart + g));
			// 与编辑器实际导入范围一致，不改变未引用或被跳过条目的处理。
			if(pGroup->m_Version < 1 || pGroup->m_Version > 3)
				continue;
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
			}
		}
		return Resources;
	}
}

#endif
