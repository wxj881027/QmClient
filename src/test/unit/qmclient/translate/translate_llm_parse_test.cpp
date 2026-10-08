// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/system.h>

#include <engine/shared/json.h>

#include <game/client/components/qmclient/translate/translate_parse.h>

#include <gtest/gtest.h>
#include <test/test.h>

// 通过生产解析器验证服务返回的成功内容与失败分类。

TEST(ParseLlmResponseJson, RefusalCannotBecomeTranslation)
{
	const char *pText = R"({"choices":[{"message":{"content":"partial","refusal":"Long provider explanation"}}]})";
	json_value *pJson = json_parse(pText, str_length(pText));
	SLlmParseResult Result;
	EXPECT_FALSE(ParseLlmResponseJson(pJson, Result));
	EXPECT_TRUE(Result.m_Refused);
	EXPECT_STREQ(Result.m_aText, "");
	EXPECT_EQ(str_find(Result.m_aError, "Long provider"), nullptr);
	json_value_free(pJson);
}

TEST(ParseLlmResponseJson, ContentFilteredFinishDiscardsPartialText)
{
	const char *pText = R"({"choices":[{"finish_reason":"content_filter","message":{"content":"partial"}}]})";
	json_value *pJson = json_parse(pText, str_length(pText));
	SLlmParseResult Result;
	EXPECT_FALSE(ParseLlmResponseJson(pJson, Result));
	EXPECT_TRUE(Result.m_Refused);
	EXPECT_STREQ(Result.m_aText, "");
	json_value_free(pJson);
}

TEST(ParseLlmResponseJson, NullRefusalDoesNotRejectOrdinaryText)
{
	const char *pText = R"({"error":null,"choices":[{"message":{"content":"I cannot translate this sentence.","refusal":null}}]})";
	json_value *pJson = json_parse(pText, str_length(pText));
	SLlmParseResult Result;
	EXPECT_TRUE(ParseLlmResponseJson(pJson, Result));
	EXPECT_FALSE(Result.m_Refused);
	EXPECT_STREQ(Result.m_aText, "I cannot translate this sentence.");
	json_value_free(pJson);
}

// 测试：有效的 LLM 响应
TEST(ParseLlmResponseJson, ValidResponse)
{
	const char aJson[] =
		"{"
		"\"choices\": [{"
		"\"message\": {"
		"\"content\": \"你好世界\","
		"\"role\": \"assistant\""
		"}"
		"}]"
		"}";

	json_value *pJson = json_parse(aJson, str_length(aJson));
	ASSERT_NE(pJson, nullptr);

	SLlmParseResult Result;
	bool Success = ParseLlmResponseJson(pJson, Result);

	EXPECT_TRUE(Success);
	EXPECT_TRUE(Result.m_Success);
	EXPECT_STREQ(Result.m_aText, "你好世界");

	json_value_free(pJson);
}

// 测试：null JSON 输入
TEST(ParseLlmResponseJson, NullInput)
{
	SLlmParseResult Result;
	bool Success = ParseLlmResponseJson(nullptr, Result);

	EXPECT_FALSE(Success);
	EXPECT_FALSE(Result.m_Success);
	EXPECT_STREQ(Result.m_aError, "Response is not valid JSON");
}

// 测试：非对象 JSON（数组）
TEST(ParseLlmResponseJson, NonObjectJson)
{
	const char aJson[] = "[1, 2, 3]";

	json_value *pJson = json_parse(aJson, str_length(aJson));
	ASSERT_NE(pJson, nullptr);

	SLlmParseResult Result;
	bool Success = ParseLlmResponseJson(pJson, Result);

	EXPECT_FALSE(Success);
	EXPECT_FALSE(Result.m_Success);
	EXPECT_STREQ(Result.m_aError, "Response is not a JSON object");

	json_value_free(pJson);
}

