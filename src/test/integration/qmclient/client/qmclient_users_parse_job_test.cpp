#include <game/client/components/qmclient/qm_realtime.h>
#include <game/client/components/qmclient/qmclient_users_parse_job.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>

#include <cstring>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace
{
	constexpr const char *USERS = R"({"type":"users","data":{"users":[{"server_address":"one:8303","player_name":"Alice","qid":"one"},{"server_address":"one:8303","player_name":"Dummy","dummy":true},{"server_address":"two:8303","player_name":"Other"}]}})";

	std::shared_ptr<const json_value> Payload()
	{
		SQmRealtimeMessage Message;
		EXPECT_TRUE(ParseQmRealtimeMessage(USERS, std::strlen(USERS), Message));
		return Message.m_pPayload;
	}

	class CUsersParseEngine : public IEngine
	{
	public:
		bool m_Accept = true;
		bool m_Throw = false;
		int m_Attempts = 0;
		CJobPool *m_pPool = nullptr;
		IJob::EJobState m_LastNativeState = IJob::STATE_DONE;
		std::weak_ptr<IJob> m_LastAttempt;
		std::vector<std::shared_ptr<IJob>> m_vAccepted;

		void Init() override {}
		void AddJob(std::shared_ptr<IJob> pJob) override { (void)TryAddJob(std::move(pJob)); }
		bool TryAddJob(std::shared_ptr<IJob> pJob) override
		{
			++m_Attempts;
			m_LastAttempt = pJob;
			if(m_Throw)
				throw std::bad_alloc();
			if(!m_Accept)
				return false;
			if(m_pPool)
			{
				const bool Accepted = m_pPool->TryAdd(pJob);
				m_LastNativeState = pJob->State();
				return Accepted;
			}
			m_vAccepted.push_back(std::move(pJob));
			return true;
		}
		void ShutdownJobs() override {}
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
	};

	bool Queue(std::shared_ptr<IJob> &pPending, std::shared_ptr<const json_value> &pPayload, IEngine &Engine)
	{
		return TryQueueQmClientUsersParseJob(pPending, pPayload, Engine, [](const auto &pInput) {
			return std::make_shared<CQmClientUsersParseJob>(pInput, "one:8303", 900, 12);
		});
	}

	std::shared_ptr<CQmClientUsersParseJob> CompletedJob(std::weak_ptr<const json_value> *pInputOwner = nullptr)
	{
		auto pInput = Payload();
		if(pInputOwner)
			*pInputOwner = pInput;
		auto pJob = std::make_shared<CQmClientUsersParseJob>(std::move(pInput), "one:8303", 900, 12);
		CJobPool Pool;
		if(!Pool.TryInit(1))
		{
			ADD_FAILURE() << "could not initialize native job pool";
			return nullptr;
		}
		if(!Pool.TryAdd(pJob))
		{
			ADD_FAILURE() << "native pool rejected parse job";
			return nullptr;
		}
		const auto Done = WaitForJobDone(*pJob);
		EXPECT_TRUE(Done);
		Pool.Shutdown();
		return Done ? pJob : nullptr;
	}
}

TEST(QmClientUsersParseJob, FactoryBadAllocRetainsSnapshotWithoutDispatch)
{
	CUsersParseEngine Engine;
	std::shared_ptr<IJob> pPending;
	auto pInput = Payload();
	ASSERT_NE(pInput, nullptr);
	const auto pOriginal = pInput;
	EXPECT_FALSE(TryQueueQmClientUsersParseJob(pPending, pInput, Engine, [](const auto &) -> std::shared_ptr<IJob> {
		throw std::bad_alloc();
	}));
	EXPECT_EQ(pPending, nullptr);
	EXPECT_EQ(pInput, pOriginal);
	EXPECT_EQ(Engine.m_Attempts, 0);
	ASSERT_TRUE(Queue(pPending, pInput, Engine));
	EXPECT_NE(pPending, nullptr);
	EXPECT_EQ(pInput, nullptr);
}

TEST(QmClientUsersParseJob, SubmitBadAllocRetainsSnapshotWithoutPublishingWaitingJob)
{
	CUsersParseEngine Engine;
	Engine.m_Throw = true;
	std::shared_ptr<IJob> pPending;
	auto pInput = Payload();
	const auto pOriginal = pInput;
	EXPECT_FALSE(Queue(pPending, pInput, Engine));
	EXPECT_EQ(pPending, nullptr);
	EXPECT_EQ(pInput, pOriginal);
	EXPECT_TRUE(Engine.m_LastAttempt.expired());
	Engine.m_Throw = false;
	ASSERT_TRUE(Queue(pPending, pInput, Engine));
	EXPECT_EQ(Engine.m_Attempts, 2);
}

