#include <engine/client/serverbrowser_http_parse.h>

#include <game/client/components/qmclient/cross_server_follow_logic.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace
{
	class QmServerListFollow : public ::testing::Test
	{
	protected:
		std::vector<CServerInfo> m_vServers;
		CCrossServerFollowEngine m_Follow;
		std::string m_Connect;

		void SetUp() override { m_Follow.Start("Target", "Clan"); }
		bool Parse(const std::string &Addresses)
		{
			const std::string Body = R"({"servers":[{"addresses":)" + Addresses +
						 R"(,"location":"eu","info":{"max_clients":16,"max_players":16,"passworded":false,"game_type":"DDRace","name":"Server","map":{"name":"Map"},"version":"0.6","clients":[{"name":"Target","clan":"Clan","country":0,"score":-1,"is_player":true}]}}]})";
			std::unique_ptr<json_value, decltype(&json_value_free)> pJson(JsonParse(Body.data(), Body.size()), json_value_free);
			return ServerBrowserParseHttpList(pJson.get(), &m_vServers);
		}
		EFollowAction Step(double Now, int Delay = 0, std::span<const NETADDR> Current = {})
		{
			const auto &Server = m_vServers.at(0);
			SFollowServerCandidate Candidate;
			Candidate.m_ConnectAddress = Server.m_aAddress;
			Candidate.m_vAddresses.assign(Server.m_aAddresses, Server.m_aAddresses + Server.m_NumAddresses);
			for(const auto &Player : Server.m_vClients)
				Candidate.m_Players.Add(m_Follow.Target(), Player.m_aName, Player.m_aClan);
			const auto Decision = m_Follow.Step(Now, Delay, false, std::span(&Candidate, 1), Current);
			if(Decision.m_Action == EFollowAction::CONNECT)
				m_Connect = Decision.m_pServer->m_ConnectAddress;
			return Decision.m_Action;
		}
	};
}

