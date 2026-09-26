#include <base/mem.h>
#include <base/str.h>
#include <base/windows.h>

#include <engine/server/server.h>

#include <game/gamecore.h>

#include <gtest/gtest.h>

#include <limits>
#include <vector>

TEST(StrFormat, Positional)
{
	char aBuf[256];

	// normal
	str_format(aBuf, sizeof(aBuf), "%s %s", "first", "second");
	EXPECT_STREQ(aBuf, "first second");

	// normal with positional arguments
	str_format(aBuf, sizeof(aBuf), "%1$s %2$s", "first", "second");
	EXPECT_STREQ(aBuf, "first second");

	// reverse
	str_format(aBuf, sizeof(aBuf), "%2$s %1$s", "first", "second");
	EXPECT_STREQ(aBuf, "second first");

	// duplicate
	str_format(aBuf, sizeof(aBuf), "%1$s %1$s %2$d %1$s %2$d", "str", 1);
	EXPECT_STREQ(aBuf, "str str 1 str 1");
}

TEST(StrFormat, Format)
{
	char aBuf[4];
	EXPECT_EQ(str_format(aBuf, 4, "%d:", 9), 2);
	EXPECT_STREQ(aBuf, "9:");
	EXPECT_EQ(str_format(aBuf, 4, "%d: ", 9), 3);
	EXPECT_STREQ(aBuf, "9: ");
	EXPECT_EQ(str_format(aBuf, 4, "%d: ", 99), 3);
	EXPECT_STREQ(aBuf, "99:");
}

TEST(StrFormat, FormatNumber)
{
	char aBuf[16];
	EXPECT_EQ(str_format(aBuf, sizeof(aBuf), "%d", 0), 1);
	EXPECT_STREQ(aBuf, "0");
	EXPECT_EQ(str_format(aBuf, sizeof(aBuf), "%d", std::numeric_limits<int>::min()), 11);
	EXPECT_STREQ(aBuf, "-2147483648");
	EXPECT_EQ(str_format(aBuf, sizeof(aBuf), "%d", std::numeric_limits<int>::max()), 10);
	EXPECT_STREQ(aBuf, "2147483647");
}

TEST(StrFormat, FormatTruncate)
{
	const char *pStr = "DDNet最好了";
	char aBuf[64];
	str_format(aBuf, 7, "%s", pStr);
	EXPECT_STREQ(aBuf, "DDNet");
	str_format(aBuf, 8, "%s", pStr);
	EXPECT_STREQ(aBuf, "DDNet");
	str_format(aBuf, 9, "%s", pStr);
	EXPECT_STREQ(aBuf, "DDNet最");
	str_format(aBuf, 10, "%s", pStr);
	EXPECT_STREQ(aBuf, "DDNet最");
	str_format(aBuf, 11, "%s", pStr);
	EXPECT_STREQ(aBuf, "DDNet最");
	str_format(aBuf, 12, "%s", pStr);
	EXPECT_STREQ(aBuf, "DDNet最好");
	str_format(aBuf, 13, "%s", pStr);
	EXPECT_STREQ(aBuf, "DDNet最好");
	str_format(aBuf, 14, "%s", pStr);
	EXPECT_STREQ(aBuf, "DDNet最好");
	str_format(aBuf, 15, "%s", pStr);
	EXPECT_STREQ(aBuf, "DDNet最好了");
	str_format(aBuf, 16, "%s", pStr);
	EXPECT_STREQ(aBuf, "DDNet最好了");
}

