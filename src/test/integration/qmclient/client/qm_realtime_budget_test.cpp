#include <engine/shared/json.h>

#include <game/client/components/qmclient/qm_realtime.h>
#include <game/client/components/qmclient/qm_realtime_users.h>

#include <gtest/gtest.h>
#include <zlib.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace
{
	using EResult = CQmRealtimeUsersState::EApplyResult;

	std::string Frame(const std::string &Json)
	{
		uLongf Size = compressBound(static_cast<uLong>(Json.size()));
		std::string Compressed(Size, '\0');
		EXPECT_EQ(compress2(reinterpret_cast<Bytef *>(Compressed.data()), &Size, reinterpret_cast<const Bytef *>(Json.data()), static_cast<uLong>(Json.size()), 1), Z_OK);
		Compressed.resize(Size);
		std::string Result = "QMU1";
		for(int Shift : {24, 16, 8, 0})
			Result.push_back(static_cast<char>(static_cast<uint32_t>(Json.size()) >> Shift));
		return Result + Compressed;
	}

	std::string Dense()
	{
		const size_t Count = QMCLIENT_REALTIME_JSON_MEMORY_BUDGET / sizeof(json_value) + 1;
		std::string Json = R"({"type":"users_sync","data":{"unknown":[)";
		Json.reserve(Json.size() + Count * 2 + 3);
		for(size_t Index = 0; Index < Count; ++Index)
			Json += Index ? ",0" : "0";
		return Json + "]}}";
	}

	std::string Sync(bool Full, int Revision, int Base, const char *pName)
	{
		return std::string(R"({"type":"users_sync","data":{"server_address":"one:8303","full":)") + (Full ? "true" : "false") +
		       R"(,"revision":)" + std::to_string(Revision) + R"(,"base_revision":)" + std::to_string(Base) +
		       R"(,"lease_seconds":20,"servers":[["one:8303",1,0]],"players":[["p1",")" + pName +
		       R"(","qm","",true]],"removed_servers":[],"removed_players":[]}})";
	}

	std::optional<EResult> ParseAndPublish(CQmRealtimeUsersState &State, const std::string &Json, bool Compressed, int &Published)
	{
		SQmRealtimeMessage Message;
		const auto Encoded = Compressed ? Frame(Json) : Json;
		if(!(Compressed ? ParseQmCompressedUsersMessage(Encoded.data(), Encoded.size(), Message) : ParseQmRealtimeMessage(Encoded.data(), Encoded.size(), Message)))
			return std::nullopt;
		return State.ApplyAndPublish(Message.m_pPayload.get(), "one:8303", [&Published](auto, int) {
			++Published;
			return true;
		});
	}

	std::string Padded(const std::string &Prefix, size_t Size)
	{
		std::string Text = Prefix;
		Text.resize(Size, 'x');
		return Text;
	}
}

TEST(QmRealtimeBudgetPipeline, CompressedDensePayloadCannotBypassTreeBudgetAndNextFrameRecovers)
{
	const auto Json = Dense();
	ASSERT_LE(Json.size(), QMCLIENT_USERS_SYNC_MAX_BYTES);
	const auto Encoded = Frame(Json);
	SQmRealtimeMessage Message;
	const auto Good = Frame(Sync(true, 1, 0, "Good"));
	ASSERT_TRUE(ParseQmCompressedUsersMessage(Good.data(), Good.size(), Message));
	std::weak_ptr<const json_value> Previous = Message.m_pPayload;
	EXPECT_FALSE(ParseQmCompressedUsersMessage(Encoded.data(), Encoded.size(), Message));
	EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::INVALID);
	EXPECT_EQ(Message.m_pPayload, nullptr);
	EXPECT_TRUE(Previous.expired());
	ASSERT_TRUE(ParseQmCompressedUsersMessage(Good.data(), Good.size(), Message));
	EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::USERS_SYNC);
	EXPECT_NE(Message.m_pPayload, nullptr);
}

TEST(QmRealtimeBudgetPipeline, ExistingDecodedByteLimitRemainsInclusive)
{
	std::string Json = Sync(true, 1, 0, "Good");
	Json.resize(QMCLIENT_USERS_SYNC_MAX_BYTES, ' ');
	const auto Encoded = Frame(Json);
	SQmRealtimeMessage Message;
	ASSERT_TRUE(ParseQmCompressedUsersMessage(Encoded.data(), Encoded.size(), Message));
	CQmRealtimeUsersState State;
	EXPECT_EQ(State.Apply(Message.m_pPayload.get(), "one:8303"), EResult::APPLIED);
	EXPECT_EQ(State.Revision(), 1);
}

