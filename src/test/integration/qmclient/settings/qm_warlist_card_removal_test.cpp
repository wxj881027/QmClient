#include <game/client/QmUi/cards/QmCardCatalogTClientInternal.h>

#include <gtest/gtest.h>

using qm_tclient_cards::SWarListRemoval;

TEST(QmWarListCardRemoval, StaleConfirmationKeepsEntriesAfterConsoleRemovalShiftsRows)
{
	CQmWarListModel Model;
	ASSERT_TRUE(Model.AddWarEntry("Alice", "", "", "enemy"));
	ASSERT_TRUE(Model.AddWarEntry("Bob", "", "", "enemy"));
	ASSERT_TRUE(Model.AddWarEntry("Carol", "", "", "enemy"));
	const SWarListRemoval Removal{Model.Revision(), 1};

	Model.RemoveWarEntry("Alice", "", "enemy");
	Removal.RemoveEntry(Model);

	ASSERT_EQ(Model.m_vWarEntries.size(), 2u);
	EXPECT_STREQ(Model.m_vWarEntries[0].m_aName, "Bob");
	EXPECT_STREQ(Model.m_vWarEntries[1].m_aName, "Carol");
}

TEST(QmWarListCardRemoval, CurrentConfirmationDeletesOnlyTheSelectedRow)
{
	CQmWarListModel Model;
	ASSERT_TRUE(Model.AddWarEntry("Alice", "", "", "enemy"));
	ASSERT_TRUE(Model.AddWarEntry("Bob", "", "", "enemy"));
	ASSERT_TRUE(Model.AddWarEntry("Carol", "", "", "enemy"));
	const SWarListRemoval Removal{Model.Revision(), 1};

	Removal.RemoveEntry(Model);

	ASSERT_EQ(Model.m_vWarEntries.size(), 2u);
	EXPECT_STREQ(Model.m_vWarEntries[0].m_aName, "Alice");
	EXPECT_STREQ(Model.m_vWarEntries[1].m_aName, "Carol");
}

TEST(QmWarListCardRemoval, StaleGroupConfirmationKeepsBothRenamedAndNewGroup)
{
	CQmWarListModel Model;
	ASSERT_TRUE(Model.AddWarEntry("Alice", "", "", "custom"));
	const size_t GroupIndex = Model.m_WarTypes.size() - 1;
	const SWarListRemoval Removal{Model.Revision(), GroupIndex};

	const ColorRGBA Color(0, 1, 0, 1);
	ASSERT_TRUE(Model.UpsertWarType(static_cast<int>(GroupIndex), "renamed", Color));
	ASSERT_TRUE(Model.AddWarEntry("Bob", "", "", "custom"));
	Removal.RemoveType(Model);

	ASSERT_EQ(Model.m_vWarEntries.size(), 2u);
	EXPECT_STREQ(Model.m_vWarEntries[0].m_pWarType->m_aWarName, "renamed");
	EXPECT_STREQ(Model.m_vWarEntries[1].m_pWarType->m_aWarName, "custom");
}

TEST(QmWarListCardRemoval, CurrentGroupConfirmationKeepsEntriesInTheDefaultGroup)
{
	CQmWarListModel Model;
	ASSERT_TRUE(Model.AddWarEntry("Alice", "", "", "custom"));
	const SWarListRemoval Removal{Model.Revision(), Model.m_WarTypes.size() - 1};

	Removal.RemoveType(Model);

	ASSERT_EQ(Model.m_vWarEntries.size(), 1u);
	EXPECT_EQ(Model.m_vWarEntries.front().m_pWarType, Model.m_pWarTypeNone);
	EXPECT_EQ(Model.FindWarType("custom"), Model.m_pWarTypeNone);
}

TEST(QmWarListCardRemoval, RenameCommitDoesNotEditASelectedBuiltinGroup)
{
	CQmWarListModel Model;
	const ColorRGBA Color(0, 1, 0, 1);
	const auto *pDefault = Model.m_pWarTypeNone;
	const uint64_t Revision = Model.Revision();

	qm_tclient_cards::RenameEditableWarType(Model, pDefault, "renamed", Color);

	EXPECT_EQ(Model.Revision(), Revision);
	EXPECT_STREQ(pDefault->m_aWarName, "none");
}

TEST(QmWarListCardRemoval, RenameCommitUpdatesTheSelectedEditableGroup)
{
	CQmWarListModel Model;
	ASSERT_TRUE(Model.AddWarEntry("Alice", "", "", "custom"));

	qm_tclient_cards::RenameEditableWarType(Model, Model.FindWarType("custom"), "renamed", ColorRGBA(0, 1, 0, 1));
	EXPECT_STREQ(Model.m_vWarEntries.front().m_pWarType->m_aWarName, "renamed");
}
