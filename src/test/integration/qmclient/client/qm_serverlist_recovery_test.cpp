#include <base/detect.h>
#if !defined(CONF_PLATFORM_EMSCRIPTEN)
#include <base/hash.h>
#include <base/time.h>

#include <engine/client/serverbrowser_http.h>
#include <engine/client/serverbrowser_http_parse.h>
#include <engine/engine.h>
#include <engine/http.h>
#include <engine/shared/serverinfo.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/support/http_curl_test_peer.h>
#include <test/support/job_wait.h>
#include <test/test.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>

namespace
{
	class CResponse : public CHttpRequestCurl
	{
	public:
		explicit CResponse(const char *pUrl) : CHttpRequestCurl(pUrl) {}
		void Header(const char *) override {}
		void Reply(const std::string &Body, int Age = 0)
		{
			// 使用真实 curl 接收响应头和正文，不向结果字段伪造 Last-Modified。
			CHttpRequestCurlTestPeer::Header(*this, "HTTP/1.1 200 OK\r\n");
			CHttpRequestCurlTestPeer::Header(*this, "Age: " + std::to_string(Age) + "\r\n");
			CHttpRequestCurlTestPeer::Header(*this, "\r\n");
			CHttpRequestCurlTestPeer::Body(*this, Body);
			CHttpRequestCurlTestPeer::Complete(*this);
		}
		void Fail(int Status = 0, int RetryAfter = 0)
		{
			m_StatusCode = Status;
			if(RetryAfter)
				m_ResultRetryAfterSeconds = RetryAfter;
			IHttpRequest::OnCompletionInternal(EHttpState::ERROR);
		}
		void Abort() override
		{
			IHttpRequest::Abort();
			IHttpRequest::OnCompletionInternal(EHttpState::ABORTED);
		}
	};
	class CHttpBoundary : public IHttp
	{
	public:
		std::vector<std::shared_ptr<IHttpRequest>> m_vStarted;
		void Run(std::shared_ptr<IHttpRequest> pRequest) override { m_vStarted.push_back(std::move(pRequest)); }
		bool HasIpresolveBug() const override { return false; }
	};
	class CJobSubmissionBoundary : public IEngine
	{
	public:
		IEngine *m_pTarget = nullptr;
		int m_Submissions = 0;
		int m_RejectSubmission = 0;
		bool m_ThrowUnknown = false;
		std::shared_ptr<IJob> m_pLastAccepted;
		void Init() override { m_pTarget->Init(); }
		void ShutdownJobs() override { m_pTarget->ShutdownJobs(); }
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&pLogger) override { m_pTarget->SetAdditionalLogger(std::move(pLogger)); }
		void AddJob(std::shared_ptr<IJob> pJob) override
		{
			// 仅在入队前注入失败；已接受的任务仍交给真实线程池运行。
			if(++m_Submissions == m_RejectSubmission)
			{
				if(m_ThrowUnknown)
					throw 1;
				throw std::bad_alloc();
			}
			m_pTarget->AddJob(pJob);
			m_pLastAccepted = std::move(pJob);
		}
	};
	class CFailureBarrier
	{
		std::mutex m_Lock;
		std::condition_variable m_Changed;
		bool m_Entered = false;
		bool m_Released = false;

	public:
		void Pause()
		{
			std::unique_lock Lock(m_Lock);
			m_Entered = true;
			m_Changed.notify_all();
			if(!m_Changed.wait_for(Lock, std::chrono::seconds(5), [this] { return m_Released; }))
				throw std::runtime_error("failure barrier timed out");
		}
		bool WaitUntilEntered()
		{
			std::unique_lock Lock(m_Lock);
			return m_Changed.wait_for(Lock, std::chrono::seconds(2), [this] { return m_Entered; });
		}
		void Release()
		{
			std::lock_guard Lock(m_Lock);
			m_Released = true;
			m_Changed.notify_all();
		}
	};
	std::string List(const char *pName = "Valid")
	{
		return std::string(R"({"servers":[{"addresses":["tw-0.6+udp://127.0.0.1:8303"],"location":"eu","info":{"max_clients":16,"max_players":16,"passworded":false,"game_type":"DDRace","name":")") + pName + R"(","map":{"name":"Map"},"version":"0.6","clients":[]}}]})";
	}
	const char *OFFICIAL = "https://master1.ddnet.org/ddnet/15/servers.json";
	class QmServerListRecovery : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage = m_Info.CreateTestStorage();
		std::unique_ptr<IEngine> m_pEngine{CreateTestEngine("serverlist-test")};
		CJobSubmissionBoundary m_Jobs;
		CHttpBoundary m_Http;
		std::vector<std::shared_ptr<CResponse>> m_vRequests;
		std::unique_ptr<IServerBrowserHttp> m_pBrowser;
		int64_t m_Now = 100;
		void Begin(std::vector<std::string> vUrls = {"https://one.test/list", "https://two.test/list", "https://three.test/list"}, const char *pPrevious = "", bool Tiers = false, std::function<bool(const std::string &, const std::string &)> SaveWriter = {}, FServerListParser ParseList = {})
		{
			m_Jobs.m_pTarget = m_pEngine.get();
			m_pBrowser.reset(CreateServerBrowserHttpWithSources(&m_Jobs, m_pStorage.get(), &m_Http, std::move(vUrls), pPrevious, [this](const char *pUrl) { auto pResult = std::make_shared<CResponse>(pUrl); m_vRequests.push_back(pResult); return pResult; }, [this] { return m_Now; }, Tiers, {}, std::move(SaveWriter), std::move(ParseList)));
		}
		bool Until(const std::function<bool()> &Condition)
		{
			const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
			while(std::chrono::steady_clock::now() < Deadline)
			{
				m_pBrowser->Update();
				if(Condition())
					return true;
				std::this_thread::yield();
			}
			return false;
		}
		void TearDown() override
		{
			m_pBrowser.reset();
			m_pEngine->ShutdownJobs();
		}
	};
}

