#include "test.h"

#include <base/system.h>
#include <engine/shared/json.h>
#include <game/client/components/qmclient/translate_parse.h>

#include <gtest/gtest.h>

// 测试新的 ParseLlmResponseJson 函数

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
