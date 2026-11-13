#include <test/support/serverbrowser_entry_browser_fixture.h>

using ServerBrowserReplaceRecovery = ServerBrowserEntryTests::CEntryBrowserFixture;

TEST_F(ServerBrowserReplaceRecovery, FavoriteQueryFailureKeepsOldAddressInfoAndRevision)
{
	const auto Old = Server("Visible", 8301, 2);
	Publish({Old});
	ASSERT_EQ(m_Browser.NumServers(), 1);
	auto *pEntry = m_Browser.Find(Old.m_aAddresses[0]);
	ASSERT_NE(pEntry, nullptr);
	pEntry->m_FriendStateValid = true;
	const auto *pClients = pEntry->m_Info.m_vClients.data();
	const uint64_t Revision = m_Browser.FriendListRevision();
	const auto Fresh = Server("Candidate", 8302);
	m_Favorites.m_FailFavorite = true;
	EXPECT_THROW(CServerBrowserTestAccess::ReplaceEntry(m_Browser, pEntry, Fresh.m_aAddresses, 1), std::bad_alloc);
	EXPECT_EQ(m_Browser.Find(Old.m_aAddresses[0]), pEntry);
	EXPECT_EQ(m_Browser.Find(Fresh.m_aAddresses[0]), nullptr);
	EXPECT_EQ(pEntry->m_Info.m_aAddresses[0], Old.m_aAddresses[0]);
	EXPECT_STREQ(pEntry->m_Info.m_aName, "Visible");
	EXPECT_EQ(pEntry->m_Info.m_vClients.data(), pClients);
	EXPECT_EQ(pEntry->m_Info.m_vClients.size(), 2U);
	EXPECT_TRUE(pEntry->m_FriendStateValid);
	EXPECT_EQ(m_Browser.FriendListRevision(), Revision);
}

TEST_F(ServerBrowserReplaceRecovery, UnknownPingQueryFailureKeepsPreviousFavoriteAndLatency)
{
	const auto Old = Server("Visible", 8301);
	Publish({Old});
	ASSERT_EQ(m_Browser.NumServers(), 1);
	auto *pEntry = m_Browser.Find(Old.m_aAddresses[0]);
	ASSERT_NE(pEntry, nullptr);
	pEntry->m_Info.m_Favorite = TRISTATE::ALL;
	pEntry->m_Info.m_FavoriteAllowPing = TRISTATE::SOME;
	pEntry->m_Info.m_Latency = 42;
	const auto Fresh = Server("Candidate", 8302);
	const uint64_t Revision = m_Browser.FriendListRevision();
	m_Favorites.m_FailPing = true;
	EXPECT_THROW(CServerBrowserTestAccess::ReplaceEntry(m_Browser, pEntry, Fresh.m_aAddresses, 1), int);
	EXPECT_EQ(m_Browser.Find(Old.m_aAddresses[0]), pEntry);
	EXPECT_EQ(m_Browser.Find(Fresh.m_aAddresses[0]), nullptr);
	EXPECT_EQ(pEntry->m_Info.m_Favorite, TRISTATE::ALL);
	EXPECT_EQ(pEntry->m_Info.m_FavoriteAllowPing, TRISTATE::SOME);
	EXPECT_EQ(pEntry->m_Info.m_Latency, 42);
	EXPECT_EQ(m_Browser.FriendListRevision(), Revision);
}

TEST_F(ServerBrowserReplaceRecovery, FailedReplacementPreservesVisibleIdentityAndRequestChain)
{
	const auto Old = Server("Visible", 8301);
	Publish({Old});
	ASSERT_EQ(m_Browser.NumServers(), 1);
	auto *pEntry = m_Browser.Find(Old.m_aAddresses[0]);
	ASSERT_NE(pEntry, nullptr);
	m_Browser.QueueRequest(pEntry);
	pEntry->m_RequestTime = 37;
	const auto Fresh = Server("Candidate", 8302);
	m_Favorites.m_FailFavorite = true;
	EXPECT_THROW(CServerBrowserTestAccess::ReplaceEntry(m_Browser, pEntry, Fresh.m_aAddresses, 1), std::bad_alloc);
	EXPECT_EQ(m_Browser.SortedGet(0), &pEntry->m_Info);
	EXPECT_EQ(CServerBrowserTestAccess::StorageSize(m_Browser), 1U);
	EXPECT_EQ(CServerBrowserTestAccess::FirstRequest(m_Browser), pEntry);
	EXPECT_EQ(CServerBrowserTestAccess::LastRequest(m_Browser), pEntry);
	EXPECT_EQ(CServerBrowserTestAccess::NumRequests(m_Browser), 1);
	EXPECT_EQ(pEntry->m_RequestTime, 37);
	EXPECT_EQ(m_Browser.Find(Old.m_aAddresses[0]), pEntry);
}

