#include <game/client/components/qmclient/axiom_scores_data.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <cstring>
#include <string>

namespace
{
	constexpr char PLAYER[] = "player_ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	constexpr char SEARCH[] = R"({"code":200,"data":{"results":[{"user_id":7,"player_name":"player_ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789","dummy_name":"dummy_ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"}]}})";
	constexpr char INFO[] = R"({"code":200,"data":{"player":{"player_name":"player_ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789","points":42,"total_play_time":3600,"total_maps_completed":2,"performance_points":3,"mileage":4},"difficultyData":{"difficulty_ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789":{"stats":{"points":5,"completed_maps":1,"remaining_maps":2}}}}})";
	constexpr char DDSTATS[] = R"({"profile":{"name":"player_ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"},"most_played_gametypes":[{"key":"gametype_ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789","seconds_played":7200},{"key":"another_ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789","seconds_played":3600}]})";
}

TEST(QmAxiomResponseFailure, EverySearchAllocationFailureKeepsOldMatchAndAllowsRetry)
{
	int Failures = 0;
	bool Completed = false;
	for(size_t Budget = 0; Budget < 64; ++Budget)
	{
		SCOPED_TRACE(Budget);
		SQmAxiomSearchMatch Match{99, "old_player", "old_dummy"};
		EQmAxiomParseResult Result;
		bool Triggered;
		{
			CPredictionAllocationFailure Fault(Budget);
			Result = QmParseAxiomSearchResponse(SEARCH, sizeof(SEARCH) - 1, PLAYER, Match);
			Triggered = Fault.Triggered();
		}
		if(!Triggered)
		{
			EXPECT_EQ(Result, EQmAxiomParseResult::SUCCESS);
			EXPECT_EQ(Match.m_UserId, 7);
			Completed = true;
			break;
		}
		++Failures;
		EXPECT_EQ(Result, EQmAxiomParseResult::INVALID_RESPONSE);
		EXPECT_EQ(Match.m_UserId, 99);
		EXPECT_EQ(Match.m_PlayerName, "old_player");
		EXPECT_EQ(Match.m_DummyName, "old_dummy");
		ASSERT_EQ(QmParseAxiomSearchResponse(SEARCH, sizeof(SEARCH) - 1, PLAYER, Match), EQmAxiomParseResult::SUCCESS);
		EXPECT_EQ(Match.m_UserId, 7);
		EXPECT_EQ(Match.m_PlayerName, PLAYER);
	}
	EXPECT_GT(Failures, 0);
	EXPECT_TRUE(Completed);
}

TEST(QmAxiomResponseFailure, EveryInfoAllocationFailureKeepsOldScoreAndAllowsRetry)
{
	int Failures = 0;
	bool Completed = false;
	for(size_t Budget = 0; Budget < 64; ++Budget)
	{
		SCOPED_TRACE(Budget);
		SQmAxiomModeScore Score;
		Score.m_PlayerName = "old_player";
		Score.m_Points = 99;
		Score.m_vDifficulties.push_back({"old_difficulty", 77});
		EQmAxiomParseResult Result;
		bool Triggered;
		{
			CPredictionAllocationFailure Fault(Budget);
			Result = QmParseAxiomInfoResponse(INFO, sizeof(INFO) - 1, Score);
			Triggered = Fault.Triggered();
		}
		if(!Triggered)
		{
			EXPECT_EQ(Result, EQmAxiomParseResult::SUCCESS);
			EXPECT_EQ(Score.m_Points, 42);
			Completed = true;
			break;
		}
		++Failures;
		EXPECT_EQ(Result, EQmAxiomParseResult::INVALID_RESPONSE);
		EXPECT_EQ(Score.m_PlayerName, "old_player");
		EXPECT_EQ(Score.m_Points, 99);
		ASSERT_EQ(Score.m_vDifficulties.size(), 1u);
		EXPECT_EQ(Score.m_vDifficulties[0].m_Name, "old_difficulty");
		EXPECT_EQ(Score.m_vDifficulties[0].m_Points, 77);
		ASSERT_EQ(QmParseAxiomInfoResponse(INFO, sizeof(INFO) - 1, Score), EQmAxiomParseResult::SUCCESS);
		EXPECT_EQ(Score.m_PlayerName, PLAYER);
		EXPECT_EQ(Score.m_Points, 42);
	}
	EXPECT_GT(Failures, 0);
	EXPECT_TRUE(Completed);
}

