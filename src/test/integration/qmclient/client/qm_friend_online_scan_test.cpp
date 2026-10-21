#include <engine/client/friends.h>
#include <engine/shared/config.h>

#include <game/client/components/qmclient/friend_online_notify.h>
#include <game/client/components/qmclient/friend_online_scan.h>

#include <gtest/gtest.h>
#include <test/support/friend_online_tracker_test_support.h>
#include <test/support/serverbrowser_fixture.h>

#include <memory>
#include <utility>
#include <vector>

namespace
{
	class CCountingHttpList : public ServerBrowserTests::CPublishedList
	{
	public:
		mutable std::vector<int> m_vReads;
		bool m_UnknownFailure = false;
		const CServerInfo &Server(int Index) const override
		{
			m_vReads.push_back(Index);
			if(m_UnknownFailure && Index == m_FailAt)
				throw 7;
			return CPublishedList::Server(Index);
		}
	};

	class QmFriendOnlineScan : public ::testing::Test
	{
	protected:
		int m_SavedIgnoreClan = g_Config.m_ClFriendsIgnoreClan;
		std::unique_ptr<CFriends> m_pFriends = std::make_unique<CFriends>();
		CServerBrowser m_Browser;
		CCountingHttpList *m_pHttp = nullptr;
		qm_friend_notify::COnlineScanner m_Scanner;

		void SetUp() override
		{
			g_Config.m_ClFriendsIgnoreClan = 0;
			auto pHttp = std::make_unique<CCountingHttpList>();
			m_pHttp = pHttp.get();
			// 仅替换传输来源；浏览器接管所有权，好友、扫描和通知状态均使用生产实现。
			CServerBrowserTestAccess::SetHttp(m_Browser, pHttp.release());
		}
		void TearDown() override { g_Config.m_ClFriendsIgnoreClan = m_SavedIgnoreClan; }

		static std::vector<CServerInfo> Servers(int Count)
		{
			std::vector<CServerInfo> vServers(Count);
			for(int Index = 0; Index < Count; ++Index)
			{
				auto &Server = vServers[Index];
				Server.m_NumAddresses = 1;
				Server.m_aAddresses[0] = {};
				Server.m_aAddresses[0].type = NETTYPE_IPV4;
				Server.m_aAddresses[0].ip[0] = 127;
				Server.m_aAddresses[0].ip[3] = 1;
				// 地址只用于内存中的身份比较，不打开套接字。
				Server.m_aAddresses[0].port = 8300 + Index;
				Server.m_NumClients = 0;
				str_copy(Server.m_aMap, "Map");
			}
			return vServers;
		}
		static void AddClient(CServerInfo &Server, const char *pName, const char *pClan = "Clan")
		{
			CServerInfo::CClient Client{};
			str_copy(Client.m_aName, pName);
			str_copy(Client.m_aClan, pClan);
			Server.m_vClients.push_back(Client);
			Server.m_NumClients = static_cast<int>(Server.m_vClients.size());
		}
		void Publish(std::vector<CServerInfo> vServers)
		{
			m_pHttp->m_vServers = std::move(vServers);
			++m_pHttp->m_Revision;
			m_pHttp->m_vReads.clear();
		}
		const qm_friend_notify::COnlineScanner::CSnapshot *Step()
		{
			return m_Scanner.Step(*m_pFriends, m_Browser, g_Config.m_ClFriendsIgnoreClan != 0);
		}
	};
}

