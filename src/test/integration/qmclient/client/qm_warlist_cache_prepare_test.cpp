#include <engine/shared/config.h>

#include <game/client/components/qmclient/warlist_model.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

class QmWarListCachePreparation : public ::testing::Test
{
	int m_OldDuplicates;

protected:
	void SetUp() override
	{
		m_OldDuplicates = g_Config.m_QmWarListAllowDuplicates;
		g_Config.m_QmWarListAllowDuplicates = 1;
	}
	void TearDown() override { g_Config.m_QmWarListAllowDuplicates = m_OldDuplicates; }

	static bool Fill(CQmWarListModel &Model, CWarDataCache &Cache, const char *pName, const char *pClan)
	{
		Model.RefreshWarTypeIndices();
		if(!Cache.Reset(Model.m_WarTypes.size()))
			return false;
		Model.VisitMatches(pName, pClan, [&](const CWarEntry &Entry, bool ByName) {
			Cache.ApplyMatch(Entry, ByName);
			return true;
		});
		return true;
	}
};

TEST_F(QmWarListCachePreparation, LinearAndIndexedVisitsKeepNameReasonPriorityAndGroupUnion)
{
	CQmWarListModel Model;
	ASSERT_TRUE(Model.AddWarType("custom", ColorRGBA(0, 0, 1, 1)));
	ASSERT_TRUE(Model.AddWarEntry("Alice", "", "first", "team"));
	ASSERT_TRUE(Model.AddWarEntry("", "Clan", "clan", "enemy"));
	ASSERT_TRUE(Model.AddWarEntry("Alice", "", "last name", "custom"));
	CWarDataCache Cache;
	auto ExpectDisplay = [&] {
		EXPECT_TRUE(Cache.m_WarName);
		EXPECT_TRUE(Cache.m_WarClan);
		EXPECT_STREQ(Cache.m_aReason, "last name");
		EXPECT_EQ(Cache.m_NameColor, ColorRGBA(0, 0, 1, 1));
		EXPECT_EQ(Cache.m_ClanColor, Model.FindWarType("enemy")->m_Color);
		ASSERT_EQ(Cache.m_WarGroupMatches.size(), 4u);
		EXPECT_TRUE(Cache.m_WarGroupMatches[1]);
		EXPECT_TRUE(Cache.m_WarGroupMatches[2]);
		EXPECT_TRUE(Cache.m_WarGroupMatches[3]);
	};
	ASSERT_TRUE(Model.m_WarEntryLookupDirty);
	ASSERT_TRUE(Fill(Model, Cache, "Alice", "Clan"));
	ExpectDisplay();
	ASSERT_TRUE(Model.RebuildWarEntryLookup());
	ASSERT_TRUE(Fill(Model, Cache, "Alice", "Clan"));
	ExpectDisplay();
}

TEST_F(QmWarListCachePreparation, LookupOrCacheAllocationFailureHasACompleteFallbackAndCanRetry)
{
	bool ReachedSuccess = false;
	int Failures = 0;
	for(int Allocation = 0; Allocation < 128; ++Allocation)
	{
		CQmWarListModel Model;
		ASSERT_TRUE(Model.AddWarEntry("Alice", "", "name", "custom"));
		ASSERT_TRUE(Model.AddWarEntry("", "Clan", "clan", "enemy"));
		CWarDataCache Cache;
		bool Prepared, Triggered;
		{
			CPredictionAllocationFailure Failure(Allocation);
			Model.RebuildWarEntryLookup();
			Prepared = Fill(Model, Cache, "Alice", "Clan");
			Triggered = Failure.Triggered();
		}
		if(!Triggered)
		{
			EXPECT_TRUE(Prepared);
			ReachedSuccess = Prepared;
			break;
		}
		++Failures;
		if(Prepared)
		{
			EXPECT_TRUE(Cache.m_WarName);
			EXPECT_TRUE(Cache.m_WarClan);
			EXPECT_STREQ(Cache.m_aReason, "name");
		}
		else
		{
			ASSERT_EQ(Cache.m_WarGroupMatches.size(), 3u);
			EXPECT_FALSE(Cache.m_WarName);
			EXPECT_FALSE(Cache.m_WarClan);
			EXPECT_STREQ(Cache.m_aReason, "");
		}
		ASSERT_TRUE(Model.RebuildWarEntryLookup());
		ASSERT_TRUE(Fill(Model, Cache, "Alice", "Clan"));
		EXPECT_TRUE(Cache.m_WarName);
		EXPECT_TRUE(Cache.m_WarClan);
		EXPECT_STREQ(Cache.m_aReason, "name");
		EXPECT_TRUE(Cache.m_WarGroupMatches[1]);
		EXPECT_TRUE(Cache.m_WarGroupMatches[3]);
	}
	EXPECT_GE(Failures, 3);
	EXPECT_TRUE(ReachedSuccess);
}

TEST_F(QmWarListCachePreparation, RemovingACustomOwnerRetargetsExistingEntriesToNone)
{
	CQmWarListModel Model;
	ASSERT_TRUE(Model.AddWarEntry("Alice", "", "reason", "custom"));
	CWarDataCache Cache;
	ASSERT_TRUE(Fill(Model, Cache, "Alice", ""));
	ASSERT_EQ(Cache.m_WarGroupMatches.size(), 4u);
	EXPECT_TRUE(Cache.m_WarGroupMatches[3]);
	Model.RemoveWarType("custom");
	ASSERT_TRUE(Fill(Model, Cache, "Alice", ""));
	ASSERT_EQ(Cache.m_WarGroupMatches.size(), 3u);
	EXPECT_TRUE(Cache.m_WarGroupMatches[0]);
	EXPECT_TRUE(Cache.m_WarName);
	EXPECT_EQ(Cache.m_NameColor, Model.m_pWarTypeNone->m_Color);
	EXPECT_EQ(Model.m_vWarEntries.front().m_pWarType, Model.m_pWarTypeNone);
}

TEST_F(QmWarListCachePreparation, EntryGrowthAndRenameDoNotReadTheOldCachedAddresses)
{
	CQmWarListModel Model;
	ASSERT_TRUE(Model.AddWarEntry("Alice", "", "reason", "enemy"));
	ASSERT_TRUE(Model.RebuildWarEntryLookup());
	for(int i = 0; i < 16; ++i)
	{
		char aName[16];
		str_format(aName, sizeof(aName), "Player%d", i);
		ASSERT_TRUE(Model.AddWarEntry(aName, "", "", "team"));
	}
	Model.UpdateWarEntry(0, "Bob", "", "updated", Model.FindWarType("enemy"));
	ASSERT_TRUE(Model.m_WarEntryLookupDirty);
	CWarDataCache Cache;
	ASSERT_TRUE(Fill(Model, Cache, "Alice", ""));
	EXPECT_FALSE(Cache.m_WarName);
	ASSERT_TRUE(Fill(Model, Cache, "Bob", ""));
	EXPECT_TRUE(Cache.m_WarName);
	EXPECT_STREQ(Cache.m_aReason, "updated");
	EXPECT_TRUE(Cache.m_WarGroupMatches[1]);
	ASSERT_TRUE(Model.RebuildWarEntryLookup());
	ASSERT_TRUE(Fill(Model, Cache, "Bob", ""));
	EXPECT_STREQ(Cache.m_aReason, "updated");
}