TEST(QmAxiomResponseFailure, EveryDdStatsAllocationFailureKeepsOldListAndAllowsRetry)
{
	int Failures = 0;
	bool Completed = false;
	for(size_t Budget = 0; Budget < 64; ++Budget)
	{
		SCOPED_TRACE(Budget);
		std::vector<SQmDdStatsGameType> vGameTypes{{"old_gametype", 99}};
		EQmAxiomParseResult Result;
		bool Triggered;
		{
			CPredictionAllocationFailure Fault(Budget);
			Result = QmParseDdStatsPlayerResponse(DDSTATS, sizeof(DDSTATS) - 1, PLAYER, vGameTypes);
			Triggered = Fault.Triggered();
		}
		if(!Triggered)
		{
			EXPECT_EQ(Result, EQmAxiomParseResult::SUCCESS);
			EXPECT_EQ(vGameTypes.size(), 2u);
			Completed = true;
			break;
		}
		++Failures;
		EXPECT_EQ(Result, EQmAxiomParseResult::INVALID_RESPONSE);
		ASSERT_EQ(vGameTypes.size(), 1u);
		EXPECT_EQ(vGameTypes[0].m_Name, "old_gametype");
		EXPECT_EQ(vGameTypes[0].m_PlayTimeSeconds, 99);
		ASSERT_EQ(QmParseDdStatsPlayerResponse(DDSTATS, sizeof(DDSTATS) - 1, PLAYER, vGameTypes), EQmAxiomParseResult::SUCCESS);
		ASSERT_EQ(vGameTypes.size(), 2u);
		EXPECT_EQ(vGameTypes[0].m_PlayTimeSeconds, 7200);
		EXPECT_EQ(vGameTypes[1].m_PlayTimeSeconds, 3600);
	}
	EXPECT_GT(Failures, 0);
	EXPECT_TRUE(Completed);
}

TEST(QmAxiomResponseFailure, InvalidDdStatsTailKeepsOldListAndValidRetryReplacesIt)
{
	constexpr char INVALID[] = R"({"profile":{"name":"player"},"most_played_gametypes":[{"key":"Gores","seconds_played":7200},{"key":"AXRace","seconds_played":-1}]})";
	std::vector<SQmDdStatsGameType> vGameTypes{{"old", 99}};
	EXPECT_EQ(QmParseDdStatsPlayerResponse(INVALID, sizeof(INVALID) - 1, "player", vGameTypes), EQmAxiomParseResult::INVALID_RESPONSE);
	ASSERT_EQ(vGameTypes.size(), 1u);
	EXPECT_EQ(vGameTypes[0].m_Name, "old");
	EXPECT_EQ(vGameTypes[0].m_PlayTimeSeconds, 99);
	ASSERT_EQ(QmParseDdStatsPlayerResponse(DDSTATS, sizeof(DDSTATS) - 1, PLAYER, vGameTypes), EQmAxiomParseResult::SUCCESS);
	EXPECT_EQ(vGameTypes.size(), 2u);
}

TEST(QmAxiomResponseFailure, ValidDdStatsDuplicatesKeepFirstNameAndLastValue)
{
	constexpr char JSON[] = R"({"profile":{"name":"PLAYER"},"most_played_gametypes":[{"key":"Gores","seconds_played":1},{"key":"AXRace","seconds_played":2},{"key":"gores","seconds_played":3}]})";
	std::vector<SQmDdStatsGameType> vGameTypes;
	ASSERT_EQ(QmParseDdStatsPlayerResponse(JSON, sizeof(JSON) - 1, "player", vGameTypes), EQmAxiomParseResult::SUCCESS);
	ASSERT_EQ(vGameTypes.size(), 2u);
	EXPECT_EQ(vGameTypes[0].m_Name, "Gores");
	EXPECT_EQ(vGameTypes[0].m_PlayTimeSeconds, 3);
	EXPECT_EQ(vGameTypes[1].m_Name, "AXRace");
	EXPECT_EQ(vGameTypes[1].m_PlayTimeSeconds, 2);
}

