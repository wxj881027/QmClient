#include <engine/shared/http_curl.h>

#include <gtest/gtest.h>

#if !defined(CONF_PLATFORM_EMSCRIPTEN)
#include <test/support/http_curl_scheduler_test_peer.h>

#include <cstdlib>
#include <memory>

namespace
{
	class CRequest : public CHttpRequestCurl
	{
	public:
		bool m_Fail = true;
		CRequest() :
			CHttpRequestCurl("https://body-allocation.test/request") { LogProgress(HTTPLOG::NONE); }

	private:
		void *AllocateRequestBody(size_t Size) noexcept override { return m_Fail ? nullptr : std::malloc(Size); }
	};
	class CCompletion : public IHttpRequest::IProgressCallback
	{
	public:
		int m_Calls = 0;
		EHttpState m_State = EHttpState::QUEUED;
		void OnProgress() override {}
		void OnCompletion(EHttpState State) override
		{
			++m_Calls;
			m_State = State;
		}
	};
}

TEST(HttpRequestBodyCurl, AllocationFailureCompletesAndReleasesSchedulerCapacity)
{
	CHttpCurlSchedulerTestPeer Http;
	ASSERT_TRUE(Http.Ready());
	auto pFailed = std::make_shared<CRequest>();
	pFailed->PostJson("payload");
	Http.Run(pFailed);
	ASSERT_TRUE(Http.Pump());
	EXPECT_TRUE(pFailed->Done());
	EXPECT_EQ(pFailed->State(), EHttpState::ABORTED);
	EXPECT_FALSE(Http.Tracked(pFailed.get()));
	EXPECT_EQ(Http.Running(), 0u);
	EXPECT_TRUE(Http.Pending().empty());
	auto pNext = std::make_shared<CRequest>();
	pNext->m_Fail = false;
	pNext->PostJson("payload");
	Http.Run(pNext);
	ASSERT_TRUE(Http.Pump());
	EXPECT_EQ(pNext->State(), EHttpState::RUNNING);
	EXPECT_EQ(Http.Running(), 1u);
}

TEST(HttpRequestBodyCurl, PreparationFailureCompletesConsumerOnceWithoutUserAbort)
{
	CCompletion Completion;
	CHttpCurlSchedulerTestPeer Http;
	ASSERT_TRUE(Http.Ready());
	auto pRequest = std::make_shared<CRequest>();
	pRequest->SetProgressCallback(&Completion);
	pRequest->PostJson("payload");
	Http.Run(pRequest);
	ASSERT_TRUE(Http.Pump());
	EXPECT_EQ(Completion.m_Calls, 1);
	EXPECT_EQ(Completion.m_State, EHttpState::ABORTED);
	EXPECT_FALSE(pRequest->IsAbortRequested());
	EXPECT_TRUE(pRequest->Done());
	ASSERT_TRUE(Http.Pump());
	EXPECT_EQ(Completion.m_Calls, 1);
}
#endif
