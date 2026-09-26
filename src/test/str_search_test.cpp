#include <base/mem.h>
#include <base/str.h>
#include <base/windows.h>

#include <engine/server/server.h>

#include <game/gamecore.h>

#include <gtest/gtest.h>

#include <limits>
#include <vector>

TEST(StrSearch, StrDelim)
{
	int Start, End;
	//                                      0123456
	//                            01234567891111111
	EXPECT_EQ(str_delimiters_around_offset("123;123456789;aaa", ";", 5, &Start, &End), true);
	EXPECT_EQ(Start, 4);
	EXPECT_EQ(End, 13);

	EXPECT_EQ(str_delimiters_around_offset("123;123", ";", 1, &Start, &End), false);
	EXPECT_EQ(Start, 0);
	EXPECT_EQ(End, 3);

	EXPECT_EQ(str_delimiters_around_offset("---foo---bar---baz---hello", "---", 1, &Start, &End), false);
	EXPECT_EQ(Start, 0);
	EXPECT_EQ(End, 0);

	EXPECT_EQ(str_delimiters_around_offset("---foo---bar---baz---hello", "---", 2, &Start, &End), false);
	EXPECT_EQ(Start, 0);
	EXPECT_EQ(End, 0);

	EXPECT_EQ(str_delimiters_around_offset("---foo---bar---baz---hello", "---", 3, &Start, &End), true);
	EXPECT_EQ(Start, 3);
	EXPECT_EQ(End, 6);

	EXPECT_EQ(str_delimiters_around_offset("---foo---bar---baz---hello", "---", 4, &Start, &End), true);
	EXPECT_EQ(Start, 3);
	EXPECT_EQ(End, 6);

	EXPECT_EQ(str_delimiters_around_offset("---foo---bar---baz---hello", "---", 9, &Start, &End), true);
	EXPECT_EQ(Start, 9);
	EXPECT_EQ(End, 12);

	EXPECT_EQ(str_delimiters_around_offset("---foo---bar---baz---hello", "---", 22, &Start, &End), false);
	EXPECT_EQ(Start, 21);
	EXPECT_EQ(End, 26);

	EXPECT_EQ(str_delimiters_around_offset("foo;;;;bar;;;;;;", ";", 2, &Start, &End), false);
	EXPECT_EQ(Start, 0);
	EXPECT_EQ(End, 3);

	EXPECT_EQ(str_delimiters_around_offset("foo;;;;bar;;;;;;", ";", 3, &Start, &End), false);
	EXPECT_EQ(Start, 0);
	EXPECT_EQ(End, 3);

	EXPECT_EQ(str_delimiters_around_offset("foo;;;;bar;;;;;;", ";", 4, &Start, &End), true);
	EXPECT_EQ(Start, 4);
	EXPECT_EQ(End, 4);

	EXPECT_EQ(str_delimiters_around_offset("", ";", 4, &Start, &End), false);
	EXPECT_EQ(Start, 0);
	EXPECT_EQ(End, 0);
}

TEST(StrSearch, StrIsNum)
{
	EXPECT_EQ(str_isnum('/'), false);
	EXPECT_EQ(str_isnum('0'), true);
	EXPECT_EQ(str_isnum('1'), true);
	EXPECT_EQ(str_isnum('2'), true);
	EXPECT_EQ(str_isnum('8'), true);
	EXPECT_EQ(str_isnum('9'), true);
	EXPECT_EQ(str_isnum(':'), false);
	EXPECT_EQ(str_isnum(' '), false);
}

TEST(StrSearch, StrIsAllNum)
{
	EXPECT_EQ(str_isallnum("/"), 0);
	EXPECT_EQ(str_isallnum("0"), 1);
	EXPECT_EQ(str_isallnum("1"), 1);
	EXPECT_EQ(str_isallnum("2"), 1);
	EXPECT_EQ(str_isallnum("8"), 1);
	EXPECT_EQ(str_isallnum("9"), 1);
	EXPECT_EQ(str_isallnum(":"), 0);
	EXPECT_EQ(str_isallnum(" "), 0);

	EXPECT_EQ(str_isallnum("123"), 1);
	EXPECT_EQ(str_isallnum("123/"), 0);
	EXPECT_EQ(str_isallnum("123:"), 0);
}

TEST(StrSearch, Startswith)
{
	EXPECT_TRUE(str_startswith("abcdef", "abc"));
	EXPECT_FALSE(str_startswith("abc", "abcdef"));

	EXPECT_TRUE(str_startswith("xyz", ""));
	EXPECT_FALSE(str_startswith("", "xyz"));

	EXPECT_FALSE(str_startswith("house", "home"));
	EXPECT_FALSE(str_startswith("blackboard", "board"));

	EXPECT_TRUE(str_startswith("поплавать", "по"));
	EXPECT_FALSE(str_startswith("плавать", "по"));

	static const char ABCDEFG[] = "abcdefg";
	static const char ABC[] = "abc";
	EXPECT_EQ(str_startswith(ABCDEFG, ABC) - ABCDEFG, str_length(ABC));
}

TEST(StrSearch, StartswithNocase)
{
	EXPECT_TRUE(str_startswith_nocase("Abcdef", "abc"));
	EXPECT_FALSE(str_startswith_nocase("aBc", "abcdef"));

	EXPECT_TRUE(str_startswith_nocase("xYz", ""));
	EXPECT_FALSE(str_startswith_nocase("", "xYz"));

	EXPECT_FALSE(str_startswith_nocase("house", "home"));
	EXPECT_FALSE(str_startswith_nocase("Blackboard", "board"));

	EXPECT_TRUE(str_startswith_nocase("поплавать", "по"));
	EXPECT_FALSE(str_startswith_nocase("плавать", "по"));

	static const char ABCDEFG[] = "aBcdefg";
	static const char ABC[] = "abc";
	EXPECT_EQ(str_startswith_nocase(ABCDEFG, ABC) - ABCDEFG, str_length(ABC));
}

