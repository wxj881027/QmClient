#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>

#include <game/client/components/qmclient/axiom_scores.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <string>

namespace
{
	const char *s_pMode = R"({"mode":"Gores","points":9,"total_play_time":2,"total_maps_completed":3,"performance_points":4,"mileage":5,"difficulties":[]})";

	std::string Player(const std::string &Modes = s_pMode)
	{
		return R"({"name":"wolf","user_id":5528,"player_name":"Wolf","dummy_name":"Dummy","modes":[)" + Modes + "]}";
	}

	std::string Document(const std::string &Players)
	{
		return R"({"remote":{"axiom":{"players":[)" + Players + "]}}}";
	}

	std::string StoredTypes(unsigned Count, bool Unique = true)
	{
		std::string Text = "[";
		for(unsigned Index = 0; Index < Count; ++Index)
		{
			if(Index)
				Text += ',';
			Text += "{\"name\":\"Type_" + std::to_string(Unique ? Index : 0) + "\",\"play_time_seconds\":" + std::to_string(Index) + '}';
		}
		return Text + ']';
	}

	// 只修改输入文档；加载、筛选、缓存发布及序列化均调用生产接口。
	void ReplaceFirst(std::string &Text, const std::string &From, const std::string &To)
	{
		const size_t Position = Text.find(From);
		ASSERT_NE(Position, std::string::npos);
		Text.replace(Position, From.size(), To);
	}

	class CAxiomPersistence : public testing::Test
	{
	protected:
		CQmAxiomScores m_Scores;

		void Load(const std::string &Text)
		{
			std::unique_ptr<json_value, decltype(&json_value_free)> pRoot(JsonParse(Text.data(), Text.size()), json_value_free);
			ASSERT_NE(pRoot, nullptr);
			m_Scores.LoadPersistentCache(pRoot.get());
		}

		void SeedOldEntry()
		{
			std::string Text = Document(Player());
			ReplaceFirst(Text, "\"points\":9", "\"points\":1");
			Load(Text);
			ASSERT_NE(m_Scores.GetResult("wolf"), nullptr);
		}

		void ExpectOldEntry()
		{
			const auto *pResult = m_Scores.GetResult("wolf");
			ASSERT_NE(pResult, nullptr);
			EXPECT_EQ(pResult->Mode(EQmAxiomMode::GORES).m_Score.m_Points, 1);
		}
	};

	class CAxiomRequiredField : public CAxiomPersistence, public testing::WithParamInterface<const char *>
	{
	};

	class CAxiomOptionalName : public CAxiomPersistence, public testing::WithParamInterface<const char *>
	{
	};

	class CAxiomRequiredNumber : public CAxiomPersistence, public testing::WithParamInterface<const char *>
	{
	};

	struct SValidNumber
	{
		const char *m_pJson;
		int64_t m_Value;
	};

	class CAxiomValidNumber : public CAxiomPersistence, public testing::WithParamInterface<SValidNumber>
	{
	};

	class CAxiomDifficultyField : public CAxiomPersistence, public testing::WithParamInterface<const char *>
	{
	};
}

TEST_P(CAxiomRequiredField, TruncatedRequiredKeyKeepsOldEntry)
{
	SeedOldEntry();
	std::string Text = Document(Player());
	ReplaceFirst(Text, std::string("\"") + GetParam() + "\":", std::string("\"") + GetParam() + "\\u0000tail\":");
	Load(Text);
	ExpectOldEntry();
}

TEST_P(CAxiomRequiredField, UnknownPrefixKeyDoesNotHideFollowingValidField)
{
	std::string Text = Document(Player());
	const std::string Key = std::string("\"") + GetParam();
	ReplaceFirst(Text, Key + "\":", Key + "\\u0000tail\":null," + Key + "\":");
	Load(Text);
	const auto *pResult = m_Scores.GetResult("wolf");
	ASSERT_NE(pResult, nullptr);
	EXPECT_EQ(pResult->Mode(EQmAxiomMode::GORES).m_Score.m_Points, 9);
}