TEST_F(QmServerListRecovery, FirstValidGetPublishesWithoutWaitingForSlowSourceAndCancelsIt)
{
	Begin();
	ASSERT_EQ(m_vRequests.size(), 2U);
	m_vRequests[1]->Reply(List());
	ASSERT_TRUE(Until([this] { return m_pBrowser->NumServers() == 1; }));
	EXPECT_FALSE(m_pBrowser->IsRefreshing());
	EXPECT_TRUE(m_vRequests[0]->IsAbortRequested());
	EXPECT_EQ(m_vRequests.size(), 2U);
	EXPECT_EQ(m_pBrowser->Revision(), 1U);
	EXPECT_STREQ(m_pBrowser->Server(0).m_aName, "Valid");
}

TEST_F(QmServerListRecovery, BadJsonAndTimeoutAdvanceRemainingSources)
{
	Begin();
	m_vRequests[0]->Reply("<html>blocked</html>");
	m_vRequests[1]->Fail();
	ASSERT_TRUE(Until([this] { return m_vRequests.size() == 3; }));
	m_vRequests[2]->Reply(List("Recovered"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->NumServers() == 1; }));
	EXPECT_STREQ(m_pBrowser->Server(0).m_aName, "Recovered");
	EXPECT_FALSE(m_pBrowser->IsError());
}

TEST_F(QmServerListRecovery, AllFailuresBackOffThenAutomaticRecoveryPublishesNewRevision)
{
	Begin({"https://one.test/list"});
	m_vRequests[0]->Fail();
	m_pBrowser->Update();
	EXPECT_TRUE(m_pBrowser->IsError());
	m_Now = 104;
	m_pBrowser->Update();
	EXPECT_EQ(m_vRequests.size(), 1U);
	m_Now = 105;
	m_pBrowser->Update();
	ASSERT_EQ(m_vRequests.size(), 2U);
	m_vRequests[1]->Fail();
	m_pBrowser->Update();
	m_Now = 114;
	m_pBrowser->Update();
	EXPECT_EQ(m_vRequests.size(), 2U);
	m_Now = 115;
	m_pBrowser->Update();
	ASSERT_EQ(m_vRequests.size(), 3U);
	m_vRequests[2]->Reply(List());
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 1; }));
	EXPECT_FALSE(m_pBrowser->IsError());
}

TEST_F(QmServerListRecovery, ManualRefreshBypassesBackoffAndRepeatedClicksDoNotDuplicateRequests)
{
	Begin({"https://one.test/list"});
	m_pBrowser->Refresh();
	EXPECT_EQ(m_vRequests.size(), 1U);
	m_vRequests[0]->Fail();
	m_pBrowser->Update();
	m_pBrowser->Refresh();
	m_pBrowser->Refresh();
	EXPECT_EQ(m_vRequests.size(), 2U);
}

TEST_F(QmServerListRecovery, OfficialNetworkFailureRetriesWithEncryptedDns)
{
	Begin({OFFICIAL});
	ASSERT_EQ(m_vRequests.size(), 1U);
	EXPECT_FALSE(m_vRequests[0]->RevocationBestEffortEnabled());
	m_vRequests[0]->Fail();
	m_pBrowser->Update();
	ASSERT_EQ(m_vRequests.size(), 2U);
	EXPECT_TRUE(m_vRequests[1]->ServerListDnsFallbackEnabled());
	EXPECT_TRUE(m_vRequests[1]->RevocationBestEffortEnabled());
	EXPECT_STREQ(m_vRequests[1]->Url(), OFFICIAL);
	m_vRequests[1]->Reply(List());
	ASSERT_TRUE(Until([this] { return m_pBrowser->NumServers() == 1; }));
}