TEST_F(QmFriendOnlineScan, PublishesOnlyAfterAllServersWithThirtyTwoReadsPerStep)
{
	m_pFriends->AddFriend("Alice", "Clan");
	m_pFriends->AddFriend("Bob", "Clan");
	auto vServers = Servers(33);
	AddClient(vServers[0], "Alice");
	AddClient(vServers[32], "Bob");
	Publish(std::move(vServers));
	EXPECT_EQ(m_Browser.HttpListRevision(), m_pHttp->m_Revision);
	EXPECT_EQ(Step(), nullptr);
	EXPECT_TRUE(m_Scanner.Running());
	ASSERT_EQ(m_pHttp->m_vReads.size(), 32U);
	EXPECT_EQ(m_pHttp->m_vReads.back(), 31);
	const auto *pSnapshot = Step();
	ASSERT_NE(pSnapshot, nullptr);
	EXPECT_FALSE(m_Scanner.Running());
	EXPECT_EQ(m_pHttp->m_vReads.size(), 33U);
	ASSERT_EQ(pSnapshot->m_vFriends.size(), 2U);
	EXPECT_EQ(pSnapshot->m_vFriends[0].m_Key, "Alice\tClan");
	EXPECT_EQ(pSnapshot->m_vFriends[1].m_Name, "Bob");
	EXPECT_EQ(pSnapshot->m_vFriends[1].m_Map, "Map");
	EXPECT_EQ(pSnapshot->m_vFriends[1].m_Server, "127.0.0.1:8332");
	EXPECT_TRUE(pSnapshot->m_vFriends[1].m_IsFriend);
	EXPECT_EQ(pSnapshot->m_AvailableServers.size(), 33U);
}

TEST_F(QmFriendOnlineScan, SameSizeReplacementDiscardsOldPrefixAndRestartsAtZero)
{
	m_pFriends->AddFriend("Alice", "Clan");
	m_pFriends->AddFriend("Bob", "Clan");
	m_pFriends->AddFriend("Carol", "Clan");
	auto vOld = Servers(33);
	AddClient(vOld[0], "Alice");
	Publish(std::move(vOld));
	ASSERT_EQ(Step(), nullptr);
	auto vNew = Servers(33);
	AddClient(vNew[0], "Bob");
	AddClient(vNew[32], "Carol");
	str_copy(vNew[0].m_aMap, "Replacement");
	Publish(std::move(vNew));
	ASSERT_EQ(Step(), nullptr);
	ASSERT_EQ(m_pHttp->m_vReads.size(), 32U);
	EXPECT_EQ(m_pHttp->m_vReads.front(), 0);
	const auto *pSnapshot = Step();
	ASSERT_NE(pSnapshot, nullptr);
	ASSERT_EQ(pSnapshot->m_vFriends.size(), 2U);
	EXPECT_EQ(pSnapshot->m_vFriends[0].m_Name, "Bob");
	EXPECT_EQ(pSnapshot->m_vFriends[0].m_Map, "Replacement");
	EXPECT_EQ(pSnapshot->m_vFriends[1].m_Name, "Carol");
}

TEST_F(QmFriendOnlineScan, ShorterReplacementCompletesWithoutKeepingOldCandidate)
{
	m_pFriends->AddFriend("Alice", "Clan");
	m_pFriends->AddFriend("Bob", "Clan");
	auto vOld = Servers(65);
	AddClient(vOld[0], "Alice");
	Publish(std::move(vOld));
	ASSERT_EQ(Step(), nullptr);
	auto vNew = Servers(1);
	AddClient(vNew[0], "Bob");
	Publish(std::move(vNew));
	const auto *pSnapshot = Step();
	ASSERT_NE(pSnapshot, nullptr);
	EXPECT_EQ(m_pHttp->m_vReads, (std::vector<int>{0}));
	ASSERT_EQ(pSnapshot->m_vFriends.size(), 1U);
	EXPECT_EQ(pSnapshot->m_vFriends[0].m_Name, "Bob");
	EXPECT_EQ(pSnapshot->m_AvailableServers.size(), 1U);
}

