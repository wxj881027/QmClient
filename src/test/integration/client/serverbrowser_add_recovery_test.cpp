#include <test/support/serverbrowser_fixture.h>

namespace
{
	class CFailureFavorites : public IFavorites
	{
		IFavorites &m_Delegate;
		void OnConfigSave(IConfigManager *) override {}

	public:
		bool m_FailFavorite = false;
		bool m_FailPing = false;
		explicit CFailureFavorites(IFavorites &Delegate) :
			m_Delegate(Delegate) {}
		TRISTATE IsFavorite(const NETADDR *pAddresses, int Count) const override
		{
			if(m_FailFavorite)
				throw std::bad_alloc();
			return m_Delegate.IsFavorite(pAddresses, Count);
		}
		TRISTATE IsPingAllowed(const NETADDR *pAddresses, int Count) const override
		{
			if(m_FailPing)
				throw 7;
			return m_Delegate.IsPingAllowed(pAddresses, Count);
		}
		void Add(const NETADDR *pAddresses, int Count) override { m_Delegate.Add(pAddresses, Count); }
		void AllowPing(const NETADDR *pAddresses, int Count, bool Allow) override { m_Delegate.AllowPing(pAddresses, Count, Allow); }
		void Remove(const NETADDR *pAddresses, int Count) override { m_Delegate.Remove(pAddresses, Count); }
		void AllEntries(const CEntry **ppEntries, int *pCount) override { m_Delegate.AllEntries(ppEntries, pCount); }
	};

	class ServerBrowserAddRecovery : public ServerBrowserTests::CServerBrowserFixture
	{
	protected:
		CFailureFavorites m_Favorites{*m_pFavorites};
		void SetUp() override
		{
			CServerBrowserFixture::SetUp();
			CServerBrowserTestAccess::Initialize(m_Browser, &m_Friends, &m_Favorites);
		}
	};
}

TEST_F(ServerBrowserAddRecovery, FavoriteQueryAllocationFailureRemovesNodeAndKeepsRevision)
{
	const auto Info = Server("Candidate", 8301);
	const uint64_t Revision = m_Browser.FriendListRevision();
	m_Favorites.m_FailFavorite = true;
	EXPECT_THROW(CServerBrowserTestAccess::AddEntry(m_Browser, Info.m_aAddresses, 1), std::bad_alloc);
	EXPECT_EQ(CServerBrowserTestAccess::StorageSize(m_Browser), 0U);
	EXPECT_EQ(m_Browser.NumServers(), 0);
	EXPECT_EQ(m_Browser.Find(Info.m_aAddresses[0]), nullptr);
	EXPECT_EQ(m_Browser.FriendListRevision(), Revision);
}

TEST_F(ServerBrowserAddRecovery, UnknownPingQueryFailureRetractsUnpublishedNode)
{
	const auto Info = Server("Candidate", 8301);
	const uint64_t Revision = m_Browser.FriendListRevision();
	m_Favorites.m_FailPing = true;
	EXPECT_THROW(CServerBrowserTestAccess::AddEntry(m_Browser, Info.m_aAddresses, 1), int);
	EXPECT_EQ(CServerBrowserTestAccess::StorageSize(m_Browser), 0U);
	EXPECT_EQ(m_Browser.NumServers(), 0);
	EXPECT_EQ(m_Browser.Find(Info.m_aAddresses[0]), nullptr);
	EXPECT_EQ(m_Browser.FriendListRevision(), Revision);
}

