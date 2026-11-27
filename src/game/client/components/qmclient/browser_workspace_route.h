#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_WORKSPACE_ROUTE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_WORKSPACE_ROUTE_H

namespace QmBrowserWorkspace
{
	// 收藏地图工作区的页签顺序：收藏地图 / 地图游玩历史 / 本地存档 / 统计数据。
	inline constexpr int TAB_COUNT = 4;
	inline constexpr int TAB_FAVORITES = 0;
	inline constexpr int TAB_HISTORY = 1;
	inline constexpr int TAB_SAVES = 2;
	inline constexpr int TAB_STATISTICS = 3;

	enum class EIngameContent
	{
		SERVER_BROWSER,
		STATISTICS,
	};

	// 游戏内 ESC 的「浏览」页同时承载服务器列表和统计数据子页：
	// 统计数据不是独立的顶部页签，而是收藏地图工作区的子页，由 m_MenuPage 记录。
	constexpr EIngameContent ResolveIngameContent(int MenuPage, int StatisticsPage)
	{
		return MenuPage == StatisticsPage ? EIngameContent::STATISTICS : EIngameContent::SERVER_BROWSER;
	}
} // namespace QmBrowserWorkspace

#endif
