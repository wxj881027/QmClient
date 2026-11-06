#include <engine/shared/config.h>

#include <game/client/components/qmclient/warlist_model.h>

#include <gtest/gtest.h>

class CQmWarListModelTest : public ::testing::Test
{
	int m_PreviousAllowDuplicates = 0;

protected:
	CQmWarListModel m_Model;
	void SetUp() override
	{
		m_PreviousAllowDuplicates = g_Config.m_QmWarListAllowDuplicates;
		g_Config.m_QmWarListAllowDuplicates = 1;
	}
	void TearDown() override
	{
		g_Config.m_QmWarListAllowDuplicates = m_PreviousAllowDuplicates;
	}
};

TEST_F(CQmWarListModelTest, OverridingNameRebuildsLookupAndInvalidatesUiRevision)
{
	m_Model.AddWarEntry("Alice", "", "old", "enemy");
	ASSERT_TRUE(m_Model.RebuildWarEntryLookup());
	ASSERT_EQ(m_Model.m_WarEntriesByName.count("Alice"), 1u);
	const uint64_t Before = m_Model.Revision();
	m_Model.UpdateWarEntry(0, "Bob", "", "new", m_Model.FindWarType("team"));
	EXPECT_NE(m_Model.Revision(), Before);
	ASSERT_TRUE(m_Model.RebuildWarEntryLookup());
	EXPECT_EQ(m_Model.m_WarEntriesByName.count("Alice"), 0u);
	ASSERT_EQ(m_Model.m_WarEntriesByName.count("Bob"), 1u);
	const auto *pEntry = m_Model.m_WarEntriesByName.at("Bob").front();
	EXPECT_STREQ(pEntry->m_aReason, "new");
	EXPECT_EQ(pEntry->m_pWarType, m_Model.FindWarType("team"));
}

TEST_F(CQmWarListModelTest, SwitchingNameToClanMovesLookupDomain)
{
	m_Model.AddWarEntry("Alice", "", "", "enemy");
	ASSERT_TRUE(m_Model.RebuildWarEntryLookup());
	m_Model.UpdateWarEntry(0, "", "Clan", "", m_Model.FindWarType("enemy"));
	ASSERT_TRUE(m_Model.RebuildWarEntryLookup());
	EXPECT_TRUE(m_Model.m_WarEntriesByName.empty());
	EXPECT_EQ(m_Model.m_WarEntriesByClan.count("Clan"), 1u);
}

TEST_F(CQmWarListModelTest, ConsoleAppendAndRemovalInvalidateCachedEntryAddresses)
{
	m_Model.AddWarEntry("Alice", "", "", "enemy");
	ASSERT_TRUE(m_Model.RebuildWarEntryLookup());
	const uint64_t BeforeAppend = m_Model.Revision();
	m_Model.AddWarEntry("Bob", "", "", "team");
	EXPECT_NE(m_Model.Revision(), BeforeAppend);
	ASSERT_TRUE(m_Model.RebuildWarEntryLookup());
	EXPECT_EQ(m_Model.m_WarEntriesByName.at("Alice").front(), &m_Model.m_vWarEntries.front());
	const uint64_t BeforeRemove = m_Model.Revision();
	m_Model.RemoveWarEntry(&m_Model.m_vWarEntries.front());
	EXPECT_NE(m_Model.Revision(), BeforeRemove);
	ASSERT_TRUE(m_Model.RebuildWarEntryLookup());
	EXPECT_EQ(m_Model.m_WarEntriesByName.count("Alice"), 0u);
	EXPECT_EQ(m_Model.m_WarEntriesByName.at("Bob").front(), &m_Model.m_vWarEntries.front());
}

TEST_F(CQmWarListModelTest, GroupRenameInvalidatesFilterAndKeepsEntryAssociation)
{
	m_Model.AddWarEntry("Alice", "", "", "custom");
	CWarType *pType = m_Model.FindWarType("custom");
	m_Model.RefreshWarTypeIndices();
	const uint64_t Before = m_Model.Revision();
	m_Model.UpsertWarType(pType->m_Index, "renamed", ColorRGBA(0, 1, 0, 1));
	EXPECT_NE(m_Model.Revision(), Before);
	EXPECT_EQ(m_Model.FindWarType("renamed"), pType);
	EXPECT_EQ(m_Model.m_vWarEntries.front().m_pWarType, pType);
}

TEST_F(CQmWarListModelTest, RemovingGroupMovesEntriesToNone)
{
	m_Model.AddWarEntry("Alice", "", "", "custom");
	const uint64_t Before = m_Model.Revision();
	m_Model.RemoveWarType("custom");
	EXPECT_NE(m_Model.Revision(), Before);
	EXPECT_EQ(m_Model.m_vWarEntries.front().m_pWarType, m_Model.m_pWarTypeNone);
	m_Model.RefreshWarTypeIndices();
	EXPECT_EQ(m_Model.m_pWarTypeNone->m_Index, WARLIST_BUILTIN_GROUP_NONE);
}

