#include "str_test_helpers.h"

#include <base/mem.h>
#include <base/str.h>
#include <base/windows.h>

#include <engine/server/server.h>

#include <game/gamecore.h>

#include <gtest/gtest.h>

#include <limits>
#include <vector>

TEST(StrWhitespace, TrimWords)
{
	const char *pStr1 = "aa bb ccc   dddd    eeeee";
	EXPECT_STREQ(str_trim_words(pStr1, 0), "aa bb ccc   dddd    eeeee");
	EXPECT_STREQ(str_trim_words(pStr1, 1), "bb ccc   dddd    eeeee");
	EXPECT_STREQ(str_trim_words(pStr1, 2), "ccc   dddd    eeeee");
	EXPECT_STREQ(str_trim_words(pStr1, 3), "dddd    eeeee");
	EXPECT_STREQ(str_trim_words(pStr1, 4), "eeeee");
	EXPECT_STREQ(str_trim_words(pStr1, 5), "");
	EXPECT_STREQ(str_trim_words(pStr1, 100), "");
	const char *pStr2 = "   aaa  bb   ";
	EXPECT_STREQ(str_trim_words(pStr2, 0), "aaa  bb   ");
	EXPECT_STREQ(str_trim_words(pStr2, 1), "bb   ");
	EXPECT_STREQ(str_trim_words(pStr2, 2), "");
	EXPECT_STREQ(str_trim_words(pStr2, 100), "");
	const char *pStr3 = "\n\naa  bb\t\tccc\r\n\r\ndddd";
	EXPECT_STREQ(str_trim_words(pStr3, 0), "aa  bb\t\tccc\r\n\r\ndddd");
	EXPECT_STREQ(str_trim_words(pStr3, 1), "bb\t\tccc\r\n\r\ndddd");
	EXPECT_STREQ(str_trim_words(pStr3, 2), "ccc\r\n\r\ndddd");
	EXPECT_STREQ(str_trim_words(pStr3, 3), "dddd");
	EXPECT_STREQ(str_trim_words(pStr3, 4), "");
	EXPECT_STREQ(str_trim_words(pStr3, 100), "");
	const char *pStr4 = "";
	EXPECT_STREQ(str_trim_words(pStr4, 0), "");
	EXPECT_STREQ(str_trim_words(pStr4, 1), "");
	EXPECT_STREQ(str_trim_words(pStr4, 2), "");
	const char *pStr5 = "     ";
	EXPECT_STREQ(str_trim_words(pStr5, 0), "");
	EXPECT_STREQ(str_trim_words(pStr5, 1), "");
	EXPECT_STREQ(str_trim_words(pStr5, 2), "");
}

TEST(StrWhitespace, HasCc)
{
	EXPECT_FALSE(str_has_cc(""));
	EXPECT_FALSE(str_has_cc("a"));
	EXPECT_FALSE(str_has_cc("Merhaba dünya!"));

	EXPECT_TRUE(str_has_cc("\n"));
	EXPECT_TRUE(str_has_cc("\r"));
	EXPECT_TRUE(str_has_cc("\t"));
	EXPECT_TRUE(str_has_cc("a\n"));
	EXPECT_TRUE(str_has_cc("a\rb"));
	EXPECT_TRUE(str_has_cc("\tb"));
	EXPECT_TRUE(str_has_cc("\n\n"));
	EXPECT_TRUE(str_has_cc("\x1C"));
	EXPECT_TRUE(str_has_cc("\x1D"));
	EXPECT_TRUE(str_has_cc("\x1E"));
	EXPECT_TRUE(str_has_cc("\x1F"));
}

TEST(StrWhitespace, SanitizeCc)
{
	TestInplace<str_sanitize_cc>("", "");
	TestInplace<str_sanitize_cc>("a", "a");
	TestInplace<str_sanitize_cc>("Merhaba dünya!", "Merhaba dünya!");
	TestInplace<str_sanitize_cc>("\n", " ");
	TestInplace<str_sanitize_cc>("\r", " ");
	TestInplace<str_sanitize_cc>("\t", " ");
	TestInplace<str_sanitize_cc>("a\n", "a ");
	TestInplace<str_sanitize_cc>("a\rb", "a b");
	TestInplace<str_sanitize_cc>("\tb", " b");
	TestInplace<str_sanitize_cc>("\n\n", "  ");
	TestInplace<str_sanitize_cc>("\x1C", " ");
	TestInplace<str_sanitize_cc>("\x1D", " ");
	TestInplace<str_sanitize_cc>("\x1E", " ");
	TestInplace<str_sanitize_cc>("\x1F", " ");
}

