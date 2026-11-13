#include <test/support/serverbrowser_fixture.h>

using ServerBrowserPublication = ServerBrowserTests::CServerBrowserFixture;
using ServerBrowserTests::CPublicationLogger;

TEST_F(ServerBrowserPublication, PartialCopyFailurePreservesVisibleListAndRetriesSameRevision)
{
	const auto Old = Server("Old", 8300, 2);
	Publish({Old});
	const auto *pOld = m_Browser.Get(0);
	const auto FriendRevision = m_Browser.FriendListRevision();
	const auto Fresh = Server("Fresh", 8301);
	m_pHttp->m_FailAt = 1;
	EXPECT_NO_THROW(Publish({Fresh, Server("Second", 8302)}));
	ASSERT_EQ(m_Browser.NumServers(), 1);
	EXPECT_EQ(m_Browser.Get(0), pOld);
	EXPECT_EQ(m_Browser.SortedGet(0), pOld);
	EXPECT_EQ(m_Browser.NumSortedPlayers(), 2);
	ASSERT_NE(m_Browser.Find(Old.m_aAddresses[0]), nullptr);
	EXPECT_EQ(m_Browser.Find(Old.m_aAddresses[0])->m_Info.m_NumClients, 2);
	EXPECT_EQ(m_Browser.Find(Fresh.m_aAddresses[0]), nullptr);
	EXPECT_EQ(m_Browser.FriendListRevision(), FriendRevision);
	EXPECT_EQ(CServerBrowserTestAccess::HttpRevision(m_Browser), 1U);
	EXPECT_TRUE(m_Browser.IsServerlistError());
	m_pHttp->m_FailAt = -1;
	Retry();
	ASSERT_EQ(m_Browser.NumServers(), 2);
	EXPECT_EQ(m_Browser.Find(Old.m_aAddresses[0]), nullptr);
	EXPECT_STREQ(m_Browser.SortedGet(0)->m_aName, "Fresh");
	EXPECT_EQ(CServerBrowserTestAccess::HttpRevision(m_Browser), 2U);
	EXPECT_FALSE(m_Browser.IsServerlistError());
}

TEST_F(ServerBrowserPublication, FailureBeforeFirstVisibleResultCanRecoverWithoutNewHttpRevision)
{
	m_pHttp->m_FailAt = 0;
	EXPECT_NO_THROW(Publish({Server("First", 8301)}));
	EXPECT_EQ(m_Browser.NumServers(), 0);
	EXPECT_EQ(m_Browser.NumSortedServers(), 0);
	EXPECT_EQ(CServerBrowserTestAccess::HttpRevision(m_Browser), 0U);
	m_pHttp->m_FailAt = -1;
	Retry();
	ASSERT_EQ(m_Browser.NumServers(), 1);
	EXPECT_STREQ(m_Browser.Get(0)->m_aName, "First");
}

TEST_F(ServerBrowserPublication, UnknownLatencyFailureRestoresAddressAndRequestChain)
{
	const auto First = Server("First", 8301);
	const auto Second = Server("Second", 8302);
	Publish({First, Second});
	auto *pFirst = m_Browser.Find(First.m_aAddresses[0]);
	auto *pSecond = m_Browser.Find(Second.m_aAddresses[0]);
	m_Browser.QueueRequest(pFirst);
	m_Browser.QueueRequest(pSecond);
	pFirst->m_RequestTime = 41;
	CServerBrowserTestAccess::SetMaxRequests(m_Browser, 3);
	m_pPings->m_Fail = true;
	EXPECT_NO_THROW(Publish({Server("Replacement", 8303)}));
	EXPECT_EQ(CServerBrowserTestAccess::NumRequests(m_Browser), 2);
	ASSERT_EQ(CServerBrowserTestAccess::FirstRequest(m_Browser), pFirst);
	ASSERT_EQ(CServerBrowserTestAccess::LastRequest(m_Browser), pSecond);
	EXPECT_EQ(pFirst->m_pNextReq, pSecond);
	EXPECT_EQ(pSecond->m_pPrevReq, pFirst);
	EXPECT_EQ(pFirst->m_RequestTime, 41);
	EXPECT_EQ(CServerBrowserTestAccess::MaxRequests(m_Browser), 3);
	m_pPings->m_Fail = false;
	Retry();
	EXPECT_EQ(CServerBrowserTestAccess::NumRequests(m_Browser), 0);
	EXPECT_EQ(CServerBrowserTestAccess::FirstRequest(m_Browser), nullptr);
	EXPECT_EQ(CServerBrowserTestAccess::LastRequest(m_Browser), nullptr);
	EXPECT_EQ(CServerBrowserTestAccess::MaxRequests(m_Browser), 8);
}