TEST_F(QmServerListRecovery, HttpRejectionDoesNotTriggerDnsRetryOrBypassRetryAfter)
{
	Begin({OFFICIAL});
	m_vRequests[0]->Fail(429, 120);
	m_pBrowser->Update();
	m_pBrowser->Refresh();
	EXPECT_EQ(m_vRequests.size(), 1U);
	m_pBrowser->Update();
	m_Now = 219;
	m_pBrowser->Update();
	EXPECT_EQ(m_vRequests.size(), 1U);
	m_Now = 220;
	m_pBrowser->Update();
	EXPECT_EQ(m_vRequests.size(), 2U);
}

TEST_F(QmServerListRecovery, CustomSourceNeverUsesOfficialDnsFallback)
{
	Begin({"https://custom.test/list"});
	m_vRequests[0]->Fail();
	m_pBrowser->Update();
	EXPECT_EQ(m_vRequests.size(), 1U);
	EXPECT_TRUE(m_pBrowser->IsError());
	EXPECT_FALSE(m_vRequests[0]->ServerListDnsFallbackEnabled());
	EXPECT_FALSE(m_vRequests[0]->RevocationBestEffortEnabled());
}

TEST_F(QmServerListRecovery, ShutdownCancelsOutstandingAndPreventsAutomaticOrManualRestart)
{
	Begin();
	m_pBrowser->Shutdown();
	m_Now += 1000;
	m_pBrowser->Update();
	m_pBrowser->Refresh();
	EXPECT_EQ(m_vRequests.size(), 2U);
	EXPECT_TRUE(m_vRequests[0]->IsAbortRequested());
	EXPECT_TRUE(m_vRequests[1]->IsAbortRequested());
}

TEST_F(QmServerListRecovery, StaleFirstResponseDoesNotCancelFreshSource)
{
	Begin();
	m_vRequests[0]->Reply(List("Old"), 600);
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 1; }));
	EXPECT_TRUE(m_pBrowser->IsStale());
	EXPECT_FALSE(m_vRequests[1]->IsAbortRequested());
	m_vRequests[1]->Reply(List("Fresh"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 2; }));
	EXPECT_FALSE(m_pBrowser->IsStale());
	EXPECT_STREQ(m_pBrowser->Server(0).m_aName, "Fresh");
}

TEST_F(QmServerListRecovery, RestartLoadsBoundedPersistentListAndDoesNotReuseForDifferentSources)
{
	Begin({"https://one.test/list"});
	m_vRequests[0]->Reply(List("Saved"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->NumServers() == 1; }));
	m_pBrowser.reset();
	m_pEngine->ShutdownJobs();
	m_pEngine.reset(CreateTestEngine("serverlist-test-restart"));
	Begin({"https://one.test/list"});
	EXPECT_EQ(m_pBrowser->NumServers(), 1);
	EXPECT_TRUE(m_pBrowser->IsStale());
	EXPECT_STREQ(m_pBrowser->Server(0).m_aName, "Saved");
	m_pBrowser.reset();
	Begin({"https://different.test/list"});
	EXPECT_EQ(m_pBrowser->NumServers(), 0);
}

TEST_F(QmServerListRecovery, RateLimitedSourceDoesNotDelayRecoveryOfAnotherSource)
{
	Begin({"https://one.test/list", "https://two.test/list"});
	m_vRequests[0]->Fail(429, 120);
	m_vRequests[1]->Fail();
	m_pBrowser->Update();
	m_Now += 5;
	m_pBrowser->Update();
	ASSERT_EQ(m_vRequests.size(), 3U);
	EXPECT_STREQ(m_vRequests[2]->Url(), "https://two.test/list");
	m_vRequests[2]->Reply(List());
	ASSERT_TRUE(Until([this] { return m_pBrowser->NumServers() == 1; }));
}

TEST_F(QmServerListRecovery, FailedRefreshRetainsLastSuccessfulListAndRevision)
{
	Begin({"https://one.test/list"});
	m_vRequests[0]->Reply(List("Previous"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 1; }));
	m_pBrowser->Refresh();
	m_vRequests[1]->Fail();
	m_pBrowser->Update();
	EXPECT_TRUE(m_pBrowser->IsError());
	EXPECT_EQ(m_pBrowser->Revision(), 1U);
	EXPECT_EQ(m_pBrowser->NumServers(), 1);
	EXPECT_STREQ(m_pBrowser->Server(0).m_aName, "Previous");
}

