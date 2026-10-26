#include <base/system.h>

#include <engine/shared/network.h>

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace
{
	struct CProtocolCase
	{
		int m_NetType;
		bool m_Sixup;
	};

	class CQmUpstreamRedundancyLoopback : public ::testing::TestWithParam<CProtocolCase>
	{
	protected:
		using TPackets = std::vector<std::vector<unsigned char>>;
		NETSOCKET m_Receiver = nullptr;
		NETSOCKET m_Sender = nullptr;
		NETADDR m_Target = {};
		static constexpr SECURITY_TOKEN TOKEN = 0x12345678;

		void SetUp() override
		{
			CNetBase::Init();
			NETADDR BindAddr = {};
			ASSERT_EQ(net_addr_from_str(&BindAddr, GetParam().m_NetType == NETTYPE_IPV4 ? "127.0.0.1" : "[::1]"), 0);
			for(int Attempt = 0; Attempt < 100 && !m_Receiver; ++Attempt)
			{
				BindAddr.port = secure_rand_below(65535 - 1024) + 1024;
				m_Receiver = net_udp_create(BindAddr);
			}
			if(!m_Receiver && GetParam().m_NetType == NETTYPE_IPV6)
				GTEST_SKIP() << "IPv6 loopback is unavailable";
			ASSERT_NE(m_Receiver, nullptr);
			m_Target = BindAddr;
			BindAddr.port = 0;
			m_Sender = net_udp_create(BindAddr);
			ASSERT_NE(m_Sender, nullptr);
		}

		void TearDown() override
		{
			if(m_Receiver)
				net_udp_close(m_Receiver);
			if(m_Sender)
				net_udp_close(m_Sender);
		}

		TPackets ReceivePackets()
		{
			TPackets Packets;
			const auto Deadline = std::chrono::steady_clock::now() + 1s;
			while(std::chrono::steady_clock::now() < Deadline)
			{
				NETADDR From;
				unsigned char *pData;
				const int Size = net_udp_recv(m_Receiver, &From, &pData);
				if(Size > 0)
				{
					Packets.emplace_back(pData, pData + Size);
					continue;
				}
				// 先取空 net_udp_recv 的批量缓冲，再等待内核队列，整个接收过程有超时。
				if(net_socket_read_wait(m_Receiver, 50ms) != 1)
					break;
			}
			return Packets;
		}

		CNetPacketConstruct DataPacket(int PayloadSize = 1) const
		{
			CNetPacketConstruct Packet = {};
			Packet.m_NumChunks = 1;
			CNetChunkHeader Header;
			Header.m_Flags = 0;
			Header.m_Size = PayloadSize;
			Header.m_Sequence = -1;
			unsigned char *pPayload = Header.Pack(Packet.m_aChunkData, GetParam().m_Sixup ? 6 : 4);
			mem_zero(pPayload, PayloadSize);
			Packet.m_DataSize = (int)(pPayload + PayloadSize - Packet.m_aChunkData);
			return Packet;
		}

		TPackets SendPacket(CNetPacketConstruct Packet, int Copies)
		{
			CNetBase::SendPacket(m_Sender, &m_Target, &Packet, TOKEN, GetParam().m_Sixup, Copies);
			return ReceivePackets();
		}

		void InitConnection(CNetConnection &Connection)
		{
			Connection.Init(m_Sender, false);
			Connection.DirectInit(m_Target, TOKEN, TOKEN, GetParam().m_Sixup);
		}

		TPackets SendConnection(CNetConnection &Connection)
		{
			const unsigned char aData[] = {1, 2, 3};
			EXPECT_EQ(Connection.QueueChunk(0, sizeof(aData), aData), 0);
			EXPECT_EQ(Connection.Flush(), 1);
			return ReceivePackets();
		}
	};
}

TEST_P(CQmUpstreamRedundancyLoopback, DataPacketsUseConfiguredCopyCount)
{
	for(int Copies : {1, 2, 3})
	{
		SCOPED_TRACE(Copies);
		EXPECT_EQ(SendPacket(DataPacket(), Copies).size(), (size_t)Copies);
	}
}

TEST_P(CQmUpstreamRedundancyLoopback, CopyCountStaysWithinOneAndThree)
{
	EXPECT_EQ(SendPacket(DataPacket(), 0).size(), 1u);
	EXPECT_EQ(SendPacket(DataPacket(), 10).size(), 3u);
}

TEST_P(CQmUpstreamRedundancyLoopback, CompressedPacketsRetainEveryCopy)
{
	auto Packets = SendPacket(DataPacket(128), 3);
	ASSERT_EQ(Packets.size(), 3u);
	for(auto &Bytes : Packets)
	{
		bool Sixup = GetParam().m_Sixup;
		// 六人协议的数据包头部带安全令牌，UnpackPacket 在 Sixup 时必须能写回它。
		SECURITY_TOKEN SecurityToken = NET_SECURITY_TOKEN_UNKNOWN;
		CNetPacketConstruct Packet = {};
		EXPECT_EQ(CNetBase::UnpackPacket(Bytes.data(), (int)Bytes.size(), &Packet, Sixup, true, Sixup ? &SecurityToken : nullptr), 0);
		EXPECT_NE(Packet.m_Flags & NET_PACKETFLAG_COMPRESSION, 0);
		EXPECT_EQ(Packet.m_NumChunks, 1);
	}
}