TEST(StrFormat, Time)
{
	char aBuf[32] = "foobar";

	EXPECT_EQ(str_time(123456, TIME_DAYS, aBuf, 0), -1);
	EXPECT_STREQ(aBuf, "foobar");

	EXPECT_EQ(str_time(123456, TIME_SECS_CENTISECS + 1, aBuf, sizeof(aBuf)), -1);
	EXPECT_STREQ(aBuf, "");

	EXPECT_EQ(str_time(-123456, TIME_MINS_CENTISECS, aBuf, sizeof(aBuf)), 5);
	EXPECT_STREQ(aBuf, "00.00");

	EXPECT_EQ(str_time(INT64_MAX, TIME_DAYS, aBuf, sizeof(aBuf)), 23);
	EXPECT_STREQ(aBuf, "1067519911673d 00:09:18");

	EXPECT_EQ(str_time(123456, TIME_DAYS, aBuf, sizeof(aBuf)), 5);
	EXPECT_STREQ(aBuf, "20:34");
	EXPECT_EQ(str_time(1234567, TIME_DAYS, aBuf, sizeof(aBuf)), 8);
	EXPECT_STREQ(aBuf, "03:25:45");
	EXPECT_EQ(str_time(12345678, TIME_DAYS, aBuf, sizeof(aBuf)), 11);
	EXPECT_STREQ(aBuf, "1d 10:17:36");

	EXPECT_EQ(str_time(123456, TIME_HOURS, aBuf, sizeof(aBuf)), 5);
	EXPECT_STREQ(aBuf, "20:34");
	EXPECT_EQ(str_time(1234567, TIME_HOURS, aBuf, sizeof(aBuf)), 8);
	EXPECT_STREQ(aBuf, "03:25:45");
	EXPECT_EQ(str_time(12345678, TIME_HOURS, aBuf, sizeof(aBuf)), 8);
	EXPECT_STREQ(aBuf, "34:17:36");

	EXPECT_EQ(str_time(123456, TIME_MINS, aBuf, sizeof(aBuf)), 5);
	EXPECT_STREQ(aBuf, "20:34");
	EXPECT_EQ(str_time(1234567, TIME_MINS, aBuf, sizeof(aBuf)), 6);
	EXPECT_STREQ(aBuf, "205:45");
	EXPECT_EQ(str_time(12345678, TIME_MINS, aBuf, sizeof(aBuf)), 7);
	EXPECT_STREQ(aBuf, "2057:36");

	EXPECT_EQ(str_time(123456, TIME_HOURS_CENTISECS, aBuf, sizeof(aBuf)), 8);
	EXPECT_STREQ(aBuf, "20:34.56");
	EXPECT_EQ(str_time(1234567, TIME_HOURS_CENTISECS, aBuf, sizeof(aBuf)), 11);
	EXPECT_STREQ(aBuf, "03:25:45.67");
	EXPECT_EQ(str_time(12345678, TIME_HOURS_CENTISECS, aBuf, sizeof(aBuf)), 11);
	EXPECT_STREQ(aBuf, "34:17:36.78");

	EXPECT_EQ(str_time(123456, TIME_MINS_CENTISECS, aBuf, sizeof(aBuf)), 8);
	EXPECT_STREQ(aBuf, "20:34.56");
	EXPECT_EQ(str_time(1234567, TIME_MINS_CENTISECS, aBuf, sizeof(aBuf)), 9);
	EXPECT_STREQ(aBuf, "205:45.67");
	EXPECT_EQ(str_time(12345678, TIME_MINS_CENTISECS, aBuf, sizeof(aBuf)), 10);
	EXPECT_STREQ(aBuf, "2057:36.78");

	EXPECT_EQ(str_time(123456, TIME_SECS_CENTISECS, aBuf, sizeof(aBuf)), 5);
	EXPECT_STREQ(aBuf, "34.56");
	EXPECT_EQ(str_time(1234567, TIME_SECS_CENTISECS, aBuf, sizeof(aBuf)), 5);
	EXPECT_STREQ(aBuf, "45.67");
	EXPECT_EQ(str_time(12345678, TIME_SECS_CENTISECS, aBuf, sizeof(aBuf)), 5);
	EXPECT_STREQ(aBuf, "36.78");
}

TEST(StrFormat, TimeFloat)
{
	char aBuf[64];
	EXPECT_EQ(str_time_float(123456.78, TIME_DAYS, aBuf, sizeof(aBuf)), 11);
	EXPECT_STREQ(aBuf, "1d 10:17:36");

	EXPECT_EQ(str_time_float(12.16, TIME_HOURS_CENTISECS, aBuf, sizeof(aBuf)), 5);
	EXPECT_STREQ(aBuf, "12.16");

	EXPECT_EQ(str_time_float(22.995, TIME_MINS, aBuf, sizeof(aBuf)), 5);
	EXPECT_STREQ(aBuf, "00:22");

	EXPECT_EQ(str_time_float(36000000.0f, TIME_HOURS_CENTISECS, aBuf, sizeof(aBuf)), 14);
	EXPECT_STREQ(aBuf, "10000:00:00.00");
}