TEST_F(QmServerListRecovery, OfficialThenDnsRecoveryThenMirrorAndFinallyDatedSnapshot)
{
	const char *pSnapshot = "https://raw.githubusercontent.com/wxj881027/QmClient/serverlist-snapshot/servers.json";
	Begin({OFFICIAL, "https://ddrace.cn/ddnet/15/servers.json", pSnapshot}, "", true);
	ASSERT_EQ(m_vRequests.size(), 1U);
	m_vRequests[0]->Fail();
	m_pBrowser->Update();
	ASSERT_EQ(m_vRequests.size(), 2U);
	EXPECT_TRUE(m_vRequests[1]->ServerListDnsFallbackEnabled());
	m_vRequests[1]->Fail();
	m_pBrowser->Update();
	ASSERT_EQ(m_vRequests.size(), 3U);
	EXPECT_STREQ(m_vRequests[2]->Url(), "https://ddrace.cn/ddnet/15/servers.json");
	m_vRequests[2]->Fail(503);
	m_pBrowser->Update();
	ASSERT_EQ(m_vRequests.size(), 4U);
	EXPECT_STREQ(m_vRequests[3]->Url(), pSnapshot);
	std::string Body = List("Snapshot");
	Body.insert(1, "\"qm_snapshot_updated_at\":" + std::to_string(time_timestamp() - 600) + ",");
	m_vRequests[3]->Reply(Body);
	ASSERT_TRUE(Until([this] { return m_pBrowser->NumServers() == 1; }));
	EXPECT_STREQ(m_pBrowser->Server(0).m_aName, "Snapshot");
	EXPECT_TRUE(m_pBrowser->IsStale());
}

TEST_F(QmServerListRecovery, SnapshotWithoutCollectionTimestampIsRejected)
{
	Begin({"https://raw.githubusercontent.com/wxj881027/QmClient/serverlist-snapshot/servers.json"}, "", true);
	m_pBrowser->Update();
	ASSERT_EQ(m_vRequests.size(), 1U);
	m_vRequests[0]->Reply(List());
	ASSERT_TRUE(Until([this] { return m_pBrowser->IsError(); }));
	EXPECT_EQ(m_pBrowser->NumServers(), 0);
}

