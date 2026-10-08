#include <engine/shared/json.h>

#include <game/client/components/qmclient/translate/translate_backend_http.h>

#include <test/support/translate_http_test_support.h>

class CTranslateBaiduTest : public CTranslateBackendTest
{
protected:
	void SetUp() override
	{
		CTranslateBackendTest::SetUp();
		str_copy(g_Config.m_QmTranslateBackend, "baidu");
		str_copy(g_Config.m_QmTranslateBaiduAppId, "test-app");
		str_copy(g_Config.m_QmTranslateBaiduKey, "test-secret");
	}
};

TEST_F(CTranslateBaiduTest, OfficialSignatureVectorSignsUnencodedText)
{
	// 固定预期来自百度公开文档的 apple 示例，不在测试中重算签名。
	EXPECT_EQ(BuildBaiduTranslateForm("apple", "en", "zh", "2015063000000001", "12345678", "1435660288"),
		"q=apple&from=en&to=zh&appid=2015063000000001&salt=1435660288&sign=f89f9594663708c1605f3d736d01d2d4");
}

TEST_F(CTranslateBaiduTest, PostPreservesUnicodeAndDoesNotSendSecretOrTextInUrl)
{
	auto pBackend = Create("你好 &+\n", "zh-TW", "ja");
	ASSERT_EQ(m_Http.m_vSubmissions.size(), 1u);
	const auto &Request = m_Http.m_vSubmissions[0];
	EXPECT_STREQ(Request.m_pRequest->Url(), "https://fanyi-api.baidu.com/api/trans/vip/translate");
	EXPECT_EQ(Request.m_Method, "POST");
	EXPECT_EQ(Request.m_Body.find("q=%E4%BD%A0%E5%A5%BD%20%26%2B%0A&from=jp&to=cht&appid=test-app&salt="), 0u);
	EXPECT_EQ(Request.m_Body.find("test-secret"), std::string::npos);
	ASSERT_EQ(Request.m_pRequest->m_vHeaders.size(), 1u);
	EXPECT_EQ(Request.m_pRequest->m_vHeaders[0], "Content-Type: application/x-www-form-urlencoded; charset=UTF-8");
}

TEST_F(CTranslateBaiduTest, ClientLanguagesMapToBaiduAndCompareAsEquivalent)
{
	struct SCase
	{
		const char *m_pClient;
		const char *m_pService;
	};
	for(const auto &Case : {SCase{"ja", "jp"}, SCase{"ko", "kor"}, SCase{"fr", "fra"}, SCase{"es", "spa"}, SCase{"zh-CN", "zh"}, SCase{"zh-Hant", "cht"}})
	{
		SCOPED_TRACE(Case.m_pClient);
		auto pBackend = Create("hello", Case.m_pClient, Case.m_pClient);
		const auto &Body = m_Http.m_vSubmissions.back().m_Body;
		EXPECT_NE(Body.find(std::string("&from=") + Case.m_pService + "&to=" + Case.m_pService), std::string::npos);
		EXPECT_TRUE(pBackend->CompareTargets(Case.m_pClient, Case.m_pService));
	}
}

TEST_F(CTranslateBaiduTest, MissingCredentialsSubmitNothingAndNewConfigurationRecovers)
{
	g_Config.m_QmTranslateBaiduKey[0] = '\0';
	EXPECT_TRUE(TranslateBackendNeedsConfiguration());
	auto pMissing = Create();
	CTranslateResponse Response;
	EXPECT_EQ(pMissing->Update(Response), std::optional<bool>(false));
	EXPECT_EQ(Response.m_Notice, ETranslateNotice::INVALID_CONFIGURATION);
	EXPECT_TRUE(m_Http.m_vSubmissions.empty());
	str_copy(g_Config.m_QmTranslateBaiduKey, "new-secret");
	EXPECT_FALSE(TranslateBackendNeedsConfiguration());
	auto pReady = Create();
	ASSERT_EQ(m_Http.m_vSubmissions.size(), 1u);
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"from":"en","to":"zh","trans_result":[{"dst":"你好"}]})");
	EXPECT_EQ(pReady->Update(Response), std::optional<bool>(true));
	EXPECT_STREQ(Response.m_Text, "你好");
}

