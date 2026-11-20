#ifndef GAME_MAP_QM_ENTITY_OVERLAY_BUILD_PLAN_H
#define GAME_MAP_QM_ENTITY_OVERLAY_BUILD_PLAN_H

#include <game/mapitems.h>

#include <array>

// 在基础实体层遍历时收集类别，只调度地图中实际存在的特殊覆盖层。
class CQmEntityOverlayBuildPlan
{
public:
	enum ECategory
	{
		DEATH,
		FREEZE,
		UNFREEZE,
		DEEP_FREEZE,
		DEEP_UNFREEZE,
		NUM_CATEGORIES,
	};

	static ECategory Classify(int Index)
	{
		switch(Index)
		{
		case TILE_DEATH: return DEATH;
		case TILE_FREEZE:
		case TILE_LFREEZE: return FREEZE;
		case TILE_UNFREEZE:
		case TILE_LUNFREEZE: return UNFREEZE;
		case TILE_DFREEZE: return DEEP_FREEZE;
		case TILE_DUNFREEZE: return DEEP_UNFREEZE;
		default: return NUM_CATEGORIES;
		}
	}

	void Record(int Index, bool IncludeDeath = true)
	{
		const ECategory Category = Classify(Index);
		if(Category != NUM_CATEGORIES && (IncludeDeath || Category != DEATH))
			m_aPresent[Category] = true;
	}

	ECategory NextPresent(int Start = 0) const
	{
		for(int Category = Start; Category < NUM_CATEGORIES; ++Category)
		{
			if(m_aPresent[Category])
				return static_cast<ECategory>(Category);
		}
		return NUM_CATEGORIES;
	}

private:
	std::array<bool, NUM_CATEGORIES> m_aPresent{};
};

#endif
