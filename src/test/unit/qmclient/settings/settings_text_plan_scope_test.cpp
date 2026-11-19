#include <game/client/QmUi/SettingsMenuPrewarmScope.h>
#include <game/client/components/menus.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <new>

namespace
{
	using TItem = CMenus::SMenuTextPlanItem;
	using TScope = CSettingsTextPlanCollectionScope<TItem>;
	TItem PendingItem()
	{
		TItem Item;
		Item.m_TextId = "previous pending text identifier that exceeds small string storage";
		Item.m_Text = "previous translated text that exceeds small string storage";
		Item.m_AllowlistReason = "previous reason that exceeds small string storage";
		Item.m_SourceTag = "previous source tag that exceeds small string storage";
		Item.m_Page = 7;
		Item.m_Tab = 3;
		Item.m_Rect = {10, 20, 180, 24};
		Item.m_LabelProps.m_vColorSplits.resize(3);
		return Item;
	}
	struct SState
	{
		bool m_Collecting = false;
		std::vector<TItem> m_vPrevious, m_vTemporary;
		std::vector<TItem> *m_pCollection = &m_vPrevious;
		bool m_PendingActive = true;
		TItem m_Pending = PendingItem();
	};
	void ExpectOriginal(const SState &State, const TItem &Expected)
	{
		EXPECT_FALSE(State.m_Collecting);
		EXPECT_EQ(State.m_pCollection, &State.m_vPrevious);
		EXPECT_TRUE(State.m_PendingActive);
		EXPECT_EQ(State.m_Pending.m_TextId, Expected.m_TextId);
		EXPECT_EQ(State.m_Pending.m_Text, Expected.m_Text);
		EXPECT_EQ(State.m_Pending.m_AllowlistReason, Expected.m_AllowlistReason);
		EXPECT_EQ(State.m_Pending.m_SourceTag, Expected.m_SourceTag);
		EXPECT_EQ(State.m_Pending.m_Page, Expected.m_Page);
		EXPECT_EQ(State.m_Pending.m_Tab, Expected.m_Tab);
		EXPECT_FLOAT_EQ(State.m_Pending.m_Rect.w, Expected.m_Rect.w);
		EXPECT_EQ(State.m_Pending.m_LabelProps.m_vColorSplits.size(), Expected.m_LabelProps.m_vColorSplits.size());
	}
}

TEST(SettingsTextPlanScope, TemporaryCollectionRestoresActivePendingItemAndKeepsCollectedOutput)
{
	SState State;
	const TItem Original = State.m_Pending;
	{
		const TScope Scope(State.m_Collecting, State.m_pCollection, State.m_PendingActive, State.m_Pending, State.m_vTemporary);
		EXPECT_TRUE(State.m_Collecting);
		EXPECT_EQ(State.m_pCollection, &State.m_vTemporary);
		EXPECT_FALSE(State.m_PendingActive);
		State.m_Pending = {};
		State.m_Pending.m_Text = "new collected text";
		State.m_pCollection->push_back(State.m_Pending);
	}
	ExpectOriginal(State, Original);
	ASSERT_EQ(State.m_vTemporary.size(), 1u);
	EXPECT_EQ(State.m_vTemporary.front().m_Text, "new collected text");
}

TEST(SettingsTextPlanScope, CollectionExceptionRestoresPointerAndPendingState)
{
	SState State;
	const TItem Original = State.m_Pending;
	EXPECT_THROW({
		const TScope Scope(State.m_Collecting, State.m_pCollection, State.m_PendingActive, State.m_Pending, State.m_vTemporary);
		State.m_Pending = {};
		State.m_PendingActive = true;
		throw 7; }, int);
	ExpectOriginal(State, Original);
}

TEST(SettingsTextPlanScope, EveryPendingSnapshotAllocationFailureLeavesOriginalContextAndCanRetry)
{
	bool ReachedSuccess = false;
	int Failures = 0;
	for(int Allocation = 0; Allocation < 32; ++Allocation)
	{
		SCOPED_TRACE(Allocation);
		SState State;
		const TItem Original = State.m_Pending;
		bool Threw = false, Triggered = false;
		{
			CPredictionAllocationFailure Failure(Allocation);
			try
			{
				const TScope Scope(State.m_Collecting, State.m_pCollection, State.m_PendingActive, State.m_Pending, State.m_vTemporary);
			}
			catch(const std::bad_alloc &)
			{
				Threw = true;
			}
			Triggered = Failure.Triggered();
		}
		ExpectOriginal(State, Original);
		if(!Triggered)
		{
			EXPECT_FALSE(Threw);
			ReachedSuccess = true;
			break;
		}
		EXPECT_TRUE(Threw);
		++Failures;
		{
			const TScope Retry(State.m_Collecting, State.m_pCollection, State.m_PendingActive, State.m_Pending, State.m_vTemporary);
			EXPECT_EQ(State.m_pCollection, &State.m_vTemporary);
		}
		ExpectOriginal(State, Original);
	}
	EXPECT_TRUE(ReachedSuccess);
	EXPECT_GT(Failures, 0);
}