TEST_F(CTranslateBaiduTest, MissingAppIdDoesNotSubmitEvenWithKey)
{
	g_Config.m_QmTranslateBaiduAppId[0] = '\0';
	auto pBackend = Create();
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_TRUE(m_Http.m_vSubmissions.empty());
}

TEST_F(CTranslateBaiduTest, MultipleResultsPublishTogetherAndNormalizeDetectedSource)
{
	auto pBackend = Create("first\nsecond");
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"from":"jp","to":"zh","trans_result":[{"dst":"第一行"},{"dst":"第二行"}]})");
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(true));
	EXPECT_STREQ(Response.m_Text, "第一行\n第二行");
	EXPECT_STREQ(Response.m_Language, "ja");
}

TEST_F(CTranslateBaiduTest, InvalidLaterResultDoesNotPublishEarlierTranslation)
{
	auto pBackend = Create();
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"trans_result":[{"dst":"部分译文"},{"dst":null}]})");
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_EQ(Response.m_Notice, ETranslateNotice::INVALID_RESPONSE);
	EXPECT_EQ(str_find(Response.m_Text, "部分译文"), nullptr);
}

TEST_F(CTranslateBaiduTest, BusinessErrorsNeverBecomeTranslations)
{
	struct SCase
	{
		int m_Code;
		ETranslateNotice m_Notice;
	};
	for(const auto &Case : {SCase{54001, ETranslateNotice::AUTHENTICATION}, SCase{54003, ETranslateNotice::RATE_LIMIT}, SCase{54004, ETranslateNotice::QUOTA_EXCEEDED}, SCase{58001, ETranslateNotice::INVALID_CONFIGURATION}, SCase{52002, ETranslateNotice::SERVICE_UNAVAILABLE}})
	{
		SCOPED_TRACE(Case.m_Code);
		auto pBackend = Create();
		const std::string Body = "{\"error_code\":\"" + std::to_string(Case.m_Code) + "\",\"error_msg\":\"vendor message\",\"trans_result\":[{\"dst\":\"must not publish\"}]}";
		m_Http.m_vSubmissions.back().m_pRequest->Finish(Body.c_str());
		CTranslateResponse Response;
		EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
		EXPECT_EQ(Response.m_Notice, Case.m_Notice);
		EXPECT_EQ(str_find(Response.m_Text, "must not publish"), nullptr);
	}
}

TEST_F(CTranslateBaiduTest, HttpFailureCannotPublishValidLookingTranslation)
{
	auto pBackend = Create();
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"trans_result":[{"dst":"你好"}]})", 503);
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_EQ(Response.m_Notice, ETranslateNotice::SERVICE_UNAVAILABLE);
}

TEST_F(CTranslateBaiduTest, ByteLimitRejectsWholeInputRatherThanTruncating)
{
	std::string Text(6000, 'x');
	auto pAtLimit = Create(Text.c_str());
	ASSERT_EQ(m_Http.m_vSubmissions.size(), 1u);
	EXPECT_EQ(m_Http.m_vSubmissions[0].m_Body.find("q=" + Text + "&from=auto"), 0u);
	Text += 'x';
	auto pTooLong = Create(Text.c_str());
	CTranslateResponse Response;
	EXPECT_EQ(pTooLong->Update(Response), std::optional<bool>(false));
	EXPECT_EQ(Response.m_Notice, ETranslateNotice::INPUT_TOO_LONG);
	EXPECT_EQ(m_Http.m_vSubmissions.size(), 1u);
}

