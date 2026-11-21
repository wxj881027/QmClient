#include <base/fs.h>

#include <game/client/components/qmclient/custom_community_info.h>

#include <gtest/gtest.h>
#include <test/support/info_download_response.h>
#include <test/support/job_wait.h>
#include <test/test.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace
{
	class CCommunityCacheJobEngine final : public IEngine
	{
		CJobPool m_Pool;

	public:
		void Init() override { m_Pool.Init(1); }
		void AddJob(std::shared_ptr<IJob> pJob) override { m_Pool.Add(std::move(pJob)); }
		bool TryAddJob(std::shared_ptr<IJob> pJob) override { return m_Pool.TryAdd(std::move(pJob)); }
		void ShutdownJobs() override { m_Pool.Shutdown(); }
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
	};

	class QmCustomCommunityCacheDownload : public ::testing::Test
	{
	protected:
		CTestInfo m_TestInfo;
		std::unique_ptr<IStorage> m_pStorage;
		const std::string m_Old = R"({"communities":[{"id":"old"}]})";
		const std::string m_New = R"({"communities":[{"id":"new"}]})";
		void SetUp() override
		{
			m_pStorage = m_TestInfo.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			// 客户端启动时会先建好存档布局；缓存写入需要 qmclient/cache/network 父目录。
			ASSERT_TRUE(qm_storage::EnsureStorageLayout(m_pStorage.get()));
			ASSERT_NO_FATAL_FAILURE(WriteInfoDownloadCache(m_pStorage.get(), QMCLIENT_CUSTOM_COMMUNITIES_INFO_FILE, m_Old));
		}
		std::shared_ptr<CInfoDownloadTestRequest> Request()
		{
			return std::make_shared<CInfoDownloadTestRequest>(m_pStorage.get(), QMCLIENT_CUSTOM_COMMUNITIES_INFO_FILE);
		}
		std::shared_ptr<CQmCustomCommunityCacheJob> Job(const std::shared_ptr<CInfoDownloadTestRequest> &pRequest)
		{
			return std::make_shared<CQmCustomCommunityCacheJob>(m_pStorage.get(), pRequest, true);
		}
		void RunJob(const std::shared_ptr<CQmCustomCommunityCacheJob> &pJob)
		{
			CJobPool Pool;
			Pool.Init(1);
			Pool.Add(pJob);
			const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while(pJob->State() != IJob::STATE_DONE && std::chrono::steady_clock::now() < Deadline)
				std::this_thread::yield();
			ASSERT_EQ(pJob->State(), IJob::STATE_DONE);
		}
		std::string Cached() { return ReadInfoDownloadCache(m_pStorage.get(), QMCLIENT_CUSTOM_COMMUNITIES_INFO_FILE); }
	};
}

TEST_F(QmCustomCommunityCacheDownload, WorkerOnlyPreparesAndAcceptancePublishesTheCache)
{
	auto pRequest = Request();
	ASSERT_NO_FATAL_FAILURE(pRequest->Finish(m_New));
	auto pJob = Job(pRequest);
	ASSERT_NO_FATAL_FAILURE(RunJob(pJob));
	EXPECT_EQ(Cached(), m_Old);
	auto pInfo = pJob->TakeInfo();
	ASSERT_NE(pInfo, nullptr);
	EXPECT_STREQ((*pInfo)["communities"][0]["id"], "new");
	EXPECT_EQ(Cached(), m_New);
	EXPECT_FALSE(fs_is_file(pRequest->TemporaryPath()));
}

TEST_F(QmCustomCommunityCacheDownload, InvalidResponseKeepsTheOldCacheAndDiscardsTheCandidate)
{
	auto pRequest = Request();
	ASSERT_NO_FATAL_FAILURE(pRequest->Finish("{invalid"));
	auto pJob = Job(pRequest);
	ASSERT_NO_FATAL_FAILURE(RunJob(pJob));
	EXPECT_EQ(pJob->TakeInfo(), nullptr);
	EXPECT_EQ(Cached(), m_Old);
	EXPECT_EQ(pRequest->State(), EHttpState::ERROR);
	EXPECT_FALSE(fs_is_file(pRequest->TemporaryPath()));
}

