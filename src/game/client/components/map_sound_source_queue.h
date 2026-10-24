#ifndef GAME_CLIENT_COMPONENTS_MAP_SOUND_SOURCE_QUEUE_H
#define GAME_CLIENT_COMPONENTS_MAP_SOUND_SOURCE_QUEUE_H

#include <engine/sound.h>

#include <game/layers.h>
#include <game/map/map_render_items.h>

#include <span>
#include <vector>

struct SMapSoundSource
{
	int m_Sound = -1;
	bool m_HighDetail = false;
	ISound::CVoiceHandle m_Voice;
	const CMapItemGroup *m_pGroup = nullptr;
	const CSoundSource *m_pSource = nullptr;
};

// 调用前须终结旧来源的播放；复用队列容量，完整准备后才重新发布。
// 来源仍借用地图数据，调用方须在卸载地图前清空队列。
template<typename TAllocator>
bool PrepareMapSoundSourceQueue(const CLayers &Layers, std::span<const int> aSounds,
	std::vector<SMapSoundSource, TAllocator> &vOutput) noexcept
{
	try
	{
		std::vector<SMapSoundSource, TAllocator> vCandidate(vOutput.get_allocator());
		vCandidate.swap(vOutput);
		vCandidate.clear();
		IMap *pMap = Layers.Map();
		if(!pMap)
			return false;
		int LayersStart, LayersNum;
		pMap->GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);
		for(int GroupIndex = 0; GroupIndex < Layers.NumGroups(); ++GroupIndex)
		{
			const CMapItemGroup *pGroup = Layers.GetGroup(GroupIndex);
			if(!pGroup)
				continue;
			for(int LayerIndex = 0; LayerIndex < pGroup->m_NumLayers; ++LayerIndex)
			{
				const CMapItemLayer *pLayer = Layers.GetLayer(pGroup->m_StartLayer + LayerIndex);
				if(!pLayer || pLayer->m_Type != LAYERTYPE_SOUNDS)
					continue;
				const auto SoundLayer = MapRenderItems::ReadSoundLayer(*pMap, LayersStart + pGroup->m_StartLayer + LayerIndex);
				if(!SoundLayer || SoundLayer->m_Version < 1 || SoundLayer->m_Version > 2)
					continue;
				if(SoundLayer->m_Sound < 0 || static_cast<size_t>(SoundLayer->m_Sound) >= aSounds.size() || aSounds[SoundLayer->m_Sound] == -1)
					continue;
				const auto Sources = MapRenderItems::ReadDataItems<CSoundSource>(*pMap, SoundLayer->m_Data,
					SoundLayer->m_NumSources, MapRenderItems::EDataCountPolicy::ALLOW_PARTIAL);
				for(const auto &Source : Sources)
				{
					vCandidate.push_back({SoundLayer->m_Sound, (pLayer->m_Flags & LAYERFLAG_DETAIL) != 0,
						{}, pGroup, &Source});
				}
			}
		}
		vOutput.swap(vCandidate);
		return true;
	}
	catch(...)
	{
		// 候选自行回收；分配或读取失败不发布部分来源。
		vOutput.clear();
		return false;
	}
}

#endif // GAME_CLIENT_COMPONENTS_MAP_SOUND_SOURCE_QUEUE_H