TEST_F(QmServerListRecovery, CorruptFutureAndExpiredPersistentCachesAreIgnored)
{
	m_pStorage->CreateFolder("cache", IStorage::TYPE_SAVE);
	const std::string SourceKey = "https://one.test/list\n";
	char aHash[SHA256_MAXSTRSIZE];
	sha256_str(sha256(SourceKey.data(), SourceKey.size()), aHash, sizeof(aHash));
	for(const int64_t Received : {time_timestamp() + 1000, time_timestamp() - 86401, INT64_MIN})
	{
		SCOPED_TRACE(Received);
		const std::string Text = "QMSL1 " + std::to_string(Received) + " 0 " + aHash + "\n" + List();
		IOHANDLE File = m_pStorage->OpenFile("cache/qm-serverlist.json", IOFLAG_WRITE, IStorage::TYPE_SAVE);
		ASSERT_NE(File, nullptr);
		io_write(File, Text.data(), Text.size());
		io_close(File);
		Begin({"https://one.test/list"});
		EXPECT_EQ(m_pBrowser->NumServers(), 0);
		m_pBrowser.reset();
	}
	const std::string Corrupt = "QMSL1 " + std::to_string(time_timestamp()) + " 0 " + aHash + "\n<html>not JSON</html>";
	IOHANDLE File = m_pStorage->OpenFile("cache/qm-serverlist.json", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	io_write(File, Corrupt.data(), Corrupt.size());
	io_close(File);
	Begin({"https://one.test/list"});
	EXPECT_EQ(m_pBrowser->NumServers(), 0);
}

TEST_F(QmServerListRecovery, UserConfigurationReplacesDefaultsAndDeduplicatesUrls)
{
	const char *pText = "https://custom.test/list\nhttps://custom.test/list\nhttps://other.test/list\n";
	IOHANDLE File = m_pStorage->OpenFile("ddnet-serverlist-urls.cfg", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	io_write(File, pText, str_length(pText));
	io_close(File);
	m_pBrowser.reset(CreateServerBrowserHttp(m_pEngine.get(), m_pStorage.get(), &m_Http, ""));
	ASSERT_EQ(m_Http.m_vStarted.size(), 2U);
	EXPECT_STREQ(m_Http.m_vStarted[0]->Url(), "https://custom.test/list");
	EXPECT_STREQ(m_Http.m_vStarted[1]->Url(), "https://other.test/list");
	m_pBrowser->Refresh();
	EXPECT_EQ(m_Http.m_vStarted.size(), 2U);
}

TEST_F(QmServerListRecovery, DefaultFactoryStartsTwoOfficialSourcesBeforeMirror)
{
	m_pBrowser.reset(CreateServerBrowserHttp(m_pEngine.get(), m_pStorage.get(), &m_Http, "https://ddrace.cn/ddnet/15/servers.json"));
	ASSERT_EQ(m_Http.m_vStarted.size(), 2U);
	EXPECT_STREQ(m_Http.m_vStarted[0]->Url(), "https://master1.ddnet.org/ddnet/15/servers.json");
	EXPECT_STREQ(m_Http.m_vStarted[1]->Url(), "https://master2.ddnet.org/ddnet/15/servers.json");
}

TEST_F(QmServerListRecovery, DestructionCancelsRequestsWithoutRequiringExplicitShutdown)
{
	Begin();
	m_pBrowser.reset();
	EXPECT_TRUE(m_vRequests[0]->IsAbortRequested());
	EXPECT_TRUE(m_vRequests[1]->IsAbortRequested());
	EXPECT_EQ(m_vRequests.size(), 2U);
}

TEST_F(QmServerListRecovery, OfficialTierBudgetCancelsHungRequestsAndStartsMirror)
{
	Begin({OFFICIAL, "https://master2.ddnet.org/ddnet/15/servers.json", "https://ddrace.cn/ddnet/15/servers.json"}, "", true);
	ASSERT_EQ(m_vRequests.size(), 2U);
	m_Now += 29;
	m_pBrowser->Update();
	EXPECT_EQ(m_vRequests.size(), 2U);
	m_Now += 1;
	m_pBrowser->Update();
	ASSERT_EQ(m_vRequests.size(), 3U);
	EXPECT_TRUE(m_vRequests[0]->IsAbortRequested());
	EXPECT_TRUE(m_vRequests[1]->IsAbortRequested());
	EXPECT_STREQ(m_vRequests[2]->Url(), "https://ddrace.cn/ddnet/15/servers.json");
	m_vRequests[0]->Reply(List("Late cancelled response"));
	m_vRequests[2]->Reply(List("Mirror"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->NumServers() == 1; }));
	EXPECT_STREQ(m_pBrowser->Server(0).m_aName, "Mirror");
}

TEST_F(QmServerListRecovery, EachFallbackTierHasItsOwnBudgetAndExhaustionSchedulesRetry)
{
	Begin({OFFICIAL, "https://ddrace.cn/ddnet/15/servers.json", "https://raw.githubusercontent.com/wxj881027/QmClient/serverlist-snapshot/servers.json"}, "", true);
	m_Now += 30;
	m_pBrowser->Update();
	ASSERT_EQ(m_vRequests.size(), 2U);
	m_Now += 29;
	m_pBrowser->Update();
	EXPECT_EQ(m_vRequests.size(), 2U);
	m_Now += 1;
	m_pBrowser->Update();
	ASSERT_EQ(m_vRequests.size(), 3U);
	m_Now += 30;
	m_pBrowser->Update();
	EXPECT_TRUE(m_pBrowser->IsError());
	EXPECT_FALSE(m_pBrowser->IsRefreshing());
	EXPECT_TRUE(m_vRequests[2]->IsAbortRequested());
	m_Now += 5;
	m_pBrowser->Update();
	EXPECT_EQ(m_vRequests.size(), 4U);
}

TEST_F(QmServerListRecovery, CustomTierBudgetIsBoundedWithoutAddingDefaultSources)
{
	Begin({"https://custom.test/list"});
	m_Now += 119;
	m_pBrowser->Update();
	EXPECT_FALSE(m_vRequests[0]->IsAbortRequested());
	m_Now += 1;
	m_pBrowser->Update();
	EXPECT_TRUE(m_vRequests[0]->IsAbortRequested());
	EXPECT_TRUE(m_pBrowser->IsError());
	EXPECT_EQ(m_vRequests.size(), 1U);
}

TEST_F(QmServerListRecovery, MirrorAgeHeaderWithoutLastModifiedIsAcceptedAsFresh)
{
	Begin({"https://ddrace.cn/ddnet/15/servers.json"});
	m_vRequests[0]->Reply(List("Fresh mirror"), 20);
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 1; }));
	EXPECT_FALSE(m_pBrowser->IsStale());
	EXPECT_FALSE(m_pBrowser->IsRefreshing());
	m_Now += 61;
	m_pBrowser->Update();
	EXPECT_EQ(m_vRequests.size(), 1U);
}

TEST_F(QmServerListRecovery, KnownResponseOlderThanOneDayDoesNotBecomeFiveMinuteFallback)
{
	Begin({"https://one.test/list"});
	m_vRequests[0]->Reply(List("Expired"), 90000);
	ASSERT_TRUE(Until([this] { return !m_pBrowser->IsRefreshing(); }));
	EXPECT_EQ(m_pBrowser->NumServers(), 0);
	EXPECT_EQ(m_pBrowser->Revision(), 0U);
	EXPECT_TRUE(m_pBrowser->IsError());
}

TEST_F(QmServerListRecovery, OlderRefreshDoesNotReplaceFresherExistingCache)
{
	Begin({"https://one.test/list"});
	m_vRequests[0]->Reply(List("Recent"), 5);
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 1; }));
	m_Now += 20;
	m_pBrowser->Refresh();
	m_vRequests[1]->Reply(List("Older"), 600);
	ASSERT_TRUE(Until([this] { return !m_pBrowser->IsRefreshing(); }));
	EXPECT_EQ(m_pBrowser->Revision(), 1U);
	EXPECT_STREQ(m_pBrowser->Server(0).m_aName, "Recent");
}

