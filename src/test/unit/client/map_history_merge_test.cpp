#include <game/client/components/tclient/map_history.h>

#include <gtest/gtest.h>

#include <string>

using namespace QmMapHistory;

TEST(MapHistoryMerge, CollectionKeepsFirstAppearanceOrderInsteadOfIdentityOrder)
{
	CMapHistory History;
	ASSERT_TRUE(History.FromJson(R"({"entries":[
		{"map_name":"Z","map_id":"z"},{"map_name":"A","map_id":"a"},{"map_name":"M","map_id":"m"}
	]})"));
	ASSERT_EQ(History.Size(), 3u);
	EXPECT_EQ(History.Entries()[0].m_MapId, "z");
	EXPECT_EQ(History.Entries()[1].m_MapId, "a");
	EXPECT_EQ(History.Entries()[2].m_MapId, "m");
}

TEST(MapHistoryMerge, EqualTimestampUsesLaterMetadataAndKeepsMaximumDeaths)
{
	CMapHistory History;
	ASSERT_TRUE(History.FromJson(R"({"entries":[
		{"map_name":"First","map_id":"map","last_entered":100,"deaths":9,"finished":false,"last_played_date":"first"},
		{"map_name":"Later","map_id":"map","last_entered":100,"deaths":2,"finished":true,"finish_time_ms":500,"play_time_ms":750,"last_played_date":"later"}
	]})"));
	ASSERT_EQ(History.Size(), 1u);
	const auto &Record = History.Entries().front();
	EXPECT_EQ(Record.m_MapName, "Later");
	EXPECT_EQ(Record.m_DeathCount, 9);
	EXPECT_EQ(Record.m_LastPlayedDate, "later");
	EXPECT_TRUE(Record.m_Finished);
	EXPECT_EQ(Record.m_FinishTimeMs, 500);
	EXPECT_EQ(Record.m_PlayTimeMs, 750);
}

TEST(MapHistoryMerge, DuplicateOfAnEarlyShortIdentityStillUpdatesAfterManyDistinctEntries)
{
	constexpr int COUNT = 2048;
	std::string Json = R"({"entries":[)";
	for(int i = 0; i < COUNT; ++i)
	{
		if(i)
			Json += ',';
		Json += R"({"map_name":"Map","map_id":"id:)" + std::to_string(i) + R"(","last_entered":1})";
	}
	Json += R"(,{"map_name":"Later first","map_id":"id:0","last_entered":10000,"deaths":7}]})";
	CMapHistory History;
	ASSERT_TRUE(History.FromJson(Json));
	ASSERT_EQ(History.Size(), static_cast<size_t>(COUNT));
	for(int i = 0; i < COUNT; ++i)
	{
		SCOPED_TRACE(i);
		EXPECT_EQ(History.Entries()[i].m_MapId, "id:" + std::to_string(i));
	}
	EXPECT_EQ(History.Entries().front().m_MapName, "Later first");
	EXPECT_EQ(History.Entries().front().m_LastEnteredAt, 10000);
	EXPECT_EQ(History.Entries().front().m_DeathCount, 7);
}

TEST(MapHistoryMerge, NameFallbackIdentitiesMergeWithMatchingExplicitIdentities)
{
	CMapHistory History;
	ASSERT_TRUE(History.FromJson(R"({"entries":[
		{"map_name":"Legacy","last_entered":100,"deaths":9},
		{"map_name":"Empty","map_id":"","last_entered":10,"deaths":2},
		{"map_name":"Renamed legacy","map_id":"Legacy","last_entered":200,"deaths":1},
		{"map_name":"Empty","map_id":null,"last_entered":20,"deaths":5}
	]})"));
	ASSERT_EQ(History.Size(), 2u);
	EXPECT_EQ(History.Entries()[0].m_MapId, "Legacy");
	EXPECT_EQ(History.Entries()[0].m_MapName, "Renamed legacy");
	EXPECT_EQ(History.Entries()[0].m_DeathCount, 9);
	EXPECT_EQ(History.Entries()[1].m_MapId, "Empty");
	EXPECT_EQ(History.Entries()[1].m_LastEnteredAt, 20);
	EXPECT_EQ(History.Entries()[1].m_DeathCount, 5);
}

TEST(MapHistoryMerge, RepeatedLoadsBuildIndependentIdentitiesAndReplacePreviousOrder)
{
	CMapHistory History;
	ASSERT_TRUE(History.FromJson(R"({"entries":[{"map_name":"Z","map_id":"z"},{"map_name":"A","map_id":"a"}]})"));
	ASSERT_TRUE(History.FromJson(R"({"entries":[{"map_name":"New A","map_id":"a"},{"map_name":"New Z","map_id":"z"}]})"));
	ASSERT_EQ(History.Size(), 2u);
	EXPECT_EQ(History.Entries()[0].m_MapId, "a");
	EXPECT_EQ(History.Entries()[0].m_MapName, "New A");
	EXPECT_EQ(History.Entries()[1].m_MapId, "z");
	EXPECT_EQ(History.Entries()[1].m_MapName, "New Z");
}
