#include "translate_http_test_support.h"

#include <engine/shared/json.h>

TEST_F(CTranslateBackendTest, LibreRequestIsCompleteAtSubmission)
{
	str_copy(g_Config.m_QmTranslateBackend, "libretranslate");
	str_copy(g_Config.m_QmTranslateLibreEndpoint, "https://translate.test/translate");
	auto pBackend = Create("你好\n\"世界\"", "en", "zh");
	ASSERT_EQ(m_Http.m_vSubmissions.size(), 1u);
	EXPECT_STREQ(m_Http.m_vSubmissions[0].m_Method.c_str(), "POST");
	const auto &Body = m_Http.m_vSubmissions[0].m_Body;
	json_value *pJson = json_parse(Body.c_str(), Body.size());
	ASSERT_NE(pJson, nullptr);
	EXPECT_STREQ(json_object_get(pJson, "q")->u.string.ptr, "你好\n\"世界\"");
	json_value_free(pJson);
}

TEST_F(CTranslateBackendTest, ResponsesKeepsAllTextSegments)
{
	str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "https://translate.test/v1/responses");
	auto pBackend = Create();
	ASSERT_EQ(m_Http.m_vSubmissions.size(), 1u);
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"output":[{"type":"message","content":[{"type":"output_text","text":"你好"},{"type":"output_text","text":"世界"}]}]})");
	CTranslateResponse Response;
	ASSERT_EQ(pBackend->Update(Response), std::optional<bool>(true));
	EXPECT_STREQ(Response.m_Text, "你好世界");
}

TEST_F(CTranslateBackendTest, MissingSelectedKeyIsTerminalWithoutHttp)
{
	g_Config.m_QmTranslateLlmKeyCustom[0] = '\0';
	// 空环境覆盖仅作用于本进程，析构时恢复原值。
	const char *pPrevious = std::getenv("QMTRANSLATE_LLM_KEY_CUSTOM");
	const std::string Previous = pPrevious ? pPrevious : "";
#if defined(CONF_FAMILY_WINDOWS)
	_putenv_s("QMTRANSLATE_LLM_KEY_CUSTOM", "");
#else
	unsetenv("QMTRANSLATE_LLM_KEY_CUSTOM");
#endif
	auto pBackend = Create();
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_NE(str_find(Response.m_Text, "Missing API Key"), nullptr);
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_TRUE(m_Http.m_vSubmissions.empty());
#if defined(CONF_FAMILY_WINDOWS)
	_putenv_s("QMTRANSLATE_LLM_KEY_CUSTOM", Previous.c_str());
#else
	if(pPrevious)
		setenv("QMTRANSLATE_LLM_KEY_CUSTOM", Previous.c_str(), 1);
#endif
}

TEST_F(CTranslateBackendTest, MissingEndpointIsTerminalWithoutHttp)
{
	g_Config.m_QmTranslateLlmEndpointCustom[0] = '\0';
	auto pBackend = Create();
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_NE(str_find(Response.m_Text, "Missing Endpoint"), nullptr);
	EXPECT_TRUE(m_Http.m_vSubmissions.empty());
}

TEST_F(CTranslateBackendTest, InvalidEndpointIsTerminalWithoutHttp)
{
	str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "ftp://translate.test");
	auto pBackend = Create();
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_NE(str_find(Response.m_Text, "Invalid Endpoint"), nullptr);
	EXPECT_TRUE(m_Http.m_vSubmissions.empty());
}

TEST_F(CTranslateBackendTest, ChatSuccessUsesSharedParser)
{
	str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "https://chat-success.test/v1/chat/completions");
	auto pBackend = Create();
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::nullopt);
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"choices":[{"message":{"content":"你好"}}]})");
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(true));
	EXPECT_STREQ(Response.m_Text, "你好");
}

TEST_F(CTranslateBackendTest, MalformedChatResponseFails)
{
	str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "https://malformed.test/v1/chat/completions");
	auto pBackend = Create();
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"choices":[]})");
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_STREQ(Response.m_Text, "choices is empty");
}

