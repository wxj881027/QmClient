#include <engine/shared/http_curl.h>

#include <gtest/gtest.h>

#if !defined(CONF_PLATFORM_EMSCRIPTEN)
#include <base/fs.h>

#include <engine/storage.h>

#include <test/support/http_curl_scheduler_test_peer.h>
#include <test/support/http_curl_test_peer.h>
#include <test/test.h>

#include <cstdlib>
#include <memory>
#include <string>

namespace
{
	class CHeaderRequest : public CHttpRequestCurl
	{
	public:
		int m_AppendCalls = 0;
		int m_FailAt = 0;
		int m_CloseCalls = 0;
		CHeaderRequest() :
			CHttpRequestCurl("https://headers.test/request") { LogProgress(HTTPLOG::NONE); }
		std::string TemporaryPath() const { return m_aDestAbsoluteTmp; }

	private:
		curl_slist *AppendRequestHeader(curl_slist *pHeaders, const char *pHeader) noexcept override
		{
			++m_AppendCalls;
			return m_AppendCalls == m_FailAt ? nullptr : CHttpRequestCurl::AppendRequestHeader(pHeaders, pHeader);
		}
		int CloseResponseFile(IOHANDLE File) noexcept override
		{
			++m_CloseCalls;
			return IHttpRequest::CloseResponseFile(File);
		}
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

	class HttpRequestHeadersStorage : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage = m_Info.CreateTestStorage();
		void SetUp() override { ASSERT_NE(m_pStorage, nullptr); }
		void WriteOldFile()
		{
			IOHANDLE File = m_pStorage->OpenFile("headers.bin", IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, "old", 3), 3u);
			EXPECT_EQ(io_close(File), 0);
		}
		std::string ReadOldFile()
		{
			char *pText = m_pStorage->ReadFileStr("headers.bin", IStorage::TYPE_SAVE);
			if(!pText)
				return {};
			std::string Text(pText);
			std::free(pText);
			return Text;
		}
	};
}

TEST(HttpRequestHeadersCurl, LaterAppendFailurePreservesOwnedNodesAndStopsFurtherAppend)
{
	CHeaderRequest Request;
	Request.Header("Accept: application/json");
	Request.Header("X-Request: one");
	const auto *pOriginal = CHttpRequestCurlTestPeer::RequestHeaders(Request);
	ASSERT_NE(pOriginal, nullptr);
	ASSERT_NE(pOriginal->next, nullptr);
	Request.m_FailAt = 3;
	Request.Header("Authorization: value");
	EXPECT_EQ(CHttpRequestCurlTestPeer::RequestHeaders(Request), pOriginal);
	EXPECT_STREQ(pOriginal->data, "Accept: application/json");
	EXPECT_STREQ(pOriginal->next->data, "X-Request: one");
	EXPECT_EQ(pOriginal->next->next, nullptr);
	Request.m_FailAt = 0;
	Request.Header("X-Late: ignored");
	EXPECT_EQ(Request.m_AppendCalls, 3);
	EXPECT_EQ(CHttpRequestCurlTestPeer::RequestHeaders(Request), pOriginal);
}

TEST(HttpRequestHeadersCurl, InitialAppendFailureCompletesOnceWithoutStartingTransfer)
{
	CCompletion Completion;
	CHttpCurlSchedulerTestPeer Http;
	ASSERT_TRUE(Http.Ready());
	auto pRequest = std::make_shared<CHeaderRequest>();
	pRequest->SetProgressCallback(&Completion);
	pRequest->m_FailAt = 1;
	pRequest->Header("Accept: application/json");
	Http.Run(pRequest);
	ASSERT_TRUE(Http.Pump());
	EXPECT_EQ(pRequest->State(), EHttpState::ABORTED);
	EXPECT_FALSE(pRequest->IsAbortRequested());
	EXPECT_EQ(Completion.m_Calls, 1);
	EXPECT_EQ(Completion.m_State, EHttpState::ABORTED);
	EXPECT_EQ(Http.Running(), 0u);
	EXPECT_FALSE(Http.Tracked(pRequest.get()));
	ASSERT_TRUE(Http.Pump());
	EXPECT_EQ(Completion.m_Calls, 1);
}

