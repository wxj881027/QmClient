// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/shared/network.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace
{

	TEST(Net, Ipv6SocketUsesIpv6TrafficClassOutsideWindows)
	{
		const std::string Source = ReadTestSourceFile("src/base/net.cpp");
		const size_t Ipv6Start = Source.find("if(bindaddr.type & NETTYPE_IPV6)");
		ASSERT_NE(Ipv6Start, std::string::npos);
		const size_t Ipv6End = Source.find("#if defined(CONF_WEBSOCKETS)", Ipv6Start);
		ASSERT_NE(Ipv6End, std::string::npos);
		const std::string Ipv6SocketSetup = Source.substr(Ipv6Start, Ipv6End - Ipv6Start);

		EXPECT_NE(Ipv6SocketSetup.find("setsockopt(socket, IPPROTO_IPV6, IPV6_TCLASS"), std::string::npos);
		EXPECT_EQ(Ipv6SocketSetup.find("setsockopt(socket, IPPROTO_IP, IP_TOS"), std::string::npos);
	}

	void InitNetBase()
	{
		static bool s_Initialized = false;
		if(!s_Initialized)
		{
			CNetBase::Init();
			s_Initialized = true;
		}
	}

	unsigned char *PackTestChunk(CNetPacketConstruct *pPacket, int Flags, int DataSize, const unsigned char *pData, bool Sixup, int Sequence = 17)
	{
		CNetChunkHeader Header;
		Header.m_Flags = Flags;
		Header.m_Size = DataSize;
		Header.m_Sequence = (Flags & NET_CHUNKFLAG_VITAL) ? Sequence : -1;
		unsigned char *pChunkData = Header.Pack(pPacket->m_aChunkData + pPacket->m_DataSize, Sixup ? 6 : 4);
		mem_copy(pChunkData, pData, DataSize);
		pPacket->m_DataSize = (int)(pChunkData + DataSize - pPacket->m_aChunkData);
		pPacket->m_NumChunks++;
		return pChunkData;
	}

	CNetPacketConstruct BuildTestPacket(bool Sixup)
	{
		CNetPacketConstruct Packet;
		mem_zero(&Packet, sizeof(Packet));
		Packet.m_Flags = 0;
		Packet.m_Ack = 234;
		const unsigned char aChunk1[] = {'h', 'e', 'l', 'l', 'o'};
		const unsigned char aChunk2[] = {'s', 'n', 'a', 'p'};
		PackTestChunk(&Packet, NET_CHUNKFLAG_VITAL, sizeof(aChunk1), aChunk1, Sixup);
		PackTestChunk(&Packet, 0, sizeof(aChunk2), aChunk2, Sixup);
		return Packet;
	}

	void ExpectPacketRoundtrip(const CNetPacketConstruct &Original, SECURITY_TOKEN SecurityToken, bool Sixup)
	{
		CNetPacketConstruct Packet = Original;
		unsigned char aBuffer[NET_MAX_PACKETSIZE];
		const int PackedSize = CNetBase::PackPacket(aBuffer, sizeof(aBuffer), &Packet, SecurityToken, Sixup);
		ASSERT_GT(PackedSize, 0);

		CNetPacketConstruct Unpacked;
		bool UnpackedSixup = Sixup;
		SECURITY_TOKEN UnpackedToken = NET_SECURITY_TOKEN_UNKNOWN;
		SECURITY_TOKEN ResponseToken = NET_SECURITY_TOKEN_UNKNOWN;
		ASSERT_EQ(CNetBase::UnpackPacket(aBuffer, PackedSize, &Unpacked, UnpackedSixup, &UnpackedToken, &ResponseToken), 0);
		EXPECT_EQ(UnpackedSixup, Sixup);
		EXPECT_EQ(Unpacked.m_Flags & ~NET_PACKETFLAG_COMPRESSION, Original.m_Flags);
		EXPECT_EQ(Unpacked.m_Ack, Original.m_Ack);
		EXPECT_EQ(Unpacked.m_NumChunks, Original.m_NumChunks);
		EXPECT_EQ(Unpacked.m_DataSize, Original.m_DataSize);
		EXPECT_EQ(mem_comp(Unpacked.m_aChunkData, Original.m_aChunkData, Original.m_DataSize), 0);
		if(Sixup)
			EXPECT_EQ(UnpackedToken, SecurityToken);
	}

	NETSOCKET BindUdpSocket(int Port)
	{
		NETADDR BindAddr = {};
		BindAddr.type = NETTYPE_IPV4;
		BindAddr.port = Port;
		return net_udp_create(BindAddr);
	}

	int OnTestClientConnected(int ClientId, void *pUser, bool Sixup)
	{
		(void)Sixup;
		*static_cast<int *>(pUser) = ClientId;
		return 0;
	}

	int OnTestClientDisconnected(int ClientId, const char *pReason, void *pUser)
	{
		(void)ClientId;
		(void)pReason;
		(void)pUser;
		return 0;
	}

	bool OpenLoopbackServer(CNetServer &Server, NETADDR &ServerAddr)
	{
		if(net_addr_from_str(&ServerAddr, "127.0.0.1"))
			return false;
		for(int Attempt = 0; Attempt < 100; ++Attempt)
		{
			ServerAddr.port = secure_rand_below(64511) + 1024;
			if(Server.Open(ServerAddr, nullptr, 1, 1))
				return true;
		}
		return false;
	}

	void DrainServerChunks(CNetServer &Server, std::vector<std::string> *pChunks)
	{
		CNetChunk Chunk;
		SECURITY_TOKEN ResponseToken;
		while(Server.Recv(&Chunk, &ResponseToken))
		{
			if(pChunks != nullptr)
				pChunks->emplace_back(static_cast<const char *>(Chunk.m_pData), Chunk.m_DataSize);
		}
	}

	void DrainClientChunks(CNetClient &Client, std::vector<std::string> *pChunks)
	{
		CNetChunk Chunk;
		SECURITY_TOKEN ResponseToken;
		while(Client.Recv(&Chunk, &ResponseToken, false))
		{
			if(pChunks != nullptr)
				pChunks->emplace_back(static_cast<const char *>(Chunk.m_pData), Chunk.m_DataSize);
		}
	}

	void PumpServerChunks(CNetServer &Server, CNetClient &Client, std::vector<std::string> *pChunks, size_t ExpectedChunks)
	{
		for(int Attempt = 0; Attempt < 200 && pChunks->size() < ExpectedChunks; ++Attempt)
		{
			DrainServerChunks(Server, pChunks);
			DrainClientChunks(Client, nullptr);
			Server.Update();
			Client.Update();
			if(pChunks->size() < ExpectedChunks)
				std::this_thread::sleep_for(1ms);
		}
	}

	void PumpClientChunks(CNetClient &Client, CNetServer &Server, std::vector<std::string> *pChunks, size_t ExpectedChunks)
	{
		for(int Attempt = 0; Attempt < 200 && pChunks->size() < ExpectedChunks; ++Attempt)
		{
			DrainClientChunks(Client, pChunks);
			DrainServerChunks(Server, nullptr);
			Client.Update();
			Server.Update();
			if(pChunks->size() < ExpectedChunks)
				std::this_thread::sleep_for(1ms);
		}
	}

	std::vector<unsigned char> MakeTestPayload(int Size, uint32_t Seed)
	{
		std::vector<unsigned char> vPayload(Size);
		uint32_t State = Seed;
		for(unsigned char &Byte : vPayload)
		{
			State = State * 1664525u + 1013904223u;
			Byte = State >> 24;
		}
		return vPayload;
	}

	class CNetKcpBypassTest : public ::testing::Test
	{
	protected:
		CNetServer m_Server;
		CNetClient m_Client;
		int m_ClientId = -1;
		int m_OldConnTimeout = 0;
		int m_OldSvKcp = 0;
		int m_OldSvVanillaAntiSpoof = 0;

		void SetUp() override
		{
			InitNetBase();
			m_OldConnTimeout = g_Config.m_ConnTimeout;
			m_OldSvKcp = g_Config.m_SvKcp;
			m_OldSvVanillaAntiSpoof = g_Config.m_SvVanillaAntiSpoof;
			g_Config.m_ConnTimeout = 10;
			g_Config.m_SvKcp = 1;
			g_Config.m_SvVanillaAntiSpoof = 0;

			NETADDR ServerAddr;
			ASSERT_TRUE(OpenLoopbackServer(m_Server, ServerAddr));
			m_Server.SetCallbacks(OnTestClientConnected, OnTestClientDisconnected, &m_ClientId);

			NETADDR ClientBindAddr = {};
			ClientBindAddr.type = NETTYPE_IPV4;
			ASSERT_TRUE(m_Client.Open(ClientBindAddr));
			m_Client.Connect(&ServerAddr, 1);

			for(int Attempt = 0; Attempt < 100 && (m_Client.State() != NETSTATE_ONLINE || m_ClientId < 0); ++Attempt)
			{
				DrainServerChunks(m_Server, nullptr);
				DrainClientChunks(m_Client, nullptr);
				m_Server.Update();
				m_Client.Update();
				std::this_thread::sleep_for(1ms);
			}
			ASSERT_EQ(m_Client.State(), NETSTATE_ONLINE) << m_Client.ErrorString();
			ASSERT_EQ(m_ClientId, 0);

			DrainServerChunks(m_Server, nullptr);
			DrainClientChunks(m_Client, nullptr);
			constexpr uint32_t KcpConv = 0x1357247u;
			ASSERT_TRUE(m_Server.ActivateKcp(m_ClientId, KcpConv));
			ASSERT_TRUE(m_Client.ActivateKcp(KcpConv));
		}

		void TearDown() override
		{
			m_Client.Close();
			m_Server.Close();
			g_Config.m_ConnTimeout = m_OldConnTimeout;
			g_Config.m_SvKcp = m_OldSvKcp;
			g_Config.m_SvVanillaAntiSpoof = m_OldSvVanillaAntiSpoof;
		}
	};

} // namespace