INSTANTIATE_TEST_SUITE_P(RequiredCacheFields, CAxiomRequiredField,
	testing::Values("remote", "axiom", "players", "name", "user_id", "modes", "mode", "points", "total_play_time", "total_maps_completed", "performance_points", "mileage"));

TEST_F(CAxiomPersistence, TruncatedPlayerNameCannotReplacePrefixPlayer)
{
	SeedOldEntry();
	std::string Text = Document(Player());
	ReplaceFirst(Text, "\"name\":\"wolf\"", "\"name\":\"wolf\\u0000other\"");
	Load(Text);
	ExpectOldEntry();
}

TEST_F(CAxiomPersistence, TruncatedUserIdKeepsOldEntry)
{
	SeedOldEntry();
	std::string Text = Document(Player());
	ReplaceFirst(Text, "\"user_id\":5528", "\"user_id\":\"5528\\u0000tail\"");
	Load(Text);
	ExpectOldEntry();
}

TEST_P(CAxiomOptionalName, InvalidDisplayNameIsIgnoredWithoutDiscardingScore)
{
	std::string Text = Document(Player());
	const std::string Key = std::string("\"") + GetParam() + "\":";
	const char *pOriginal = std::string(GetParam()) == "player_name" ? "\"Wolf\"" : "\"Dummy\"";
	ReplaceFirst(Text, Key + pOriginal, Key + "\"prefix\\u0000tail\"");
	Load(Text);
	const auto *pResult = m_Scores.GetResult("wolf");
	ASSERT_NE(pResult, nullptr);
	EXPECT_EQ(pResult->Mode(EQmAxiomMode::GORES).m_Score.m_Points, 9);
	if(std::string(GetParam()) == "player_name")
	{
		EXPECT_TRUE(pResult->m_Match.m_PlayerName.empty());
		EXPECT_EQ(pResult->m_Match.m_DummyName, "Dummy");
	}
	else
	{
		EXPECT_TRUE(pResult->m_Match.m_DummyName.empty());
		EXPECT_EQ(pResult->m_Match.m_PlayerName, "Wolf");
	}
}

INSTANTIATE_TEST_SUITE_P(DisplayNames, CAxiomOptionalName, testing::Values("player_name", "dummy_name"));

TEST_F(CAxiomPersistence, TruncatedModeValueDoesNotConsumeValidMode)
{
	std::string BadMode = s_pMode;
	ReplaceFirst(BadMode, "Gores", "Gores\\u0000tail");
	ReplaceFirst(BadMode, "\"points\":9", "\"points\":1");
	Load(Document(Player(BadMode + "," + s_pMode)));
	const auto *pResult = m_Scores.GetResult("wolf");
	ASSERT_NE(pResult, nullptr);
	EXPECT_TRUE(pResult->Mode(EQmAxiomMode::GORES).m_HasData);
	EXPECT_EQ(pResult->Mode(EQmAxiomMode::GORES).m_Score.m_Points, 9);
}

TEST_F(CAxiomPersistence, InvalidModeValueCannotCreateEntry)
{
	std::string Mode = s_pMode;
	ReplaceFirst(Mode, "Gores", "Gores\\u0000tail");
	Load(Document(Player(Mode)));
	EXPECT_EQ(m_Scores.GetResult("wolf"), nullptr);
}

