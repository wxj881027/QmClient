#include <engine/shared/json.h>

#include <game/client/components/qmclient/qm_realtime.h>
#include <game/client/components/qmclient/qm_realtime_users.h>

#include <gtest/gtest.h>
#include <zlib.h>

#include <cstring>
#include <memory>
#include <string>

namespace
{
	using EResult = CQmRealtimeUsersState::EApplyResult;
	constexpr const char *FULL = R"({"server_address":"one:8303","revision":1,"base_revision":0,"full":true,"lease_seconds":20,"servers":[["one:8303",1,0],["two:8303",3,1]],"players":[["p1","本服玩家","arg","qid-one",false]],"removed_servers":[],"removed_players":[]})";
	constexpr const char *LEASE = R"({"server_address":"one:8303","revision":1,"base_revision":1,"full":false,"lease_seconds":15,"servers":[],"players":[],"removed_servers":[],"removed_players":[]})";

	EResult Apply(CQmRealtimeUsersState &State, const char *pJson, const char *pAddress = "one:8303")
	{
		std::unique_ptr<json_value, decltype(&json_value_free)> Root(json_parse(pJson, std::strlen(pJson)), json_value_free);
		return State.Apply(Root.get(), pAddress);
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

TEST(QmRealtimeUsers, FullSnapshotKeepsLocalIdentityAndGlobalCounts)
{
	CQmRealtimeUsersState State;
	ASSERT_EQ(Apply(State, FULL), EResult::APPLIED);
	const auto Result = State.Result();
	EXPECT_TRUE(Result.m_Parsed);
	EXPECT_EQ(Result.m_OnlineUserCount, 4);
	EXPECT_EQ(Result.m_OnlineDummyCount, 1);
	ASSERT_EQ(Result.m_vServerDistribution.size(), 2u);
	EXPECT_EQ(Result.m_vServerDistribution[0].m_ServerAddress, "two:8303");
	ASSERT_EQ(Result.m_vLocalServerMarks.size(), 1u);
	EXPECT_EQ(Result.m_vLocalServerMarks[0].m_Name, "本服玩家");
	EXPECT_EQ(Result.m_vLocalServerMarks[0].m_ClientBrand, EClientBrand::ARG);
	EXPECT_EQ(Result.m_vLocalServerMarks[0].m_Qid, "qid-one");
	EXPECT_FALSE(Result.m_vLocalServerMarks[0].m_VoiceSupported);
	EXPECT_EQ(State.Revision(), 1);
	EXPECT_FALSE(State.NeedsFull());
}

TEST(QmRealtimeUsers, DeltaUpdatesMembersAndRemovesRemoteServer)
{
	CQmRealtimeUsersState State;
	ASSERT_EQ(Apply(State, FULL), EResult::APPLIED);
	const char *pDelta = R"({"server_address":"one:8303","revision":2,"base_revision":1,"full":false,"lease_seconds":20,"servers":[["one:8303",1,1]],"players":[["p1","新名字","qm","qid-one",true],["p2","分身","arg","qid-two",false]],"removed_servers":["two:8303"],"removed_players":[]})";
	ASSERT_EQ(Apply(State, pDelta), EResult::APPLIED);
	const auto Result = State.Result();
	EXPECT_EQ(Result.m_OnlineUserCount, 1);
	EXPECT_EQ(Result.m_OnlineDummyCount, 1);
	ASSERT_EQ(Result.m_vServerDistribution.size(), 1u);
	ASSERT_EQ(Result.m_vLocalServerMarks.size(), 2u);
	EXPECT_EQ(Result.m_vLocalServerMarks[0].m_Name, "新名字");
	EXPECT_TRUE(Result.m_vLocalServerMarks[0].m_VoiceSupported);
	EXPECT_EQ(Result.m_vLocalServerMarks[1].m_Name, "分身");
	EXPECT_EQ(State.Revision(), 2);
}

TEST(QmRealtimeUsers, RemovingLastMemberClearsAllCountsAndMarks)
{
	CQmRealtimeUsersState State;
	ASSERT_EQ(Apply(State, FULL), EResult::APPLIED);
	const char *pDelta = R"({"server_address":"one:8303","revision":2,"base_revision":1,"full":false,"lease_seconds":20,"servers":[],"players":[],"removed_servers":["one:8303","two:8303"],"removed_players":["p1"]})";
	ASSERT_EQ(Apply(State, pDelta), EResult::APPLIED);
	const auto Result = State.Result();
	EXPECT_TRUE(Result.m_Parsed);
	EXPECT_TRUE(Result.m_vLocalServerMarks.empty());
	EXPECT_TRUE(Result.m_vServerDistribution.empty());
	EXPECT_EQ(Result.m_OnlineUserCount, 0);
}

TEST(QmRealtimeUsers, UnchangedLeaseRenewsWithoutLosingState)
{
	CQmRealtimeUsersState State;
	ASSERT_EQ(Apply(State, FULL), EResult::APPLIED);
	ASSERT_EQ(Apply(State, LEASE), EResult::APPLIED);
	EXPECT_EQ(State.Revision(), 1);
	EXPECT_EQ(State.LeaseSeconds(), 15);
	EXPECT_EQ(State.Result().m_OnlineUserCount, 4);
	ASSERT_EQ(State.Result().m_vLocalServerMarks.size(), 1u);
	EXPECT_EQ(State.Result().m_vLocalServerMarks[0].m_Name, "本服玩家");
}

TEST(QmRealtimeUsers, MissingInitialSnapshotRequiresFull)
{
	CQmRealtimeUsersState State;
	EXPECT_EQ(Apply(State, LEASE), EResult::RESYNC);
	EXPECT_TRUE(State.NeedsFull());
	EXPECT_FALSE(State.Result().m_Parsed);
	ASSERT_EQ(Apply(State, FULL), EResult::APPLIED);
	EXPECT_FALSE(State.NeedsFull());
}

TEST(QmRealtimeUsers, MissingRevisionPreservesStateUntilFullRecovery)
{
	CQmRealtimeUsersState State;
	ASSERT_EQ(Apply(State, FULL), EResult::APPLIED);
	const char *pGap = R"({"server_address":"one:8303","revision":3,"base_revision":2,"full":false,"lease_seconds":20,"servers":[],"players":[],"removed_servers":["one:8303"],"removed_players":["p1"]})";
	EXPECT_EQ(Apply(State, pGap), EResult::RESYNC);
	EXPECT_EQ(State.Revision(), 1);
	EXPECT_EQ(State.Result().m_OnlineUserCount, 4);
	EXPECT_EQ(Apply(State, LEASE), EResult::RESYNC);
	const char *pRecovered = R"({"server_address":"one:8303","revision":4,"base_revision":0,"full":true,"lease_seconds":20,"servers":[["one:8303",2,0]],"players":[],"removed_servers":[],"removed_players":[]})";
	ASSERT_EQ(Apply(State, pRecovered), EResult::APPLIED);
	EXPECT_EQ(State.Revision(), 4);
	EXPECT_EQ(State.Result().m_OnlineUserCount, 2);
	EXPECT_FALSE(State.NeedsFull());
}

TEST(QmRealtimeUsers, InvalidLaterRowDoesNotPartiallyApplyDelta)
{
	CQmRealtimeUsersState State;
	ASSERT_EQ(Apply(State, FULL), EResult::APPLIED);
	const char *pInvalid = R"({"server_address":"one:8303","revision":2,"base_revision":1,"full":false,"lease_seconds":20,"servers":[["one:8303",20,0],["bad:8303",-1,0]],"players":[],"removed_servers":["two:8303"],"removed_players":["p1"]})";
	EXPECT_EQ(Apply(State, pInvalid), EResult::RESYNC);
	EXPECT_EQ(State.Revision(), 1);
	EXPECT_EQ(State.Result().m_OnlineUserCount, 4);
	EXPECT_EQ(State.Result().m_vLocalServerMarks.size(), 1u);
}

TEST(QmRealtimeUsers, ReplayedFullDoesNotOverwriteNewerState)
{
	CQmRealtimeUsersState State;
	ASSERT_EQ(Apply(State, FULL), EResult::APPLIED);
	const char *pNext = R"({"server_address":"one:8303","revision":2,"base_revision":1,"full":false,"lease_seconds":20,"servers":[["one:8303",2,0]],"players":[],"removed_servers":[],"removed_players":[]})";
	ASSERT_EQ(Apply(State, pNext), EResult::APPLIED);
	EXPECT_EQ(Apply(State, FULL), EResult::IGNORED);
	EXPECT_EQ(State.Revision(), 2);
	EXPECT_EQ(State.Result().m_OnlineUserCount, 5);
}

TEST(QmRealtimeUsers, WrongRoomIsIgnoredWithoutRequestingResync)
{
	CQmRealtimeUsersState State;
	EXPECT_EQ(Apply(State, FULL, "two:8303"), EResult::IGNORED);
	EXPECT_FALSE(State.NeedsFull());
	EXPECT_FALSE(State.Result().m_Parsed);
}

TEST(QmRealtimeUsers, SwitchingRoomsReplacesLocalMembersWithFreshBaseline)
{
	CQmRealtimeUsersState State;
	ASSERT_EQ(Apply(State, FULL), EResult::APPLIED);
	const char *pOther = R"({"server_address":"two:8303","revision":1,"base_revision":0,"full":true,"lease_seconds":20,"servers":[["two:8303",1,0]],"players":[["p2","另一房间","qm","",true]],"removed_servers":[],"removed_players":[]})";
	ASSERT_EQ(Apply(State, pOther, "two:8303"), EResult::APPLIED);
	ASSERT_EQ(State.Result().m_vLocalServerMarks.size(), 1u);
	EXPECT_EQ(State.Result().m_vLocalServerMarks[0].m_Name, "另一房间");
	EXPECT_EQ(Apply(State, LEASE, "two:8303"), EResult::IGNORED);
	ASSERT_EQ(Apply(State, FULL), EResult::APPLIED);
	EXPECT_EQ(State.Result().m_vLocalServerMarks[0].m_Name, "本服玩家");
}

TEST(QmRealtimeUsers, ReconnectResetRejectsDeltaFromPreviousConnection)
{
	CQmRealtimeUsersState State;
	ASSERT_EQ(Apply(State, FULL), EResult::APPLIED);
	State.Reset();
	EXPECT_EQ(State.Revision(), 0);
	EXPECT_FALSE(State.Result().m_Parsed);
	EXPECT_EQ(Apply(State, LEASE), EResult::RESYNC);
	EXPECT_EQ(Apply(State, FULL), EResult::APPLIED);
}

TEST(QmRealtimeUsers, MenuSnapshotPreservesDistributionWithoutLocalMarks)
{
	CQmRealtimeUsersState State;
	const char *pMenu = R"({"server_address":"","revision":1,"base_revision":0,"full":true,"lease_seconds":20,"servers":[["one:8303",2,1]],"players":[],"removed_servers":[],"removed_players":[]})";
	ASSERT_EQ(Apply(State, pMenu, ""), EResult::APPLIED);
	EXPECT_EQ(State.Result().m_OnlineUserCount, 2);
	EXPECT_EQ(State.Result().m_OnlineDummyCount, 1);
	EXPECT_TRUE(State.Result().m_vLocalServerMarks.empty());
}

TEST(QmRealtimeUsers, InvalidLeaseAndChangedSameRevisionDoNotRenew)
{
	for(const char *pInvalid : {
		    R"({"server_address":"one:8303","revision":1,"base_revision":1,"full":false,"lease_seconds":21,"servers":[],"players":[],"removed_servers":[],"removed_players":[]})",
		    R"({"server_address":"one:8303","revision":1,"base_revision":1,"full":false,"lease_seconds":0,"servers":[],"players":[],"removed_servers":[],"removed_players":[]})",
		    R"({"server_address":"one:8303","revision":1,"base_revision":1,"full":false,"lease_seconds":20,"servers":[["one:8303",8,0]],"players":[],"removed_servers":[],"removed_players":[]})"})
	{
		SCOPED_TRACE(pInvalid);
		CQmRealtimeUsersState State;
		ASSERT_EQ(Apply(State, FULL), EResult::APPLIED);
		EXPECT_EQ(Apply(State, pInvalid), EResult::RESYNC);
		EXPECT_EQ(State.Result().m_OnlineUserCount, 4);
		EXPECT_EQ(State.LeaseSeconds(), 20);
	}
}

TEST(QmRealtimeUsers, DuplicateOrOverlappingIdsRejectEntireUpdate)
{
	CQmRealtimeUsersState State;
	ASSERT_EQ(Apply(State, FULL), EResult::APPLIED);
	const char *pDuplicate = R"({"server_address":"one:8303","revision":2,"base_revision":1,"full":false,"lease_seconds":20,"servers":[],"players":[["p1","替换","qm","",true]],"removed_servers":[],"removed_players":["p1"]})";
	EXPECT_EQ(Apply(State, pDuplicate), EResult::RESYNC);
	EXPECT_EQ(State.Result().m_vLocalServerMarks[0].m_Name, "本服玩家");
}

TEST(QmRealtimeUsers, CountsCannotOverflowDuringAggregation)
{
	CQmRealtimeUsersState State;
	const char *pHuge = R"({"server_address":"one:8303","revision":1,"base_revision":0,"full":true,"lease_seconds":20,"servers":[["one:8303",1000000,0],["two:8303",1,0]],"players":[],"removed_servers":[],"removed_players":[]})";
	EXPECT_EQ(Apply(State, pHuge), EResult::RESYNC);
	EXPECT_FALSE(State.Result().m_Parsed);
}

TEST(QmRealtimeUsers, CompressedSnapshotDecodesAndOwnsItsPayload)
{
	SQmRealtimeMessage Message;
	{
		const auto Encoded = Frame(std::string(R"({"type":"users_sync","v":2,"data":)") + FULL + "}");
		ASSERT_TRUE(ParseQmCompressedUsersMessage(Encoded.data(), Encoded.size(), Message));
	}
	EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::USERS_SYNC);
	CQmRealtimeUsersState State;
	ASSERT_EQ(State.Apply(Message.m_pPayload.get(), "one:8303"), EResult::APPLIED);
	EXPECT_EQ(State.Result().m_OnlineUserCount, 4);
}

TEST(QmRealtimeUsers, CompressedFrameRejectsTruncationAndTrailingBytes)
{
	const auto Encoded = Frame(std::string(R"({"type":"users_sync","v":2,"data":)") + FULL + "}");
	SQmRealtimeMessage Message;
	EXPECT_FALSE(ParseQmCompressedUsersMessage(Encoded.data(), Encoded.size() - 1, Message));
	const auto Trailing = Encoded + "x";
	EXPECT_FALSE(ParseQmCompressedUsersMessage(Trailing.data(), Trailing.size(), Message));
	EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::INVALID);
}

