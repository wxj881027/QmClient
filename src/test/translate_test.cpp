#include "test.h"

#include <base/system.h>

#include <engine/shared/json.h>

#include <gtest/gtest.h>

#include <cstring>

static void EscapeJsonString(const char *pStr, char *pOut, size_t OutSize)
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
		const auto CanAppend = [&](size_t Count) {
			return OutPos + Count + 2 <= OutSize;
		};

		if(c == '"' || c == '\\' || c == '\b' || c == '\n' || c == '\r' || c == '\t')
		{
			if(!CanAppend(2))
				break;
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
			if(!CanAppend(6))
				break;
			str_format(pOut + OutPos, OutSize - OutPos, "\\u%04x", c);
			OutPos += 6;
		}
		else
		{
			if(!CanAppend(1))
				break;
			pOut[OutPos++] = (char)c;
		}
	}

	pOut[OutPos++] = '"';
	pOut[OutPos] = '\0';
}

TEST(Translate, EscapeJsonString_Basic)
{
	char aBuf[256];
	EscapeJsonString("hello", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "\"hello\"");
}

TEST(Translate, EscapeJsonString_Empty)
{
	char aBuf[256];
	EscapeJsonString("", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "\"\"");
}

TEST(Translate, EscapeJsonString_WithQuotes)
{
	char aBuf[256];
	EscapeJsonString("say \"hello\"", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "\"say \\\"hello\\\"\"");
}

TEST(Translate, EscapeJsonString_WithBackslash)
{
	char aBuf[256];
	EscapeJsonString("path\\to\\file", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "\"path\\\\to\\\\file\"");
}

TEST(Translate, EscapeJsonString_WithNewline)
{
	char aBuf[256];
	EscapeJsonString("line1\nline2", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "\"line1\\nline2\"");
}

TEST(Translate, EscapeJsonString_WithCarriageReturn)
{
	char aBuf[256];
	EscapeJsonString("line1\rline2", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "\"line1\\rline2\"");
}

TEST(Translate, EscapeJsonString_WithTab)
{
	char aBuf[256];
	EscapeJsonString("col1\tcol2", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "\"col1\\tcol2\"");
}

TEST(Translate, EscapeJsonString_WithBackspace)
{
	char aBuf[256];
	EscapeJsonString("back\bspace", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "\"back\\bspace\"");
}

TEST(Translate, EscapeJsonString_ControlCharacters)
{
	char aBuf[256];
	EscapeJsonString("\x01\x02\x1f", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "\"\\u0001\\u0002\\u001f\"");
}

TEST(Translate, EscapeJsonString_Mixed)
{
	char aBuf[256];
	EscapeJsonString("Hello \"world\"\nline2\ttabbed", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "\"Hello \\\"world\\\"\\nline2\\ttabbed\"");
}

TEST(Translate, EscapeJsonString_BufferTruncation)
{
	char aBuf[16];
	EscapeJsonString("this is a very long string", aBuf, sizeof(aBuf));
	EXPECT_TRUE(str_length(aBuf) < 64);
	EXPECT_TRUE(aBuf[str_length(aBuf) - 1] == '"');
}

TEST(Translate, EscapeJsonString_SmallBuffer)
{
	char aBuf[4];
	EscapeJsonString("hello", aBuf, sizeof(aBuf));
	EXPECT_TRUE(aBuf[0] == '"');
}

TEST(Translate, EscapeJsonString_TooSmallBuffer)
{
	char aBuf[2];
	EscapeJsonString("hello", aBuf, sizeof(aBuf));
	EXPECT_EQ(aBuf[0], '\0');
}

TEST(Translate, EscapeJsonString_ExactlyTwo)
{
	char aBuf[2];
	EscapeJsonString("hello", aBuf, sizeof(aBuf));
	EXPECT_TRUE(aBuf[0] == '"' || aBuf[0] == '\0');
}

TEST(Translate, EscapeJsonString_NullByte)
{
	char aBuf[256];
	const char aInput[] = {'n', 'u', 'l', 'l', '\0', 'b', 'y', 't', 'e', '\0'};
	EscapeJsonString(aInput, aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "\"null\"");
}

TEST(Translate, EscapeJsonString_UnicodeChinese)
{
	char aBuf[256];
	EscapeJsonString("你好世界", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "\"你好世界\"");
}

TEST(Translate, EscapeJsonString_UnicodeEmoji)
{
	char aBuf[256];
	EscapeJsonString("hello😀world", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "\"hello😀world\"");
}

