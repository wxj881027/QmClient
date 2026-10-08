#include <engine/shared/json.h>

#include <test/support/translate_http_test_support.h>

TEST_F(CTranslateBackendTest, AutomaticServiceSubmitsOnlyTextAndLanguages)
{
	str_copy(g_Config.m_QmTranslateBackend, "auto");
	auto pBackend = Create("我来帮你", "ja");
	ASSERT_NE(pBackend, nullptr);
	ASSERT_EQ(m_Http.m_vSubmissions.size(), 1u);
	const auto &Request = m_Http.m_vSubmissions[0];
	EXPECT_EQ(Request.m_Method, "POST");
	json_value *pJson = json_parse(Request.m_Body.c_str(), Request.m_Body.size());
	ASSERT_NE(pJson, nullptr);
	EXPECT_EQ(pJson->u.object.length, 3u);
	EXPECT_STREQ((*pJson)["text"].u.string.ptr, "我来帮你");
	EXPECT_STREQ((*pJson)["source"].u.string.ptr, "auto");
	EXPECT_STREQ((*pJson)["target"].u.string.ptr, "ja");
	json_value_free(pJson);
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::nullopt);
	Request.m_pRequest->Finish(R"({"ok":true,"text":"手伝います","language":"zh"})");
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(true));
	EXPECT_FALSE(Response.m_Error);
	EXPECT_EQ(Response.m_HttpStatus, 200);
	EXPECT_STREQ(Response.m_Text, "手伝います");
	EXPECT_STREQ(Response.m_Language, "zh");
}

TEST_F(CTranslateBackendTest, AutomaticRefusalDiscardsPartialTextAndDetails)
{
	str_copy(g_Config.m_QmTranslateBackend, "auto");
	auto pBackend = Create();
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"ok":false,"error":"content_refused","text":"partial","detail":"provider explanation"})", 503);
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_TRUE(Response.m_Error);
	EXPECT_EQ(Response.m_HttpStatus, 503);
	EXPECT_EQ(Response.m_Notice, ETranslateNotice::CONTENT_REFUSED);
	EXPECT_STREQ(Response.m_Text, "");
	EXPECT_EQ(m_Http.m_vSubmissions.size(), 1u);
}

TEST_F(CTranslateBackendTest, AutomaticBackendDestructionCancelsTheRequest)
{
	str_copy(g_Config.m_QmTranslateBackend, "auto");
	auto pBackend = Create();
	pBackend.reset();
	EXPECT_TRUE(m_Http.m_vSubmissions[0].m_pRequest->IsAbortRequested());
}

TEST_F(CTranslateBackendTest, AutomaticMalformedResponseDoesNotReturnServerErrorText)
{
	str_copy(g_Config.m_QmTranslateBackend, "auto");
	auto pBackend = Create();
	m_Http.m_vSubmissions[0].m_pRequest->Finish("<html>error</html>", 502);
	CTranslateResponse Response;
	EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
	EXPECT_EQ(Response.m_Notice, ETranslateNotice::SERVICE_UNAVAILABLE);
	EXPECT_STREQ(Response.m_Text, "");
}
