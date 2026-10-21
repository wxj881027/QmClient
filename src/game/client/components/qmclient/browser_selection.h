#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_SELECTION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_SELECTION_H

#include <cstddef>
#include <vector>

// 列表框返回可见行下标，详情和好友高亮使用浏览器排序列表中的下标。
inline int QmBrowserSelectedSortedIndex(const std::vector<int> &vVisibleServerIndices, int VisibleIndex)
{
	if(VisibleIndex < 0 || static_cast<size_t>(VisibleIndex) >= vVisibleServerIndices.size())
		return -1;
	return vVisibleServerIndices[VisibleIndex];
}

#endif
