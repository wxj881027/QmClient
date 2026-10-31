#include <game/client/components/qmclient/pending_hammer_hits.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

namespace
{
	CQmPendingHammerHits::SEvent Event(int Ordinal, int Connection = 0)
	{
		return {vec2(12.0f, 34.0f), 100, Connection, Ordinal, true};
	}
}

TEST(QmPendingHammerHits, NewQueueIsEmpty)
{
	CQmPendingHammerHits Queue;
	EXPECT_TRUE(Queue.Events().empty());
}

TEST(QmPendingHammerHits, AppendPreservesSnapshotFieldsAndOrder)
{
	CQmPendingHammerHits Queue;
	const CQmPendingHammerHits::SEvent First{vec2(-12.0f, 34.0f), 200, 1, 17, false};
	ASSERT_TRUE(Queue.TryAppend(First));
	ASSERT_TRUE(Queue.TryAppend(Event(20)));
	const auto Events = Queue.Events();
	ASSERT_EQ(Events.size(), 2u);
	EXPECT_EQ(Events[0].m_Pos, vec2(-12.0f, 34.0f));
	EXPECT_EQ(Events[0].m_SnapshotTick, 200);
	EXPECT_EQ(Events[0].m_Connection, 1);
	EXPECT_EQ(Events[0].m_EventOrdinal, 17);
	EXPECT_FALSE(Events[0].m_RenderEffect);
	EXPECT_EQ(Events[1].m_EventOrdinal, 20);
	EXPECT_TRUE(Events[1].m_RenderEffect);
}

TEST(QmPendingHammerHits, EverySnapshotItemCanBeAHammerHit)
{
	CQmPendingHammerHits Queue;
	for(int Ordinal = 0; Ordinal < CSnapshot::MAX_ITEMS; ++Ordinal)
		ASSERT_TRUE(Queue.TryAppend(Event(Ordinal)));
	const auto Events = Queue.Events();
	ASSERT_EQ(Events.size(), static_cast<std::size_t>(CSnapshot::MAX_ITEMS));
	for(int Ordinal = 0; Ordinal < CSnapshot::MAX_ITEMS; ++Ordinal)
		EXPECT_EQ(Events[Ordinal].m_EventOrdinal, Ordinal);
}

TEST(QmPendingHammerHits, AppendPastSnapshotLimitKeepsAllExistingRecords)
{
	CQmPendingHammerHits Queue;
	for(int Ordinal = 0; Ordinal < CSnapshot::MAX_ITEMS; ++Ordinal)
		ASSERT_TRUE(Queue.TryAppend(Event(Ordinal)));
	EXPECT_FALSE(Queue.TryAppend(Event(-1, 1)));
	const auto Events = Queue.Events();
	ASSERT_EQ(Events.size(), static_cast<std::size_t>(CSnapshot::MAX_ITEMS));
	for(int Ordinal = 0; Ordinal < CSnapshot::MAX_ITEMS; ++Ordinal)
	{
		EXPECT_EQ(Events[Ordinal].m_EventOrdinal, Ordinal);
		EXPECT_EQ(Events[Ordinal].m_Connection, 0);
	}
}

TEST(QmPendingHammerHits, ResetAfterFullBatchAllowsAnEntireNewSnapshot)
{
	CQmPendingHammerHits Queue;
	for(int Ordinal = 0; Ordinal < CSnapshot::MAX_ITEMS; ++Ordinal)
		ASSERT_TRUE(Queue.TryAppend(Event(Ordinal)));
	Queue.Reset();
	EXPECT_TRUE(Queue.Events().empty());
	for(int Ordinal = 0; Ordinal < CSnapshot::MAX_ITEMS; ++Ordinal)
		ASSERT_TRUE(Queue.TryAppend(Event(Ordinal, 1)));
	for(const auto &Stored : Queue.Events())
		EXPECT_EQ(Stored.m_Connection, 1);
}

TEST(QmPendingHammerHits, RepeatedResetDoesNotExposeOldEvents)
{
	CQmPendingHammerHits Queue;
	ASSERT_TRUE(Queue.TryAppend(Event(1)));
	Queue.Reset();
	Queue.Reset();
	ASSERT_TRUE(Queue.TryAppend(Event(3, 1)));
	ASSERT_EQ(Queue.Events().size(), 1u);
	EXPECT_EQ(Queue.Events()[0].m_EventOrdinal, 3);
	EXPECT_EQ(Queue.Events()[0].m_Connection, 1);
}

TEST(QmPendingHammerHits, IndependentOwnersDoNotSharePendingRecords)
{
	CQmPendingHammerHits First, Second;
	ASSERT_TRUE(First.TryAppend(Event(1)));
	ASSERT_TRUE(Second.TryAppend(Event(2, 1)));
	First.Reset();
	EXPECT_TRUE(First.Events().empty());
	ASSERT_EQ(Second.Events().size(), 1u);
	EXPECT_EQ(Second.Events()[0].m_EventOrdinal, 2);
}

TEST(QmPendingHammerHits, ConstructionFullAppendOverflowAndResetDoNotAllocate)
{
	bool Allocated, Threw = false, Accepted = true, OverflowAccepted = true, Empty = false;
	std::size_t Count = 0;
	{
		CPredictionAllocationFailure Failure;
		try
		{
			CQmPendingHammerHits Queue;
			for(int Ordinal = 0; Ordinal < CSnapshot::MAX_ITEMS; ++Ordinal)
				Accepted = Queue.TryAppend(Event(Ordinal)) && Accepted;
			Count = Queue.Events().size();
			OverflowAccepted = Queue.TryAppend(Event(-1));
			Queue.Reset();
			Empty = Queue.Events().empty();
		}
		catch(...)
		{
			Threw = true;
		}
		Allocated = Failure.Triggered();
	}
	EXPECT_FALSE(Threw);
	EXPECT_FALSE(Allocated);
	EXPECT_TRUE(Accepted);
	EXPECT_EQ(Count, static_cast<std::size_t>(CSnapshot::MAX_ITEMS));
	EXPECT_FALSE(OverflowAccepted);
	EXPECT_TRUE(Empty);
}
