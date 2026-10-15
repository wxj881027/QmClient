#include "collab_camera.h"

#include <game/editor/mapitems/layer_group.h>
#include <game/editor/mapitems/layer_quads.h>
#include <game/editor/mapitems/layer_sounds.h>
#include <game/editor/mapitems/layer_tiles.h>
#include <game/editor/mapitems/map.h>

#include <algorithm>
#include <limits>

// 捕获依赖编辑器图层派生类；这些类的 RTTI 只在 CLIENT 目标链接集里，
// 行为测试只链接本文件之外的核心逻辑（见 collab_camera.cpp 顶部说明）。
CEditorCollabPlacementSnapshot CEditorCollabPlacementSnapshot::Capture(const CEditorMap &Map)
{
	CEditorCollabPlacementSnapshot Snapshot;
	size_t Remaining = 64 * 1024 * 1024;
	const auto Reserve = [&Remaining](size_t Count, size_t ElementSize) {
		if(Count > Remaining / ElementSize)
			return false;
		Remaining -= Count * ElementSize;
		return true;
	};
	const auto Abort = [&Snapshot]() {
		Snapshot.m_Complete = false;
		Snapshot.m_vLayers.clear();
		return Snapshot;
	};
	for(size_t GroupIndex = 0; GroupIndex < Map.m_vpGroups.size(); ++GroupIndex)
	{
		const auto &Group = *Map.m_vpGroups[GroupIndex];
		for(const auto &pLayer : Group.m_vpLayers)
		{
			if(pLayer->m_Type != LAYERTYPE_TILES && pLayer->m_Type != LAYERTYPE_QUADS && pLayer->m_Type != LAYERTYPE_SOUNDS)
				continue;
			if(!Reserve(1, sizeof(CEditorCollabPlacementSnapshot::SLayer)))
				return Abort();
			CEditorCollabPlacementSnapshot::SLayer Layer;
			Layer.m_Kind = pLayer->m_Type;
			Layer.m_GroupIndex = (int)GroupIndex;
			Layer.m_GroupName = Group.m_aName;
			Layer.m_LayerName = pLayer->m_aName;
			Layer.m_GroupOffset = ivec2(Group.m_OffsetX, Group.m_OffsetY);
			Layer.m_Parallax = ivec2(Group.m_ParallaxX, Group.m_ParallaxY);
			if(pLayer->m_Type == LAYERTYPE_TILES)
			{
				const auto &Tiles = static_cast<const CLayerTiles &>(*pLayer);
				const size_t Count = (size_t)Tiles.m_Width * Tiles.m_Height;
				if(!Reserve(Count, sizeof(uint16_t)))
					return Abort();
				Layer.m_Kind |= (Tiles.m_HasGame | (Tiles.m_HasFront << 1) | (Tiles.m_HasTele << 2) |
							(Tiles.m_HasSpeedup << 3) | (Tiles.m_HasSwitch << 4) | (Tiles.m_HasTune << 5))
						<< 8;
				Layer.m_Width = Tiles.m_Width;
				Layer.m_Height = Tiles.m_Height;
				Layer.m_vTiles.resize(Count);
				for(size_t i = 0; i < Count; ++i)
				{
					const CTile &Tile = Tiles.m_pTiles[i];
					Layer.m_vTiles[i] = Tile.m_Index == 0 ? 0 : Tile.m_Index | ((Tile.m_Flags & (TILEFLAG_XFLIP | TILEFLAG_YFLIP | TILEFLAG_ROTATE)) << 8);
				}
			}
			else if(pLayer->m_Type == LAYERTYPE_QUADS)
			{
				const auto &Quads = static_cast<const CLayerQuads &>(*pLayer).m_vQuads;
				if(!Reserve(Quads.size(), sizeof(std::array<int, 8>)))
					return Abort();
				Layer.m_vQuads.reserve(Quads.size());
				for(const CQuad &Quad : Quads)
				{
					std::array<int, 8> aPoints;
					for(int i = 0; i < 4; ++i)
					{
						aPoints[2 * i] = Quad.m_aPoints[i].x;
						aPoints[2 * i + 1] = Quad.m_aPoints[i].y;
					}
					Layer.m_vQuads.push_back(aPoints);
				}
				std::sort(Layer.m_vQuads.begin(), Layer.m_vQuads.end());
			}
			else
			{
				const auto &Sounds = static_cast<const CLayerSounds &>(*pLayer).m_vSources;
				if(!Reserve(Sounds.size(), sizeof(std::array<int, 2>)))
					return Abort();
				Layer.m_vSounds.reserve(Sounds.size());
				for(const CSoundSource &Sound : Sounds)
					Layer.m_vSounds.push_back({Sound.m_Position.x, Sound.m_Position.y});
				std::sort(Layer.m_vSounds.begin(), Layer.m_vSounds.end());
			}
			Snapshot.m_vLayers.push_back(std::move(Layer));
		}
	}
	return Snapshot;
}
