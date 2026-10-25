#include <game/client/components/qmclient/translate/translate_jobs.h>

#include <test/support/translate_http_test_support.h>

class CTranslateServiceCodesTest : public CTranslateBackendTest
{
protected:
	void SetUp() override
	{
		CTranslateBackendTest::SetUp();
		str_copy(g_Config.m_QmTranslateBaiduAppId, "test-app");
		str_copy(g_Config.m_QmTranslateBaiduKey, "test-key");
	}
	void Check(const char *pBackend, const char *pBody, bool Success)
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
		m_Http.m_vSubmissions.back().m_pRequest->Finish(pBody);
		const auto Done = Queue.Update([](const auto &) { return true; });
		ASSERT_EQ(Done.size(), 1u);
		EXPECT_EQ(Done[0].m_Success, Success);
		if(!Success)
			EXPECT_EQ(Done[0].m_Job.m_pTranslateResponse->m_Notice, ETranslateNotice::INVALID_RESPONSE);
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
};

TEST_F(CTranslateServiceCodesTest, MyMemoryExtremeDoublesAreRejectedBeforeNarrowing)
{
	for(const char *pCode : {"1e100", "-1e100", "2147483648.0", "-2147483649.0"})
	{
		SCOPED_TRACE(pCode);
		const std::string Body = std::string("{\"responseStatus\":") + pCode + ",\"responseData\":{\"translatedText\":\"must not publish\"}}";
		Check("mymemory", Body.c_str(), false);
	}
}

TEST_F(CTranslateServiceCodesTest, MyMemoryWideIntegersCannotWrapIntoSuccess)
{
	for(const char *pCode : {"4294967496", "-4294967096"})
	{
		SCOPED_TRACE(pCode);
		const std::string Body = std::string("{\"responseStatus\":") + pCode + ",\"responseData\":{\"translatedText\":\"must not publish\"}}";
		Check("mymemory", Body.c_str(), false);
	}
}

TEST_F(CTranslateServiceCodesTest, MyMemoryFractionalStatusCannotBecomeSuccess)
{
	Check("mymemory", R"({"responseStatus":200.5,"responseData":{"translatedText":"must not publish"}})", false);
}

TEST_F(CTranslateServiceCodesTest, MyMemoryWrongStatusTypeCannotDefaultToSuccess)
{
	for(const char *pCode : {R"("200")", "null", "true", "{}"})
	{
		SCOPED_TRACE(pCode);
		const std::string Body = std::string("{\"responseStatus\":") + pCode + ",\"responseData\":{\"translatedText\":\"must not publish\"}}";
		Check("mymemory", Body.c_str(), false);
	}
}

TEST_F(CTranslateServiceCodesTest, MyMemoryIntegerWholeDoubleAndAbsentStatusRetainSuccess)
{
	for(const char *pBody : {
		    R"({"responseStatus":200,"responseData":{"translatedText":"translated"}})",
		    R"({"responseStatus":200.0,"responseData":{"translatedText":"translated"}})",
		    R"({"responseData":{"translatedText":"translated"}})"})
	{
		SCOPED_TRACE(pBody);
		Check("mymemory", pBody, true);
	}
}

TEST_F(CTranslateServiceCodesTest, BaiduWideIntegersCannotWrapIntoSuccess)
{
	for(const char *pCode : {"4295019296", "-4294915296"})
	{
		SCOPED_TRACE(pCode);
		const std::string Body = std::string("{\"error_code\":") + pCode + ",\"trans_result\":[{\"dst\":\"must not publish\"}]}";
		Check("baidu", Body.c_str(), false);
	}
}

TEST_F(CTranslateServiceCodesTest, BaiduMalformedDecimalStringCannotBecomeSuccess)
{
	for(const char *pCode : {R"("52000suffix")", R"("52000\u0000suffix")", R"("4295019296")"})
	{
		SCOPED_TRACE(pCode);
		const std::string Body = std::string("{\"error_code\":") + pCode + ",\"trans_result\":[{\"dst\":\"must not publish\"}]}";
		Check("baidu", Body.c_str(), false);
	}
}

TEST_F(CTranslateServiceCodesTest, BaiduIntegerAndDecimalStringRetainSuccessButDoubleIsRejected)
{
	Check("baidu", R"({"error_code":52000,"trans_result":[{"dst":"translated"}]})", true);
	Check("baidu", R"({"error_code":"52000","trans_result":[{"dst":"translated"}]})", true);
	Check("baidu", R"({"error_code":52000.0,"trans_result":[{"dst":"must not publish"}]})", false);
}

TEST_F(CTranslateServiceCodesTest, ZhipuWideOrMalformedCodeDoesNotBecomeKnownNotice)
{
	g_Config.m_QmTranslateLlmProvider = 0;
	str_copy(g_Config.m_QmTranslateLlmKeyZhipu, "test-key");
	str_copy(g_Config.m_QmTranslateLlmEndpointZhipu, "https://translate.test/v1/chat/completions");
	for(const char *pCode : {"4294968597", R"("1301suffix")", R"("1301\u0000suffix")"})
	{
		SCOPED_TRACE(pCode);
		const std::string Body = std::string("{\"error\":{\"code\":") + pCode + ",\"message\":\"vendor error\"}}";
		Check("llm", Body.c_str(), false);
	}
}