TEST(StrSearch, Endswith)
{
	EXPECT_TRUE(str_endswith("abcdef", "def"));
	EXPECT_FALSE(str_endswith("def", "abcdef"));

	EXPECT_TRUE(str_endswith("xyz", ""));
	EXPECT_FALSE(str_endswith("", "xyz"));

	EXPECT_FALSE(str_endswith("rhyme", "mine"));
	EXPECT_FALSE(str_endswith("blackboard", "black"));

	EXPECT_TRUE(str_endswith("люди", "юди"));
	EXPECT_FALSE(str_endswith("люди", "любовь"));

	static const char ABCDEFG[] = "abcdefg";
	static const char DEFG[] = "defg";
	EXPECT_EQ(str_endswith(ABCDEFG, DEFG) - ABCDEFG,
		str_length(ABCDEFG) - str_length(DEFG));
}

TEST(StrSearch, EndswithNocase)
{
	EXPECT_TRUE(str_endswith_nocase("abcdef", "deF"));
	EXPECT_FALSE(str_endswith_nocase("def", "abcdef"));

	EXPECT_TRUE(str_endswith_nocase("xyz", ""));
	EXPECT_FALSE(str_endswith_nocase("", "xyz"));

	EXPECT_FALSE(str_endswith_nocase("rhyme", "minE"));
	EXPECT_FALSE(str_endswith_nocase("blackboard", "black"));

	EXPECT_TRUE(str_endswith_nocase("люди", "юди"));
	EXPECT_FALSE(str_endswith_nocase("люди", "любовь"));

	static const char ABCDEFG[] = "abcdefG";
	static const char DEFG[] = "defg";
	EXPECT_EQ(str_endswith_nocase(ABCDEFG, DEFG) - ABCDEFG,
		str_length(ABCDEFG) - str_length(DEFG));
}

TEST(StrSearch, Tokenize)
{
	char aTest[] = "GER,RUS,ZAF,BRA,CAN";
	const char *apOut[] = {"GER", "RUS", "ZAF", "BRA", "CAN"};
	char aBuf[4];

	int n = 0;
	for(const char *pTok = aTest; (pTok = str_next_token(pTok, ",", aBuf, sizeof(aBuf)));)
		EXPECT_STREQ(apOut[n++], aBuf);

	char aTest2[] = "";
	EXPECT_EQ(str_next_token(aTest2, ",", aBuf, sizeof(aBuf)), nullptr);

	char aTest3[] = "+b";
	str_next_token(aTest3, "+", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "b");
}

TEST(StrSearch, InList)
{
	char aTest[] = "GER,RUS,ZAF,BRA,CAN";
	EXPECT_TRUE(str_in_list(aTest, ",", "GER"));
	EXPECT_TRUE(str_in_list(aTest, ",", "RUS"));
	EXPECT_TRUE(str_in_list(aTest, ",", "ZAF"));
	EXPECT_TRUE(str_in_list(aTest, ",", "BRA"));
	EXPECT_TRUE(str_in_list(aTest, ",", "CAN"));

	EXPECT_FALSE(str_in_list(aTest, ",", "CHN"));
	EXPECT_FALSE(str_in_list(aTest, ",", "R,R"));

	EXPECT_FALSE(str_in_list("abc,xyz", ",", "abcdef"));
	EXPECT_FALSE(str_in_list("", ",", ""));
	EXPECT_FALSE(str_in_list("", ",", "xyz"));

	EXPECT_TRUE(str_in_list("FOO,,BAR", ",", ""));
	EXPECT_TRUE(str_in_list("abc,,def", ",", "def"));
}

TEST(StrSearch, RightChar)
{
	const char *pStr = "a bb ccc dddd       eeeee";
	EXPECT_EQ(str_rchr(pStr, 'a'), pStr);
	EXPECT_EQ(str_rchr(pStr, 'b'), pStr + 3);
	EXPECT_EQ(str_rchr(pStr, 'c'), pStr + 7);
	EXPECT_EQ(str_rchr(pStr, 'd'), pStr + 12);
	EXPECT_EQ(str_rchr(pStr, ' '), pStr + 19);
	EXPECT_EQ(str_rchr(pStr, 'e'), pStr + 24);
	EXPECT_EQ(str_rchr(pStr, '\0'), pStr + str_length(pStr));
	EXPECT_EQ(str_rchr(pStr, 'y'), nullptr);
}

TEST(StrSearch, CountChar)
{
	const char *pStr = "a bb ccc dddd       eeeee";
	EXPECT_EQ(str_countchr(pStr, 'a'), 1);
	EXPECT_EQ(str_countchr(pStr, 'b'), 2);
	EXPECT_EQ(str_countchr(pStr, 'c'), 3);
	EXPECT_EQ(str_countchr(pStr, 'd'), 4);
	EXPECT_EQ(str_countchr(pStr, 'e'), 5);
	EXPECT_EQ(str_countchr(pStr, ' '), 10);
	EXPECT_EQ(str_countchr(pStr, '\0'), 0);
	EXPECT_EQ(str_countchr(pStr, 'y'), 0);
}