TEST(Net, Ipv4AndIpv6Work)
{
	NETADDR Bindaddr = {};
	NETSOCKET Socket1;
	NETSOCKET Socket2;

	Bindaddr.type = NETTYPE_IPV4 | NETTYPE_IPV6;
	Socket2 = net_udp_create(Bindaddr);
	do
	{
		Bindaddr.port = secure_rand_below(65535 - 1024) + 1024;
	} while(!(Socket1 = net_udp_create(Bindaddr)));

	NETADDR LocalhostV4;
	NETADDR LocalhostV6;
	NETADDR TargetV4;
	NETADDR TargetV6;
	ASSERT_FALSE(net_addr_from_str(&LocalhostV4, "127.0.0.1"));
	ASSERT_FALSE(net_addr_from_str(&LocalhostV6, "[::1]"));
	TargetV4 = LocalhostV4;
	TargetV6 = LocalhostV6;
	TargetV4.port = Bindaddr.port;
	TargetV6.port = Bindaddr.port;

	NETADDR Addr;
	unsigned char *pData;

	EXPECT_EQ(net_udp_send(Socket2, &TargetV4, "abc", 3), 3);

	EXPECT_EQ(net_socket_read_wait(Socket1, 10s), 1);
	ASSERT_EQ(net_udp_recv(Socket1, &Addr, &pData), 3);
	Addr.port = 0;
	EXPECT_EQ(Addr, LocalhostV4);
	EXPECT_EQ(mem_comp(pData, "abc", 3), 0);

	EXPECT_EQ(net_udp_send(Socket2, &TargetV6, "def", 3), 3);

	EXPECT_EQ(net_socket_read_wait(Socket1, 10s), 1);
	ASSERT_EQ(net_udp_recv(Socket1, &Addr, &pData), 3);
	Addr.port = 0;
	EXPECT_EQ(Addr, LocalhostV6);
	EXPECT_EQ(mem_comp(pData, "def", 3), 0);

	net_udp_close(Socket1);
	net_udp_close(Socket2);
}