// 测试：包含 error 字段的响应
TEST(ParseLlmResponseJson, ErrorResponse)
{
	const char aJson[] =
		"{"
		"\"error\": {"
		"\"message\": \"Invalid API key\","
		"\"type\": \"authentication_error\""
		"}"
		"}";

	json_value *pJson = json_parse(aJson, str_length(aJson));
	ASSERT_NE(pJson, nullptr);

	SLlmParseResult Result;
	bool Success = ParseLlmResponseJson(pJson, Result);

	EXPECT_FALSE(Success);
	EXPECT_FALSE(Result.m_Success);
	EXPECT_STREQ(Result.m_aError, "Invalid API key");

	json_value_free(pJson);
}

// 测试：error 字段但没有 message
TEST(ParseLlmResponseJson, ErrorWithoutMessage)
{
	const char aJson[] =
		"{"
		"\"error\": {"
		"\"type\": \"unknown_error\""
		"}"
		"}";

	json_value *pJson = json_parse(aJson, str_length(aJson));
	ASSERT_NE(pJson, nullptr);

	SLlmParseResult Result;
	bool Success = ParseLlmResponseJson(pJson, Result);

	EXPECT_FALSE(Success);
	EXPECT_FALSE(Result.m_Success);
	EXPECT_STREQ(Result.m_aError, "LLM API request failed");

	json_value_free(pJson);
}

// 测试：缺少 choices 字段
TEST(ParseLlmResponseJson, MissingChoices)
{
	const char aJson[] = "{}";

	json_value *pJson = json_parse(aJson, str_length(aJson));
	ASSERT_NE(pJson, nullptr);

	SLlmParseResult Result;
	bool Success = ParseLlmResponseJson(pJson, Result);

	EXPECT_FALSE(Success);
	EXPECT_FALSE(Result.m_Success);
	EXPECT_STREQ(Result.m_aError, "No choices in response");

	json_value_free(pJson);
}

// 测试：智谱 AI 格式的错误（code/msg）
TEST(ParseLlmResponseJson, ZhipuErrorFormat)
{
	const char aJson[] =
		"{"
		"\"code\": \"1001\","
		"\"msg\": \"API rate limit exceeded\""
		"}";

	json_value *pJson = json_parse(aJson, str_length(aJson));
	ASSERT_NE(pJson, nullptr);

	SLlmParseResult Result;
	bool Success = ParseLlmResponseJson(pJson, Result);

	EXPECT_FALSE(Success);
	EXPECT_FALSE(Result.m_Success);
	EXPECT_STREQ(Result.m_aError, "No choices in response (code: 1001, API rate limit exceeded)");

	json_value_free(pJson);
}

// 测试：choices 不是数组
TEST(ParseLlmResponseJson, ChoicesNotArray)
{
	const char aJson[] =
		"{"
		"\"choices\": \"not an array\""
		"}";

	json_value *pJson = json_parse(aJson, str_length(aJson));
	ASSERT_NE(pJson, nullptr);

	SLlmParseResult Result;
	bool Success = ParseLlmResponseJson(pJson, Result);

	EXPECT_FALSE(Success);
	EXPECT_FALSE(Result.m_Success);
	EXPECT_STREQ(Result.m_aError, "choices is not array");

	json_value_free(pJson);
}

// 测试：choices 是空数组
TEST(ParseLlmResponseJson, EmptyChoices)
{
	const char aJson[] =
		"{"
		"\"choices\": []"
		"}";

	json_value *pJson = json_parse(aJson, str_length(aJson));
	ASSERT_NE(pJson, nullptr);

	SLlmParseResult Result;
	bool Success = ParseLlmResponseJson(pJson, Result);

	EXPECT_FALSE(Success);
	EXPECT_FALSE(Result.m_Success);
	EXPECT_STREQ(Result.m_aError, "choices is empty");

	json_value_free(pJson);
}

