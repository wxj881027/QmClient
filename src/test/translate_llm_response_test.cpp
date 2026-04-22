#include "test.h"

#include <base/system.h>
#include <engine/shared/json.h>

#include <gtest/gtest.h>

// 模拟 LLM API 的各种响应场景

// 测试：当 API 返回有效的 LLM 响应时，能正确解析翻译结果
TEST(TranslateLlmResponse, ParseValidResponse)
{
	// 模拟标准的 OpenAI/智谱 AI 响应格式
	const char aValidResponse[] =
		"{"
		"\"choices\": [{"
		"\"message\": {"
		"\"content\": \"你好世界\","
		"\"role\": \"assistant\""
		"}"
		"}]"
		"}";

	json_value *pJson = json_parse(aValidResponse, str_length(aValidResponse));
	ASSERT_NE(pJson, nullptr);
	EXPECT_EQ(pJson->type, json_object);

	const json_value *pChoices = json_object_get(pJson, "choices");
	ASSERT_NE(pChoices, &json_value_none);
	EXPECT_EQ(pChoices->type, json_array);
	EXPECT_EQ(pChoices->u.array.length, 1u);

	const json_value *pChoice = pChoices->u.array.values[0];
	ASSERT_EQ(pChoice->type, json_object);

	const json_value *pMessage = json_object_get(pChoice, "message");
	ASSERT_NE(pMessage, &json_value_none);
	EXPECT_EQ(pMessage->type, json_object);

	const json_value *pContent = json_object_get(pMessage, "content");
	ASSERT_NE(pContent, &json_value_none);
	EXPECT_EQ(pContent->type, json_string);
	EXPECT_STREQ(pContent->u.string.ptr, "你好世界");

	json_value_free(pJson);
}

// 测试：当 API 返回非 JSON 响应（如 HTML 错误页面）时，应给出清晰的错误信息
TEST(TranslateLlmResponse, ParseInvalidJsonReturnsNull)
{
	// 模拟 HTML 错误页面响应（如 502 Bad Gateway）
	const char aHtmlResponse[] =
		"<html>"
		"<head><title>502 Bad Gateway</title></head>"
		"<body>"
		"<center><h1>502 Bad Gateway</h1></center>"
		"</body>"
		"</html>";

	json_value *pJson = json_parse(aHtmlResponse, str_length(aHtmlResponse));
	// json_parse 在解析失败时返回 nullptr
	EXPECT_EQ(pJson, nullptr);
}

// 测试：当 API 返回错误字段时，应正确提取错误信息
TEST(TranslateLlmResponse, ParseErrorResponse)
{
	// 模拟 API 返回的错误响应
	const char aErrorResponse[] =
		"{"
		"\"error\": {"
		"\"message\": \"Invalid API key\","
		"\"type\": \"authentication_error\""
		"}"
		"}";

	json_value *pJson = json_parse(aErrorResponse, str_length(aErrorResponse));
	ASSERT_NE(pJson, nullptr);
	EXPECT_EQ(pJson->type, json_object);

	const json_value *pError = json_object_get(pJson, "error");
	ASSERT_NE(pError, &json_value_none);
	EXPECT_EQ(pError->type, json_object);

	const json_value *pMessage = json_object_get(pError, "message");
	ASSERT_NE(pMessage, &json_value_none);
	EXPECT_EQ(pMessage->type, json_string);
	EXPECT_STREQ(pMessage->u.string.ptr, "Invalid API key");

	json_value_free(pJson);
}

// 测试：当 choices 为空数组时
TEST(TranslateLlmResponse, EmptyChoicesArray)
{
	const char aEmptyChoices[] =
		"{"
		"\"choices\": []"
		"}";

	json_value *pJson = json_parse(aEmptyChoices, str_length(aEmptyChoices));
	ASSERT_NE(pJson, nullptr);

	const json_value *pChoices = json_object_get(pJson, "choices");
	ASSERT_NE(pChoices, &json_value_none);
	EXPECT_EQ(pChoices->type, json_array);
	EXPECT_EQ(pChoices->u.array.length, 0u);

	json_value_free(pJson);
}

