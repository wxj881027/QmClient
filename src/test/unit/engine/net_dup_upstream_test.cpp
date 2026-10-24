#include <base/system.h>

#include <gtest/gtest.h>

#include <chrono>

using namespace std::chrono_literals;

namespace
{
	// 包头布局：byte0 高 6 位为 flags，byte2 为块数量。
	constexpr unsigned char FLAG_CONTROL = 1 << 2;
	constexpr unsigned char FLAG_CONNLESS = 1 << 3;
	constexpr unsigned char FLAG_RESEND = 1 << 4;
	constexpr unsigned char FLAG_COMPRESSION = 1 << 5;

	class CDupUpstreamLoopback : public ::testing::Test
	{
	protected:
		NETSOCKET m_Receiver = nullptr;
		NETSOCKET m_Sender = nullptr;
		NETADDR m_Target = {};
		int m_OldDup = 0;

		void SetUp() override
		{
			m_OldDup = g_NetDupUpstream;
			NETADDR BindAddr = {};
			BindAddr.type = NETTYPE_IPV4;
			do
			{
				BindAddr.port = secure_rand_below(65535 - 1024) + 1024;
				m_Receiver = net_udp_create(BindAddr);
			} while(!m_Receiver);
			const unsigned short ReceiverPort = BindAddr.port;
			BindAddr.port = 0;
			m_Sender = net_udp_create(BindAddr);
			ASSERT_NE(m_Sender, nullptr);
			ASSERT_FALSE(net_addr_from_str(&m_Target, "127.0.0.1"));
			m_Target.port = ReceiverPort;
		}

		void TearDown() override
		{
			g_NetDupUpstream = m_OldDup;
			net_udp_close(m_Receiver);
			net_udp_close(m_Sender);
		}

		// 本机回环在 sendto 返回时数据已入队，等短暂时间后取空接收队列即可。
		int SendAndCount(int Copies, const unsigned char *pPacket, int Size)
		{
			g_NetDupUpstream = Copies;
			EXPECT_EQ(net_udp_send(m_Sender, &m_Target, pPacket, Size), Size);
			int Received = 0;
			NETADDR From;
			unsigned char *pData;
			while(net_socket_read_wait(m_Receiver, 50ms) == 1)
			{
				const int Len = net_udp_recv(m_Receiver, &From, &pData);
				if(Len <= 0)
					continue;
				EXPECT_EQ(Len, Size);
				EXPECT_EQ(mem_comp(pData, pPacket, Size), 0);
				Received++;
			}
			return Received;
		}
	};
}

TEST_F(CDupUpstreamLoopback, DataPacketIsSentRequestedNumberOfTimes)
{
	const unsigned char aPacket[] = {0x00, 0x00, 0x01, 0xAA, 0xBB};
	EXPECT_EQ(SendAndCount(3, aPacket, sizeof(aPacket)), 3);
	EXPECT_EQ(SendAndCount(2, aPacket, sizeof(aPacket)), 2);
}

TEST_F(CDupUpstreamLoopback, DisabledOrSingleCopySendsOnce)
{
	const unsigned char aPacket[] = {0x00, 0x00, 0x01, 0xAA, 0xBB};
	EXPECT_EQ(SendAndCount(0, aPacket, sizeof(aPacket)), 1);
	EXPECT_EQ(SendAndCount(1, aPacket, sizeof(aPacket)), 1);
}

TEST_F(CDupUpstreamLoopback, ResendAndCompressedDataPacketsAreStillDuplicated)
{
	const unsigned char aResend[] = {static_cast<unsigned char>(FLAG_RESEND << 2), 0x00, 0x01, 0xAA};
	const unsigned char aCompressed[] = {static_cast<unsigned char>(FLAG_COMPRESSION << 2), 0x00, 0x02, 0xAA};
	EXPECT_EQ(SendAndCount(2, aResend, sizeof(aResend)), 2);
	EXPECT_EQ(SendAndCount(2, aCompressed, sizeof(aCompressed)), 2);
}

TEST_F(CDupUpstreamLoopback, ControlPacketIsNotDuplicated)
{
	const unsigned char aPacket[] = {static_cast<unsigned char>(FLAG_CONTROL << 2), 0x00, 0x01, 0x00};
	EXPECT_EQ(SendAndCount(3, aPacket, sizeof(aPacket)), 1);
}

TEST_F(CDupUpstreamLoopback, ConnlessPacketIsNotDuplicated)
{
	const unsigned char aPacket[] = {static_cast<unsigned char>(FLAG_CONNLESS << 2), 0x00, 0x01, 0xAA};
	EXPECT_EQ(SendAndCount(3, aPacket, sizeof(aPacket)), 1);
}

TEST_F(CDupUpstreamLoopback, AckOnlyPacketWithoutChunksIsNotDuplicated)
{
	const unsigned char aPacket[] = {0x00, 0x05, 0x00};
	EXPECT_EQ(SendAndCount(3, aPacket, sizeof(aPacket)), 1);
}

TEST_F(CDupUpstreamLoopback, PacketShorterThanHeaderIsNotDuplicated)
{
	const unsigned char aPacket[] = {0x00, 0x00};
	EXPECT_EQ(SendAndCount(3, aPacket, sizeof(aPacket)), 1);
}
