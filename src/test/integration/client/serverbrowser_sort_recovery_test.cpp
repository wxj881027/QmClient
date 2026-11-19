#include <test/support/serverbrowser_fixture.h>

using ServerBrowserSortRecovery = ServerBrowserTests::CServerBrowserFixture;
using ServerBrowserTests::CPublicationLogger;

TEST_F(ServerBrowserSortRecovery, FailedResortKeepsVisibleOrderAndPlayerCountsUntilRetry)
{
	auto Alpha = Server("Alpha", 8301, 2);
	Alpha.m_NumPlayers = 1;
	Alpha.m_vClients[1].m_Player = false;
	Publish({Alpha, Server("Beta", 8302, 2)});
	const auto *pFirst = m_Browser.SortedGet(0);
	const auto Revision = m_Browser.FriendListRevision();
	m_Friends.AddFriend("Player0", "");
	m_Friends.m_Calls = 0;
	m_Friends.m_FailAt = 3;
	g_Config.m_BrFilterSpectators = 1;
	g_Config.m_BrSortOrder = 1;
	m_Browser.RequestResort();
	EXPECT_NO_THROW(m_Browser.Update());
	EXPECT_EQ(m_Browser.SortedGet(0), pFirst);
	EXPECT_EQ(m_Browser.NumSortedPlayers(), 4);
	EXPECT_EQ(m_Browser.Get(0)->m_NumFilteredPlayers, 2);
	EXPECT_EQ(m_Browser.FriendListRevision(), Revision);
	EXPECT_EQ(CServerBrowserTestAccess::HttpRevision(m_Browser), 1U);
	EXPECT_TRUE(m_Browser.IsServerlistError());
	EXPECT_TRUE(CServerBrowserTestAccess::NeedsResort(m_Browser));
	m_Friends.m_FailAt = 0;
	Retry();
	EXPECT_STREQ(m_Browser.SortedGet(0)->m_aName, "Beta");
	EXPECT_EQ(m_Browser.NumSortedPlayers(), 3);
	EXPECT_EQ(m_Browser.Get(0)->m_NumFilteredPlayers, 1);
	EXPECT_FALSE(m_Browser.IsServerlistError());
	EXPECT_FALSE(CServerBrowserTestAccess::NeedsResort(m_Browser));
}

TEST_F(ServerBrowserSortRecovery, PartialFriendUpdateRestoresCompletedAndIncompleteCaches)
{
	const auto Alpha = Server("Alpha", 8301, 2);
	const auto Beta = Server("Beta", 8302, 2);
	Publish({Alpha, Beta});
	auto *pAlpha = m_Browser.Find(Alpha.m_aAddresses[0]);
	auto *pBeta = m_Browser.Find(Beta.m_aAddresses[0]);
	const auto OldFriendsRevision = pAlpha->m_FriendStateRevision;
	m_Friends.AddFriend("Player0", "");
	m_Friends.AddFriend("Player1", "");
	m_Friends.m_Calls = 0;
	m_Friends.m_FailAt = 4;
	m_Browser.RequestResort();
	EXPECT_NO_THROW(m_Browser.Update());
	for(const auto *pEntry : {pAlpha, pBeta})
	{
		EXPECT_EQ(pEntry->m_Info.m_FriendState, IFriends::FRIEND_NO);
		EXPECT_EQ(pEntry->m_Info.m_FriendNum, 0);
		EXPECT_EQ(pEntry->m_FriendStateRevision, OldFriendsRevision);
		EXPECT_TRUE(pEntry->m_FriendStateValid);
		for(const auto &Client : pEntry->m_Info.m_vClients)
			EXPECT_EQ(Client.m_FriendState, IFriends::FRIEND_NO);
	}
	m_Friends.m_FailAt = 0;
	Retry();
	EXPECT_EQ(m_Friends.m_Calls, 8);
	for(const auto *pEntry : {pAlpha, pBeta})
	{
		EXPECT_EQ(pEntry->m_Info.m_FriendNum, 2);
		EXPECT_EQ(pEntry->m_FriendStateRevision, m_Friends.Revision());
	}
}