TEST_F(QmFriendOnlineScan, ReplacingPartialScanPreservesCompletedNotificationBaseline)
{
	qm_friend_notify::COnlineTracker Tracker;
	m_pFriends->AddFriend("Alice", "Clan");
	m_pFriends->AddFriend("Bob", "Clan");
	Publish(Servers(1));
	const auto *pBaseline = Step();
	ASSERT_NE(pBaseline, nullptr);
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, pBaseline->m_vFriends, pBaseline->m_AvailableServers).empty());
	auto vOld = Servers(33);
	AddClient(vOld[0], "Alice");
	Publish(std::move(vOld));
	ASSERT_EQ(Step(), nullptr);
	auto vNew = Servers(1);
	AddClient(vNew[0], "Bob");
	Publish(std::move(vNew));
	const auto *pSnapshot = Step();
	ASSERT_NE(pSnapshot, nullptr);
	const auto vNotifications = QmFriendTests::UpdateOnline(Tracker, pSnapshot->m_vFriends, pSnapshot->m_AvailableServers);
	ASSERT_EQ(vNotifications.size(), 1U);
	EXPECT_EQ(vNotifications[0].m_Name, "Bob");
	m_Scanner.Reset();
	pSnapshot = Step();
	ASSERT_NE(pSnapshot, nullptr);
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, pSnapshot->m_vFriends, pSnapshot->m_AvailableServers).empty());
}

TEST_F(QmFriendOnlineScan, FriendRevisionRebuildsNamedFilterBeforeContinuing)
{
	m_pFriends->AddFriend("Alice", "Clan");
	auto vServers = Servers(33);
	AddClient(vServers[0], "Alice");
	AddClient(vServers[1], "Bob");
	Publish(std::move(vServers));
	ASSERT_EQ(Step(), nullptr);
	m_pFriends->AddFriend("Bob", "Clan");
	m_pHttp->m_vReads.clear();
	ASSERT_EQ(Step(), nullptr);
	ASSERT_EQ(m_pHttp->m_vReads.size(), 32U);
	EXPECT_EQ(m_pHttp->m_vReads.front(), 0);
	const auto *pSnapshot = Step();
	ASSERT_NE(pSnapshot, nullptr);
	ASSERT_EQ(pSnapshot->m_vFriends.size(), 2U);
	EXPECT_EQ(pSnapshot->m_vFriends[0].m_Name, "Alice");
	EXPECT_EQ(pSnapshot->m_vFriends[1].m_Name, "Bob");
	EXPECT_TRUE(pSnapshot->m_vFriends[1].m_IsFriend);
}

TEST_F(QmFriendOnlineScan, IgnoreClanChangeRestartsAndRebuildsIdentityKeys)
{
	m_pFriends->AddFriend("Alice", "Clan");
	auto vServers = Servers(33);
	AddClient(vServers[0], "Alice", "Other");
	Publish(std::move(vServers));
	ASSERT_EQ(Step(), nullptr);
	g_Config.m_ClFriendsIgnoreClan = 1;
	m_pHttp->m_vReads.clear();
	ASSERT_EQ(Step(), nullptr);
	ASSERT_EQ(m_pHttp->m_vReads.size(), 32U);
	EXPECT_EQ(m_pHttp->m_vReads.front(), 0);
	const auto *pSnapshot = Step();
	ASSERT_NE(pSnapshot, nullptr);
	ASSERT_EQ(pSnapshot->m_vFriends.size(), 1U);
	EXPECT_EQ(pSnapshot->m_vFriends[0].m_Key, "Alice");
	EXPECT_TRUE(pSnapshot->m_vFriends[0].m_IsFriend);
}

TEST_F(QmFriendOnlineScan, ResetCancelsIncompleteCandidateAndNextScanStartsOver)
{
	m_pFriends->AddFriend("Alice", "Clan");
	auto vServers = Servers(33);
	AddClient(vServers[0], "Alice");
	Publish(std::move(vServers));
	ASSERT_EQ(Step(), nullptr);
	m_Scanner.Reset();
	EXPECT_FALSE(m_Scanner.Running());
	m_pHttp->m_vReads.clear();
	ASSERT_EQ(Step(), nullptr);
	ASSERT_EQ(m_pHttp->m_vReads.size(), 32U);
	EXPECT_EQ(m_pHttp->m_vReads.front(), 0);
	const auto *pSnapshot = Step();
	ASSERT_NE(pSnapshot, nullptr);
	EXPECT_EQ(pSnapshot->m_vFriends.size(), 1U);
}

