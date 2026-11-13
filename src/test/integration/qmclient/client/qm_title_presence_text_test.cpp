#include <engine/shared/json.h>
#include <engine/shared/protocol.h>

#include <game/client/components/qmclient/qmclient_utils.h>

#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace
{
	std::string Row(const std::string &Field = "", const std::string &Value = "", bool SuffixName = false)
	{
		const char *apNames[] = {"server_address", "player_name", "title", "player_id", "issued_at", "expires_at"};
		const char *apValues[] = {"\"one\"", "\"Name\"", "\"Title\"", "3", "900", "1100"};
		std::string Json = "{";
		for(int Index = 0; Index < 6; ++Index)
		{
			if(Index)
				Json += ',';
			Json += std::string("\"") + apNames[Index] + (SuffixName && Field == apNames[Index] ? "\\u0000extra" : "") + "\":";
			Json += Field == apNames[Index] && !Value.empty() ? Value : apValues[Index];
		}
		return Json + '}';
	}

	std::string Snapshot(const std::string &Rows)
	{
		return R"({"server_time":1000,"presences":[)" + Rows + "]}";
	}

	bool Parse(const std::string &Json, SQmTitlePresenceParseResult &Out)
	{
		// 使用原始解析器，将异常 UTF-8 留给实际消费模块检查。
		std::unique_ptr<json_value, decltype(&json_value_free)> Root(json_parse(Json.data(), Json.size()), json_value_free);
		EXPECT_NE(Root, nullptr);
		return ParseQmTitlePresencesJson(Root.get(), "one", Out);
	}

	void ExpectOnlyGood(const std::string &BadRow)
	{
		SQmTitlePresenceParseResult Result;
		ASSERT_TRUE(Parse(Snapshot(BadRow + ',' + Row("player_id", "4")), Result));
		ASSERT_EQ(Result.m_vPresences.size(), 1u);
		EXPECT_EQ(Result.m_vPresences.front().m_PlayerId, 4);
	}
}

TEST(QmTitlePresenceText, MalformedSnapshotDoesNotPublishPositiveTimeOrPartialOutput)
{
	for(const char *pJson : {"null", "[]", R"({"server_time":1000})", R"({"server_time":1000,"presences":{}})", R"({"server_time":0,"presences":[]})",
		    R"({"server_time\u0000extra":1000,"presences":[]})", R"({"server_time":1000,"presences\u0000extra":[]})"})
	{
		SCOPED_TRACE(pJson);
		SQmTitlePresenceParseResult Result{true, 500, {SQmTitlePresence{}}};
		EXPECT_FALSE(Parse(pJson, Result));
		EXPECT_FALSE(Result.m_Parsed);
		EXPECT_EQ(Result.m_ServerTime, 0);
		EXPECT_TRUE(Result.m_vPresences.empty());
	}
}

TEST(QmTitlePresenceText, ValidEmptySnapshotRemainsSuccessfulAndCanClearOldTitles)
{
	SQmTitlePresenceParseResult Result;
	ASSERT_TRUE(Parse(Snapshot(""), Result));
	EXPECT_TRUE(Result.m_Parsed);
	EXPECT_EQ(Result.m_ServerTime, 1000);
	EXPECT_TRUE(Result.m_vPresences.empty());
}

TEST(QmTitlePresenceText, RequiredMemberNamesMustMatchCompletely)
{
	for(const char *pField : {"server_address", "player_name", "title", "player_id", "issued_at", "expires_at"})
	{
		SCOPED_TRACE(pField);
		ExpectOnlyGood(Row(pField, "", true));
	}
}

TEST(QmTitlePresenceText, NullSuffixedRequiredTextSkipsOnlyBadRow)
{
	for(const auto &[Field, Value] : {std::pair{"server_address", "\"one\\u0000extra\""}, std::pair{"player_name", "\"Name\\u0000extra\""}, std::pair{"title", "\"Title\\u0000extra\""}})
	{
		SCOPED_TRACE(Field);
		ExpectOnlyGood(Row(Field, Value));
	}
}

TEST(QmTitlePresenceText, InvalidUtf8RequiredTextCannotEnterRecords)
{
	for(const char *pField : {"server_address", "player_name", "title"})
	{
		SCOPED_TRACE(pField);
		for(const std::string Bad : {"\x80", "\xe4\xbd", "\xed\xa0\x80"})
		{
			SCOPED_TRACE(::testing::PrintToString(Bad));
			ExpectOnlyGood(Row(pField, '"' + Bad + '"'));
		}
	}
}

TEST(QmTitlePresenceText, UnknownPrefixedMembersCannotShadowExactRequiredFields)
{
	for(const char *pField : {"server_address", "player_name", "title", "player_id", "issued_at", "expires_at"})
	{
		SCOPED_TRACE(pField);
		const auto Json = Snapshot(std::string("{\"") + pField + R"(\u0000extra":null,)" + Row().substr(1));
		SQmTitlePresenceParseResult Result;
		ASSERT_TRUE(Parse(Json, Result));
		ASSERT_EQ(Result.m_vPresences.size(), 1u);
		EXPECT_EQ(Result.m_vPresences.front().m_Title, "Title");
		EXPECT_EQ(Result.m_vPresences.front().m_PlayerId, 3);
	}
}

