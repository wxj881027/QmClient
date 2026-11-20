#include <game/client/prediction/gameworld.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

TEST(PredictionCharacterOrder, ReordersAndResetsWithAllocationFailureArmed)
{
	CCharOrder Order;
	ASSERT_NO_THROW({
		CPredictionAllocationFailure Failure;
		Order.GiveStrong(MAX_CLIENTS - 1);
		Order.GiveWeak(0);
	});
	EXPECT_EQ(Order.m_Ids.front(), MAX_CLIENTS - 1);
	EXPECT_EQ(Order.m_Ids.back(), 0);
	EXPECT_TRUE(Order.HasStrongAgainst(1, 2));

	ASSERT_NO_THROW({
		CPredictionAllocationFailure Failure;
		Order.Reset();
	});
	EXPECT_EQ(Order.m_Ids.front(), 0);
	EXPECT_EQ(Order.m_Ids.back(), MAX_CLIENTS - 1);
}