TEST_F(QmServerListFollow, ParsedIpv4TargetConnectsImmediatelyInTheMainMenu)
{
	ASSERT_FALSE(Parse(R"(["tw-0.6+udp://127.0.0.1:8303"])"));
	ASSERT_EQ(m_vServers.size(), 1U);
	ASSERT_EQ(m_vServers[0].m_vClients.size(), 1U);
	EXPECT_STREQ(m_vServers[0].m_vClients[0].m_aName, "Target");
	ASSERT_EQ(Step(100.0, 3), EFollowAction::CONNECT);
	EXPECT_EQ(m_Connect, "127.0.0.1:8303");
}

TEST_F(QmServerListFollow, ParsedSevenIpv6TargetKeepsProtocolAndPort)
{
	ASSERT_FALSE(Parse(R"(["tw-0.7+udp://[2001:db8::1]:8304"])"));
	ASSERT_EQ(m_vServers.size(), 1U);
	ASSERT_EQ(Step(100.0), EFollowAction::CONNECT);
	EXPECT_EQ(m_Connect, "tw-0.7+udp://[2001:db8::1]:8304");
	NETADDR Connected;
	ASSERT_EQ(net_addr_from_url(&Connected, m_Connect.c_str(), nullptr, 0), 0);
	EXPECT_EQ(Connected, m_vServers[0].m_aAddresses[0]);
	EXPECT_NE(Connected.type & NETTYPE_TW7, 0U);
}

TEST_F(QmServerListFollow, MultipleConnectionCandidatesKeepTheirOrder)
{
	ASSERT_FALSE(Parse(R"(["tw-0.7+udp://[2001:db8::1]:8303","tw-0.7+udp://127.0.0.1:8304"])"));
	ASSERT_EQ(m_vServers.size(), 1U);
	EXPECT_EQ(m_vServers[0].m_NumAddresses, 2);
	ASSERT_EQ(Step(100.0), EFollowAction::CONNECT);
	EXPECT_EQ(m_Connect, "tw-0.7+udp://[2001:db8::1]:8303,tw-0.7+udp://127.0.0.1:8304");
}

TEST_F(QmServerListFollow, DualStackListingDoesNotReconnectThroughTheOtherAddress)
{
	ASSERT_FALSE(Parse(R"(["tw-0.7+udp://[2001:db8::1]:8303","tw-0.7+udp://127.0.0.1:8304"])"));
	ASSERT_EQ(m_vServers.size(), 1U);
	ASSERT_EQ(m_vServers[0].m_NumAddresses, 2);
	NETADDR Current{};
	ASSERT_EQ(net_addr_from_str(&Current, "127.0.0.1:8304"), 0);
	EXPECT_EQ(Step(100.0, 0, std::span(&Current, 1)), EFollowAction::NONE);
	EXPECT_TRUE(m_Connect.empty());
}

TEST_F(QmServerListFollow, PreferredSixAddressIsAlsoTheFollowDestination)
{
	ASSERT_FALSE(Parse(R"(["tw-0.7+udp://127.0.0.1:8303","tw-0.6+udp://127.0.0.1:8304"])"));
	ASSERT_EQ(m_vServers.size(), 1U);
	EXPECT_EQ(m_vServers[0].m_NumAddresses, 1);
	ASSERT_EQ(Step(100.0), EFollowAction::CONNECT);
	EXPECT_EQ(m_Connect, "127.0.0.1:8304");
}

TEST_F(QmServerListFollow, SixAddressWithZeroPortKeepsUsableSevenDestination)
{
	ASSERT_FALSE(Parse(R"(["tw-0.6+udp://127.0.0.1:0","tw-0.7+udp://127.0.0.1:8304"])"));
	ASSERT_EQ(m_vServers.size(), 1U);
	EXPECT_EQ(m_vServers[0].m_NumAddresses, 1);
	ASSERT_EQ(Step(100.0), EFollowAction::CONNECT);
	EXPECT_EQ(m_Connect, "tw-0.7+udp://127.0.0.1:8304");
}

TEST_F(QmServerListFollow, RefreshedDestinationRestartsDelayWithoutBorrowingOldEntry)
{
	NETADDR Current{};
	ASSERT_EQ(net_addr_from_str(&Current, "127.0.0.3:8303"), 0);
	m_Follow.Start("Target", "Clan", &Current);
	const auto CurrentAddresses = std::span(&Current, 1);
	ASSERT_FALSE(Parse(R"(["tw-0.6+udp://127.0.0.1:8303"])"));
	ASSERT_EQ(m_vServers.size(), 1U);
	EXPECT_EQ(Step(100.0, 3, CurrentAddresses), EFollowAction::NOTIFY_PENDING_SWITCH);
	ASSERT_FALSE(Parse(R"(["tw-0.6+udp://127.0.0.2:8304"])"));
	ASSERT_EQ(m_vServers.size(), 1U);
	EXPECT_EQ(Step(102.0, 3, CurrentAddresses), EFollowAction::NOTIFY_PENDING_SWITCH);
	EXPECT_EQ(Step(104.0, 3, CurrentAddresses), EFollowAction::NONE);
	ASSERT_EQ(Step(105.0, 3, CurrentAddresses), EFollowAction::CONNECT);
	EXPECT_EQ(m_Connect, "127.0.0.2:8304");
}

TEST_F(QmServerListFollow, FullIpv6ListNeverPublishesAPartialConnectionCandidate)
{
	const char *pUrl = "tw-0.7+udp://[1111:2222:3333:4444:5555:6666:7777:8888]:65535";
	std::string Addresses = "[";
	for(int Index = 0; Index < MAX_SERVER_ADDRESSES; ++Index)
	{
		if(Index != 0)
			Addresses += ',';
		Addresses += std::string("\"") + pUrl + '"';
	}
	Addresses += ']';
	ASSERT_FALSE(Parse(Addresses));
	ASSERT_EQ(m_vServers.size(), 1U);
	EXPECT_EQ(m_vServers[0].m_NumAddresses, MAX_SERVER_ADDRESSES);
	ASSERT_EQ(Step(100.0), EFollowAction::CONNECT);
	EXPECT_NE(str_endswith(m_Connect.c_str(), pUrl), nullptr);
	EXPECT_LT(m_Connect.size(), sizeof(m_vServers[0].m_aAddress));
}
