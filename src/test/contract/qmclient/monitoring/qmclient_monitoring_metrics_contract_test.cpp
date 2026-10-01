// 请抬头享受阳光｜日子很好 我很我---------致咩子
#define CONF_TEST 1

#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>

#include <string>

TEST(QmMonitoringMetricsContract, ManualPingTimeoutIsCheckedBeforeKcpEarlyContinue)
{
	const std::string Client = ReadRepoFile("src/engine/client/client.cpp");
	const std::string AutomaticPing = ExtractSourceFunctionBody(Client, "void CClient::UpdateGamePing()");
	ASSERT_FALSE(AutomaticPing.empty());
	const size_t Timeout = AutomaticPing.find("m_ManualPingProbe.HandleTimeout");
	const size_t KcpBranch = AutomaticPing.find("m_aNetClient[Conn].IsKcpActive()");
	ASSERT_NE(Timeout, std::string::npos);
	ASSERT_NE(KcpBranch, std::string::npos);
	EXPECT_LT(Timeout, KcpBranch);
}

TEST(QmMonitoringMetricsContract, LegacyPingPathRemainsPresent)
{
	const std::string Client = ReadRepoFile("src/engine/client/client.cpp");
	const std::string AutomaticPing = ExtractSourceFunctionBody(Client, "void CClient::UpdateGamePing()");
	ASSERT_FALSE(AutomaticPing.empty());
	EXPECT_NE(AutomaticPing.find("BeginLegacy"), std::string::npos);
	EXPECT_NE(AutomaticPing.find("NETMSG_PING, true"), std::string::npos);
}

TEST(QmMonitoringMetricsContract, AutomaticAndManualPingPathsCoordinateWithExplicitLegacySharing)
{
	const std::string Client = ReadRepoFile("src/engine/client/client.cpp");
	const std::string AutomaticPing = ExtractSourceFunctionBody(Client, "void CClient::UpdateGamePing()");
	const std::string PingMs = ExtractSourceFunctionBody(Client, "float CClient::PingMs() const");
	const std::string ManualPing = ExtractSourceFunctionBody(Client, "void CClient::Con_Ping(IConsole::IResult *pResult, void *pUserData)");
	const std::string ProcessPacket = ExtractSourceFunctionBody(Client, "void CClient::ProcessServerPacket(CNetChunk *pPacket, int Conn, bool Dummy)");
	const std::string PingReply = ExtractSourceBlock(ProcessPacket, "else if(Msg == NETMSG_PING_REPLY)", "else if(Msg == NETMSG_INPUTTIMING)");

	ASSERT_FALSE(AutomaticPing.empty());
	ASSERT_FALSE(PingMs.empty());
	ASSERT_FALSE(ManualPing.empty());
	ASSERT_FALSE(PingReply.empty());
	EXPECT_NE(AutomaticPing.find("NETMSG_PINGEX"), std::string::npos);
	EXPECT_NE(AutomaticPing.find("NETMSG_PING, true"), std::string::npos);
	EXPECT_EQ(PingMs.find("!m_ServerCapabilities.m_PingEx"), std::string::npos);
	EXPECT_NE(ManualPing.find("NETMSG_PING, true"), std::string::npos);
	EXPECT_NE(ManualPing.find("m_aGamePingProbes"), std::string::npos);
	EXPECT_NE(ManualPing.find("m_ManualPingProbe.Begin"), std::string::npos);
	EXPECT_NE(PingReply.find("m_ManualPingProbe.HandlePong"), std::string::npos);
	EXPECT_NE(PingReply.find("m_aGamePingProbes"), std::string::npos);
	EXPECT_EQ(Client.find("m_aGamePingIgnoreNextReply"), std::string::npos);
	EXPECT_EQ(Client.find("m_PingStartTime"), std::string::npos);
}
