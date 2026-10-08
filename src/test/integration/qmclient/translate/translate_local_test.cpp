#include <engine/shared/json.h>

#include <game/client/components/qmclient/translate/translate_probe.h>

#include <test/support/translate_http_test_support.h>

class CTranslateLocalTest : public CTranslateBackendTest
{
protected:
	void SetUp() override
	{
		CTranslateBackendTest::SetUp();
		g_Config.m_QmTranslateLlmCustomAuth = 1;
		g_Config.m_QmTranslateLlmKeyCustom[0] = '\0';
		str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "http://127.0.0.1:8000/v1/chat/completions");
	}
};

TEST_F(CTranslateLocalTest, ExplicitLocalNoAuthSubmitsWithoutAuthorizationHeader)
{
	EXPECT_FALSE(TranslateBackendNeedsConfiguration());
	auto pBackend = Create();
	ASSERT_EQ(m_Http.m_vSubmissions.size(), 1u);
	const auto &Request = m_Http.m_vSubmissions[0];
	for(const auto &Header : Request.m_pRequest->m_vHeaders)
		EXPECT_EQ(Header.find("Authorization"), std::string::npos);
	const auto &Timeout = Request.m_pRequest->TimeoutSettings();
	EXPECT_LE(Timeout.m_ConnectTimeoutMs, 2000L);
	json_value *pJson = JsonParse(Request.m_Body.c_str(), Request.m_Body.size());
	ASSERT_NE(pJson, nullptr);
	EXPECT_EQ(json_object_get(pJson, "thinking"), &json_value_none);
	EXPECT_EQ(json_object_get(pJson, "temperature"), &json_value_none);
	EXPECT_EQ(json_object_get(pJson, "max_tokens"), &json_value_none);
	json_value_free(pJson);
}

TEST_F(CTranslateLocalTest, LoopbackModeRejectsExternalOrLookalikeHosts)
{
	for(const char *pUrl : {"http://localhost.evil.test/v1", "https://api.example.com/v1", "http://127.0.0.1@evil.test/v1", "http://[::1].evil.test/v1"})
	{
		SCOPED_TRACE(pUrl);
		str_copy(g_Config.m_QmTranslateLlmEndpointCustom, pUrl);
		auto pBackend = Create();
		CTranslateResponse Response;
		EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
		EXPECT_EQ(Response.m_Notice, ETranslateNotice::INVALID_CONFIGURATION);
		EXPECT_TRUE(m_Http.m_vSubmissions.empty());
	}
}

TEST_F(CTranslateLocalTest, LocalhostAndIpv6LoopbackAreAccepted)
{
	for(const char *pUrl : {"http://localhost:11434/v1/chat/completions", "http://[::1]:8000/v1/chat/completions"})
	{
		SCOPED_TRACE(pUrl);
		str_copy(g_Config.m_QmTranslateLlmEndpointCustom, pUrl);
		auto pBackend = Create();
		EXPECT_FALSE(TranslateBackendNeedsConfiguration());
	}
	EXPECT_EQ(m_Http.m_vSubmissions.size(), 2u);
}

TEST_F(CTranslateLocalTest, BearerModeStillRequiresKeyForLocalEndpoint)
{
	g_Config.m_QmTranslateLlmCustomAuth = 0;
	EXPECT_TRUE(TranslateBackendNeedsConfiguration());
	auto pBackend = Create();
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_EQ(Response.m_Notice, ETranslateNotice::INVALID_CONFIGURATION);
	EXPECT_TRUE(m_Http.m_vSubmissions.empty());
}

TEST_F(CTranslateLocalTest, ConfiguredThinkingDialectUsesOnlyThatParameter)
{
	for(int Style : {1, 2, 3})
		for(int Enabled : {0, 1})
		{
			SCOPED_TRACE(Style);
			SCOPED_TRACE(Enabled);
			g_Config.m_QmTranslateLlmCustomThinking = Style;
			g_Config.m_QmTranslateLlmEnableThinking = Enabled;
			auto pBackend = Create();
			const auto &Body = m_Http.m_vSubmissions.back().m_Body;
			json_value *pJson = JsonParse(Body.c_str(), Body.size());
			ASSERT_NE(pJson, nullptr);
			if(Style == 1)
				EXPECT_STREQ(json_string_get(json_object_get(json_object_get(pJson, "thinking"), "type")), Enabled ? "enabled" : "disabled");
			else if(Style == 2)
				EXPECT_EQ(json_boolean_get(json_object_get(pJson, "enable_thinking")), Enabled);
			else
				EXPECT_EQ(json_boolean_get(json_object_get(json_object_get(pJson, "chat_template_kwargs"), "enable_thinking")), Enabled);
			if(Style != 1)
				EXPECT_EQ(json_object_get(pJson, "thinking"), &json_value_none);
			if(Style != 2)
				EXPECT_EQ(json_object_get(pJson, "enable_thinking"), &json_value_none);
			if(Style != 3)
				EXPECT_EQ(json_object_get(pJson, "chat_template_kwargs"), &json_value_none);
			json_value_free(pJson);
		}
}