TEST_F(ServerBrowserPublication, SortFailureRestoresCommunityCountsOrderAndPendingSort)
{
	ASSERT_NO_FATAL_FAILURE(LoadCommunities());
	Publish({Server("Alpha", 8301, 3), Server("Beta", 8302, 1)});
	m_Browser.RequestResort();
	const auto FriendRevision = m_Browser.FriendListRevision();
	const auto *pCountry = &m_Browser.Community("a")->Countries()[0];
	m_Friends.m_Calls = 0;
	m_Friends.m_FailAt = 3;
	EXPECT_NO_THROW(Publish({Server("New Beta", 8302, 2), Server("New Gamma", 8303, 4)}));
	ASSERT_EQ(m_Browser.Communities().size(), 4U);
	EXPECT_STREQ(m_Browser.Communities()[0].Id(), "a");
	EXPECT_EQ(m_Browser.Community("a")->NumPlayers(), 3);
	EXPECT_EQ(m_Browser.Community("b")->NumPlayers(), 1);
	EXPECT_EQ(m_Browser.Community("c")->NumPlayers(), 0);
	EXPECT_EQ(&m_Browser.Community("a")->Countries()[0], pCountry);
	EXPECT_EQ(m_Browser.NumSortedPlayers(), 4);
	EXPECT_EQ(m_Browser.FriendListRevision(), FriendRevision);
	EXPECT_TRUE(CServerBrowserTestAccess::NeedsResort(m_Browser));
	m_Friends.m_FailAt = 0;
	Retry();
	EXPECT_STREQ(m_Browser.Communities()[0].Id(), "c");
	EXPECT_STREQ(m_Browser.Communities()[1].Id(), "b");
	EXPECT_EQ(m_Browser.NumSortedPlayers(), 6);
	EXPECT_FALSE(CServerBrowserTestAccess::NeedsResort(m_Browser));
}

TEST_F(ServerBrowserPublication, CommunityReorderPreservesNestedResourcesAndStableTies)
{
	ASSERT_NO_FATAL_FAILURE(LoadCommunities());
	const auto *pCountryA = &m_Browser.Community("a")->Countries()[0];
	const auto *pTypeC = &m_Browser.Community("c")->Types()[0];
	Publish({Server("Alpha", 8301, 2), Server("Beta", 8302, 1), Server("Gamma", 8303, 3)});
	EXPECT_STREQ(m_Browser.Communities()[0].Id(), "c");
	EXPECT_STREQ(m_Browser.Communities()[1].Id(), "a");
	EXPECT_STREQ(m_Browser.Communities()[2].Id(), "b");
	EXPECT_EQ(&m_Browser.Community("a")->Countries()[0], pCountryA);
	EXPECT_EQ(&m_Browser.Community("c")->Types()[0], pTypeC);
	EXPECT_EQ(m_Browser.Community("a")->HasRank("Map"), CServerInfo::RANK_RANKED);
	EXPECT_STREQ(m_Browser.Community("a")->Name(), "Alpha");
	EXPECT_STREQ(m_Browser.Community("c")->IconUrl(), "https://c.test/icon");
	Publish({Server("Alpha", 8301, 1), Server("Beta", 8302, 1), Server("Gamma", 8303, 1)});
	EXPECT_STREQ(m_Browser.Communities()[0].Id(), "c");
	EXPECT_STREQ(m_Browser.Communities()[1].Id(), "a");
	EXPECT_STREQ(m_Browser.Communities()[2].Id(), "b");
	EXPECT_EQ(&m_Browser.Community("a")->Countries()[0], pCountryA);
}

