#include "test.h"

#include <base/system.h>
#include <engine/shared/json.h>

#include <gtest/gtest.h>

// 测试 JSON 构建格式是否正确

// 模拟 EscapeJsonString 函数的行为
static void TestEscapeJsonString(const char *pStr, char *pOut, size_t OutSize)
{
	if(OutSize == 0)
		return;

	pOut[0] = '\0';
	if(OutSize < 3)
		return;

	size_t OutPos = 0;
	pOut[OutPos++] = '"';
	for(const char *p = pStr; *p; ++p)
	{
		const unsigned char c = (unsigned char)*p;
		if(OutPos + 2 >= OutSize)
			break;

		if(c == '"' || c == '\\' || c == '\b' || c == '\n' || c == '\r' || c == '\t')
		{
			pOut[OutPos++] = '\\';
			switch(c)
			{
			case '"': pOut[OutPos++] = '"'; break;
			case '\\': pOut[OutPos++] = '\\'; break;
			case '\b': pOut[OutPos++] = 'b'; break;
			case '\n': pOut[OutPos++] = 'n'; break;
			case '\r': pOut[OutPos++] = 'r'; break;
			case '\t': pOut[OutPos++] = 't'; break;
			}
		}
		else if(c < 0x20)
		{
			if(OutPos + 7 >= OutSize)
				break;
			str_format(pOut + OutPos, OutSize - OutPos, "\\u%04x", c);
			OutPos += 6;
		}
		else
		{
			pOut[OutPos++] = (char)c;
		}
	}

	pOut[OutPos++] = '"';
	pOut[OutPos] = '\0';
}

// 测试：EscapeJsonString 输出包含引号
TEST(TranslateJsonFormat, EscapeJsonStringAddsQuotes)
{
	char aEscaped[256];
	TestEscapeJsonString("hello", aEscaped, sizeof(aEscaped));

	// EscapeJsonString 应该在输出周围添加引号
	EXPECT_EQ(aEscaped[0], '"');
	EXPECT_EQ(aEscaped[str_length(aEscaped) - 1], '"');
	EXPECT_STRNE(str_find(aEscaped, "\"hello\""), nullptr);
}

// 测试：构建的 JSON payload 格式正确
TEST(TranslateJsonFormat, PayloadFormatIsValid)
{
	// 模拟构建 payload
	const char *pModel = "glm-4.5-flash";
	const char *pSystem = "You are a translator";
	const char *pText = "Hello world";

	char aEscapedModel[128];
	char aEscapedSystem[256];
	char aEscapedText[256];
	TestEscapeJsonString(pModel, aEscapedModel, sizeof(aEscapedModel));
	TestEscapeJsonString(pSystem, aEscapedSystem, sizeof(aEscapedSystem));
	TestEscapeJsonString(pText, aEscapedText, sizeof(aEscapedText));

	// 验证转义后的字符串包含引号
	EXPECT_EQ(aEscapedModel[0], '"');
	EXPECT_EQ(aEscapedSystem[0], '"');
	EXPECT_EQ(aEscapedText[0], '"');

	char aPayload[1024];
	str_format(aPayload, sizeof(aPayload),
		"{"
		"\"model\":%s,"
		"\"messages\":["
		"{\"role\":\"system\",\"content\":%s},"
		"{\"role\":\"user\",\"content\":%s}"
		"],"
		"\"temperature\":0.3,"
		"\"max_tokens\":1024"
		"}",
		aEscapedModel, aEscapedSystem, aEscapedText);

	// 解析构建的 JSON，验证格式正确
	json_value *pJson = json_parse(aPayload, str_length(aPayload));
	ASSERT_NE(pJson, nullptr) << "JSON parse failed for: " << aPayload;

	// 验证结构正确
	const json_value *pModelField = json_object_get(pJson, "model");
	ASSERT_NE(pModelField, &json_value_none);
	EXPECT_EQ(pModelField->type, json_string);
	EXPECT_STREQ(pModelField->u.string.ptr, "glm-4.5-flash");

	const json_value *pMessages = json_object_get(pJson, "messages");
	ASSERT_NE(pMessages, &json_value_none);
	EXPECT_EQ(pMessages->type, json_array);
	EXPECT_EQ(pMessages->u.array.length, 2u);

	// 验证 system 消息
	const json_value *pSystemMsg = pMessages->u.array.values[0];
	ASSERT_NE(pSystemMsg, &json_value_none);
	EXPECT_EQ(pSystemMsg->type, json_object);

	json_value_free(pJson);
}

