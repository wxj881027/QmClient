#include <base/mem.h>
#include <base/str.h>
#include <base/windows.h>

#include <engine/server/server.h>

#include <game/gamecore.h>

#include <gtest/gtest.h>

#include <limits>
#include <vector>

TEST(StrCopy, CopyNum)
{
	const char *pFoo = "Foobaré";
	char aBuf[64];
	str_utf8_truncate(aBuf, 3, pFoo, 1);
	EXPECT_STREQ(aBuf, "F");
	str_utf8_truncate(aBuf, 3, pFoo, 2);
	EXPECT_STREQ(aBuf, "Fo");
	str_utf8_truncate(aBuf, 3, pFoo, 3);
	EXPECT_STREQ(aBuf, "Fo");
	str_utf8_truncate(aBuf, sizeof(aBuf), pFoo, 6);
	EXPECT_STREQ(aBuf, "Foobar");
	str_utf8_truncate(aBuf, sizeof(aBuf), pFoo, 7);
	EXPECT_STREQ(aBuf, "Foobaré");
	str_utf8_truncate(aBuf, sizeof(aBuf), pFoo, 0);
	EXPECT_STREQ(aBuf, "");

	char aBuf2[8];
	str_utf8_truncate(aBuf2, sizeof(aBuf2), pFoo, 7);
	EXPECT_STREQ(aBuf2, "Foobar");
	char aBuf3[9];
	str_utf8_truncate(aBuf3, sizeof(aBuf3), pFoo, 7);
	EXPECT_STREQ(aBuf3, "Foobaré");
}

TEST(StrCopy, Copy)
{
	const char *pStr = "DDNet最好了";
	char aBuf[64];
	str_copy(aBuf, pStr, 7);
	EXPECT_STREQ(aBuf, "DDNet");
	str_copy(aBuf, pStr, 8);
	EXPECT_STREQ(aBuf, "DDNet");
	str_copy(aBuf, pStr, 9);
	EXPECT_STREQ(aBuf, "DDNet最");
	str_copy(aBuf, pStr, 10);
	EXPECT_STREQ(aBuf, "DDNet最");
	str_copy(aBuf, pStr, 11);
	EXPECT_STREQ(aBuf, "DDNet最");
	str_copy(aBuf, pStr, 12);
	EXPECT_STREQ(aBuf, "DDNet最好");
	str_copy(aBuf, pStr, 13);
	EXPECT_STREQ(aBuf, "DDNet最好");
	str_copy(aBuf, pStr, 14);
	EXPECT_STREQ(aBuf, "DDNet最好");
	str_copy(aBuf, pStr, 15);
	EXPECT_STREQ(aBuf, "DDNet最好了");
	str_copy(aBuf, pStr, 16);
	EXPECT_STREQ(aBuf, "DDNet最好了");
	str_copy(aBuf, pStr);
	EXPECT_STREQ(aBuf, "DDNet最好了");
}

TEST(StrCopy, CopyUnterminatedSource)
{
	const std::vector<char> vSrc(8, 'a');
	char aBuf[4];
	str_copy(aBuf, vSrc.data(), sizeof(aBuf));
	EXPECT_STREQ(aBuf, "aaa");
	char aExactBuf[9];
	str_copy(aExactBuf, vSrc.data(), sizeof(aExactBuf));
	EXPECT_STREQ(aExactBuf, "aaaaaaaa");
}

TEST(StrCopy, CopyArray)
{
	std::array<char, 512> aBuf;
	str_copy(aBuf, "hello");
	EXPECT_STREQ(aBuf.data(), "hello");

	std::array<char, 8> aSmallBuf;
	str_copy(aSmallBuf, "long string");
	EXPECT_STREQ(aSmallBuf.data(), "long st");
}

TEST(StrCopy, Append)
{
	char aBuf[64];
	aBuf[0] = '\0';
	str_append(aBuf, "DDNet最好了", 7);
	EXPECT_STREQ(aBuf, "DDNet");
	str_append(aBuf, "最", 8);
	EXPECT_STREQ(aBuf, "DDNet");
	str_append(aBuf, "最", 9);
	EXPECT_STREQ(aBuf, "DDNet最");
	str_append(aBuf, "好", 10);
	EXPECT_STREQ(aBuf, "DDNet最");
	str_append(aBuf, "好", 11);
	EXPECT_STREQ(aBuf, "DDNet最");
	str_append(aBuf, "好", 12);
	EXPECT_STREQ(aBuf, "DDNet最好");
	str_append(aBuf, "了", 13);
	EXPECT_STREQ(aBuf, "DDNet最好");
	str_append(aBuf, "了", 14);
	EXPECT_STREQ(aBuf, "DDNet最好");
	str_append(aBuf, "了", 15);
	EXPECT_STREQ(aBuf, "DDNet最好了");
	str_append(aBuf, "了", 16);
	EXPECT_STREQ(aBuf, "DDNet最好了");
	aBuf[0] = '\0';
	str_append(aBuf, "DDNet最好了");
	EXPECT_STREQ(aBuf, "DDNet最好了");
}

TEST(StrCopy, AppendNull)
{
	char aBuf[64];
	str_copy(aBuf, "DDNet", sizeof(aBuf));
	str_append(aBuf, nullptr, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "DDNet");
}