TEST(QmRealtimeBudgetPipeline, ExistingMaximumUsersListBoundsFitRawAndCompressedBudgets)
{
	std::string Json = R"({"type":"users_sync","data":{"server_address":"one:8303","full":true,"revision":1,"base_revision":0,"lease_seconds":20,"servers":[)";
	for(int Index = 0; Index < 8192; ++Index)
	{
		if(Index)
			Json += ',';
		Json += "[\"" + Padded("server-" + std::to_string(Index), 128) + "\",1,0]";
	}
	Json += R"(],"players":[)";
	for(int Index = 0; Index < 512; ++Index)
	{
		if(Index)
			Json += ',';
		const auto Id = std::to_string(Index);
		Json += "[\"" + Padded("player-" + Id, 256) + "\",\"" + Padded("name-" + Id, 63) + R"(","qm",")" + Padded("qid-" + Id, 128) + R"(",true])";
	}
	Json += R"(],"removed_servers":[],"removed_players":[]}})";
	ASSERT_LE(Json.size(), QMCLIENT_USERS_SYNC_MAX_BYTES);
	const auto Compressed = Frame(Json);
	for(const bool UseCompression : {false, true})
	{
		SCOPED_TRACE(UseCompression);
		const auto &Input = UseCompression ? Compressed : Json;
		SQmRealtimeMessage Message;
		ASSERT_TRUE(UseCompression ? ParseQmCompressedUsersMessage(Input.data(), Input.size(), Message) : ParseQmRealtimeMessage(Input.data(), Input.size(), Message));
		CQmRealtimeUsersState State;
		ASSERT_EQ(State.Apply(Message.m_pPayload.get(), "one:8303"), EResult::APPLIED);
		const auto Result = State.Result();
		EXPECT_EQ(Result.m_OnlineUserCount, 8192);
		EXPECT_EQ(Result.m_vServerDistribution.size(), 8192u);
		EXPECT_EQ(Result.m_vLocalServerMarks.size(), 512u);
	}
}

TEST(QmRealtimeBudgetPipeline, RejectedFramePreservesOldStateAndLaterGapRequiresFullRecovery)
{
	const auto OverBudget = Dense();
	ASSERT_LE(OverBudget.size(), QMCLIENT_USERS_SYNC_MAX_BYTES);
	for(const bool UseCompression : {false, true})
	{
		SCOPED_TRACE(UseCompression);
		CQmRealtimeUsersState State;
		int Published = 0;
		ASSERT_EQ(ParseAndPublish(State, Sync(true, 1, 0, "Old"), UseCompression, Published), EResult::APPLIED);
		ASSERT_EQ(Published, 1);
		EXPECT_FALSE(ParseAndPublish(State, OverBudget, UseCompression, Published).has_value());
		EXPECT_EQ(Published, 1);
		EXPECT_EQ(State.Revision(), 1);
		EXPECT_EQ(State.LeaseSeconds(), 20);
		EXPECT_FALSE(State.NeedsFull());
		ASSERT_EQ(State.Result().m_vLocalServerMarks.size(), 1u);
		EXPECT_EQ(State.Result().m_vLocalServerMarks.front().m_Name, "Old");
		EXPECT_EQ(ParseAndPublish(State, Sync(false, 3, 2, "Skipped"), UseCompression, Published), EResult::RESYNC);
		EXPECT_TRUE(State.NeedsFull());
		EXPECT_EQ(State.Revision(), 1);
		EXPECT_EQ(Published, 1);
		ASSERT_EQ(ParseAndPublish(State, Sync(true, 3, 0, "Full"), UseCompression, Published), EResult::APPLIED);
		EXPECT_FALSE(State.NeedsFull());
		ASSERT_EQ(ParseAndPublish(State, Sync(false, 4, 3, "Next"), UseCompression, Published), EResult::APPLIED);
		EXPECT_EQ(State.Revision(), 4);
		EXPECT_EQ(State.Result().m_vLocalServerMarks.front().m_Name, "Next");
		EXPECT_EQ(Published, 3);
	}
}