TEST_F(ServerBrowserSortRecovery, FailedSearchRestoresHighlightsAndFavoritesThenUsesLatestSelection)
{
	const auto Alpha = Server("Alpha", 8301, 2);
	const auto Beta = Server("Beta", 8302, 2);
	Publish({Alpha, Beta});
	const auto OldAllowPing = m_Browser.Get(0)->m_FavoriteAllowPing;
	m_pFavorites->Add(Alpha.m_aAddresses, 1);
	m_pFavorites->AllowPing(Alpha.m_aAddresses, 1, true);
	m_Friends.AddFriend("Player0", "");
	m_Friends.m_Calls = 0;
	m_Friends.m_FailAt = 3;
	str_copy(g_Config.m_BrFilterString, "Player0");
	m_Browser.RequestResort();
	EXPECT_NO_THROW(m_Browser.Update());
	EXPECT_EQ(m_Browser.NumSortedServers(), 2);
	EXPECT_EQ(m_Browser.Get(0)->m_QuickSearchHit, 0);
	EXPECT_EQ(m_Browser.Get(0)->m_Favorite, TRISTATE::NONE);
	EXPECT_EQ(m_Browser.Get(0)->m_FavoriteAllowPing, OldAllowPing);
	str_copy(g_Config.m_BrFilterString, "Beta");
	m_pFavorites->Remove(Alpha.m_aAddresses, 1);
	m_pFavorites->Add(Beta.m_aAddresses, 1);
	m_Friends.m_FailAt = 0;
	Retry();
	ASSERT_EQ(m_Browser.NumSortedServers(), 1);
	EXPECT_STREQ(m_Browser.SortedGet(0)->m_aName, "Beta");
	EXPECT_EQ(m_Browser.SortedGet(0)->m_QuickSearchHit, IServerBrowser::QUICK_SERVERNAME);
	EXPECT_NE(m_Browser.SortedGet(0)->m_Favorite, TRISTATE::NONE);
}

TEST_F(ServerBrowserSortRecovery, FailedClanModeChangeRestoresFriendCacheValidity)
{
	m_Friends.AddFriend("Player0", "OldClan");
	auto Alpha = Server("Alpha", 8301);
	str_copy(Alpha.m_vClients[0].m_aClan, "NewClan");
	const auto Beta = Server("Beta", 8302);
	Publish({Alpha, Beta});
	g_Config.m_ClFriendsIgnoreClan = 1;
	m_Friends.m_Calls = 0;
	m_Friends.m_FailAt = 2;
	m_Browser.RequestResort();
	EXPECT_NO_THROW(m_Browser.Update());
	EXPECT_FALSE(m_Browser.Find(Alpha.m_aAddresses[0])->m_FriendStateIgnoreClan);
	EXPECT_EQ(m_Browser.Get(0)->m_FriendNum, 0);
	m_Friends.m_FailAt = 0;
	Retry();
	EXPECT_EQ(m_Friends.m_Calls, 4);
	EXPECT_TRUE(m_Browser.Find(Alpha.m_aAddresses[0])->m_FriendStateIgnoreClan);
	EXPECT_EQ(m_Browser.Get(0)->m_FriendNum, 1);
}

TEST_F(ServerBrowserSortRecovery, FailedHashChangeRemainsRetryableAfterTheOptionIsRestored)
{
	Publish({Server("Alpha", 8301)});
	ASSERT_FALSE(CServerBrowserTestAccess::NeedsResort(m_Browser));
	m_Friends.AddFriend("Player0", "");
	m_Friends.m_Calls = 0;
	m_Friends.m_FailAt = 1;
	g_Config.m_BrSortOrder = 1;
	EXPECT_NO_THROW(m_Browser.Update());
	g_Config.m_BrSortOrder = 0;
	m_Friends.m_FailAt = 0;
	Retry();
	EXPECT_EQ(m_Friends.m_Calls, 2);
	EXPECT_EQ(m_Browser.Get(0)->m_FriendNum, 1);
	EXPECT_FALSE(m_Browser.IsServerlistError());
}

TEST_F(ServerBrowserSortRecovery, RetryCooldownDoesNotQueryFriendsEveryFrame)
{
	Publish({Server("Alpha", 8301)});
	m_Friends.AddFriend("Player0", "");
	m_Friends.m_Calls = 0;
	m_Friends.m_FailAt = 1;
	m_Browser.RequestResort();
	EXPECT_NO_THROW(m_Browser.Update());
	m_Friends.m_FailAt = 0;
	m_Now += time_freq() - 1;
	m_Browser.Update();
	EXPECT_EQ(m_Friends.m_Calls, 1);
	EXPECT_EQ(m_Browser.Get(0)->m_FriendNum, 0);
	++m_Now;
	m_Browser.Update();
	EXPECT_EQ(m_Friends.m_Calls, 2);
	EXPECT_EQ(m_Browser.Get(0)->m_FriendNum, 1);
}

TEST_F(ServerBrowserSortRecovery, ManualRefreshRecoversBeforeTheSortRetryDeadline)
{
	Publish({Server("Alpha", 8301)});
	m_Friends.AddFriend("Player0", "");
	m_Friends.m_Calls = 0;
	m_Friends.m_FailAt = 1;
	m_Browser.RequestResort();
	EXPECT_NO_THROW(m_Browser.Update());
	m_Friends.m_FailAt = 0;
	m_Browser.Refresh(IServerBrowser::TYPE_INTERNET);
	EXPECT_EQ(m_Now, 100);
	EXPECT_EQ(m_Browser.Get(0)->m_FriendNum, 1);
	EXPECT_TRUE(m_pHttp->m_Refreshing);
	EXPECT_FALSE(m_Browser.IsServerlistError());
}