TEST_F(ServerBrowserPublication, ForcedRefreshRetriesFailedCachedPublicationWhileFetchIsPending)
{
	Publish({Server("Old", 8301)});
	m_pHttp->m_vServers = {Server("Cached", 8302)};
	m_pHttp->m_FailAt = 0;
	EXPECT_NO_THROW(m_Browser.Refresh(IServerBrowser::TYPE_INTERNET, true));
	EXPECT_TRUE(CServerBrowserTestAccess::RefreshPending(m_Browser));
	EXPECT_TRUE(m_pHttp->m_Refreshing);
	EXPECT_STREQ(m_Browser.Get(0)->m_aName, "Old");
	m_pHttp->m_FailAt = -1;
	Retry();
	EXPECT_TRUE(m_pHttp->m_Refreshing);
	ASSERT_EQ(m_Browser.NumServers(), 1);
	EXPECT_STREQ(m_Browser.Get(0)->m_aName, "Cached");
	EXPECT_FALSE(m_Browser.IsServerlistError());
}

TEST_F(ServerBrowserPublication, FailedCategoryChangeKeepsSelectionAndRebuildsFavoriteRequestsOnRetry)
{
	const auto Favorite = Server("Favorite", 8301);
	const auto Missing = Server("Offline favorite", 8303);
	const auto Other = Server("Other", 8302);
	Publish({Favorite, Other});
	auto *pOldRequest = m_Browser.Find(Other.m_aAddresses[0]);
	m_Browser.QueueRequest(pOldRequest);
	m_pFavorites->Add(Favorite.m_aAddresses, 1);
	m_pFavorites->Add(Missing.m_aAddresses, 1);
	m_pFavorites->AllowPing(Missing.m_aAddresses, 1, true);
	m_Friends.m_FailAt = m_Friends.m_Calls + 1;
	EXPECT_NO_THROW(m_Browser.Refresh(IServerBrowser::TYPE_FAVORITES));
	EXPECT_EQ(m_Browser.GetCurrentType(), IServerBrowser::TYPE_FAVORITES);
	EXPECT_EQ(m_Browser.NumServers(), 2);
	EXPECT_EQ(CServerBrowserTestAccess::NumRequests(m_Browser), 1);
	EXPECT_EQ(CServerBrowserTestAccess::FirstRequest(m_Browser), pOldRequest);
	EXPECT_EQ(m_Browser.Find(Missing.m_aAddresses[0]), nullptr);
	m_Friends.m_FailAt = 0;
	Retry();
	EXPECT_EQ(m_Browser.NumServers(), 2);
	auto *pMissing = m_Browser.Find(Missing.m_aAddresses[0]);
	ASSERT_NE(pMissing, nullptr);
	EXPECT_EQ(CServerBrowserTestAccess::FirstRequest(m_Browser), pMissing);
	EXPECT_EQ(CServerBrowserTestAccess::LastRequest(m_Browser), pMissing);
	EXPECT_EQ(CServerBrowserTestAccess::NumRequests(m_Browser), 1);
}

TEST_F(ServerBrowserPublication, EmptyPublicationClearsOldListAndConsumesRevision)
{
	Publish({Server("Old", 8301)});
	Publish({});
	EXPECT_EQ(m_Browser.NumServers(), 0);
	EXPECT_EQ(m_Browser.NumSortedServers(), 0);
	EXPECT_EQ(m_Browser.NumSortedPlayers(), 0);
	EXPECT_EQ(CServerBrowserTestAccess::HttpRevision(m_Browser), 2U);
	const auto FriendRevision = m_Browser.FriendListRevision();
	m_Browser.Update();
	EXPECT_EQ(m_Browser.FriendListRevision(), FriendRevision);
}

