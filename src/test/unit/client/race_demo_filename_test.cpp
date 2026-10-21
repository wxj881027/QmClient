#include <game/client/race.h>

#include <gtest/gtest.h>

#include <limits>
#include <string>

TEST(RaceDemoFilename, ParsesNamedDemo)
{
	EXPECT_EQ(CRaceHelper::TimeFromDemoFilename("Kobra_100.125_Alpha.demo", "Kobra", "Alpha"), 100125);
}

TEST(RaceDemoFilename, ParsesUnnamedDemoWithExistingTimeSeparators)
{
	EXPECT_EQ(CRaceHelper::TimeFromDemoFilename("map_ 1,234.demo", "map", nullptr), 1234);
	EXPECT_EQ(CRaceHelper::TimeFromDemoFilename("map_1.2.demo", "map", nullptr), 1200);
}

TEST(RaceDemoFilename, MatchesWholeMapWithUnderscoresAndDigits)
{
	EXPECT_EQ(CRaceHelper::TimeFromDemoFilename("map_1_2_3.456.demo", "map_1_2", nullptr), 3456);
}

TEST(RaceDemoFilename, RejectsMapPrefixWithoutSeparatorOrDifferentMap)
{
	EXPECT_EQ(CRaceHelper::TimeFromDemoFilename("maple_1.234.demo", "map", nullptr), -1);
	EXPECT_EQ(CRaceHelper::TimeFromDemoFilename("other_1.234.demo", "map", nullptr), -1);
	EXPECT_EQ(CRaceHelper::TimeFromDemoFilename("map.demo", "map.demo", nullptr), -1);
}

TEST(RaceDemoFilename, RequiresExactDemoExtension)
{
	for(const char *pFilename : {"map_1.234", "map_1.234.DEMO", "map_1.234.demox", "map_1.234.demo.tmp"})
	{
		SCOPED_TRACE(pFilename);
		EXPECT_EQ(CRaceHelper::TimeFromDemoFilename(pFilename, "map", nullptr), -1);
	}
}

TEST(RaceDemoFilename, RequiresExactConfiguredPlayerSuffix)
{
	EXPECT_EQ(CRaceHelper::TimeFromDemoFilename("map_1.234.demo", "map", "Alpha"), -1);
	EXPECT_EQ(CRaceHelper::TimeFromDemoFilename("map_1.234_AlphaBeta.demo", "map", "Alpha"), -1);
	EXPECT_EQ(CRaceHelper::TimeFromDemoFilename("map_1.234_Beta.demo", "map", "Alpha"), -1);
}

TEST(RaceDemoFilename, RejectsPlayerSuffixWhenNamesAreDisabled)
{
	EXPECT_EQ(CRaceHelper::TimeFromDemoFilename("map_1.234_Alpha.demo", "map", nullptr), -1);
}

TEST(RaceDemoFilename, ParsesMapAtFormerBufferBoundary)
{
	const std::string Map(127, 'm');
	const std::string Filename = Map + "_1.234.demo";
	EXPECT_EQ(CRaceHelper::TimeFromDemoFilename(Filename.c_str(), Map.c_str(), nullptr), 1234);
}

TEST(RaceDemoFilename, MatchesTimeAndPlayerBeyondFormerBuffer)
{
	const std::string Map(140, 'm');
	const std::string Filename = Map + "_1.234_Alpha.demo";
	EXPECT_EQ(CRaceHelper::TimeFromDemoFilename(Filename.c_str(), Map.c_str(), "Alpha"), 1234);
	EXPECT_EQ(CRaceHelper::TimeFromDemoFilename(Filename.c_str(), Map.c_str(), "Beta"), -1);
}

TEST(RaceDemoFilename, PreservesUtf8MapAndPlayerNames)
{
	EXPECT_EQ(CRaceHelper::TimeFromDemoFilename("地图_1.234_玩家.demo", "地图", "玩家"), 1234);
}

TEST(RaceDemoFilename, PreservesUnderscoresAndDotsInPlayerName)
{
	EXPECT_EQ(CRaceHelper::TimeFromDemoFilename("map_1.234_Player_2.0.demo", "map", "Player_2.0"), 1234);
}

TEST(RaceDemoFilename, RejectsTimeFieldWithUnexpectedCharacters)
{
	for(const char *pFilename : {"map_.demo", "map_  .demo", "map_1a2.demo", "map_\xc3\xa9.demo", "map_-1.demo"})
	{
		SCOPED_TRACE(pFilename);
		EXPECT_EQ(CRaceHelper::TimeFromDemoFilename(pFilename, "map", nullptr), -1);
	}
}

TEST(RaceDemoFilename, RejectsOverflowingTime)
{
	EXPECT_EQ(CRaceHelper::TimeFromDemoFilename("map_2147483.647.demo", "map", nullptr), std::numeric_limits<int>::max());
	EXPECT_EQ(CRaceHelper::TimeFromDemoFilename("map_2147483.648.demo", "map", nullptr), -1);
	EXPECT_EQ(CRaceHelper::TimeFromDemoFilename("map_99999999999999999999.demo", "map", nullptr), -1);
}

TEST(RaceDemoFilename, LeavesZeroTimeForRecordingOwnerToIgnore)
{
	EXPECT_EQ(CRaceHelper::TimeFromDemoFilename("map_0.000.demo", "map", nullptr), 0);
}
