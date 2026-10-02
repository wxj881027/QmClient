#include <game/client/QmUi/SettingsCardWidth.h>
#include <gtest/gtest.h>

TEST(SettingsCardWidth, TogglesOnlySelectedCardAndRestoresItsColumn)
{
	qm_card_order::CModel Model;
	Model.SetEntries({{"left", "tab", 1, 0}, {"right", "tab", 2, 0}, {"other", "other-tab", 1, 0}});
	int Column = -1, Order = -1;
	EXPECT_TRUE(ToggleSettingsCardWidth(Model, "right", 1, Column, Order));
	EXPECT_EQ(Model.Entry(Model.FindByStableId("right")).m_Column, 0);
	EXPECT_EQ(Model.Entry(Model.FindByStableId("left")).m_Column, 1);
	EXPECT_STREQ(Model.Entry(Model.FindByStableId("other")).m_pDefaultTab, "other-tab");
	EXPECT_TRUE(ToggleSettingsCardWidth(Model, "right", 1, Column, Order));
	EXPECT_EQ(Model.Entry(Model.FindByStableId("right")).m_Column, 2);
	EXPECT_EQ(Model.Entry(Model.FindByStableId("right")).m_OrderInColumn, 0);
}

TEST(SettingsCardWidth, SavedFullWidthUsesExistingLayoutFormat)
{
	const std::vector<qm_card_order::SEntry> Defaults{{"one", "tab", 2, 0}, {"two", "tab", 1, 0}};
	qm_card_order::CModel Model;
	Model.SetEntries(Defaults);
	int Column = -1, Order = -1;
	ToggleSettingsCardWidth(Model, "one", 2, Column, Order);
	char aSaved[256];
	ASSERT_TRUE(Model.Serialize(aSaved, sizeof(aSaved)));
	qm_card_order::CModel Reloaded;
	ASSERT_TRUE(Reloaded.LoadMerged(aSaved, Defaults));
	EXPECT_EQ(Reloaded.Entry(Reloaded.FindByStableId("one")).m_Column, 0);
	Column = Order = -1;
	ToggleSettingsCardWidth(Reloaded, "one", 2, Column, Order);
	EXPECT_EQ(Reloaded.Entry(Reloaded.FindByStableId("one")).m_Column, 2);
}

TEST(SettingsCardWidth, MissingCardLeavesLayoutUntouched)
{
	qm_card_order::CModel Model;
	Model.SetEntries({{"one", "tab", 1, 0}});
	const auto Revision = Model.LayoutRevision();
	int Column = -1, Order = -1;
	EXPECT_FALSE(ToggleSettingsCardWidth(Model, "missing", 1, Column, Order));
	EXPECT_EQ(Model.LayoutRevision(), Revision);
}
