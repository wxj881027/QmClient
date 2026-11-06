#include <engine/shared/config.h>

#include <game/client/components/qmclient/warlist_model.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <array>
#include <new>

class QmWarListAllocation : public ::testing::Test
{
	int m_OldDuplicates;

protected:
	void SetUp() override
	{
		m_OldDuplicates = g_Config.m_QmWarListAllowDuplicates;
		g_Config.m_QmWarListAllowDuplicates = 1;
	}
	void TearDown() override { g_Config.m_QmWarListAllowDuplicates = m_OldDuplicates; }
};

TEST_F(QmWarListAllocation, ConstructorFailureCanBeFollowedByAValidModelWithBuiltinIdentities)
{
	bool Constructed = false, Triggered;
	{
		CPredictionAllocationFailure Failure;
		try
		{
			CQmWarListModel Model;
			Constructed = true;
		}
		catch(const std::bad_alloc &)
		{
		}
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Constructed);
	CQmWarListModel Model;
	ASSERT_EQ(Model.m_WarTypes.size(), 3u);
	EXPECT_EQ(Model.FindWarType("none"), Model.m_WarTypes[0]);
	EXPECT_EQ(Model.FindWarType("enemy"), Model.m_WarTypes[1]);
	EXPECT_EQ(Model.FindWarType("team"), Model.m_WarTypes[2]);
	EXPECT_TRUE(Model.LoadWarType(0, "none", ColorRGBA(1, 1, 1, 1)));
	EXPECT_FALSE(Model.ConfigIncomplete());
}

TEST_F(QmWarListAllocation, TypePreparationFailuresPublishNothingAndKeepBuiltinPointersStable)
{
	bool ReachedSuccess = false;
	int Failures = 0;
	for(int Allocation = 0; Allocation < 16; ++Allocation)
	{
		CQmWarListModel Model;
		const std::array<CWarType *, 3> aBuiltins{Model.m_WarTypes[0], Model.m_WarTypes[1], Model.m_WarTypes[2]};
		const auto Before = Model.Revision();
		bool Added, Triggered;
		{
			CPredictionAllocationFailure Failure(Allocation);
			Added = Model.AddWarType("custom", ColorRGBA(0, 0, 1, 1));
			Triggered = Failure.Triggered();
		}
		if(!Triggered)
		{
			EXPECT_TRUE(Added);
			EXPECT_EQ(Model.m_WarTypes.size(), 4u);
			ReachedSuccess = Added;
			break;
		}
		++Failures;
		EXPECT_FALSE(Added);
		EXPECT_EQ(Model.Revision(), Before);
		EXPECT_EQ(Model.m_WarTypes.size(), 3u);
		EXPECT_EQ(Model.FindWarType("custom"), Model.m_pWarTypeNone);
		EXPECT_FALSE(Model.ConfigIncomplete());
		ASSERT_TRUE(Model.AddWarType("custom", ColorRGBA(0, 0, 1, 1)));
		ASSERT_TRUE(Model.AddWarEntry("Alice", "", "reason", "custom"));
		Model.RemoveWarType("custom");
		EXPECT_EQ(Model.m_vWarEntries.front().m_pWarType, Model.m_pWarTypeNone);
		for(int i = 0; i < 3; ++i)
			EXPECT_EQ(Model.m_WarTypes[i], aBuiltins[i]);
	}
	EXPECT_GE(Failures, 2);
	EXPECT_TRUE(ReachedSuccess);
}

TEST_F(QmWarListAllocation, ExistingTypeColorUpdateDoesNotAllocate)
{
	CQmWarListModel Model;
	CWarType *pEnemy = Model.FindWarType("enemy");
	bool Added, Triggered;
	{
		CPredictionAllocationFailure Failure;
		Added = Model.AddWarType("enemy", ColorRGBA(0, 0, 1, 1));
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Added);
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Model.FindWarType("enemy"), pEnemy);
	EXPECT_EQ(pEnemy->m_Color, ColorRGBA(0, 0, 1, 1));
}

