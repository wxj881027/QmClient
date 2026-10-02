#include <game/client/components/player_points.h>

#include <gtest/gtest.h>

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
