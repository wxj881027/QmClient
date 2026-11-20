#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_HISTORY_ROWS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_HISTORY_ROWS_H

#include <game/client/components/tclient/map_history.h>
#include <game/client/ui.h>

#include <new>
#include <stdexcept>
#include <vector>

namespace QmMapHistoryUi
{
	// 列表开始前只准备CPU数据；失败不使用旧索引，避免模型增删后访问失效位置。
	inline bool PrepareRows(const QmMapHistory::CMapHistory &History, QmMapHistory::EMapHistoryFilter Filter,
		std::vector<size_t> &vIndices, std::vector<int> &vItemIds, std::vector<CButtonContainer> &vRemoveButtons) noexcept
	{
		try
		{
			History.SortedIndices(Filter, vIndices);
			if(!vIndices.empty())
			{
				vItemIds.resize(vIndices.size());
				vRemoveButtons.resize(vIndices.size());
			}
			return true;
		}
		catch(const std::bad_alloc &)
		{
			vIndices.clear();
			return false;
		}
		catch(const std::length_error &)
		{
			vIndices.clear();
			return false;
		}
	}
}

#endif
