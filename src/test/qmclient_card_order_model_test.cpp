#include <game/client/ui/card_order_model.h>

#include <gtest/gtest.h>

TEST(CardOrderModel, NormalizesPageColumnsAndAppliesKnownOverrides)
{
	CCardOrderModel Model;
	Model.SetDefaults({
		{"qm.a", "home", ECardColumn::LEFT, 8},
		{"qm.b", "other", ECardColumn::LEFT, 2},
		{"qm.c", "home", ECardColumn::RIGHT, 0},
		{"qm.a", "home", ECardColumn::LEFT, 0},
		{"invalid-column", "home", static_cast<ECardColumn>(99), 0},
	});
	ASSERT_TRUE(Model.ApplyOverrides({{"qm.a", "home", ECardColumn::RIGHT, 4}, {"unknown", "home", ECardColumn::LEFT, 0}, {"qm.b", "home", ECardColumn::LEFT, -1}}));
	EXPECT_TRUE(Model.IsDirty());
	ASSERT_EQ(Model.EntriesForPage("home", ECardColumn::RIGHT).size(), 2);
	EXPECT_EQ(Model.EntriesForPage("home", ECardColumn::RIGHT)[0]->m_Id, "qm.c");
	EXPECT_EQ(Model.EntriesForPage("home", ECardColumn::RIGHT)[1]->m_Id, "qm.a");
	ASSERT_EQ(Model.EntriesForPage("other", ECardColumn::LEFT).size(), 1);
	EXPECT_EQ(Model.EntriesForPage("other", ECardColumn::LEFT)[0]->m_Order, 0);
	EXPECT_EQ(Model.Entries().size(), 3);
}

TEST(CardOrderModel, MoveUpdatesLayoutRevisionWithoutChangingStateIdentity)
{
	CCardOrderModel Model;
	Model.SetDefaults({{"qm.a", "home", ECardColumn::LEFT, 0}, {"qm.b", "home", ECardColumn::LEFT, 1}});
	const unsigned StateRevision = Model.StateRevision();
	const unsigned LayoutRevision = Model.LayoutRevision();
	ASSERT_TRUE(Model.Move("qm.b", ECardColumn::RIGHT, 0));
	EXPECT_EQ(Model.StateRevision(), StateRevision);
	EXPECT_GT(Model.LayoutRevision(), LayoutRevision);
	EXPECT_EQ(Model.Find("qm.b")->m_Column, ECardColumn::RIGHT);
	EXPECT_FALSE(Model.Move("missing", ECardColumn::LEFT, 0));
	EXPECT_FALSE(Model.Move("qm.a", static_cast<ECardColumn>(99), 0));
}

TEST(CardOrderModel, MoveInsertsAtRequestedPositionAndCompactsSource)
{
	CCardOrderModel Model;
	Model.SetDefaults({{"qm.a", "home", ECardColumn::LEFT, 0}, {"qm.b", "home", ECardColumn::LEFT, 1}, {"qm.c", "home", ECardColumn::LEFT, 2}, {"qm.d", "home", ECardColumn::RIGHT, 0}});
	ASSERT_TRUE(Model.Move("qm.c", ECardColumn::LEFT, 0));
	EXPECT_EQ(Model.EntriesForPage("home", ECardColumn::LEFT)[0]->m_Id, "qm.c");
	ASSERT_TRUE(Model.Move("qm.b", ECardColumn::RIGHT, 0));
	EXPECT_EQ(Model.EntriesForPage("home", ECardColumn::LEFT).size(), 2);
	EXPECT_EQ(Model.EntriesForPage("home", ECardColumn::RIGHT)[0]->m_Id, "qm.b");
	EXPECT_EQ(Model.EntriesForPage("home", ECardColumn::RIGHT)[1]->m_Id, "qm.d");
}

TEST(CardOrderModel, MoveToPageChangesPlacementAndPreservesStateRevision)
{
	CCardOrderModel Model;
	Model.SetDefaults({{"qm.a", "visual", ECardColumn::LEFT, 0}, {"qm.b", "visual", ECardColumn::LEFT, 1}, {"qm.c", "hud", ECardColumn::LEFT, 0}});
	const unsigned StateRevision = Model.StateRevision();
	ASSERT_TRUE(Model.MoveToPage("qm.a", "hud", ECardColumn::LEFT, 1));
	EXPECT_EQ(Model.StateRevision(), StateRevision);
	EXPECT_TRUE(Model.EntriesForPage("visual", ECardColumn::LEFT).size() == 1);
	ASSERT_EQ(Model.EntriesForPage("hud", ECardColumn::LEFT).size(), 2);
	EXPECT_EQ(Model.EntriesForPage("hud", ECardColumn::LEFT)[1]->m_Id, "qm.a");
	EXPECT_FALSE(Model.MoveToPage("qm.a", "", ECardColumn::LEFT, 0));
}

TEST(CardOrderModel, SerializesAndMergesUserOverrides)
{
	const std::vector<SCardOrderEntry> Defaults = {
		{"qm.a", "home", ECardColumn::LEFT, 0},
		{"qm.b", "home", ECardColumn::RIGHT, 0},
		{"qm.c", "hud", ECardColumn::RIGHT, 0},
	};
	CCardOrderModel Source;
	Source.SetDefaults(Defaults);
	ASSERT_TRUE(Source.MoveToPage("qm.a", "hud", ECardColumn::RIGHT, 1));
	const std::string Serialized = Source.Serialize();
	CCardOrderModel Reloaded;
	EXPECT_TRUE(Reloaded.LoadMerged(Serialized, Defaults));
	EXPECT_EQ(Reloaded.Find("qm.a")->m_Column, ECardColumn::RIGHT);
	EXPECT_EQ(Reloaded.Find("qm.a")->m_Order, 1);
	EXPECT_EQ(Reloaded.Find("qm.a")->m_PageId, "hud");
	EXPECT_FALSE(Reloaded.IsDirty());
}

TEST(CardOrderModel, AcceptsLegacyFormatAndIgnoresInvalidEntries)
{
	const std::vector<SCardOrderEntry> Defaults = {{"qm.a", "home", ECardColumn::LEFT, 0}, {"qm.b", "home", ECardColumn::RIGHT, 0}};
	CCardOrderModel Model;
	EXPECT_TRUE(Model.LoadMerged("qm.a:2:3;unknown:1:0;qm.b|wrong|left|-1;", Defaults));
	EXPECT_EQ(Model.Find("qm.a")->m_Column, ECardColumn::RIGHT);
	EXPECT_EQ(Model.Find("qm.a")->m_Order, 1);
	EXPECT_EQ(Model.Find("qm.a")->m_PageId, "home");
	EXPECT_EQ(Model.Find("qm.b")->m_Column, ECardColumn::RIGHT);
}

TEST(CardOrderModel, LoadMergedAllowsCrossPageOverrideAndFiltersDuplicateIds)
{
	const std::vector<SCardOrderEntry> Defaults = {{"qm.a", "home", ECardColumn::LEFT, 0}, {"qm.b", "hud", ECardColumn::RIGHT, 0}};
	CCardOrderModel Model;
	ASSERT_TRUE(Model.LoadMerged("qm.a|hud|right|1;qm.a|later|left|0;", Defaults));
	EXPECT_EQ(Model.Find("qm.a")->m_PageId, "hud");
	EXPECT_EQ(Model.Find("qm.a")->m_Column, ECardColumn::RIGHT);
	EXPECT_EQ(Model.Find("qm.a")->m_Order, 1);
}
