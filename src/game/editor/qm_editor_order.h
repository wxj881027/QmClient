#ifndef GAME_EDITOR_QM_EDITOR_ORDER_H
#define GAME_EDITOR_QM_EDITOR_ORDER_H

#include <cstddef>
#include <vector>

namespace QmEditorOrder
{
	// 先移除整组选中项，再按最终下标插入，避免同一容器内移动时中间下标漂移。
	template<typename T>
	bool MoveItemsToIndices(std::vector<T> &vSource, std::vector<T> &vDestination, const std::vector<int> &vSourceIndices, const std::vector<int> &vDestinationIndices)
	{
		if(vSourceIndices.empty() || vSourceIndices.size() != vDestinationIndices.size())
			return false;
		const bool SameContainer = &vSource == &vDestination;
		if(vSourceIndices.size() > vSource.size())
			return false;
		const size_t DestinationSize = vDestination.size() - (SameContainer ? vSourceIndices.size() : 0);
		for(size_t Index = 0; Index < vSourceIndices.size(); ++Index)
		{
			if(vSourceIndices[Index] < 0 || (size_t)vSourceIndices[Index] >= vSource.size() ||
				vDestinationIndices[Index] < 0 || (size_t)vDestinationIndices[Index] > DestinationSize + Index ||
				(Index > 0 && (vSourceIndices[Index] <= vSourceIndices[Index - 1] || vDestinationIndices[Index] <= vDestinationIndices[Index - 1])))
				return false;
		}
		std::vector<T> vItems;
		vItems.reserve(vSourceIndices.size());
		for(int Index : vSourceIndices)
			vItems.push_back(vSource[Index]);
		for(auto It = vSourceIndices.rbegin(); It != vSourceIndices.rend(); ++It)
			vSource.erase(vSource.begin() + *It);
		for(size_t Index = 0; Index < vItems.size(); ++Index)
			vDestination.insert(vDestination.begin() + vDestinationIndices[Index], vItems[Index]);
		return true;
	}
}

#endif