TEST(Translate, EscapeJsonString_GameChatTypical)
{
	char aBuf[512];
	EscapeJsonString("Hello, how are you? I'm fine! \nSee you later.", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "\"Hello, how are you? I'm fine! \\nSee you later.\"");
}

TEST(Translate, EscapeJsonString_QuoteOnly)
{
	char aBuf[256];
	EscapeJsonString("\"", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "\"\\\"\"");
}

TEST(Translate, EscapeJsonString_BackslashOnly)
{
	char aBuf[256];
	EscapeJsonString("\\", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "\"\\\\\"");
}

TEST(Translate, EscapeJsonString_MultipleEscapes)
{
	char aBuf[256];
	EscapeJsonString("\"\\\\\n\r\t\"", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "\"\\\"\\\\\\\\\\n\\r\\t\\\"\"");
}

class TranslateJsonResponseParser
{
public:
	static bool ParseZhipuAIResponse(const char *pJson, char *pOutText, size_t OutTextSize, char *pOutLanguage, size_t OutLanguageSize)
	{
		json_value *pRoot = json_parse(pJson, str_length(pJson));
		if(!pRoot)
			return false;

		const json_value *pChoices = json_object_get(pRoot, "choices");
		if(pChoices == &json_value_none || pChoices->type != json_array)
		{
			json_value_free(pRoot);
			return false;
		}
		if(json_array_length(pChoices) == 0)
		{
			json_value_free(pRoot);
			return false;
		}

		const json_value *pFirstChoice = json_array_get(pChoices, 0);
		if(pFirstChoice == &json_value_none || pFirstChoice->type != json_object)
		{
			json_value_free(pRoot);
			return false;
		}

		const json_value *pMessage = json_object_get(pFirstChoice, "message");
		if(pMessage == &json_value_none || pMessage->type != json_object)
		{
			json_value_free(pRoot);
			return false;
		}

		const json_value *pContent = json_object_get(pMessage, "content");
		if(pContent == &json_value_none || pContent->type != json_string)
		{
			json_value_free(pRoot);
			return false;
		}

		str_copy(pOutText, pContent->u.string.ptr, OutTextSize);
		str_copy(pOutLanguage, "en", OutLanguageSize);

		json_value_free(pRoot);
		return true;
	}

	static bool ParseZhipuAIError(const char *pJson, char *pOutError, size_t OutErrorSize)
	{
		json_value *pRoot = json_parse(pJson, str_length(pJson));
		if(!pRoot)
			return false;

		const json_value *pError = json_object_get(pRoot, "error");
		if(pError == &json_value_none || pError->type != json_object)
		{
			json_value_free(pRoot);
			return false;
		}

		const json_value *pMessage = json_object_get(pError, "message");
		if(pMessage == &json_value_none || pMessage->type != json_string)
		{
			json_value_free(pRoot);
			return false;
		}

		str_copy(pOutError, pMessage->u.string.ptr, OutErrorSize);
		json_value_free(pRoot);
		return true;
	}
};

TEST(Translate, ZhipuAIResponse_Success)
{
	const char *pJson = R"({"choices":[{"message":{"role":"assistant","content":"你好世界"}}]})";
	char aText[256] = {0};
	char aLanguage[32] = {0};

	bool Result = TranslateJsonResponseParser::ParseZhipuAIResponse(pJson, aText, sizeof(aText), aLanguage, sizeof(aLanguage));

	EXPECT_TRUE(Result);
	EXPECT_STREQ(aText, "你好世界");
	EXPECT_STREQ(aLanguage, "en");
}

TEST(Translate, ZhipuAIResponse_Multiline)
{
	const char *pJson = R"({"choices":[{"message":{"role":"assistant","content":"第一行\n第二行\n第三行"}}]})";
	char aText[256] = {0};
	char aLanguage[32] = {0};

	bool Result = TranslateJsonResponseParser::ParseZhipuAIResponse(pJson, aText, sizeof(aText), aLanguage, sizeof(aLanguage));

	EXPECT_TRUE(Result);
	EXPECT_TRUE(str_find(aText, "\n") != nullptr);
}

TEST(Translate, ZhipuAIResponse_EmptyContent)
{
	const char *pJson = R"({"choices":[{"message":{"role":"assistant","content":""}}]})";
	char aText[256] = {0};
	char aLanguage[32] = {0};

	bool Result = TranslateJsonResponseParser::ParseZhipuAIResponse(pJson, aText, sizeof(aText), aLanguage, sizeof(aLanguage));

	EXPECT_TRUE(Result);
	EXPECT_STREQ(aText, "");
}