TEST(Net, FailedUdpSendDoesNotCountAsSentTraffic)
{
	NETSOCKET Socket = BindUdpSocket(0);
	ASSERT_NE(Socket, nullptr);

	NETADDR IncompatibleTarget = {};
	ASSERT_FALSE(net_addr_from_str(&IncompatibleTarget, "[::1]:1"));

	NETSTATS Before = {};
	NETSTATS After = {};
	net_stats(&Before);
	EXPECT_EQ(net_udp_send(Socket, &IncompatibleTarget, "x", 1), -1);
	net_stats(&After);

	EXPECT_EQ(After.sent_packets, Before.sent_packets);
	EXPECT_EQ(After.sent_bytes, Before.sent_bytes);
	EXPECT_EQ(After.send_errors, Before.send_errors + 1);
	net_udp_close(Socket);
}

TEST(Net, SuccessfulUdpSendCountsAsSentTraffic)
{
	NETSOCKET Socket = BindUdpSocket(0);
	ASSERT_NE(Socket, nullptr);

	NETADDR Target = {};
	ASSERT_FALSE(net_addr_from_str(&Target, "127.0.0.1:1"));
	NETSTATS Before = {};
	NETSTATS After = {};
	net_stats(&Before);
	EXPECT_EQ(net_udp_send(Socket, &Target, "abc", 3), 3);
	net_stats(&After);

	EXPECT_EQ(After.sent_packets, Before.sent_packets + 1);
	EXPECT_EQ(After.sent_bytes, Before.sent_bytes + 3);
	EXPECT_EQ(After.send_errors, Before.send_errors);
	net_udp_close(Socket);
}

