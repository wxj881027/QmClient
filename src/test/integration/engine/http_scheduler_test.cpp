#include <engine/shared/http_curl.h>

#include <gtest/gtest.h>

#if !defined(CONF_PLATFORM_EMSCRIPTEN)
#include <base/fs.h>

#include <engine/storage.h>

#include <test/support/async_io_wait.h>
#include <test/test.h>

#include <functional>
#include <memory>
#include <new>
#include <string>
#include <vector>

// 只安排已初始化的传输边界；调用真实调度器，不执行multi_perform或访问网络。
class CHttpCurlSchedulerTestPeer
{
	CHttpCurl m_Http;

public:
	CHttpCurlSchedulerTestPeer()
	{
		m_Http.m_pMultiH = curl_multi_init();
		m_Http.m_State = CHttpCurl::RUNNING;
	}
	~CHttpCurlSchedulerTestPeer()
	{
		m_Http.DrainRequests(true);
		curl_multi_cleanup(m_Http.m_pMultiH);
		m_Http.m_pMultiH = nullptr;
	}
	bool Ready() const { return m_Http.m_pMultiH != nullptr; }
	void Run(const std::shared_ptr<IHttpRequest> &pRequest) { m_Http.Run(pRequest); }
	bool Pump() { return m_Http.ProcessPendingRequests(); }
	void RejectNewRequests() { m_Http.m_State = CHttpCurl::ERROR; }
	void Drain() { m_Http.DrainRequests(true); }
	void Cancel() { m_Http.CancelAbortedRequests(); }
	size_t Running() const { return m_Http.m_RunningRequests.size(); }
	size_t RunningForHost(std::string_view Host) const { return m_Http.RunningForHost(Host); }
	bool Tracked(const IHttpRequest *pRequest) const
	{
		for(const auto &Entry : m_Http.m_RunningRequests)
			if(Entry.second.get() == pRequest)
				return true;
		return false;
	}
	std::vector<std::shared_ptr<CHttpRequestCurl>> Pending() const
	{
		return {m_Http.m_PendingRequests.begin(), m_Http.m_PendingRequests.end()};
	}
};

namespace
{
	class CPreparedRequest : public CHttpRequestCurl
	{
	public:
		explicit CPreparedRequest(const char *pUrl = "https://scheduler.test/body") :
			CHttpRequestCurl(pUrl) { LogProgress(HTTPLOG::NONE); }
		int m_ConfigureCalls = 0;
		bool m_ThrowBefore = false;
		bool m_ThrowAfter = false;
		bool m_ConfigureFails = false;
		std::function<void()> m_PrepareAction;
		std::string TemporaryPath() const { return m_aDestAbsoluteTmp; }

	private:
		bool ConfigureHandle(CURL *pHandle) override
		{
			++m_ConfigureCalls;
			if(m_PrepareAction)
				m_PrepareAction();
			if(m_ThrowBefore)
				throw std::bad_alloc();
			if(m_ConfigureFails)
				return false;
			const bool Result = CHttpRequestCurl::ConfigureHandle(pHandle);
			if(m_ThrowAfter)
				throw std::bad_alloc();
			return Result;
		}
	};

	class CCompletion : public IHttpRequest::IProgressCallback
	{
	public:
		int m_Calls = 0;
		bool m_Throw = false;
		EHttpState m_Observed = EHttpState::QUEUED;
		std::function<void()> m_Action;
		void OnProgress() override {}
		void OnCompletion(EHttpState State) override
		{
			++m_Calls;
			m_Observed = State;
			if(m_Action)
				m_Action();
			if(m_Throw)
				throw std::bad_alloc();
		}
	};

	class HttpScheduler : public ::testing::Test
	{
	protected:
		CHttpCurlSchedulerTestPeer m_Http;
		void SetUp() override { ASSERT_TRUE(m_Http.Ready()); }
		std::shared_ptr<CPreparedRequest> Submit(const char *pUrl = "https://scheduler.test/body")
		{
			auto pRequest = std::make_shared<CPreparedRequest>(pUrl);
			m_Http.Run(pRequest);
			return pRequest;
		}
	};
}

TEST_F(HttpScheduler, DuplicateQueuedOrRunningSubmissionDoesNotStartAnotherTransfer)
{
	auto pRequest = Submit();
	m_Http.Run(pRequest);
	ASSERT_EQ(m_Http.Pending().size(), 1u);
	ASSERT_TRUE(m_Http.Pump());
	EXPECT_EQ(pRequest->State(), EHttpState::RUNNING);
	EXPECT_EQ(pRequest->m_ConfigureCalls, 1);
	m_Http.Run(pRequest);
	EXPECT_TRUE(m_Http.Pending().empty());
	EXPECT_EQ(m_Http.Running(), 1u);
}

