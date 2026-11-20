#include <game/client/components/system_media_controls_mpris_policy.h>

#include <gtest/gtest.h>

using namespace SystemMediaControls;

TEST(QmMprisRequestContext, MissingBudgetDoesNotAllowRequests)
{
	CMprisRequestContext Context;
	EXPECT_FALSE(Context.Allowed(0));
	EXPECT_EQ(Context.Timeout(0), 0);
	std::atomic_bool Stop{false};
	const CMprisRequestContextScope Scope(Context, Stop);
	EXPECT_FALSE(Context.Allowed(0));
	EXPECT_EQ(Context.Remaining(0), 0);
}

TEST(QmMprisRequestContext, ReservedTimeIsRemovedUntilCandidateSelectionCompletes)
{
	CMprisRequestContext Context;
	std::atomic_bool Stop{false};
	const CMprisPollBudget Budget(0, 400);
	const CMprisRequestContextScope Scope(Context, Stop, &Budget, 200);
	EXPECT_TRUE(Context.Allowed(0));
	EXPECT_EQ(Context.Timeout(0), 200);
	EXPECT_FALSE(Context.Allowed(200));
	Context.SetReserved(0);
	EXPECT_TRUE(Context.Allowed(200));
	EXPECT_EQ(Context.Timeout(200), 200);
}

TEST(QmMprisRequestContext, ExpiredBudgetAndStopBothPreventFurtherRequests)
{
	CMprisRequestContext Context;
	std::atomic_bool Stop{false};
	const CMprisPollBudget Budget(0, 1000);
	const CMprisRequestContextScope Scope(Context, Stop, &Budget);
	EXPECT_FALSE(Context.Allowed(1000));
	EXPECT_EQ(Context.Timeout(1000), 0);
	Stop = true;
	EXPECT_FALSE(Context.Allowed(1));
	EXPECT_EQ(Context.Timeout(1), 0);
}

TEST(QmMprisRequestContext, LeavingPollScopeDropsTheBorrowedBudget)
{
	CMprisRequestContext Context;
	std::atomic_bool Stop{false};
	const CMprisRequestContextScope ThreadScope(Context, Stop);
	{
		const CMprisPollBudget Budget(0, 1000);
		const CMprisRequestContextScope PollScope(Context, Stop, &Budget, 200);
		EXPECT_TRUE(Context.Allowed(1));
	}
	EXPECT_FALSE(Context.Allowed(1));
	EXPECT_EQ(Context.Timeout(1), 0);
}

TEST(QmMprisRequestContext, ExceptionRestoresTheOuterBudgetAndReservedTime)
{
	CMprisRequestContext Context;
	std::atomic_bool Stop{false};
	const CMprisPollBudget Outer(0, 900);
	const CMprisRequestContextScope OuterScope(Context, Stop, &Outer, 200);
	try
	{
		const CMprisPollBudget Inner(0, 100);
		const CMprisRequestContextScope InnerScope(Context, Stop, &Inner);
		EXPECT_EQ(Context.Timeout(0), 100);
		throw 7;
	}
	catch(int)
	{
	}
	EXPECT_EQ(Context.Timeout(600), 100);
	EXPECT_FALSE(Context.Allowed(700));
}

TEST(QmMprisRequestContext, LeavingThreadScopeDoesNotRetainItsStopFlag)
{
	CMprisRequestContext Context;
	{
		std::atomic_bool Stop{false};
		const CMprisPollBudget Budget(0, 1000);
		const CMprisRequestContextScope Scope(Context, Stop, &Budget);
		EXPECT_TRUE(Context.Allowed(0));
	}
	EXPECT_TRUE(Context.StopRequested());
	EXPECT_FALSE(Context.Allowed(0));
}
