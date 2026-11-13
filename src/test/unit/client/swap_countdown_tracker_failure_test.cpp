#include <base/str.h>

#include <game/client/components/tclient/swap_countdown_message.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

TEST(SwapCountdownTrackerFailure, InitialAllocationFailureKeepsTheTrackerEmptyAndRetryable)
{
	CSwapCountdownTracker Tracker;
	bool Started, Triggered;
	{
		CPredictionAllocationFailure Fault;
		Started = Tracker.Start("Alpha", false, 100);
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Started);
	EXPECT_TRUE(Tracker.Entries().empty());
	ASSERT_TRUE(Tracker.Start("Alpha", false, 100));
	ASSERT_EQ(Tracker.Entries().size(), 1u);
	EXPECT_EQ(Tracker.Entries()[0].m_InstanceId, 1);
}

TEST(SwapCountdownTrackerFailure, CapacityFailureKeepsTheExistingEntryAndItsBorrow)
{
	CSwapCountdownTracker Tracker;
	ASSERT_TRUE(Tracker.Start("Alpha", false, 100));
	const auto *pBefore = Tracker.Entries().data();
	const int Id = pBefore->m_InstanceId;
	bool Started, Triggered;
	{
		CPredictionAllocationFailure Fault;
		Started = Tracker.Start("Beta", false, 200);
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Started);
	ASSERT_EQ(Tracker.Entries().size(), 1u);
	EXPECT_EQ(Tracker.Entries().data(), pBefore);
	EXPECT_EQ(Tracker.Entries()[0].m_Counterpart, "Alpha");
	EXPECT_EQ(Tracker.Entries()[0].m_StartTick, 100);
	EXPECT_EQ(Tracker.Entries()[0].m_InstanceId, Id);
	ASSERT_TRUE(Tracker.Start("Beta", false, 200));
	EXPECT_EQ(Tracker.Entries()[0].m_InstanceId, Id + 1);
}

TEST(SwapCountdownTrackerFailure, NamePreparationFailureKeepsTheOutgoingAndIncomingRequests)
{
	CSwapCountdownTracker Tracker;
	ASSERT_TRUE(Tracker.Start("Alpha", false, 100));
	ASSERT_TRUE(Tracker.Start("Beta", true, 200));
	const int Id = Tracker.Entries()[0].m_InstanceId;
	const std::string Name(128, 'x');
	bool Started, Triggered;
	{
		CPredictionAllocationFailure Fault;
		Started = Tracker.Start(Name.c_str(), true, 300);
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Started);
	ASSERT_EQ(Tracker.Entries().size(), 2u);
	EXPECT_EQ(Tracker.Entries()[0].m_Counterpart, "Beta");
	EXPECT_TRUE(Tracker.Entries()[0].m_Outgoing);
	EXPECT_EQ(Tracker.Entries()[0].m_StartTick, 200);
	EXPECT_EQ(Tracker.Entries()[1].m_Counterpart, "Alpha");
	ASSERT_TRUE(Tracker.Start(Name.c_str(), true, 300));
	EXPECT_EQ(Tracker.Entries()[0].m_InstanceId, Id + 1);
}

TEST(SwapCountdownTrackerFailure, CapacityFailureAfterNamePreparationKeepsTheOriginalState)
{
	CSwapCountdownTracker Tracker;
	ASSERT_TRUE(Tracker.Start("Alpha", false, 100));
	const std::string Name(128, 'x');
	bool Started, Triggered;
	{
		CPredictionAllocationFailure Fault(1);
		Started = Tracker.Start(Name.c_str(), false, 200);
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Started);
	ASSERT_EQ(Tracker.Entries().size(), 1u);
	EXPECT_EQ(Tracker.Entries()[0].m_Counterpart, "Alpha");
	EXPECT_EQ(Tracker.Entries()[0].m_InstanceId, 1);
	ASSERT_TRUE(Tracker.Start(Name.c_str(), false, 200));
	EXPECT_EQ(Tracker.Entries()[0].m_InstanceId, 2);
}

TEST(SwapCountdownTrackerFailure, BorrowedCounterpartIsCopiedBeforeReplacingItsEntry)
{
	CSwapCountdownTracker Tracker;
	const std::string Name(128, 'x');
	ASSERT_TRUE(Tracker.Start(Name.c_str(), false, 100));
	const char *pBorrow = Tracker.Entries()[0].m_Counterpart.c_str();
	ASSERT_TRUE(Tracker.Start(pBorrow, false, 200));
	ASSERT_EQ(Tracker.Entries().size(), 1u);
	EXPECT_EQ(Tracker.Entries()[0].m_Counterpart, Name);
	EXPECT_EQ(Tracker.Entries()[0].m_StartTick, 200);
}

TEST(SwapCountdownTrackerFailure, EmptyInputDoesNotConsumeAnIdentityOrAllocate)
{
	CSwapCountdownTracker Tracker;
	bool NullStarted, EmptyStarted, Triggered;
	{
		CPredictionAllocationFailure Fault;
		NullStarted = Tracker.Start(nullptr, false, 100);
		EmptyStarted = Tracker.Start("", true, 100);
		Triggered = Fault.Triggered();
	}
	EXPECT_FALSE(NullStarted);
	EXPECT_FALSE(EmptyStarted);
	EXPECT_FALSE(Triggered);
	ASSERT_TRUE(Tracker.Start("Alpha", false, 100));
	EXPECT_EQ(Tracker.Entries()[0].m_InstanceId, 1);
}