TEST_F(CTranslateLocalTest, SamplingParametersAreExplicitlyOptedIn)
{
	g_Config.m_QmTranslateLlmCustomParameters = 1;
	auto pBackend = Create();
	const auto &Body = m_Http.m_vSubmissions[0].m_Body;
	json_value *pJson = JsonParse(Body.c_str(), Body.size());
	ASSERT_NE(pJson, nullptr);
	EXPECT_EQ(json_int_get(json_object_get(pJson, "max_tokens")), 1024);
	EXPECT_NE(json_object_get(pJson, "temperature"), &json_value_none);
	json_value_free(pJson);
}

TEST_F(CTranslateLocalTest, UnavailableModelServerDoesNotTriggerCloudRequest)
{
	auto pBackend = Create();
	m_Http.m_vSubmissions[0].m_pRequest->SetState(EHttpState::ERROR);
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_EQ(Response.m_Notice, ETranslateNotice::NETWORK_ERROR);
	EXPECT_EQ(m_Http.m_vSubmissions.size(), 1u);
}

TEST_F(CTranslateLocalTest, ModelNotFoundIsDistinctFromResponseFormatFailure)
{
	auto pBackend = Create();
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"error":{"code":"model_not_found","message":"private model information"}})", 404);
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_EQ(Response.m_Notice, ETranslateNotice::MODEL_NOT_FOUND);
	EXPECT_EQ(Response.m_HttpStatus, 404);
}

TEST_F(CTranslateLocalTest, ProbeShowsActualTranslationAndDoesNotDuplicateActiveRequest)
{
	CTranslateProbe Probe;
	EXPECT_TRUE(Probe.Start(m_Http, "hello", "zh", "en", CreateTranslateTestRequest));
	EXPECT_FALSE(Probe.Start(m_Http, "again", "zh", "en", CreateTranslateTestRequest));
	ASSERT_EQ(m_Http.m_vSubmissions.size(), 1u);
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"choices":[{"message":{"content":"你好"}}]})");
	Probe.AdvanceFrame();
	EXPECT_FALSE(Probe.Pending());
	EXPECT_TRUE(Probe.HasResult());
	EXPECT_FALSE(Probe.Response().m_Error);
	EXPECT_STREQ(Probe.Response().m_Text, "你好");
	EXPECT_EQ(Probe.Response().m_HttpStatus, 200);
}

TEST_F(CTranslateLocalTest, LeavingPageAbortsProbeAndLateCompletionCannotPublish)
{
	CTranslateProbe Probe;
	ASSERT_TRUE(Probe.Start(m_Http, "hello", "zh", "en", CreateTranslateTestRequest));
	auto pOld = m_Http.m_vSubmissions[0].m_pRequest;
	pOld->SetState(EHttpState::RUNNING);
	Probe.AdvanceFrame();
	Probe.AdvanceFrame(); // 没有 Touch，表示上一帧测试控件已离页。
	EXPECT_TRUE(pOld->IsAbortRequested());
	pOld->Finish(R"({"choices":[{"message":{"content":"过期译文"}}]})");
	Probe.Touch();
	Probe.AdvanceFrame();
	EXPECT_FALSE(Probe.Pending());
	EXPECT_FALSE(Probe.HasResult());
}

TEST_F(CTranslateLocalTest, CancelThenReopenPublishesOnlyNewTask)
{
	CTranslateProbe Probe;
	ASSERT_TRUE(Probe.Start(m_Http, "old", "zh", "en", CreateTranslateTestRequest));
	auto pOld = m_Http.m_vSubmissions[0].m_pRequest;
	Probe.Cancel();
	EXPECT_TRUE(pOld->IsAbortRequested());
	ASSERT_TRUE(Probe.Start(m_Http, "new", "zh", "en", CreateTranslateTestRequest));
	pOld->Finish(R"({"choices":[{"message":{"content":"旧"}}]})");
	m_Http.m_vSubmissions[1].m_pRequest->Finish(R"({"choices":[{"message":{"content":"新"}}]})");
	Probe.AdvanceFrame();
	ASSERT_TRUE(Probe.HasResult());
	EXPECT_STREQ(Probe.Response().m_Text, "新");
}

TEST_F(CTranslateLocalTest, ChangingModelInvalidatesPendingProbe)
{
	CTranslateProbe Probe;
	ASSERT_TRUE(Probe.Start(m_Http, "hello", "zh", "en", CreateTranslateTestRequest));
	auto pOld = m_Http.m_vSubmissions[0].m_pRequest;
	str_copy(g_Config.m_QmTranslateLlmModelCustom, "different-model");
	Probe.Touch();
	EXPECT_TRUE(pOld->IsAbortRequested());
	EXPECT_FALSE(Probe.Pending());
	EXPECT_FALSE(Probe.HasResult());
}