TEST(QmTitlePresenceText, BadOptionalStyleTextFallsBackWithoutDroppingTitle)
{
	for(const char *pField : {"style", "chat_style"})
	{
		SCOPED_TRACE(pField);
		for(const std::string Bad : {std::string("platinum\\u0000extra"), std::string("\x80")})
		{
			SCOPED_TRACE(::testing::PrintToString(Bad));
			SQmTitlePresenceParseResult Result;
			ASSERT_TRUE(Parse(Snapshot(std::string("{\"") + pField + R"(":")" + Bad + R"(",)" + Row().substr(1)), Result));
			ASSERT_EQ(Result.m_vPresences.size(), 1u);
			EXPECT_EQ(Result.m_vPresences.front().m_Title, "Title");
			EXPECT_TRUE(Result.m_vPresences.front().m_Style.empty());
			EXPECT_EQ(Result.m_vPresences.front().m_ChatStyle, EQmSponsorChatStyle::NONE);
		}
	}
}

TEST(QmTitlePresenceText, OptionalExactMembersAndAppearanceCannotBeShadowed)
{
	const auto Json = Snapshot(R"({"style\u0000extra":"bad","style":"gold","chat_style\u0000extra":"soft_glow","chat_style":"platinum","appearance\u0000extra":{},"appearance":{"style":"sun","color":0,"opacity":0},)" + Row().substr(1));
	SQmTitlePresenceParseResult Result;
	ASSERT_TRUE(Parse(Json, Result));
	ASSERT_EQ(Result.m_vPresences.size(), 1u);
	EXPECT_EQ(Result.m_vPresences.front().m_Style, "gold");
	EXPECT_EQ(Result.m_vPresences.front().m_ChatStyle, EQmSponsorChatStyle::PLATINUM);
	EXPECT_EQ(Result.m_vPresences.front().m_Appearance, (SQmTitleAppearance{EQmTitleBadgeStyle::SUN, 0, 0}));
}

TEST(QmTitlePresenceText, ExistingNameByteLimitAndUnicodeRemainSupported)
{
	for(const std::string Name : {std::string(MAX_NAME_LENGTH - 1, 'x'), std::string("玩家😀")})
	{
		SQmTitlePresenceParseResult Result;
		ASSERT_TRUE(Parse(Snapshot(Row("player_name", '"' + Name + '"')), Result));
		ASSERT_EQ(Result.m_vPresences.size(), 1u);
		EXPECT_EQ(Result.m_vPresences.front().m_PlayerName, Name);
	}
	ExpectOnlyGood(Row("player_name", '"' + std::string(MAX_NAME_LENGTH, 'x') + '"'));
}

TEST(QmTitlePresenceText, ExistingNumericAndLifetimeLimitsRemain)
{
	for(const auto &[Field, Value] : {std::pair{"player_id", "-1"}, std::pair{"issued_at", "1001"}, std::pair{"expires_at", "1000"}})
	{
		SCOPED_TRACE(Field);
		ExpectOnlyGood(Row(Field, Value));
	}
	SQmTitlePresenceParseResult Result;
	ExpectOnlyGood(Row("player_id", std::to_string(MAX_CLIENTS)));
	ASSERT_TRUE(Parse(Snapshot(Row("expires_at", std::to_string(std::numeric_limits<int64_t>::max()))), Result));
	ASSERT_EQ(Result.m_vPresences.size(), 1u);
	EXPECT_EQ(Result.m_vPresences.front().m_RemainingSeconds, 15);
}

TEST(QmTitlePresenceText, DuplicatePlayerRowsRemainInInputOrder)
{
	SQmTitlePresenceParseResult Result;
	ASSERT_TRUE(Parse(Snapshot(Row("title", "\"First\"") + ',' + Row("title", "\"Last\"")), Result));
	ASSERT_EQ(Result.m_vPresences.size(), 2u);
	EXPECT_EQ(Result.m_vPresences[0].m_Title, "First");
	EXPECT_EQ(Result.m_vPresences[1].m_Title, "Last");
	EXPECT_EQ(Result.m_vPresences[0].m_PlayerId, Result.m_vPresences[1].m_PlayerId);
}

TEST(QmTitlePresenceText, ParsedTextIsOwnedAfterJsonTreeIsReleased)
{
	SQmTitlePresenceParseResult Result;
	ASSERT_TRUE(Parse(Snapshot(Row("player_name", "\"玩家\"")), Result));
	ASSERT_EQ(Result.m_vPresences.size(), 1u);
	EXPECT_EQ(Result.m_vPresences.front().m_PlayerName, "玩家");
	EXPECT_EQ(Result.m_vPresences.front().m_Title, "Title");
}

TEST(QmTitlePresenceText, ValidSnapshotCanReuseOutputAfterMalformedSnapshot)
{
	SQmTitlePresenceParseResult Result;
	ASSERT_FALSE(Parse(R"({"server_time":1000})", Result));
	ASSERT_TRUE(Parse(Snapshot(Row()), Result));
	EXPECT_TRUE(Result.m_Parsed);
	EXPECT_EQ(Result.m_ServerTime, 1000);
	EXPECT_EQ(Result.m_vPresences.size(), 1u);
}
