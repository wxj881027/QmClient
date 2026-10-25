// 下载会话与真实作业池、隔离存储协作；fake 只替换 HTTP 传输和图片解码边界。
#include <base/system.h>

#include <engine/storage.h>

#include <game/client/components/qmclient/skin_download_session.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace
{
	class CTestSkinDataJob : public IQmSkinDataJob
	{
		bool m_SourceValid;
		bool m_Valid = false;
		void Run() override { m_Valid = m_SourceValid; }

	public:
		explicit CTestSkinDataJob(bool Valid) :
			m_SourceValid(Valid) { Abortable(true); }
		bool HasData() const override { return m_Valid; }
	};

	class CTestSkinRequest : public IHttpRequest
	{
	public:
		CTestSkinRequest(const char *pUrl, IStorage *pStorage, bool Unconditional) :
			IHttpRequest(pUrl)
		{
			WriteToFileAndMemory(pStorage, "downloadedskins/test.png", IStorage::TYPE_SAVE);
			m_IfModifiedSince = Unconditional ? -1 : 1;
			m_State = EHttpState::RUNNING;
		}
		void Header(const char *) override {}
		void Finish(int Status, const char *pBody = "new-valid-image")
		{
			m_StatusCode = Status;
			m_ResponseLength = str_length(pBody);
			m_pBuffer = static_cast<unsigned char *>(malloc(m_ResponseLength));
			mem_copy(m_pBuffer, pBody, m_ResponseLength);
			IOHANDLE File = io_open(m_aDestAbsoluteTmp, IOFLAG_WRITE);
			ASSERT_NE(File, nullptr);
			io_write(File, pBody, m_ResponseLength);
			io_close(File);
			m_State = EHttpState::DONE;
		}
		void Fail() { m_State = EHttpState::ERROR; }
		bool Unconditional() const { return m_IfModifiedSince < 0; }
		HTTPLOG LogSetting() const { return m_LogProgress; }
		const CTimeout &TimeoutSetting() const { return m_Timeout; }
		const char *TemporaryPath() const { return m_aDestAbsoluteTmp; }
	};
	class CTestSkinHttp : public IHttp
	{
	public:
		std::vector<std::shared_ptr<CTestSkinRequest>> m_vRequests;
		bool m_ThrowBeforeAccept = false;
		std::shared_ptr<CTestSkinRequest> m_pRejected;
		void Run(std::shared_ptr<IHttpRequest> pRequest) override
		{
			if(m_ThrowBeforeAccept)
			{
				m_pRejected = std::static_pointer_cast<CTestSkinRequest>(pRequest);
				throw std::bad_alloc();
			}
			m_vRequests.push_back(std::static_pointer_cast<CTestSkinRequest>(pRequest));
		}
		bool HasIpresolveBug() const override { return false; }
	};

	class QmSkinDownloadSession : public ::testing::Test
	{
	protected:
		using EResult = CQmSkinDownloadSession::EResult;
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CJobPool m_Pool;
		CTestSkinHttp m_Http;
		std::vector<std::shared_ptr<IJob>> m_vJobs;
		bool m_DecodeValid = true;
		bool m_DecodeThrows = false;
		int m_RequestCalls = 0, m_RequestThrowAt = 0;
		int m_SubmitCalls = 0, m_SubmitThrowAt = 0;
		std::shared_ptr<IJob> m_pLastSubmitted;
		bool m_PoolStopped = false;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("downloadedskins", IStorage::TYPE_SAVE));
			m_Pool.Init(1);
		}
		void StopPool()
		{
			if(!m_PoolStopped)
			{
				m_Pool.Shutdown();
				m_PoolStopped = true;
			}
		}
		void TearDown() override { StopPool(); }
		void WaitJobs()
		{
			const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			for(const auto &pJob : m_vJobs)
			{
				while(!pJob->Done() && std::chrono::steady_clock::now() < Deadline)
					std::this_thread::yield();
				ASSERT_TRUE(pJob->Done());
			}
		}
		std::unique_ptr<CQmSkinDownloadSession> Create(bool CacheValid, bool Community = true, bool SameUrl = false)
		{
			return std::make_unique<CQmSkinDownloadSession>(m_Http, "https://official.test/test.png", Community ? (SameUrl ? "https://official.test/test.png" : "https://community.test/test.png") : "", std::make_shared<CTestSkinDataJob>(CacheValid), [this](const char *pUrl, bool Unconditional) {
					if(++m_RequestCalls == m_RequestThrowAt)
						throw std::bad_alloc();
					return std::make_shared<CTestSkinRequest>(pUrl, m_pStorage.get(), Unconditional); }, [this](std::shared_ptr<IHttpRequest>) {
					if(m_DecodeThrows)
						throw std::bad_alloc();
					return std::make_shared<CTestSkinDataJob>(m_DecodeValid); }, [this](std::shared_ptr<IJob> pJob) {
					m_pLastSubmitted = pJob;
					if(++m_SubmitCalls == m_SubmitThrowAt)
						throw std::bad_alloc();
					m_vJobs.push_back(pJob);
					m_Pool.Add(std::move(pJob)); });
		}
		void WriteCache(const char *pBody)
		{
			IOHANDLE File = m_pStorage->OpenFile("downloadedskins/test.png", IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			io_write(File, pBody, str_length(pBody));
			io_close(File);
		}
		std::string ReadCache()
		{
			char *pData = m_pStorage->ReadFileStr("downloadedskins/test.png", IStorage::TYPE_SAVE);
			if(!pData)
				return {};
			std::string Data(pData);
			free(pData);
			return Data;
		}
		void BeginWithoutCache(CQmSkinDownloadSession &Session)
		{
			WaitJobs();
			ASSERT_EQ(Session.Poll(), EResult::WAITING);
			ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
		}
	};
}

