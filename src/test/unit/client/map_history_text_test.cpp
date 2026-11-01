#include <game/client/components/tclient/map_history.h>

#include <gtest/gtest.h>

#include <string>

using namespace QmMapHistory;

TEST(MapHistoryText, NullSuffixedIdentityCannotMergeIntoAValidPrefixRecord)
{
	CMapHistory History;
	ASSERT_TRUE(History.FromJson(R"({"entries":[
		{"map_name":"Real","map_id":"a","last_entered":100,"deaths":3},
		{"map_name":"Injected","map_id":"a\u0000suffix","last_entered":200,"deaths":99,"finished":true}
	]})"));
	ASSERT_EQ(History.Size(), 1u);
	const auto *pReal = History.Find("a");
	ASSERT_NE(pReal, nullptr);
	EXPECT_EQ(pReal->m_MapName, "Real");
	EXPECT_EQ(pReal->m_LastEnteredAt, 100);
	EXPECT_EQ(pReal->m_DeathCount, 3);
	EXPECT_FALSE(pReal->m_Finished);
}

TEST(MapHistoryText, NullInNameSkipsTheRecordAndKeepsOtherValidEntries)
{
	CMapHistory History;
	ASSERT_TRUE(History.FromJson(R"({"entries":[
		{"map_name":"Real","map_id":"real"},
		{"map_name":"Bad\u0000suffix","map_id":"bad"}
	]})"));
	EXPECT_EQ(History.Size(), 1u);
	EXPECT_NE(History.Find("real"), nullptr);
	EXPECT_EQ(History.Find("bad"), nullptr);
}

TEST(MapHistoryText, LeadingNullIdentityDoesNotFallBackToTheMapName)
{
	CMapHistory History;
	ASSERT_TRUE(History.FromJson(R"({"entries":[{"map_name":"Name","map_id":"\u0000suffix"}]})"));
	EXPECT_EQ(History.Size(), 0u);
	EXPECT_EQ(History.Find("Name"), nullptr);
}

TEST(MapHistoryText, NullInOptionalDateClearsOnlyTheDate)
{
	CMapHistory History;
	ASSERT_TRUE(History.FromJson(R"({"entries":[{"map_name":"Map","map_id":"map","last_played_date":"2026\u0000suffix","last_entered":123,"deaths":4,"finished":true,"finish_time_ms":500,"play_time_ms":750}]})"));
	const auto *pRecord = History.Find("map");
	ASSERT_NE(pRecord, nullptr);
	EXPECT_TRUE(pRecord->m_LastPlayedDate.empty());
	EXPECT_EQ(pRecord->m_LastEnteredAt, 123);
	EXPECT_EQ(pRecord->m_DeathCount, 4);
	EXPECT_TRUE(pRecord->m_Finished);
	EXPECT_EQ(pRecord->m_FinishTimeMs, 500);
	EXPECT_EQ(pRecord->m_PlayTimeMs, 750);
}

TEST(MapHistoryText, InvalidUtf8RequiredTextSkipsOnlyThatRecord)
{
	for(bool BadName : {false, true})
	{
		for(const char *pBytes : {"\x80", "\xe4\xbd", "\xc0\xaf", "\xed\xa0\x80"})
		{
			SCOPED_TRACE(BadName ? "map_name" : "map_id");
			SCOPED_TRACE(::testing::PrintToString(pBytes));
			// fixture只把不含引号/反斜杠的原始坏字节放入指定字段。
			const std::string Json = std::string(R"({"entries":[{"map_name":"Real","map_id":"real"},{"map_name":")") +
						 (BadName ? pBytes : "Bad") + R"(","map_id":")" + (BadName ? "bad" : pBytes) + R"("}]})";
			CMapHistory History;
			ASSERT_TRUE(History.FromJson(Json));
			EXPECT_EQ(History.Size(), 1u);
			EXPECT_NE(History.Find("real"), nullptr);
		}
	}
}

