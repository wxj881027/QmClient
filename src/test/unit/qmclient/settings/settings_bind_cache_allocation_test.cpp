#include <game/client/QmUi/SettingsBindCache.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

TEST(SettingsBindCacheAllocation, DefaultConstructionAndEmptyInvalidationDoNotAllocate)
{
	bool Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		CSettingsBindCache Cache;
		Cache.Invalidate();
		Cache.Invalidate();
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
}
