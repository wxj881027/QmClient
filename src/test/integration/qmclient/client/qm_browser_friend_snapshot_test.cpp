#include <engine/client/friends.h>
#include <engine/client/serverbrowser.h>
#include <engine/shared/config.h>

#include <game/client/components/qmclient/browser_friend_list.h>

#include <gtest/gtest.h>

#include <initializer_list>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace
{
	class CMemorySnapshotBrowser : public CServerBrowser
	{
		std::vector<CServerInfo> m_vServers;
		uint64_t m_Revision = 0;

	public:
		void SetServers(std::vector<CServerInfo> Servers)
		{
			m_vServers = std::move(Servers);
			++m_Revision;
		}
		int NumServers() const override { return static_cast<int>(m_vServers.size()); }
		const CServerInfo *Get(int Index) const override { return &m_vServers.at(Index); }
		uint64_t FriendListRevision() const override { return m_Revision; }
	};

	class BrowserFriendSnapshot : public ::testing::Test
	{
		int m_PreviousIgnoreClan = g_Config.m_ClFriendsIgnoreClan;

	protected:
		std::unique_ptr<CFriends> m_pFriends = std::make_unique<CFriends>();
		CMemorySnapshotBrowser m_Browser;
		CQmBrowserFriendList m_List;

		void SetUp() override
		{
			g_Config.m_ClFriendsIgnoreClan = 0;
			m_pFriends->AddFriend("Alice", "Clan");
			m_pFriends->AddFriend("Bob", "Clan");
		}
		void TearDown() override { g_Config.m_ClFriendsIgnoreClan = m_PreviousIgnoreClan; }
		CServerInfo Server(const char *pName, std::initializer_list<const char *> Names) const
		{
			CServerInfo Info{};
			str_copy(Info.m_aName, pName);
			for(const char *pClientName : Names)
			{
				CServerInfo::CClient Client{};
				str_copy(Client.m_aName, pClientName);
				str_copy(Client.m_aClan, "Clan");
				Client.m_FriendState = m_pFriends->GetFriendState(pClientName, "Clan");
				Info.m_FriendState = maximum(Info.m_FriendState, Client.m_FriendState);
				Info.m_vClients.push_back(Client);
			}
			return Info;
		}
		const std::vector<CQmBrowserFriendList::CItem> &Online() const
		{
			return m_List.Groups().at(m_pFriends->FindCategory(IFriends::DEFAULT_CATEGORY));
		}
	};
}

TEST_F(BrowserFriendSnapshot, SeveralPlayersInOneServerShareOneOwnedSnapshot)
{
	m_Browser.SetServers({Server("Shared", {"Alice", "Bob"})});
	int Copies = 0;
	ASSERT_TRUE(m_List.Update(*m_pFriends, m_Browser, false, [&](const CServerInfo &Info) {
		++Copies;
		return std::make_shared<CServerInfo>(Info);
	}));
	ASSERT_EQ(Online().size(), 2U);
	EXPECT_EQ(Copies, 1);
	EXPECT_EQ(Online()[0].ServerInfo(), Online()[1].ServerInfo());
	const CServerInfo *pSnapshot = Online()[0].ServerInfo();
	ASSERT_NE(pSnapshot, nullptr);
	m_Browser.SetServers({});
	EXPECT_STREQ(pSnapshot->m_aName, "Shared");
	EXPECT_EQ(pSnapshot->m_vClients.size(), 2U);
}

TEST_F(BrowserFriendSnapshot, FailedSecondCopyKeepsTheEntirePreviousView)
{
	m_Browser.SetServers({Server("Previous", {"Alice"})});
	ASSERT_TRUE(m_List.Update(*m_pFriends, m_Browser, false));
	ASSERT_EQ(Online().size(), 1U);
	const CServerInfo *pPrevious = Online()[0].ServerInfo();
	const auto *pGroups = m_List.Groups().data();
	m_Browser.SetServers({Server("New one", {"Alice"}), Server("New two", {"Bob"})});
	int Copies = 0;
	auto pDeleted = std::make_shared<int>(0);
	EXPECT_FALSE(m_List.Update(*m_pFriends, m_Browser, false, [&](const CServerInfo &Info) {
		if(++Copies == 2)
			throw std::bad_alloc();
		return std::shared_ptr<CServerInfo>(new CServerInfo(Info), [pDeleted](CServerInfo *pInfo) {
			++*pDeleted;
			delete pInfo;
		});
	}));
	EXPECT_EQ(Copies, 2);
	EXPECT_EQ(*pDeleted, 1);
	EXPECT_EQ(m_List.Groups().data(), pGroups);
	ASSERT_EQ(Online().size(), 1U);
	EXPECT_EQ(Online()[0].ServerInfo(), pPrevious);
	EXPECT_STREQ(Online()[0].ServerInfo()->m_aName, "Previous");
}