TEST_F(ServerBrowserReplaceRecovery, SameReplacementCanRetryWithCurrentFavoriteAndCommunity)
{
	LoadCommunities();
	const auto Old = Server("Visible", 8301, 2);
	Publish({Old});
	ASSERT_EQ(m_Browser.NumServers(), 1);
	auto *pEntry = m_Browser.Find(Old.m_aAddresses[0]);
	ASSERT_NE(pEntry, nullptr);
	const auto *pClients = pEntry->m_Info.m_vClients.data();
	const auto Fresh = Server("Candidate", 8302);
	m_pFavorites->Add(Fresh.m_aAddresses, 1);
	m_pFavorites->AllowPing(Fresh.m_aAddresses, 1, true);
	const uint64_t Revision = m_Browser.FriendListRevision();
	m_Favorites.m_FailFavorite = true;
	EXPECT_THROW(CServerBrowserTestAccess::ReplaceEntry(m_Browser, pEntry, Fresh.m_aAddresses, 1), std::bad_alloc);
	m_Favorites.m_FailFavorite = false;
	EXPECT_EQ(CServerBrowserTestAccess::ReplaceEntry(m_Browser, pEntry, Fresh.m_aAddresses, 1), pEntry);
	EXPECT_EQ(m_Browser.Find(Old.m_aAddresses[0]), nullptr);
	EXPECT_EQ(m_Browser.Find(Fresh.m_aAddresses[0]), pEntry);
	EXPECT_STREQ(pEntry->m_Info.m_aName, "127.0.0.1:8302");
	EXPECT_STREQ(pEntry->m_Info.m_aCommunityId, "b");
	EXPECT_EQ(pEntry->m_Info.m_Favorite, TRISTATE::ALL);
	EXPECT_EQ(pEntry->m_Info.m_FavoriteAllowPing, TRISTATE::ALL);
	EXPECT_EQ(pEntry->m_Info.m_HasRank, CServerInfo::RANK_UNAVAILABLE);
	EXPECT_EQ(pEntry->m_Info.m_Latency, 999);
	EXPECT_EQ(pEntry->m_Info.m_vClients.data(), pClients);
	EXPECT_EQ(pEntry->m_Info.m_vClients.size(), 2U);
	EXPECT_EQ(m_Browser.FriendListRevision(), Revision + 1);
}

TEST_F(ServerBrowserReplaceRecovery, SelfAliasedAddressKeepsPlayerStorageAndExistingEntry)
{
	const auto Old = Server("Visible", 8301, 2);
	Publish({Old});
	ASSERT_EQ(m_Browser.NumServers(), 1);
	auto *pEntry = m_Browser.Find(Old.m_aAddresses[0]);
	ASSERT_NE(pEntry, nullptr);
	const auto *pClients = pEntry->m_Info.m_vClients.data();
	CServerBrowserTestAccess::ReplaceEntry(m_Browser, pEntry, pEntry->m_Info.m_aAddresses, 1);
	EXPECT_EQ(m_Browser.Find(Old.m_aAddresses[0]), pEntry);
	EXPECT_EQ(m_Browser.Get(0), &pEntry->m_Info);
	EXPECT_EQ(pEntry->m_Info.m_vClients.data(), pClients);
	EXPECT_EQ(pEntry->m_Info.m_vClients.size(), 2U);
}

TEST_F(ServerBrowserReplaceRecovery, SuccessfulMultiAddressReplacementPublishesEveryNewKey)
{
	const auto Old = Server("Visible", 8301);
	Publish({Old});
	ASSERT_EQ(m_Browser.NumServers(), 1);
	auto *pEntry = m_Browser.Find(Old.m_aAddresses[0]);
	ASSERT_NE(pEntry, nullptr);
	const auto B = Server("B", 8302).m_aAddresses[0];
	const auto C = Server("C", 8303).m_aAddresses[0];
	const NETADDR aAddresses[] = {B, C};
	CServerBrowserTestAccess::ReplaceEntry(m_Browser, pEntry, aAddresses, 2);
	EXPECT_EQ(m_Browser.Find(Old.m_aAddresses[0]), nullptr);
	EXPECT_EQ(m_Browser.Find(B), pEntry);
	EXPECT_EQ(m_Browser.Find(C), pEntry);
	EXPECT_EQ(pEntry->m_Info.m_NumAddresses, 2);
	EXPECT_STREQ(pEntry->m_Info.m_aAddress, "127.0.0.1:8302,127.0.0.1:8303");
}