TEST_F(CTranslateBaiduTest, PendingRequestsFinishOnceAndDestructionAborts)
{
	auto pBackend = Create();
	auto pRequest = m_Http.m_vSubmissions[0].m_pRequest;
	CTranslateResponse Response;
	pRequest->SetState(EHttpState::RUNNING);
	EXPECT_EQ(pBackend->Update(Response), std::nullopt);
	pBackend.reset();
	EXPECT_TRUE(pRequest->IsAbortRequested());
	EXPECT_EQ(m_Http.m_vSubmissions.size(), 1u);
}

class CTranslateServiceRegressionTest : public CTranslateBackendTest
{
};

TEST_F(CTranslateServiceRegressionTest, LibreExplicitSourceSucceedsWithoutLanguageDetection)
{
	str_copy(g_Config.m_QmTranslateBackend, "libretranslate");
	str_copy(g_Config.m_QmTranslateLibreEndpoint, "https://translate.test/translate");
	auto pBackend = Create("hello", "zh", "en");
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"translatedText":"你好"})");
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(true));
	EXPECT_STREQ(Response.m_Text, "你好");
	EXPECT_STREQ(Response.m_Language, "");
}

TEST_F(CTranslateServiceRegressionTest, NonLlmServicesRejectEmptyOrOversizedResults)
{
	struct SCase
	{
		const char *m_pBackend;
		const char *m_pPrefix;
		const char *m_pSuffix;
	};
	const SCase aCases[] = {
		{"libretranslate", "{\"translatedText\":\"", "\"}"},
		{"ftapi", "{\"destination-text\":\"", "\",\"source-language\":\"en\"}"},
		{"mymemory", "{\"responseData\":{\"translatedText\":\"", "\"}}"},
		{"tencentcloud", "{\"Response\":{\"TargetText\":\"", "\"}}"},
	};
	str_copy(g_Config.m_QmTranslateTcSecretId, "test-id");
	str_copy(g_Config.m_QmTranslateTcSecretKey, "test-key");
	for(const auto &Case : aCases)
		for(const std::string &Text : {std::string(), std::string(1024, 'x')})
		{
			SCOPED_TRACE(Case.m_pBackend);
			SCOPED_TRACE(Text.size());
			str_copy(g_Config.m_QmTranslateBackend, Case.m_pBackend);
			auto pBackend = Create();
			const std::string Body = std::string(Case.m_pPrefix) + Text + Case.m_pSuffix;
			m_Http.m_vSubmissions.back().m_pRequest->Finish(Body.c_str());
			CTranslateResponse Response;
			EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
			EXPECT_EQ(Response.m_Notice, ETranslateNotice::INVALID_RESPONSE);
		}
}

TEST_F(CTranslateServiceRegressionTest, MymemoryRejectsOversizedUtf8InputBeforeHttp)
{
	str_copy(g_Config.m_QmTranslateBackend, "mymemory");
	std::string Text;
	for(int i = 0; i < 167; ++i)
		Text += "中";
	auto pBackend = Create(Text.c_str());
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_EQ(Response.m_Notice, ETranslateNotice::INPUT_TOO_LONG);
	EXPECT_TRUE(m_Http.m_vSubmissions.empty());
}

TEST_F(CTranslateServiceRegressionTest, MymemoryPreservesInputAtByteLimit)
{
	str_copy(g_Config.m_QmTranslateBackend, "mymemory");
	const std::string Text(500, 'a');
	auto pBackend = Create(Text.c_str());
	ASSERT_EQ(m_Http.m_vSubmissions.size(), 1u);
	EXPECT_NE(str_find(m_Http.m_vSubmissions[0].m_pRequest->Url(), Text.c_str()), nullptr);
}

TEST_F(CTranslateServiceRegressionTest, FtapiRejectsUrlBeyondHttpCapacityWithoutTruncating)
{
	str_copy(g_Config.m_QmTranslateBackend, "ftapi");
	const std::string Text = std::string(5000, 'a') + "最后";
	auto pBackend = Create(Text.c_str(), "ja&text=evil");
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_EQ(Response.m_Notice, ETranslateNotice::INPUT_TOO_LONG);
	EXPECT_TRUE(m_Http.m_vSubmissions.empty());
}