TEST_F(HttpScheduler, PreviouslyCancelledRequestCompletesWithoutConfiguringTransport)
{
	auto pRequest = std::make_shared<CPreparedRequest>();
	pRequest->Abort();
	m_Http.Run(pRequest);
	ASSERT_TRUE(m_Http.Pump());
	EXPECT_EQ(pRequest->State(), EHttpState::ABORTED);
	EXPECT_EQ(pRequest->m_ConfigureCalls, 0);
	EXPECT_EQ(m_Http.Running(), 0u);
}

TEST_F(HttpScheduler, RequestIsTrackedBeforeExternalConfigurationBegins)
{
	auto pRequest = std::make_shared<CPreparedRequest>();
	bool Tracked = false;
	pRequest->m_PrepareAction = [&] { Tracked = m_Http.Tracked(pRequest.get()); };
	m_Http.Run(pRequest);
	ASSERT_TRUE(m_Http.Pump());
	EXPECT_TRUE(Tracked);
	EXPECT_EQ(pRequest->State(), EHttpState::RUNNING);
}

TEST_F(HttpScheduler, ConfigurationExceptionFailsOneRequestAndContinuesTheBatch)
{
	auto pFailed = std::make_shared<CPreparedRequest>();
	pFailed->m_ThrowBefore = true;
	auto pConsumer = std::make_shared<CCompletion>();
	pFailed->SetProgressCallback(pConsumer);
	m_Http.Run(pFailed);
	auto pNext = Submit();
	EXPECT_NO_THROW(EXPECT_TRUE(m_Http.Pump()));
	EXPECT_EQ(pFailed->State(), EHttpState::ERROR);
	EXPECT_EQ(pConsumer->m_Observed, EHttpState::ERROR);
	EXPECT_EQ(pConsumer->m_Calls, 1);
	EXPECT_FALSE(m_Http.Tracked(pFailed.get()));
	EXPECT_EQ(pNext->State(), EHttpState::RUNNING);
	EXPECT_EQ(m_Http.RunningForHost("scheduler.test"), 1u);
}

TEST_F(HttpScheduler, ConfigurationFalseRetainsExistingAbortClassification)
{
	auto pRequest = std::make_shared<CPreparedRequest>();
	pRequest->m_ConfigureFails = true;
	m_Http.Run(pRequest);
	ASSERT_TRUE(m_Http.Pump());
	EXPECT_EQ(pRequest->State(), EHttpState::ABORTED);
	EXPECT_FALSE(m_Http.Tracked(pRequest.get()));
	EXPECT_TRUE(m_Http.Pending().empty());
}

TEST_F(HttpScheduler, CancellationDuringFailedPreparationTakesPriority)
{
	auto pRequest = std::make_shared<CPreparedRequest>();
	pRequest->m_ThrowBefore = true;
	pRequest->m_PrepareAction = [pRawRequest = pRequest.get()] { pRawRequest->Abort(); };
	m_Http.Run(pRequest);
	ASSERT_TRUE(m_Http.Pump());
	EXPECT_EQ(pRequest->State(), EHttpState::ABORTED);
	EXPECT_FALSE(m_Http.Tracked(pRequest.get()));
}

TEST_F(HttpScheduler, KnownFileDigestCompletesWithoutOpeningAnotherTransfer)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	IOHANDLE File = pStorage->OpenFile("cached.bin", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	EXPECT_EQ(io_write(File, "cached", 6), 6u);
	EXPECT_EQ(io_close(File), 0);
	auto pRequest = std::make_shared<CPreparedRequest>();
	pRequest->WriteToFile(pStorage.get(), "cached.bin", IStorage::TYPE_SAVE);
	pRequest->ExpectSha256(sha256("cached", 6));
	auto pConsumer = std::make_shared<CCompletion>();
	pRequest->SetProgressCallback(pConsumer);
	m_Http.Run(pRequest);
	ASSERT_TRUE(m_Http.Pump());
	EXPECT_EQ(pRequest->State(), EHttpState::DONE);
	EXPECT_EQ(pRequest->m_ConfigureCalls, 0);
	EXPECT_EQ(pConsumer->m_Calls, 1);
	EXPECT_EQ(pConsumer->m_Observed, EHttpState::DONE);
	EXPECT_EQ(m_Http.Running(), 0u);
}

TEST_F(HttpScheduler, ExceptionAfterOpeningCandidateClosesAndRemovesIt)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	auto pRequest = std::make_shared<CPreparedRequest>();
	pRequest->WriteToFile(pStorage.get(), "download.bin", IStorage::TYPE_SAVE);
	pRequest->m_ThrowAfter = true;
	m_Http.Run(pRequest);
	EXPECT_NO_THROW(EXPECT_TRUE(m_Http.Pump()));
	EXPECT_EQ(pRequest->State(), EHttpState::ERROR);
	EXPECT_FALSE(fs_is_file(pRequest->TemporaryPath().c_str()));
	EXPECT_FALSE(m_Http.Tracked(pRequest.get()));
}