TEST(NetClient, QosCanBeDisabledAndReenabledWithoutAConnection)
{
	CNetClient Client;
	NETADDR BindAddr = {};
	BindAddr.type = NETTYPE_IPV4;
	ASSERT_TRUE(Client.Open(BindAddr, true));
	EXPECT_EQ(Client.QosStatus(), ENetQosStatus::PENDING);

	Client.SetLowLatency(false);
	EXPECT_EQ(Client.QosStatus(), ENetQosStatus::DISABLED);

	Client.SetLowLatency(true);
	EXPECT_EQ(Client.QosStatus(), ENetQosStatus::PENDING);
	EXPECT_STREQ(Client.QosStatusName(), "pending");

	NETADDR UnvalidatedPeer = {};
	ASSERT_FALSE(net_addr_from_str(&UnvalidatedPeer, "127.0.0.1:8303"));
	Client.Connect(&UnvalidatedPeer, 1);
	Client.SetLowLatency(false);
	Client.SetLowLatency(true);
	EXPECT_EQ(Client.QosStatus(), ENetQosStatus::PENDING);

	Client.Disconnect("test");
	EXPECT_EQ(Client.QosStatus(), ENetQosStatus::PENDING);

	Client.Connect7(&UnvalidatedPeer, 1);
	Client.SetLowLatency(false);
	Client.SetLowLatency(true);
	EXPECT_EQ(Client.QosStatus(), ENetQosStatus::PENDING);
	Client.Disconnect("test");
	Client.SetLowLatency(false);
	Client.SetLowLatency(true);
	EXPECT_EQ(Client.QosStatus(), ENetQosStatus::PENDING);
	Client.Close();
}

TEST(Net, PackPacketKeepsLegacyRoundtrip)
{
	InitNetBase();

	ExpectPacketRoundtrip(BuildTestPacket(false), NET_SECURITY_TOKEN_UNSUPPORTED, false);
	ExpectPacketRoundtrip(BuildTestPacket(true), 0x1234567, true);
}

TEST(Net, PackPacketRejectsTooSmallBuffer)
{
	InitNetBase();

	CNetPacketConstruct Packet;
	mem_zero(&Packet, sizeof(Packet));
	Packet.m_Flags = NET_PACKETFLAG_CONTROL;
	Packet.m_Ack = 1;
	Packet.m_DataSize = 1;
	Packet.m_aChunkData[0] = NET_CTRLMSG_KEEPALIVE;

	unsigned char aBuffer[2];
	EXPECT_EQ(CNetBase::PackPacket(aBuffer, sizeof(aBuffer), &Packet, NET_SECURITY_TOKEN_UNSUPPORTED), -1);
}