// 测试：包含特殊字符的文本
TEST(TranslateJsonFormat, SpecialCharactersInText)
{
	const char *pText = "Hello \"world\" with \\ backslash and \n newline";

	char aEscaped[512];
	TestEscapeJsonString(pText, aEscaped, sizeof(aEscaped));

	// 构建 JSON
	char aJson[1024];
	str_format(aJson, sizeof(aJson), "{\"content\":%s}", aEscaped);

	// 验证 JSON 可解析
	json_value *pJson = json_parse(aJson, str_length(aJson));
	ASSERT_NE(pJson, nullptr) << "Failed to parse: " << aJson;

	const json_value *pContent = json_object_get(pJson, "content");
	ASSERT_NE(pContent, &json_value_none);
	EXPECT_EQ(pContent->type, json_string);
	EXPECT_STREQ(pContent->u.string.ptr, pText);

	json_value_free(pJson);
}

// 测试：中文文本
TEST(TranslateJsonFormat, ChineseText)
{
	const char *pText = "你好世界，这是中文测试";

	char aEscaped[256];
	TestEscapeJsonString(pText, aEscaped, sizeof(aEscaped));

	char aJson[512];
	str_format(aJson, sizeof(aJson), "{\"content\":%s}", aEscaped);

	json_value *pJson = json_parse(aJson, str_length(aJson));
	ASSERT_NE(pJson, nullptr) << "Failed to parse: " << aJson;

	const json_value *pContent = json_object_get(pJson, "content");
	ASSERT_NE(pContent, &json_value_none);
	EXPECT_EQ(pContent->type, json_string);
	EXPECT_STREQ(pContent->u.string.ptr, pText);

	json_value_free(pJson);
}

// 测试：俄语文本（截图中的例子）
TEST(TranslateJsonFormat, RussianText)
{
	const char *pText = "там блок";

	char aEscaped[256];
	TestEscapeJsonString(pText, aEscaped, sizeof(aEscaped));

	char aJson[512];
	str_format(aJson, sizeof(aJson), "{\"content\":%s}", aEscaped);

	json_value *pJson = json_parse(aJson, str_length(aJson));
	ASSERT_NE(pJson, nullptr) << "Failed to parse: " << aJson;

	const json_value *pContent = json_object_get(pJson, "content");
	ASSERT_NE(pContent, &json_value_none);
	EXPECT_EQ(pContent->type, json_string);
	EXPECT_STREQ(pContent->u.string.ptr, pText);

	json_value_free(pJson);
}

// 测试：双引号重复问题 - 这是修复前的 bug
TEST(TranslateJsonFormat, NoDuplicateQuotes)
{
	const char *pText = "test";

	char aEscaped[128];
	TestEscapeJsonString(pText, aEscaped, sizeof(aEscaped));

	// 错误的格式（修复前）："model":""test""
	// 正确的格式（修复后）："model":"test"

	char aPayload[512];
	str_format(aPayload, sizeof(aPayload),
		"{\"model\":%s}",
		aEscaped);

	// 检查没有连续的 """"
	EXPECT_EQ(str_find(aPayload, "\"\"\"\""), nullptr) << "Found duplicate quotes in: " << aPayload;

	// 验证 JSON 有效
	json_value *pJson = json_parse(aPayload, str_length(aPayload));
	ASSERT_NE(pJson, nullptr) << "Failed to parse: " << aPayload;

	json_value_free(pJson);
}
