#include <engine/shared/masterserver.h>
#include <engine/shared/network.h>

#include <test/support/serverbrowser_fixture.h>

namespace
{
	class ServerBrowserLanResponse : public ServerBrowserTests::CServerBrowserFixture
	{
	protected:
		CNetClient m_NetClient{};
		int m_Token = 0;
		int m_BasicToken = 0;

		void SetUp() override
		{
			CServerBrowserFixture::SetUp();
			NETADDR Bind{};
			ASSERT_EQ(net_addr_from_str(&Bind, "127.0.0.1"), 0);
			// 仅供真实 NetType 查询；绑定临时端口，不调用广播或收发。
			ASSERT_TRUE(m_NetClient.Open(Bind));
			ASSERT_EQ(m_NetClient.NetType(), NETTYPE_IPV4);
		}

		void TearDown() override
		{
			m_NetClient.Close();
			CServerBrowserFixture::TearDown();
		}

		void BeginLan()
		{
			CServerBrowserTestAccess::SetLan(m_Browser, &m_NetClient, time_get());
			NETADDR Broadcast = NETADDR_ZEROED;
			Broadcast.type = NETTYPE_IPV4 | NETTYPE_LINK_BROADCAST;
			m_Token = CServerBrowserTestAccess::GenerateToken(m_Browser, Broadcast);
			m_BasicToken = CServerBrowserTestAccess::BasicToken(m_Token);
		}

		CServerInfo Bridge()
		{
			auto Result = Server("Bridge", 8301, 2);
			Result.m_aAddresses[0].type |= NETTYPE_TW7;
			return Result;
		}

		void ExpectBridgeUnchanged(const CServerInfo &Old, CServerBrowser::CServerEntry *pEntry,
			const CServerInfo::CClient *pClients, uint64_t Revision)
		{
			auto Address6 = Old.m_aAddresses[0];
			Address6.type &= ~NETTYPE_TW7;
			EXPECT_EQ(m_Browser.NumServers(), 1);
			EXPECT_EQ(m_Browser.Find(Old.m_aAddresses[0]), pEntry);
			EXPECT_EQ(m_Browser.Find(Address6), nullptr);
			EXPECT_EQ(m_Browser.Get(0), &pEntry->m_Info);
			EXPECT_EQ(m_Browser.SortedGet(0), &pEntry->m_Info);
			EXPECT_EQ(pEntry->m_Info.m_aAddresses[0], Old.m_aAddresses[0]);
			EXPECT_STREQ(pEntry->m_Info.m_aName, "Bridge");
			EXPECT_STREQ(pEntry->m_Info.m_aAddress, "tw-0.7+udp://127.0.0.1:8301");
			ASSERT_EQ(pEntry->m_Info.m_vClients.size(), 2U);
			EXPECT_EQ(pEntry->m_Info.m_vClients.data(), pClients);
			EXPECT_EQ(m_Browser.FriendListRevision(), Revision);
			EXPECT_FALSE(CServerBrowserTestAccess::NeedsResort(m_Browser));
		}
	};
}

TEST_F(ServerBrowserLanResponse, WrongBasicTokenCannotReplaceExistingBridgeOrConsumeItsRequest)
{
	const auto Old = Bridge();
	Publish({Old});
	ASSERT_EQ(m_Browser.NumServers(), 1);
	auto *pEntry = m_Browser.Find(Old.m_aAddresses[0]);
	ASSERT_NE(pEntry, nullptr);
	m_Browser.QueueRequest(pEntry);
	pEntry->m_RequestTime = 17;
	pEntry->m_FriendStateValid = true;
	const auto *pClients = pEntry->m_Info.m_vClients.data();
	const uint64_t Revision = m_Browser.FriendListRevision();
	BeginLan();
	auto Reply = Server("Rejected", 8301);
	Reply.m_Type = SERVERINFO_VANILLA;
	m_Browser.OnServerInfoUpdate(Reply.m_aAddresses[0], m_BasicToken ^ 1, &Reply);
	ExpectBridgeUnchanged(Old, pEntry, pClients, Revision);
	EXPECT_TRUE(pEntry->m_FriendStateValid);
	EXPECT_EQ(pEntry->m_RequestTime, 17);
	EXPECT_EQ(CServerBrowserTestAccess::NumRequests(m_Browser), 1);
	EXPECT_EQ(CServerBrowserTestAccess::FirstRequest(m_Browser), pEntry);
	EXPECT_EQ(CServerBrowserTestAccess::LastRequest(m_Browser), pEntry);
}