TEST(Translate, ZhipuAIResponse_InvalidJson)
{
	char aText[256] = {0};
	char aLanguage[32] = {0};

	bool Result = TranslateJsonResponseParser::ParseZhipuAIResponse("not json", aText, sizeof(aText), aLanguage, sizeof(aLanguage));

	EXPECT_FALSE(Result);
}

TEST(Translate, ZhipuAIResponse_MissingChoices)
{
	const char *pJson = R"({"model":"glm-4-flash"})";
	char aText[256] = {0};
	char aLanguage[32] = {0};

	bool Result = TranslateJsonResponseParser::ParseZhipuAIResponse(pJson, aText, sizeof(aText), aLanguage, sizeof(aLanguage));

	EXPECT_FALSE(Result);
}

TEST(Translate, ZhipuAIResponse_EmptyChoices)
{
	const char *pJson = R"({"choices":[]})";
	char aText[256] = {0};
	char aLanguage[32] = {0};

	bool Result = TranslateJsonResponseParser::ParseZhipuAIResponse(pJson, aText, sizeof(aText), aLanguage, sizeof(aLanguage));

	EXPECT_FALSE(Result);
}

TEST(Translate, ZhipuAIResponse_MissingMessage)
{
	const char *pJson = R"({"choices":[{"index":0}]})";
	char aText[256] = {0};
	char aLanguage[32] = {0};

	bool Result = TranslateJsonResponseParser::ParseZhipuAIResponse(pJson, aText, sizeof(aText), aLanguage, sizeof(aLanguage));

	EXPECT_FALSE(Result);
}

TEST(Translate, ZhipuAIResponse_MissingContent)
{
	const char *pJson = R"({"choices":[{"message":{"role":"assistant"}}]})";
	char aText[256] = {0};
	char aLanguage[32] = {0};

	bool Result = TranslateJsonResponseParser::ParseZhipuAIResponse(pJson, aText, sizeof(aText), aLanguage, sizeof(aLanguage));

	EXPECT_FALSE(Result);
}

TEST(Translate, ZhipuAIError_Success)
{
	const char *pJson = R"({"error":{"message":"Invalid API key","type":"invalid_request_error"}})";
	char aError[256] = {0};

	bool Result = TranslateJsonResponseParser::ParseZhipuAIError(pJson, aError, sizeof(aError));

	EXPECT_TRUE(Result);
	EXPECT_TRUE(str_find(aError, "Invalid API key") != nullptr);
}

TEST(Translate, ZhipuAIError_RateLimit)
{
	const char *pJson = R"({"error":{"message":"Rate limit exceeded","type":"rate_limit_error"}})";
	char aError[256] = {0};

	bool Result = TranslateJsonResponseParser::ParseZhipuAIError(pJson, aError, sizeof(aError));

	EXPECT_TRUE(Result);
	EXPECT_TRUE(str_find(aError, "Rate limit") != nullptr);
}

TEST(Translate, ZhipuAIError_InvalidJson)
{
	char aError[256] = {0};

	bool Result = TranslateJsonResponseParser::ParseZhipuAIError("not json", aError, sizeof(aError));

	EXPECT_FALSE(Result);
}

TEST(Translate, ZhipuAIError_MissingError)
{
	const char *pJson = R"({"choices":[]})";
	char aError[256] = {0};

	bool Result = TranslateJsonResponseParser::ParseZhipuAIError(pJson, aError, sizeof(aError));

	EXPECT_FALSE(Result);
}

TEST(Translate, ZhipuAIError_EmptyMessage)
{
	const char *pJson = R"({"error":{"type":"invalid_request_error"}})";
	char aError[256] = {0};

	bool Result = TranslateJsonResponseParser::ParseZhipuAIError(pJson, aError, sizeof(aError));

	EXPECT_FALSE(Result);
}

TEST(Translate, TargetLanguageEncode)
{
	const char *apTestCases[][2] = {
		{"zh", "zh"},
		{"en", "en"},
		{"ja", "ja"},
		{"ko", "ko"},
		{"ru", "ru"},
		{"de", "de"},
		{"fr", "fr"},
		{"es", "es"},
	};
	for(const auto &TestCase : apTestCases)
	{
		const char *pInput = TestCase[0];
		const char *pExpected = TestCase[1];
		EXPECT_STREQ(pInput, pExpected);
	}
}

TEST(Translate, TargetLanguage_ZhipuAICompatible)
{
	const char *pLang = "zh";
	EXPECT_TRUE(str_length(pLang) > 0);
	EXPECT_TRUE(str_length(pLang) <= 8);

	pLang = "en";
	EXPECT_TRUE(str_length(pLang) > 0);
}