TEST_F(CAxiomPersistence, TruncatedGameTypeCannotOverrideAxiomTime)
{
	std::string Text = Document(Player());
	ReplaceFirst(Text, "\"modes\":", R"("ddstats_gametypes":[{"name":"Gores\u0000tail","play_time_seconds":99},{"name":"AXRace","play_time_seconds":11}],"modes":)");
	Load(Text);
	const auto *pResult = m_Scores.GetResult("wolf");
	ASSERT_NE(pResult, nullptr);
	EXPECT_EQ(pResult->Mode(EQmAxiomMode::GORES).m_Score.m_TotalPlayTime, 2);
	const auto *pGameTypes = m_Scores.GetDdStatsGameTypes("wolf");
	ASSERT_NE(pGameTypes, nullptr);
	ASSERT_EQ(pGameTypes->size(), 1u);
	EXPECT_EQ(pGameTypes->front().m_Name, "AXRace");
}

TEST_P(CAxiomRequiredNumber, InvalidRequiredNumberKeepsOldEntry)
{
	SeedOldEntry();
	std::string Text = Document(Player());
	ReplaceFirst(Text, "\"points\":9", std::string("\"points\":") + GetParam());
	Load(Text);
	ExpectOldEntry();
}

INSTANTIATE_TEST_SUITE_P(InvalidPersistedNumbers, CAxiomRequiredNumber,
	testing::Values(R"("9\u0000tail")", R"("")", R"("+9")", R"("09")", R"(" 9")", R"("9 ")", R"("9223372036854775808")", "-1", "9.5", "null"));

TEST_P(CAxiomValidNumber, IntegerAndCanonicalStringKeepExactValue)
{
	std::string Text = Document(Player());
	ReplaceFirst(Text, "\"points\":9", std::string("\"points\":") + GetParam().m_pJson);
	Load(Text);
	const auto *pResult = m_Scores.GetResult("wolf");
	ASSERT_NE(pResult, nullptr);
	EXPECT_EQ(pResult->Mode(EQmAxiomMode::GORES).m_Score.m_Points, GetParam().m_Value);
}

INSTANTIATE_TEST_SUITE_P(ExactPersistedNumbers, CAxiomValidNumber,
	testing::Values(SValidNumber{"0", 0}, SValidNumber{R"("0")", 0},
		SValidNumber{"9223372036854775807", std::numeric_limits<int64_t>::max()},
		SValidNumber{R"("9223372036854775807")", std::numeric_limits<int64_t>::max()},
		SValidNumber{R"("9007199254740993")", INT64_C(9007199254740993)}));

TEST_F(CAxiomPersistence, InvalidOptionalNumbersUseExistingFallbacks)
{
	std::string Text = Document(Player());
	ReplaceFirst(Text, "\"mileage\":5", R"("mileage":5,"global_rank":"7\u0000tail","axiom_play_time":"12\u0000tail")");
	ReplaceFirst(Text, "\"modes\":", R"("ddstats_gametypes":[{"name":"Gores","play_time_seconds":"99\u0000tail"}],"modes":)");
	Load(Text);
	const auto *pResult = m_Scores.GetResult("wolf");
	ASSERT_NE(pResult, nullptr);
	const auto &Score = pResult->Mode(EQmAxiomMode::GORES).m_Score;
	EXPECT_EQ(Score.m_TotalPlayTime, 2);
	EXPECT_FALSE(Score.m_GlobalRank.has_value());
	EXPECT_EQ(m_Scores.GetDdStatsGameTypes("wolf"), nullptr);

	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("remote");
	Writer.BeginObject();
	m_Scores.WritePersistentCache(Writer);
	Writer.EndObject();
	Writer.EndObject();
	const std::string Saved = Writer.GetOutputString();
	std::unique_ptr<json_value, decltype(&json_value_free)> pRoot(JsonParse(Saved.data(), Saved.size()), json_value_free);
	ASSERT_NE(pRoot, nullptr);
	const auto *pPlayer = json_array_get(json_object_get(json_object_get(json_object_get(pRoot.get(), "remote"), "axiom"), "players"), 0);
	const auto *pMode = json_array_get(json_object_get(pPlayer, "modes"), 0);
	EXPECT_STREQ(json_string_get(json_object_get(pMode, "axiom_play_time")), "2");
}

