#include <game/client/components/menus.h>
#include <game/client/components/qmclient/browser_workspace_route.h>

#include <test/test.h>

TEST(QmBrowserWorkspaceRoute, IngameBrowserRendersStatisticsForStatisticsSubpage)
{
	EXPECT_EQ(
		QmBrowserWorkspace::ResolveIngameContent(CMenus::PAGE_STATS, CMenus::PAGE_STATS),
		QmBrowserWorkspace::EIngameContent::STATISTICS);
}

TEST(QmBrowserWorkspaceRoute, IngameBrowserKeepsServerListForBrowserPages)
{
	EXPECT_EQ(
		QmBrowserWorkspace::ResolveIngameContent(CMenus::PAGE_FAVORITE_MAPS, CMenus::PAGE_STATS),
		QmBrowserWorkspace::EIngameContent::SERVER_BROWSER);
	EXPECT_EQ(
		QmBrowserWorkspace::ResolveIngameContent(CMenus::PAGE_INTERNET, CMenus::PAGE_STATS),
		QmBrowserWorkspace::EIngameContent::SERVER_BROWSER);
}