TEST(Net, PacketChunkUnpackerSkipsOldVitalChunk)
{
	CNetPacketConstruct Packet;
	mem_zero(&Packet, sizeof(Packet));
	const unsigned char aOldVital[] = {'o', 'l', 'd'};
	const unsigned char aNextChunk[] = {'n', 'e', 'x', 't'};
	PackTestChunk(&Packet, NET_CHUNKFLAG_VITAL, sizeof(aOldVital), aOldVital, false, 0);
	PackTestChunk(&Packet, 0, sizeof(aNextChunk), aNextChunk, false);

	NETADDR Addr = {};
	Addr.type = NETTYPE_IPV4;
	CNetConnection Connection;
	Connection.DirectInit(Addr, NET_SECURITY_TOKEN_UNSUPPORTED, NET_TOKEN_NONE, false);

	CPacketChunkUnpacker Unpacker;
	Unpacker.FeedPacket(Addr, Packet, &Connection, 0);

	CNetChunk Chunk;
	ASSERT_TRUE(Unpacker.UnpackNextChunk(&Chunk));
	EXPECT_EQ(Chunk.m_DataSize, (int)sizeof(aNextChunk));
	EXPECT_EQ(mem_comp(Chunk.m_pData, aNextChunk, sizeof(aNextChunk)), 0);
	EXPECT_FALSE(Unpacker.UnpackNextChunk(&Chunk));
}

TEST(Net, PacketChunkUnpackerRejectsMissingDeclaredChunk)
{
	CNetPacketConstruct Packet;
	mem_zero(&Packet, sizeof(Packet));
	const unsigned char aChunk[] = {'o', 'n', 'e'};
	PackTestChunk(&Packet, 0, sizeof(aChunk), aChunk, false);
	Packet.m_NumChunks++;

	NETADDR Addr = {};
	Addr.type = NETTYPE_IPV4;
	CNetConnection Connection;
	Connection.DirectInit(Addr, NET_SECURITY_TOKEN_UNSUPPORTED, NET_TOKEN_NONE, false);

	CPacketChunkUnpacker Unpacker;
	Unpacker.FeedPacket(Addr, Packet, &Connection, 0);

	CNetChunk Chunk;
	ASSERT_TRUE(Unpacker.UnpackNextChunk(&Chunk));
	EXPECT_EQ(Chunk.m_DataSize, (int)sizeof(aChunk));
	EXPECT_EQ(mem_comp(Chunk.m_pData, aChunk, sizeof(aChunk)), 0);
	EXPECT_FALSE(Unpacker.UnpackNextChunk(&Chunk));
}

TEST(Net, KcpHeaderRejectsInvalidPackets)
{
	unsigned char aPacket[NET_KCP_HEADER_SIZE + 1] = {'Q', 'K', 'C', 'P', 1, 0, 0, 0, 1, 0};
	uint32_t Conv = 0;
	const unsigned char *pPayload = nullptr;
	int PayloadSize = 0;

	EXPECT_FALSE(CNetKcpSession::UnpackHeader(nullptr, sizeof(aPacket), &Conv, &pPayload, &PayloadSize));
	EXPECT_FALSE(CNetKcpSession::UnpackHeader(aPacket, NET_KCP_HEADER_SIZE, &Conv, &pPayload, &PayloadSize));
	EXPECT_TRUE(CNetKcpSession::UnpackHeader(aPacket, sizeof(aPacket), &Conv, &pPayload, &PayloadSize));
	EXPECT_EQ(Conv, 1u);
	EXPECT_EQ(pPayload, aPacket + NET_KCP_HEADER_SIZE);
	EXPECT_EQ(PayloadSize, 1);

	aPacket[0] = 'X';
	EXPECT_FALSE(CNetKcpSession::UnpackHeader(aPacket, sizeof(aPacket), &Conv, &pPayload, &PayloadSize));
	aPacket[0] = 'Q';
	aPacket[4] = 2;
	EXPECT_FALSE(CNetKcpSession::UnpackHeader(aPacket, sizeof(aPacket), &Conv, &pPayload, &PayloadSize));
	aPacket[4] = 1;
	aPacket[8] = 0;
	EXPECT_FALSE(CNetKcpSession::UnpackHeader(aPacket, sizeof(aPacket), &Conv, &pPayload, &PayloadSize));
}