// 测试：choice 不是对象
TEST(ParseLlmResponseJson, ChoiceNotObject)
{
	const char aJson[] =
		"{"
		"\"choices\": [\"not an object\"]"
		"}";

	json_value *pJson = json_parse(aJson, str_length(aJson));
	ASSERT_NE(pJson, nullptr);

	SLlmParseResult Result;
	bool Success = ParseLlmResponseJson(pJson, Result);

	EXPECT_FALSE(Success);
	EXPECT_FALSE(Result.m_Success);
	EXPECT_STREQ(Result.m_aError, "choice is not object");

	json_value_free(pJson);
}

// 测试：缺少 message 字段
TEST(ParseLlmResponseJson, MissingMessage)
{
	const char aJson[] =
		"{"
		"\"choices\": [{\"index\": 0}]"
		"}";

	json_value *pJson = json_parse(aJson, str_length(aJson));
	ASSERT_NE(pJson, nullptr);

	SLlmParseResult Result;
	bool Success = ParseLlmResponseJson(pJson, Result);

	EXPECT_FALSE(Success);
	EXPECT_FALSE(Result.m_Success);
	EXPECT_STREQ(Result.m_aError, "No message in choice");

	json_value_free(pJson);
}

// 测试：message 不是对象
TEST(ParseLlmResponseJson, MessageNotObject)
{
	const char aJson[] =
		"{"
		"\"choices\": [{\"message\": \"not an object\"}]"
		"}";

	json_value *pJson = json_parse(aJson, str_length(aJson));
	ASSERT_NE(pJson, nullptr);

	SLlmParseResult Result;
	bool Success = ParseLlmResponseJson(pJson, Result);

	EXPECT_FALSE(Success);
	EXPECT_FALSE(Result.m_Success);
	EXPECT_STREQ(Result.m_aError, "message is not object");

	json_value_free(pJson);
}

// 测试：缺少 content 字段
TEST(ParseLlmResponseJson, MissingContent)
{
	const char aJson[] =
		"{"
		"\"choices\": [{\"message\": {\"role\": \"assistant\"}}]"
		"}";

	json_value *pJson = json_parse(aJson, str_length(aJson));
	ASSERT_NE(pJson, nullptr);

	SLlmParseResult Result;
	bool Success = ParseLlmResponseJson(pJson, Result);

	EXPECT_FALSE(Success);
	EXPECT_FALSE(Result.m_Success);
	EXPECT_STREQ(Result.m_aError, "No content in message");

	json_value_free(pJson);
}

// 测试：content 不是字符串
TEST(ParseLlmResponseJson, ContentNotString)
{
	const char aJson[] =
		"{"
		"\"choices\": [{\"message\": {\"content\": 123, \"role\": \"assistant\"}}]"
		"}";

	json_value *pJson = json_parse(aJson, str_length(aJson));
	ASSERT_NE(pJson, nullptr);

	SLlmParseResult Result;
	bool Success = ParseLlmResponseJson(pJson, Result);

	EXPECT_FALSE(Success);
	EXPECT_FALSE(Result.m_Success);
	EXPECT_STREQ(Result.m_aError, "content is not string");

	json_value_free(pJson);
}

// 测试：content 是空字符串
TEST(ParseLlmResponseJson, EmptyContent)
{
	const char aJson[] =
		"{"
		"\"choices\": [{"
		"\"message\": {"
		"\"content\": \"\","
		"\"role\": \"assistant\""
		"}"
		"}]"
		"}";

	json_value *pJson = json_parse(aJson, str_length(aJson));
	ASSERT_NE(pJson, nullptr);

	SLlmParseResult Result;
	bool Success = ParseLlmResponseJson(pJson, Result);

	EXPECT_TRUE(Success);
	EXPECT_TRUE(Result.m_Success);
	EXPECT_STREQ(Result.m_aText, "");

	json_value_free(pJson);
}

