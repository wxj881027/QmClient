#include <base/fs.h>

#include <game/client/components/qmclient/custom_community_info.h>

#include <gtest/gtest.h>
#include <test/support/info_download_response.h>
#include <test/test.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace
{
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
