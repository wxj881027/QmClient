#include <game/client/QmUi/QmCardOrderModel.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/components/menus.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

TEST(SettingsCardOrder, CrossColumnDropMovesOnlyTheGlobalModel)
{
	qm_card_order::CModel Model;
	Model.LoadMerged("", qm_card_registry::BuildDefaultEntries());

	ASSERT_TRUE(CommitSettingsCardDeckDrop(Model, "graphics", "deck:graphics-display", 2, 0));
	const int Index = Model.FindByStableId("deck:graphics-display");
	ASSERT_GE(Index, 0);
	EXPECT_EQ(Model.Entry(Index).m_Column, 2);
	EXPECT_EQ(Model.Entry(Index).m_OrderInColumn, 0);
	EXPECT_STREQ(Model.Entry(Index).m_pDefaultTab, "graphics");
	EXPECT_TRUE(Model.IsDirty());
}

TEST(SettingsCardOrder, CrossColumnDropUsesVisibleOrderWhenHiddenCardsInterleave)
{
	qm_card_order::CModel Model;
	Model.SetEntries({
		{"sound-toggle", "sound", 1, 0},
		{"sound-hidden-before", "sound", 2, 0},
		{"sound-visible", "sound", 2, 1},
		{"sound-hidden-after", "sound", 2, 2},
	});
	const std::vector<int> vActiveStateIndices{
		Model.StateIndexForStableId("sound-toggle"),
		Model.StateIndexForStableId("sound-visible"),
	};

	ASSERT_TRUE(CommitSettingsCardDeckDrop(Model, "sound", "sound-toggle", 2, 1, &vActiveStateIndices));
	EXPECT_EQ(Model.StableIdOrder("", "sound", 2), (std::vector<std::string>{"sound-hidden-before", "sound-visible", "sound-toggle", "sound-hidden-after"}));
}

TEST(SettingsCardOrder, EmptyVisibleColumnDropsBeforeHiddenCards)
{
	qm_card_order::CModel Model;
	Model.SetEntries({
		{"sound-toggle", "sound", 1, 0},
		{"sound-hidden", "sound", 2, 0},
	});
	const std::vector<int> vActiveStateIndices{Model.StateIndexForStableId("sound-toggle")};

	ASSERT_TRUE(CommitSettingsCardDeckDrop(Model, "sound", "sound-toggle", 2, 0, &vActiveStateIndices));
	EXPECT_EQ(Model.StableIdOrder("", "sound", 2), (std::vector<std::string>{"sound-toggle", "sound-hidden"}));
}

TEST(SettingsCardOrder, SameColumnVisualNoOpPreservesHiddenRelativeOrder)
{
	qm_card_order::CModel Model;
	Model.SetEntries({
		{"sound-toggle", "sound", 2, 0},
		{"sound-hidden", "sound", 2, 1},
		{"sound-visible", "sound", 2, 2},
	});
	const std::vector<int> vActiveStateIndices{
		Model.StateIndexForStableId("sound-toggle"),
		Model.StateIndexForStableId("sound-visible"),
	};

	EXPECT_FALSE(CommitSettingsCardDeckDrop(Model, "sound", "sound-toggle", 2, 0, &vActiveStateIndices));
	EXPECT_EQ(Model.StableIdOrder("", "sound", 2), (std::vector<std::string>{"sound-toggle", "sound-hidden", "sound-visible"}));
}
