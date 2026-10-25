#include <base/system.h>

#include <engine/shared/json.h>

#include <game/client/components/qmclient/translate/translate_backend_http.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace
{
	using TJson = std::unique_ptr<json_value, decltype(&json_value_free)>;
	TJson Raw(const std::string &Body) { return TJson(json_parse(Body.data(), Body.size()), json_value_free); }

	void ExpectLanguage(const std::string &Body, bool Valid, const char *pExpected = "")
	{
		const auto pJson = Raw(Body);
		ASSERT_NE(pJson, nullptr);
		CTranslateResponse Response;
		str_copy(Response.m_Text, "normal translation");
		str_copy(Response.m_Language, "zh");
		Response.m_Notice = ETranslateNotice::RATE_LIMIT;
		EXPECT_EQ(CopyTranslateLanguage(pJson.get(), Response), Valid);
		EXPECT_STREQ(Response.m_Language, pExpected);
		EXPECT_STREQ(Response.m_Text, "normal translation");
		EXPECT_EQ(Response.m_Notice, ETranslateNotice::RATE_LIMIT);
	}
}

TEST(TranslateLanguageMetadata, CompleteLanguagePreservesTextAndNotice)
{
	ExpectLanguage(R"("zh-TW")", true, "zh-TW");
}

TEST(TranslateLanguageMetadata, ExactByteCapacityIsPreservedWithoutTruncation)
{
	const std::string Language(sizeof(CTranslateResponse().m_Language) - 1, 'z');
	ExpectLanguage("\"" + Language + "\"", true, Language.c_str());
}

TEST(TranslateLanguageMetadata, OversizedLanguageClearsOnlyLanguage)
{
	for(size_t Length : {sizeof(CTranslateResponse().m_Language), size_t{64}})
	{
		SCOPED_TRACE(Length);
		ExpectLanguage("\"" + std::string(Length, 'z') + "\"", false);
	}
}

TEST(TranslateLanguageMetadata, EmbeddedNullCannotBecomeValidLanguagePrefix)
{
	ExpectLanguage(R"("zh\u0000en")", false);
}

TEST(TranslateLanguageMetadata, InvalidUtf8ClearsOnlyLanguage)
{
	ExpectLanguage(std::string("\"zh") + "\xC3\x28" + "\"", false);
}

TEST(TranslateLanguageMetadata, EmptyMissingAndWrongTypesCannotRetainOldLanguage)
{
	for(const char *pBody : {R"("")", "null", "true", "1", "{}", "[]"})
	{
		SCOPED_TRACE(pBody);
		ExpectLanguage(pBody, false);
	}
	CTranslateResponse Response;
	str_copy(Response.m_Language, "zh");
	EXPECT_FALSE(CopyTranslateLanguage(nullptr, Response));
	EXPECT_STREQ(Response.m_Language, "");
}

TEST(TranslateLanguageMetadata, ValidLanguageCanReplaceRejectedMetadata)
{
	const auto Invalid = Raw(R"("zh\u0000en")");
	const auto Valid = Raw(R"("en")");
	ASSERT_NE(Invalid, nullptr);
	ASSERT_NE(Valid, nullptr);
	CTranslateResponse Response;
	str_copy(Response.m_Text, "normal translation");
	EXPECT_FALSE(CopyTranslateLanguage(Invalid.get(), Response));
	EXPECT_TRUE(CopyTranslateLanguage(Valid.get(), Response));
	EXPECT_STREQ(Response.m_Language, "en");
	EXPECT_STREQ(Response.m_Text, "normal translation");
}

TEST(TranslateLanguageMetadata, DefaultJsonValidationRemainsStrict)
{
	const std::string Body = std::string(R"({"language":")") + "\xC3\x28" + R"(","text":"normal"})";
	const auto pJson = Raw(Body);
	ASSERT_NE(pJson, nullptr);
	EXPECT_FALSE(JsonValidateUtf8(pJson.get()));
	EXPECT_TRUE(JsonValidateUtf8(pJson.get(), json_object_get(pJson.get(), "language")));
	const TJson pStrict(JsonParse(Body.data(), Body.size()), json_value_free);
	EXPECT_EQ(pStrict, nullptr);
}

TEST(TranslateLanguageMetadata, IgnoredMetadataDoesNotSkipSiblingValuesOrMemberNames)
{
	for(const std::string &Body : {
		    std::string(R"({"language":"en","text":")") + "\xC3\x28" + R"("})",
		    std::string(R"({"language":"en",")") + "\xC3\x28" + R"(":"normal"})"})
	{
		const auto pJson = Raw(Body);
		ASSERT_NE(pJson, nullptr);
		EXPECT_FALSE(JsonValidateUtf8(pJson.get(), json_object_get(pJson.get(), "language")));
	}
}

TEST(TranslateLanguageMetadata, NullJsonRootIsNotValid)
{
	EXPECT_FALSE(JsonValidateUtf8(nullptr));
}

TEST(TranslateLanguageMetadata, IgnoringValueStillValidatesItsParentMemberName)
{
	const std::string Body = std::string("{\"") + "\xC3\x28" + R"(":"normal"})";
	const auto pJson = Raw(Body);
	ASSERT_NE(pJson, nullptr);
	ASSERT_EQ(pJson->type, json_object);
	ASSERT_EQ(pJson->u.object.length, 1u);
	EXPECT_FALSE(JsonValidateUtf8(pJson.get(), pJson->u.object.values[0].value));
}