TEST_F(QmWarListAllocation, EntryAndNewTypeArePreparedBeforeAnyPublishedMutation)
{
	bool ReachedSuccess = false;
	for(int Allocation = 0; Allocation < 32; ++Allocation)
	{
		CQmWarListModel Model;
		ASSERT_TRUE(Model.AddWarEntry("Old", "", "old", "enemy"));
		ASSERT_TRUE(Model.RebuildWarEntryLookup());
		const CWarEntry *pOld = &Model.m_vWarEntries.front();
		const auto Before = Model.Revision();
		bool Added, Triggered;
		{
			CPredictionAllocationFailure Failure(Allocation);
			Added = Model.AddWarEntry("New", "", "new", "custom");
			Triggered = Failure.Triggered();
		}
		if(!Triggered)
		{
			EXPECT_TRUE(Added);
			EXPECT_EQ(Model.m_vWarEntries.size(), 2u);
			EXPECT_NE(Model.FindWarType("custom"), Model.m_pWarTypeNone);
			ReachedSuccess = Added;
			break;
		}
		EXPECT_FALSE(Added);
		EXPECT_EQ(Model.Revision(), Before);
		EXPECT_EQ(Model.m_WarTypes.size(), 3u);
		ASSERT_EQ(Model.m_vWarEntries.size(), 1u);
		EXPECT_EQ(&Model.m_vWarEntries.front(), pOld);
		EXPECT_FALSE(Model.m_WarEntryLookupDirty);
		EXPECT_EQ(Model.m_WarEntriesByName.at("Old").front(), pOld);
		ASSERT_TRUE(Model.AddWarEntry("New", "", "new", "custom"));
		EXPECT_TRUE(Model.MatchesEnemy("Old", ""));
		EXPECT_FALSE(Model.MatchesEnemy("New", ""));
	}
	EXPECT_TRUE(ReachedSuccess);
}

TEST_F(QmWarListAllocation, KnownTypeEntryGrowthFailureKeepsTheOldListAndAllowsRetry)
{
	CQmWarListModel Model;
	ASSERT_TRUE(Model.AddWarEntry("Old", "", "old", "enemy"));
	while(Model.m_vWarEntries.size() < Model.m_vWarEntries.capacity())
		ASSERT_TRUE(Model.AddWarEntry("Filler", "", "", "enemy"));
	const size_t OldSize = Model.m_vWarEntries.size();
	const auto Before = Model.Revision();
	bool Added, Triggered;
	{
		CPredictionAllocationFailure Failure;
		Added = Model.AddWarEntry("New", "", "new", "team");
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Added);
	EXPECT_EQ(Model.Revision(), Before);
	EXPECT_EQ(Model.m_vWarEntries.size(), OldSize);
	EXPECT_FALSE(Model.ConfigIncomplete());
	ASSERT_TRUE(Model.AddWarEntry("New", "", "new", "team"));
	EXPECT_EQ(Model.m_vWarEntries.size(), OldSize + 1);
}

TEST_F(QmWarListAllocation, DuplicateReplacementReusesItsExistingEntryCapacity)
{
	g_Config.m_QmWarListAllowDuplicates = 0;
	CQmWarListModel Model;
	ASSERT_TRUE(Model.AddWarEntry("Alice", "", "old", "enemy"));
	bool Added, Triggered;
	{
		CPredictionAllocationFailure Failure;
		Added = Model.AddWarEntry("Alice", "", "new", "team");
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Added);
	EXPECT_FALSE(Triggered);
	ASSERT_EQ(Model.m_vWarEntries.size(), 1u);
	EXPECT_STREQ(Model.m_vWarEntries.front().m_aReason, "new");
	EXPECT_FALSE(Model.MatchesEnemy("Alice", ""));
}

TEST_F(QmWarListAllocation, EntryInputsMayBorrowExistingFieldsDuringGrowth)
{
	CQmWarListModel Model;
	ASSERT_TRUE(Model.AddWarEntry("Alice", "", "kept", "enemy"));
	while(Model.m_vWarEntries.size() < Model.m_vWarEntries.capacity())
		ASSERT_TRUE(Model.AddWarEntry("Filler", "", "", "enemy"));
	const size_t OldSize = Model.m_vWarEntries.size();
	const auto &Old = Model.m_vWarEntries.front();
	ASSERT_TRUE(Model.AddWarEntry(Old.m_aName, Old.m_aClan, Old.m_aReason, "team"));
	ASSERT_EQ(Model.m_vWarEntries.size(), OldSize + 1);
	EXPECT_STREQ(Model.m_vWarEntries.back().m_aName, "Alice");
	EXPECT_STREQ(Model.m_vWarEntries.back().m_aReason, "kept");
	EXPECT_EQ(Model.m_vWarEntries.back().m_pWarType, Model.FindWarType("team"));
}

TEST_F(QmWarListAllocation, EveryLookupAllocationFailureClearsPrefixesAndLeavesLinearMatchesUsable)
{
	bool ReachedSuccess = false;
	for(int Allocation = 0; Allocation < 128; ++Allocation)
	{
		CQmWarListModel Model;
		ASSERT_TRUE(Model.AddWarEntry("Alice", "", "name", "enemy"));
		ASSERT_TRUE(Model.AddWarEntry("", "Clan", "clan", "team"));
		ASSERT_TRUE(Model.AddWarEntry("Bob", "", "other", "enemy"));
		bool Indexed, Triggered;
		{
			CPredictionAllocationFailure Failure(Allocation);
			Indexed = Model.RebuildWarEntryLookup();
			Triggered = Failure.Triggered();
		}
		if(!Triggered)
		{
			EXPECT_TRUE(Indexed);
			ReachedSuccess = Indexed;
			break;
		}
		EXPECT_FALSE(Indexed);
		EXPECT_TRUE(Model.m_WarEntryLookupDirty);
		EXPECT_TRUE(Model.m_WarEntriesByName.empty());
		EXPECT_TRUE(Model.m_WarEntriesByClan.empty());
		std::array<const CWarEntry *, 2> aMatches{};
		int Count = 0;
		Model.VisitMatches("Alice", "Clan", [&](const CWarEntry &Entry, bool) {
			if(Count < 2)
				aMatches[Count] = &Entry;
			++Count;
			return true;
		});
		ASSERT_EQ(Count, 2);
		EXPECT_EQ(aMatches[0], &Model.m_vWarEntries[0]);
		EXPECT_EQ(aMatches[1], &Model.m_vWarEntries[1]);
		EXPECT_TRUE(Model.RebuildWarEntryLookup());
	}
	EXPECT_TRUE(ReachedSuccess);
}