// 测试：当 content 为空字符串时
TEST(TranslateLlmResponse, EmptyContent)
{
	const char aEmptyContent[] =
		"{"
		"\"choices\": [{"
		"\"message\": {"
		"\"content\": \"\","
		"\"role\": \"assistant\""
		"}"
		"}]"
		"}";

	json_value *pJson = json_parse(aEmptyContent, str_length(aEmptyContent));
	ASSERT_NE(pJson, nullptr);

	const json_value *pChoices = json_object_get(pJson, "choices");
	const json_value *pChoice = pChoices->u.array.values[0];
	const json_value *pMessage = json_object_get(pChoice, "message");
	const json_value *pContent = json_object_get(pMessage, "content");

	EXPECT_EQ(pContent->type, json_string);
	EXPECT_STREQ(pContent->u.string.ptr, "");

	json_value_free(pJson);
}

// 测试：截断的 JSON 响应（模拟网络中断）
TEST(TranslateLlmResponse, TruncatedJson)
{
	// 模拟不完整的 JSON 响应
	const char aTruncated[] =
		"{\"choices\": [{\"message\": {\"content\": \"不完整\"";

	json_value *pJson = json_parse(aTruncated, str_length(aTruncated));
	// 截断的 JSON 应该返回 nullptr 或部分解析的结果
	// 实际行为取决于 json_parse 的实现

	// 这个测试验证我们能检测到不完整的响应
	// 实际处理中应该检查 ResultJson() 的返回值
	if(pJson != nullptr)
	{
		// 如果解析器尝试容错解析，我们需要检查结构是否完整
		const json_value *pChoices = json_object_get(pJson, "choices");
		EXPECT_EQ(pChoices, &json_value_none);
		json_value_free(pJson);
	}
}

// 测试：智谱 AI 特定的错误响应格式
TEST(TranslateLlmResponse, ZhipuErrorFormat)
{
	// 智谱 AI 可能使用 code/msg 格式的错误
	const char aZhipuError[] =
		"{"
		"\"code\": \"1001\","
		"\"msg\": \"API rate limit exceeded\""
		"}";

	json_value *pJson = json_parse(aZhipuError, str_length(aZhipuError));
	ASSERT_NE(pJson, nullptr);

	const json_value *pCode = json_object_get(pJson, "code");
	ASSERT_NE(pCode, &json_value_none);
	EXPECT_EQ(pCode->type, json_string);
	EXPECT_STREQ(pCode->u.string.ptr, "1001");

	const json_value *pMsg = json_object_get(pJson, "msg");
	ASSERT_NE(pMsg, &json_value_none);
	EXPECT_EQ(pMsg->type, json_string);
	EXPECT_STREQ(pMsg->u.string.ptr, "API rate limit exceeded");

	json_value_free(pJson);
}

// 测试：非 JSON 响应（以 'g' 开头，对应截图中的错误）
TEST(TranslateLlmResponse, NonJsonStartingWithG)
{
	// 模拟 "gateway error" 或其他以 g 开头的非 JSON 响应
	const char aGatewayError[] = "gateway timeout";

	json_value *pJson = json_parse(aGatewayError, str_length(aGatewayError));
	EXPECT_EQ(pJson, nullptr);
}

// 测试：空响应
TEST(TranslateLlmResponse, EmptyResponse)
{
	const char aEmpty[] = "";

	json_value *pJson = json_parse(aEmpty, str_length(aEmpty));
	EXPECT_EQ(pJson, nullptr);
}

// 测试：仅包含空白字符的响应
TEST(TranslateLlmResponse, WhitespaceOnlyResponse)
{
	const char aWhitespace[] = "   \n\t  ";

	json_value *pJson = json_parse(aWhitespace, str_length(aWhitespace));
	EXPECT_EQ(pJson, nullptr);
}