TEST(QmClientUsersParseJob, RejectedEngineAllowsRetryWithSameSnapshot)
{
	CUsersParseEngine Engine;
	Engine.m_Accept = false;
	std::shared_ptr<IJob> pPending;
	auto pInput = Payload();
	const auto pOriginal = pInput;
	EXPECT_FALSE(Queue(pPending, pInput, Engine));
	EXPECT_EQ(pPending, nullptr);
	EXPECT_EQ(pInput, pOriginal);
	EXPECT_TRUE(Engine.m_LastAttempt.expired());
	Engine.m_Accept = true;
	ASSERT_TRUE(Queue(pPending, pInput, Engine));
	ASSERT_EQ(Engine.m_vAccepted.size(), 1u);
	EXPECT_EQ(pPending, Engine.m_vAccepted[0]);
	EXPECT_EQ(pInput, nullptr);
}

TEST(QmClientUsersParseJob, NativePoolParsesOwnedSnapshotAfterOriginalMessageExpires)
{
	CJobPool Pool;
	ASSERT_TRUE(Pool.TryInit(1));
	CUsersParseEngine Engine;
	Engine.m_pPool = &Pool;
	std::shared_ptr<IJob> pPending;
	auto pInput = Payload();
	ASSERT_NE(pInput, nullptr);
	std::weak_ptr<const json_value> InputOwner = pInput;
	ASSERT_TRUE(Queue(pPending, pInput, Engine));
	EXPECT_EQ(pInput, nullptr);
	ASSERT_NE(pPending, nullptr);
	ASSERT_TRUE(WaitForJobDone(*pPending));
	Pool.Shutdown();
	EXPECT_TRUE(InputOwner.expired());
	auto pJob = std::static_pointer_cast<CQmClientUsersParseJob>(pPending);
	EXPECT_STREQ(pJob->ServerAddress(), "one:8303");
	EXPECT_EQ(pJob->ExpireTick(), 900);
	EXPECT_EQ(pJob->ConnectionTick(), 12);
	const auto Result = pJob->TakeResult();
	ASSERT_TRUE(Result.m_Parsed);
	EXPECT_EQ(Result.m_OnlineUserCount, 2);
	EXPECT_EQ(Result.m_OnlineDummyCount, 1);
	ASSERT_EQ(Result.m_vServerDistribution.size(), 2u);
	ASSERT_EQ(Result.m_vLocalServerMarks.size(), 2u);
	EXPECT_EQ(Result.m_vLocalServerMarks[0].m_Name, "Alice");
	EXPECT_EQ(Result.m_vLocalServerMarks[0].m_Qid, "one");
	EXPECT_FALSE(pJob->TakeResult().m_Parsed);
}

TEST(QmClientUsersParseJob, ExistingJobKeepsIdentityAndDoesNotConsumeNewerSnapshot)
{
	CUsersParseEngine Engine;
	std::shared_ptr<IJob> pPending;
	auto pInput = Payload();
	ASSERT_TRUE(Queue(pPending, pInput, Engine));
	const auto pAccepted = pPending;
	pInput = Payload();
	const auto pNewer = pInput;
	EXPECT_FALSE(Queue(pPending, pInput, Engine));
	EXPECT_EQ(pPending, pAccepted);
	EXPECT_EQ(pInput, pNewer);
	EXPECT_EQ(Engine.m_Attempts, 1);
}

TEST(QmClientUsersParseJob, ClosedPoolRejectsQueuedCandidateAndReopenedPoolCanRetry)
{
	CJobPool Pool;
	ASSERT_TRUE(Pool.TryInit(1));
	Pool.Shutdown();
	CUsersParseEngine Engine;
	Engine.m_pPool = &Pool;
	std::shared_ptr<IJob> pPending;
	auto pInput = Payload();
	EXPECT_FALSE(Queue(pPending, pInput, Engine));
	EXPECT_EQ(pPending, nullptr);
	EXPECT_NE(pInput, nullptr);
	EXPECT_EQ(Engine.m_LastNativeState, IJob::STATE_QUEUED);
	EXPECT_TRUE(Engine.m_LastAttempt.expired());
	ASSERT_TRUE(Pool.TryInit(1));
	ASSERT_TRUE(Queue(pPending, pInput, Engine));
	ASSERT_TRUE(WaitForJobDone(*pPending));
	Pool.Shutdown();
	EXPECT_TRUE(std::static_pointer_cast<CQmClientUsersParseJob>(pPending)->TakeResult().m_Parsed);
}

TEST(QmClientUsersParseJob, EmptyInputDoesNotConstructOrDispatchJob)
{
	CUsersParseEngine Engine;
	std::shared_ptr<IJob> pPending;
	std::shared_ptr<const json_value> pInput;
	int Constructs = 0;
	EXPECT_FALSE(TryQueueQmClientUsersParseJob(pPending, pInput, Engine, [&](const auto &pPayload) {
		++Constructs;
		return std::make_shared<CQmClientUsersParseJob>(pPayload, "one:8303", 900, 12);
	}));
	EXPECT_EQ(Constructs, 0);
	EXPECT_EQ(Engine.m_Attempts, 0);
}