TEST(QmAxiomResponseFailure, SuccessfulEmptyDdStatsListClearsOldResult)
{
	constexpr char JSON[] = R"({"profile":{"name":"player"},"most_played_gametypes":[]})";
	std::vector<SQmDdStatsGameType> vGameTypes{{"old", 99}};
	ASSERT_EQ(QmParseDdStatsPlayerResponse(JSON, sizeof(JSON) - 1, "player", vGameTypes), EQmAxiomParseResult::SUCCESS);
	EXPECT_TRUE(vGameTypes.empty());
}

TEST(QmAxiomResponseFailure, ManyDdStatsNamesKeepInputOrderAndCaseInsensitiveReplacement)
{
	std::string Json = R"({"profile":{"name":"player"},"most_played_gametypes":[)";
	for(int Index = 0; Index < 512; ++Index)
	{
		if(Index > 0)
			Json += ',';
		Json += "{\"key\":\"Mode_" + std::to_string(Index) + "\",\"seconds_played\":" + std::to_string(Index) + '}';
	}
	Json += R"(,{"key":"mode_256","seconds_played":999}]})";
	std::vector<SQmDdStatsGameType> vGameTypes;
	ASSERT_EQ(QmParseDdStatsPlayerResponse(Json.data(), Json.size(), "player", vGameTypes), EQmAxiomParseResult::SUCCESS);
	ASSERT_EQ(vGameTypes.size(), 512u);
	for(size_t Index = 0; Index < vGameTypes.size(); ++Index)
	{
		SCOPED_TRACE(Index);
		EXPECT_EQ(vGameTypes[Index].m_Name, "Mode_" + std::to_string(Index));
		EXPECT_EQ(vGameTypes[Index].m_PlayTimeSeconds, Index == 256 ? 999 : static_cast<int64_t>(Index));
	}
}

TEST(QmAxiomResponseFailure, SearchDoesNotMatchTruncatedPlayerOrDummyName)
{
	const char *apJson[] = {
		R"({"code":200,"data":{"results":[{"user_id":7,"player_name":"player\u0000other","dummy_name":""}]}})",
		R"({"code":200,"data":{"results":[{"user_id":7,"player_name":"other","dummy_name":"player\u0000other"}]}})"};
	for(const char *pJson : apJson)
	{
		SCOPED_TRACE(pJson);
		SQmAxiomSearchMatch Match{99, "old", ""};
		EXPECT_EQ(QmParseAxiomSearchResponse(pJson, std::strlen(pJson), "player", Match), EQmAxiomParseResult::NOT_FOUND);
		EXPECT_EQ(Match.m_UserId, 99);
	}
}

TEST(QmAxiomResponseFailure, SearchIgnoresFieldNameWithEmbeddedZeroBeforeExactField)
{
	constexpr char JSON[] = R"({"code\u0000extra":500,"code":200,"data":{"results":[{"user_id\u0000extra":99,"user_id":7,"player_name":"player"}]}})";
	SQmAxiomSearchMatch Match;
	ASSERT_EQ(QmParseAxiomSearchResponse(JSON, sizeof(JSON) - 1, "player", Match), EQmAxiomParseResult::SUCCESS);
	EXPECT_EQ(Match.m_UserId, 7);
}

TEST(QmAxiomResponseFailure, DdStatsRejectsTruncatedProfileAndGametypeNames)
{
	const char *apJson[] = {
		R"({"profile":{"name":"player\u0000other"},"most_played_gametypes":[]})",
		R"({"profile":{"name":"player"},"most_played_gametypes":[{"key":"Gores\u0000other","seconds_played":1}]})"};
	for(const char *pJson : apJson)
	{
		SCOPED_TRACE(pJson);
		std::vector<SQmDdStatsGameType> vGameTypes{{"old", 99}};
		EXPECT_EQ(QmParseDdStatsPlayerResponse(pJson, std::strlen(pJson), "player", vGameTypes), EQmAxiomParseResult::INVALID_RESPONSE);
		ASSERT_EQ(vGameTypes.size(), 1u);
		EXPECT_EQ(vGameTypes[0].m_Name, "old");
	}
}