TEST(HttpRequestHeadersCurl, FailedRequestDoesNotConsumeCapacityForIndependentValidRequest)
{
	CHttpCurlSchedulerTestPeer Http;
	ASSERT_TRUE(Http.Ready());
	auto pFailed = std::make_shared<CHeaderRequest>();
	pFailed->m_FailAt = 1;
	pFailed->Header("Accept: application/json");
	Http.Run(pFailed);
	ASSERT_TRUE(Http.Pump());
	ASSERT_TRUE(pFailed->Done());
	auto pHealthy = std::make_shared<CHeaderRequest>();
	pHealthy->Header("Accept: application/json");
	Http.Run(pHealthy);
	ASSERT_TRUE(Http.Pump());
	EXPECT_EQ(pHealthy->State(), EHttpState::RUNNING);
	EXPECT_EQ(Http.Running(), 1u);
	EXPECT_TRUE(Http.Tracked(pHealthy.get()));
}

TEST_F(HttpRequestHeadersStorage, ContentTypeAppendFailureClosesCandidateAndPreservesDestination)
{
	ASSERT_NO_FATAL_FAILURE(WriteOldFile());
	CHttpCurlSchedulerTestPeer Http;
	ASSERT_TRUE(Http.Ready());
	auto pRequest = std::make_shared<CHeaderRequest>();
	pRequest->WriteToFile(m_pStorage.get(), "headers.bin", IStorage::TYPE_SAVE);
	pRequest->PostJson("{}");
	pRequest->Header("Accept: application/json");
	pRequest->m_FailAt = 2;
	Http.Run(pRequest);
	ASSERT_TRUE(Http.Pump());
	EXPECT_EQ(pRequest->State(), EHttpState::ABORTED);
	EXPECT_EQ(pRequest->m_AppendCalls, 2);
	EXPECT_EQ(pRequest->m_CloseCalls, 1);
	EXPECT_EQ(fs_is_file(pRequest->TemporaryPath().c_str()), 0);
	EXPECT_EQ(ReadOldFile(), "old");
	EXPECT_EQ(Http.Running(), 0u);
}

TEST_F(HttpRequestHeadersStorage, KnownHeaderFailureRejectsBeforeOpeningCandidate)
{
	CHttpCurlSchedulerTestPeer Http;
	ASSERT_TRUE(Http.Ready());
	auto pRequest = std::make_shared<CHeaderRequest>();
	pRequest->WriteToFile(m_pStorage.get(), "headers.bin", IStorage::TYPE_SAVE);
	const std::string Candidate = pRequest->TemporaryPath();
	pRequest->m_FailAt = 1;
	pRequest->Header("Accept: application/json");
	Http.Run(pRequest);
	ASSERT_TRUE(Http.Pump());
	EXPECT_EQ(pRequest->State(), EHttpState::ABORTED);
	EXPECT_EQ(pRequest->m_CloseCalls, 0);
	EXPECT_EQ(fs_is_file(Candidate.c_str()), 0);
	EXPECT_FALSE(m_pStorage->FileExists("headers.bin", IStorage::TYPE_SAVE));
}

TEST(HttpRequestHeadersCurl, SuccessfulJsonPreparationKeepsCallerHeadersAndContentTypeInOrder)
{
	CHttpCurlSchedulerTestPeer Http;
	ASSERT_TRUE(Http.Ready());
	auto pRequest = std::make_shared<CHeaderRequest>();
	pRequest->Header("Accept: application/json");
	pRequest->PostJson("{}");
	Http.Run(pRequest);
	ASSERT_TRUE(Http.Pump());
	ASSERT_EQ(pRequest->State(), EHttpState::RUNNING);
	const auto *pHeaders = CHttpRequestCurlTestPeer::RequestHeaders(*pRequest);
	ASSERT_NE(pHeaders, nullptr);
	EXPECT_STREQ(pHeaders->data, "Accept: application/json");
	ASSERT_NE(pHeaders->next, nullptr);
	EXPECT_STREQ(pHeaders->next->data, "Content-Type: application/json");
	EXPECT_EQ(pHeaders->next->next, nullptr);
}

TEST(HttpRequestHeadersCurl, RetryAfterDateCallbackStillPublishesCompleteResponse)
{
	CHeaderRequest Request;
	std::string Header = "Retry-After: Wed, 21 Oct 2015 07:28:00 GMT\r\n";
	EXPECT_EQ(CHttpRequestCurlTestPeer::HeaderCallback(Request, Header.data(), Header.size()), Header.size());
	CHttpRequestCurlTestPeer::Complete(Request);
	EXPECT_EQ(Request.State(), EHttpState::DONE);
	ASSERT_TRUE(Request.ResultRetryAfterSeconds().has_value());
	EXPECT_GE(*Request.ResultRetryAfterSeconds(), 0);
}
#endif