TEST_F(HttpScheduler, PerHostLimitDefersOnlyThatHostAndCancellationReleasesCapacity)
{
	std::vector<std::shared_ptr<CPreparedRequest>> vpRequests;
	for(int Index = 0; Index < 5; ++Index)
		vpRequests.push_back(Submit());
	auto pOther = Submit("https://other.test/body");
	ASSERT_TRUE(m_Http.Pump());
	EXPECT_EQ(m_Http.RunningForHost("scheduler.test"), 4u);
	EXPECT_EQ(pOther->State(), EHttpState::RUNNING);
	EXPECT_EQ(vpRequests.back()->State(), EHttpState::QUEUED);
	vpRequests.front()->Abort();
	m_Http.Cancel();
	EXPECT_EQ(vpRequests.front()->State(), EHttpState::ABORTED);
	EXPECT_EQ(m_Http.RunningForHost("scheduler.test"), 3u);
	ASSERT_TRUE(m_Http.Pump());
	EXPECT_EQ(vpRequests.back()->State(), EHttpState::RUNNING);
	EXPECT_EQ(m_Http.RunningForHost("scheduler.test"), 4u);
}

TEST_F(HttpScheduler, GlobalLimitDefersSeventeenthRequestAcrossDifferentHosts)
{
	std::vector<std::shared_ptr<CPreparedRequest>> vpRequests;
	for(int Index = 0; Index < 17; ++Index)
	{
		const std::string Url = "https://host" + std::to_string(Index) + ".test/body";
		vpRequests.push_back(Submit(Url.c_str()));
	}
	ASSERT_TRUE(m_Http.Pump());
	EXPECT_EQ(m_Http.Running(), 16u);
	EXPECT_EQ(vpRequests.back()->State(), EHttpState::QUEUED);
	EXPECT_EQ(vpRequests.back()->m_ConfigureCalls, 0);
	vpRequests.front()->Abort();
	m_Http.Cancel();
	ASSERT_TRUE(m_Http.Pump());
	EXPECT_EQ(vpRequests.back()->State(), EHttpState::RUNNING);
	EXPECT_EQ(m_Http.Running(), 16u);
}

TEST_F(HttpScheduler, DeferredRequestsRetainPriorityOverLaterSubmissions)
{
	std::vector<std::shared_ptr<CPreparedRequest>> vpRequests;
	for(int Index = 0; Index < 6; ++Index)
		vpRequests.push_back(Submit());
	ASSERT_TRUE(m_Http.Pump());
	auto pNewest = Submit();
	vpRequests.front()->Abort();
	m_Http.Cancel();
	ASSERT_TRUE(m_Http.Pump());
	EXPECT_EQ(vpRequests[4]->State(), EHttpState::RUNNING);
	const auto Pending = m_Http.Pending();
	ASSERT_EQ(Pending.size(), 2u);
	EXPECT_EQ(Pending[0], vpRequests[5]);
	EXPECT_EQ(Pending[1], pNewest);
}

TEST_F(HttpScheduler, CancelledDeferredRequestCompletesWhileHostRemainsFull)
{
	for(int Index = 0; Index < 4; ++Index)
		Submit();
	auto pDeferred = Submit();
	ASSERT_TRUE(m_Http.Pump());
	pDeferred->Abort();
	ASSERT_TRUE(m_Http.Pump());
	EXPECT_EQ(pDeferred->State(), EHttpState::ABORTED);
	EXPECT_EQ(pDeferred->m_ConfigureCalls, 0);
	EXPECT_TRUE(m_Http.Pending().empty());
	EXPECT_EQ(m_Http.Running(), 4u);
}

TEST_F(HttpScheduler, CompletedRequestCannotBeResubmittedOrNotifyTwice)
{
	auto pRequest = Submit();
	auto pConsumer = std::make_shared<CCompletion>();
	pRequest->SetProgressCallback(pConsumer);
	ASSERT_TRUE(m_Http.Pump());
	pRequest->Abort();
	m_Http.Cancel();
	m_Http.Run(pRequest);
	EXPECT_TRUE(m_Http.Pending().empty());
	EXPECT_EQ(pConsumer->m_Calls, 1);
	EXPECT_EQ(pRequest->m_ConfigureCalls, 1);
	EXPECT_EQ(pRequest->State(), EHttpState::ABORTED);
}

