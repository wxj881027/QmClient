#include <game/client/components/qmclient/auto_reply.h>

#include <test/support/prediction_allocation_failure.h>

#include <gtest/gtest.h>

TEST(QmAutoReplyAllocation, FailedRepeatCachePreparationLeavesReplyEligibleForRetry)
{
	qm_auto_reply::CSendState State;
	const std::string Identity = "Alice";
	const std::string Text(96, 'a');
	int SendCount = 0;
	const auto Send = [&] {
		++SendCount;
		return true;
	};
	bool Sent, AllocationFailed;
	{
		CPredictionAllocationFailure Failure;
		Sent = State.TrySend(0, 1, Identity, qm_auto_reply::EKind::AWAY, Text, 100, 10, Send);
		AllocationFailed = Failure.Triggered();
	}
	EXPECT_TRUE(AllocationFailed);
	EXPECT_FALSE(Sent);
	EXPECT_EQ(SendCount, 0);

	EXPECT_TRUE(State.TrySend(0, 1, Identity, qm_auto_reply::EKind::AWAY, Text, 100, 10, Send));
	EXPECT_FALSE(State.TrySend(0, 1, Identity, qm_auto_reply::EKind::AWAY, Text, 110, 10, Send));
	EXPECT_EQ(SendCount, 1);
}
