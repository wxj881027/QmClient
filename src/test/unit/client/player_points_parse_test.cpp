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

TEST(PlayerPointsParse, PlaytimeSumsAllActivityHoursInsteadOfPastYearTotal)
{
	const auto Result = ParsePoints(R"({"points":{"points":123},"activity":[{"date":"2024-01-01","hours_played":10},{"date":"2025-01-01","hours_played":25},{"hours_played":7}],"hours_played_past_365_days":32})");
	EXPECT_EQ(Result.m_Points, 123);
	EXPECT_EQ(Result.m_PlaytimeHours, 42);
}

TEST(PlayerPointsParse, EmptyActivityRepresentsZeroPlaytime)
{
	const auto Result = ParsePoints(R"({"activity":[]})");
	EXPECT_EQ(Result.m_PlaytimeHours, 0);
}

TEST(PlayerPointsParse, ZeroHourActivityIsAvailable)
{
	const auto Result = ParsePoints(R"({"activity":[{"hours_played":0}]})");
	EXPECT_EQ(Result.m_PlaytimeHours, 0);
}

TEST(PlayerPointsParse, MissingOrWrongTypeActivityIsUnavailable)
{
	for(const char *pJson : {"{}", R"({"activity":null})", R"({"activity":{}})", R"({"activity":"42"})"})
	{
		SCOPED_TRACE(pJson);
		EXPECT_EQ(ParsePoints(pJson).m_PlaytimeHours, -1);
	}
}

TEST(PlayerPointsParse, InvalidActivityEntriesDoNotContributeHours)
{
	const auto Result = ParsePoints(R"({"activity":[null,8,{},{"hours_played":-5},{"hours_played":"5"},{"hours_played":1.5},{"hours_played":true},{"hours_played":12},{"hours_played":30}]})");
	EXPECT_EQ(Result.m_PlaytimeHours, 42);
}

TEST(PlayerPointsParse, ActivityWithoutAnyValidHoursIsUnavailable)
{
	const auto Result = ParsePoints(R"({"activity":[null,{},{"hours_played":-1}]})");
	EXPECT_EQ(Result.m_PlaytimeHours, -1);
}

TEST(PlayerPointsParse, ActivityHoursKeepInt64Precision)
{
	const auto Result = ParsePoints(R"({"activity":[{"hours_played":2147483647},{"hours_played":1}]})");
	EXPECT_EQ(Result.m_PlaytimeHours, int64_t{2147483648});
}

TEST(PlayerPointsParse, MaximumTotalIsAvailableButOverflowIsUnavailable)
{
	const auto Maximum = ParsePoints(R"({"activity":[{"hours_played":9223372036854775807},{"hours_played":0}]})");
	EXPECT_EQ(Maximum.m_PlaytimeHours, std::numeric_limits<int64_t>::max());
	const auto Overflow = ParsePoints(R"({"activity":[{"hours_played":9223372036854775807},{"hours_played":1}]})");
	EXPECT_EQ(Overflow.m_PlaytimeHours, -1);
}

TEST(PlayerPointsParse, RepeatedParsingDoesNotAccumulatePreviousResponses)
{
	EXPECT_EQ(ParsePoints(R"({"activity":[{"hours_played":42}]})").m_PlaytimeHours, 42);
	EXPECT_EQ(ParsePoints(R"({"activity":[{"hours_played":5}]})").m_PlaytimeHours, 5);
	EXPECT_EQ(ParsePoints("{").m_PlaytimeHours, -1);
}