TEST_F(QmFriendOnlineScan, EmptySourceProducesNoObservationOrOfflineEvidence)
{
	qm_friend_notify::COnlineTracker Tracker;
	m_pFriends->AddFriend("Alice", "Clan");
	auto vServers = Servers(1);
	AddClient(vServers[0], "Alice");
	Publish(vServers);
	const auto *pSnapshot = Step();
	ASSERT_NE(pSnapshot, nullptr);
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, pSnapshot->m_vFriends, pSnapshot->m_AvailableServers).empty());
	Publish({});
	EXPECT_EQ(Step(), nullptr);
	EXPECT_FALSE(m_Scanner.Running());
	EXPECT_EQ(Step(), nullptr);
	Publish(std::move(vServers));
	pSnapshot = Step();
	ASSERT_NE(pSnapshot, nullptr);
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, pSnapshot->m_vFriends, pSnapshot->m_AvailableServers).empty());
}

TEST_F(QmFriendOnlineScan, ReadAllocationFailureDiscardsPartialAndSameRevisionCanRetry)
{
	m_pFriends->AddFriend("Alice", "Clan");
	m_pFriends->AddFriend("Bob", "Clan");
	auto vServers = Servers(33);
	AddClient(vServers[0], "Alice");
	AddClient(vServers[32], "Bob");
	Publish(std::move(vServers));
	ASSERT_EQ(Step(), nullptr);
	m_pHttp->m_FailAt = 32;
	EXPECT_EQ(Step(), nullptr);
	EXPECT_FALSE(m_Scanner.Running());
	const uint64_t Revision = m_Browser.HttpListRevision();
	m_pHttp->m_FailAt = -1;
	m_pHttp->m_vReads.clear();
	ASSERT_EQ(Step(), nullptr);
	EXPECT_EQ(m_Browser.HttpListRevision(), Revision);
	ASSERT_EQ(m_pHttp->m_vReads.size(), 32U);
	EXPECT_EQ(m_pHttp->m_vReads.front(), 0);
	const auto *pSnapshot = Step();
	ASSERT_NE(pSnapshot, nullptr);
	ASSERT_EQ(pSnapshot->m_vFriends.size(), 2U);
	EXPECT_EQ(pSnapshot->m_vFriends[0].m_Name, "Alice");
	EXPECT_EQ(pSnapshot->m_vFriends[1].m_Name, "Bob");
}

TEST_F(QmFriendOnlineScan, UnknownReadFailurePublishesNothingAndAllowsRetry)
{
	m_pFriends->AddFriend("Alice", "Clan");
	auto vServers = Servers(1);
	AddClient(vServers[0], "Alice");
	Publish(std::move(vServers));
	m_pHttp->m_UnknownFailure = true;
	m_pHttp->m_FailAt = 0;
	EXPECT_EQ(Step(), nullptr);
	EXPECT_FALSE(m_Scanner.Running());
	m_pHttp->m_FailAt = -1;
	const auto *pSnapshot = Step();
	ASSERT_NE(pSnapshot, nullptr);
	ASSERT_EQ(pSnapshot->m_vFriends.size(), 1U);
	EXPECT_EQ(pSnapshot->m_vFriends[0].m_Name, "Alice");
}

TEST_F(QmFriendOnlineScan, PreservesIncompletePresenceAndExcludesClanOnlyMatches)
{
	m_pFriends->AddFriend("Alice", "Clan");
	m_pFriends->AddFriend("", "Clan");
	auto vServers = Servers(4);
	AddClient(vServers[0], "Alice", "Other");
	vServers[0].m_NumClients = 2;
	AddClient(vServers[1], "ClanOnly");
	AddClient(vServers[2], "Alice");
	vServers[2].m_NumAddresses = 0;
	Publish(std::move(vServers));
	const auto *pSnapshot = Step();
	ASSERT_NE(pSnapshot, nullptr);
	ASSERT_EQ(pSnapshot->m_vFriends.size(), 1U);
	EXPECT_EQ(pSnapshot->m_vFriends[0].m_Key, "Alice\tOther");
	EXPECT_FALSE(pSnapshot->m_vFriends[0].m_IsFriend);
	EXPECT_EQ(pSnapshot->m_AvailableServers.count("127.0.0.1:8300"), 0U);
	EXPECT_EQ(pSnapshot->m_AvailableServers.count("127.0.0.1:8301"), 1U);
	EXPECT_EQ(pSnapshot->m_AvailableServers.count("127.0.0.1:8302"), 0U);
	EXPECT_EQ(pSnapshot->m_AvailableServers.count("127.0.0.1:8303"), 1U);
}