TEST(MapHistoryText, InvalidUtf8OptionalDateKeepsTheRecordWithAnEmptyDate)
{
	for(const char *pBytes : {"\x80", "\xe4\xbd", "\xc0\xaf", "\xed\xa0\x80"})
	{
		SCOPED_TRACE(::testing::PrintToString(pBytes));
		const std::string Json = std::string(R"({"entries":[{"map_name":"Map","map_id":"map","last_played_date":")") + pBytes + R"(","deaths":4}]})";
		CMapHistory History;
		ASSERT_TRUE(History.FromJson(Json));
		const auto *pRecord = History.Find("map");
		ASSERT_NE(pRecord, nullptr);
		EXPECT_TRUE(pRecord->m_LastPlayedDate.empty());
		EXPECT_EQ(pRecord->m_DeathCount, 4);
	}
}

TEST(MapHistoryText, ValidUnicodeEscapesAndQuotesKeepTheirCompleteValues)
{
	CMapHistory History;
	ASSERT_TRUE(History.FromJson(R"({"entries":[{"map_name":"地图 \"甲\" \ud83d\ude00","map_id":"path:maps/地图.map","last_played_date":"2026-11-01"}]})"));
	const auto *pRecord = History.Find("path:maps/地图.map");
	ASSERT_NE(pRecord, nullptr);
	EXPECT_EQ(pRecord->m_MapName, "地图 \"甲\" 😀");
	EXPECT_EQ(pRecord->m_LastPlayedDate, "2026-11-01");
	CMapHistory Reloaded;
	ASSERT_TRUE(Reloaded.FromJson(History.ToJson()));
	EXPECT_EQ(Reloaded.ToJson(), History.ToJson());
}

TEST(MapHistoryText, MissingEmptyAndNonStringIdentityKeepTheExistingNameFallback)
{
	for(const char *pId : {"", R"(,"map_id":"")", R"(,"map_id":null)", R"(,"map_id":17)"})
	{
		SCOPED_TRACE(pId);
		CMapHistory History;
		ASSERT_TRUE(History.FromJson(std::string(R"({"entries":[{"map_name":"Legacy")") + pId + "}]}"));
		EXPECT_EQ(History.Size(), 1u);
		EXPECT_NE(History.Find("Legacy"), nullptr);
	}
}

TEST(MapHistoryText, NullSuffixedRootMemberDoesNotShadowTheActualEntries)
{
	CMapHistory History;
	ASSERT_TRUE(History.FromJson(R"({"entries\u0000suffix":[{"map_name":"Injected","map_id":"bad"}],"entries":[{"map_name":"Real","map_id":"real"}]})"));
	EXPECT_EQ(History.Size(), 1u);
	EXPECT_NE(History.Find("real"), nullptr);
	EXPECT_EQ(History.Find("bad"), nullptr);
}

TEST(MapHistoryText, NullSuffixedRecordMembersCannotOverrideExactMetadata)
{
	CMapHistory History;
	ASSERT_TRUE(History.FromJson(R"({"entries":[{
		"map_name\u0000suffix":"Injected","map_name":"Real",
		"map_id\u0000suffix":"bad","map_id":"real",
		"last_entered\u0000suffix":999,"last_entered":123,
		"last_played_date\u0000suffix":"bad","last_played_date":"2026-11-01",
		"deaths\u0000suffix":99,"deaths":3,
		"finished\u0000suffix":false,"finished":true,
		"finish_time_ms\u0000suffix":999,"finish_time_ms":500,
		"play_time_ms\u0000suffix":999,"play_time_ms":750
	}]})"));
	const auto *pRecord = History.Find("real");
	ASSERT_NE(pRecord, nullptr);
	EXPECT_EQ(pRecord->m_MapName, "Real");
	EXPECT_EQ(pRecord->m_LastEnteredAt, 123);
	EXPECT_EQ(pRecord->m_LastPlayedDate, "2026-11-01");
	EXPECT_EQ(pRecord->m_DeathCount, 3);
	EXPECT_TRUE(pRecord->m_Finished);
	EXPECT_EQ(pRecord->m_FinishTimeMs, 500);
	EXPECT_EQ(pRecord->m_PlayTimeMs, 750);
}
