// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/race.h>

#include <gtest/gtest.h>

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