TEST_F(CTranslateBackendTest, AutoRetriesUnsupportedChatOnceThenParsesAllResponses)
{
	str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "https://retry-once.test/v1");
	auto pBackend = Create();
	m_Http.m_vSubmissions[0].m_pRequest->Finish("{}", 404);
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::nullopt);
	ASSERT_EQ(m_Http.m_vSubmissions.size(), 2u);
	EXPECT_NE(str_find(m_Http.m_vSubmissions[1].m_pRequest->Url(), "/responses"), nullptr);
	m_Http.m_vSubmissions[1].m_pRequest->Finish(R"({"output":[{"type":"message","content":[{"type":"output_text","text":""},{"type":"output_text","text":"你好"}]},{"type":"message","content":[{"type":"output_text","text":"世界"}]}]})");
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(true));
	EXPECT_STREQ(Response.m_Text, "你好世界");
	EXPECT_EQ(m_Http.m_vSubmissions.size(), 2u);
}

TEST_F(CTranslateBackendTest, AutoDoesNotRetryExplicitChatEndpoint)
{
	str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "https://explicit.test/v1/chat/completions");
	auto pBackend = Create();
	m_Http.m_vSubmissions[0].m_pRequest->Finish("{}", 404);
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_EQ(m_Http.m_vSubmissions.size(), 1u);
}

TEST_F(CTranslateBackendTest, AutoDoesNotRetryNetworkFailureOrCancellation)
{
	for(EHttpState State : {EHttpState::ERROR, EHttpState::ABORTED})
	{
		SCOPED_TRACE(static_cast<int>(State));
		str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "https://terminal-network.test/v1");
		auto pBackend = Create();
		m_Http.m_vSubmissions.back().m_pRequest->SetState(State);
		const auto Count = m_Http.m_vSubmissions.size();
		CTranslateResponse Response;
		EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
		EXPECT_EQ(m_Http.m_vSubmissions.size(), Count);
	}
}

TEST_F(CTranslateBackendTest, AutoDoesNotRetryQuotaError)
{
	str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "https://quota.test/v1");
	auto pBackend = Create();
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"error":{"message":"quota reached"}})", 429);
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_NE(str_find(Response.m_Text, "quota reached"), nullptr);
	EXPECT_EQ(m_Http.m_vSubmissions.size(), 1u);
}

TEST_F(CTranslateBackendTest, FailedResponsesRetryDoesNotCreateThirdRequest)
{
	str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "https://failed-retry.test/v1");
	auto pBackend = Create();
	m_Http.m_vSubmissions[0].m_pRequest->Finish("{}", 405);
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::nullopt);
	ASSERT_EQ(m_Http.m_vSubmissions.size(), 2u);
	m_Http.m_vSubmissions[1].m_pRequest->Finish("{}", 404);
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_EQ(m_Http.m_vSubmissions.size(), 2u);
}

TEST_F(CTranslateBackendTest, PayloadPreservesUnicodeControlCharactersAndLongPrompt)
{
	std::string Prompt;
	for(int i = 0; i < 160; ++i)
		Prompt += "中";
	Prompt += "\"\\\n";
	str_copy(g_Config.m_QmTranslateSystemPrompt, Prompt.c_str());
	const char *pText = "你好\n\t\"\\😀";
	auto pBackend = Create(pText);
	const auto &Body = m_Http.m_vSubmissions[0].m_Body;
	json_value *pJson = JsonParse(Body.c_str(), Body.size());
	ASSERT_NE(pJson, nullptr);
	const json_value *pMessages = json_object_get(pJson, "messages");
	ASSERT_EQ(pMessages->type, json_array);
	EXPECT_STREQ(json_object_get(pMessages->u.array.values[0], "content")->u.string.ptr, Prompt.c_str());
	EXPECT_STREQ(json_object_get(pMessages->u.array.values[1], "content")->u.string.ptr, pText);
	json_value_free(pJson);
}

TEST_F(CTranslateBackendTest, SelectedProviderKeyNeverUsesAnotherProvider)
{
	str_copy(g_Config.m_QmTranslateLlmKeyZhipu, "z-key");
	str_copy(g_Config.m_QmTranslateLlmKeyDeepseek, "d-key");
	str_copy(g_Config.m_QmTranslateLlmKeyOpenai, "o-key");
	str_copy(g_Config.m_QmTranslateLlmKeyCustom, "c-key");
	const char *apExpected[] = {"z-key", "d-key", "o-key", "c-key"};
	for(int i = 0; i < 4; ++i)
	{
		g_Config.m_QmTranslateLlmProvider = i;
		EXPECT_STREQ(GetSelectedTranslateLlmKey(), apExpected[i]);
	}
}