TEST_F(CTranslateServiceRegressionTest, FtapiEncodesLanguageAndUnicodeWithoutQueryInjection)
{
	str_copy(g_Config.m_QmTranslateBackend, "ftapi");
	auto pBackend = Create("最后", "ja&text=evil");
	ASSERT_EQ(m_Http.m_vSubmissions.size(), 1u);
	const char *pUrl = m_Http.m_vSubmissions[0].m_pRequest->Url();
	EXPECT_NE(str_find(pUrl, "dl=ja%26text%3Devil&text="), nullptr);
	EXPECT_NE(str_find(pUrl, "%E6%9C%80%E5%90%8E"), nullptr);
}

TEST_F(CTranslateServiceRegressionTest, DeeplNormalizesChineseAndRegionalSources)
{
	str_copy(g_Config.m_QmTranslateBackend, "deepl");
	str_copy(g_Config.m_QmTranslateDeeplKey, "test:fx");
	struct SCase
	{
		const char *m_pSource;
		const char *m_pExpected;
	};
	for(const auto &Case : {SCase{"zh-CN", "ZH"}, SCase{"zh-Hant", "ZH"}, SCase{"en-US", "EN"}, SCase{"pt-BR", "PT"}})
	{
		SCOPED_TRACE(Case.m_pSource);
		auto pBackend = Create("hello", "zh-CN", Case.m_pSource);
		const auto &Body = m_Http.m_vSubmissions.back().m_Body;
		json_value *pJson = JsonParse(Body.c_str(), Body.size());
		ASSERT_NE(pJson, nullptr);
		EXPECT_STREQ(json_string_get(json_object_get(pJson, "source_lang")), Case.m_pExpected);
		EXPECT_STREQ(json_string_get(json_object_get(pJson, "target_lang")), "ZH");
		json_value_free(pJson);
	}
}

TEST_F(CTranslateServiceRegressionTest, ZhipuBusinessErrorsHaveStableNotices)
{
	g_Config.m_QmTranslateLlmProvider = 0;
	str_copy(g_Config.m_QmTranslateLlmKeyZhipu, "test-key");
	struct SCase
	{
		int m_Code;
		ETranslateNotice m_Notice;
	};
	for(const auto &Case : {SCase{1113, ETranslateNotice::QUOTA_EXCEEDED}, SCase{1301, ETranslateNotice::CONTENT_REFUSED}, SCase{1302, ETranslateNotice::RATE_LIMIT}, SCase{1305, ETranslateNotice::SERVICE_UNAVAILABLE}, SCase{1308, ETranslateNotice::QUOTA_EXCEEDED}, SCase{1211, ETranslateNotice::MODEL_NOT_FOUND}, SCase{1000, ETranslateNotice::AUTHENTICATION}})
		for(int Status : {200, 400})
		{
			SCOPED_TRACE(Case.m_Code);
			SCOPED_TRACE(Status);
			auto pBackend = Create();
			const std::string Body = "{\"error\":{\"code\":\"" + std::to_string(Case.m_Code) + "\",\"message\":\"vendor message\"}}";
			m_Http.m_vSubmissions.back().m_pRequest->Finish(Body.c_str(), Status);
			CTranslateResponse Response;
			EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
			EXPECT_EQ(Response.m_Notice, Case.m_Notice);
		}
}

TEST_F(CTranslateServiceRegressionTest, TencentBusinessRateLimitIsNotPublishedAsTranslation)
{
	str_copy(g_Config.m_QmTranslateBackend, "tencentcloud");
	str_copy(g_Config.m_QmTranslateTcSecretId, "test-id");
	str_copy(g_Config.m_QmTranslateTcSecretKey, "test-key");
	auto pBackend = Create();
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"Response":{"Error":{"Code":"RequestLimitExceeded","Message":"long service notice"}}})");
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_EQ(Response.m_Notice, ETranslateNotice::RATE_LIMIT);
}