namespace
{
	struct CSaveBoundary
	{
		std::mutex m_Lock;
		std::condition_variable m_Changed;
		std::vector<std::string> m_vBodies;
		bool m_FirstStarted = false;
		bool m_Release = false;
		bool Write(const std::string &Body)
		{
			std::unique_lock Lock(m_Lock);
			if(m_vBodies.empty())
			{
				m_FirstStarted = true;
				m_Changed.notify_all();
				if(!m_Changed.wait_for(Lock, std::chrono::seconds(3), [this] { return m_Release; }))
					return false;
			}
			m_vBodies.push_back(Body);
			return true;
		}
	};
}

TEST_F(QmServerListRecovery, ShutdownDrainsLatestCoalescedSaveAfterBrowserDestruction)
{
	auto pWrites = std::make_shared<CSaveBoundary>();
	Begin({"https://one.test/list"}, "", false, [pWrites](const std::string &Path, const std::string &Body) {
		if(!pWrites->Write(Body))
			return false;
		IOHANDLE File = io_open(Path.c_str(), IOFLAG_WRITE);
		if(!File)
			return false;
		const bool Written = io_write(File, Body.data(), Body.size()) == Body.size();
		return io_close(File) == 0 && Written;
	});
	m_vRequests[0]->Reply(List("First"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 1; }));
	{
		std::unique_lock Lock(pWrites->m_Lock);
		ASSERT_TRUE(pWrites->m_Changed.wait_for(Lock, std::chrono::seconds(2), [pWrites] { return pWrites->m_FirstStarted; }));
	}
	m_pBrowser->Refresh();
	m_vRequests[1]->Reply(List("Intermediate"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 2; }));
	m_pBrowser->Refresh();
	m_vRequests[2]->Reply(List("Latest"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 3; }));
	m_pBrowser->Shutdown();
	m_pBrowser->Shutdown();
	m_pBrowser.reset();
	{
		std::lock_guard Lock(pWrites->m_Lock);
		pWrites->m_Release = true;
	}
	pWrites->m_Changed.notify_all();
	m_pEngine->ShutdownJobs();
	m_pEngine.reset(CreateTestEngine("serverlist-test-after-drain"));
	ASSERT_EQ(pWrites->m_vBodies.size(), 2U);
	EXPECT_NE(pWrites->m_vBodies[0].find("First"), std::string::npos);
	EXPECT_NE(pWrites->m_vBodies[1].find("Latest"), std::string::npos);
	EXPECT_EQ(pWrites->m_vBodies[1].find("Intermediate"), std::string::npos);
	void *pSaved;
	unsigned Size;
	ASSERT_TRUE(m_pStorage->ReadFile("cache/qm-serverlist.json", IStorage::TYPE_SAVE, &pSaved, &Size));
	const std::string Saved(static_cast<char *>(pSaved), Size);
	free(pSaved);
	EXPECT_NE(Saved.find("Latest"), std::string::npos);
	EXPECT_EQ(Saved.find("First"), std::string::npos);
	Begin({"https://one.test/list"});
	ASSERT_EQ(m_pBrowser->NumServers(), 1);
	EXPECT_STREQ(m_pBrowser->Server(0).m_aName, "Latest");
}

TEST_F(QmServerListRecovery, SavedCacheKeepsRealAgeForComparisonWhileAwaitingLiveRefresh)
{
	Begin({"https://one.test/list"});
	m_vRequests[0]->Reply(List("Recent persisted"), 5);
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 1; }));
	m_pBrowser.reset();
	m_pEngine->ShutdownJobs();
	m_pEngine.reset(CreateTestEngine("serverlist-test-restart-age"));
	Begin({"https://one.test/list"});
	ASSERT_EQ(m_pBrowser->NumServers(), 1);
	EXPECT_TRUE(m_pBrowser->IsStale());
	const auto Revision = m_pBrowser->Revision();
	m_vRequests.back()->Reply(List("Older fetched"), 100);
	ASSERT_TRUE(Until([this] { return !m_pBrowser->IsRefreshing(); }));
	EXPECT_EQ(m_pBrowser->Revision(), Revision);
	EXPECT_STREQ(m_pBrowser->Server(0).m_aName, "Recent persisted");
}

TEST_F(QmServerListRecovery, ParserAllocationExceptionRetainsCacheAndRecoversAfterBackoff)
{
	auto pFail = std::make_shared<std::atomic<bool>>(false);
	Begin({"https://one.test/list"}, "", false, {}, [pFail](json_value *pJson, std::vector<CServerInfo> *pvServers) {
		const bool Failed = ServerBrowserParseHttpList(pJson, pvServers);
		if(pFail->load())
			throw std::bad_alloc();
		return Failed;
	});
	m_vRequests[0]->Reply(List("Previous"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 1; }));
	pFail->store(true);
	m_pBrowser->Refresh();
	m_vRequests[1]->Reply(List("Unpublished"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->IsError(); }));
	EXPECT_EQ(m_pBrowser->Revision(), 1U);
	EXPECT_EQ(m_pBrowser->NumServers(), 1);
	EXPECT_STREQ(m_pBrowser->Server(0).m_aName, "Previous");
	m_Now += 4;
	m_pBrowser->Update();
	EXPECT_EQ(m_vRequests.size(), 2U);
	pFail->store(false);
	++m_Now;
	m_pBrowser->Update();
	ASSERT_EQ(m_vRequests.size(), 3U);
	m_vRequests[2]->Reply(List("Recovered"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 2; }));
	EXPECT_FALSE(m_pBrowser->IsError());
	EXPECT_STREQ(m_pBrowser->Server(0).m_aName, "Recovered");
}

