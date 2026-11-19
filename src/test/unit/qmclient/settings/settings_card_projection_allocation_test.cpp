#include <game/client/QmUi/SettingsCardDeckLogic.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <limits>
#include <new>

TEST(SettingsCardProjectionAllocation, FailedRebuildKeepsPreviousActiveCards)
{
	qm_card_order::CModel Model;
	Model.SetEntries({{"first", "projection", 1, 0}, {"second", "projection", 2, 0}});
	const std::vector<int> vOldActive{Model.FindByStableId("first")};
	const std::vector<int> vNewActive{Model.FindByStableId("first"), Model.FindByStableId("second")};

	settings_card_deck_logic::CProjectionCache Successful;
	const auto aOldColumns = Successful.Resolve(Model, "projection", vOldActive);
	size_t AllocationCount;
	{
		CPredictionAllocationFailure Trace(std::numeric_limits<size_t>::max());
		Successful.Resolve(Model, "projection", vNewActive);
		AllocationCount = Trace.Attempts();
	}
	const auto aNewColumns = Successful.Resolve(Model, "projection", vNewActive);
	ASSERT_NE(aOldColumns, aNewColumns);
	ASSERT_GT(AllocationCount, 0u);

	bool HadRejection = false;
	for(size_t Index = 0; Index < AllocationCount; ++Index)
	{
		SCOPED_TRACE(Index);
		settings_card_deck_logic::CProjectionCache Cache;
		ASSERT_EQ(Cache.Resolve(Model, "projection", vOldActive), aOldColumns);
		bool Rejected = false;
		bool Triggered;
		{
			CPredictionAllocationFailure Failure(Index);
			try
			{
				Cache.Resolve(Model, "projection", vNewActive);
			}
			catch(const std::bad_alloc &)
			{
				Rejected = true;
			}
			Triggered = Failure.Triggered();
		}
		ASSERT_TRUE(Triggered);
		if(Rejected)
		{
			HadRejection = true;
			EXPECT_EQ(Cache.Resolve(Model, "projection", vOldActive), aOldColumns);
			EXPECT_EQ(Cache.RebuildCount(), 1u);
		}
		EXPECT_EQ(Cache.Resolve(Model, "projection", vNewActive), aNewColumns);
		EXPECT_EQ(Cache.RebuildCount(), 2u);
	}
	EXPECT_TRUE(HadRejection);
}
