#include <game/client/QmUi/SettingsTextPlanCommit.h>
#include <game/client/components/menus.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <new>
#include <optional>

namespace
{
	using TItem = CMenus::SMenuTextPlanItem;
	TItem Item(const char *pId)
	{
		TItem Result;
		Result.m_Page = 1;
		Result.m_TextId = pId;
		Result.m_Text = "translated text large enough to require owned heap storage";
		Result.m_LabelProps.m_vColorSplits.resize(2);
		return Result;
	}
	std::optional<std::pair<std::string, std::string>> ItemKeys(const TItem &Value)
	{
		if(Value.m_Page < 0)
			return std::nullopt;
		return std::make_pair("descriptor:" + Value.m_TextId, "key:" + Value.m_TextId);
	}
	struct SState
	{
		std::vector<TItem> m_vPlan{Item("old")};
		std::unordered_set<std::string> m_Descriptors{"descriptor:old"}, m_Keys{"key:old"};
		void ExpectOriginal() const
		{
			ASSERT_EQ(m_vPlan.size(), 1u);
			EXPECT_EQ(m_vPlan.front().m_TextId, "old");
			EXPECT_EQ(m_vPlan.front().m_LabelProps.m_vColorSplits.size(), 2u);
			EXPECT_EQ(m_Descriptors, (std::unordered_set<std::string>{"descriptor:old"}));
			EXPECT_EQ(m_Keys, (std::unordered_set<std::string>{"key:old"}));
		}
	};
	void CollectTwo(std::vector<TItem> &vItems)
	{
		vItems.push_back(Item("new-a"));
		vItems.push_back(Item("new-b"));
	}
}

TEST(SettingsTextPlanCommit, SuccessPublishesAllItemsAndBothIndices)
{
	SState State;
	ASSERT_TRUE(TryCollectSettingsTextPlan(State.m_vPlan, State.m_Descriptors, State.m_Keys, CollectTwo, ItemKeys));
	ASSERT_EQ(State.m_vPlan.size(), 3u);
	EXPECT_EQ(State.m_vPlan[1].m_TextId, "new-a");
	EXPECT_EQ(State.m_vPlan[2].m_TextId, "new-b");
	EXPECT_EQ(State.m_vPlan[2].m_Text, "translated text large enough to require owned heap storage");
	EXPECT_EQ(State.m_vPlan[2].m_LabelProps.m_vColorSplits.size(), 2u);
	EXPECT_EQ(State.m_Descriptors.size(), 3u);
	EXPECT_EQ(State.m_Keys.size(), 3u);
	EXPECT_EQ(State.m_Descriptors.count("descriptor:new-a"), 1u);
	EXPECT_EQ(State.m_Keys.count("key:new-b"), 1u);
}

