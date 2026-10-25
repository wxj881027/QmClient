#include <game/client/components/player_points.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
	SPlayerPointsParseResult ParsePoints(const char *pJson)
	{
		json_value *pRoot = JsonParse(pJson, str_length(pJson));
		const auto Result = ExtractPlayerPointsJson(pRoot);
		if(pRoot)
			json_value_free(pRoot);
		return Result;
	}
}

TEST(PlayerPointsParse, IntegerPointsAreReturnedFromNestedResponse)
{
	const auto Result = ParsePoints(R"({"points":{"points":123}})");
	EXPECT_TRUE(Result.m_JsonParsed);
	EXPECT_TRUE(Result.m_PointsFound);
	EXPECT_EQ(Result.m_Points, 123);
}

TEST(PlayerPointsParse, MissingPointsPreserveLegacyZeroFallback)
{
	const auto Result = ParsePoints("{}");
	EXPECT_TRUE(Result.m_JsonParsed);
	EXPECT_TRUE(Result.m_PointsFound);
	EXPECT_EQ(Result.m_Points, 0);
}

TEST(PlayerPointsParse, StringPointsPreserveTheExistingAcceptedResponseShape)
{
	// 旧接口没有严格类型过滤；本次迁移只保留旧测试对接受形状的承诺。
	const auto Result = ParsePoints(R"({"points":{"points":"123"}})");
	EXPECT_TRUE(Result.m_JsonParsed);
	EXPECT_TRUE(Result.m_PointsFound);
}

TEST(PlayerPointsParse, MalformedJsonDoesNotReportParsedPoints)
{
	const auto Result = ParsePoints("{");
	EXPECT_FALSE(Result.m_JsonParsed);
	EXPECT_FALSE(Result.m_PointsFound);
	EXPECT_EQ(Result.m_Points, 0);
}

TEST(PlayerPlaytimeParse, DdStatsSumsAllModesBeforeRoundingToHours)
{
	const char *pJson = R"({"profile":{"name":"player"},"most_played_gametypes":[{"key":"DDrace","seconds_played":1800},{"key":"Gores","seconds_played":1800}]})";
	EXPECT_EQ(ExtractPlayerDdStatsHours(pJson, str_length(pJson), "player"), 1);
}

TEST(PlayerPlaytimeParse, EmptyDdStatsHistoryIsAnAvailableZero)
{
	const char *pJson = R"({"profile":{"name":"player"},"most_played_gametypes":[]})";
	EXPECT_EQ(ExtractPlayerDdStatsHours(pJson, str_length(pJson), "player"), 0);
}

TEST(PlayerPlaytimeParse, OtherPlayersAndInvalidTimesAreRejected)
{
	const char *pWrongPlayer = R"({"profile":{"name":"other"},"most_played_gametypes":[]})";
	EXPECT_EQ(ExtractPlayerDdStatsHours(pWrongPlayer, str_length(pWrongPlayer), "player"), -1);
	const char *pNegative = R"({"profile":{"name":"player"},"most_played_gametypes":[{"key":"DDrace","seconds_played":-1}]})";
	EXPECT_EQ(ExtractPlayerDdStatsHours(pNegative, str_length(pNegative), "player"), -1);
}

TEST(PlayerPlaytimeParse, MissingOrMalformedDdStatsIsUnavailable)
{
	EXPECT_EQ(ExtractPlayerDdStatsHours(nullptr, 0, "player"), -1);
	EXPECT_EQ(ExtractPlayerDdStatsHours("{}", 2, "player"), -1);
	EXPECT_EQ(ExtractPlayerDdStatsHours("{", 1, "player"), -1);
}

TEST(PlayerPlaytimeParse, LargeTotalsKeepInt64PrecisionAndRejectOverflow)
{
	const char *pMaximum = R"({"profile":{"name":"player"},"most_played_gametypes":[{"key":"DDrace","seconds_played":9223372036854775807}]})";
	EXPECT_EQ(ExtractPlayerDdStatsHours(pMaximum, str_length(pMaximum), "player"), std::numeric_limits<int64_t>::max() / 3600);
	const char *pOverflow = R"({"profile":{"name":"player"},"most_played_gametypes":[{"key":"DDrace","seconds_played":9223372036854775807},{"key":"Gores","seconds_played":1}]})";
	EXPECT_EQ(ExtractPlayerDdStatsHours(pOverflow, str_length(pOverflow), "player"), -1);
}

TEST(PlayerPlaytimeParse, DdnetActivityIsNotUsedForScoreboardPlaytime)
{
	const auto Result = ParsePoints(R"({"points":{"points":123},"activity":[{"hours_played":42}]})");
	EXPECT_EQ(Result.m_Points, 123);
	EXPECT_EQ(Result.m_PlaytimeHours, -1);
}
