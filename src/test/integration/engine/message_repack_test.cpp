#include <engine/client/message_repack.h>

#include <generated/protocol.h>
#include <generated/protocol7.h>

#include <gtest/gtest.h>

#include <array>
#include <limits>

namespace
{
	class CMessageRepackTest : public testing::Test
	{
	protected:
		CUuidManager m_Uuids;
		CPacker m_Output;
		CUnpacker m_Unpacker;
		void SetUp() override { m_Uuids.RegisterName(OFFSET_UUID, "message-repack-test@qmclient"); }
		void ReadOutput() { m_Unpacker.Reset(m_Output.Data(), m_Output.Size()); }
	};
}

TEST_F(CMessageRepackTest, LegacySystemHeaderAndPayloadArePreserved)
{
	CMsgPacker Msg(NETMSG_PING, true);
	Msg.AddInt(123);
	ASSERT_TRUE(client_message::Repack(Msg, m_Output, false, m_Uuids, 0));
	ReadOutput();
	EXPECT_EQ(m_Unpacker.GetInt(), (NETMSG_PING << 1) | 1);
	EXPECT_EQ(m_Unpacker.GetInt(), 123);
	EXPECT_FALSE(m_Unpacker.Error());
	EXPECT_FALSE(Msg.Error());
	EXPECT_EQ(Msg.m_MsgId, NETMSG_PING);
}

TEST_F(CMessageRepackTest, LegacyGameHeaderKeepsSystemBitClear)
{
	CMsgPacker Msg(NETMSGTYPE_CL_KILL);
	ASSERT_TRUE(client_message::Repack(Msg, m_Output, false, m_Uuids, 0));
	ReadOutput();
	EXPECT_EQ(m_Unpacker.GetInt(), NETMSGTYPE_CL_KILL << 1);
	EXPECT_FALSE(m_Unpacker.Error());
}

TEST_F(CMessageRepackTest, SourceErrorRejectsItsPreviouslyWrittenPrefix)
{
	CMsgPacker Msg(NETMSG_PING, true);
	Msg.AddString("prefix");
	Msg.AddRaw(nullptr, 1);
	ASSERT_TRUE(Msg.Error());
	ASSERT_GT(Msg.Size(), 0);
	EXPECT_FALSE(client_message::Repack(Msg, m_Output, false, m_Uuids, 0));
	EXPECT_EQ(m_Output.Size(), 0);
	EXPECT_TRUE(Msg.Error());
}

TEST_F(CMessageRepackTest, HeaderOverheadCanRejectAnOtherwiseValidSource)
{
	CMsgPacker Msg(NETMSG_INFO, true);
	const std::array<unsigned char, CPacker::PACKER_BUFFER_SIZE> aPayload{};
	Msg.AddRaw(aPayload.data(), static_cast<int>(aPayload.size()));
	ASSERT_FALSE(Msg.Error());
	EXPECT_FALSE(client_message::Repack(Msg, m_Output, false, m_Uuids, 0));
	EXPECT_TRUE(m_Output.Error());
}

TEST_F(CMessageRepackTest, RecordOnlyMessageRetainsTheFullCodecCapacity)
{
	CMsgPacker Msg(NETMSG_INFO, true);
	const std::array<unsigned char, CPacker::PACKER_BUFFER_SIZE - 1> aPayload{};
	Msg.AddRaw(aPayload.data(), static_cast<int>(aPayload.size()));
	ASSERT_TRUE(client_message::Repack(Msg, m_Output, false, m_Uuids, MSGFLAG_RECORD | MSGFLAG_NOSEND));
	EXPECT_EQ(m_Output.Size(), CPacker::PACKER_BUFFER_SIZE);
	EXPECT_FALSE(m_Output.Error());
}

TEST_F(CMessageRepackTest, SixupSystemUsesTheRealOutgoingIdMapping)
{
	CMsgPacker Msg(NETMSG_PING_REPLY, true);
	ASSERT_TRUE(client_message::Repack(Msg, m_Output, true, m_Uuids, 0));
	ReadOutput();
	EXPECT_EQ(m_Unpacker.GetInt(), (protocol7::NETMSG_PING_REPLY << 1) | 1);
}

TEST_F(CMessageRepackTest, NoTranslatePreservesRawSixupSystemId)
{
	CMsgPacker Msg(protocol7::NETMSG_REQUEST_MAP_DATA, true, true);
	ASSERT_TRUE(client_message::Repack(Msg, m_Output, true, m_Uuids, 0));
	ReadOutput();
	EXPECT_EQ(m_Unpacker.GetInt(), (protocol7::NETMSG_REQUEST_MAP_DATA << 1) | 1);
}