TEST_F(ServerBrowserPublication, RetryPublishesNewestRevisionAfterIntermediateFailure)
{
	Publish({Server("Old", 8301)});
	m_pHttp->m_FailAt = 0;
	EXPECT_NO_THROW(Publish({Server("Failed candidate", 8302)}));
	EXPECT_EQ(CServerBrowserTestAccess::HttpRevision(m_Browser), 1U);
	m_pHttp->m_FailAt = -1;
	m_Now += time_freq();
	Publish({Server("Newest", 8303)});
	ASSERT_EQ(m_Browser.NumServers(), 1);
	EXPECT_STREQ(m_Browser.Get(0)->m_aName, "Newest");
	EXPECT_EQ(CServerBrowserTestAccess::HttpRevision(m_Browser), 3U);
	EXPECT_FALSE(m_Browser.IsServerlistError());
}

TEST_F(ServerBrowserPublication, RepeatedFailureLogsOnceAndNewFailureAfterRecoveryCanLogAgain)
{
	CPublicationLogger Logger;
	CLogScope Scope(&Logger);
	m_pHttp->m_FailAt = 0;
	EXPECT_NO_THROW(Publish({Server("First", 8301)}));
	EXPECT_NO_THROW(m_Browser.Update());
	EXPECT_EQ(Logger.m_Messages, 1);
	m_pHttp->m_FailAt = -1;
	Retry();
	m_pHttp->m_FailAt = 0;
	EXPECT_NO_THROW(Publish({Server("Second", 8302)}));
	EXPECT_EQ(Logger.m_Messages, 2);
}

TEST_F(ServerBrowserPublication, RetryWaitsOneSecondButManualRefreshCanRecoverImmediately)
{
	m_pHttp->m_FailAt = 0;
	EXPECT_NO_THROW(Publish({Server("Ready", 8301)}));
	m_pHttp->m_FailAt = -1;
	m_Now += time_freq() - 1;
	m_Browser.Update();
	EXPECT_EQ(m_Browser.NumServers(), 0);
	++m_Now;
	m_Browser.Update();
	ASSERT_EQ(m_Browser.NumServers(), 1);
	m_pHttp->m_FailAt = 0;
	EXPECT_NO_THROW(Publish({Server("Manual", 8302)}));
	m_pHttp->m_FailAt = -1;
	m_Browser.Refresh(IServerBrowser::TYPE_INTERNET);
	ASSERT_EQ(m_Browser.NumServers(), 1);
	EXPECT_STREQ(m_Browser.Get(0)->m_aName, "Manual");
	EXPECT_FALSE(m_Browser.IsServerlistError());
}

TEST_F(ServerBrowserPublication, FavoriteCommunityCacheKeepsItsIdentityAfterPlayerCountReorder)
{
	ASSERT_NO_FATAL_FAILURE(LoadCommunities());
	m_Browser.FavoriteCommunitiesFilter().Add("c");
	m_Browser.Refresh(IServerBrowser::TYPE_FAVORITE_COMMUNITY_1);
	m_Browser.CommunityCache().Update(true);
	EXPECT_STREQ(m_Browser.CommunityCache().CountryTypeFilterKey(), "c");
	Publish({Server("Gamma", 8303, 2)});
	EXPECT_STREQ(m_Browser.Communities()[0].Id(), "c");
	// 刷新缓存之前，筛选键也不能跟随旧内存槽位变成另一个社区。
	EXPECT_STREQ(m_Browser.CommunityCache().CountryTypeFilterKey(), "c");
	m_Browser.CommunityCache().Update(false);
	const auto &Selected = m_Browser.CommunityCache().SelectedCommunities();
	ASSERT_EQ(Selected.size(), 1U);
	EXPECT_EQ(Selected[0], m_Browser.Community("c"));
	ASSERT_EQ(m_Browser.CommunityCache().SelectableCountries().size(), 1U);
	EXPECT_STREQ(m_Browser.CommunityCache().SelectableCountries()[0]->Name(), "CC");
	ASSERT_EQ(m_Browser.CommunityCache().SelectableTypes().size(), 1U);
	EXPECT_STREQ(m_Browser.CommunityCache().SelectableTypes()[0]->Name(), "RaceC");
}