TEST_F(QmCustomCommunityCacheDownload, MissingOrNonArrayCommunitiesKeepsTheOldCache)
{
	const char *apInvalidBodies[] = {
		"{}",
		R"({"communities":null})",
		R"({"communities":{}})",
		R"({"communities":"none"})",
		R"({"communities":0})",
		R"({"communities":false})",
	};
	for(const char *pBody : apInvalidBodies)
	{
		SCOPED_TRACE(pBody);
		auto pRequest = Request();
		ASSERT_NO_FATAL_FAILURE(pRequest->Finish(pBody));
		ASSERT_EQ(pRequest->State(), EHttpState::DONE);
		auto pJob = Job(pRequest);
		ASSERT_NO_FATAL_FAILURE(RunJob(pJob));
		EXPECT_EQ(pJob->TakeInfo(), nullptr);
		EXPECT_EQ(Cached(), m_Old);
		EXPECT_EQ(pRequest->State(), EHttpState::ERROR);
		EXPECT_FALSE(fs_is_file(pRequest->TemporaryPath()));
	}
}

TEST_F(QmCustomCommunityCacheDownload, EmptyCommunitiesArrayReplacesTheOldCache)
{
	const std::string Body = R"({"communities":[]})";
	auto pRequest = Request();
	ASSERT_NO_FATAL_FAILURE(pRequest->Finish(Body));
	auto pJob = Job(pRequest);
	ASSERT_NO_FATAL_FAILURE(RunJob(pJob));
	auto pInfo = pJob->TakeInfo();
	ASSERT_NE(pInfo, nullptr);
	const json_value &Communities = (*pInfo)["communities"];
	ASSERT_EQ(Communities.type, json_array);
	EXPECT_EQ(Communities.u.array.length, 0u);
	EXPECT_EQ(Cached(), Body);
	EXPECT_FALSE(fs_is_file(pRequest->TemporaryPath()));
}

TEST_F(QmCustomCommunityCacheDownload, FailedRenameDoesNotPublishThePreparedInfo)
{
	auto pRequest = Request();
	ASSERT_NO_FATAL_FAILURE(pRequest->Finish(m_New));
	auto pJob = Job(pRequest);
	ASSERT_NO_FATAL_FAILURE(RunJob(pJob));
	ASSERT_EQ(fs_remove(pRequest->TemporaryPath()), 0);
	EXPECT_EQ(pJob->TakeInfo(), nullptr);
	EXPECT_EQ(pRequest->State(), EHttpState::ERROR);
	EXPECT_EQ(Cached(), m_Old);
}

TEST_F(QmCustomCommunityCacheDownload, ClosedPoolDiscardsTheCandidateAndNextRequestCanPublish)
{
	CCommunityCacheJobEngine Engine;
	auto pRequest = Request();
	ASSERT_NO_FATAL_FAILURE(pRequest->Finish(m_New));
	const std::string TemporaryPath = pRequest->TemporaryPath();
	EXPECT_EQ(CQmCustomCommunityCacheJob::TryQueue(Engine, m_pStorage.get(), std::move(pRequest)), nullptr);
	EXPECT_FALSE(fs_is_file(TemporaryPath.c_str()));
	EXPECT_EQ(Cached(), m_Old);

	Engine.Init();
	auto pRetry = Request();
	ASSERT_NO_FATAL_FAILURE(pRetry->Finish(m_New));
	auto pJob = CQmCustomCommunityCacheJob::TryQueue(Engine, m_pStorage.get(), std::move(pRetry));
	ASSERT_NE(pJob, nullptr);
	ASSERT_TRUE(WaitForJobDone(*pJob));
	ASSERT_NE(pJob->TakeInfo(), nullptr);
	EXPECT_EQ(Cached(), m_New);
}

TEST_F(QmCustomCommunityCacheDownload, CancelBeforeQueueingCannotPublishTheResponse)
{
	auto pRequest = Request();
	ASSERT_NO_FATAL_FAILURE(pRequest->Finish(m_New));
	auto pJob = Job(pRequest);
	ASSERT_TRUE(pJob->Abort());
	EXPECT_EQ(pJob->TakeInfo(), nullptr);
	const std::string TemporaryPath = pRequest->TemporaryPath();
	pJob.reset();
	pRequest.reset();
	EXPECT_EQ(Cached(), m_Old);
	EXPECT_FALSE(fs_is_file(TemporaryPath.c_str()));
}