TEST_F(QmSkinDownloadSession, ValidCacheIsReadyBeforeNetworkCompletes)
{
	auto pSession = Create(true);
	WaitJobs();
	EXPECT_EQ(pSession->Poll(), EResult::READY);
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	EXPECT_FALSE(m_Http.m_vRequests[0]->Done());
	auto pReady = pSession->TakeReadyJob();
	ASSERT_NE(pReady, nullptr);
	EXPECT_TRUE(pReady->HasData());
	EXPECT_EQ(pSession->Poll(), EResult::WAITING);
	EXPECT_EQ(pSession->TakeReadyJob(), nullptr);
}

TEST_F(QmSkinDownloadSession, PendingNetworkDoesNotOccupySingleWorker)
{
	auto pSession = Create(false);
	BeginWithoutCache(*pSession);
	auto pOther = std::make_shared<CTestSkinDataJob>(true);
	m_vJobs.push_back(pOther);
	m_Pool.Add(pOther);
	WaitJobs();
	EXPECT_TRUE(pOther->HasData());
	EXPECT_FALSE(m_Http.m_vRequests[0]->Done());
}

TEST_F(QmSkinDownloadSession, OfficialNotFoundFallsBackToEnabledCommunity)
{
	auto pSession = Create(false);
	BeginWithoutCache(*pSession);
	m_Http.m_vRequests[0]->Finish(404);
	EXPECT_EQ(pSession->Poll(), EResult::WAITING);
	ASSERT_EQ(m_Http.m_vRequests.size(), 2u);
	EXPECT_STREQ(m_Http.m_vRequests[1]->Url(), "https://community.test/test.png");
	EXPECT_TRUE(m_Http.m_vRequests[1]->Unconditional());
	m_Http.m_vRequests[1]->Finish(200);
	EXPECT_EQ(pSession->Poll(), EResult::WAITING);
	WaitJobs();
	EXPECT_EQ(pSession->Poll(), EResult::READY);
	EXPECT_NE(pSession->TakeReadyJob(), nullptr);
	EXPECT_EQ(pSession->Poll(), EResult::DONE);
	EXPECT_EQ(ReadCache(), "new-valid-image");
}

TEST_F(QmSkinDownloadSession, CommunityDisabledDoesNotFallBack)
{
	auto pSession = Create(false, false);
	BeginWithoutCache(*pSession);
	m_Http.m_vRequests[0]->Finish(404);
	EXPECT_EQ(pSession->Poll(), EResult::NOT_FOUND);
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
}

TEST_F(QmSkinDownloadSession, IdenticalUrlsDoNotDuplicateRequest)
{
	auto pSession = Create(false, true, true);
	BeginWithoutCache(*pSession);
	m_Http.m_vRequests[0]->Finish(404);
	EXPECT_EQ(pSession->Poll(), EResult::NOT_FOUND);
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
}

