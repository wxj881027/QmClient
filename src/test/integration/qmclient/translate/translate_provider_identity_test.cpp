#include <game/client/components/qmclient/translate/translate_jobs.h>

#include <test/support/translate_http_test_support.h>

class CTranslateProviderIdentityTest : public CTranslateBackendTest
{
protected:
	void SetUp() override
	{
		CTranslateBackendTest::SetUp();
		str_copy(g_Config.m_QmTranslateLibreEndpoint, "https://translate.test/translate");
		str_copy(g_Config.m_QmTranslateTcSecretId, "test-id");
		str_copy(g_Config.m_QmTranslateTcSecretKey, "test-key");
		str_copy(g_Config.m_QmTranslateDeeplKey, "test-key:fx");
		str_copy(g_Config.m_QmTranslateBaiduAppId, "test-app");
		str_copy(g_Config.m_QmTranslateBaiduKey, "test-key");
	}
	void Check(const char *pBackend, const char *pBody, bool Success, ETranslateNotice Notice = ETranslateNotice::NONE, int HttpStatus = 200)
	{
		str_copy(g_Config.m_QmTranslateBackend, pBackend);
		CTranslateJobQueue Queue;
		ASSERT_TRUE(Queue.CreateAndSubmit([&] {
			STranslateJob Job;
			Job.m_Outgoing = true;
			Job.m_OriginalText = "original";
			Job.m_pBackend = Create("original");
			return Job;
		},
			1));
		m_Http.m_vSubmissions.back().m_pRequest->Finish(pBody, HttpStatus);
		const auto Done = Queue.Update([](const auto &) { return true; });
		ASSERT_EQ(Done.size(), 1u);
		EXPECT_EQ(Done[0].m_Success, Success);
		EXPECT_EQ(Done[0].m_Job.m_pTranslateResponse->m_Notice, Notice);
		int Sends = 0;
		EXPECT_EQ(DispatchOutgoingTranslation(Done[0], [&](int, const char *pText, const char *pOriginal, int) {
			++Sends;
			EXPECT_STREQ(pText, "translated");
			EXPECT_STREQ(pOriginal, "original");
			return true;
		}),
			Success);
		EXPECT_EQ(Sends, Success ? 1 : 0);
		EXPECT_EQ(Queue.Size(), 0u);
	}
	struct SCase
	{
		const char *m_pBackend;
		const char *m_pBody;
	};
};

TEST_F(CTranslateProviderIdentityTest, CanonicalTranslationWinsAcrossSevenServices)
{
	for(const auto &Case : {
		    SCase{"libretranslate", R"({"translatedText\u0000other":"forged","translatedText":"translated"})"},
		    SCase{"tencentcloud", R"({"Response":{"TargetText\u0000other":"forged","TargetText":"translated"}})"},
		    SCase{"ftapi", R"({"destination-text\u0000other":"forged","destination-text":"translated","source-language":"en"})"},
		    SCase{"mymemory", R"({"responseData":{"translatedText\u0000other":"forged","translatedText":"translated"}})"},
		    SCase{"deepl", R"({"translations\u0000other":[{"text":"forged"}],"translations":[{"text":"translated"}]})"},
		    SCase{"baidu", R"({"trans_result":[{"dst\u0000other":"forged","dst":"translated"}]})"},
		    SCase{"auto", R"({"ok":true,"text\u0000other":"forged","text":"translated"})"}})
	{
		SCOPED_TRACE(Case.m_pBackend);
		Check(Case.m_pBackend, Case.m_pBody, true);
	}
}

TEST_F(CTranslateProviderIdentityTest, UnknownTextAliasesAloneNeverCreateTranslation)
{
	for(const auto &Case : {
		    SCase{"libretranslate", R"({"translatedText\u0000other":"forged"})"},
		    SCase{"tencentcloud", R"({"Response":{"TargetText\u0000other":"forged"}})"},
		    SCase{"ftapi", R"({"destination-text\u0000other":"forged","source-language":"en"})"},
		    SCase{"mymemory", R"({"responseData":{"translatedText\u0000other":"forged"}})"},
		    SCase{"deepl", R"({"translations\u0000other":[{"text":"forged"}]})"},
		    SCase{"baidu", R"({"trans_result":[{"dst\u0000other":"forged"}]})"},
		    SCase{"auto", R"({"ok":true,"text\u0000other":"forged"})"}})
	{
		SCOPED_TRACE(Case.m_pBackend);
		Check(Case.m_pBackend, Case.m_pBody, false, ETranslateNotice::INVALID_RESPONSE);
	}
}

TEST_F(CTranslateProviderIdentityTest, TencentCanonicalErrorWinsOverShadowResponse)
{
	Check("tencentcloud", R"({"Response\u0000other":{"TargetText":"forged"},"Response":{"Error":{"Code":"RequestLimitExceeded"}}})", false, ETranslateNotice::RATE_LIMIT);
}

TEST_F(CTranslateProviderIdentityTest, MyMemoryCanonicalRateLimitWinsOverSuccessAlias)
{
	Check("mymemory", R"({"responseStatus\u0000other":200,"responseStatus":429,"responseData":{"translatedText":"must not publish"}})", false, ETranslateNotice::RATE_LIMIT);
}

TEST_F(CTranslateProviderIdentityTest, BaiduCanonicalErrorWinsOverSuccessAlias)
{
	Check("baidu", R"({"error_code\u0000other":52000,"error_code":54003,"trans_result":[{"dst":"must not publish"}]})", false, ETranslateNotice::RATE_LIMIT);
}

TEST_F(CTranslateProviderIdentityTest, AutomaticServiceCanonicalFailureWinsOverTrueAlias)
{
	Check("auto", R"({"ok\u0000other":true,"ok":false,"text":"must not publish"})", false, ETranslateNotice::SERVICE_UNAVAILABLE);
}

TEST_F(CTranslateProviderIdentityTest, AutomaticMalformedErrorCodeDoesNotBecomeKnownPrefix)
{
	Check("auto", R"({"ok":false,"error":"rate_limit\u0000other"})", false, ETranslateNotice::SERVICE_UNAVAILABLE);
}

TEST_F(CTranslateProviderIdentityTest, LlmCanonicalModelErrorDoesNotStartFallback)
{
	str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "https://identity-fallback.test/v1");
	Check("llm", R"({"error\u0000other":null,"error":{"code":"model_not_found","message":"vendor error"}})", false, ETranslateNotice::MODEL_NOT_FOUND, 404);
	EXPECT_EQ(m_Http.m_vSubmissions.size(), 1u);
}

TEST_F(CTranslateProviderIdentityTest, LlmMalformedKnownCodeIsInvalidAndDoesNotStartFallback)
{
	str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "https://identity-malformed.test/v1");
	Check("llm", R"({"error":{"code":"model_not_found\u0000other","message":"vendor error"}})", false, ETranslateNotice::INVALID_RESPONSE, 404);
	EXPECT_EQ(m_Http.m_vSubmissions.size(), 1u);
}
