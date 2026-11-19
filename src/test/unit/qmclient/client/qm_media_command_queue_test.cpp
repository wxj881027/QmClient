#include <game/client/components/qmclient/media_command_queue.h>

#include <gtest/gtest.h>

#include <initializer_list>
#include <new>
#include <stdexcept>
#include <string>

namespace
{
	using ECommand = CQmMediaCommandQueue::ECommand;
	using TBatch = CQmMediaCommandQueue::TBatch;

	class CPreparation : public CQmMediaCommandQueue::CPreparationOperations
	{
	public:
		bool m_FailCopy = false;
		bool m_FailReserve = false;
		bool m_After = false;
		int m_Exception = 0;
		int m_CopyCalls = 0;
		int m_ReserveCalls = 0;

		void Fail(bool Enabled, bool After)
		{
			if(!Enabled || After != m_After)
				return;
			if(m_Exception == 1)
				throw std::length_error("media command preparation");
			if(m_Exception == 2)
				throw 7;
			throw std::bad_alloc();
		}
		void CopySource(std::string &Source, const char *pSource) override
		{
			++m_CopyCalls;
			Fail(m_FailCopy, false);
			CPreparationOperations::CopySource(Source, pSource);
			Fail(m_FailCopy, true);
		}
		void Reserve(TBatch &Commands, size_t Count) override
		{
			++m_ReserveCalls;
			Fail(m_FailReserve, false);
			CPreparationOperations::Reserve(Commands, Count);
			Fail(m_FailReserve, true);
		}
	};
}

TEST(QmMediaCommandQueue, DefaultQueueAndRepeatedEmptyTakesStayEmpty)
{
	CQmMediaCommandQueue Queue;
	TBatch Commands;
	EXPECT_TRUE(Queue.Empty());
	EXPECT_EQ(Queue.Count(), 0u);
	Queue.Take(Commands);
	Queue.Take(Commands);
	EXPECT_TRUE(Commands.empty());
	EXPECT_TRUE(Queue.Empty());
}

TEST(QmMediaCommandQueue, AcceptedCommandOwnsSourceAndPreservesMetadata)
{
	CQmMediaCommandQueue Queue;
	std::string Source(120, 'p');
	const std::string Expected = Source;
	ASSERT_TRUE(Queue.TryEnqueue(ECommand::Next, Source.c_str(), 41));
	Source.assign("replacement");
	TBatch Commands;
	Queue.Take(Commands);
	ASSERT_EQ(Commands.size(), 1u);
	EXPECT_EQ(Commands[0].m_Type, ECommand::Next);
	EXPECT_EQ(Commands[0].m_SourceAppId, Expected);
	EXPECT_EQ(Commands[0].m_Generation, 41u);
	EXPECT_TRUE(Queue.Empty());
}

TEST(QmMediaCommandQueue, AllActionsKeepFifoOrderAndDoNotRewriteGeneration)
{
	CQmMediaCommandQueue Queue;
	ASSERT_TRUE(Queue.TryEnqueue(ECommand::Prev, "first", 0));
	ASSERT_TRUE(Queue.TryEnqueue(ECommand::PlayPause, "second", 17));
	ASSERT_TRUE(Queue.TryEnqueue(ECommand::Next, "third", 18));
	TBatch Commands;
	Queue.Take(Commands);
	ASSERT_EQ(Commands.size(), 3u);
	EXPECT_EQ(Commands[0].m_Type, ECommand::Prev);
	EXPECT_EQ(Commands[0].m_Generation, 0u);
	EXPECT_EQ(Commands[1].m_Type, ECommand::PlayPause);
	EXPECT_EQ(Commands[1].m_SourceAppId, "second");
	EXPECT_EQ(Commands[2].m_Type, ECommand::Next);
	EXPECT_EQ(Commands[2].m_Generation, 18u);
}