TEST_P(CAxiomDifficultyField, InvalidDetailKeepsAggregateAndDiscardsWholeDetailList)
{
	std::string Text = Document(Player());
	ReplaceFirst(Text, "\"difficulties\":[]", std::string(R"("difficulties":[{"name":"Good","points":1,"completed_maps":2,"remaining_maps":3},)") + GetParam() + "]");
	Load(Text);
	const auto *pResult = m_Scores.GetResult("wolf");
	ASSERT_NE(pResult, nullptr);
	const auto &Mode = pResult->Mode(EQmAxiomMode::GORES);
	EXPECT_TRUE(Mode.m_HasData);
	EXPECT_EQ(Mode.m_Score.m_Points, 9);
	EXPECT_TRUE(Mode.m_Score.m_vDifficulties.empty());
}

INSTANTIATE_TEST_SUITE_P(InvalidDetails, CAxiomDifficultyField,
	testing::Values(R"({"name":"Bad\u0000tail","points":1,"completed_maps":2,"remaining_maps":3})",
		R"({"name":"Bad","points":"1\u0000tail","completed_maps":2,"remaining_maps":3})",
		R"({"name\u0000tail":"Bad","points":1,"completed_maps":2,"remaining_maps":3})"));

TEST_F(CAxiomPersistence, FullLengthUtf8AndExactIntegersRoundTrip)
{
	std::string Name;
	for(int Index = 0; Index < 128; ++Index)
		Name += "é";
	std::string Difficulty;
	for(int Index = 0; Index < 64; ++Index)
		Difficulty += "难";
	std::string Text = Document(Player());
	ReplaceFirst(Text, "\"name\":\"wolf\"", "\"name\":\"" + Name + "\"");
	ReplaceFirst(Text, "\"player_name\":\"Wolf\"", "\"player_name\":\"" + Name + "\"");
	ReplaceFirst(Text, "\"dummy_name\":\"Dummy\"", "\"dummy_name\":\"" + Name + "\"");
	ReplaceFirst(Text, "\"modes\":", "\"ddstats_gametypes\":[{\"name\":\"" + Name + "\",\"play_time_seconds\":\"9007199254740993\"}],\"modes\":");
	ReplaceFirst(Text, "\"mode\":\"Gores\"", "\"mode\":\"gOrEs\"");
	ReplaceFirst(Text, "\"points\":9", "\"points\":\"9223372036854775807\"");
	ReplaceFirst(Text, "\"user_id\":5528", "\"user_id\":\"9007199254740993\"");
	ReplaceFirst(Text, "\"difficulties\":[]", "\"difficulties\":[{\"name\":\"" + Difficulty + "\",\"points\":1,\"completed_maps\":2,\"remaining_maps\":3}]");
	Load(Text);
	ASSERT_NE(m_Scores.GetResult(Name.c_str()), nullptr);

	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("remote");
	Writer.BeginObject();
	m_Scores.WritePersistentCache(Writer);
	Writer.EndObject();
	Writer.EndObject();
	const std::string Saved = Writer.GetOutputString();
	std::unique_ptr<json_value, decltype(&json_value_free)> pRoot(JsonParse(Saved.data(), Saved.size()), json_value_free);
	ASSERT_NE(pRoot, nullptr);
	CQmAxiomScores Restored;
	Restored.LoadPersistentCache(pRoot.get());
	const auto *pResult = Restored.GetResult(Name.c_str());
	ASSERT_NE(pResult, nullptr);
	EXPECT_EQ(pResult->m_Match.m_PlayerName, Name);
	EXPECT_EQ(pResult->m_Match.m_DummyName, Name);
	EXPECT_EQ(pResult->m_Match.m_UserId, INT64_C(9007199254740993));
	const auto &Score = pResult->Mode(EQmAxiomMode::GORES).m_Score;
	EXPECT_EQ(Score.m_Points, std::numeric_limits<int64_t>::max());
	ASSERT_EQ(Score.m_vDifficulties.size(), 1u);
	EXPECT_EQ(Score.m_vDifficulties.front().m_Name, Difficulty);
	const auto *pGameTypes = Restored.GetDdStatsGameTypes(Name.c_str());
	ASSERT_NE(pGameTypes, nullptr);
	ASSERT_EQ(pGameTypes->size(), 1u);
	EXPECT_EQ(pGameTypes->front().m_Name, Name);
	EXPECT_EQ(pGameTypes->front().m_PlayTimeSeconds, INT64_C(9007199254740993));
}