TEST_F(BrowserFriendSnapshot, RetryWithTheSameSourceRevisionPublishesTheWholeUpdate)
{
	m_Browser.SetServers({Server("Previous", {"Alice"})});
	ASSERT_TRUE(m_List.Update(*m_pFriends, m_Browser, false));
	m_Browser.SetServers({Server("Updated", {"Alice", "Bob"})});
	EXPECT_FALSE(m_List.Update(*m_pFriends, m_Browser, false, [](const CServerInfo &) -> std::shared_ptr<const CServerInfo> { throw std::bad_alloc(); }));
	ASSERT_TRUE(m_List.Update(*m_pFriends, m_Browser, false));
	ASSERT_EQ(Online().size(), 2U);
	EXPECT_STREQ(Online()[0].ServerInfo()->m_aName, "Updated");
	EXPECT_STREQ(Online()[1].ServerInfo()->m_aName, "Updated");
}

TEST_F(BrowserFriendSnapshot, EmptySnapshotDoesNotReplaceThePreviousView)
{
	m_Browser.SetServers({Server("Previous", {"Alice"})});
	ASSERT_TRUE(m_List.Update(*m_pFriends, m_Browser, false));
	m_Browser.SetServers({Server("Updated", {"Alice", "Bob"})});
	EXPECT_FALSE(m_List.Update(*m_pFriends, m_Browser, false, [](const CServerInfo &) { return std::shared_ptr<const CServerInfo>(); }));
	ASSERT_EQ(Online().size(), 1U);
	EXPECT_STREQ(Online()[0].ServerInfo()->m_aName, "Previous");
	ASSERT_TRUE(m_List.Update(*m_pFriends, m_Browser, false));
	EXPECT_EQ(Online().size(), 2U);
}

TEST_F(BrowserFriendSnapshot, CacheHitAndUnmatchedServersDoNotRequestSnapshots)
{
	m_Browser.SetServers({Server("No friends", {"Charlie"})});
	int Copies = 0;
	const auto Copy = [&](const CServerInfo &Info) {
		++Copies;
		return std::make_shared<CServerInfo>(Info);
	};
	ASSERT_TRUE(m_List.Update(*m_pFriends, m_Browser, false, Copy));
	EXPECT_EQ(Copies, 0);
	m_Browser.SetServers({Server("Matched", {"Alice"})});
	ASSERT_TRUE(m_List.Update(*m_pFriends, m_Browser, false, Copy));
	ASSERT_TRUE(m_List.Update(*m_pFriends, m_Browser, false, Copy));
	EXPECT_EQ(Copies, 1);
}

TEST_F(BrowserFriendSnapshot, UnknownCopyExceptionLeavesThePreviousSnapshotUsable)
{
	m_Browser.SetServers({Server("Previous", {"Alice"})});
	ASSERT_TRUE(m_List.Update(*m_pFriends, m_Browser, false));
	m_Browser.SetServers({Server("Updated", {"Bob"})});
	EXPECT_FALSE(m_List.Update(*m_pFriends, m_Browser, false, [](const CServerInfo &) -> std::shared_ptr<const CServerInfo> { throw 7; }));
	ASSERT_EQ(Online().size(), 1U);
	EXPECT_STREQ(Online()[0].Name(), "Alice");
	EXPECT_STREQ(Online()[0].ServerInfo()->m_aName, "Previous");
}

TEST_F(BrowserFriendSnapshot, ClearingTheViewReleasesOneSharedSnapshotAfterItsLastItem)
{
	auto pDeleted = std::make_shared<int>(0);
	m_Browser.SetServers({Server("Shared", {"Alice", "Bob"})});
	ASSERT_TRUE(m_List.Update(*m_pFriends, m_Browser, false, [pDeleted](const CServerInfo &Info) {
		return std::shared_ptr<const CServerInfo>(new CServerInfo(Info), [pDeleted](const CServerInfo *pInfo) {
			++*pDeleted;
			delete pInfo;
		});
	}));
	EXPECT_EQ(*pDeleted, 0);
	m_Browser.SetServers({});
	ASSERT_TRUE(m_List.Update(*m_pFriends, m_Browser, false));
	EXPECT_EQ(*pDeleted, 1);
	EXPECT_TRUE(Online().empty());
}

TEST_F(BrowserFriendSnapshot, CategoryChangeIsPublishedOnlyWithTheCompleteSnapshot)
{
	m_Browser.SetServers({Server("Previous", {"Alice"})});
	ASSERT_TRUE(m_List.Update(*m_pFriends, m_Browser, false));
	const size_t PreviousCategories = m_List.Groups().size();
	ASSERT_TRUE(m_pFriends->AddCategory("Team"));
	ASSERT_TRUE(m_pFriends->SetFriendCategory("Alice", "Clan", "Team"));
	EXPECT_FALSE(m_List.Update(*m_pFriends, m_Browser, false, [](const CServerInfo &) -> std::shared_ptr<const CServerInfo> { throw std::bad_alloc(); }));
	EXPECT_EQ(m_List.Groups().size(), PreviousCategories);
	ASSERT_TRUE(m_List.Update(*m_pFriends, m_Browser, false));
	EXPECT_EQ(m_List.Groups().size(), PreviousCategories + 1);
	const auto &Team = m_List.Groups().at(m_pFriends->FindCategory("Team"));
	ASSERT_EQ(Team.size(), 1U);
	EXPECT_STREQ(Team[0].Name(), "Alice");
}