TEST_F(CTranslateBackendTest, ConcurrencyUsesProductionDefaultsAndUserOverride)
{
	const int aExpected[] = {1, 3, 2, 4};
	g_Config.m_QmTranslateLlmConcurrencyDefault = 4;
	for(int i = 0; i < 4; ++i)
	{
		g_Config.m_QmTranslateLlmProvider = i;
		EXPECT_EQ(GetTranslateConcurrency(), aExpected[i]);
	}
	const char *apBackends[] = {"mymemory", "ftapi", "libretranslate", "tencentcloud", "unknown"};
	const int aBackendExpected[] = {1, 1, 2, 5, 3};
	for(int i = 0; i < 5; ++i)
	{
		SCOPED_TRACE(apBackends[i]);
		str_copy(g_Config.m_QmTranslateBackend, apBackends[i]);
		EXPECT_EQ(GetTranslateConcurrency(), aBackendExpected[i]);
		g_Config.m_QmTranslateLlmConcurrency = 7;
		EXPECT_EQ(GetTranslateConcurrency(), 7);
		g_Config.m_QmTranslateLlmConcurrency = 0;
	}
}

TEST_F(CTranslateBackendTest, AllConfiguredBackendsHaveFiniteDeadlineAndAbortOnDestruction)
{
	str_copy(g_Config.m_QmTranslateTcSecretId, "fake-id");
	str_copy(g_Config.m_QmTranslateTcSecretKey, "fake-secret");
	for(const char *pBackend : {"llm", "libretranslate", "mymemory", "ftapi", "tencentcloud"})
	{
		SCOPED_TRACE(pBackend);
		str_copy(g_Config.m_QmTranslateBackend, pBackend);
		auto pTranslation = Create();
		auto pRequest = m_Http.m_vSubmissions.back().m_pRequest;
		EXPECT_GT(pRequest->TimeoutSettings().m_TimeoutMs, 0);
		EXPECT_FALSE(pRequest->IsAbortRequested());
		pTranslation.reset();
		EXPECT_TRUE(pRequest->IsAbortRequested());
	}
}

TEST_F(CTranslateBackendTest, LibreResponseSucceedsAndHttpFailureReportsError)
{
	str_copy(g_Config.m_QmTranslateBackend, "libretranslate");
	auto pBackend = Create();
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"translatedText":"你好","detectedLanguage":{"confidence":100,"language":"en"}})");
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(true));
	EXPECT_STREQ(Response.m_Text, "你好");
	EXPECT_STREQ(Response.m_Language, "en");
	auto pFailure = Create();
	m_Http.m_vSubmissions[1].m_pRequest->Finish(R"({"error":"invalid key"})", 403);
	EXPECT_EQ(pFailure->Update(Response), std::optional<bool>(false));
	EXPECT_STREQ(Response.m_Text, "invalid key");
}

TEST_F(CTranslateBackendTest, MymemoryQuotaWarningIsFailure)
{
	str_copy(g_Config.m_QmTranslateBackend, "mymemory");
	auto pBackend = Create();
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"responseData":{"translatedText":"MYMEMORY WARNING: YOU USED ALL AVAILABLE FREE TRANSLATIONS"}})");
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_NE(str_find(Response.m_Text, "daily anonymous quota reached"), nullptr);
}

TEST_F(CTranslateBackendTest, OutputExceedingChatCapacityIsRejectedWithoutPartialTranslation)
{
	str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "https://capacity.test/v1/responses");
	auto pBackend = Create();
	const std::string Body = "{\"output_text\":\"" + std::string(1100, 'a') + "\"}";
	m_Http.m_vSubmissions[0].m_pRequest->Finish(Body.c_str());
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_NE(str_find(Response.m_Text, "capacity"), nullptr);
}

TEST_F(CTranslateBackendTest, BuiltInPromptPreservesTailAndExplicitSourceHint)
{
	g_Config.m_QmTranslateSystemPrompt[0] = '\0';
	auto pBackend = Create("hello", "zh", "en");
	const auto &Body = m_Http.m_vSubmissions[0].m_Body;
	json_value *pJson = JsonParse(Body.c_str(), Body.size());
	ASSERT_NE(pJson, nullptr);
	const json_value *pMessages = json_object_get(pJson, "messages");
	ASSERT_EQ(pMessages->type, json_array);
	const char *pPrompt = json_object_get(pMessages->u.array.values[0], "content")->u.string.ptr;
	EXPECT_NE(str_find(pPrompt, "Keep it concise and natural"), nullptr);
	EXPECT_NE(str_find(pPrompt, "input language is en"), nullptr);
	EXPECT_NE(str_find(pPrompt, "message into zh"), nullptr);
	json_value_free(pJson);
}
