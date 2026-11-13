#include <game/client/components/qmclient/map_history_session.h>

#include <gtest/gtest.h>

#include <array>

TEST(MapHistoryId, NonemptyPathKeepsTheExistingPathIdentity)
{
	std::string Output = "old";
	ASSERT_TRUE(QmMapHistory::TrySessionMapId("Map", "maps/Map.map", Output));
	EXPECT_EQ(Output, "path:maps/Map.map");
}

TEST(MapHistoryId, MissingPathUsesTheExistingNameIdentity)
{
	for(const char *pPath : std::array<const char *, 2>{nullptr, ""})
	{
		std::string Output = "old";
		ASSERT_TRUE(QmMapHistory::TrySessionMapId("Map", pPath, Output));
		EXPECT_EQ(Output, "name:Map");
	}
}

TEST(MapHistoryId, MissingNameSuccessfullyClearsTheIdentityEvenWithAPath)
{
	for(const char *pName : std::array<const char *, 2>{nullptr, ""})
	{
		std::string Output = "old";
		ASSERT_TRUE(QmMapHistory::TrySessionMapId(pName, "maps/Map.map", Output));
		EXPECT_TRUE(Output.empty());
	}
}

TEST(MapHistoryId, MapIdentityPreservesUnicodeAndPathBytes)
{
	std::string Output;
	ASSERT_TRUE(QmMapHistory::TrySessionMapId("地图 \"甲\"", "maps/地图 \"甲\".map", Output));
	EXPECT_EQ(Output, "path:maps/地图 \"甲\".map");
}
