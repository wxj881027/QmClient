#include <game/client/components/qmclient/translate/translate_jobs.h>

#include <test/support/translate_http_test_support.h>

class CTranslateJsonIdentityTest : public CTranslateBackendTest
{
};

TEST_F(CTranslateJsonIdentityTest, ShadowStatusCannotPublishIncompleteResponsesTranslation)
{
	str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "https://translate.test/v1/responses");
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.CreateAndSubmit([&] {
		STranslateJob Job;
		Job.m_Outgoing = true;
		Job.m_OriginalText = "original";
		Job.m_pBackend = Create();
		return Job;
	},
		1));
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"status\u0000other":"completed","status":"incomplete","output_text":"partial"})");
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Success);
	int Sends = 0;
	EXPECT_FALSE(DispatchOutgoingTranslation(Done[0], [&](int, const char *, const char *, int) { ++Sends; return true; }));
	EXPECT_EQ(Sends, 0);
	EXPECT_EQ(Queue.Size(), 0u);
}

TEST_F(CTranslateJsonIdentityTest, ChatDispatchUsesCanonicalContentInsteadOfNulKeyPrefix)
{
	str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "https://translate.test/v1/chat/completions");
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.CreateAndSubmit([&] {
		STranslateJob Job;
		Job.m_Outgoing = true;
		Job.m_OriginalText = "original";
		Job.m_pBackend = Create();
		return Job;
	},
		1));
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"choices":[{"message":{"content\u0000other":"forged","content":"translated"}}]})");
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	int Sends = 0;
	EXPECT_TRUE(DispatchOutgoingTranslation(Done[0], [&](int, const char *pText, const char *pOriginal, int) {
		++Sends;
		EXPECT_STREQ(pText, "translated");
		EXPECT_STREQ(pOriginal, "original");
		return true;
	}));
	EXPECT_EQ(Sends, 1);
}