TEST(SwapCountdownTrackerFailure, CaseInsensitiveReplacementRetainsTheOtherDirection)
{
	CSwapCountdownTracker Tracker;
	ASSERT_TRUE(Tracker.Start("Alpha", false, 100));
	ASSERT_TRUE(Tracker.Start("Alpha", true, 200));
	ASSERT_TRUE(Tracker.Start("ALPHA", false, 300));
	ASSERT_EQ(Tracker.Entries().size(), 2u);
	EXPECT_EQ(Tracker.Entries()[0].m_Counterpart, "ALPHA");
	EXPECT_FALSE(Tracker.Entries()[0].m_Outgoing);
	EXPECT_EQ(Tracker.Entries()[0].m_StartTick, 300);
	EXPECT_TRUE(Tracker.Entries()[1].m_Outgoing);
	EXPECT_EQ(Tracker.Entries()[1].m_StartTick, 200);
}

TEST(SwapCountdownTrackerFailure, ExistingCapacityLimitKeepsTheNewestRequests)
{
	CSwapCountdownTracker Tracker;
	for(int i = 0; i < MAX_CLIENTS + 2; ++i)
	{
		char aName[32];
		str_format(aName, sizeof(aName), "player-%d", i);
		ASSERT_TRUE(Tracker.Start(aName, false, 100 + i));
	}
	ASSERT_EQ(Tracker.Entries().size(), size_t(MAX_CLIENTS + 1));
	EXPECT_EQ(Tracker.Entries().front().m_StartTick, 100 + MAX_CLIENTS + 1);
	EXPECT_EQ(Tracker.Entries().back().m_StartTick, 101);
}

TEST(SwapCountdownTrackerFailure, ClearKeepsIdentityProgressForTheNextRequest)
{
	CSwapCountdownTracker Tracker;
	ASSERT_TRUE(Tracker.Start("Alpha", false, 100));
	const int Id = Tracker.Entries()[0].m_InstanceId;
	Tracker.Clear();
	ASSERT_TRUE(Tracker.Start("Beta", false, 200));
	EXPECT_EQ(Tracker.Entries()[0].m_InstanceId, Id + 1);
}

TEST(SwapCountdownTrackerFailure, FailedReplacementStillAllowsCancellingTheOldRequest)
{
	CSwapCountdownTracker Tracker;
	ASSERT_TRUE(Tracker.Start("Alpha", true, 100));
	const std::string Name(128, 'x');
	bool Started, Triggered;
	{
		CPredictionAllocationFailure Fault;
		Started = Tracker.Start(Name.c_str(), true, 200);
		Triggered = Fault.Triggered();
	}
	EXPECT_FALSE(Started);
	EXPECT_TRUE(Triggered);
	Tracker.Cancel("alpha", true);
	EXPECT_TRUE(Tracker.Entries().empty());
}

TEST(SwapCountdownDisplayId, OrdinaryInstancesKeepTheirOriginalDummyMapping)
{
	EXPECT_EQ(QmSwapCountdownDisplayId(1, 0), 2);
	EXPECT_EQ(QmSwapCountdownDisplayId(1, 1), 3);
	EXPECT_EQ(QmSwapCountdownDisplayId(2, 0), 4);
	EXPECT_EQ(QmSwapCountdownDisplayId(2, 1), 5);
}

TEST(SwapCountdownDisplayId, InvalidIdentityAndDummyAreRejected)
{
	EXPECT_FALSE(QmSwapCountdownDisplayId(0, 0));
	EXPECT_FALSE(QmSwapCountdownDisplayId(-1, 0));
	EXPECT_FALSE(QmSwapCountdownDisplayId(1, -1));
	EXPECT_FALSE(QmSwapCountdownDisplayId(1, NUM_DUMMIES));
}

TEST(SwapCountdownDisplayId, LastRepresentablePairIsAcceptedBeforeTheNextIsRejected)
{
	constexpr int Last = (std::numeric_limits<int>::max() - 1) / 2;
	EXPECT_EQ(QmSwapCountdownDisplayId(Last, 0), std::numeric_limits<int>::max() - 1);
	EXPECT_EQ(QmSwapCountdownDisplayId(Last, 1), std::numeric_limits<int>::max());
	EXPECT_FALSE(QmSwapCountdownDisplayId(Last + 1, 0));
	EXPECT_FALSE(QmSwapCountdownDisplayId(Last + 1, 1));
}

TEST(SwapCountdownDisplayId, ExtremeSignedInputsDoNotEnterTheMultiply)
{
	EXPECT_FALSE(QmSwapCountdownDisplayId(std::numeric_limits<int>::max(), 0));
	EXPECT_FALSE(QmSwapCountdownDisplayId(std::numeric_limits<int>::min(), 1));
}
