#include <engine/shared/json.h>
#include <engine/shared/protocol.h>

#include <game/client/components/qmclient/qmclient_utils.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

namespace
{
	std::string Row(const std::string &Field = "", const std::string &Value = "", bool SuffixName = false)
	{
		const char *apNames[] = {"developer_id", "server_address", "player_id", "player_name", "dummy", "issued_at", "expires_at", "style_bucket"};
		const char *apValues[] = {"\"dev\"", "\"one\"", "3", "\"Name\"", "false", "900", "1100", "2"};
		std::string Json = "{";
		for(int Index = 0; Index < 8; ++Index)
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

	bool Parse(const std::string &Json, SQmDeveloperPresenceParseResult &Out, const char *pAddress = "one")
	{
		// 不预先净化编码，使用真实消费模块判断异常身份字段。
		std::unique_ptr<json_value, decltype(&json_value_free)> Root(json_parse(Json.data(), Json.size()), json_value_free);
		EXPECT_NE(Root, nullptr);
		return ParseQmDeveloperPresencesJson(Root.get(), pAddress, Out);
	}

	void ExpectOnlyGood(const std::string &BadRow)
	{
		SQmDeveloperPresenceParseResult Result;
		ASSERT_TRUE(Parse(Snapshot(BadRow + ',' + Row("player_id", "4")), Result));
		ASSERT_EQ(Result.m_vPresences.size(), 1u);
		EXPECT_EQ(Result.m_vPresences.front().m_PlayerId, 4);
	}
}

TEST(QmDeveloperPresenceText, MalformedRootClearsOutputInsteadOfPublishingParsedPrefix)
{
	for(const char *pJson : {"null", "[]", R"({"server_time":1000})", R"({"server_time":1000,"presences":{}})", R"({"server_time":0,"presences":[]})",
		    R"({"server_time\u0000extra":1000,"presences":[]})", R"({"server_time":1000,"presences\u0000extra":[]})"})
	{
		SCOPED_TRACE(pJson);
		SQmDeveloperPresenceParseResult Result{true, 500, {SQmDeveloperPresence{}}};
		EXPECT_FALSE(Parse(pJson, Result));
		EXPECT_FALSE(Result.m_Parsed);
		EXPECT_EQ(Result.m_ServerTime, 0);
		EXPECT_TRUE(Result.m_vPresences.empty());
	}
}

TEST(QmDeveloperPresenceText, EmptySnapshotRemainsSuccessful)
{
	SQmDeveloperPresenceParseResult Result;
	ASSERT_TRUE(Parse(Snapshot(""), Result));
	EXPECT_TRUE(Result.m_Parsed);
	EXPECT_EQ(Result.m_ServerTime, 1000);
	EXPECT_TRUE(Result.m_vPresences.empty());
}

TEST(QmDeveloperPresenceText, MissingLocalContextKeepsExistingSuccessfulEmptyResult)
{
	const char *apAddresses[] = {nullptr, ""};
	for(const char *pAddress : apAddresses)
	{
		SQmDeveloperPresenceParseResult Result;
		ASSERT_TRUE(Parse(Snapshot(Row()), Result, pAddress));
		EXPECT_TRUE(Result.m_Parsed);
		EXPECT_EQ(Result.m_ServerTime, 1000);
		EXPECT_TRUE(Result.m_vPresences.empty());
	}
}

TEST(QmDeveloperPresenceText, RequiredMemberNamesMustMatchCompletely)
{
	for(const char *pField : {"developer_id", "server_address", "player_id", "player_name", "dummy", "issued_at", "expires_at", "style_bucket"})
	{
		SCOPED_TRACE(pField);
		ExpectOnlyGood(Row(pField, "", true));
	}
}

TEST(QmDeveloperPresenceText, NullSuffixedIdentityCannotMatchPrefixPlayerOrServer)
{
	for(const auto &[Field, Value] : {std::pair{"developer_id", "\"dev\\u0000extra\""}, std::pair{"server_address", "\"one\\u0000extra\""}, std::pair{"player_name", "\"Name\\u0000extra\""}})
	{
		SCOPED_TRACE(Field);
		ExpectOnlyGood(Row(Field, Value));
	}
}

TEST(QmDeveloperPresenceText, InvalidUtf8IdentitySkipsOnlyBadRow)
{
	for(const char *pField : {"developer_id", "server_address", "player_name"})
	{
		SCOPED_TRACE(pField);
		for(const std::string Bad : {"\x80", "\xe4\xbd", "\xed\xa0\x80"})
		{
			SCOPED_TRACE(::testing::PrintToString(Bad));
			ExpectOnlyGood(Row(pField, '"' + Bad + '"'));
		}
	}
}

TEST(QmDeveloperPresenceText, UnknownPrefixedMembersCannotShadowExactRequiredFields)
{
	for(const char *pField : {"developer_id", "server_address", "player_id", "player_name", "dummy", "issued_at", "expires_at", "style_bucket"})
	{
		SCOPED_TRACE(pField);
		SQmDeveloperPresenceParseResult Result;
		ASSERT_TRUE(Parse(Snapshot(std::string("{\"") + pField + R"(\u0000extra":null,)" + Row().substr(1)), Result));
		ASSERT_EQ(Result.m_vPresences.size(), 1u);
		EXPECT_EQ(Result.m_vPresences.front().m_DeveloperId, "dev");
		EXPECT_EQ(Result.m_vPresences.front().m_PlayerId, 3);
	}
}

TEST(QmDeveloperPresenceText, ExistingUnicodeAndLongNameSupportIsPreserved)
{
	for(const std::string Name : {std::string("玩家😀"), std::string(256, 'x')})
	{
		SQmDeveloperPresenceParseResult Result;
		ASSERT_TRUE(Parse(Snapshot(Row("player_name", '"' + Name + '"')), Result));
		ASSERT_EQ(Result.m_vPresences.size(), 1u);
		EXPECT_EQ(Result.m_vPresences.front().m_PlayerName, Name);
		EXPECT_NE(FindQmDeveloperPresence(Result.m_vPresences, "one", 3, Name.c_str(), 1000), nullptr);
	}
}

TEST(QmDeveloperPresenceText, ExistingNumericLifetimeAndDummyRulesRemain)
{
	for(const auto &[Field, Value] : {std::pair{"player_id", "-1"}, std::pair{"style_bucket", "100"}, std::pair{"issued_at", "1001"}, std::pair{"expires_at", "1000"}, std::pair{"dummy", "0"}})
	{
		SCOPED_TRACE(Field);
		ExpectOnlyGood(Row(Field, Value));
	}
	ExpectOnlyGood(Row("player_id", std::to_string(MAX_CLIENTS)));
	SQmDeveloperPresenceParseResult Result;
	ASSERT_TRUE(Parse(Snapshot(Row("dummy", "true")), Result));
	ASSERT_EQ(Result.m_vPresences.size(), 1u);
	EXPECT_TRUE(Result.m_vPresences.front().m_Dummy);
}

TEST(QmDeveloperPresenceText, DuplicateRecordsKeepInputOrderAndFirstMatchingLookup)
{
	SQmDeveloperPresenceParseResult Result;
	ASSERT_TRUE(Parse(Snapshot(Row("developer_id", "\"first\"") + ',' + Row("developer_id", "\"second\"")), Result));
	ASSERT_EQ(Result.m_vPresences.size(), 2u);
	EXPECT_EQ(Result.m_vPresences[0].m_DeveloperId, "first");
	EXPECT_EQ(Result.m_vPresences[1].m_DeveloperId, "second");
	const auto *pFound = FindQmDeveloperPresence(Result.m_vPresences, "one", 3, "Name", 1000);
	ASSERT_NE(pFound, nullptr);
	EXPECT_EQ(pFound->m_DeveloperId, "first");
}

TEST(QmDeveloperPresenceText, ParsedRecordsOwnTextAfterJsonTreeIsReleased)
{
	SQmDeveloperPresenceParseResult Result;
	ASSERT_TRUE(Parse(Snapshot(Row("player_name", "\"玩家\"")), Result));
	ASSERT_EQ(Result.m_vPresences.size(), 1u);
	EXPECT_EQ(Result.m_vPresences.front().m_PlayerName, "玩家");
	EXPECT_EQ(Result.m_vPresences.front().m_DeveloperId, "dev");
	EXPECT_EQ(Result.m_vPresences.front().m_ServerAddress, "one");
}

TEST(QmDeveloperPresenceText, InvalidRowsDoNotDisplaceValidRecord)
{
	std::string Rows;
	for(int Index = 0; Index < 4096; ++Index)
		Rows += "null,";
	SQmDeveloperPresenceParseResult Result;
	ASSERT_TRUE(Parse(Snapshot(Rows + Row()), Result));
	ASSERT_EQ(Result.m_vPresences.size(), 1u);
	EXPECT_EQ(Result.m_vPresences.front().m_PlayerId, 3);
}

TEST(QmDeveloperPresenceText, OutputCanRecoverAfterMalformedSnapshot)
{
	SQmDeveloperPresenceParseResult Result;
	ASSERT_FALSE(Parse(R"({"server_time":1000})", Result));
	ASSERT_TRUE(Parse(Snapshot(Row()), Result));
	EXPECT_TRUE(Result.m_Parsed);
	EXPECT_EQ(Result.m_ServerTime, 1000);
	EXPECT_EQ(Result.m_vPresences.size(), 1u);
}