TEST_F(CQmWarListModelTest, DuplicateReplacementRefreshesLookup)
{
	g_Config.m_QmWarListAllowDuplicates = 0;
	m_Model.AddWarEntry("Alice", "", "old", "enemy");
	ASSERT_TRUE(m_Model.RebuildWarEntryLookup());
	m_Model.AddWarEntry("Alice", "", "new", "team");
	ASSERT_TRUE(m_Model.RebuildWarEntryLookup());
	ASSERT_EQ(m_Model.m_vWarEntries.size(), 1u);
	ASSERT_EQ(m_Model.m_WarEntriesByName.at("Alice").size(), 1u);
	EXPECT_STREQ(m_Model.m_WarEntriesByName.at("Alice").front()->m_aReason, "new");
}

TEST_F(CQmWarListModelTest, InvalidEntryIndexDoesNotMutateModel)
{
	const uint64_t Before = m_Model.Revision();
	m_Model.UpdateWarEntry(-1, "Alice", "", "", m_Model.m_pWarTypeNone);
	m_Model.UpdateWarEntry(0, "Alice", "", "", m_Model.m_pWarTypeNone);
	EXPECT_EQ(m_Model.Revision(), Before);
	EXPECT_TRUE(m_Model.m_vWarEntries.empty());
}

TEST_F(CQmWarListModelTest, EnemyNameMatchesAcrossClans)
{
	m_Model.AddWarEntry("Alice", "", "", "enemy");
	EXPECT_TRUE(m_Model.MatchesEnemy("Alice", "One"));
	EXPECT_TRUE(m_Model.MatchesEnemy("Alice", "Two"));
	EXPECT_FALSE(m_Model.MatchesEnemy("Bob", "One"));
	EXPECT_FALSE(m_Model.MatchesEnemy("", ""));
}

TEST_F(CQmWarListModelTest, EnemyClanMatchesCurrentMembers)
{
	m_Model.AddWarEntry("", "Clan", "", "enemy");
	EXPECT_TRUE(m_Model.MatchesEnemy("Alice", "Clan"));
	EXPECT_TRUE(m_Model.MatchesEnemy("Bob", "Clan"));
	EXPECT_FALSE(m_Model.MatchesEnemy("Alice", "Other"));
	EXPECT_FALSE(m_Model.MatchesEnemy("Alice", ""));
}

TEST_F(CQmWarListModelTest, TeamAndCustomGroupsDoNotMatchEnemies)
{
	m_Model.AddWarEntry("Alice", "", "", "team");
	m_Model.AddWarEntry("", "Clan", "", "custom");
	EXPECT_FALSE(m_Model.MatchesEnemy("Alice", "Clan"));
	m_Model.AddWarEntry("Alice", "", "", "enemy");
	EXPECT_TRUE(m_Model.MatchesEnemy("Alice", "Clan"));
}

TEST_F(CQmWarListModelTest, EntryOverrideImmediatelyChangesEnemyMatch)
{
	m_Model.AddWarEntry("Alice", "", "", "enemy");
	const CQmWarListModel &Model = m_Model;
	ASSERT_TRUE(Model.MatchesEnemy("Alice", ""));
	m_Model.UpdateWarEntry(0, "Bob", "", "", m_Model.FindWarType("enemy"));
	EXPECT_FALSE(Model.MatchesEnemy("Alice", ""));
	EXPECT_TRUE(Model.MatchesEnemy("Bob", ""));
}

TEST_F(CQmWarListModelTest, RemovingEntryImmediatelyRestoresVisibility)
{
	m_Model.AddWarEntry("Alice", "", "", "enemy");
	ASSERT_TRUE(m_Model.MatchesEnemy("Alice", ""));
	m_Model.RemoveWarEntry("Alice", "", "enemy");
	EXPECT_FALSE(m_Model.MatchesEnemy("Alice", ""));
	m_Model.AddWarEntry("Alice", "", "", "enemy");
	EXPECT_TRUE(m_Model.MatchesEnemy("Alice", ""));
}

TEST_F(CQmWarListModelTest, DuplicateReplacementWithTeamStopsEnemyMatch)
{
	g_Config.m_QmWarListAllowDuplicates = 0;
	m_Model.AddWarEntry("Alice", "", "", "enemy");
	ASSERT_TRUE(m_Model.MatchesEnemy("Alice", ""));
	m_Model.AddWarEntry("Alice", "", "", "team");
	EXPECT_FALSE(m_Model.MatchesEnemy("Alice", ""));
}

TEST_F(CQmWarListModelTest, RenamingBuiltInEnemyGroupKeepsEnemyMatch)
{
	m_Model.AddWarEntry("Alice", "", "", "enemy");
	ASSERT_TRUE(m_Model.MatchesEnemy("Alice", ""));
	m_Model.UpsertWarType(WARLIST_BUILTIN_GROUP_ENEMY, "blocked", ColorRGBA(1, 0, 0, 1));
	EXPECT_TRUE(m_Model.MatchesEnemy("Alice", ""));
}

TEST_F(CQmWarListModelTest, AppendingEntriesKeepsExistingEnemyQueriesValid)
{
	m_Model.AddWarEntry("Alice", "", "", "enemy");
	ASSERT_TRUE(m_Model.MatchesEnemy("Alice", ""));
	for(int i = 0; i < 32; ++i)
	{
		char aName[32];
		str_format(aName, sizeof(aName), "Player%d", i);
		m_Model.AddWarEntry(aName, "", "", "team");
	}
	EXPECT_TRUE(m_Model.MatchesEnemy("Alice", ""));
	EXPECT_FALSE(m_Model.MatchesEnemy("Player31", ""));
}
