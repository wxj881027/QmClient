#include <base/str.h>
#include <engine/shared/json.h>
#include <game/client/components/qmclient/ddnet_activity.h>

#include <gtest/gtest.h>
#include <limits>

namespace
{
	int64_t ParseActivity(const char *pJson)
	{
		json_value *pRoot = JsonParse(pJson, str_length(pJson));
		const int64_t Result = QmDdnetActivityHours(pRoot);
		if(pRoot)
			json_value_free(pRoot);
		return Result;
	}
}

TEST(QmDdnetActivity, PlaytimeSumsAllActivityHoursInsteadOfPastYearTotal)
{
	const auto Result = ParseActivity(R"({"points":{"points":123},"activity":[{"date":"2024-01-01","hours_played":10},{"date":"2025-01-01","hours_played":25},{"hours_played":7}],"hours_played_past_365_days":32})");
	EXPECT_EQ(Result, 42);
}

TEST(QmDdnetActivity, EmptyActivityRepresentsZeroPlaytime)
{
	const auto Result = ParseActivity(R"({"activity":[]})");
	EXPECT_EQ(Result, 0);
}

TEST(QmDdnetActivity, ZeroHourActivityIsAvailable)
{
	const auto Result = ParseActivity(R"({"activity":[{"hours_played":0}]})");
	EXPECT_EQ(Result, 0);
}

TEST(QmDdnetActivity, MissingOrWrongTypeActivityIsUnavailable)
{
	for(const char *pJson : {"{}", R"({"activity":null})", R"({"activity":{}})", R"({"activity":"42"})"})
	{
		SCOPED_TRACE(pJson);
		EXPECT_EQ(ParseActivity(pJson), -1);
	}
}

TEST(QmDdnetActivity, InvalidActivityEntriesDoNotContributeHours)
{
	const auto Result = ParseActivity(R"({"activity":[null,8,{},{"hours_played":-5},{"hours_played":"5"},{"hours_played":1.5},{"hours_played":true},{"hours_played":12},{"hours_played":30}]})");
	EXPECT_EQ(Result, 42);
}

TEST(QmDdnetActivity, ActivityWithoutAnyValidHoursIsUnavailable)
{
	const auto Result = ParseActivity(R"({"activity":[null,{},{"hours_played":-1}]})");
	EXPECT_EQ(Result, -1);
}

TEST(QmDdnetActivity, ActivityHoursKeepInt64Precision)
{
	const auto Result = ParseActivity(R"({"activity":[{"hours_played":2147483647},{"hours_played":1}]})");
	EXPECT_EQ(Result, int64_t{2147483648});
}

TEST(QmDdnetActivity, MaximumTotalIsAvailableButOverflowIsUnavailable)
{
	const auto Maximum = ParseActivity(R"({"activity":[{"hours_played":9223372036854775807},{"hours_played":0}]})");
	EXPECT_EQ(Maximum, std::numeric_limits<int64_t>::max());
	const auto Overflow = ParseActivity(R"({"activity":[{"hours_played":9223372036854775807},{"hours_played":1}]})");
	EXPECT_EQ(Overflow, -1);
}

TEST(QmDdnetActivity, RepeatedParsingDoesNotAccumulatePreviousResponses)
{
	EXPECT_EQ(ParseActivity(R"({"activity":[{"hours_played":42}]})"), 42);
	EXPECT_EQ(ParseActivity(R"({"activity":[{"hours_played":5}]})"), 5);
	EXPECT_EQ(ParseActivity("{"), -1);
}