TEST(SettingsTextPlanScope, PendingRestorationDoesNotAllocate)
{
	SState State;
	const TItem Original = State.m_Pending;
	std::optional<TScope> Scope;
	Scope.emplace(State.m_Collecting, State.m_pCollection, State.m_PendingActive, State.m_Pending, State.m_vTemporary);
	State.m_Pending = {};
	bool Triggered;
	size_t Attempts;
	{
		CPredictionAllocationFailure Failure;
		Scope.reset();
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
	ExpectOriginal(State, Original);
}

TEST(SettingsTextPlanScope, InactivePendingPayloadIsNotCopiedOrRestored)
{
	SState State;
	State.m_PendingActive = false;
	bool Triggered;
	size_t Attempts;
	{
		CPredictionAllocationFailure Failure;
		{
			const TScope Scope(State.m_Collecting, State.m_pCollection, State.m_PendingActive, State.m_Pending, State.m_vTemporary);
			State.m_Pending = {};
			State.m_Pending.m_Page = 19;
			State.m_PendingActive = true;
		}
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
	EXPECT_FALSE(State.m_Collecting);
	EXPECT_EQ(State.m_pCollection, &State.m_vPrevious);
	EXPECT_FALSE(State.m_PendingActive);
	EXPECT_EQ(State.m_Pending.m_Page, 19);
}

TEST(SettingsTextPlanScope, NestedCollectionRestoresOuterContextBeforeOriginalContext)
{
	SState State;
	const TItem Original = State.m_Pending;
	std::vector<TItem> vNested;
	{
		const TScope Outer(State.m_Collecting, State.m_pCollection, State.m_PendingActive, State.m_Pending, State.m_vTemporary);
		State.m_Pending = {};
		State.m_Pending.m_Page = 23;
		State.m_PendingActive = true;
		{
			const TScope Inner(State.m_Collecting, State.m_pCollection, State.m_PendingActive, State.m_Pending, vNested);
			EXPECT_EQ(State.m_pCollection, &vNested);
			State.m_Pending = {};
		}
		EXPECT_TRUE(State.m_Collecting);
		EXPECT_EQ(State.m_pCollection, &State.m_vTemporary);
		EXPECT_TRUE(State.m_PendingActive);
		EXPECT_EQ(State.m_Pending.m_Page, 23);
	}
	ExpectOriginal(State, Original);
}

TEST(SettingsTextPlanScope, NestedSnapshotFailureLeavesOuterCollectionUsable)
{
	SState State;
	const TItem Original = State.m_Pending;
	std::vector<TItem> vNested;
	{
		const TScope Outer(State.m_Collecting, State.m_pCollection, State.m_PendingActive, State.m_Pending, State.m_vTemporary);
		State.m_PendingActive = true;
		bool Threw = false;
		{
			CPredictionAllocationFailure Failure;
			try
			{
				const TScope Inner(State.m_Collecting, State.m_pCollection, State.m_PendingActive, State.m_Pending, vNested);
			}
			catch(const std::bad_alloc &)
			{
				Threw = true;
			}
		}
		ASSERT_TRUE(Threw);
		EXPECT_EQ(State.m_pCollection, &State.m_vTemporary);
		EXPECT_TRUE(State.m_PendingActive);
		State.m_pCollection->push_back(State.m_Pending);
	}
	ExpectOriginal(State, Original);
	EXPECT_EQ(State.m_vTemporary.size(), 1u);
	EXPECT_TRUE(vNested.empty());
}

TEST(SettingsTextPlanScope, NullOriginalCollectionIsRestoredAfterAnException)
{
	SState State;
	State.m_pCollection = nullptr;
	EXPECT_THROW({
		const TScope Scope(State.m_Collecting, State.m_pCollection, State.m_PendingActive, State.m_Pending, State.m_vTemporary);
		throw std::bad_alloc(); }, std::bad_alloc);
	EXPECT_EQ(State.m_pCollection, nullptr);
	EXPECT_FALSE(State.m_Collecting);
}

TEST(SettingsTextPlanScope, PageTabAndBooleanRestoreDuringExceptionUnwinding)
{
	int Page = 3, Tab = 8;
	bool Opening = false;
	EXPECT_THROW({
		const CSettingsTextPlanValueRestore PageRestore(Page);
		const CSettingsTextPlanValueRestore TabRestore(Tab);
		const CSettingsTextPlanValueRestore OpeningRestore(Opening);
		Page = 19;
		Tab = 2;
		Opening = true;
		throw 7; }, int);
	EXPECT_EQ(Page, 3);
	EXPECT_EQ(Tab, 8);
	EXPECT_FALSE(Opening);
}

TEST(SettingsTextPlanScope, NestedPageRestorationReturnsToOuterThenOriginalValue)
{
	int Page = 3;
	{
		const CSettingsTextPlanValueRestore Outer(Page);
		Page = 8;
		{
			const CSettingsTextPlanValueRestore Inner(Page);
			Page = 19;
		}
		EXPECT_EQ(Page, 8);
	}
	EXPECT_EQ(Page, 3);
}