TEST(Net, KcpSessionSendsOverUdpAndRoundtripsPacket)
{
	InitNetBase();

	NETSOCKET Socket1 = nullptr;
	NETSOCKET Socket2 = nullptr;
	int Port1 = 0;
	int Port2 = 0;
	for(int Attempt = 0; Attempt < 100 && (!Socket1 || !Socket2); ++Attempt)
	{
		if(Socket1)
		{
			net_udp_close(Socket1);
			Socket1 = nullptr;
		}
		if(Socket2)
		{
			net_udp_close(Socket2);
			Socket2 = nullptr;
		}
		Port1 = secure_rand_below(65535 - 1024) + 1024;
		Port2 = secure_rand_below(65535 - 1024) + 1024;
		if(Port1 == Port2)
			continue;
		Socket1 = BindUdpSocket(Port1);
		Socket2 = BindUdpSocket(Port2);
	}
	ASSERT_NE(Socket1, nullptr);
	ASSERT_NE(Socket2, nullptr);

	NETADDR Addr1;
	NETADDR Addr2;
	ASSERT_FALSE(net_addr_from_str(&Addr1, "127.0.0.1"));
	ASSERT_FALSE(net_addr_from_str(&Addr2, "127.0.0.1"));
	Addr1.port = Port1;
	Addr2.port = Port2;

	const uint32_t Conv = 0x1234567u;
	CNetKcpSession Sender;
	CNetKcpSession Receiver;
	ASSERT_TRUE(Sender.Init(Socket1, Addr2, Conv));
	ASSERT_TRUE(Receiver.Init(Socket2, Addr1, Conv));

	CNetPacketConstruct Packet;
	mem_zero(&Packet, sizeof(Packet));
	Packet.m_Flags = 0;
	Packet.m_Ack = 42;
	const unsigned char aPayload[] = {'k', 'c', 'p'};
	PackTestChunk(&Packet, 0, sizeof(aPayload), aPayload, false);
	ASSERT_EQ(Sender.SendPacket(&Packet, NET_SECURITY_TOKEN_UNSUPPORTED, false), 0);
	Sender.Flush();

	NETADDR From;
	unsigned char *pUdpData = nullptr;
	ASSERT_EQ(net_socket_read_wait(Socket2, 10s), 1);
	const int UdpBytes = net_udp_recv(Socket2, &From, &pUdpData);
	ASSERT_GT(UdpBytes, NET_KCP_HEADER_SIZE);
	EXPECT_TRUE(CNetKcpSession::IsKcpPacket(pUdpData, UdpBytes));
	ASSERT_TRUE(Receiver.Input(From, pUdpData, UdpBytes, false));

	unsigned char aPacked[NET_MAX_PACKETSIZE];
	const int PackedSize = Receiver.Recv(aPacked, sizeof(aPacked));
	ASSERT_GT(PackedSize, 0);

	CNetPacketConstruct Unpacked;
	bool Sixup = false;
	SECURITY_TOKEN Token = NET_SECURITY_TOKEN_UNKNOWN;
	SECURITY_TOKEN ResponseToken = NET_SECURITY_TOKEN_UNKNOWN;
	ASSERT_EQ(CNetBase::UnpackPacket(aPacked, PackedSize, &Unpacked, Sixup, &Token, &ResponseToken), 0);
	EXPECT_EQ(Unpacked.m_Ack, Packet.m_Ack);
	EXPECT_EQ(Unpacked.m_NumChunks, Packet.m_NumChunks);
	EXPECT_EQ(Unpacked.m_DataSize, Packet.m_DataSize);
	EXPECT_EQ(mem_comp(Unpacked.m_aChunkData, Packet.m_aChunkData, Packet.m_DataSize), 0);

	net_udp_close(Socket1);
	net_udp_close(Socket2);
}