TEST_F(CMessageRepackTest, SixupGameUsesTheGeneratedMapping)
{
	CMsgPacker Msg(NETMSGTYPE_CL_KILL);
	ASSERT_TRUE(client_message::Repack(Msg, m_Output, true, m_Uuids, 0));
	ReadOutput();
	EXPECT_EQ(m_Unpacker.GetInt(), protocol7::NETMSGTYPE_CL_KILL << 1);
}

TEST_F(CMessageRepackTest, UnmappedGameMessageIsRejected)
{
	CMsgPacker Msg(OFFSET_UUID - 1);
	EXPECT_FALSE(client_message::Repack(Msg, m_Output, true, m_Uuids, 0));
}

TEST_F(CMessageRepackTest, NegativeIdsAreRejectedBeforeEncodingEvenWithoutTranslation)
{
	for(bool Sixup : {false, true})
	{
		SCOPED_TRACE(Sixup);
		CMsgPacker Msg(-1, true, true);
		EXPECT_FALSE(client_message::Repack(Msg, m_Output, Sixup, m_Uuids, 0));
		EXPECT_EQ(m_Output.Size(), 0);
	}
}

TEST_F(CMessageRepackTest, RegisteredUuidKeepsIdentityAndPayloadForBothProtocols)
{
	for(bool Sixup : {false, true})
		for(bool System : {false, true})
		{
			SCOPED_TRACE(Sixup);
			SCOPED_TRACE(System);
			CMsgPacker Msg(OFFSET_UUID, System);
			Msg.AddInt(42);
			ASSERT_TRUE(client_message::Repack(Msg, m_Output, Sixup, m_Uuids, 0));
			ReadOutput();
			EXPECT_EQ(m_Unpacker.GetInt(), System ? 1 : 0);
			EXPECT_EQ(m_Uuids.UnpackUuid(&m_Unpacker), OFFSET_UUID);
			EXPECT_EQ(m_Unpacker.GetInt(), 42);
			EXPECT_FALSE(m_Unpacker.Error());
		}
}

TEST_F(CMessageRepackTest, UnregisteredUuidIdsDoNotIndexTheRegistry)
{
	for(int Id : {OFFSET_UUID + 1, std::numeric_limits<int>::max()})
	{
		SCOPED_TRACE(Id);
		CMsgPacker Msg(Id, true);
		EXPECT_FALSE(client_message::Repack(Msg, m_Output, false, m_Uuids, 0));
		EXPECT_EQ(m_Output.Size(), 0);
	}
}

TEST_F(CMessageRepackTest, UuidHeaderOverheadAlsoRejectsOutputOverflow)
{
	CMsgPacker Msg(OFFSET_UUID, true);
	const std::array<unsigned char, CPacker::PACKER_BUFFER_SIZE - 16> aPayload{};
	Msg.AddRaw(aPayload.data(), static_cast<int>(aPayload.size()));
	ASSERT_FALSE(Msg.Error());
	EXPECT_FALSE(client_message::Repack(Msg, m_Output, false, m_Uuids, 0));
	EXPECT_TRUE(m_Output.Error());
}

TEST_F(CMessageRepackTest, UnsupportedOutgoingSystemMessageIsRejected)
{
	CMsgPacker Msg(NETMSG_RCON_LINE, true);
	EXPECT_FALSE(client_message::Repack(Msg, m_Output, true, m_Uuids, 0));
}

TEST_F(CMessageRepackTest, ExactNetworkChunkLimitCanBeSent)
{
	CMsgPacker Msg(NETMSG_INFO, true);
	const std::array<unsigned char, NET_MAX_CHUNK_SIZE - 1> aPayload{};
	Msg.AddRaw(aPayload.data(), static_cast<int>(aPayload.size()));
	ASSERT_TRUE(client_message::Repack(Msg, m_Output, false, m_Uuids, MSGFLAG_VITAL | MSGFLAG_RECORD));
	EXPECT_EQ(m_Output.Size(), NET_MAX_CHUNK_SIZE);
}

TEST_F(CMessageRepackTest, SendingOneByteOverNetworkLimitFailsEvenWithRecordFlag)
{
	CMsgPacker Msg(NETMSG_INFO, true);
	const std::array<unsigned char, NET_MAX_CHUNK_SIZE> aPayload{};
	Msg.AddRaw(aPayload.data(), static_cast<int>(aPayload.size()));
	ASSERT_FALSE(Msg.Error());
	EXPECT_FALSE(client_message::Repack(Msg, m_Output, false, m_Uuids, MSGFLAG_RECORD));
	EXPECT_FALSE(m_Output.Error());
	EXPECT_EQ(m_Output.Size(), NET_MAX_CHUNK_SIZE + 1);
}
