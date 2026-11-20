#include <engine/shared/http_curl.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>
#include <test/support/thread_creation_probe.h>

#include <chrono>
#include <memory>
#include <thread>

#if !defined(CONF_PLATFORM_EMSCRIPTEN)
class CHttpCurlStartupTestPeer
{
public:
	static bool RunWithInitHooks(CHttpCurl &Http, decltype(&curl_multi_init) pfnMultiInit, decltype(&curl_global_cleanup) pfnGlobalCleanup)
	{
		Http.m_pfnMultiInit = pfnMultiInit;
		Http.m_pfnGlobalCleanup = pfnGlobalCleanup;
		Http.RunLoop();
		return Http.m_State == CHttpCurl::ERROR;
	}
};

namespace
{
	struct CStartupResult
	{
		std::shared_ptr<CThreadCreationProbe::CCounts> m_pCounts = std::make_shared<CThreadCreationProbe::CCounts>();
		bool m_First = true;
		bool m_Retry = false;
		EHttpState m_RequestState = EHttpState::QUEUED;
	};
}

TEST(HttpStartup, ThreadFailureReturnsWithoutWaitingForInitialization)
{
	auto pResult = std::make_shared<CStartupResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		CThreadCreationProbe Probe(pResult->m_pCounts, 1);
		CHttpCurl Http(CThreadCreationProbe::Create);
		pResult->m_First = Http.Init(std::chrono::milliseconds(0));
		Http.Shutdown();
	}));
	EXPECT_FALSE(pResult->m_First);
	EXPECT_EQ(pResult->m_pCounts->m_Attempts.load(), 1);
	EXPECT_EQ(pResult->m_pCounts->m_Completed.load(), 0);
}

TEST(HttpStartup, MultiHandleCreationFailureReleasesGlobalInitialization)
{
	static thread_local bool MultiInitAttempted;
	static thread_local int CleanupCalls;
	MultiInitAttempted = false;
	CleanupCalls = 0;
	CHttpCurl Http;
	const bool Failed = CHttpCurlStartupTestPeer::RunWithInitHooks(Http, []() -> CURLM * {
		MultiInitAttempted = true;
		return nullptr; }, [] {
			++CleanupCalls;
			curl_global_cleanup(); });
	ASSERT_TRUE(MultiInitAttempted);
	// 回归时测试补做清理，避免影响后续用例。
	if(CleanupCalls == 0)
		curl_global_cleanup();

	EXPECT_EQ(CleanupCalls, 1);
	ASSERT_TRUE(Failed);
	auto pRequest = std::make_shared<CHttpRequestCurl>("startup-test://unused/");
	Http.Run(pRequest);
	EXPECT_EQ(pRequest->State(), EHttpState::ABORTED);
}

TEST(HttpStartup, RequestsAfterThreadFailureCompleteWithoutTransport)
{
	auto pResult = std::make_shared<CStartupResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		CThreadCreationProbe Probe(pResult->m_pCounts, 1);
		CHttpCurl Http(CThreadCreationProbe::Create);
		pResult->m_First = Http.Init(std::chrono::milliseconds(0));
		auto pRequest = std::make_shared<CHttpRequestCurl>("startup-test://unused/");
		Http.Run(pRequest);
		pResult->m_RequestState = pRequest->State();
	}));
	EXPECT_FALSE(pResult->m_First);
	EXPECT_EQ(pResult->m_RequestState, EHttpState::ABORTED);
}

TEST(HttpStartup, RequestRacingFailedInitializationDoesNotStayQueued)
{
	auto pResult = std::make_shared<CStartupResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		CThreadCreationProbe Probe(pResult->m_pCounts, 1);
		auto pHttp = std::make_shared<CHttpCurl>(CThreadCreationProbe::Create);
		auto pRequest = std::make_shared<CHttpRequestCurl>("startup-test://unused/");
		std::thread Submit([pHttp, pRequest] { pHttp->Run(pRequest); });
		pResult->m_First = pHttp->Init(std::chrono::milliseconds(0));
		Submit.join();
		pResult->m_RequestState = pRequest->State();
	}));
	EXPECT_FALSE(pResult->m_First);
	EXPECT_EQ(pResult->m_RequestState, EHttpState::ABORTED);
}

TEST(HttpStartup, CreationCanRecoverAfterAnEarlierFailure)
{
	auto pResult = std::make_shared<CStartupResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		CThreadCreationProbe Probe(pResult->m_pCounts, 1);
		CHttpCurl Http(CThreadCreationProbe::Create);
		pResult->m_First = Http.Init(std::chrono::milliseconds(0));
		pResult->m_Retry = Http.Init(std::chrono::milliseconds(0));
	}));
	EXPECT_FALSE(pResult->m_First);
	EXPECT_TRUE(pResult->m_Retry);
	EXPECT_EQ(pResult->m_pCounts->m_Attempts.load(), 2);
	EXPECT_EQ(pResult->m_pCounts->m_Completed.load(), 1);
}

TEST(HttpStartup, RepeatedInitDoesNotReplaceALiveThreadHandle)
{
	auto pResult = std::make_shared<CStartupResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		CThreadCreationProbe Probe(pResult->m_pCounts, 0);
		CHttpCurl Http(CThreadCreationProbe::Create);
		pResult->m_First = Http.Init(std::chrono::milliseconds(0));
		pResult->m_Retry = Http.Init(std::chrono::milliseconds(0));
	}));
	EXPECT_TRUE(pResult->m_First);
	EXPECT_TRUE(pResult->m_Retry);
	EXPECT_EQ(pResult->m_pCounts->m_Attempts.load(), 1);
	EXPECT_EQ(pResult->m_pCounts->m_Completed.load(), 1);
}
#endif