TEST(StrWhitespace, Sanitize)
{
	TestInplace<str_sanitize>("", "");
	TestInplace<str_sanitize>("a", "a");
	TestInplace<str_sanitize>("Merhaba dünya!", "Merhaba dünya!");
	TestInplace<str_sanitize>("\n", "\n");
	TestInplace<str_sanitize>("\r", "\r");
	TestInplace<str_sanitize>("\t", "\t");
	TestInplace<str_sanitize>("a\n", "a\n");
	TestInplace<str_sanitize>("a\rb", "a\rb");
	TestInplace<str_sanitize>("\tb", "\tb");
	TestInplace<str_sanitize>("\n\n", "\n\n");
	TestInplace<str_sanitize>("\x1C", " ");
	TestInplace<str_sanitize>("\x1D", " ");
	TestInplace<str_sanitize>("\x1E", " ");
	TestInplace<str_sanitize>("\x1F", " ");
}

TEST(StrWhitespace, CleanWhitespaces)
{
	TestInplace<str_clean_whitespaces>("aa bb ccc dddd eeeee", "aa bb ccc dddd eeeee");
	TestInplace<str_clean_whitespaces>("     ", "");
	TestInplace<str_clean_whitespaces>("     aa", "aa");
	TestInplace<str_clean_whitespaces>("aa     ", "aa");
	TestInplace<str_clean_whitespaces>("  aa   bb    ccc     dddd       eeeee    ", "aa bb ccc dddd eeeee");
}

TEST(StrWhitespace, SkipToWhitespace)
{
	char aBuf[64];
	str_copy(aBuf, "");
	EXPECT_EQ(str_skip_to_whitespace(aBuf), aBuf);
	EXPECT_EQ(str_skip_to_whitespace_const(aBuf), aBuf);
	str_copy(aBuf, "    a");
	EXPECT_EQ(str_skip_to_whitespace(aBuf), aBuf);
	EXPECT_EQ(str_skip_to_whitespace_const(aBuf), aBuf);
	str_copy(aBuf, "aaaa  b");
	EXPECT_EQ(str_skip_to_whitespace(aBuf), aBuf + 4);
	EXPECT_EQ(str_skip_to_whitespace_const(aBuf), aBuf + 4);
	str_copy(aBuf, "aaaa\n\nb");
	EXPECT_EQ(str_skip_to_whitespace(aBuf), aBuf + 4);
	EXPECT_EQ(str_skip_to_whitespace_const(aBuf), aBuf + 4);
	str_copy(aBuf, "aaaa\r\rb");
	EXPECT_EQ(str_skip_to_whitespace(aBuf), aBuf + 4);
	EXPECT_EQ(str_skip_to_whitespace_const(aBuf), aBuf + 4);
	str_copy(aBuf, "aaaa\t\tb");
	EXPECT_EQ(str_skip_to_whitespace(aBuf), aBuf + 4);
	EXPECT_EQ(str_skip_to_whitespace_const(aBuf), aBuf + 4);
}

TEST(StrWhitespace, SkipWhitespaces)
{
	char aBuf[64];
	str_copy(aBuf, "");
	EXPECT_EQ(str_skip_whitespaces(aBuf), aBuf);
	EXPECT_EQ(str_skip_whitespaces_const(aBuf), aBuf);
	str_copy(aBuf, "aaaa");
	EXPECT_EQ(str_skip_whitespaces(aBuf), aBuf);
	EXPECT_EQ(str_skip_whitespaces_const(aBuf), aBuf);
	str_copy(aBuf, " \n\r\taaaa");
	EXPECT_EQ(str_skip_whitespaces(aBuf), aBuf + 4);
	EXPECT_EQ(str_skip_whitespaces_const(aBuf), aBuf + 4);
}