TEST_F(QmSkinDownloadSession, BothSourcesNotFoundEndsWithoutRetryLoop)
{
	auto pSession = Create(false);
	BeginWithoutCache(*pSession);
	m_Http.m_vRequests[0]->Finish(404);
	ASSERT_EQ(pSession->Poll(), EResult::WAITING);
	ASSERT_EQ(m_Http.m_vRequests.size(), 2u);
	m_Http.m_vRequests[1]->Finish(404);
	EXPECT_EQ(pSession->Poll(), EResult::NOT_FOUND);
	EXPECT_EQ(pSession->Poll(), EResult::NOT_FOUND);
	EXPECT_EQ(m_Http.m_vRequests.size(), 2u);
}

TEST_F(QmSkinDownloadSession, ServerFailureDoesNotChangeSource)
{
	auto pSession = Create(false);
	BeginWithoutCache(*pSession);
	m_Http.m_vRequests[0]->Finish(503);
	EXPECT_EQ(pSession->Poll(), EResult::ERROR);
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
}

TEST_F(QmSkinDownloadSession, NetworkFailureWithoutCacheEndsInError)
{
	auto pSession = Create(false);
	BeginWithoutCache(*pSession);
	m_Http.m_vRequests[0]->Fail();
	EXPECT_EQ(pSession->Poll(), EResult::ERROR);
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
}

TEST_F(QmSkinDownloadSession, NetworkFailureKeepsDeliveredCache)
{
	WriteCache("old-valid-image");
	auto pSession = Create(true);
	WaitJobs();
	ASSERT_EQ(pSession->Poll(), EResult::READY);
	auto pReady = pSession->TakeReadyJob();
	m_Http.m_vRequests[0]->Fail();
	EXPECT_EQ(pSession->Poll(), EResult::DONE);
	EXPECT_TRUE(pReady->HasData());
	EXPECT_EQ(ReadCache(), "old-valid-image");
}

TEST_F(QmSkinDownloadSession, NotModifiedKeepsValidCacheWithoutDecode)
{
	WriteCache("old-valid-image");
	auto pSession = Create(true);
	WaitJobs();
	ASSERT_EQ(pSession->Poll(), EResult::READY);
	pSession->TakeReadyJob();
	m_Http.m_vRequests[0]->Finish(304);
	EXPECT_EQ(pSession->Poll(), EResult::DONE);
	EXPECT_EQ(m_vJobs.size(), 1u);
	EXPECT_EQ(ReadCache(), "old-valid-image");
}

TEST_F(QmSkinDownloadSession, NotModifiedWithInvalidCacheRetriesUnconditionally)
{
	auto pSession = Create(false);
	BeginWithoutCache(*pSession);
	m_Http.m_vRequests[0]->Finish(304);
	EXPECT_EQ(pSession->Poll(), EResult::WAITING);
	ASSERT_EQ(m_Http.m_vRequests.size(), 2u);
	EXPECT_TRUE(m_Http.m_vRequests[1]->Unconditional());
	m_Http.m_vRequests[1]->Finish(304);
	EXPECT_EQ(pSession->Poll(), EResult::ERROR);
	EXPECT_EQ(m_Http.m_vRequests.size(), 2u);
}

TEST_F(QmSkinDownloadSession, InvalidDownloadedImageDoesNotOverwriteValidCache)
{
	WriteCache("old-valid-image");
	m_DecodeValid = false;
	auto pSession = Create(true);
	WaitJobs();
	ASSERT_EQ(pSession->Poll(), EResult::READY);
	pSession->TakeReadyJob();
	m_Http.m_vRequests[0]->Finish(200, "broken-image");
	ASSERT_EQ(pSession->Poll(), EResult::WAITING);
	WaitJobs();
	EXPECT_EQ(pSession->Poll(), EResult::DONE);
	EXPECT_EQ(pSession->TakeReadyJob(), nullptr);
	EXPECT_EQ(ReadCache(), "old-valid-image");
}

TEST_F(QmSkinDownloadSession, InvalidDownloadedImageWithoutCacheEndsInError)
{
	m_DecodeValid = false;
	auto pSession = Create(false);
	BeginWithoutCache(*pSession);
	m_Http.m_vRequests[0]->Finish(200, "broken-image");
	ASSERT_EQ(pSession->Poll(), EResult::WAITING);
	WaitJobs();
	EXPECT_EQ(pSession->Poll(), EResult::ERROR);
	EXPECT_EQ(ReadCache(), "");
}