TEST_F(QmServerListRecovery, UnknownParserExceptionAllowsRemainingSourceToPublish)
{
	auto pCalls = std::make_shared<std::atomic<int>>(0);
	Begin({"https://one.test/list", "https://two.test/list"}, "", false, {}, [pCalls](json_value *pJson, std::vector<CServerInfo> *pvServers) {
		const bool Failed = ServerBrowserParseHttpList(pJson, pvServers);
		if(pCalls->fetch_add(1) == 0)
			throw 1;
		return Failed;
	});
	m_vRequests[0]->Reply(List("Unpublished"));
	m_pBrowser->Update();
	ASSERT_NE(m_Jobs.m_pLastAccepted, nullptr);
	ASSERT_TRUE(WaitForJobDone(*m_Jobs.m_pLastAccepted));
	m_pBrowser->Update();
	EXPECT_EQ(m_pBrowser->Revision(), 0U);
	EXPECT_TRUE(m_pBrowser->IsRefreshing());
	m_vRequests[1]->Reply(List("Second source"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 1; }));
	EXPECT_STREQ(m_pBrowser->Server(0).m_aName, "Second source");
}

TEST_F(QmServerListRecovery, ParserExceptionAfterBrowserDestructionDoesNotPoisonWorkerPool)
{
	auto pBarrier = std::make_shared<CFailureBarrier>();
	Begin({"https://one.test/list"}, "", false, {}, [pBarrier](json_value *pJson, std::vector<CServerInfo> *pvServers) -> bool {
		ServerBrowserParseHttpList(pJson, pvServers);
		pBarrier->Pause();
		throw std::bad_alloc();
	});
	m_vRequests[0]->Reply(List("Cancelled"));
	m_pBrowser->Update();
	ASSERT_TRUE(pBarrier->WaitUntilEntered());
	const auto pCancelledJob = m_Jobs.m_pLastAccepted;
	m_pBrowser->Shutdown();
	m_pBrowser.reset();
	pBarrier->Release();
	ASSERT_TRUE(WaitForJobDone(*pCancelledJob));
	Begin({"https://one.test/list"});
	EXPECT_EQ(m_pBrowser->Revision(), 0U);
	m_vRequests.back()->Reply(List("New browser"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 1; }));
	EXPECT_STREQ(m_pBrowser->Server(0).m_aName, "New browser");
}

TEST_F(QmServerListRecovery, ThrowingWriterPreservesPublishedListAndLaterSaveSucceeds)
{
	struct CWrites
	{
		int m_Calls = 0;
		std::string m_Body;
	};
	auto pWrites = std::make_shared<CWrites>();
	Begin({"https://one.test/list"}, "", false, [pWrites](const std::string &, const std::string &Body) {
		if(++pWrites->m_Calls == 1)
			throw std::bad_alloc();
		pWrites->m_Body = Body;
		return true;
	});
	m_vRequests[0]->Reply(List("First"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 1; }));
	ASSERT_TRUE(WaitForJobDone(*m_Jobs.m_pLastAccepted));
	EXPECT_EQ(pWrites->m_Calls, 1);
	EXPECT_FALSE(m_pBrowser->IsError());
	EXPECT_STREQ(m_pBrowser->Server(0).m_aName, "First");
	m_pBrowser->Refresh();
	m_vRequests[1]->Reply(List("Latest"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 2; }));
	ASSERT_TRUE(WaitForJobDone(*m_Jobs.m_pLastAccepted));
	EXPECT_EQ(pWrites->m_Calls, 2);
	EXPECT_NE(pWrites->m_Body.find("Latest"), std::string::npos);
}