TEST_F(QmFriendOnlineScan, CompletedSnapshotOwnsTextAfterRawSourceReplacement)
{
	m_pFriends->AddFriend("Alice", "Clan");
	auto vServers = Servers(1);
	AddClient(vServers[0], "Alice");
	str_copy(vServers[0].m_aMap, "Original");
	Publish(std::move(vServers));
	const auto *pSnapshot = Step();
	ASSERT_NE(pSnapshot, nullptr);
	Publish(Servers(1));
	ASSERT_EQ(pSnapshot->m_vFriends.size(), 1U);
	EXPECT_EQ(pSnapshot->m_vFriends[0].m_Name, "Alice");
	EXPECT_EQ(pSnapshot->m_vFriends[0].m_Map, "Original");
	EXPECT_EQ(pSnapshot->m_vFriends[0].m_Server, "127.0.0.1:8300");
}

TEST_F(QmFriendOnlineScan, DisplayListRevisionDoesNotRestartRawHttpScan)
{
	m_pFriends->AddFriend("Alice", "Clan");
	auto vServers = Servers(33);
	AddClient(vServers[32], "Alice");
	Publish(std::move(vServers));
	ASSERT_EQ(Step(), nullptr);
	const uint64_t DisplayRevision = m_Browser.FriendListRevision();
	const uint64_t HttpRevision = m_Browser.HttpListRevision();
	CServerBrowserTestAccess::CleanUp(m_Browser);
	EXPECT_NE(m_Browser.FriendListRevision(), DisplayRevision);
	EXPECT_EQ(m_Browser.HttpListRevision(), HttpRevision);
	m_pHttp->m_vReads.clear();
	const auto *pSnapshot = Step();
	ASSERT_NE(pSnapshot, nullptr);
	EXPECT_EQ(m_pHttp->m_vReads, (std::vector<int>{32}));
	ASSERT_EQ(pSnapshot->m_vFriends.size(), 1U);
	EXPECT_EQ(pSnapshot->m_vFriends[0].m_Name, "Alice");
}

TEST_F(QmFriendOnlineScan, CompleteSnapshotCanRetryNotificationPreparationWithoutSourceChange)
{
	qm_friend_notify::COnlineNotifier Notifier;
	std::vector<std::string> vNames;
	const auto Notify = [&](const auto &Friend) { vNames.push_back(Friend.m_Name); };
	m_pFriends->AddFriend("Alice", "Clan");
	m_pFriends->AddFriend("Bob", "Clan");
	Publish(Servers(1));
	const auto *pSnapshot = Step();
	ASSERT_NE(pSnapshot, nullptr);
	ASSERT_TRUE(Notifier.Update(pSnapshot->m_vFriends, pSnapshot->m_AvailableServers, 0.0f, Notify));
	auto vServers = Servers(1);
	AddClient(vServers[0], "Alice");
	AddClient(vServers[0], "Bob");
	Publish(std::move(vServers));
	pSnapshot = Step();
	ASSERT_NE(pSnapshot, nullptr);
	QmFriendTests::CFailingFriendCopy Copy{2};
	EXPECT_FALSE(Notifier.Update(pSnapshot->m_vFriends, pSnapshot->m_AvailableServers, 1.0f, Notify, Copy));
	EXPECT_TRUE(vNames.empty());
	const uint64_t Revision = m_Browser.HttpListRevision();
	m_Scanner.Reset();
	pSnapshot = Step();
	ASSERT_NE(pSnapshot, nullptr);
	EXPECT_EQ(m_Browser.HttpListRevision(), Revision);
	ASSERT_TRUE(Notifier.Update(pSnapshot->m_vFriends, pSnapshot->m_AvailableServers, 2.0f, Notify));
	EXPECT_EQ(vNames, (std::vector<std::string>{"Alice", "Bob"}));
}
