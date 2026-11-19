#include <game/client/components/qmclient/translate/translate_jobs.h>

#include <test/support/translate_http_test_support.h>

#include <string>

namespace
{
	struct SProvider
	{
		const char *m_pName;
		const char *m_pPrefix;
		const char *m_pSuffix;
		const char *m_pWithoutMetadata;
	};
	constexpr SProvider s_aProviders[] = {
		{"auto", R"({"ok":true,"text":"translated","language":)", "}", R"({"ok":true,"text":"translated"})"},
		{"libretranslate", R"({"translatedText":"translated","detectedLanguage":{"language":)", "}}", R"({"translatedText":"translated"})"},
		{"tencentcloud", R"({"Response":{"TargetText":"translated","Source":)", "}}", R"({"Response":{"TargetText":"translated"}})"},
		{"ftapi", R"({"destination-text":"translated","source-language":)", "}", R"({"destination-text":"translated"})"},
		{"deepl", R"({"translations":[{"text":"translated","detected_source_language":)", "}]}", R"({"translations":[{"text":"translated"}]})"},
		{"baidu", R"({"trans_result":[{"dst":"translated"}],"from":)", "}", R"({"trans_result":[{"dst":"translated"}]})"},
	};

	class CTranslateLanguageMetadataTest : public CTranslateBackendTest
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
		void CheckBody(const char *pProvider, const std::string &Body, const char *pLanguage = "")
		{
			SCOPED_TRACE(pProvider);
			str_copy(g_Config.m_QmTranslateBackend, pProvider);
			CTranslateJobQueue Queue;
			ASSERT_TRUE(Queue.CreateAndSubmit([&] {
				STranslateJob Job;
				Job.m_Outgoing = true;
				Job.m_OriginalText = "original";
				Job.m_pBackend = Create("original", "zh", "auto");
				return Job;
			},
				1));
			ASSERT_FALSE(m_Http.m_vSubmissions.empty());
			m_Http.m_vSubmissions.back().m_pRequest->Finish(Body.c_str());
			const auto Done = Queue.Update([](const auto &) { return true; });
			ASSERT_EQ(Done.size(), 1u);
			ASSERT_TRUE(Done[0].m_Success);
			const auto &Response = *Done[0].m_Job.m_pTranslateResponse;
			EXPECT_STREQ(Response.m_Text, "translated");
			EXPECT_STREQ(Response.m_Language, pLanguage);
			EXPECT_EQ(Response.m_Notice, ETranslateNotice::NONE);
			int Sends = 0;
			EXPECT_TRUE(DispatchOutgoingTranslation(Done[0], [&](int, const char *pText, const char *pOriginal, int) {
				++Sends;
				EXPECT_STREQ(pText, "translated");
				EXPECT_STREQ(pOriginal, "original");
				return true;
			}));
			EXPECT_EQ(Sends, 1);
		}
		void Check(const SProvider &Provider, const std::string &Value, const char *pLanguage = "")
		{
			CheckBody(Provider.m_pName, std::string(Provider.m_pPrefix) + Value + Provider.m_pSuffix, pLanguage);
		}
		void Reject(const SProvider &Provider, const std::string &Body)
		{
			SCOPED_TRACE(Provider.m_pName);
			str_copy(g_Config.m_QmTranslateBackend, Provider.m_pName);
			auto pBackend = Create();
			ASSERT_FALSE(m_Http.m_vSubmissions.empty());
			m_Http.m_vSubmissions.back().m_pRequest->Finish(Body.c_str());
			CTranslateResponse Response;
			EXPECT_EQ(pBackend->Update(Response), std::optional<bool>(false));
		}
	};
}

TEST_F(CTranslateLanguageMetadataTest, ValidLanguageReachesResultAcrossSixSchemas)
{
	for(const auto &Provider : s_aProviders)
		Check(Provider, R"("en")", "en");
}

