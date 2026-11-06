// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/race.h>

#include <gtest/gtest.h>

#include <limits>
#include <string>

TEST(RaceHelper, ParsesEnglishFinishMessage)
{
	char aName[64];
	EXPECT_EQ(CRaceHelper::TimeFromFinishMessage("Alpha finished in: 1 minute(s) 40.00 second(s)", aName, sizeof(aName)), 100000);
	EXPECT_STREQ(aName, "Alpha");
}

TEST(RaceHelper, ParsesChineseFinishMessage)
{
	char aName[64];
	EXPECT_EQ(CRaceHelper::TimeFromFinishMessage("Alpha 完成了地图，用时：1 分钟 40.00 秒", aName, sizeof(aName)), 100000);
	EXPECT_STREQ(aName, "Alpha");
}

TEST(RaceHelper, ParsesSecondsWithLeadingSpacesAndComma)
{
	EXPECT_EQ(CRaceHelper::TimeFromSecondsStr("  12,345"), 12345);
	EXPECT_EQ(CRaceHelper::TimeFromSecondsStr("12.3"), 12300);
	EXPECT_EQ(CRaceHelper::TimeFromSecondsStr("12.34"), 12340);
}

TEST(RaceHelper, TruncatesFractionAfterMilliseconds)
{
	EXPECT_EQ(CRaceHelper::TimeFromSecondsStr("12.345999"), 12345);
}

TEST(RaceHelper, PreservesExistingSecondsSuffixParsing)
{
	EXPECT_EQ(CRaceHelper::TimeFromSecondsStr("12.345 second(s)"), 12345);
	EXPECT_EQ(CRaceHelper::TimeFromSecondsStr("12suffix"), 12000);
}

TEST(RaceHelper, AcceptsMaximumMillisecondTime)
{
	EXPECT_EQ(CRaceHelper::TimeFromSecondsStr("2147483.647"), std::numeric_limits<int>::max());
}

TEST(RaceHelper, RejectsFractionThatOverflowsMillisecondTime)
{
	EXPECT_EQ(CRaceHelper::TimeFromSecondsStr("2147483.648"), -1);
	EXPECT_EQ(CRaceHelper::TimeFromSecondsStr("2147483.999"), -1);
}

TEST(RaceHelper, RejectsWholeSecondsBeyondMillisecondRange)
{
	EXPECT_EQ(CRaceHelper::TimeFromSecondsStr("2147484"), -1);
	EXPECT_EQ(CRaceHelper::TimeFromSecondsStr("999999999999999999999999999999"), -1);
}

TEST(RaceHelper, PreservesZeroAndLeadingZeroes)
{
	EXPECT_EQ(CRaceHelper::TimeFromSecondsStr("0"), 0);
	EXPECT_EQ(CRaceHelper::TimeFromSecondsStr(" 00000.010"), 10);
	const std::string LongZeroPrefix = std::string(512, '0') + "1.234";
	EXPECT_EQ(CRaceHelper::TimeFromSecondsStr(LongZeroPrefix.c_str()), 1234);
}

TEST(RaceHelper, RejectsSecondsWithoutInitialAsciiDigit)
{
	for(const char *pInput : {"", "   ", ".1", "-1", "+1", "\xc3\xa9"})
	{
		SCOPED_TRACE(pInput);
		EXPECT_EQ(CRaceHelper::TimeFromSecondsStr(pInput), -1);
	}
}

TEST(RaceHelper, ParsesMinuteTimesWithBothLanguagesAndComma)
{
	EXPECT_EQ(CRaceHelper::TimeFromStr(" 2 minute(s) 3,456 second(s)"), 123456);
	EXPECT_EQ(CRaceHelper::TimeFromStr(" 2 分钟 3,456 秒"), 123456);
}

TEST(RaceHelper, AcceptsMinuteAndSecondSumAtMaximum)
{
	EXPECT_EQ(CRaceHelper::TimeFromStr("35791 minute(s) 23.647 second(s)"), std::numeric_limits<int>::max());
	EXPECT_EQ(CRaceHelper::TimeFromStr("35791 分钟 23.647 秒"), std::numeric_limits<int>::max());
}

TEST(RaceHelper, RejectsMinuteProductOverflow)
{
	EXPECT_EQ(CRaceHelper::TimeFromStr("35792 minute(s) 0 second(s)"), -1);
	EXPECT_EQ(CRaceHelper::TimeFromStr("99999999999999999999 分钟 0 秒"), -1);
}

TEST(RaceHelper, RejectsMinuteAndSecondSumOverflow)
{
	EXPECT_EQ(CRaceHelper::TimeFromStr("35791 minute(s) 23.648 second(s)"), -1);
	EXPECT_EQ(CRaceHelper::TimeFromStr("1 分钟 2147483.647 秒"), -1);
}

TEST(RaceHelper, RejectsMissingMinuteOrSecondNumber)
{
	EXPECT_EQ(CRaceHelper::TimeFromStr("x minute(s) 1 second(s)"), -1);
	EXPECT_EQ(CRaceHelper::TimeFromStr("1 分钟  秒"), -1);
}

TEST(RaceHelper, ParsesSecondsWithoutMinuteComponent)
{
	EXPECT_EQ(CRaceHelper::TimeFromStr("90.125 second(s)"), 90125);
	EXPECT_EQ(CRaceHelper::TimeFromStr("90.125 秒"), 90125);
}

TEST(RaceHelper, RejectsOverflowingFinishMessageTime)
{
	char aName[64];
	EXPECT_EQ(CRaceHelper::TimeFromFinishMessage("Alpha finished in: 999999999999 second(s)", aName, sizeof(aName)), -1);
	EXPECT_STREQ(aName, "Alpha");
	EXPECT_EQ(CRaceHelper::TimeFromFinishMessage("Alpha 完成了地图，用时：35791 分钟 23.648 秒", aName, sizeof(aName)), -1);
}

TEST(RaceHelper, RejectsEmptyOrTruncatedFinishName)
{
	char aName[6] = "keep";
	EXPECT_EQ(CRaceHelper::TimeFromFinishMessage(" finished in: 1 second(s)", aName, sizeof(aName)), -1);
	EXPECT_STREQ(aName, "keep");
	EXPECT_EQ(CRaceHelper::TimeFromFinishMessage("Longer finished in: 1 second(s)", aName, sizeof(aName)), -1);
	EXPECT_STREQ(aName, "keep");
	EXPECT_EQ(CRaceHelper::TimeFromFinishMessage("Alpha finished in: 1 second(s)", aName, sizeof(aName)), 1000);
	EXPECT_STREQ(aName, "Alpha");
}

TEST(RaceHelper, RejectsUnrecognizedFinishText)
{
	char aName[16] = "keep";
	EXPECT_EQ(CRaceHelper::TimeFromFinishMessage("Alpha: 1 second(s)", aName, sizeof(aName)), -1);
	EXPECT_STREQ(aName, "keep");
	EXPECT_EQ(CRaceHelper::TimeFromStr("1"), -1);
}
