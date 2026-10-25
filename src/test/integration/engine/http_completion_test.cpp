#include <engine/shared/http_curl.h>

#include <gtest/gtest.h>

#if !defined(CONF_PLATFORM_EMSCRIPTEN)
#include <test/support/http_curl_test_peer.h>

#include <new>

namespace
{
	class CFailingCompletion : public IHttpRequest::IProgressCallback
	{
	public:
		int m_Calls = 0;
		EHttpState m_Observed = EHttpState::QUEUED;
		bool m_ThrowOnProgress = false;
		bool m_ThrowOnCompletion = true;
		void OnProgress() override
		{
			if(m_ThrowOnProgress)
				throw std::bad_alloc();
		}
		void OnCompletion(EHttpState State) override
		{
			++m_Calls;
			m_Observed = State;
			if(m_ThrowOnCompletion)
				throw std::bad_alloc();
		}
	};
}

TEST(HttpCompletionCurl, SuccessfulTransferConsumerFailureStillCompletesRequest)
{
	CFailingCompletion Consumer;
	CHttpRequestCurl Request("https://completion.test/body");
	Request.LogProgress(HTTPLOG::NONE);
	Request.SetProgressCallback(&Consumer);
	CHttpRequestCurlTestPeer::Header(Request, "HTTP/1.1 200 OK\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "\r\n");
	ASSERT_EQ(CHttpRequestCurlTestPeer::Body(Request, "body"), 4u);
	EXPECT_NO_THROW(CHttpRequestCurlTestPeer::Complete(Request));
	EXPECT_TRUE(Request.Done());
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
	EXPECT_EQ(Consumer.m_Observed, EHttpState::DONE);
	EXPECT_EQ(Consumer.m_Calls, 1);
}

TEST(HttpCompletionCurl, ConsumerFailureDoesNotReplaceUserCancellation)
{
	CFailingCompletion Consumer;
	CHttpRequestCurl Request("https://completion.test/body");
	Request.LogProgress(HTTPLOG::NONE);
	Request.SetProgressCallback(&Consumer);
	Request.Abort();
	EXPECT_NO_THROW(CHttpRequestCurlTestPeer::Complete(Request));
	EXPECT_EQ(Request.State(), EHttpState::ABORTED);
	EXPECT_EQ(Consumer.m_Observed, EHttpState::ABORTED);
	EXPECT_EQ(Consumer.m_Calls, 1);
}

TEST(HttpCompletionCurl, ProgressConsumerExceptionAbortsTransportAsLocalError)
{
	CFailingCompletion Consumer;
	Consumer.m_ThrowOnProgress = true;
	Consumer.m_ThrowOnCompletion = false;
	CHttpRequestCurl Request("https://completion.test/body");
	Request.LogProgress(HTTPLOG::NONE);
	Request.SetProgressCallback(&Consumer);
	EXPECT_NE(CHttpRequestCurlTestPeer::Progress(Request, 100, 20), 0);
	CHttpRequestCurlTestPeer::Complete(Request, nullptr, CURLE_ABORTED_BY_CALLBACK);
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
	EXPECT_EQ(Consumer.m_Observed, EHttpState::ERROR);
	EXPECT_FALSE(Request.IsAbortRequested());
}

TEST(HttpCompletionCurl, UserCancellationAfterProgressExceptionStillWins)
{
	CFailingCompletion Consumer;
	Consumer.m_ThrowOnProgress = true;
	Consumer.m_ThrowOnCompletion = false;
	CHttpRequestCurl Request("https://completion.test/body");
	Request.LogProgress(HTTPLOG::NONE);
	Request.SetProgressCallback(&Consumer);
	EXPECT_NE(CHttpRequestCurlTestPeer::Progress(Request, 100, 20), 0);
	Request.Abort();
	CHttpRequestCurlTestPeer::Complete(Request, nullptr, CURLE_ABORTED_BY_CALLBACK);
	EXPECT_EQ(Request.State(), EHttpState::ABORTED);
	EXPECT_EQ(Consumer.m_Observed, EHttpState::ABORTED);
}
#endif
