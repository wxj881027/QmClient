// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/str.h>

#include <engine/shared/json.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

static void TestParseValidation(const char *pJson, bool ExpectedSuccess)
{
	json_value *pParsed = JsonParse(pJson, str_length(pJson));
	EXPECT_EQ(pParsed != nullptr, ExpectedSuccess) << "Expected parsing to " << (ExpectedSuccess ? "succeed" : "fail") << " for '" << pJson << "'";
	json_value_free(pParsed);

	json_settings Settings{};
	char aError[json_error_max] = {};
	pParsed = JsonParseEx(&Settings, pJson, str_length(pJson), aError);
	EXPECT_EQ(pParsed != nullptr, ExpectedSuccess);
	if(!ExpectedSuccess)
		EXPECT_STREQ(aError, "invalid utf-8 in string literal");
	json_value_free(pParsed);
}

TEST(Json, ParseValidation)
{
	TestParseValidation("\"a\"", true);
	TestParseValidation("\"\xff\"", false);
	TestParseValidation("[\"a\"]", true);
	TestParseValidation("[\"\xff\"]", false);
	TestParseValidation("[[[[[\"a\"]]]]]", true);
	TestParseValidation("[[[[[\"\xff\"]]]]]", false);
	TestParseValidation("{\"a\": \"b\"}", true);
	TestParseValidation("{\"\xff\": \"b\"}", false);
	TestParseValidation("{\"a\": \"\xff\"}", false);
	TestParseValidation("{\"\xff\": \"\xff\"}", false);
	TestParseValidation("{\"a\": {\"a\": \"b\"}}", true);
	TestParseValidation("{\"a\": {\"\xff\": \"b\"}}", false);
	TestParseValidation("{\"a\": {\"a\": \"\xff\"}}", false);
	TestParseValidation("{\"a\": [{\"a\": \"b\"}]}", true);
	TestParseValidation("{\"a\": [{\"a\": \"\xff\"}]}", false);
}

TEST(Json, Escape)
{
	char aBuf[128];
	EXPECT_STREQ(EscapeJson(aBuf, sizeof(aBuf), ""), "");
	EXPECT_STREQ(EscapeJson(aBuf, sizeof(aBuf), " "), " ");
	EXPECT_STREQ(EscapeJson(aBuf, sizeof(aBuf), "a"), "a");
	EXPECT_STREQ(EscapeJson(aBuf, sizeof(aBuf), "\""), "\\\"");
	EXPECT_STREQ(EscapeJson(aBuf, sizeof(aBuf), "\\"), "\\\\"); // https://www.xkcd.com/1638/
	EXPECT_STREQ(EscapeJson(aBuf, sizeof(aBuf), "\b"), "\\b");
	EXPECT_STREQ(EscapeJson(aBuf, sizeof(aBuf), "\n"), "\\n");
	EXPECT_STREQ(EscapeJson(aBuf, sizeof(aBuf), "\r"), "\\r");
	EXPECT_STREQ(EscapeJson(aBuf, sizeof(aBuf), "\t"), "\\t");
	EXPECT_STREQ(EscapeJson(aBuf, sizeof(aBuf), "\x1b"), "\\u001b"); // escape
	EXPECT_STREQ(EscapeJson(aBuf, sizeof(aBuf), "愛"), "愛");
	EXPECT_STREQ(EscapeJson(aBuf, sizeof(aBuf), "😂"), "😂");

	// Truncations
	EXPECT_STREQ(EscapeJson(aBuf, 2, "\\"), "");
	EXPECT_STREQ(EscapeJson(aBuf, 3, "\\"), "\\\\");
	EXPECT_STREQ(EscapeJson(aBuf, 4, "\\"), "\\\\");
	EXPECT_STREQ(EscapeJson(aBuf, 6, "\x01"), "");
	EXPECT_STREQ(EscapeJson(aBuf, 7, "\x01"), "\\u0001");
	EXPECT_STREQ(EscapeJson(aBuf, 8, "\x01"), "\\u0001");
	EXPECT_STREQ(EscapeJson(aBuf, 5, "aaaaaa"), "aaaa");
	EXPECT_STREQ(EscapeJson(aBuf, 6, "aaaaaa"), "aaaaa");
	EXPECT_STREQ(EscapeJson(aBuf, 7, "aaaaaa"), "aaaaaa");
}

TEST(JsonDeathTest, DeepNestingDoesNotOverflowValidationStack)
{
	// 子进程隔离旧实现的栈溢出，避免终止整套测试。
	EXPECT_EXIT(
		{
			constexpr size_t Depth = 100000;
			const std::string Json = std::string(Depth, '[') + "\"valid\"" + std::string(Depth, ']');
			json_value *pParsed = JsonParse(Json.c_str(), Json.size());
			const bool Parsed = pParsed != nullptr;
			json_value_free(pParsed);
			std::exit(Parsed ? 0 : 1);
		},
		::testing::ExitedWithCode(0), "");
}

TEST(JsonDeathTest, DeepInvalidUtf8IsRejectedWithoutStackOverflow)
{
	EXPECT_EXIT(
		{
			constexpr size_t Depth = 100000;
			const std::string Json = std::string(Depth, '[') + "{\"value\":\"\xff\"}" + std::string(Depth, ']');
			json_settings Settings{};
			char aError[json_error_max] = {};
			json_value *pParsed = JsonParseEx(&Settings, Json.c_str(), Json.size(), aError);
			const bool Rejected = pParsed == nullptr && str_comp(aError, "invalid utf-8 in string literal") == 0;
			json_value_free(pParsed);
			std::exit(Rejected ? 0 : 1);
		},
		::testing::ExitedWithCode(0), "");
}