TEST_F(QmSkinDownloadSession, ValidUpdateIsDeliveredAfterValidation)
{
	WriteCache("old-valid-image");
	auto pSession = Create(true);
	WaitJobs();
	ASSERT_EQ(pSession->Poll(), EResult::READY);
	pSession->TakeReadyJob();
	m_Http.m_vRequests[0]->Finish(200);
	ASSERT_EQ(pSession->Poll(), EResult::WAITING);
	EXPECT_EQ(ReadCache(), "old-valid-image");
	WaitJobs();
	EXPECT_EQ(pSession->Poll(), EResult::READY);
	EXPECT_EQ(ReadCache(), "new-valid-image");
	EXPECT_NE(pSession->TakeReadyJob(), nullptr);
	EXPECT_EQ(pSession->Poll(), EResult::DONE);
}

TEST_F(QmSkinDownloadSession, RepeatedPollDoesNotResubmitDecode)
{
	auto pSession = Create(false);
	BeginWithoutCache(*pSession);
	m_Http.m_vRequests[0]->Finish(200);
	ASSERT_EQ(pSession->Poll(), EResult::WAITING);
	WaitJobs();
	EXPECT_EQ(pSession->Poll(), EResult::READY);
	EXPECT_EQ(pSession->Poll(), EResult::READY);
	EXPECT_EQ(m_vJobs.size(), 2u);
	pSession->TakeReadyJob();
	EXPECT_EQ(pSession->Poll(), EResult::DONE);
}

TEST_F(QmSkinDownloadSession, CancelDuringDecodePreventsLateCacheReplacement)
{
	WriteCache("old-valid-image");
	auto pSession = Create(false);
	BeginWithoutCache(*pSession);
	m_Http.m_vRequests[0]->Finish(200);
	ASSERT_EQ(pSession->Poll(), EResult::WAITING);
	pSession->Cancel();
	WaitJobs();
	EXPECT_EQ(pSession->Poll(), EResult::DONE);
	EXPECT_EQ(pSession->TakeReadyJob(), nullptr);
	EXPECT_EQ(ReadCache(), "old-valid-image");
}

TEST_F(QmSkinDownloadSession, CancelDuringNetworkStopsPollingAndFallback)
{
	auto pSession = Create(false);
	BeginWithoutCache(*pSession);
	pSession->Cancel();
	pSession->Cancel();
	EXPECT_TRUE(m_Http.m_vRequests[0]->IsAbortRequested());
	EXPECT_EQ(pSession->Poll(), EResult::DONE);
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
}

TEST_F(QmSkinDownloadSession, RequestsHaveFiniteDeadlineAndFailureLogging)
{
	auto pSession = Create(false);
	BeginWithoutCache(*pSession);
	const auto &pRequest = m_Http.m_vRequests[0];
	EXPECT_GT(pRequest->TimeoutSetting().m_TimeoutMs, 0);
	EXPECT_GT(pRequest->TimeoutSetting().m_ConnectTimeoutMs, 0);
	EXPECT_EQ(pRequest->LogSetting(), HTTPLOG::FAILURE);
}

TEST_F(QmSkinDownloadSession, CancelBeforeCacheCompletesNeverStartsNetwork)
{
	auto pSession = Create(true);
	pSession->Cancel();
	WaitJobs();
	EXPECT_EQ(pSession->Poll(), EResult::DONE);
	EXPECT_EQ(pSession->TakeReadyJob(), nullptr);
	EXPECT_TRUE(m_Http.m_vRequests.empty());
}

TEST_F(QmSkinDownloadSession, BothSourcesNotFoundKeepDeliveredCache)
{
	WriteCache("old-valid-image");
	auto pSession = Create(true);
	WaitJobs();
	ASSERT_EQ(pSession->Poll(), EResult::READY);
	pSession->TakeReadyJob();
	m_Http.m_vRequests[0]->Finish(404);
	ASSERT_EQ(pSession->Poll(), EResult::WAITING);
	ASSERT_EQ(m_Http.m_vRequests.size(), 2u);
	m_Http.m_vRequests[1]->Finish(404);
	EXPECT_EQ(pSession->Poll(), EResult::DONE);
	EXPECT_EQ(ReadCache(), "old-valid-image");
}