TEST(QmMediaCommandQueue, InitialSourceCopyFailureCanBeRetriedWithoutPartialCommand)
{
	for(bool After : {false, true})
	{
		SCOPED_TRACE(After);
		CPreparation Operations;
		CQmMediaCommandQueue Queue(&Operations);
		Operations.m_FailCopy = true;
		Operations.m_After = After;
		EXPECT_FALSE(Queue.TryEnqueue(ECommand::Next, "player", 1));
		EXPECT_TRUE(Queue.Empty());
		EXPECT_EQ(Operations.m_ReserveCalls, 0);
		Operations.m_FailCopy = false;
		ASSERT_TRUE(Queue.TryEnqueue(ECommand::Prev, "retry", 2));
		TBatch Commands;
		Queue.Take(Commands);
		ASSERT_EQ(Commands.size(), 1u);
		EXPECT_EQ(Commands[0].m_SourceAppId, "retry");
	}
}

TEST(QmMediaCommandQueue, InitialCapacityFailureCanBeRetriedWithoutPartialCommand)
{
	for(bool After : {false, true})
	{
		SCOPED_TRACE(After);
		CPreparation Operations;
		CQmMediaCommandQueue Queue(&Operations);
		Operations.m_FailReserve = true;
		Operations.m_After = After;
		EXPECT_FALSE(Queue.TryEnqueue(ECommand::Next, "player", 1));
		EXPECT_TRUE(Queue.Empty());
		Operations.m_FailReserve = false;
		ASSERT_TRUE(Queue.TryEnqueue(ECommand::PlayPause, "retry", 2));
		TBatch Commands;
		Queue.Take(Commands);
		ASSERT_EQ(Commands.size(), 1u);
		EXPECT_EQ(Commands[0].m_Type, ECommand::PlayPause);
	}
}

TEST(QmMediaCommandQueue, LaterCopyFailureKeepsEarlierCommandsAndNeverReplaysFailedRequest)
{
	CPreparation Operations;
	CQmMediaCommandQueue Queue(&Operations);
	ASSERT_TRUE(Queue.TryEnqueue(ECommand::Prev, "before", 1));
	Operations.m_FailCopy = true;
	Operations.m_After = true;
	EXPECT_FALSE(Queue.TryEnqueue(ECommand::Next, "failed", 2));
	EXPECT_EQ(Queue.Count(), 1u);
	Operations.m_FailCopy = false;
	ASSERT_TRUE(Queue.TryEnqueue(ECommand::PlayPause, "after", 3));
	TBatch Commands;
	Queue.Take(Commands);
	ASSERT_EQ(Commands.size(), 2u);
	EXPECT_EQ(Commands[0].m_SourceAppId, "before");
	EXPECT_EQ(Commands[1].m_SourceAppId, "after");
	Queue.Take(Commands);
	EXPECT_TRUE(Commands.empty());
}

TEST(QmMediaCommandQueue, LaterCapacityFailureKeepsEarlierValuesEvenAfterReservation)
{
	for(bool After : {false, true})
	{
		SCOPED_TRACE(After);
		CPreparation Operations;
		CQmMediaCommandQueue Queue(&Operations);
		ASSERT_TRUE(Queue.TryEnqueue(ECommand::Prev, "before", 7));
		Operations.m_FailReserve = true;
		Operations.m_After = After;
		EXPECT_FALSE(Queue.TryEnqueue(ECommand::Next, "failed", 8));
		EXPECT_EQ(Queue.Count(), 1u);
		Operations.m_FailReserve = false;
		ASSERT_TRUE(Queue.TryEnqueue(ECommand::PlayPause, "after", 9));
		TBatch Commands;
		Queue.Take(Commands);
		ASSERT_EQ(Commands.size(), 2u);
		EXPECT_EQ(Commands[0].m_Generation, 7u);
		EXPECT_EQ(Commands[0].m_SourceAppId, "before");
		EXPECT_EQ(Commands[1].m_Generation, 9u);
	}
}