// 测试：content 包含长文本
TEST(ParseLlmResponseJson, LongContent)
{
	char aJson[512];
	str_format(aJson, sizeof(aJson),
		"{"
		"\"choices\": [{"
		"\"message\": {"
		"\"content\": \"%s\","
		"\"role\": \"assistant\""
		"}"
		"}]"
		"}",
		"这是一个很长的翻译结果，用来测试缓冲区是否足够大");

	json_value *pJson = json_parse(aJson, str_length(aJson));
	ASSERT_NE(pJson, nullptr);

	SLlmParseResult Result;
	bool Success = ParseLlmResponseJson(pJson, Result);

	EXPECT_TRUE(Success);
	EXPECT_TRUE(Result.m_Success);
	EXPECT_STREQ(Result.m_aText, "这是一个很长的翻译结果，用来测试缓冲区是否足够大");

	json_value_free(pJson);
}

// 测试：端点归一——完整 chat/completions URL 剥离为 base URL 并判定 Chat 格式
TEST(NormalizeLlmEndpoint, FullChatCompletionsUrl)
{
	SLlmEndpointInfo Info;
	ASSERT_TRUE(NormalizeLlmEndpoint("https://api.ziliao.xyz/v1/chat/completions", Info));
	EXPECT_STREQ(Info.m_aBaseUrl, "https://api.ziliao.xyz/v1");
	EXPECT_EQ(Info.m_Style, ELlmApiStyle::CHAT);
}

// 测试：端点归一——只填到 /v1 的 base URL，格式为 AUTO
TEST(NormalizeLlmEndpoint, BaseUrlWithV1)
{
	SLlmEndpointInfo Info;
	ASSERT_TRUE(NormalizeLlmEndpoint("https://api.ziliao.xyz/v1", Info));
	EXPECT_STREQ(Info.m_aBaseUrl, "https://api.ziliao.xyz/v1");
	EXPECT_EQ(Info.m_Style, ELlmApiStyle::AUTO);
}

// 测试：端点归一——responses 后缀判定 Responses 格式
TEST(NormalizeLlmEndpoint, ResponsesUrl)
{
	SLlmEndpointInfo Info;
	ASSERT_TRUE(NormalizeLlmEndpoint("https://api.openai.com/v1/responses", Info));
	EXPECT_STREQ(Info.m_aBaseUrl, "https://api.openai.com/v1");
	EXPECT_EQ(Info.m_Style, ELlmApiStyle::RESPONSES);
}

// 测试：端点归一——chat/completion 少写 s 的笔误同样识别
TEST(NormalizeLlmEndpoint, ChatCompletionTypo)
{
	SLlmEndpointInfo Info;
	ASSERT_TRUE(NormalizeLlmEndpoint("https://relay.example.com/v1/chat/completion", Info));
	EXPECT_STREQ(Info.m_aBaseUrl, "https://relay.example.com/v1");
	EXPECT_EQ(Info.m_Style, ELlmApiStyle::CHAT);
}

// 测试：端点归一——尾斜杠、首尾空白与大小写容错
TEST(NormalizeLlmEndpoint, TrailingSlashAndWhitespace)
{
	SLlmEndpointInfo Info;
	ASSERT_TRUE(NormalizeLlmEndpoint("  HTTPS://Api.Example.com/v1/CHAT/COMPLETIONS/  ", Info));
	EXPECT_STREQ(Info.m_aBaseUrl, "HTTPS://Api.Example.com/v1");
	EXPECT_EQ(Info.m_Style, ELlmApiStyle::CHAT);
}

// 测试：端点归一——非法输入拒绝
TEST(NormalizeLlmEndpoint, RejectsInvalid)
{
	SLlmEndpointInfo Info;
	EXPECT_FALSE(NormalizeLlmEndpoint("", Info));
	EXPECT_FALSE(NormalizeLlmEndpoint(nullptr, Info));
	EXPECT_FALSE(NormalizeLlmEndpoint("api.example.com/v1", Info));
	EXPECT_FALSE(NormalizeLlmEndpoint("ftp://api.example.com", Info));
	EXPECT_FALSE(NormalizeLlmEndpoint("   ", Info));
}