TEST_F(QmWarListAllocation, EnemyQueriesKeepNameDomainPriorityWhenTheIndexCannotAllocate)
{
	CQmWarListModel Model;
	ASSERT_TRUE(Model.AddWarEntry("Alice", "", "", "enemy"));
	Model.UpdateWarEntry(0, "Alice", "Clan", "", Model.FindWarType("enemy"));
	bool Matched, Triggered;
	{
		CPredictionAllocationFailure Failure;
		Matched = Model.MatchesEnemy("Bob", "Clan");
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Matched);
	{
		CPredictionAllocationFailure Failure;
		Matched = Model.MatchesEnemy("Alice", "Other");
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_TRUE(Matched);
}

TEST_F(QmWarListAllocation, FailedConfigEntryLoadStaysIncompleteAfterLaterSuccessfulCommands)
{
	CQmWarListModel Model;
	bool Loaded, Triggered;
	{
		CPredictionAllocationFailure Failure;
		Loaded = Model.LoadWarEntry("Alice", "", "", "enemy");
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Loaded);
	EXPECT_TRUE(Model.ConfigIncomplete());
	ASSERT_TRUE(Model.LoadWarEntry("Alice", "", "", "enemy"));
	EXPECT_TRUE(Model.ConfigIncomplete());
	CQmWarListModel NewSession;
	EXPECT_FALSE(NewSession.ConfigIncomplete());
}

TEST_F(QmWarListAllocation, FailedConfigTypeLoadStaysIncompleteAfterAValidOverride)
{
	CQmWarListModel Model;
	bool Loaded, Triggered;
	{
		CPredictionAllocationFailure Failure;
		Loaded = Model.LoadWarType(100, "custom", ColorRGBA(0, 0, 1, 1));
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Loaded);
	EXPECT_TRUE(Model.ConfigIncomplete());
	ASSERT_TRUE(Model.LoadWarType(1, "enemy", ColorRGBA(0, 0, 1, 1)));
	EXPECT_TRUE(Model.ConfigIncomplete());
}

TEST_F(QmWarListAllocation, PlayerCacheFailureKeepsThreeSafeNeutralBuiltinSlotsAndCanRetry)
{
	CWarDataCache Cache;
	Cache.m_WarName = Cache.m_WarClan = true;
	Cache.m_WarGroupMatches[1] = Cache.m_WarGroupMatches[2] = true;
	str_copy(Cache.m_aReason, "old");
	Cache.m_NameColor = ColorRGBA(1, 0, 0, 1);
	const size_t Target = Cache.m_WarGroupMatches.capacity() + 1;
	bool Prepared, Triggered;
	{
		CPredictionAllocationFailure Failure;
		Prepared = Cache.Reset(Target);
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Prepared);
	ASSERT_EQ(Cache.m_WarGroupMatches.size(), 3u);
	EXPECT_FALSE(Cache.m_WarGroupMatches[1]);
	EXPECT_FALSE(Cache.m_WarGroupMatches[2]);
	EXPECT_FALSE(Cache.m_WarName);
	EXPECT_FALSE(Cache.m_WarClan);
	EXPECT_STREQ(Cache.m_aReason, "");
	EXPECT_EQ(Cache.m_NameColor, ColorRGBA(1, 1, 1, 1));
	ASSERT_TRUE(Cache.Reset(Target));
	EXPECT_EQ(Cache.m_WarGroupMatches.size(), Target);
}

TEST_F(QmWarListAllocation, PlayerCacheResetReusesCapacityAndClearsCustomMatches)
{
	CWarDataCache Cache;
	ASSERT_TRUE(Cache.Reset(64));
	Cache.m_WarGroupMatches[20] = true;
	bool Prepared, Triggered;
	{
		CPredictionAllocationFailure Failure;
		Prepared = Cache.Reset(5);
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Prepared);
	EXPECT_FALSE(Triggered);
	ASSERT_EQ(Cache.m_WarGroupMatches.size(), 5u);
	EXPECT_TRUE(std::all_of(Cache.m_WarGroupMatches.begin(), Cache.m_WarGroupMatches.end(), [](char Match) { return !Match; }));
}
