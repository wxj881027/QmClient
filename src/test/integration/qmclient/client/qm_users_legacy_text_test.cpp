#include <engine/shared/json.h>

#include <game/client/components/qmclient/qm_realtime.h>
#include <game/client/components/qmclient/qmclient_utils.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace
{
	SQmClientUsersParseResult Parse(const std::string &Json)
	{
		std::unique_ptr<json_value, decltype(&json_value_free)> Root(json_parse(Json.data(), Json.size()), json_value_free);
		EXPECT_NE(Root, nullptr);
		SQmClientUsersParseResult Result;
		EXPECT_TRUE(ParseQmClientUsersJson(Root.get(), "one:8303", Result));
		return Result;
	}

	std::string WithGoodRow(const std::string &BadRow)
	{
		return "[" + BadRow + R"(,{"server_address":"one:8303","player_name":"Good"}])";
	}

	void ExpectOnlyGood(const SQmClientUsersParseResult &Result)
	{
		EXPECT_TRUE(Result.m_Parsed);
		EXPECT_EQ(Result.m_OnlineUserCount, 1);
		EXPECT_EQ(Result.m_OnlineDummyCount, 0);
		ASSERT_EQ(Result.m_vServerDistribution.size(), 1U);
		EXPECT_EQ(Result.m_vServerDistribution.front().m_ServerAddress, "one:8303");
		ASSERT_EQ(Result.m_vLocalServerMarks.size(), 1U);
		EXPECT_EQ(Result.m_vLocalServerMarks.front().m_Name, "Good");
	}
}

TEST(QmUsersLegacyText, NullSuffixedNameIsSkippedInsteadOfMarkingPrefixPlayer)
{
	for(const char *pField : {"player_name", "name"})
	{
		SCOPED_TRACE(pField);
		const auto Row = std::string(R"({"server_address":"one:8303",")") + pField + R"(":"Alice\u0000Bob"})";
		ExpectOnlyGood(Parse(WithGoodRow(Row)));
	}
}

TEST(QmUsersLegacyText, NullSuffixedAddressCannotBecomeCurrentServerPrefix)
{
	for(const char *pField : {"server_address", "server"})
	{
		SCOPED_TRACE(pField);
		const auto Row = std::string("{\"") + pField + R"(":"one:8303\u0000other","player_name":"Bad"})";
		ExpectOnlyGood(Parse(WithGoodRow(Row)));
	}
}

TEST(QmUsersLegacyText, InvalidUtf8InRequiredIdentitySkipsOnlyThatRow)
{
	for(const char *pField : {"server_address", "server", "player_name", "name"})
	{
		SCOPED_TRACE(pField);
		const bool Address = std::string(pField) == "server_address" || std::string(pField) == "server";
		const auto Row = std::string("{\"") + pField + "\":\"bad\x80\"," +
				 (Address ? R"("player_name":"Bad"})" : R"("server_address":"one:8303"})");
		ExpectOnlyGood(Parse(WithGoodRow(Row)));
	}
}