TEST(QmAxiomResponseFailure, DdStatsDoesNotAcceptTruncatedRequiredFieldName)
{
	constexpr char JSON[] = R"({"profile\u0000extra":{"name":"player"},"most_played_gametypes":[]})";
	std::vector<SQmDdStatsGameType> vGameTypes{{"old", 99}};
	EXPECT_EQ(QmParseDdStatsPlayerResponse(JSON, sizeof(JSON) - 1, "player", vGameTypes), EQmAxiomParseResult::INVALID_RESPONSE);
	ASSERT_EQ(vGameTypes.size(), 1u);
	EXPECT_EQ(vGameTypes[0].m_Name, "old");
}

TEST(QmAxiomResponseFailure, InfoRejectsTruncatedPlayerNameWithoutReplacingScore)
{
	constexpr char JSON[] = R"({"code":200,"data":{"player":{"player_name":"player\u0000other","points":42,"total_play_time":0,"total_maps_completed":0,"performance_points":0,"mileage":0},"difficultyData":{}}})";
	SQmAxiomModeScore Score;
	Score.m_Points = 99;
	EXPECT_EQ(QmParseAxiomInfoResponse(JSON, sizeof(JSON) - 1, Score), EQmAxiomParseResult::INVALID_RESPONSE);
	EXPECT_EQ(Score.m_Points, 99);
}

TEST(QmAxiomResponseFailure, InfoRejectsTruncatedDifficultyKeyWithoutReplacingScore)
{
	constexpr char JSON[] = R"({"code":200,"data":{"player":{"player_name":"player","points":42,"total_play_time":0,"total_maps_completed":0,"performance_points":0,"mileage":0},"difficultyData":{"Gores\u0000other":{"stats":{"points":1,"completed_maps":1,"remaining_maps":1}}}}})";
	SQmAxiomModeScore Score;
	Score.m_Points = 99;
	EXPECT_EQ(QmParseAxiomInfoResponse(JSON, sizeof(JSON) - 1, Score), EQmAxiomParseResult::INVALID_RESPONSE);
	EXPECT_EQ(Score.m_Points, 99);
}

namespace
{
	std::string DdStatsTypes(unsigned Count, bool Unique)
	{
		std::string Text = std::string("{\"profile\":{\"name\":\"") + PLAYER + "\"},\"most_played_gametypes\":[";
		for(unsigned Index = 0; Index < Count; ++Index)
		{
			if(Index)
				Text += ',';
			Text += "{\"key\":\"Type_" + std::to_string(Unique ? Index : 0) + "\",\"seconds_played\":" + std::to_string(Index) + '}';
		}
		return Text + "]}";
	}

	enum class EBudgetResponse
	{
		SEARCH,
		INFO,
		DDSTATS,
	};

	class QmAxiomParseBudget : public testing::TestWithParam<EBudgetResponse>
	{
	};
}

TEST(QmAxiomResponseBudget, UniqueTypeLimitKeepsOrderAndDuplicateReplacement)
{
	std::string Text = DdStatsTypes(1024, true);
	Text.insert(Text.size() - 2, R"(,{"key":"type_512","seconds_played":9999})");
	std::vector<SQmDdStatsGameType> Types;
	ASSERT_EQ(QmParseDdStatsPlayerResponse(Text.data(), Text.size(), PLAYER, Types), EQmAxiomParseResult::SUCCESS);
	ASSERT_EQ(Types.size(), 1024u);
	EXPECT_EQ(Types.front().m_Name, "Type_0");
	EXPECT_EQ(Types[512].m_Name, "Type_512");
	EXPECT_EQ(Types[512].m_PlayTimeSeconds, 9999);
	EXPECT_EQ(Types.back().m_Name, "Type_1023");
}

TEST(QmAxiomResponseBudget, UniqueTypeOverflowKeepsOldOutputAndAllowsRetry)
{
	const std::string Text = DdStatsTypes(1025, true);
	std::vector<SQmDdStatsGameType> Types{{"old", 99}};
	EXPECT_EQ(QmParseDdStatsPlayerResponse(Text.data(), Text.size(), PLAYER, Types), EQmAxiomParseResult::INVALID_RESPONSE);
	ASSERT_EQ(Types.size(), 1u);
	EXPECT_EQ(Types.front().m_Name, "old");
	ASSERT_EQ(QmParseDdStatsPlayerResponse(DDSTATS, sizeof(DDSTATS) - 1, PLAYER, Types), EQmAxiomParseResult::SUCCESS);
	EXPECT_EQ(Types.size(), 2u);
}