TEST_P(CQmUpstreamRedundancyLoopback, CopiesHaveIdenticalPackedBytes)
{
	const auto Packets = SendPacket(DataPacket(128), 3);
	ASSERT_EQ(Packets.size(), 3u);
	EXPECT_EQ(Packets[0], Packets[1]);
	EXPECT_EQ(Packets[0], Packets[2]);
}

TEST_P(CQmUpstreamRedundancyLoopback, ResendDataPacketsRetainEveryCopy)
{
	auto Packet = DataPacket();
	Packet.m_Flags = NET_PACKETFLAG_RESEND;
	EXPECT_EQ(SendPacket(Packet, 2).size(), 2u);
}

TEST_P(CQmUpstreamRedundancyLoopback, ControlPacketsStaySingle)
{
	CNetPacketConstruct Packet = {};
	Packet.m_Flags = NET_PACKETFLAG_CONTROL;
	Packet.m_aChunkData[0] = NET_CTRLMSG_KEEPALIVE;
	Packet.m_DataSize = 1;
	EXPECT_EQ(SendPacket(Packet, 3).size(), 1u);
}

TEST_P(CQmUpstreamRedundancyLoopback, EmptyResendRequestsStaySingle)
{
	CNetPacketConstruct Packet = {};
	Packet.m_Flags = NET_PACKETFLAG_RESEND;
	EXPECT_EQ(SendPacket(Packet, 3).size(), 1u);
}

TEST_P(CQmUpstreamRedundancyLoopback, TrafficCountersIncludeAllSuccessfulCopies)
{
	NETSTATS Before;
	net_stats(&Before);
	const auto Packets = SendPacket(DataPacket(), 3);
	NETSTATS After;
	net_stats(&After);
	ASSERT_EQ(Packets.size(), 3u);
	EXPECT_EQ(After.sent_packets - Before.sent_packets, 3u);
	EXPECT_EQ(After.sent_bytes - Before.sent_bytes, 3u * Packets[0].size());
}

TEST_P(CQmUpstreamRedundancyLoopback, SendFailureStopsRemainingCopiesAndCountsError)
{
	NETSTATS Before;
	net_stats(&Before);
	NETADDR Unsupported = {};
	auto Packet = DataPacket();
	CNetBase::SendPacket(m_Sender, &Unsupported, &Packet, TOKEN, GetParam().m_Sixup, 3);
	NETSTATS After;
	net_stats(&After);
	EXPECT_EQ(After.sent_packets, Before.sent_packets);
	EXPECT_EQ(After.sent_bytes, Before.sent_bytes);
	EXPECT_EQ(After.send_errors - Before.send_errors, 1u);
}

TEST_P(CQmUpstreamRedundancyLoopback, CopySettingIsLocalToOneConnection)
{
	CNetConnection First;
	CNetConnection Second;
	InitConnection(First);
	InitConnection(Second);
	First.SetQmUpstreamCopies(3);
	EXPECT_EQ(SendConnection(First).size(), 3u);
	EXPECT_EQ(SendConnection(Second).size(), 1u);
}

TEST_P(CQmUpstreamRedundancyLoopback, ResetRemovesPreviousSessionCopySetting)
{
	CNetConnection Connection;
	InitConnection(Connection);
	Connection.SetQmUpstreamCopies(3);
	Connection.Reset();
	Connection.DirectInit(m_Target, TOKEN, TOKEN, GetParam().m_Sixup);
	EXPECT_EQ(SendConnection(Connection).size(), 1u);
}

TEST_P(CQmUpstreamRedundancyLoopback, DisablingCopiesRestoresSingleSend)
{
	CNetConnection Connection;
	InitConnection(Connection);
	Connection.SetQmUpstreamCopies(3);
	EXPECT_EQ(SendConnection(Connection).size(), 3u);
	Connection.SetQmUpstreamCopies(1);
	EXPECT_EQ(SendConnection(Connection).size(), 1u);
}

TEST_P(CQmUpstreamRedundancyLoopback, ConnectionlessAndRawUdpTrafficStaySingle)
{
	CNetConnection Connection;
	InitConnection(Connection);
	Connection.SetQmUpstreamCopies(3);
	const unsigned char aData[] = {0, 0, 1, 2, 3};
	CNetBase::SendPacketConnless(m_Sender, &m_Target, aData, sizeof(aData), false, nullptr);
	EXPECT_EQ(ReceivePackets().size(), 1u);
	ASSERT_EQ(net_udp_send(m_Sender, &m_Target, aData, sizeof(aData)), (int)sizeof(aData));
	EXPECT_EQ(ReceivePackets().size(), 1u);
}

INSTANTIATE_TEST_SUITE_P(Protocols, CQmUpstreamRedundancyLoopback,
	::testing::Values(CProtocolCase{NETTYPE_IPV4, false}, CProtocolCase{NETTYPE_IPV4, true}, CProtocolCase{NETTYPE_IPV6, false}, CProtocolCase{NETTYPE_IPV6, true}),
	[](const ::testing::TestParamInfo<CProtocolCase> &Info) {
		return std::string(Info.param.m_NetType == NETTYPE_IPV4 ? "IPv4" : "IPv6") + (Info.param.m_Sixup ? "Sixup" : "Legacy");
	});