TEST_F(ServerBrowserSortRecovery, NewHttpPublicationClearsSortFailureAndReusesScratchForNewEntries)
{
	Publish({Server("Old", 8301)});
	m_Friends.AddFriend("Player0", "");
	m_Friends.m_Calls = 0;
	m_Friends.m_FailAt = 1;
	m_Browser.RequestResort();
	EXPECT_NO_THROW(m_Browser.Update());
	m_Friends.m_FailAt = 0;
	Publish({Server("New A", 8302), Server("New B", 8303)});
	ASSERT_EQ(m_Browser.NumSortedServers(), 2);
	EXPECT_STREQ(m_Browser.SortedGet(0)->m_aName, "New A");
	EXPECT_FALSE(m_Browser.IsServerlistError());
	g_Config.m_BrSortOrder = 1;
	m_Browser.Update();
	EXPECT_STREQ(m_Browser.SortedGet(0)->m_aName, "New B");
	const auto Revision = m_Browser.FriendListRevision();
	Retry();
	EXPECT_EQ(m_Browser.FriendListRevision(), Revision);
}

TEST_F(ServerBrowserSortRecovery, RepeatedFailuresLogOnceAndCanReportAgainAfterRecovery)
{
	CPublicationLogger Logger;
	CLogScope Scope(&Logger);
	Publish({Server("Alpha", 8301)});
	m_Friends.AddFriend("Player0", "");
	m_Friends.m_Calls = 0;
	m_Friends.m_FailAt = 1;
	m_Browser.RequestResort();
	EXPECT_NO_THROW(m_Browser.Update());
	EXPECT_EQ(Logger.m_Messages, 1);
	m_Friends.m_FailAt = 2;
	EXPECT_NO_THROW(Retry());
	EXPECT_EQ(Logger.m_Messages, 1);
	m_Friends.m_FailAt = 0;
	Retry();
	m_Friends.RemoveFriend("Player0", "");
	m_Friends.m_FailAt = m_Friends.m_Calls + 1;
	m_Browser.RequestResort();
	EXPECT_NO_THROW(m_Browser.Update());
	EXPECT_EQ(Logger.m_Messages, 2);
}

TEST_F(ServerBrowserSortRecovery, SuccessfulResortReusesValidFriendResults)
{
	Publish({Server("Alpha", 8301, 2)});
	const int Queries = m_Friends.m_Calls;
	str_copy(g_Config.m_BrFilterString, "Player0");
	m_Browser.RequestResort();
	m_Browser.Update();
	EXPECT_EQ(m_Friends.m_Calls, Queries);
	EXPECT_EQ(m_Browser.NumSortedServers(), 1);
	EXPECT_EQ(m_Browser.Get(0)->m_QuickSearchHit, IServerBrowser::QUICK_PLAYER);
}

TEST_F(ServerBrowserSortRecovery, FailedResortRestoresCommunityCountsAndResourceIdentity)
{
	ASSERT_NO_FATAL_FAILURE(LoadCommunities());
	Publish({Server("Alpha", 8301, 3), Server("Beta", 8302)});
	const auto *pCountry = &m_Browser.Community("a")->Countries()[0];
	m_Friends.AddFriend("Player0", "");
	m_Friends.m_Calls = 0;
	m_Friends.m_FailAt = 4;
	m_Browser.RequestResort();
	EXPECT_NO_THROW(m_Browser.Update());
	EXPECT_EQ(m_Browser.Community("a")->NumPlayers(), 3);
	EXPECT_EQ(m_Browser.Community("b")->NumPlayers(), 1);
	EXPECT_STREQ(m_Browser.Communities()[0].Id(), "a");
	EXPECT_EQ(&m_Browser.Community("a")->Countries()[0], pCountry);
	m_Friends.m_FailAt = 0;
	Retry();
	EXPECT_FALSE(m_Browser.IsServerlistError());
	EXPECT_EQ(m_Browser.NumSortedPlayers(), 4);
}

TEST_F(ServerBrowserSortRecovery, UnknownFriendExceptionLeavesThePublishedStateUsable)
{
	Publish({Server("Alpha", 8301)});
	const auto *pEntry = m_Browser.Get(0);
	m_Friends.AddFriend("Player0", "");
	m_Friends.m_Calls = 0;
	m_Friends.m_FailAt = 1;
	m_Friends.m_UnknownFailure = true;
	m_Browser.RequestResort();
	EXPECT_NO_THROW(m_Browser.Update());
	EXPECT_EQ(m_Browser.Get(0), pEntry);
	EXPECT_EQ(m_Browser.SortedGet(0), pEntry);
	EXPECT_EQ(m_Browser.NumSortedPlayers(), 1);
	m_Friends.m_FailAt = 0;
	Retry();
	EXPECT_FALSE(m_Browser.IsServerlistError());
}