TEST(QmRealtimeUsers, CompressedFrameRejectsOversizeAndIncorrectDeclaredLength)
{
	auto Encoded = Frame(std::string(R"({"type":"users_sync","v":2,"data":)") + FULL + "}");
	SQmRealtimeMessage Message;
	++Encoded[7];
	EXPECT_FALSE(ParseQmCompressedUsersMessage(Encoded.data(), Encoded.size(), Message));
	Encoded[4] = 0x7f;
	EXPECT_FALSE(ParseQmCompressedUsersMessage(Encoded.data(), Encoded.size(), Message));
	EXPECT_FALSE(ParseQmCompressedUsersMessage(nullptr, 0, Message));
	EXPECT_FALSE(ParseQmCompressedUsersMessage("QMU1", 4, Message));
}

TEST(QmRealtimeUsers, CompressedChannelCannotInjectOtherEventTypes)
{
	const auto Encoded = Frame(R"({"type":"titles","v":2,"data":{"server_time":100,"presences":[]}})");
	SQmRealtimeMessage Message;
	EXPECT_FALSE(ParseQmCompressedUsersMessage(Encoded.data(), Encoded.size(), Message));
	EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::INVALID);
}

// 固定帧由服务端 realtime_users.js 的 PrepareUsersSync 生成，校验跨语言协议。
TEST(QmRealtimeUsers, NodeEncodedFullAndDeltaMatchClientState)
{
	const unsigned char aFull[] = {
		0x51,
		0x4d,
		0x55,
		0x31,
		0x00,
		0x00,
		0x01,
		0x15,
		0x78,
		0x01,
		0x4d,
		0xcf,
		0x51,
		0x8a,
		0xc2,
		0x40,
		0x0c,
		0x06,
		0xe0,
		0xbb,
		0xfc,
		0xcf,
		0x59,
		0x98,
		0xaa,
		0x0f,
		0x92,
		0xab,
		0xd4,
		0xa1,
		0x8c,
		0x4e,
		0x0a,
		0x85,
		0xb1,
		0xe3,
		0x26,
		0xd3,
		0x4a,
		0x11,
		0x8f,
		0xb0,
		0x37,
		0xf0,
		0x08,
		0x7b,
		0x2e,
		0xcf,
		0x21,
		0x83,
		0x05,
		0x7d,
		0x09,
		0x24,
		0x24,
		0x5f,
		0x92,
		0x1b,
		0xca,
		0x72,
		0x11,
		0x30,
		0x26,
		0x13,
		0xb5,
		0xce,
		0x96,
		0xf1,
		0x04,
		0xc2,
		0x0c,
		0xde,
		0x10,
		0x62,
		0x28,
		0x01,
		0x7c,
		0x83,
		0x89,
		0xce,
		0xa2,
		0x5d,
		0x88,
		0x51,
		0xc5,
		0x0c,
		0x8c,
		0x3c,
		0x0a,
		0xef,
		0xb7,
		0x6e,
		0x0b,
		0xc2,
		0x31,
		0x98,
		0x74,
		0x2a,
		0xf3,
		0x60,
		0x43,
		0x1e,
		0xc1,
		0x8e,
		0xf0,
		0x49,
		0x1a,
		0x42,
		0x3f,
		0xa5,
		0x04,
		0x2e,
		0x3a,
		0x09,
		0xad,
		0x8c,
		0x81,
		0xdb,
		0xf6,
		0x4b,
		0x68,
		0xc8,
		0x79,
		0x6a,
		0x51,
		0xae,
		0x79,
		0x25,
		0x1d,
		0x35,
		0xde,
		0x13,
		0x2e,
		0x29,
		0x2c,
		0xa2,
		0xef,
		0xee,
		0xf6,
		0x80,
		0x7e,
		0x50,
		0x2b,
		0x07,
		0xd0,
		0x8e,
		0xfa,
		0x90,
		0x4c,
		0x3c,
		0x08,
		0xcf,
		0xc7,
		0xff,
		0xf3,
		0xf1,
		0x07,
		0xc2,
		0xef,
		0xb9,
		0x86,
		0x21,
		0xfe,
		0x04,
		0x50,
		0x5d,
		0x55,
		0xc7,
		0x55,
		0xce,
		0x79,
		0x96,
		0xd8,
		0xbd,
		0x6f,
		0xaf,
		0xcc,
		0x57,
		0xf1,
		0x63,
		0x7b,
		0x42,
		0x92,
		0xfa,
		0x82,
		0xc9,
		0x29,
		0x8f,
		0xd1,
		0xc0,
		0x1b,
		0x77,
		0xbf,
		0xbf,
		0x00,
		0x31,
		0xf5,
		0x5c,
		0x61,
	};
	const unsigned char aDelta[] = {
		0x51,
		0x4d,
		0x55,
		0x31,
		0x00,
		0x00,
		0x01,
		0x43,
		0x78,
		0x01,
		0x4d,
		0x8f,
		0x41,
		0x6a,
		0xc3,
		0x40,
		0x0c,
		0x45,
		0xef,
		0xf2,
		0xd7,
		0x2a,
		0xc4,
		0x4e,
		0x29,
		0x45,
		0x57,
		0x71,
		0x06,
		0x33,
		0xcd,
		0xc8,
		0xc5,
		0x30,
		0xf1,
		0xa4,
		0xa3,
		0xb1,
		0x83,
		0x09,
		0xd9,
		0x96,
		0x6c,
		0x02,
		0x5d,
		0xf4,
		0x16,
		0x3d,
		0x41,
		0x7b,
		0x9f,
		0x94,
		0x1e,
		0xa3,
		0x4c,
		0x1c,
		0xb0,
		0x97,
		0xe2,
		0xe9,
		0x7d,
		0xe9,
		0x1f,
		0x91,
		0xc6,
		0xbd,
		0x80,
		0xd1,
		0xab,
		0x44,
		0xad,
		0x75,
		0xec,
		0xb6,
		0x20,
		0x0c,
		0xe0,
		0x92,
		0xe0,
		0x6c,
		0xb2,
		0xe0,
		0x23,
		0x54,
		0xe2,
		0x20,
		0xb1,
		0xb6,
		0xce,
		0x45,
		0x51,
		0x05,
		0x23,
		0x74,
		0xc2,
		0xcf,
		0xeb,
		0xd5,
		0x1a,
		0x84,
		0x17,
		0xab,
		0x52,
		0x47,
		0x19,
		0x5a,
		0x6d,
		0x43,
		0x07,
		0x2e,
		0x08,
		0xf3,
		0x50,
		0x12,
		0x9a,
		0xde,
		0x7b,
		0x70,
		0x63,
		0xbd,
		0x0a,
		0xdd,
		0x73,
		0x14,
		0x5c,
		0x55,
		0x8b,
		0x88,
		0x82,
		0x0a,
		0x63,
		0x08,
		0x7b,
		0x6f,
		0x47,
		0x89,
		0x13,
		0xac,
		0x36,
		0x68,
		0xda,
		0xa8,
		0x69,
		0x03,
		0x7a,
		0xa4,
		0x9b,
		0x6c,
		0x40,
		0xf8,
		0xfd,
		0xfc,
		0xbe,
		0x7e,
		0x5c,
		0x40,
		0xb0,
		0xf1,
		0x15,
		0x84,
		0xb7,
		0xd6,
		0x3d,
		0x58,
		0xdc,
		0x31,
		0x55,
		0x58,
		0x48,
		0x4f,
		0x94,
		0x62,
		0x2f,
		0xd9,
		0xb9,
		0x9e,
		0xdf,
		0xff,
		0x7e,
		0xbe,
		0xf2,
		0xf6,
		0x6e,
		0x56,
		0x6e,
		0xd0,
		0xe4,
		0x4f,
		0x77,
		0x61,
		0x10,
		0x57,
		0x4f,
		0xfd,
		0xf2,
		0x69,
		0xa4,
		0x43,
		0x98,
		0x9a,
		0x2d,
		0xf0,
		0xfc,
		0x99,
		0x21,
		0x78,
		0xc9,
		0x85,
		0x55,
		0xb6,
		0xa1,
		0x73,
		0x0a,
		0x2e,
		0x57,
		0xa7,
		0xd3,
		0x3f,
		0x10,
		0x17,
		0x6d,
		0x29,
	};
	CQmRealtimeUsersState State;
	SQmRealtimeMessage Message;
	ASSERT_TRUE(ParseQmCompressedUsersMessage(reinterpret_cast<const char *>(aFull), sizeof(aFull), Message));
	ASSERT_EQ(State.Apply(Message.m_pPayload.get(), "one:8303"), EResult::APPLIED);
	EXPECT_EQ(State.Result().m_OnlineUserCount, 1);
	EXPECT_EQ(State.Result().m_OnlineDummyCount, 1);
	ASSERT_EQ(State.Result().m_vLocalServerMarks.size(), 1u);
	EXPECT_EQ(State.Result().m_vLocalServerMarks[0].m_Name, "本服");
	ASSERT_TRUE(ParseQmCompressedUsersMessage(reinterpret_cast<const char *>(aDelta), sizeof(aDelta), Message));
	ASSERT_EQ(State.Apply(Message.m_pPayload.get(), "one:8303"), EResult::APPLIED);
	const auto Result = State.Result();
	EXPECT_EQ(State.Revision(), 2);
	EXPECT_EQ(Result.m_OnlineUserCount, 1);
	EXPECT_EQ(Result.m_OnlineDummyCount, 1);
	ASSERT_EQ(Result.m_vServerDistribution.size(), 1u);
	ASSERT_EQ(Result.m_vLocalServerMarks.size(), 2u);
	EXPECT_EQ(Result.m_vLocalServerMarks[0].m_Name, "改名");
	EXPECT_EQ(Result.m_vLocalServerMarks[0].m_ClientBrand, EClientBrand::ARG);
	EXPECT_FALSE(Result.m_vLocalServerMarks[0].m_VoiceSupported);
	EXPECT_EQ(Result.m_vLocalServerMarks[1].m_Name, "分身");
	EXPECT_EQ(Result.m_vLocalServerMarks[1].m_Qid, "qid-a");
}