TEST_F(CAxiomPersistence, StoredTypesAtOnlineLimitRoundTripWithoutDiscardingList)
{
	std::string Types = StoredTypes(1024);
	Types.insert(Types.size() - 1, R"(,{"name":"type_512","play_time_seconds":9999})");
	std::string Text = Document(Player());
	ReplaceFirst(Text, "\"modes\":", "\"ddstats_gametypes\":" + Types + ",\"modes\":");
	Load(Text);
	const auto *pLoaded = m_Scores.GetDdStatsGameTypes("wolf");
	ASSERT_NE(pLoaded, nullptr);
	ASSERT_EQ(pLoaded->size(), 1024u);
	EXPECT_EQ((*pLoaded)[512].m_Name, "Type_512");
	EXPECT_EQ((*pLoaded)[512].m_PlayTimeSeconds, 9999);

	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("remote");
	Writer.BeginObject();
	m_Scores.WritePersistentCache(Writer);
	Writer.EndObject();
	Writer.EndObject();
	const std::string Saved = Writer.GetOutputString();
	std::unique_ptr<json_value, decltype(&json_value_free)> pRoot(JsonParse(Saved.data(), Saved.size()), json_value_free);
	ASSERT_NE(pRoot, nullptr);
	CQmAxiomScores Restored;
	Restored.LoadPersistentCache(pRoot.get());
	const auto *pRestored = Restored.GetDdStatsGameTypes("wolf");
	ASSERT_NE(pRestored, nullptr);
	ASSERT_EQ(pRestored->size(), 1024u);
	EXPECT_EQ(pRestored->front().m_Name, "Type_0");
	EXPECT_EQ((*pRestored)[512].m_PlayTimeSeconds, 9999);
	EXPECT_EQ(pRestored->back().m_Name, "Type_1023");
}

TEST_F(CAxiomPersistence, TooManyStoredTypesKeepAxiomAggregateWithoutPartialTypeAuthority)
{
	std::string Text = Document(Player());
	ReplaceFirst(Text, "\"modes\":", "\"ddstats_gametypes\":" + StoredTypes(1025) + ",\"modes\":");
	Load(Text);
	EXPECT_EQ(m_Scores.GetDdStatsGameTypes("wolf"), nullptr);
	const auto *pResult = m_Scores.GetResult("wolf");
	ASSERT_NE(pResult, nullptr);
	EXPECT_TRUE(pResult->Mode(EQmAxiomMode::GORES).m_HasData);
	EXPECT_EQ(pResult->Mode(EQmAxiomMode::GORES).m_Score.m_Points, 9);
}

TEST_F(CAxiomPersistence, RawStoredRecordOverflowIgnoresTypeArrayAndKeepsAggregate)
{
	std::string Text = Document(Player());
	ReplaceFirst(Text, "\"modes\":", "\"ddstats_gametypes\":" + StoredTypes(4097, false) + ",\"modes\":");
	Load(Text);
	EXPECT_EQ(m_Scores.GetDdStatsGameTypes("wolf"), nullptr);
	const auto *pResult = m_Scores.GetResult("wolf");
	ASSERT_NE(pResult, nullptr);
	EXPECT_EQ(pResult->Mode(EQmAxiomMode::GORES).m_Score.m_TotalPlayTime, 2);
}