TEST(Net, KcpSessionRejectsInvalidRebindWithoutChangingPeer)
{
	NETSOCKET Socket = BindUdpSocket(0);
	ASSERT_NE(Socket, nullptr);

	NETADDR OriginalPeer = {};
	NETADDR RebindPeer = {};
	ASSERT_FALSE(net_addr_from_str(&OriginalPeer, "127.0.0.1:8303"));
	ASSERT_FALSE(net_addr_from_str(&RebindPeer, "127.0.0.1:8304"));
	const uint32_t Conv = 0x1234567u;
	CNetKcpSession Session;
	ASSERT_TRUE(Session.Init(Socket, OriginalPeer, Conv));

	unsigned char aInvalidPacket[NET_KCP_HEADER_SIZE + 1] = {'Q', 'K', 'C', 'P', 1};
	aInvalidPacket[5] = (Conv >> 24) & 0xff;
	aInvalidPacket[6] = (Conv >> 16) & 0xff;
	aInvalidPacket[7] = (Conv >> 8) & 0xff;
	aInvalidPacket[8] = Conv & 0xff;
	EXPECT_FALSE(Session.Input(RebindPeer, aInvalidPacket, sizeof(aInvalidPacket), true));
	EXPECT_EQ(*Session.PeerAddress(), OriginalPeer);

	net_udp_close(Socket);
}

TEST_F(CNetKcpBypassTest, ClientFlushBypassDrainsQueuedVitalChunk)
{
	const std::vector<std::string> vCommands = {"client-spec", "client-tp", "client-spec-restore"};
	for(const std::string &Command : vCommands)
	{
		CNetChunk VitalChunk = {};
		VitalChunk.m_ClientId = 0;
		VitalChunk.m_pData = Command.data();
		VitalChunk.m_DataSize = Command.size();
		VitalChunk.m_Flags = NETSENDFLAG_VITAL;
		ASSERT_EQ(m_Client.Send(&VitalChunk), 0);
	}

	const char aBypass[] = "client-bypass";
	CNetChunk BypassChunk = {};
	BypassChunk.m_ClientId = 0;
	BypassChunk.m_pData = aBypass;
	BypassChunk.m_DataSize = sizeof(aBypass) - 1;
	BypassChunk.m_Flags = NETSENDFLAG_FLUSH;
	ASSERT_EQ(m_Client.Send(&BypassChunk), 0);

	std::vector<std::string> vReceived;
	PumpServerChunks(m_Server, m_Client, &vReceived, vCommands.size() + 1);
	std::vector<std::string> vExpected = vCommands;
	vExpected.emplace_back(aBypass);
	EXPECT_EQ(vReceived, vExpected);
}

TEST_F(CNetKcpBypassTest, ClientFlushBypassJoinsPendingKcpPacket)
{
	const std::vector<unsigned char> vVital1 = MakeTestPayload(700, 1);
	const std::vector<unsigned char> vVital2 = MakeTestPayload(600, 2);
	for(const std::vector<unsigned char> *pVital : {&vVital1, &vVital2})
	{
		CNetChunk VitalChunk = {};
		VitalChunk.m_ClientId = 0;
		VitalChunk.m_pData = pVital->data();
		VitalChunk.m_DataSize = pVital->size();
		VitalChunk.m_Flags = NETSENDFLAG_VITAL;
		ASSERT_EQ(m_Client.Send(&VitalChunk), 0);
	}
	EXPECT_EQ(m_Client.TransportStats().m_SendQueueDepth, 0);

	const std::vector<unsigned char> vTrailingInput = MakeTestPayload(70, 3);
	CNetChunk InputChunk = {};
	InputChunk.m_ClientId = 0;
	InputChunk.m_pData = vTrailingInput.data();
	InputChunk.m_DataSize = vTrailingInput.size();
	InputChunk.m_Flags = NETSENDFLAG_FLUSH;
	ASSERT_EQ(m_Client.Send(&InputChunk), 0);
	EXPECT_GE(m_Client.TransportStats().m_SendQueueDepth, 2);

	std::vector<std::string> vReceived;
	PumpServerChunks(m_Server, m_Client, &vReceived, 3);
	ASSERT_EQ(vReceived.size(), 3u);
	EXPECT_EQ(vReceived[0], std::string(reinterpret_cast<const char *>(vVital1.data()), vVital1.size()));
	EXPECT_EQ(vReceived[1], std::string(reinterpret_cast<const char *>(vVital2.data()), vVital2.size()));
	EXPECT_EQ(vReceived[2], std::string(reinterpret_cast<const char *>(vTrailingInput.data()), vTrailingInput.size()));
}