TEST_F(QmCustomCommunityCacheDownload, CancelAfterParsingCannotPublishThePreparedResult)
{
	auto pRequest = Request();
	ASSERT_NO_FATAL_FAILURE(pRequest->Finish(m_New));
	auto pJob = Job(pRequest);
	ASSERT_NO_FATAL_FAILURE(RunJob(pJob));
	ASSERT_TRUE(pJob->Abort());
	EXPECT_EQ(pJob->TakeInfo(), nullptr);
	const std::string TemporaryPath = pRequest->TemporaryPath();
	pJob.reset();
	pRequest.reset();
	EXPECT_EQ(Cached(), m_Old);
	EXPECT_FALSE(fs_is_file(TemporaryPath.c_str()));
}

TEST_F(QmCustomCommunityCacheDownload, ResponseAbortAfterParsingRejectsAcceptance)
{
	auto pRequest = Request();
	ASSERT_NO_FATAL_FAILURE(pRequest->Finish(m_New));
	auto pJob = Job(pRequest);
	ASSERT_NO_FATAL_FAILURE(RunJob(pJob));
	pRequest->Abort();
	EXPECT_EQ(pJob->TakeInfo(), nullptr);
	EXPECT_EQ(pRequest->State(), EHttpState::ERROR);
	EXPECT_EQ(Cached(), m_Old);
}

TEST_F(QmCustomCommunityCacheDownload, CorrectedResponseCanReplaceARejectedCandidate)
{
	auto pInvalid = Request();
	ASSERT_NO_FATAL_FAILURE(pInvalid->Finish("[]"));
	auto pRejected = Job(pInvalid);
	ASSERT_NO_FATAL_FAILURE(RunJob(pRejected));
	EXPECT_EQ(pRejected->TakeInfo(), nullptr);
	EXPECT_EQ(Cached(), m_Old);
	auto pValid = Request();
	ASSERT_NO_FATAL_FAILURE(pValid->Finish(m_New));
	auto pRetry = Job(pValid);
	ASSERT_NO_FATAL_FAILURE(RunJob(pRetry));
	ASSERT_NE(pRetry->TakeInfo(), nullptr);
	EXPECT_EQ(Cached(), m_New);
}

TEST_F(QmCustomCommunityCacheDownload, RepeatedTakeDoesNotValidateTwice)
{
	auto pRequest = Request();
	ASSERT_NO_FATAL_FAILURE(pRequest->Finish(m_New));
	auto pJob = Job(pRequest);
	ASSERT_NO_FATAL_FAILURE(RunJob(pJob));
	ASSERT_NE(pJob->TakeInfo(), nullptr);
	EXPECT_EQ(pJob->TakeInfo(), nullptr);
	EXPECT_EQ(pRequest->State(), EHttpState::DONE);
	EXPECT_EQ(Cached(), m_New);
}

TEST_F(QmCustomCommunityCacheDownload, CancelledJobOwnsItsResponseUntilItIsReleased)
{
	auto pRequest = Request();
	ASSERT_NO_FATAL_FAILURE(pRequest->Finish(m_New));
	const std::string TemporaryPath = pRequest->TemporaryPath();
	std::weak_ptr<IHttpRequest> pWeak = pRequest;
	auto pJob = Job(pRequest);
	pRequest.reset();
	ASSERT_FALSE(pWeak.expired());
	ASSERT_TRUE(pJob->Abort());
	pJob.reset();
	EXPECT_TRUE(pWeak.expired());
	EXPECT_EQ(Cached(), m_Old);
	EXPECT_FALSE(fs_is_file(TemporaryPath.c_str()));
}

TEST_F(QmCustomCommunityCacheDownload, FailedTransferDoesNotReadTheOldCacheAsANewResponse)
{
	auto pRequest = Request();
	ASSERT_NO_FATAL_FAILURE(pRequest->Finish(m_New, EHttpState::ERROR));
	auto pJob = Job(pRequest);
	ASSERT_NO_FATAL_FAILURE(RunJob(pJob));
	EXPECT_EQ(pJob->TakeInfo(), nullptr);
	EXPECT_EQ(Cached(), m_Old);
}