TEST(QmClientUsersParseJob, QueuedPublicationWaitsWithoutConsumingInput)
{
	using EPublish = CQmClientUsersParseJob::EPublishResult;
	auto pJob = std::make_shared<CQmClientUsersParseJob>(Payload(), "one:8303", 900, 12);
	int Calls = 0;
	EXPECT_EQ(pJob->PublishResult(true, [&](auto &) { ++Calls; return true; }), EPublish::WAITING);
	EXPECT_EQ(Calls, 0);
	CJobPool Pool;
	ASSERT_TRUE(Pool.TryInit(1));
	ASSERT_TRUE(Pool.TryAdd(pJob));
	ASSERT_TRUE(WaitForJobDone(*pJob));
	Pool.Shutdown();
	EXPECT_EQ(pJob->PublishResult(false, [&](auto &Result) {
		++Calls;
		EXPECT_TRUE(Result.m_Parsed);
		EXPECT_EQ(Result.m_OnlineUserCount, 2);
		return true;
	}),
		EPublish::PUBLISHED);
	EXPECT_EQ(Calls, 1);
}

TEST(QmClientUsersParseJob, RejectedPublicationRetainsParsedResultUntilAcceptedOnce)
{
	using EPublish = CQmClientUsersParseJob::EPublishResult;
	auto pJob = CompletedJob();
	ASSERT_NE(pJob, nullptr);
	for(int Attempt = 0; Attempt < 2; ++Attempt)
	{
		EXPECT_EQ(pJob->PublishResult(false, [](auto &Result) {
			EXPECT_TRUE(Result.m_Parsed);
			EXPECT_EQ(Result.m_OnlineUserCount, 2);
			EXPECT_EQ(Result.m_vLocalServerMarks.size(), 2u);
			return false;
		}),
			EPublish::RETRY);
	}
	SQmClientUsersParseResult Published;
	EXPECT_EQ(pJob->PublishResult(false, [&](auto &Result) { Published = std::move(Result); return true; }), EPublish::PUBLISHED);
	EXPECT_TRUE(Published.m_Parsed);
	EXPECT_EQ(Published.m_OnlineUserCount, 2);
	ASSERT_EQ(Published.m_vLocalServerMarks.size(), 2u);
	EXPECT_EQ(Published.m_vLocalServerMarks[0].m_Name, "Alice");
	int Calls = 0;
	EXPECT_EQ(pJob->PublishResult(false, [&](auto &) { ++Calls; return true; }), EPublish::INVALID);
	EXPECT_EQ(Calls, 0);
}

TEST(QmClientUsersParseJob, PublisherBadAllocCanRetryAfterInputTreeWasReleased)
{
	using EPublish = CQmClientUsersParseJob::EPublishResult;
	std::weak_ptr<const json_value> InputOwner;
	auto pJob = CompletedJob(&InputOwner);
	ASSERT_NE(pJob, nullptr);
	ASSERT_TRUE(InputOwner.expired());
	EXPECT_EQ(pJob->PublishResult(false, [](auto &) -> bool { throw std::bad_alloc(); }), EPublish::RETRY);
	EXPECT_EQ(pJob->ExpireTick(), 900);
	EXPECT_EQ(pJob->ConnectionTick(), 12);
	EXPECT_EQ(pJob->PublishResult(false, [](auto &Result) {
		EXPECT_EQ(Result.m_OnlineDummyCount, 1);
		EXPECT_EQ(Result.m_vServerDistribution.size(), 2u);
		return true;
	}),
		EPublish::PUBLISHED);
	EXPECT_TRUE(InputOwner.expired());
	EXPECT_EQ(pJob->ExpireTick(), 900);
}

TEST(QmClientUsersParseJob, NewerSnapshotSupersedesFailedResultWithoutCallingPublisher)
{
	using EPublish = CQmClientUsersParseJob::EPublishResult;
	auto pJob = CompletedJob();
	ASSERT_NE(pJob, nullptr);
	ASSERT_EQ(pJob->PublishResult(false, [](auto &) { return false; }), EPublish::RETRY);
	int Calls = 0;
	EXPECT_EQ(pJob->PublishResult(true, [&](auto &) { ++Calls; return true; }), EPublish::SUPERSEDED);
	EXPECT_EQ(Calls, 0);
	EXPECT_FALSE(pJob->TakeResult().m_Parsed);
}

TEST(QmClientUsersParseJob, NewerSnapshotAlsoSupersedesFreshCompletedResult)
{
	using EPublish = CQmClientUsersParseJob::EPublishResult;
	auto pJob = CompletedJob();
	ASSERT_NE(pJob, nullptr);
	int Calls = 0;
	EXPECT_EQ(pJob->PublishResult(true, [&](auto &) { ++Calls; return true; }), EPublish::SUPERSEDED);
	EXPECT_EQ(Calls, 0);
	EXPECT_FALSE(pJob->TakeResult().m_Parsed);
}