TEST_F(ServerBrowserLanResponse, WrongExtendedExtraTokenCannotReplaceBridgeWithMatchingBasicToken)
{
	const auto Old = Bridge();
	Publish({Old});
	ASSERT_EQ(m_Browser.NumServers(), 1);
	auto *pEntry = m_Browser.Find(Old.m_aAddresses[0]);
	ASSERT_NE(pEntry, nullptr);
	const auto *pClients = pEntry->m_Info.m_vClients.data();
	const uint64_t Revision = m_Browser.FriendListRevision();
	BeginLan();
	auto Reply = Server("Rejected", 8301);
	Reply.m_Type = SERVERINFO_EXTENDED;
	m_Browser.OnServerInfoUpdate(Reply.m_aAddresses[0], m_Token ^ 0x100, &Reply);
	ExpectBridgeUnchanged(Old, pEntry, pClients, Revision);
}

TEST_F(ServerBrowserLanResponse, WrongTokenDoesNotAddUnknownServer)
{
	BeginLan();
	const uint64_t Revision = m_Browser.FriendListRevision();
	auto Reply = Server("Unknown", 8301);
	Reply.m_Type = SERVERINFO_VANILLA;
	m_Browser.OnServerInfoUpdate(Reply.m_aAddresses[0], m_BasicToken ^ 1, &Reply);
	EXPECT_EQ(m_Browser.NumServers(), 0);
	EXPECT_EQ(m_Browser.Find(Reply.m_aAddresses[0]), nullptr);
	EXPECT_EQ(m_Browser.FriendListRevision(), Revision);
	EXPECT_FALSE(CServerBrowserTestAccess::NeedsResort(m_Browser));
}

TEST_F(ServerBrowserLanResponse, WrongTokenKeepsExistingMatchingAddressInfoAndPendingRequest)
{
	const auto Old = Server("Existing", 8301);
	Publish({Old});
	ASSERT_EQ(m_Browser.NumServers(), 1);
	auto *pEntry = m_Browser.Find(Old.m_aAddresses[0]);
	ASSERT_NE(pEntry, nullptr);
	m_Browser.QueueRequest(pEntry);
	pEntry->m_RequestTime = 23;
	const uint64_t Revision = m_Browser.FriendListRevision();
	BeginLan();
	auto Reply = Server("Rejected", 8301, 3);
	Reply.m_Type = SERVERINFO_EXTENDED;
	m_Browser.OnServerInfoUpdate(Reply.m_aAddresses[0], m_Token ^ 1, &Reply);
	EXPECT_EQ(m_Browser.Find(Old.m_aAddresses[0]), pEntry);
	EXPECT_STREQ(pEntry->m_Info.m_aName, "Existing");
	EXPECT_EQ(pEntry->m_Info.m_vClients.size(), 1U);
	EXPECT_EQ(pEntry->m_RequestTime, 23);
	EXPECT_EQ(CServerBrowserTestAccess::FirstRequest(m_Browser), pEntry);
	EXPECT_EQ(CServerBrowserTestAccess::NumRequests(m_Browser), 1);
	EXPECT_EQ(m_Browser.FriendListRevision(), Revision);
	EXPECT_FALSE(CServerBrowserTestAccess::NeedsResort(m_Browser));
}

TEST_F(ServerBrowserLanResponse, ValidVanillaResponseReplacesBridgeAndRemovesOnlyOldProtocolIndex)
{
	const auto Old = Bridge();
	Publish({Old});
	ASSERT_EQ(m_Browser.NumServers(), 1);
	auto *pEntry = m_Browser.Find(Old.m_aAddresses[0]);
	ASSERT_NE(pEntry, nullptr);
	m_Browser.QueueRequest(pEntry);
	BeginLan();
	auto Reply = Server("Native", 8301);
	Reply.m_Type = SERVERINFO_VANILLA;
	m_Browser.OnServerInfoUpdate(Reply.m_aAddresses[0], m_BasicToken, &Reply);
	EXPECT_EQ(m_Browser.NumServers(), 1);
	EXPECT_EQ(m_Browser.Find(Old.m_aAddresses[0]), nullptr);
	EXPECT_EQ(m_Browser.Find(Reply.m_aAddresses[0]), pEntry);
	EXPECT_STREQ(pEntry->m_Info.m_aName, "Native");
	EXPECT_STREQ(pEntry->m_Info.m_aAddress, "127.0.0.1:8301");
	EXPECT_EQ(pEntry->m_Info.m_vClients.size(), 1U);
	EXPECT_EQ(CServerBrowserTestAccess::NumRequests(m_Browser), 0);
	EXPECT_TRUE(CServerBrowserTestAccess::NeedsResort(m_Browser));
}

