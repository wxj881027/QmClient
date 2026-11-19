#include <game/client/components/menus.h>

#include <gtest/gtest.h>

TEST(QmAssetsEditorFilterSelection, SearchKeepsTheEditedAssetWhenItIsOutsideTheResults)
{
	EXPECT_EQ(qmclient::assets_editor::FilteredMainAssetSelection({2, 5}, 0, true), -1);
}

TEST(QmAssetsEditorFilterSelection, SearchSelectsTheFirstMatchWhenThereAreNoEdits)
{
	EXPECT_EQ(qmclient::assets_editor::FilteredMainAssetSelection({2, 5}, 0, false), 0);
}

TEST(QmAssetsEditorFilterSelection, CurrentAssetRemainsSelectedWhenItStillMatches)
{
	EXPECT_EQ(qmclient::assets_editor::FilteredMainAssetSelection({2, 5}, 5, true), 1);
}