TEST_F(CTranslateLanguageMetadataTest, EmbeddedNullLanguageIsIgnoredWithoutLosingTranslation)
{
	for(const auto &Provider : s_aProviders)
		for(const char *pValue : {R"("zh\u0000en")", R"("jp\u0000en")"})
		{
			SCOPED_TRACE(pValue);
			Check(Provider, pValue);
		}
}

TEST_F(CTranslateLanguageMetadataTest, OversizedLanguageIsIgnoredWithoutPublishingPrefix)
{
	const std::string Value = "\"" + std::string(sizeof(CTranslateResponse().m_Language), 'z') + "\"";
	for(const auto &Provider : s_aProviders)
		Check(Provider, Value);
}

TEST_F(CTranslateLanguageMetadataTest, InvalidLanguageUtf8KeepsValidTranslationDispatchable)
{
	const std::string Value = std::string("\"zh") + "\xC3\x28" + "\"";
	for(const auto &Provider : s_aProviders)
		Check(Provider, Value);
}

TEST_F(CTranslateLanguageMetadataTest, WrongLanguageTypesAreIgnored)
{
	for(const auto &Provider : s_aProviders)
		for(const char *pValue : {"null", "true", "123", "{}", "[]"})
		{
			SCOPED_TRACE(pValue);
			Check(Provider, pValue);
		}
}

TEST_F(CTranslateLanguageMetadataTest, MissingLanguageMetadataKeepsTranslation)
{
	for(const auto &Provider : s_aProviders)
		CheckBody(Provider.m_pName, Provider.m_pWithoutMetadata);
}

TEST_F(CTranslateLanguageMetadataTest, MalformedLibreDetectionContainerIsIgnored)
{
	for(const char *pValue : {"null", "true", R"("zh")", "[]"})
	{
		SCOPED_TRACE(pValue);
		CheckBody("libretranslate", std::string(R"({"translatedText":"translated","detectedLanguage":)") + pValue + "}");
	}
}

TEST_F(CTranslateLanguageMetadataTest, ExactLanguageByteCapacityIsPreservedAcrossSchemas)
{
	const std::string Language(sizeof(CTranslateResponse().m_Language) - 1, 'z');
	for(const auto &Provider : s_aProviders)
		Check(Provider, "\"" + Language + "\"", Language.c_str());
}

TEST_F(CTranslateLanguageMetadataTest, InvalidUtf8InLibreDetectionSiblingStillRejectsResponse)
{
	const std::string Body = std::string(R"({"translatedText":"translated","detectedLanguage":{"language":"en","other":")") + "\xC3\x28" + R"("}})";
	Reject(s_aProviders[1], Body);
}

TEST_F(CTranslateLanguageMetadataTest, ValidBaiduLanguageStillUsesExistingAliasMapping)
{
	CheckBody("baidu", R"({"trans_result":[{"dst":"translated"}],"from":"jp"})", "ja");
}

TEST_F(CTranslateLanguageMetadataTest, InvalidTranslationUtf8StillRejectsResponse)
{
	for(const auto &Provider : s_aProviders)
	{
		std::string Body = std::string(Provider.m_pPrefix) + R"("en")" + Provider.m_pSuffix;
		const size_t At = Body.find("\"translated\"");
		ASSERT_NE(At, std::string::npos);
		Body.replace(At + 1, std::string("translated").size(), "\xC3\x28");
		Reject(Provider, Body);
	}
}

TEST_F(CTranslateLanguageMetadataTest, InvalidUtf8OutsideLanguageMetadataStillRejectsResponse)
{
	for(const auto &Provider : s_aProviders)
	{
		std::string Body = std::string(Provider.m_pPrefix) + R"("en")" + Provider.m_pSuffix;
		ASSERT_EQ(Body.back(), '}');
		Body.pop_back();
		Body += std::string(R"(,"unrelated":")") + "\xC3\x28" + R"("})";
		Reject(Provider, Body);
	}
}
