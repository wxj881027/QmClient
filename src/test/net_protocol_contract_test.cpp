// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <gtest/gtest.h>
#include <test/test.h>

TEST(NetProtocolContract, Ipv6SocketUsesIpv6TrafficClassOutsideWindows)
{
	const std::string Source = ReadTestSourceFile("src/base/system.cpp");
	const size_t Ipv6Start = Source.find("if(bindaddr.type & NETTYPE_IPV6)");
	ASSERT_NE(Ipv6Start, std::string::npos);
	const size_t Ipv6End = Source.find("#if defined(CONF_WEBSOCKETS)", Ipv6Start);
	ASSERT_NE(Ipv6End, std::string::npos);
	const std::string Ipv6SocketSetup = Source.substr(Ipv6Start, Ipv6End - Ipv6Start);

	EXPECT_NE(Ipv6SocketSetup.find("setsockopt(socket, IPPROTO_IPV6, IPV6_TCLASS"), std::string::npos);
	EXPECT_EQ(Ipv6SocketSetup.find("setsockopt(socket, IPPROTO_IP, IP_TOS"), std::string::npos);
}

TEST(NetProtocolContract, SocketUsesExpeditedForwardingDscp)
{
	// 官方 d3810ba51：IPv4/IPv6 的 DSCP 都改用 EF(0xB8)，本地原先保留 IPTOS_LOWDELAY(0x10)。
	const std::string Source = ReadTestSourceFile("src/base/system.cpp");
	EXPECT_NE(Source.find("int iptos = 0xB8; // IPTOS_DSCP_EF, expedited forwarding"), std::string::npos);
	EXPECT_NE(Source.find("int TrafficClass = 0xB8; // IPTOS_DSCP_EF, expedited forwarding"), std::string::npos);
	EXPECT_EQ(Source.find("0x10; // IPTOS_LOWDELAY"), std::string::npos);
}

TEST(NetProtocolContract, VanillaAntispoofBoundsPreconnectionChunk)
{
	const std::string Source = ReadTestSourceFile("src/engine/shared/network_server.cpp");
	const size_t Start = Source.find("else if(!IsCtrl && g_Config.m_SvVanillaAntiSpoof");
	ASSERT_NE(Start, std::string::npos);
	// 官方 7131ad28b 把 OnConnCtrlMsg 合并进了 OnTokenCtrlMsg，这里改用新的边界标记
	const size_t End = Source.find("void CNetServer::OnTokenCtrlMsg", Start);
	ASSERT_NE(End, std::string::npos);
	const std::string Handler = Source.substr(Start, End - Start);

	EXPECT_NE(Handler.find("if(Packet.m_DataSize < 2)"), std::string::npos);
	EXPECT_NE(Handler.find("const int Remaining"), std::string::npos);
	EXPECT_NE(Handler.find("std::min(Header.m_Size, Remaining)"), std::string::npos);
}

TEST(NetProtocolContract, RejoiningClientsAreHeldOutsideGameState)
{
	// 官方 7131ad28b/b946fa9a2：重连槽位不发快照，标记在重连、掉线与换图时清理
	const std::string Header = ReadTestSourceFile("src/engine/shared/network.h");
	EXPECT_NE(Header.find("typedef int (*NETFUNC_CLIENTREJOIN)(int ClientId, void *pUser, bool Sixup, bool VanillaAuth);"), std::string::npos);
	const std::string Server = ReadTestSourceFile("src/engine/server/server.cpp");
	EXPECT_NE(Server.find("m_aClients[i].m_State != CClient::STATE_INGAME || m_aClients[i].m_Rejoining"), std::string::npos);
	EXPECT_NE(Server.find("m_Rejoining = true;"), std::string::npos);
	EXPECT_NE(Server.find("m_Rejoining = false;"), std::string::npos);
	EXPECT_NE(Server.find("GameServer()->OnClientRejoin(ClientId);"), std::string::npos);
}
