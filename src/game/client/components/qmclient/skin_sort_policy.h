#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_SORT_POLICY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_SORT_POLICY_H

#include <ctime>

// 皮肤列表的取值排序策略。渲染、I/O 与容器状态都不参与，便于单元测试与基准。
enum class ESkinSortOrder
{
	LESS,
	GREATER,
	EQUAL_KEYS,
};

// 时间排序的取值比较：目录来源（下载/目录索引）的 LastModified 会在下载完成时
// 刷新；若参与排序，浏览 7k 条目时每下载成功一张就会重排整个列表，把用户正在
// 看的位置换成别的皮肤。只有双方都是本地文件时才用修改时间。
constexpr ESkinSortOrder SkinListValueOrder(bool LeftFavorite, bool RightFavorite,
	int LeftReleaseDate, int RightReleaseDate,
	time_t LeftModified, time_t RightModified,
	bool LeftLocalSource, bool RightLocalSource, bool SortByTime)
{
	if(LeftFavorite != RightFavorite)
		return LeftFavorite ? ESkinSortOrder::LESS : ESkinSortOrder::GREATER;
	if(SortByTime)
	{
		if(LeftReleaseDate != RightReleaseDate)
			return LeftReleaseDate > RightReleaseDate ? ESkinSortOrder::LESS : ESkinSortOrder::GREATER;
		if(LeftLocalSource && RightLocalSource && LeftModified != RightModified)
			return LeftModified > RightModified ? ESkinSortOrder::LESS : ESkinSortOrder::GREATER;
	}
	return ESkinSortOrder::EQUAL_KEYS;
}

#endif