TEST(QmMediaCommandQueue, NonAllocationCppExceptionsAreAlsoContained)
{
	for(int Exception : {1, 2})
	{
		SCOPED_TRACE(Exception);
		CPreparation Operations;
		CQmMediaCommandQueue Queue(&Operations);
		ASSERT_TRUE(Queue.TryEnqueue(ECommand::Prev, "before", 1));
		Operations.m_Exception = Exception;
		Operations.m_FailCopy = true;
		EXPECT_FALSE(Queue.TryEnqueue(ECommand::Next, "failed", 2));
		TBatch Commands;
		Queue.Take(Commands);
		ASSERT_EQ(Commands.size(), 1u);
		EXPECT_EQ(Commands[0].m_SourceAppId, "before");
	}
}

TEST(QmMediaCommandQueue, TakingAcceptedCommandsDoesNotPrepareOrCopyTheirSourcesAgain)
{
	CPreparation Operations;
	CQmMediaCommandQueue Queue(&Operations);
	ASSERT_TRUE(Queue.TryEnqueue(ECommand::Prev, "player", 1));
	const int CopyCalls = Operations.m_CopyCalls;
	const int ReserveCalls = Operations.m_ReserveCalls;
	Operations.m_FailCopy = Operations.m_FailReserve = true;
	TBatch Commands;
	Queue.Take(Commands);
	ASSERT_EQ(Commands.size(), 1u);
	EXPECT_EQ(Operations.m_CopyCalls, CopyCalls);
	EXPECT_EQ(Operations.m_ReserveCalls, ReserveCalls);
	EXPECT_TRUE(Queue.Empty());
}

TEST(QmMediaCommandQueue, ReusingTakenBatchNeverReturnsExecutedCommandsToPendingQueue)
{
	CQmMediaCommandQueue Queue;
	TBatch Commands;
	ASSERT_TRUE(Queue.TryEnqueue(ECommand::Prev, "first", 1));
	Queue.Take(Commands);
	ASSERT_TRUE(Queue.TryEnqueue(ECommand::Next, "second", 2));
	Queue.Take(Commands);
	ASSERT_EQ(Commands.size(), 1u);
	EXPECT_EQ(Commands[0].m_SourceAppId, "second");
	EXPECT_TRUE(Queue.Empty());
	Queue.Take(Commands);
	EXPECT_TRUE(Commands.empty());
	EXPECT_TRUE(Queue.Empty());
}

TEST(QmMediaCommandQueue, TakenBatchSurvivesQueueAndBorrowedSourceDestruction)
{
	TBatch Commands;
	{
		CQmMediaCommandQueue Queue;
		std::string Source = "owned.player.application.with.a.long.identifier";
		ASSERT_TRUE(Queue.TryEnqueue(ECommand::PlayPause, Source.c_str(), 17));
		Queue.Take(Commands);
	}
	ASSERT_EQ(Commands.size(), 1u);
	EXPECT_EQ(Commands[0].m_SourceAppId, "owned.player.application.with.a.long.identifier");
	EXPECT_EQ(Commands[0].m_Generation, 17u);
}

TEST(QmMediaCommandQueue, ClearingPendingCommandsIsIdempotentAndAllowsNewSubmission)
{
	CQmMediaCommandQueue Queue;
	ASSERT_TRUE(Queue.TryEnqueue(ECommand::Prev, "old", 1));
	Queue.Clear();
	Queue.Clear();
	EXPECT_TRUE(Queue.Empty());
	ASSERT_TRUE(Queue.TryEnqueue(ECommand::Next, "new", 2));
	TBatch Commands;
	Queue.Take(Commands);
	ASSERT_EQ(Commands.size(), 1u);
	EXPECT_EQ(Commands[0].m_SourceAppId, "new");
}

TEST(QmMediaCommandQueue, LaterEnqueueDoesNotMutateAnAlreadyTakenBatch)
{
	CQmMediaCommandQueue Queue;
	ASSERT_TRUE(Queue.TryEnqueue(ECommand::Prev, "first", 1));
	TBatch First, Second;
	Queue.Take(First);
	ASSERT_TRUE(Queue.TryEnqueue(ECommand::Next, "second", 2));
	Queue.Take(Second);
	ASSERT_EQ(First.size(), 1u);
	ASSERT_EQ(Second.size(), 1u);
	EXPECT_EQ(First[0].m_SourceAppId, "first");
	EXPECT_EQ(Second[0].m_SourceAppId, "second");
}