TEST(QmUsersLegacyText, InvalidOptionalQidIsIgnoredWhileNormalMarkIsKept)
{
	for(const std::string Qid : {std::string("qid\\u0000other"), std::string("bad\x80")})
	{
		const auto Result = Parse(std::string(R"([{"server_address":"one:8303","player_name":"Alice","qid":")") + Qid + R"(","client_type":"arg"}])");
		EXPECT_EQ(Result.m_OnlineUserCount, 1);
		ASSERT_EQ(Result.m_vLocalServerMarks.size(), 1U);
		EXPECT_EQ(Result.m_vLocalServerMarks.front().m_Name, "Alice");
		EXPECT_TRUE(Result.m_vLocalServerMarks.front().m_Qid.empty());
		EXPECT_EQ(Result.m_vLocalServerMarks.front().m_ClientBrand, EClientBrand::ARG);
	}
}

TEST(QmUsersLegacyText, InvalidOptionalBrandUsesExistingQmFallback)
{
	for(const char *pField : {"client_type", "type"})
	{
		const auto Result = Parse(std::string(R"([{"server_address":"one:8303","player_name":"Alice",")") + pField + R"(":"arg\u0000other","qid":"valid"}])");
		ASSERT_EQ(Result.m_vLocalServerMarks.size(), 1U);
		EXPECT_EQ(Result.m_vLocalServerMarks.front().m_ClientBrand, EClientBrand::QM);
		EXPECT_EQ(Result.m_vLocalServerMarks.front().m_Qid, "valid");
	}
}

TEST(QmUsersLegacyText, NullSuffixedRowMemberCannotSupplyRequiredIdentity)
{
	ExpectOnlyGood(Parse(WithGoodRow(R"({"server_address\u0000extra":"one:8303","player_name":"Bad"})")));
	ExpectOnlyGood(Parse(WithGoodRow(R"({"server_address":"one:8303","player_name\u0000extra":"Bad"})")));
}

TEST(QmUsersLegacyText, UnknownPrefixMembersDoNotShadowExactFieldsOrAliases)
{
	const auto Result = Parse(R"([{"server_address\u0000extra":"wrong","server":"one:8303","player_name\u0000extra":"Wrong","name":"Exact","qid\u0000extra":"wrong","qid":"right"}])");
	ASSERT_EQ(Result.m_vLocalServerMarks.size(), 1U);
	EXPECT_EQ(Result.m_vLocalServerMarks.front().m_Name, "Exact");
	EXPECT_EQ(Result.m_vLocalServerMarks.front().m_Qid, "right");
	EXPECT_EQ(Result.m_OnlineUserCount, 1);
}

TEST(QmUsersLegacyText, NullSuffixedRootUsersMemberDoesNotCountAsUsersArray)
{
	const std::string Json = R"({"users\u0000extra":[{"server_address":"one:8303","player_name":"Wrong"}]})";
	std::unique_ptr<json_value, decltype(&json_value_free)> Root(json_parse(Json.data(), Json.size()), json_value_free);
	ASSERT_NE(Root, nullptr);
	SQmClientUsersParseResult Result;
	EXPECT_FALSE(ParseQmClientUsersJson(Root.get(), "one:8303", Result));
	EXPECT_FALSE(Result.m_Parsed);
}

TEST(QmUsersLegacyText, RealtimeContextRejectsNullPrefixAndInvalidUtf8Addresses)
{
	for(const std::string Address : {std::string("users\\u0000other"), std::string("one:8303\\u0000other"), std::string("bad\x80")})
	{
		const auto Json = std::string(R"({"type":"users","data":{"server_address":")") + Address + R"(","users":[]}})";
		SQmRealtimeMessage Message;
		ASSERT_TRUE(ParseQmRealtimeMessage(Json.data(), Json.size(), Message));
		EXPECT_FALSE(IsQmClientUsersSnapshotForContext(Message.m_pPayload.get(), ""));
		EXPECT_FALSE(IsQmClientUsersSnapshotForContext(Message.m_pPayload.get(), "one:8303"));
	}
}

TEST(QmUsersLegacyText, ValidUtf8AndExistingBrandAliasesKeepCompleteText)
{
	const std::string Name = std::string(80, 'x') + "中文😀";
	const auto Result = Parse(std::string(R"([{"server":"one:8303","name":")") + Name + R"(","qid":"账号😀","type":"ArGhEnA"}])");
	ASSERT_EQ(Result.m_vLocalServerMarks.size(), 1U);
	EXPECT_EQ(Result.m_vLocalServerMarks.front().m_Name, Name);
	EXPECT_EQ(Result.m_vLocalServerMarks.front().m_Qid, "账号😀");
	EXPECT_EQ(Result.m_vLocalServerMarks.front().m_ClientBrand, EClientBrand::ARG);
}

TEST(QmUsersLegacyText, MalformedPrimaryFieldDoesNotFallBackToSecondaryAlias)
{
	const auto Name = Parse(R"([{"server_address":"one:8303","player_name":"Bad\u0000suffix","name":"Good"}])");
	const auto Server = Parse(R"([{"server_address":"one:8303\u0000suffix","server":"one:8303","player_name":"Good"}])");
	EXPECT_EQ(Name.m_OnlineUserCount, 0);
	EXPECT_TRUE(Name.m_vLocalServerMarks.empty());
	EXPECT_EQ(Server.m_OnlineUserCount, 0);
}

TEST(QmUsersLegacyText, UnknownNullSuffixedFlagNamesKeepExistingDefaults)
{
	const auto Result = Parse(R"([{"server_address":"one:8303","player_name":"Alice","foot_particles_enabled\u0000extra":true,"remote_particles_enabled\u0000extra":true,"voice_supported\u0000extra":false}])");
	ASSERT_EQ(Result.m_vLocalServerMarks.size(), 1U);
	EXPECT_FALSE(Result.m_vLocalServerMarks.front().m_FootParticlesEnabled);
	EXPECT_FALSE(Result.m_vLocalServerMarks.front().m_RemoteParticlesEnabled);
	EXPECT_TRUE(Result.m_vLocalServerMarks.front().m_VoiceSupported);
}
