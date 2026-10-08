#include <base/system.h>

#include <engine/shared/json.h>

#include <game/client/components/qmclient/translate/translate_parse.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
	bool Parse(const char *pJson, SLlmParseResult &Out)
	{
		json_value *pValue = json_parse(pJson, str_length(pJson));
		const bool Success = ParseLlmResponsesJson(pValue, Out);
		json_value_free(pValue);
		return Success;
	}
}

TEST(TranslateResponses, EmptyTopLevelFallsBackToAllMessageSegments)
{
	SLlmParseResult Result;
	ASSERT_TRUE(Parse(R"({"output_text":"","output":[{"type":"reasoning","content":[]},{"type":"message","content":[{"type":"output_text","text":""},{"type":"output_text","text":"你"}]},{"type":"message","content":[{"type":"output_text","text":"好"}]}]})", Result));
	EXPECT_STREQ(Result.m_aText, "你好");
}

TEST(TranslateResponses, NonEmptyTopLevelIsNotDuplicatedByArray)
{
	SLlmParseResult Result;
	ASSERT_TRUE(Parse(R"({"output_text":"完整","output":[{"type":"message","content":[{"type":"output_text","text":"完整"}]}]})", Result));
	EXPECT_STREQ(Result.m_aText, "完整");
}

TEST(TranslateResponses, ErrorAndMalformedOrEmptyTextDoNotSucceed)
{
	SLlmParseResult Result;
	for(const char *pJson : {"null", "[]", "{}", R"({"error":{"message":"failed"}})", R"({"output":[null,{},"bad",{"type":"message","content":[{},null,{"type":"output_text","text":3}]}]})"})
	{
		SCOPED_TRACE(pJson);
		EXPECT_FALSE(Parse(pJson, Result));
		EXPECT_FALSE(Result.m_Success);
		EXPECT_STREQ(Result.m_aText, "");
	}
}

TEST(TranslateResponses, OverflowDoesNotPublishPartialText)
{
	SLlmParseResult Result;
	const std::string First(4090, 'a');
	const std::string Json = "{\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"output_text\",\"text\":\"" + First + "\"},{\"type\":\"output_text\",\"text\":\"你好\"}]}]}";
	EXPECT_FALSE(Parse(Json.c_str(), Result));
	EXPECT_FALSE(Result.m_Success);
	EXPECT_STREQ(Result.m_aText, "");
	EXPECT_NE(str_find(Result.m_aError, "capacity"), nullptr);
}

TEST(TranslateResponses, RepeatedParseClearsPreviousErrorAndText)
{
	SLlmParseResult Result;
	EXPECT_FALSE(Parse("{}", Result));
	ASSERT_TRUE(Parse(R"({"output_text":"恢复"})", Result));
	EXPECT_STREQ(Result.m_aText, "恢复");
	EXPECT_STREQ(Result.m_aError, "");
	EXPECT_FALSE(Parse("{}", Result));
	EXPECT_STREQ(Result.m_aText, "");
}

TEST(TranslateResponses, RefusalDiscardsTopLevelAndPartialTranslation)
{
	SLlmParseResult Result;
	EXPECT_FALSE(Parse(R"({"output_text":"partial","output":[{"type":"message","content":[{"type":"output_text","text":"partial"},{"type":"refusal","refusal":"A long provider explanation"}]}]})", Result));
	EXPECT_TRUE(Result.m_Refused);
	EXPECT_FALSE(Result.m_Success);
	EXPECT_STREQ(Result.m_aText, "");
	EXPECT_EQ(str_find(Result.m_aError, "A long provider"), nullptr);
}

TEST(TranslateResponses, ContentFilterReasonIsRefusal)
{
	SLlmParseResult Result;
	EXPECT_FALSE(Parse(R"({"status":"incomplete","incomplete_details":{"reason":"content_filter"},"output_text":"partial"})", Result));
	EXPECT_TRUE(Result.m_Refused);
	EXPECT_STREQ(Result.m_aText, "");
}

TEST(TranslateResponses, OrdinaryTextAboutRefusalIsStillTranslation)
{
	SLlmParseResult Result;
	ASSERT_TRUE(Parse(R"({"output_text":"I cannot translate this sentence."})", Result));
	EXPECT_FALSE(Result.m_Refused);
	EXPECT_STREQ(Result.m_aText, "I cannot translate this sentence.");
}

TEST(TranslateResponses, LaterSuccessClearsRefusal)
{
	SLlmParseResult Result;
	EXPECT_FALSE(Parse(R"({"error":{"code":"content_policy_violation","message":"blocked"}})", Result));
	ASSERT_TRUE(Result.m_Refused);
	ASSERT_TRUE(Parse(R"({"output_text":"恢复"})", Result));
	EXPECT_FALSE(Result.m_Refused);
	EXPECT_STREQ(Result.m_aText, "恢复");
}

TEST(TranslateResponses, IncompleteResponseDoesNotPublishPartialOutput)
{
	const char *pBody = R"({"status":"incomplete","incomplete_details":{"reason":"max_output_tokens"},"output_text":"partial"})";
	json_value *pJson = JsonParse(pBody, str_length(pBody));
	SLlmParseResult Result;
	EXPECT_FALSE(ParseLlmResponsesJson(pJson, Result));
	EXPECT_STREQ(Result.m_aText, "");
	json_value_free(pJson);
}