TEST_F(QmSkinDownloadSession, NewSessionAfterCancellationCanDeliverReplacement)
{
	WriteCache("old-valid-image");
	auto pOld = Create(false);
	BeginWithoutCache(*pOld);
	m_Http.m_vRequests[0]->Finish(200, "obsolete-image");
	ASSERT_EQ(pOld->Poll(), EResult::WAITING);
	pOld->Cancel();
	WaitJobs();
	auto pNew = Create(false);
	WaitJobs();
	ASSERT_EQ(pNew->Poll(), EResult::WAITING);
	ASSERT_EQ(m_Http.m_vRequests.size(), 2u);
	m_Http.m_vRequests[1]->Finish(200, "current-image");
	ASSERT_EQ(pNew->Poll(), EResult::WAITING);
	WaitJobs();
	EXPECT_EQ(pNew->Poll(), EResult::READY);
	EXPECT_EQ(ReadCache(), "current-image");
	EXPECT_EQ(pOld->Poll(), EResult::DONE);
	pNew->TakeReadyJob();
}

TEST_F(QmSkinDownloadSession, InitialSubmissionAllocationFailureTerminatesAndAbortsCacheJob)
{
	m_SubmitThrowAt = 1;
	auto pSession = Create(false);
	ASSERT_NE(m_pLastSubmitted, nullptr);
	EXPECT_EQ(m_pLastSubmitted->State(), IJob::STATE_ABORTED);
	EXPECT_EQ(pSession->Poll(), EResult::ERROR);
	EXPECT_EQ(pSession->Poll(), EResult::ERROR);
	EXPECT_EQ(pSession->TakeReadyJob(), nullptr);
	EXPECT_TRUE(m_Http.m_vRequests.empty());
}

TEST_F(QmSkinDownloadSession, RequestFactoryAllocationFailureIsTerminalWithoutCache)
{
	m_RequestThrowAt = 1;
	auto pSession = Create(false);
	WaitJobs();
	EXPECT_EQ(pSession->Poll(), EResult::ERROR);
	EXPECT_EQ(pSession->Poll(), EResult::ERROR);
	EXPECT_EQ(m_RequestCalls, 1);
	EXPECT_TRUE(m_Http.m_vRequests.empty());
}

TEST_F(QmSkinDownloadSession, RequestAllocationFailureStillDeliversPreparedCacheOnce)
{
	WriteCache("old-valid-image");
	m_RequestThrowAt = 1;
	auto pSession = Create(true);
	WaitJobs();
	EXPECT_EQ(pSession->Poll(), EResult::READY);
	EXPECT_EQ(pSession->Poll(), EResult::READY);
	auto pReady = pSession->TakeReadyJob();
	ASSERT_NE(pReady, nullptr);
	EXPECT_TRUE(pReady->HasData());
	EXPECT_EQ(pSession->Poll(), EResult::DONE);
	EXPECT_EQ(pSession->TakeReadyJob(), nullptr);
	EXPECT_EQ(m_RequestCalls, 1);
	EXPECT_EQ(ReadCache(), "old-valid-image");
}

TEST_F(QmSkinDownloadSession, DecodeAllocationFailureRejectsTemporaryFileAndPreservesDiskCache)
{
	WriteCache("old-valid-image");
	auto pSession = Create(false);
	BeginWithoutCache(*pSession);
	m_Http.m_vRequests[0]->Finish(200, "candidate-image");
	ASSERT_TRUE(fs_is_file(m_Http.m_vRequests[0]->TemporaryPath()));
	m_DecodeThrows = true;
	EXPECT_EQ(pSession->Poll(), EResult::ERROR);
	EXPECT_EQ(pSession->Poll(), EResult::ERROR);
	EXPECT_EQ(pSession->TakeReadyJob(), nullptr);
	EXPECT_EQ(ReadCache(), "old-valid-image");
	EXPECT_FALSE(fs_is_file(m_Http.m_vRequests[0]->TemporaryPath()));
}

TEST_F(QmSkinDownloadSession, DecodeSubmissionAllocationFailureAbortsCandidateInsteadOfWaiting)
{
	WriteCache("old-valid-image");
	auto pSession = Create(false);
	BeginWithoutCache(*pSession);
	m_Http.m_vRequests[0]->Finish(200, "candidate-image");
	m_SubmitThrowAt = 2;
	EXPECT_EQ(pSession->Poll(), EResult::ERROR);
	ASSERT_NE(m_pLastSubmitted, nullptr);
	EXPECT_EQ(m_pLastSubmitted->State(), IJob::STATE_ABORTED);
	EXPECT_EQ(m_vJobs.size(), 1u);
	EXPECT_EQ(pSession->Poll(), EResult::ERROR);
	EXPECT_EQ(ReadCache(), "old-valid-image");
}