TEST(SettingsTextPlanCommit, EmptyCollectionDoesNotAllocateOrChangePublishedValues)
{
	SState State;
	bool Committed, Triggered;
	size_t Attempts;
	{
		CPredictionAllocationFailure Failure;
		Committed = TryCollectSettingsTextPlan(State.m_vPlan, State.m_Descriptors, State.m_Keys, [](std::vector<TItem> &) {}, ItemKeys);
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_TRUE(Committed);
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
	State.ExpectOriginal();
}

TEST(SettingsTextPlanCommit, CollectorAllocationExceptionDiscardsItsUnpublishedPrefix)
{
	SState State;
	EXPECT_FALSE(TryCollectSettingsTextPlan(State.m_vPlan, State.m_Descriptors, State.m_Keys, [](std::vector<TItem> &vItems) {
		vItems.push_back(Item("partial"));
		throw std::bad_alloc(); }, ItemKeys));
	State.ExpectOriginal();
}

TEST(SettingsTextPlanCommit, UnknownCollectorExceptionLeavesPublishedStateUnchanged)
{
	SState State;
	EXPECT_FALSE(TryCollectSettingsTextPlan(State.m_vPlan, State.m_Descriptors, State.m_Keys, [](std::vector<TItem> &vItems) {
		vItems.push_back(Item("partial"));
		throw 7; }, ItemKeys));
	State.ExpectOriginal();
}

TEST(SettingsTextPlanCommit, KeyPreparationExceptionRollsBackPreviouslyInsertedIndices)
{
	SState State;
	int Calls = 0;
	EXPECT_FALSE(TryCollectSettingsTextPlan(State.m_vPlan, State.m_Descriptors, State.m_Keys, CollectTwo, [&](const TItem &Value) {
		if(++Calls == 2)
			throw std::length_error("key preparation");
		return ItemKeys(Value);
	}));
	EXPECT_EQ(Calls, 2);
	State.ExpectOriginal();
}

TEST(SettingsTextPlanCommit, RollbackPreservesExistingDuplicateIndices)
{
	SState State;
	int Calls = 0;
	EXPECT_FALSE(TryCollectSettingsTextPlan(State.m_vPlan, State.m_Descriptors, State.m_Keys, [](std::vector<TItem> &vItems) {
		vItems.push_back(Item("old"));
		CollectTwo(vItems); }, [&](const TItem &Value) {
		if(++Calls == 3)
			throw 7;
		return ItemKeys(Value); }));
	State.ExpectOriginal();
}

TEST(SettingsTextPlanCommit, EveryAllocationFailureRetainsPublishedValuesAndRetryAppendsOnce)
{
	bool ReachedSuccess = false;
	int Failures = 0;
	for(int Allocation = 0; Allocation < 256; ++Allocation)
	{
		SCOPED_TRACE(Allocation);
		SState State;
		bool Committed, Triggered;
		{
			CPredictionAllocationFailure Failure(Allocation);
			Committed = TryCollectSettingsTextPlan(State.m_vPlan, State.m_Descriptors, State.m_Keys, CollectTwo, ItemKeys);
			Triggered = Failure.Triggered();
		}
		if(!Triggered)
		{
			EXPECT_TRUE(Committed);
			ReachedSuccess = true;
			break;
		}
		EXPECT_FALSE(Committed);
		State.ExpectOriginal();
		++Failures;
		ASSERT_TRUE(TryCollectSettingsTextPlan(State.m_vPlan, State.m_Descriptors, State.m_Keys, CollectTwo, ItemKeys));
		ASSERT_EQ(State.m_vPlan.size(), 3u);
		EXPECT_EQ(State.m_Descriptors.size(), 3u);
		EXPECT_EQ(State.m_Keys.size(), 3u);
		EXPECT_EQ(State.m_vPlan[1].m_TextId, "new-a");
		EXPECT_EQ(State.m_vPlan[2].m_TextId, "new-b");
	}
	EXPECT_TRUE(ReachedSuccess);
	EXPECT_GT(Failures, 0);
}

TEST(SettingsTextPlanCommit, UnindexedItemsStillRemainInTheIncrementalPlan)
{
	SState State;
	ASSERT_TRUE(TryCollectSettingsTextPlan(State.m_vPlan, State.m_Descriptors, State.m_Keys, [](std::vector<TItem> &vItems) {
		TItem Unindexed = Item("unindexed");
		Unindexed.m_Page = -1;
		vItems.push_back(std::move(Unindexed)); }, ItemKeys));
	ASSERT_EQ(State.m_vPlan.size(), 2u);
	EXPECT_EQ(State.m_vPlan.back().m_TextId, "unindexed");
	EXPECT_EQ(State.m_Descriptors.size(), 1u);
	EXPECT_EQ(State.m_Keys.size(), 1u);
}

TEST(SettingsTextPlanCommit, DuplicateBatchItemsKeepTheirOrderAndShareIndices)
{
	SState State;
	ASSERT_TRUE(TryCollectSettingsTextPlan(State.m_vPlan, State.m_Descriptors, State.m_Keys, [](std::vector<TItem> &vItems) {
		vItems.push_back(Item("new"));
		vItems.push_back(Item("new")); }, ItemKeys));
	ASSERT_EQ(State.m_vPlan.size(), 3u);
	EXPECT_EQ(State.m_vPlan[1].m_TextId, "new");
	EXPECT_EQ(State.m_vPlan[2].m_TextId, "new");
	EXPECT_EQ(State.m_Descriptors.size(), 2u);
	EXPECT_EQ(State.m_Keys.size(), 2u);
}

TEST(SettingsTextPlanCommit, LaterBatchCannotSeeIndicesFromAnEarlierFailedBatch)
{
	SState State;
	int Calls = 0;
	ASSERT_FALSE(TryCollectSettingsTextPlan(State.m_vPlan, State.m_Descriptors, State.m_Keys, CollectTwo, [&](const TItem &Value) {
		if(++Calls == 2)
			throw 7;
		return ItemKeys(Value);
	}));
	ASSERT_TRUE(TryCollectSettingsTextPlan(State.m_vPlan, State.m_Descriptors, State.m_Keys, [](std::vector<TItem> &vItems) { vItems.push_back(Item("later")); }, ItemKeys));
	ASSERT_EQ(State.m_vPlan.size(), 2u);
	EXPECT_EQ(State.m_vPlan.back().m_TextId, "later");
	EXPECT_EQ(State.m_Descriptors.count("descriptor:new-a"), 0u);
	EXPECT_EQ(State.m_Keys.count("key:new-a"), 0u);
	EXPECT_EQ(State.m_Descriptors.count("descriptor:later"), 1u);
	EXPECT_EQ(State.m_Keys.count("key:later"), 1u);
}
