#include <game/client/QmUi/SettingsCardCollapseState.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <cstddef>

TEST(SettingsCardCollapseAllocation, LongIdentityLookupDoesNotAllocate)
{
	qm_card_collapse::CState State;
	ASSERT_TRUE(State.Load("deck:existing-card-with-a-long-stable-id"));
	bool Found = false, Missing = true, Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		Found = State.IsCollapsed("deck:existing-card-with-a-long-stable-id", false);
		Missing = State.IsCollapsed("deck:missing-card-with-a-long-stable-id", false);
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_TRUE(Found);
	EXPECT_FALSE(Missing);
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
}

TEST(SettingsCardCollapseAllocation, SerializationNeedsOnlyTheProvidedBuffer)
{
	qm_card_collapse::CState State;
	constexpr char SERIALIZED[] = "!deck:first-card-with-a-long-stable-id;deck:second-card-with-a-long-stable-id";
	ASSERT_TRUE(State.Load(SERIALIZED));
	char aExact[sizeof(SERIALIZED)];
	char aShort[sizeof(SERIALIZED) - 1];
	bool Exact = false, Short = true, Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		Exact = State.Serialize(aExact, sizeof(aExact));
		Short = State.Serialize(aShort, sizeof(aShort));
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_TRUE(Exact);
	EXPECT_STREQ(aExact, SERIALIZED);
	EXPECT_FALSE(Short);
	EXPECT_EQ(aShort[0], '\0');
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
}