TEST_F(QmServerListRecovery, UnknownWriterExceptionStillDrainsNewestSaveAfterDestruction)
{
	struct CWrites
	{
		CFailureBarrier m_Barrier;
		int m_Calls = 0;
		std::string m_Body;
		std::string m_Path;
	};
	auto pWrites = std::make_shared<CWrites>();
	Begin({"https://one.test/list"}, "", false, [pWrites](const std::string &Path, const std::string &Body) {
		if(++pWrites->m_Calls == 1)
		{
			pWrites->m_Barrier.Pause();
			throw 1;
		}
		pWrites->m_Body = Body;
		pWrites->m_Path = Path;
		return true;
	});
	m_vRequests[0]->Reply(List("First"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 1; }));
	ASSERT_TRUE(pWrites->m_Barrier.WaitUntilEntered());
	const auto pSaveJob = m_Jobs.m_pLastAccepted;
	m_pBrowser->Refresh();
	m_vRequests[1]->Reply(List("Intermediate"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 2; }));
	m_pBrowser->Refresh();
	m_vRequests[2]->Reply(List("Latest"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 3; }));
	m_pBrowser.reset();
	pWrites->m_Barrier.Release();
	ASSERT_TRUE(WaitForJobDone(*pSaveJob));
	EXPECT_EQ(pWrites->m_Calls, 2);
	EXPECT_FALSE(pWrites->m_Path.empty());
	EXPECT_NE(pWrites->m_Body.find("Latest"), std::string::npos);
	EXPECT_EQ(pWrites->m_Body.find("Intermediate"), std::string::npos);
	EXPECT_EQ(pWrites->m_Body.find("First"), std::string::npos);
}

TEST_F(QmServerListRecovery, ParseSubmissionFailureIsRetriedWithoutLosingRefreshState)
{
	m_Jobs.m_RejectSubmission = 1;
	Begin({"https://one.test/list"});
	m_vRequests[0]->Reply(List("Unpublished"));
	EXPECT_NO_THROW(m_pBrowser->Update());
	EXPECT_EQ(m_pBrowser->Revision(), 0U);
	EXPECT_TRUE(m_pBrowser->IsError());
	EXPECT_FALSE(m_pBrowser->IsRefreshing());
	m_Now += 5;
	m_pBrowser->Update();
	ASSERT_EQ(m_vRequests.size(), 2U);
	m_vRequests[1]->Reply(List("Recovered"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 1; }));
	EXPECT_FALSE(m_pBrowser->IsError());
	EXPECT_STREQ(m_pBrowser->Server(0).m_aName, "Recovered");
}

TEST_F(QmServerListRecovery, UnknownParseSubmissionFailureAllowsOtherSourceToFinish)
{
	m_Jobs.m_RejectSubmission = 1;
	m_Jobs.m_ThrowUnknown = true;
	Begin({"https://one.test/list", "https://two.test/list"});
	m_vRequests[0]->Reply(List("Unpublished"));
	EXPECT_NO_THROW(m_pBrowser->Update());
	EXPECT_TRUE(m_pBrowser->IsRefreshing());
	m_vRequests[1]->Reply(List("Second source"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 1; }));
	EXPECT_STREQ(m_pBrowser->Server(0).m_aName, "Second source");
}

TEST_F(QmServerListRecovery, SaveSubmissionFailureDoesNotLatchWorkerActive)
{
	auto pWrites = std::make_shared<std::atomic<int>>(0);
	m_Jobs.m_RejectSubmission = 2;
	Begin({"https://one.test/list"}, "", false, [pWrites](const std::string &, const std::string &) {
		pWrites->fetch_add(1);
		return true;
	});
	m_vRequests[0]->Reply(List("First"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 1; }));
	EXPECT_EQ(m_Jobs.m_Submissions, 2);
	EXPECT_EQ(pWrites->load(), 0);
	EXPECT_FALSE(m_pBrowser->IsError());
	EXPECT_STREQ(m_pBrowser->Server(0).m_aName, "First");
	m_pBrowser->Refresh();
	m_vRequests[1]->Reply(List("Latest"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 2; }));
	ASSERT_TRUE(WaitForJobDone(*m_Jobs.m_pLastAccepted));
	EXPECT_EQ(m_Jobs.m_Submissions, 4);
	EXPECT_EQ(pWrites->load(), 1);
}

TEST_F(QmServerListRecovery, CacheParserExceptionStillStartsLiveRefresh)
{
	Begin({"https://one.test/list"});
	m_vRequests[0]->Reply(List("Saved"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 1; }));
	ASSERT_TRUE(WaitForJobDone(*m_Jobs.m_pLastAccepted));
	m_pBrowser.reset();
	auto pCalls = std::make_shared<std::atomic<int>>(0);
	Begin({"https://one.test/list"}, "", false, {}, [pCalls](json_value *pJson, std::vector<CServerInfo> *pvServers) {
		const bool Failed = ServerBrowserParseHttpList(pJson, pvServers);
		if(pCalls->fetch_add(1) == 0)
			throw std::bad_alloc();
		return Failed;
	});
	EXPECT_EQ(pCalls->load(), 1);
	EXPECT_EQ(m_pBrowser->NumServers(), 0);
	EXPECT_EQ(m_pBrowser->Revision(), 0U);
	EXPECT_TRUE(m_pBrowser->IsRefreshing());
	ASSERT_EQ(m_vRequests.size(), 2U);
	m_vRequests[1]->Reply(List("Live"));
	ASSERT_TRUE(Until([this] { return m_pBrowser->Revision() == 1; }));
	EXPECT_STREQ(m_pBrowser->Server(0).m_aName, "Live");
}

#endif
