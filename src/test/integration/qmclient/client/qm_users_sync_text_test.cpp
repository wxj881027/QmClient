#include <game/client/components/qmclient/qm_realtime.h>
#include <game/client/components/qmclient/qm_realtime_users.h>

#include <gtest/gtest.h>
#include <zlib.h>

#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace
{
	using EResult = CQmRealtimeUsersState::EApplyResult;
	constexpr const char *BASE = R"({"server_address":"one:8303","revision":1,"base_revision":0,"full":true,"lease_seconds":20,"servers":[["one:8303",1,0],["two:8303",3,1]],"players":[["p1","Alice","qm","old-qid",true]],"removed_servers":[],"removed_players":[]})";
	constexpr const char *DELTA = R"({"server_address":"ROOM","revision":2,"base_revision":1,"full":false,"lease_seconds":15,"servers":[["SERVER_KEY",2,0]],"players":[["PLAYER_KEY","PLAYER_NAME","PLAYER_BRAND","QID",true]],"removed_servers":["REMOVED_SERVER"],"removed_players":["REMOVED_PLAYER"]})";

	std::string Delta(const char *pChanged = "", const std::string &Value = "")
	{
		std::string Json = DELTA;
		const std::pair<const char *, const char *> aDefaults[] = {
			{"ROOM", "one:8303"}, {"SERVER_KEY", "one:8303"}, {"PLAYER_KEY", "p1"}, {"PLAYER_NAME", "Next"},
			{"PLAYER_BRAND", "qm"}, {"QID", "next-qid"}, {"REMOVED_SERVER", "two:8303"}, {"REMOVED_PLAYER", "gone"}};
		for(const auto &[pMarker, pDefault] : aDefaults)
			Json.replace(Json.find(pMarker), std::strlen(pMarker), std::strcmp(pMarker, pChanged) == 0 ? Value : pDefault);
		return Json;
	}

	EResult Apply(CQmRealtimeUsersState &State, const std::string &Json, int *pPublished = nullptr)
	{
		// 故意使用原始解析器，将合法 JSON 中的异常 UTF-8 交给实际名单模块检查。
		std::unique_ptr<json_value, decltype(&json_value_free)> Root(json_parse(Json.data(), Json.size()), json_value_free);
		EXPECT_NE(Root, nullptr);
		return State.ApplyAndPublish(Root.get(), "one:8303", [pPublished](auto, int) {
			if(pPublished)
				++*pPublished;
			return true;
		});
	}

	void ExpectPrevious(const CQmRealtimeUsersState &State)
	{
		EXPECT_EQ(State.Revision(), 1);
		EXPECT_EQ(State.LeaseSeconds(), 20);
		EXPECT_TRUE(State.NeedsFull());
		const auto Result = State.Result();
		EXPECT_EQ(Result.m_OnlineUserCount, 4);
		EXPECT_EQ(Result.m_OnlineDummyCount, 1);
		ASSERT_EQ(Result.m_vServerDistribution.size(), 2U);
		ASSERT_EQ(Result.m_vLocalServerMarks.size(), 1U);
		EXPECT_EQ(Result.m_vLocalServerMarks.front().m_Name, "Alice");
	}

	std::string Frame(const std::string &Json)
	{
		uLongf Size = compressBound(static_cast<uLong>(Json.size()));
		std::string Compressed(Size, '\0');
		EXPECT_EQ(compress2(reinterpret_cast<Bytef *>(Compressed.data()), &Size, reinterpret_cast<const Bytef *>(Json.data()), static_cast<uLong>(Json.size()), 1), Z_OK);
		Compressed.resize(Size);
		std::string Result = "QMU1";
		for(int Shift : {24, 16, 8, 0})
			Result.push_back(static_cast<char>(Json.size() >> Shift));
		return Result + Compressed;
	}
}

TEST(QmUsersSyncText, InvalidUtf8InAnyIdentityFieldRejectsUpdateWithoutPublishing)
{
	for(const char *pMarker : {"ROOM", "SERVER_KEY", "PLAYER_KEY", "PLAYER_NAME", "PLAYER_BRAND", "QID", "REMOVED_SERVER", "REMOVED_PLAYER"})
	{
		SCOPED_TRACE(pMarker);
		CQmRealtimeUsersState State;
		ASSERT_EQ(Apply(State, BASE), EResult::APPLIED);
		int Published = 0;
		EXPECT_EQ(Apply(State, Delta(pMarker, "bad\x80"), &Published), EResult::RESYNC);
		EXPECT_EQ(Published, 0);
		ExpectPrevious(State);
	}
}

