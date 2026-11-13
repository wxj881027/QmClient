#include <game/client/components/qmclient/friend_last_seen.h>

#include <gtest/gtest.h>
#include <test/support/friend_last_seen_test_support.h>
#include <test/support/job_wait.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/test.h>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace
{
	constexpr int64_t NOW = 1792800000;
	constexpr const char *CACHE_FILE = "qm_friend_last_seen.json";
	constexpr const char *OLD_CACHE = R"({"version":1,"players":[{"name":"Alice","date":"2026-10-20","checked_at":"100"}]})";

	// 同一原生线程在真实解析前启用 new 故障，解析后由报告任务解除；不替换解析输入接口。
	class CFriendWorkerAllocationStep : public IJob
	{
		inline static thread_local std::optional<CPredictionAllocationFailure> s_Failure;
		bool m_Arm;
		std::shared_ptr<std::atomic<bool>> m_pTriggered;
		void Run() override
		{
			if(m_Arm)
				s_Failure.emplace();
			else
			{
				m_pTriggered->store(s_Failure && s_Failure->Triggered(), std::memory_order_release);
				s_Failure.reset();
			}
		}

	public:
		CFriendWorkerAllocationStep(bool Arm, std::shared_ptr<std::atomic<bool>> pTriggered) :
			m_Arm(Arm), m_pTriggered(std::move(pTriggered)) {}
	};

	class CFriendLastSeenAllocationServiceTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CFriendLastSeenTestFriends m_Friends;
		CFriendLastSeenTestEngine m_Engine;
		CFriendLastSeenTestHttp m_Http;
		CQmFriendLastSeenService m_Service{CreateFriendLastSeenTestRequest};
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_Friends.Set({"Alice"});
			Write(OLD_CACHE);
			ASSERT_FALSE(HasFatalFailure());
		}
		void Update(int64_t Now = NOW) { m_Service.Update(m_Friends, m_Engine, m_Http, true, Now); }
		void Write(std::string_view Body)
		{
			IOHANDLE File = m_pStorage->OpenFile(CACHE_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Body.data(), static_cast<unsigned>(Body.size())), Body.size());
			EXPECT_EQ(io_close(File), 0);
		}
		std::string Read()
		{
			void *pData = nullptr;
			unsigned Size = 0;
			const auto Result = m_pStorage->ReadFileCheckedBounded(CACHE_FILE, IStorage::TYPE_SAVE, &pData, &Size, 1024 * 1024);
			const std::unique_ptr<void, decltype(&free)> Data(pData, free);
			EXPECT_EQ(Result, IStorage::EReadFileResult::SUCCESS);
			return Data ? std::string(static_cast<const char *>(Data.get()), Size) : "";
		}
	};
}

TEST_F(CFriendLastSeenAllocationServiceTest, WorkerUtf8TreeAllocationFailureIsConsumedAsErrorWithoutLosingHistory)
{
	ASSERT_TRUE(m_Engine.Start());
	ASSERT_TRUE(m_Service.Load(*m_pStorage, NOW));
	ASSERT_NE(m_Service.Get("Alice", NOW), nullptr);
	Update();
	ASSERT_NE(m_Http.m_pLast, nullptr);
	ASSERT_TRUE(m_Http.m_pLast->Finish(R"({"recent_player_info":[{"name":"Alice","last_seen":"2026-10-23"}]})"));
	const auto pTriggered = std::make_shared<std::atomic<bool>>(false);
	const auto pArm = std::make_shared<CFriendWorkerAllocationStep>(true, pTriggered);
	const auto pReport = std::make_shared<CFriendWorkerAllocationStep>(false, pTriggered);
	ASSERT_TRUE(m_Engine.TryAddJob(pArm));
	ASSERT_TRUE(WaitForJobDone(*pArm));
	Update();
	const auto pParse = m_Engine.m_pLast.lock();
	// 即使后面的断言失败，也先保证原生线程上有解除注入的任务。
	ASSERT_TRUE(m_Engine.TryAddJob(pReport));
	ASSERT_TRUE(WaitForJobDone(*pReport));
	ASSERT_NE(pParse, nullptr);
	ASSERT_TRUE(WaitForJobFinished(*pParse));
	EXPECT_TRUE(pTriggered->load(std::memory_order_acquire));
	EXPECT_EQ(pParse->State(), IJob::STATE_FAILED);
	Update(NOW + 1);
	ASSERT_NE(m_Service.Find("Alice"), nullptr);
	EXPECT_EQ(m_Service.Find("Alice")->m_Status, EQmFriendLastSeenStatus::ERROR);
	EXPECT_EQ(m_Service.Find("Alice")->m_Date, "2026-10-20");
	EXPECT_EQ(m_Service.Find("Alice")->m_RetryAt, NOW + 1 + CQmFriendLastSeenCache::RETRY_SECONDS);
	Update(NOW + 2);
	EXPECT_EQ(m_Http.m_Submissions, 1);
	const int64_t RetryAt = NOW + 1 + CQmFriendLastSeenCache::RETRY_SECONDS;
	ASSERT_NE(m_Service.Get("Alice", RetryAt), nullptr);
	Update(RetryAt);
	EXPECT_EQ(m_Http.m_Submissions, 2);
}

TEST_F(CFriendLastSeenAllocationServiceTest, SerializeAllocationFailureDoesNotOpenOrTruncateAnyFileAndCanRetry)
{
	ASSERT_NE(m_Service.Get("Alice", NOW), nullptr);
	Update();
	ASSERT_NE(m_Http.m_pLast, nullptr);
	ASSERT_TRUE(m_Http.m_pLast->Finish("", 404));
	Update();
	CFriendLastSeenTestStorage Storage(*m_pStorage);
	bool Saved = true;
	bool Triggered = false;
	{
		CPredictionAllocationFailure Failure;
		Saved = m_Service.Save(Storage);
		Triggered = Failure.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_FALSE(Saved);
	EXPECT_EQ(Storage.m_OpenCalls, 0);
	EXPECT_EQ(Storage.m_Renames, 0);
	EXPECT_EQ(Read(), OLD_CACHE);
	ASSERT_TRUE(m_Service.Save(Storage));
	CQmFriendLastSeenCache Loaded;
	ASSERT_TRUE(Loaded.Load(Read(), NOW));
	ASSERT_NE(Loaded.Find("Alice"), nullptr);
	EXPECT_EQ(Loaded.Find("Alice")->m_Status, EQmFriendLastSeenStatus::NO_DATA);
}

TEST_F(CFriendLastSeenAllocationServiceTest, CheckedLoadAllocationFailureKeepsOldInFlightRequestUsable)
{
	ASSERT_NE(m_Service.Get("Alice", NOW), nullptr);
	Update();
	const auto pRequest = m_Http.m_pLast;
	ASSERT_NE(pRequest, nullptr);
	const auto *pOld = m_Service.Find("Alice");
	bool Loaded = true;
	bool Triggered = false;
	{
		CPredictionAllocationFailure Failure;
		Loaded = m_Service.Load(*m_pStorage, NOW);
		Triggered = Failure.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_FALSE(Loaded);
	EXPECT_EQ(m_Service.Find("Alice"), pOld);
	EXPECT_EQ(pRequest->m_AbortCalls, 0);
	ASSERT_TRUE(pRequest->Finish("", 404));
	Update();
	EXPECT_EQ(m_Service.Find("Alice")->m_Status, EQmFriendLastSeenStatus::NO_DATA);
	ASSERT_TRUE(m_Service.Load(*m_pStorage, NOW));
	EXPECT_EQ(m_Service.Find("Alice")->m_Date, "2026-10-20");
}
