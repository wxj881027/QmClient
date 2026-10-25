#include <engine/shared/http_curl.h>

#include <gtest/gtest.h>

#if !defined(CONF_PLATFORM_EMSCRIPTEN)
#include <game/client/components/qmclient/translate/translate_jobs.h>

#include <test/support/http_curl_scheduler_test_peer.h>
#include <test/support/translate_http_test_support.h>

namespace
{
	class CFailedBodyRequest : public CHttpRequestCurl
	{
	public:
		explicit CFailedBodyRequest(const char *pUrl) :
			CHttpRequestCurl(pUrl) {}

	private:
		void *AllocateRequestBody(size_t) noexcept override { return nullptr; }
	};
	std::unique_ptr<IHttpRequest> CreateFailedBodyRequest(const char *pUrl) { return std::make_unique<CFailedBodyRequest>(pUrl); }
}

class CTranslateNativeBodyFailureTest : public CTranslateBackendTest
{
protected:
	void SetUp() override
	{
		CTranslateBackendTest::SetUp();
		str_copy(g_Config.m_QmTranslateLibreEndpoint, "https://body-allocation.test/libre");
		str_copy(g_Config.m_QmTranslateTcSecretId, "test-id");
		str_copy(g_Config.m_QmTranslateTcSecretKey, "test-key");
		str_copy(g_Config.m_QmTranslateDeeplKey, "test-key:fx");
		str_copy(g_Config.m_QmTranslateBaiduAppId, "test-app");
		str_copy(g_Config.m_QmTranslateBaiduKey, "test-key");
	}
};

TEST_F(CTranslateNativeBodyFailureTest, PostServicesReleaseTaskWithoutSendingWhenNativeBodyAllocationFails)
{
	CHttpCurlSchedulerTestPeer Http;
	ASSERT_TRUE(Http.Ready());
	for(const char *pBackend : {"auto", "llm", "libretranslate", "tencentcloud", "deepl", "baidu"})
	{
		SCOPED_TRACE(pBackend);
		str_copy(g_Config.m_QmTranslateBackend, pBackend);
		CTranslateJobQueue Queue;
		ASSERT_TRUE(Queue.CreateAndSubmit([&] {
			STranslateJob Job;
			Job.m_Outgoing = true;
			Job.m_OriginalText = "original";
			Job.m_pBackend = CreateTranslateBackend(Http.Http(), "original", "zh", "auto", CreateFailedBodyRequest);
			return Job;
		},
			1));
		ASSERT_EQ(Http.Pending().size(), 1u);
		ASSERT_TRUE(Http.Pump());
		EXPECT_EQ(Http.Running(), 0u);
		const auto Done = Queue.Update([](const auto &) { return true; });
		ASSERT_EQ(Done.size(), 1u);
		EXPECT_FALSE(Done[0].m_Success);
		EXPECT_EQ(Queue.Size(), 0u);
		int Sends = 0;
		EXPECT_FALSE(DispatchOutgoingTranslation(Done[0], [&](int, const char *, const char *, int) { ++Sends; return true; }));
		EXPECT_EQ(Sends, 0);
	}
}
#endif