TEST_F(CTranslateLocalTest, FailedProbeKeepsStableClassificationAndHidesVendorMessage)
{
	CTranslateProbe Probe;
	ASSERT_TRUE(Probe.Start(m_Http, "hello", "zh", "en", CreateTranslateTestRequest));
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"error":{"code":"model_not_found","message":"SECRET private chat text"}})", 404);
	Probe.AdvanceFrame();
	ASSERT_TRUE(Probe.HasResult());
	EXPECT_TRUE(Probe.Response().m_Error);
	EXPECT_EQ(Probe.Response().m_Notice, ETranslateNotice::MODEL_NOT_FOUND);
	EXPECT_STREQ(Probe.Response().m_Text, "");
	EXPECT_STREQ(Probe.Service(), "LLM");
}

TEST_F(CTranslateLocalTest, ProbeFailuresRemainDistinctWithoutRetainingRawPayload)
{
	struct SCase
	{
		int m_Status;
		const char *m_pBody;
		ETranslateNotice m_Notice;
	};
	for(const auto &Case : {SCase{401, R"({"error":{"message":"key=SECRET"}})", ETranslateNotice::AUTHENTICATION}, SCase{429, "SECRET rate limit body", ETranslateNotice::RATE_LIMIT}, SCase{503, "SECRET busy body", ETranslateNotice::SERVICE_UNAVAILABLE}, SCase{200, "SECRET malformed response", ETranslateNotice::INVALID_RESPONSE}})
	{
		SCOPED_TRACE(Case.m_Status);
		CTranslateProbe Probe;
		ASSERT_TRUE(Probe.Start(m_Http, "hello", "zh", "en", CreateTranslateTestRequest));
		m_Http.m_vSubmissions.back().m_pRequest->Finish(Case.m_pBody, Case.m_Status);
		Probe.AdvanceFrame();
		ASSERT_TRUE(Probe.HasResult());
		EXPECT_TRUE(Probe.Response().m_Error);
		EXPECT_EQ(Probe.Response().m_Notice, Case.m_Notice);
		EXPECT_EQ(Probe.Response().m_HttpStatus, Case.m_Status);
		EXPECT_STREQ(Probe.Response().m_Text, "");
	}
}

TEST_F(CTranslateLocalTest, ProbeNetworkFailureKeepsZeroHttpStatus)
{
	CTranslateProbe Probe;
	ASSERT_TRUE(Probe.Start(m_Http, "hello", "zh", "en", CreateTranslateTestRequest));
	m_Http.m_vSubmissions[0].m_pRequest->SetState(EHttpState::ERROR);
	Probe.AdvanceFrame();
	ASSERT_TRUE(Probe.HasResult());
	EXPECT_EQ(Probe.Response().m_Notice, ETranslateNotice::NETWORK_ERROR);
	EXPECT_EQ(Probe.Response().m_HttpStatus, 0);
}

TEST_F(CTranslateLocalTest, ExplicitThinkingDialectDoesNotSilentlySwitchToResponses)
{
	g_Config.m_QmTranslateLlmCustomThinking = 3;
	str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "http://127.0.0.1:8888/v1");
	auto pBackend = Create();
	m_Http.m_vSubmissions[0].m_pRequest->Finish("{}", 404);
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_EQ(m_Http.m_vSubmissions.size(), 1u);
}

TEST_F(CTranslateLocalTest, ResponsesEndpointRejectsChatSpecificThinkingConfiguration)
{
	g_Config.m_QmTranslateLlmCustomThinking = 1;
	str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "http://127.0.0.1:8000/v1/responses");
	auto pBackend = Create();
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_EQ(Response.m_Notice, ETranslateNotice::INVALID_CONFIGURATION);
	EXPECT_TRUE(m_Http.m_vSubmissions.empty());
}

TEST_F(CTranslateLocalTest, OpenAiProviderUsesCompletionTokenLimitWithoutSamplingOrForeignThinking)
{
	g_Config.m_QmTranslateLlmProvider = 2;
	str_copy(g_Config.m_QmTranslateLlmKeyOpenai, "test-key");
	for(int Thinking : {0, 1})
	{
		SCOPED_TRACE(Thinking);
		g_Config.m_QmTranslateLlmEnableThinking = Thinking;
		auto pBackend = Create();
		const auto &Body = m_Http.m_vSubmissions.back().m_Body;
		json_value *pJson = JsonParse(Body.c_str(), Body.size());
		ASSERT_NE(pJson, nullptr);
		EXPECT_EQ(json_int_get(json_object_get(pJson, "max_completion_tokens")), 1024);
		EXPECT_EQ(json_object_get(pJson, "max_tokens"), &json_value_none);
		EXPECT_EQ(json_object_get(pJson, "temperature"), &json_value_none);
		EXPECT_EQ(json_object_get(pJson, "thinking"), &json_value_none);
		json_value_free(pJson);
	}
}