TEST(QmAxiomResponseBudget, RawRecordLimitAllowsDuplicateTypes)
{
	const std::string Text = DdStatsTypes(4096, false);
	std::vector<SQmDdStatsGameType> Types;
	ASSERT_EQ(QmParseDdStatsPlayerResponse(Text.data(), Text.size(), PLAYER, Types), EQmAxiomParseResult::SUCCESS);
	ASSERT_EQ(Types.size(), 1u);
	EXPECT_EQ(Types.front().m_PlayTimeSeconds, 4095);
}

TEST(QmAxiomResponseBudget, RawRecordOverflowKeepsOldOutput)
{
	const std::string Text = DdStatsTypes(4097, false);
	std::vector<SQmDdStatsGameType> Types{{"old", 99}};
	EXPECT_EQ(QmParseDdStatsPlayerResponse(Text.data(), Text.size(), PLAYER, Types), EQmAxiomParseResult::INVALID_RESPONSE);
	ASSERT_EQ(Types.size(), 1u);
	EXPECT_EQ(Types.front().m_Name, "old");
	EXPECT_EQ(Types.front().m_PlayTimeSeconds, 99);
}

TEST_P(QmAxiomParseBudget, DenseIgnoredArrayExhaustsTreeBudgetWithoutReplacingOutput)
{
	const char *pValid = GetParam() == EBudgetResponse::SEARCH ? SEARCH : GetParam() == EBudgetResponse::INFO ? INFO :
														    DDSTATS;
	std::string Dense = pValid;
	Dense.pop_back();
	Dense += ",\"unused\":[";
	// 正文约4 MiB，小节点的解析树超出32 MiB；该字段不参与业务筛选。
	for(unsigned Index = 0; Index < 2 * 1024 * 1024; ++Index)
		Dense += "0,";
	Dense += "0]}";
	ASSERT_LT(Dense.size(), 8u * 1024 * 1024);
	if(GetParam() == EBudgetResponse::SEARCH)
	{
		SQmAxiomSearchMatch Match;
		Match.m_UserId = 99;
		EXPECT_EQ(QmParseAxiomSearchResponse(Dense.data(), Dense.size(), PLAYER, Match), EQmAxiomParseResult::INVALID_RESPONSE);
		EXPECT_EQ(Match.m_UserId, 99);
		EXPECT_EQ(QmParseAxiomSearchResponse(SEARCH, sizeof(SEARCH) - 1, PLAYER, Match), EQmAxiomParseResult::SUCCESS);
	}
	else if(GetParam() == EBudgetResponse::INFO)
	{
		SQmAxiomModeScore Score;
		Score.m_Points = 99;
		EXPECT_EQ(QmParseAxiomInfoResponse(Dense.data(), Dense.size(), Score), EQmAxiomParseResult::INVALID_RESPONSE);
		EXPECT_EQ(Score.m_Points, 99);
		EXPECT_EQ(QmParseAxiomInfoResponse(INFO, sizeof(INFO) - 1, Score), EQmAxiomParseResult::SUCCESS);
	}
	else
	{
		std::vector<SQmDdStatsGameType> Types{{"old", 99}};
		EXPECT_EQ(QmParseDdStatsPlayerResponse(Dense.data(), Dense.size(), PLAYER, Types), EQmAxiomParseResult::INVALID_RESPONSE);
		ASSERT_EQ(Types.size(), 1u);
		EXPECT_EQ(Types.front().m_Name, "old");
		EXPECT_EQ(QmParseDdStatsPlayerResponse(DDSTATS, sizeof(DDSTATS) - 1, PLAYER, Types), EQmAxiomParseResult::SUCCESS);
	}
}

INSTANTIATE_TEST_SUITE_P(ResponseKinds, QmAxiomParseBudget,
	testing::Values(EBudgetResponse::SEARCH, EBudgetResponse::INFO, EBudgetResponse::DDSTATS));