TEST_F(ServerBrowserAddRecovery, FailedAdditionPreservesVisibleEntryAndPendingRequest)
{
	const auto Old = Server("Visible", 8301);
	Publish({Old});
	ASSERT_EQ(m_Browser.NumServers(), 1);
	auto *pOld = m_Browser.Find(Old.m_aAddresses[0]);
	ASSERT_NE(pOld, nullptr);
	m_Browser.QueueRequest(pOld);
	pOld->m_RequestTime = 31;
	const uint64_t Revision = m_Browser.FriendListRevision();
	const auto Fresh = Server("Candidate", 8302);
	m_Favorites.m_FailFavorite = true;
	EXPECT_THROW(CServerBrowserTestAccess::AddEntry(m_Browser, Fresh.m_aAddresses, 1), std::bad_alloc);
	EXPECT_EQ(CServerBrowserTestAccess::StorageSize(m_Browser), 1U);
	EXPECT_EQ(m_Browser.NumServers(), 1);
	EXPECT_EQ(m_Browser.SortedGet(0), &pOld->m_Info);
	EXPECT_EQ(m_Browser.Find(Old.m_aAddresses[0]), pOld);
	EXPECT_EQ(m_Browser.Find(Fresh.m_aAddresses[0]), nullptr);
	EXPECT_EQ(CServerBrowserTestAccess::FirstRequest(m_Browser), pOld);
	EXPECT_EQ(CServerBrowserTestAccess::LastRequest(m_Browser), pOld);
	EXPECT_EQ(CServerBrowserTestAccess::NumRequests(m_Browser), 1);
	EXPECT_EQ(pOld->m_RequestTime, 31);
	EXPECT_EQ(m_Browser.FriendListRevision(), Revision);
}

TEST_F(ServerBrowserAddRecovery, SameAddressCanBeAddedAfterPreparationFailure)
{
	const auto Info = Server("Candidate", 8301);
	const uint64_t Revision = m_Browser.FriendListRevision();
	m_Favorites.m_FailFavorite = true;
	EXPECT_THROW(CServerBrowserTestAccess::AddEntry(m_Browser, Info.m_aAddresses, 1), std::bad_alloc);
	m_Favorites.m_FailFavorite = false;
	auto *pEntry = CServerBrowserTestAccess::AddEntry(m_Browser, Info.m_aAddresses, 1);
	ASSERT_NE(pEntry, nullptr);
	EXPECT_EQ(m_Browser.NumServers(), 1);
	EXPECT_EQ(CServerBrowserTestAccess::StorageSize(m_Browser), 1U);
	EXPECT_EQ(m_Browser.Find(Info.m_aAddresses[0]), pEntry);
	EXPECT_EQ(pEntry->m_Info.m_ServerIndex, 0);
	EXPECT_EQ(m_Browser.FriendListRevision(), Revision + 1);
}

TEST_F(ServerBrowserAddRecovery, SuccessfulMultiAddressAdditionKeepsNativeMetadataRules)
{
	LoadCommunities();
	const auto A = Server("A", 8301).m_aAddresses[0];
	const auto B = Server("B", 8302).m_aAddresses[0];
	const NETADDR aAddresses[] = {A, B};
	m_pFavorites->Add(aAddresses, 2);
	m_pFavorites->AllowPing(aAddresses, 2, true);
	auto *pEntry = CServerBrowserTestAccess::AddEntry(m_Browser, aAddresses, 2);
	ASSERT_NE(pEntry, nullptr);
	EXPECT_EQ(m_Browser.Find(A), pEntry);
	EXPECT_EQ(m_Browser.Find(B), pEntry);
	EXPECT_EQ(pEntry->m_Info.m_NumAddresses, 2);
	EXPECT_STREQ(pEntry->m_Info.m_aAddress, "127.0.0.1:8301,127.0.0.1:8302");
	EXPECT_STREQ(pEntry->m_Info.m_aName, pEntry->m_Info.m_aAddress);
	EXPECT_STREQ(pEntry->m_Info.m_aCommunityId, "a");
	EXPECT_EQ(pEntry->m_Info.m_Favorite, TRISTATE::ALL);
	EXPECT_EQ(pEntry->m_Info.m_FavoriteAllowPing, TRISTATE::ALL);
	EXPECT_EQ(pEntry->m_Info.m_Latency, 999);
	EXPECT_EQ(pEntry->m_Info.m_HasRank, CServerInfo::RANK_UNAVAILABLE);
	EXPECT_EQ(pEntry->m_GotInfo, 0);
	EXPECT_EQ(pEntry->m_RequestTime, 0);
	EXPECT_TRUE(pEntry->m_Info.m_vClients.empty());
}
