#include <game/client/components/qmclient/media_command_queue.h>
#include <game/client/components/system_media_controls_mpris_policy.h>

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>

namespace
{
	using ECommand = CQmMediaCommandQueue::ECommand;
	using TBatch = CQmMediaCommandQueue::TBatch;
	using CIdentity = SystemMediaControls::CMprisSessionIdentity;
}

TEST(QmMediaCommandHandoffIntegration, ProducerAndConsumerTransferOwnedCommandsInOrder)
{
	constexpr size_t Count = 64;
	CQmMediaCommandQueue Queue;
	std::mutex Mutex;
	std::condition_variable Changed;
	bool Done = false, AcceptedAll = true;
	std::thread Producer([&] {
		for(size_t Index = 0; Index < Count; ++Index)
		{
			std::string Source = "player-" + std::to_string(Index);
			{
				const std::lock_guard<std::mutex> Lock(Mutex);
				AcceptedAll &= Queue.TryEnqueue(ECommand::Next, Source.c_str(), Index);
			}
			Source.assign("overwritten");
			Changed.notify_one();
		}
		{
			const std::lock_guard<std::mutex> Lock(Mutex);
			Done = true;
		}
		Changed.notify_one();
	});
	TBatch Received, Batch;
	const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	bool TimedOut = false;
	for(;;)
	{
		bool Finished;
		{
			std::unique_lock<std::mutex> Lock(Mutex);
			if(!Changed.wait_until(Lock, Deadline, [&] { return Done || !Queue.Empty(); }))
			{
				TimedOut = true;
				break;
			}
			Queue.Take(Batch);
			Finished = Done && Queue.Empty();
		}
		for(auto &Command : Batch)
			Received.push_back(std::move(Command));
		if(Finished)
			break;
	}
	Producer.join();
	EXPECT_FALSE(TimedOut);
	EXPECT_TRUE(AcceptedAll);
	ASSERT_EQ(Received.size(), Count);
	for(size_t Index = 0; Index < Count; ++Index)
	{
		EXPECT_EQ(Received[Index].m_SourceAppId, "player-" + std::to_string(Index));
		EXPECT_EQ(Received[Index].m_Generation, Index);
	}
}

TEST(QmMediaCommandHandoffIntegration, ReplacedMprisOwnerRejectsOldBatchAndAcceptsCurrentGeneration)
{
	CIdentity Identity;
	CQmMediaCommandQueue Queue;
	ASSERT_TRUE(Identity.Update("org.mpris.player", ":1.1"));
	const uint64_t Previous = Identity.Generation();
	ASSERT_TRUE(Queue.TryEnqueue(ECommand::Prev, "org.mpris.player", Previous));
	ASSERT_TRUE(Identity.Update("org.mpris.player", ":1.2"));
	ASSERT_TRUE(Queue.TryEnqueue(ECommand::Next, "org.mpris.player", Identity.Generation()));
	TBatch Commands;
	Queue.Take(Commands);
	ASSERT_EQ(Commands.size(), 2u);
	EXPECT_FALSE(Identity.Accepts(Commands[0].m_Generation));
	EXPECT_TRUE(Identity.Accepts(Commands[1].m_Generation));
	EXPECT_EQ(Commands[0].m_Type, ECommand::Prev);
	EXPECT_EQ(Commands[1].m_Type, ECommand::Next);
}

TEST(QmMediaCommandHandoffIntegration, ClearingSessionRejectsTakenWorkAfterSameOwnerReturns)
{
	CIdentity Identity;
	CQmMediaCommandQueue Queue;
	ASSERT_TRUE(Identity.Update("player", ":1.1"));
	ASSERT_TRUE(Queue.TryEnqueue(ECommand::PlayPause, "player", Identity.Generation()));
	TBatch Previous;
	Queue.Take(Previous);
	ASSERT_EQ(Previous.size(), 1u);
	Identity.Clear();
	Queue.Clear();
	EXPECT_FALSE(Identity.Accepts(Previous[0].m_Generation));
	ASSERT_TRUE(Identity.Update("player", ":1.1"));
	EXPECT_FALSE(Identity.Accepts(Previous[0].m_Generation));
	ASSERT_TRUE(Queue.TryEnqueue(ECommand::Next, "player", Identity.Generation()));
	TBatch Current;
	Queue.Take(Current);
	ASSERT_EQ(Current.size(), 1u);
	EXPECT_TRUE(Identity.Accepts(Current[0].m_Generation));
}

TEST(QmMediaCommandHandoffIntegration, PreparationFailureDoesNotInsertWorkIntoCurrentSessionBatch)
{
	class CFailedCopy : public CQmMediaCommandQueue::CPreparationOperations
	{
	public:
		bool m_Fail = false;
		void CopySource(std::string &Source, const char *pSource) override
		{
			CPreparationOperations::CopySource(Source, pSource);
			if(m_Fail)
				throw std::bad_alloc();
		}
	} Operations;
	CIdentity Identity;
	ASSERT_TRUE(Identity.Update("player", ":1.1"));
	CQmMediaCommandQueue Queue(&Operations);
	ASSERT_TRUE(Queue.TryEnqueue(ECommand::Prev, "player", Identity.Generation()));
	Operations.m_Fail = true;
	EXPECT_FALSE(Queue.TryEnqueue(ECommand::PlayPause, "player", Identity.Generation()));
	Operations.m_Fail = false;
	ASSERT_TRUE(Queue.TryEnqueue(ECommand::Next, "player", Identity.Generation()));
	TBatch Commands;
	Queue.Take(Commands);
	ASSERT_EQ(Commands.size(), 2u);
	EXPECT_EQ(Commands[0].m_Type, ECommand::Prev);
	EXPECT_EQ(Commands[1].m_Type, ECommand::Next);
	EXPECT_TRUE(Identity.Accepts(Commands[0].m_Generation));
	EXPECT_TRUE(Identity.Accepts(Commands[1].m_Generation));
}