TEST_F(HttpScheduler, ShutdownCompletesRunningDeferredAndNewPendingRequests)
{
	std::vector<std::shared_ptr<CPreparedRequest>> vpRequests;
	for(int Index = 0; Index < 5; ++Index)
		vpRequests.push_back(Submit());
	ASSERT_TRUE(m_Http.Pump());
	vpRequests.push_back(Submit("https://another.test/body"));
	m_Http.Drain();
	for(const auto &pRequest : vpRequests)
		EXPECT_EQ(pRequest->State(), EHttpState::ABORTED);
	EXPECT_TRUE(m_Http.Pending().empty());
	EXPECT_EQ(m_Http.Running(), 0u);
}

TEST_F(HttpScheduler, FailedPreparationConsumerCanSubmitAnotherRequest)
{
	auto pFailed = std::make_shared<CPreparedRequest>();
	pFailed->m_ThrowBefore = true;
	auto pNext = std::make_shared<CPreparedRequest>();
	auto pConsumer = std::make_shared<CCompletion>();
	pConsumer->m_Action = [this, pNext] { m_Http.Run(pNext); };
	pFailed->SetProgressCallback(pConsumer);
	m_Http.Run(pFailed);
	ASSERT_TRUE(m_Http.Pump());
	EXPECT_EQ(pFailed->State(), EHttpState::ERROR);
	ASSERT_EQ(m_Http.Pending().size(), 1u);
	ASSERT_TRUE(m_Http.Pump());
	EXPECT_EQ(pNext->State(), EHttpState::RUNNING);
}

TEST_F(HttpScheduler, ConsumerExceptionDoesNotRepeatFailedPreparationOrStopFollowingRequest)
{
	auto pFailed = std::make_shared<CPreparedRequest>();
	pFailed->m_ThrowBefore = true;
	auto pConsumer = std::make_shared<CCompletion>();
	pConsumer->m_Throw = true;
	pFailed->SetProgressCallback(pConsumer);
	m_Http.Run(pFailed);
	auto pNext = Submit();
	ASSERT_TRUE(m_Http.Pump());
	EXPECT_EQ(pFailed->State(), EHttpState::ERROR);
	EXPECT_EQ(pConsumer->m_Calls, 1);
	EXPECT_EQ(pFailed->m_ConfigureCalls, 1);
	EXPECT_EQ(pNext->State(), EHttpState::RUNNING);
}

TEST(HttpSchedulerReentry, RejectedCompletionCanSubmitAnotherRequestWithoutHoldingQueueLock)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		CHttpCurlSchedulerTestPeer Http;
		ASSERT_TRUE(Http.Ready());
		Http.RejectNewRequests();
		auto pFirst = std::make_shared<CPreparedRequest>();
		auto pSecond = std::make_shared<CPreparedRequest>();
		auto pConsumer = std::make_shared<CCompletion>();
		pConsumer->m_Action = [&Http, pSecond] { Http.Run(pSecond); };
		pFirst->SetProgressCallback(pConsumer);
		Http.Run(pFirst);
		EXPECT_EQ(pFirst->State(), EHttpState::ABORTED);
		EXPECT_EQ(pSecond->State(), EHttpState::ABORTED);
		EXPECT_EQ(pConsumer->m_Calls, 1);
	}));
}

TEST(HttpSchedulerReentry, PendingShutdownCompletionCanSubmitAnotherRequest)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		CHttpCurlSchedulerTestPeer Http;
		ASSERT_TRUE(Http.Ready());
		auto pFirst = std::make_shared<CPreparedRequest>();
		auto pSecond = std::make_shared<CPreparedRequest>();
		auto pConsumer = std::make_shared<CCompletion>();
		pConsumer->m_Action = [&Http, pSecond] { Http.Run(pSecond); };
		pFirst->SetProgressCallback(pConsumer);
		Http.Run(pFirst);
		Http.Drain();
		EXPECT_EQ(pFirst->State(), EHttpState::ABORTED);
		EXPECT_EQ(pSecond->State(), EHttpState::ABORTED);
		EXPECT_EQ(pConsumer->m_Calls, 1);
		EXPECT_TRUE(Http.Pending().empty());
	}));
}

TEST(HttpSchedulerReentry, RunningShutdownCompletionCanSubmitAnotherRequest)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		CHttpCurlSchedulerTestPeer Http;
		ASSERT_TRUE(Http.Ready());
		auto pFirst = std::make_shared<CPreparedRequest>();
		auto pSecond = std::make_shared<CPreparedRequest>();
		auto pConsumer = std::make_shared<CCompletion>();
		pConsumer->m_Action = [&Http, pSecond] { Http.Run(pSecond); };
		pFirst->SetProgressCallback(pConsumer);
		Http.Run(pFirst);
		ASSERT_TRUE(Http.Pump());
		Http.Drain();
		EXPECT_EQ(pFirst->State(), EHttpState::ABORTED);
		EXPECT_EQ(pSecond->State(), EHttpState::ABORTED);
		EXPECT_EQ(Http.Running(), 0u);
		EXPECT_EQ(pConsumer->m_Calls, 1);
	}));
}
#endif
