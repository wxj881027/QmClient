#include <engine/client/serverbrowser_http_parse.h>

#include <game/client/components/pie_menu_logic.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace
{
	class QmServerListFollow : public ::testing::Test
	{
	protected:
		std::vector<CServerInfo> m_vServers;
		qm_pie_menu::SFollowState m_Follow;
		std::string m_Connect;

		void SetUp() override { qm_pie_menu::StartFollow(m_Follow, "Target", "Clan"); }
		bool Parse(const std::string &Addresses)
		{
			const std::string Body = R"({"servers":[{"addresses":)" + Addresses +
						 R"(,"location":"eu","info":{"max_clients":16,"max_players":16,"passworded":false,"game_type":"DDRace","name":"Server","map":{"name":"Map"},"version":"0.6","clients":[{"name":"Target","clan":"Clan","country":0,"score":-1,"is_player":true}]}}]})";
			std::unique_ptr<json_value, decltype(&json_value_free)> pJson(JsonParse(Body.data(), Body.size()), json_value_free);
			return ServerBrowserParseHttpList(pJson.get(), &m_vServers);
		}
		bool Step(double Now, int Delay = 0)
		{
			// 把原始 HTTP 条目的连接字符串交给生产跟随状态，不手动补写地址。
			const char *pAddress = m_vServers.at(0).m_aAddress;
			return qm_pie_menu::FollowStep(m_Follow, pAddress[0] != '\0', pAddress, "", false, Now, Delay, m_Connect);
		}
	};
}

TEST_F(QmServerListFollow, ParsedIpv4TargetConnectsAfterConfiguredDelay)
{
	ASSERT_FALSE(Parse(R"(["tw-0.6+udp://127.0.0.1:8303"])"));
	ASSERT_EQ(m_vServers.size(), 1U);
	ASSERT_EQ(m_vServers[0].m_vClients.size(), 1U);
	EXPECT_STREQ(m_vServers[0].m_vClients[0].m_aName, "Target");
	EXPECT_FALSE(Step(100.0, 3));
	EXPECT_FALSE(Step(102.0, 3));
	ASSERT_TRUE(Step(103.0, 3));
	EXPECT_EQ(m_Connect, "127.0.0.1:8303");
}

TEST_F(QmServerListFollow, ParsedSevenIpv6TargetKeepsProtocolAndPort)
{
	ASSERT_FALSE(Parse(R"(["tw-0.7+udp://[2001:db8::1]:8304"])"));
	ASSERT_EQ(m_vServers.size(), 1U);
	ASSERT_TRUE(Step(100.0));
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
	ASSERT_TRUE(Step(100.0));
	EXPECT_EQ(m_Connect, "tw-0.7+udp://[2001:db8::1]:8303,tw-0.7+udp://127.0.0.1:8304");
}

TEST_F(QmServerListFollow, PreferredSixAddressIsAlsoTheFollowDestination)
{
	ASSERT_FALSE(Parse(R"(["tw-0.7+udp://127.0.0.1:8303","tw-0.6+udp://127.0.0.1:8304"])"));
	ASSERT_EQ(m_vServers.size(), 1U);
	EXPECT_EQ(m_vServers[0].m_NumAddresses, 1);
	ASSERT_TRUE(Step(100.0));
	EXPECT_EQ(m_Connect, "127.0.0.1:8304");
}

TEST_F(QmServerListFollow, RefreshedDestinationRestartsDelayWithoutBorrowingOldEntry)
{
	ASSERT_FALSE(Parse(R"(["tw-0.6+udp://127.0.0.1:8303"])"));
	ASSERT_EQ(m_vServers.size(), 1U);
	EXPECT_FALSE(Step(100.0, 3));
	ASSERT_FALSE(Parse(R"(["tw-0.6+udp://127.0.0.2:8304"])"));
	ASSERT_EQ(m_vServers.size(), 1U);
	EXPECT_FALSE(Step(102.0, 3));
	EXPECT_FALSE(Step(104.0, 3));
	ASSERT_TRUE(Step(105.0, 3));
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
	ASSERT_TRUE(Step(100.0));
	EXPECT_NE(str_endswith(m_Connect.c_str(), pUrl), nullptr);
	EXPECT_LT(m_Connect.size(), sizeof(m_vServers[0].m_aAddress));
}