TEST_F(ServerBrowserLanResponse, ValidExtendedResponseCanAddNewServer)
{
	BeginLan();
	auto Reply = Server("Extended", 8301, 2);
	Reply.m_Type = SERVERINFO_EXTENDED;
	m_Browser.OnServerInfoUpdate(Reply.m_aAddresses[0], m_Token, &Reply);
	ASSERT_EQ(m_Browser.NumServers(), 1);
	const auto *pEntry = m_Browser.Find(Reply.m_aAddresses[0]);
	ASSERT_NE(pEntry, nullptr);
	EXPECT_STREQ(pEntry->m_Info.m_aName, "Extended");
	EXPECT_EQ(pEntry->m_Info.m_Type, SERVERINFO_EXTENDED);
	EXPECT_EQ(pEntry->m_Info.m_vClients.size(), 2U);
}

TEST_F(ServerBrowserLanResponse, ValidSixupResponseKeepsExistingSixupEntryWithoutDuplicate)
{
	const auto Old = Bridge();
	Publish({Old});
	ASSERT_EQ(m_Browser.NumServers(), 1);
	auto *pEntry = m_Browser.Find(Old.m_aAddresses[0]);
	ASSERT_NE(pEntry, nullptr);
	BeginLan();
	auto Reply = Bridge();
	str_copy(Reply.m_aName, "Updated bridge");
	Reply.m_Type = SERVERINFO_VANILLA;
	m_Browser.OnServerInfoUpdate(Reply.m_aAddresses[0], m_BasicToken, &Reply);
	EXPECT_EQ(m_Browser.NumServers(), 1);
	EXPECT_EQ(m_Browser.Find(Old.m_aAddresses[0]), pEntry);
	EXPECT_STREQ(pEntry->m_Info.m_aName, "Updated bridge");
	EXPECT_EQ(pEntry->m_Info.m_aAddresses[0], Old.m_aAddresses[0]);
}

TEST_F(ServerBrowserLanResponse, ValidSixupResponseDoesNotReplacePreferredVanillaAddress)
{
	const auto Old = Server("Native", 8301);
	Publish({Old});
	ASSERT_EQ(m_Browser.NumServers(), 1);
	auto *pEntry = m_Browser.Find(Old.m_aAddresses[0]);
	ASSERT_NE(pEntry, nullptr);
	const uint64_t Revision = m_Browser.FriendListRevision();
	BeginLan();
	auto Reply = Bridge();
	Reply.m_Type = SERVERINFO_VANILLA;
	m_Browser.OnServerInfoUpdate(Reply.m_aAddresses[0], m_BasicToken, &Reply);
	EXPECT_EQ(m_Browser.NumServers(), 1);
	EXPECT_EQ(m_Browser.Find(Old.m_aAddresses[0]), pEntry);
	EXPECT_EQ(m_Browser.Find(Reply.m_aAddresses[0]), nullptr);
	EXPECT_STREQ(pEntry->m_Info.m_aName, "Native");
	EXPECT_EQ(m_Browser.FriendListRevision(), Revision);
	EXPECT_FALSE(CServerBrowserTestAccess::NeedsResort(m_Browser));
}

TEST_F(ServerBrowserLanResponse, CorrectResponseCanReplaceBridgeAfterRejectedResponseAndRepeatWithoutDuplicate)
{
	const auto Old = Bridge();
	Publish({Old});
	ASSERT_EQ(m_Browser.NumServers(), 1);
	auto *pEntry = m_Browser.Find(Old.m_aAddresses[0]);
	ASSERT_NE(pEntry, nullptr);
	BeginLan();
	auto Reply = Server("Native", 8301);
	Reply.m_Type = SERVERINFO_EXTENDED;
	m_Browser.OnServerInfoUpdate(Reply.m_aAddresses[0], m_Token ^ 0x100, &Reply);
	EXPECT_EQ(m_Browser.Find(Old.m_aAddresses[0]), pEntry);
	m_Browser.OnServerInfoUpdate(Reply.m_aAddresses[0], m_Token, &Reply);
	m_Browser.OnServerInfoUpdate(Reply.m_aAddresses[0], m_Token, &Reply);
	EXPECT_EQ(m_Browser.NumServers(), 1);
	EXPECT_EQ(m_Browser.Find(Old.m_aAddresses[0]), nullptr);
	EXPECT_EQ(m_Browser.Find(Reply.m_aAddresses[0]), pEntry);
	EXPECT_STREQ(pEntry->m_Info.m_aName, "Native");
}
