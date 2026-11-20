#include <game/client/components/qmclient/browser_selection.h>

#include <gtest/gtest.h>

#include <limits>
#include <vector>

TEST(BrowserSelection, FilteredRowsResolveToTheActualSortedServer)
{
	const std::vector<int> vVisible = {2, 5, 9};
	EXPECT_EQ(QmBrowserSelectedSortedIndex(vVisible, 0), 2);
	EXPECT_EQ(QmBrowserSelectedSortedIndex(vVisible, 1), 5);
	EXPECT_EQ(QmBrowserSelectedSortedIndex(vVisible, 2), 9);
}

TEST(BrowserSelection, UnfilteredRowsKeepTheirSelection)
{
	const std::vector<int> vVisible = {0, 1, 2};
	EXPECT_EQ(QmBrowserSelectedSortedIndex(vVisible, 0), 0);
	EXPECT_EQ(QmBrowserSelectedSortedIndex(vVisible, 2), 2);
}

TEST(BrowserSelection, MissingOrNegativeSelectionDoesNotResolveToAServer)
{
	const std::vector<int> vVisible = {3, 7};
	EXPECT_EQ(QmBrowserSelectedSortedIndex(vVisible, -1), -1);
	EXPECT_EQ(QmBrowserSelectedSortedIndex(vVisible, std::numeric_limits<int>::min()), -1);
	EXPECT_EQ(QmBrowserSelectedSortedIndex({}, 0), -1);
}

TEST(BrowserSelection, ASelectionOutsideTheCurrentVisibleRowsIsRejected)
{
	const std::vector<int> vVisible = {3, 7};
	EXPECT_EQ(QmBrowserSelectedSortedIndex(vVisible, 2), -1);
	EXPECT_EQ(QmBrowserSelectedSortedIndex(vVisible, std::numeric_limits<int>::max()), -1);
}

TEST(BrowserSelection, RefilteringUsesTheNewMappingAndRejectsRemovedRows)
{
	std::vector<int> vVisible = {1, 6};
	EXPECT_EQ(QmBrowserSelectedSortedIndex(vVisible, 1), 6);
	vVisible = {6};
	EXPECT_EQ(QmBrowserSelectedSortedIndex(vVisible, 0), 6);
	EXPECT_EQ(QmBrowserSelectedSortedIndex(vVisible, 1), -1);
	vVisible.clear();
	EXPECT_EQ(QmBrowserSelectedSortedIndex(vVisible, 0), -1);
}