TEST_F(CNetKcpBypassTest, ClientFlushWithoutPendingDataKeepsRawBypass)
{
	const std::vector<unsigned char> vInput = MakeTestPayload(70, 3);
	CNetChunk InputChunk = {};
	InputChunk.m_ClientId = 0;
	InputChunk.m_pData = vInput.data();
	InputChunk.m_DataSize = vInput.size();
	InputChunk.m_Flags = NETSENDFLAG_FLUSH;
	ASSERT_EQ(m_Client.Send(&InputChunk), 0);
	EXPECT_EQ(m_Client.TransportStats().m_SendQueueDepth, 0);

	std::vector<std::string> vReceived;
	PumpServerChunks(m_Server, m_Client, &vReceived, 1);
	ASSERT_EQ(vReceived.size(), 1u);
	EXPECT_EQ(vReceived[0], std::string(reinterpret_cast<const char *>(vInput.data()), vInput.size()));
}

TEST_F(CNetKcpBypassTest, ServerFlushBypassDrainsQueuedVitalChunk)
{
	const char aVital[] = "server-vital";
	CNetChunk VitalChunk = {};
	VitalChunk.m_ClientId = m_ClientId;
	VitalChunk.m_pData = aVital;
	VitalChunk.m_DataSize = sizeof(aVital) - 1;
	VitalChunk.m_Flags = NETSENDFLAG_VITAL;
	ASSERT_EQ(m_Server.Send(&VitalChunk), 0);

	const char aBypass[] = "server-bypass";
	CNetChunk BypassChunk = {};
	BypassChunk.m_ClientId = m_ClientId;
	BypassChunk.m_pData = aBypass;
	BypassChunk.m_DataSize = sizeof(aBypass) - 1;
	BypassChunk.m_Flags = NETSENDFLAG_FLUSH;
	ASSERT_EQ(m_Server.Send(&BypassChunk), 0);

	std::vector<std::string> vReceived;
	PumpClientChunks(m_Client, m_Server, &vReceived, 2);
	EXPECT_EQ(vReceived, (std::vector<std::string>{aVital, aBypass}));
}

TEST_F(CNetKcpBypassTest, ServerFlushBypassJoinsPendingKcpPacket)
{
	const std::vector<unsigned char> vVital1 = MakeTestPayload(700, 4);
	const std::vector<unsigned char> vVital2 = MakeTestPayload(600, 5);
	for(const std::vector<unsigned char> *pVital : {&vVital1, &vVital2})
	{
		CNetChunk VitalChunk = {};
		VitalChunk.m_ClientId = m_ClientId;
		VitalChunk.m_pData = pVital->data();
		VitalChunk.m_DataSize = pVital->size();
		VitalChunk.m_Flags = NETSENDFLAG_VITAL;
		ASSERT_EQ(m_Server.Send(&VitalChunk), 0);
	}
	EXPECT_EQ(m_Server.ClientTransportStats(m_ClientId).m_SendQueueDepth, 0);

	const std::vector<unsigned char> vSnapshot = MakeTestPayload(70, 6);
	CNetChunk SnapshotChunk = {};
	SnapshotChunk.m_ClientId = m_ClientId;
	SnapshotChunk.m_pData = vSnapshot.data();
	SnapshotChunk.m_DataSize = vSnapshot.size();
	SnapshotChunk.m_Flags = NETSENDFLAG_FLUSH;
	ASSERT_EQ(m_Server.Send(&SnapshotChunk), 0);
	EXPECT_GE(m_Server.ClientTransportStats(m_ClientId).m_SendQueueDepth, 2);

	std::vector<std::string> vReceived;
	PumpClientChunks(m_Client, m_Server, &vReceived, 3);
	ASSERT_EQ(vReceived.size(), 3u);
	EXPECT_EQ(vReceived[0], std::string(reinterpret_cast<const char *>(vVital1.data()), vVital1.size()));
	EXPECT_EQ(vReceived[1], std::string(reinterpret_cast<const char *>(vVital2.data()), vVital2.size()));
	EXPECT_EQ(vReceived[2], std::string(reinterpret_cast<const char *>(vSnapshot.data()), vSnapshot.size()));
}
