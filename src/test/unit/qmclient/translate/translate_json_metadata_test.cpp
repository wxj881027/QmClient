#include <base/system.h>

#include <engine/shared/json.h>

#include <game/client/components/qmclient/translate/translate_parse.h>

#include <gtest/gtest.h>

#include <memory>

namespace
{
	using FParse = bool (*)(const json_value *, SLlmParseResult &);
	using TJson = std::unique_ptr<json_value, decltype(&json_value_free)>;

	TJson Read(const char *pBody) { return TJson(json_parse(pBody, str_length(pBody)), json_value_free); }
	void ExpectRejected(const char *pBody, FParse Parse)
	{
		const auto pJson = Read(pBody);
		ASSERT_NE(pJson, nullptr);
		SLlmParseResult Out;
		EXPECT_FALSE(Parse(pJson.get(), Out));
		EXPECT_FALSE(Out.m_Success);
		EXPECT_STREQ(Out.m_aText, "");
	}
	void ExpectText(const char *pBody, FParse Parse, const char *pText)
	{
		const auto pJson = Read(pBody);
		ASSERT_NE(pJson, nullptr);
		SLlmParseResult Out;
		EXPECT_TRUE(Parse(pJson.get(), Out));
		EXPECT_TRUE(Out.m_Success);
		EXPECT_STREQ(Out.m_aText, pText);
	}
}

TEST(TranslateJsonMetadata, ResponsesStatusRequiresCompleteStringIdentity)
{
	ExpectRejected(R"({"status":"completed\u0000incomplete","output_text":"partial"})", ParseLlmResponsesJson);
}

TEST(TranslateJsonMetadata, ResponsesMessageTypeRejectsEmbeddedNul)
{
	ExpectRejected(R"({"output":[{"type":"message\u0000other","content":[{"type":"output_text","text":"partial"}]}]})", ParseLlmResponsesJson);
}

TEST(TranslateJsonMetadata, ResponsesPartTypeRejectsEmbeddedNul)
{
	ExpectRejected(R"({"output":[{"type":"message","content":[{"type":"output_text\u0000other","text":"partial"}]}]})", ParseLlmResponsesJson);
}

TEST(TranslateJsonMetadata, MalformedRefusalTypeDoesNotPublishTopLevelText)
{
	ExpectRejected(R"({"output_text":"partial","output":[{"type":"message","content":[{"type":"refusal\u0000other"}]}]})", ParseLlmResponsesJson);
}

TEST(TranslateJsonMetadata, ChatFinishReasonRejectsEmbeddedNulWithoutRelaxingLengthRefusal)
{
	for(const char *pBody : {
		    R"({"choices":[{"finish_reason":"stop\u0000other","message":{"content":"partial"}}]})",
		    R"({"choices":[{"finish_reason":"length\u0000other","message":{"content":"partial"}}]})"})
	{
		SCOPED_TRACE(pBody);
		ExpectRejected(pBody, ParseLlmResponseJson);
	}
}

TEST(TranslateJsonMetadata, MalformedIncompleteReasonDoesNotPublishCompletedText)
{
	ExpectRejected(R"({"status":"completed","incomplete_details":{"reason":"max_output_tokens\u0000other"},"output_text":"partial"})", ParseLlmResponsesJson);
}

TEST(TranslateJsonMetadata, ShadowStatusCannotHideCanonicalIncompleteState)
{
	ExpectRejected(R"({"status\u0000other":"completed","status":"incomplete","output_text":"partial"})", ParseLlmResponsesJson);
}

TEST(TranslateJsonMetadata, ShadowErrorCannotHideCanonicalRefusal)
{
	const auto pJson = Read(R"({"error\u0000other":null,"error":{"code":"content_filter"},"output_text":"partial"})");
	ASSERT_NE(pJson, nullptr);
	SLlmParseResult Out;
	EXPECT_FALSE(ParseLlmResponsesJson(pJson.get(), Out));
	EXPECT_TRUE(Out.m_Refused);
	EXPECT_STREQ(Out.m_aText, "");
}

TEST(TranslateJsonMetadata, ShadowFinishReasonCannotHideTokenLimit)
{
	ExpectRejected(R"({"choices":[{"finish_reason\u0000other":"stop","finish_reason":"length","message":{"content":"partial"}}]})", ParseLlmResponseJson);
}

TEST(TranslateJsonMetadata, ShadowContentCannotReplaceCanonicalChatText)
{
	ExpectText(R"({"choices":[{"message":{"content\u0000other":"forged","content":"translated"}}]})", ParseLlmResponseJson, "translated");
}

TEST(TranslateJsonMetadata, UnknownKeyDoesNotBecomeChoices)
{
	ExpectRejected(R"({"choices\u0000other":[{"message":{"content":"forged"}}]})", ParseLlmResponseJson);
}

TEST(TranslateJsonMetadata, UnknownKeyDoesNotBecomeOutputText)
{
	ExpectRejected(R"({"output_text\u0000other":"forged"})", ParseLlmResponsesJson);
}

TEST(TranslateJsonMetadata, ShadowTextCannotReplaceCanonicalResponsesText)
{
	ExpectText(R"({"output_text\u0000other":"forged","output_text":"translated"})", ParseLlmResponsesJson, "translated");
}

TEST(TranslateJsonMetadata, ValidUnknownTypesRemainIgnored)
{
	ExpectText(R"({"output_text":"translated","output":[{"type":"future_reasoning_type","content":[]}]})", ParseLlmResponsesJson, "translated");
}