TEST(QmUsersSyncText, EmbeddedNullCannotTurnAFieldIntoItsPrefix)
{
	for(const char *pMarker : {"ROOM", "SERVER_KEY", "PLAYER_KEY", "PLAYER_NAME", "PLAYER_BRAND", "QID", "REMOVED_SERVER", "REMOVED_PLAYER"})
	{
		SCOPED_TRACE(pMarker);
		CQmRealtimeUsersState State;
		ASSERT_EQ(Apply(State, BASE), EResult::APPLIED);
		EXPECT_EQ(Apply(State, Delta(pMarker, "prefix\\u0000suffix")), EResult::RESYNC);
		ExpectPrevious(State);
	}
}

TEST(QmUsersSyncText, NullSuffixedRequiredMemberNameIsNotTheRequiredMember)
{
	for(const char *pName : {"server_address", "revision", "base_revision", "full", "lease_seconds", "servers", "players", "removed_servers", "removed_players"})
	{
		SCOPED_TRACE(pName);
		CQmRealtimeUsersState State;
		ASSERT_EQ(Apply(State, BASE), EResult::APPLIED);
		auto Json = Delta();
		const auto Needle = std::string("\"") + pName + "\"";
		Json.replace(Json.find(Needle), Needle.size(), std::string("\"") + pName + "\\u0000extra\"");
		EXPECT_EQ(Apply(State, Json), EResult::RESYNC);
		ExpectPrevious(State);
	}
}

TEST(QmUsersSyncText, UnknownNullSuffixedMemberDoesNotShadowLaterExactMember)
{
	CQmRealtimeUsersState State;
	ASSERT_EQ(Apply(State, BASE), EResult::APPLIED);
	auto Json = Delta();
	Json.insert(1, R"("players\u0000extra":false,)");
	ASSERT_EQ(Apply(State, Json), EResult::APPLIED);
	EXPECT_EQ(State.Revision(), 2);
	ASSERT_EQ(State.Result().m_vLocalServerMarks.size(), 1U);
	EXPECT_EQ(State.Result().m_vLocalServerMarks.front().m_Name, "Next");
}

TEST(QmUsersSyncText, CorrectFullSnapshotRecoversAfterTextFailureBeforeDeltasResume)
{
	CQmRealtimeUsersState State;
	ASSERT_EQ(Apply(State, BASE), EResult::APPLIED);
	EXPECT_EQ(Apply(State, Delta("PLAYER_NAME", "bad\x80")), EResult::RESYNC);
	EXPECT_EQ(Apply(State, Delta()), EResult::RESYNC);
	ASSERT_EQ(Apply(State, BASE), EResult::APPLIED);
	EXPECT_FALSE(State.NeedsFull());
	EXPECT_EQ(Apply(State, Delta()), EResult::APPLIED);
	EXPECT_EQ(State.Revision(), 2);
}

TEST(QmUsersSyncText, Utf8NameKeepsExistingByteLimit)
{
	const std::string AtLimit = std::string(60, 'x') + "中";
	for(const auto &Name : {AtLimit, AtLimit + "x"})
	{
		SCOPED_TRACE(Name.size());
		CQmRealtimeUsersState State;
		ASSERT_EQ(Apply(State, BASE), EResult::APPLIED);
		const auto Result = Apply(State, Delta("PLAYER_NAME", Name));
		if(Name.size() == 63)
		{
			ASSERT_EQ(Result, EResult::APPLIED);
			ASSERT_EQ(State.Result().m_vLocalServerMarks.size(), 1U);
			EXPECT_EQ(State.Result().m_vLocalServerMarks.front().m_Name, AtLimit);
		}
		else
		{
			EXPECT_EQ(Result, EResult::RESYNC);
			ExpectPrevious(State);
		}
	}
}

TEST(QmUsersSyncText, ExistingControlCharacterRejectionIsPreserved)
{
	CQmRealtimeUsersState State;
	ASSERT_EQ(Apply(State, BASE), EResult::APPLIED);
	EXPECT_EQ(Apply(State, Delta("PLAYER_NAME", "bad\\nname")), EResult::RESYNC);
	ExpectPrevious(State);
}

TEST(QmUsersSyncText, CompressedRealtimePayloadStillReachesIdentityValidation)
{
	CQmRealtimeUsersState State;
	ASSERT_EQ(Apply(State, BASE), EResult::APPLIED);
	const auto Encoded = Frame(std::string(R"({"type":"users_sync","v":2,"data":)") + Delta("PLAYER_NAME", "bad\x80") + "}");
	SQmRealtimeMessage Message;
	ASSERT_TRUE(ParseQmCompressedUsersMessage(Encoded.data(), Encoded.size(), Message));
	EXPECT_EQ(State.Apply(Message.m_pPayload.get(), "one:8303"), EResult::RESYNC);
	ExpectPrevious(State);
}