TEST_F(QmSkinDownloadSession, CommunityFallbackAllocationFailureDoesNotRetryOrLeavePendingRequest)
{
	auto pSession = Create(false);
	BeginWithoutCache(*pSession);
	m_RequestThrowAt = 2;
	m_Http.m_vRequests[0]->Finish(404);
	EXPECT_EQ(pSession->Poll(), EResult::ERROR);
	EXPECT_EQ(pSession->Poll(), EResult::ERROR);
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
	EXPECT_EQ(m_RequestCalls, 2);
}

TEST_F(QmSkinDownloadSession, UnconditionalRetryAllocationFailureEndsInvalidCacheUpdate)
{
	auto pSession = Create(false);
	BeginWithoutCache(*pSession);
	m_RequestThrowAt = 2;
	m_Http.m_vRequests[0]->Finish(304);
	EXPECT_EQ(pSession->Poll(), EResult::ERROR);
	EXPECT_EQ(pSession->Poll(), EResult::ERROR);
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
	EXPECT_EQ(m_RequestCalls, 2);
}

TEST_F(QmSkinDownloadSession, DecodeAllocationFailureKeepsAlreadyDeliveredCache)
{
	WriteCache("old-valid-image");
	auto pSession = Create(true);
	WaitJobs();
	ASSERT_EQ(pSession->Poll(), EResult::READY);
	auto pReady = pSession->TakeReadyJob();
	m_Http.m_vRequests[0]->Finish(200, "candidate-image");
	m_DecodeThrows = true;
	EXPECT_EQ(pSession->Poll(), EResult::DONE);
	ASSERT_NE(pReady, nullptr);
	EXPECT_TRUE(pReady->HasData());
	EXPECT_EQ(pSession->TakeReadyJob(), nullptr);
	EXPECT_EQ(ReadCache(), "old-valid-image");
}

TEST_F(QmSkinDownloadSession, NewSessionCanRecoverAfterAllocationFailureAndOldCancelIsIdempotent)
{
	m_RequestThrowAt = 1;
	auto pOld = Create(false);
	WaitJobs();
	ASSERT_EQ(pOld->Poll(), EResult::ERROR);
	pOld->Cancel();
	pOld->Cancel();
	EXPECT_EQ(pOld->Poll(), EResult::DONE);
	m_RequestThrowAt = 0;
	auto pNew = Create(false);
	BeginWithoutCache(*pNew);
	m_Http.m_vRequests[0]->Finish(200, "recovered-image");
	ASSERT_EQ(pNew->Poll(), EResult::WAITING);
	WaitJobs();
	EXPECT_EQ(pNew->Poll(), EResult::READY);
	EXPECT_EQ(ReadCache(), "recovered-image");
	pNew->TakeReadyJob();
	EXPECT_EQ(pNew->Poll(), EResult::DONE);
	EXPECT_EQ(m_RequestCalls, 2);
}

TEST_F(QmSkinDownloadSession, ClosedRealJobPoolAbortsRejectedDecodeAndSessionDoesNotHang)
{
	StopPool();
	auto pSession = Create(false);
	BeginWithoutCache(*pSession);
	m_Http.m_vRequests[0]->Finish(200);
	ASSERT_EQ(pSession->Poll(), EResult::WAITING);
	ASSERT_NE(m_pLastSubmitted, nullptr);
	EXPECT_EQ(m_pLastSubmitted->State(), IJob::STATE_ABORTED);
	EXPECT_EQ(pSession->Poll(), EResult::ERROR);
	EXPECT_EQ(pSession->Poll(), EResult::ERROR);
}

TEST_F(QmSkinDownloadSession, ExternalHttpSubmissionAllocationFailureAbortsPreparedRequest)
{
	// 只验证外部提交边界；现行native Run已自行处理内部pending分配失败。
	m_Http.m_ThrowBeforeAccept = true;
	auto pSession = Create(false);
	WaitJobs();
	EXPECT_EQ(pSession->Poll(), EResult::ERROR);
	ASSERT_NE(m_Http.m_pRejected, nullptr);
	EXPECT_TRUE(m_Http.m_pRejected->IsAbortRequested());
	EXPECT_TRUE(m_Http.m_vRequests.empty());
	EXPECT_EQ(pSession->Poll(), EResult::ERROR);
}
