#ifndef GAME_EDITOR_MAPITEMS_LOAD_ENVELOPES_H
#define GAME_EDITOR_MAPITEMS_LOAD_ENVELOPES_H

#include <base/log.h>

#include <game/map/map_render_items.h>
#include <game/map/render_map.h>

namespace EditorMapLoad
{
	// 只在候选地图上校验，不分配声明点数对应的数组，也不改动旧编辑内容。
	inline bool ValidateEnvelopeItems(IEngineMap &Map)
	{
		if(!Map.IsLoaded())
			return false;
		const CMapBasedEnvelopePointAccess Points(&Map);
		int Start, Num;
		Map.GetType(MAPITEMTYPE_ENVELOPE, &Start, &Num);
		for(int i = 0; i < Num; ++i)
		{
			const int Index = Start + i;
			const auto Item = MapRenderItems::ReadEnvelope(Map, Index);
			if(!Item || Map.GetItemSize(Index) < static_cast<int>(sizeof(CMapItemEnvelope_v1)) ||
				(Item->m_Version >= 2 && Map.GetItemSize(Index) < static_cast<int>(sizeof(CMapItemEnvelope))))
			{
				log_error("editor/load", "Envelope %d has a truncated header, name or synchronization field.", i);
				return false;
			}
			// 空包络不消费起点；非空范围先做减法比较，避免加法溢出。
			if(Item->m_NumPoints < 0 ||
				(Item->m_NumPoints > 0 && (Item->m_StartPoint < 0 || Item->m_StartPoint > Points.NumPointsMax() ||
								  Item->m_NumPoints > Points.NumPointsMax() - Item->m_StartPoint)))
			{
				log_error("editor/load", "Envelope %d has an invalid point range.", i);
				return false;
			}
		}
		return true;
	}
}

#endif
