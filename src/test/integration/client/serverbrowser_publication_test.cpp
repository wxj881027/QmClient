#include <base/logger.h>
#include <base/time.h>

#include <engine/client/friends.h>
#include <engine/client/serverbrowser_http.h>
#include <engine/client/serverbrowser_ping_cache.h>
#include <engine/favorites.h>
#include <engine/shared/config.h>

#include <gtest/gtest.h>
#include <test/support/serverbrowser_test_access.h>

#include <memory>
#include <new>
#include <string>
#include <vector>

namespace
{
	class CPublishedList : public IServerBrowserHttp
	{
	public:
		std::vector<CServerInfo> m_vServers;
		uint64_t m_Revision = 0;
		int m_FailAt = -1;
		bool m_Refreshing = false;
		void Shutdown() override {}
		void Update() override {}
		uint64_t Revision() const override { return m_Revision; }
		bool IsRefreshing() const override { return m_Refreshing; }
		bool IsError() const override { return false; }
		bool IsStale() const override { return false; }
		void Refresh() override { m_Refreshing = true; }
		bool GetBestUrl(const char **ppUrl) const override
		{
			*ppUrl = nullptr;
			return true;
		}
		int NumServers() const override { return m_vServers.size(); }
		const CServerInfo &Server(int Index) const override
		{
			// 只在结果读取边界注入异常，列表、排序、请求链仍调用生产实现。
			if(Index == m_FailAt)
				throw std::bad_alloc();
			return m_vServers[Index];
		}
	};
	class CPublicationPings : public IServerBrowserPingCache
	{
	public:
		bool m_Fail = false;
		void Load() override {}
		int NumEntries() const override { return 0; }
		void CachePing(const NETADDR &, int) override {}
		int GetPing(const NETADDR *, int) const override
		{
			if(m_Fail)
				throw 7;
			return 42;
		}
	};
	class CPublicationFriends : public CFriends
	{
	public:
		mutable int m_Calls = 0;
		int m_FailAt = 0;
		int GetFriendState(const char *pName, const char *pClan) const override
		{
			if(++m_Calls == m_FailAt)
				throw std::bad_alloc();
			return CFriends::GetFriendState(pName, pClan);
		}
	};
	class CPublicationLogger : public ILogger
	{
	public:
		int m_Messages = 0;
		void Log(const CLogMessage *pMessage) override
		{
			if(str_comp(pMessage->m_aSystem, "serverbrowser") == 0)
			{
				++m_Messages;
				throw std::bad_alloc();
			}
		}
	};
	class ServerBrowserPublication : public ::testing::Test
	{
	protected:
		std::unique_ptr<CConfig> m_pSavedConfig;
		CPublicationFriends m_Friends;
		std::unique_ptr<IFavorites> m_pFavorites = CreateFavorites();
		int64_t m_Now = 100;
		CServerBrowser m_Browser{[this] { return m_Now; }};
		CPublishedList *m_pHttp = nullptr;
		CPublicationPings *m_pPings = nullptr;

		void SetUp() override
		{
			m_pSavedConfig = std::make_unique<CConfig>(g_Config);
			g_Config.m_BrFilterEmpty = g_Config.m_BrFilterFull = g_Config.m_BrFilterPw = 0;
			g_Config.m_BrFilterCountry = g_Config.m_BrFilterFriends = g_Config.m_BrFilterSpectators = 0;
			g_Config.m_BrFilterUnfinishedMap = g_Config.m_BrFilterLogin = g_Config.m_BrFilterConnectingPlayers = 0;
			g_Config.m_QmMapBrowserEmptyOnly = 0;
			g_Config.m_BrFilterServerAddress[0] = g_Config.m_BrFilterGametype[0] = '\0';
			g_Config.m_BrFilterString[0] = g_Config.m_BrExcludeString[0] = '\0';
			g_Config.m_QmCustomCommunitiesUrl[0] = '\0';
			g_Config.m_BrSort = IServerBrowser::SORT_NAME;
			g_Config.m_BrSortOrder = 0;
			g_Config.m_BrMaxRequests = 8;
			g_Config.m_ClFriendsIgnoreClan = 0;
			str_copy(g_Config.m_BrLocation, "auto");
			CServerBrowserTestAccess::Initialize(m_Browser, &m_Friends, m_pFavorites.get());
			m_pHttp = new CPublishedList;
			m_pPings = new CPublicationPings;
			CServerBrowserTestAccess::SetHttp(m_Browser, m_pHttp);
			CServerBrowserTestAccess::SetPingCache(m_Browser, m_pPings);
		}
		void TearDown() override { g_Config = *m_pSavedConfig; }
		CServerInfo Server(const char *pName, int Port, int Players = 1)
		{
			CServerInfo Info{};
			Info.m_NumAddresses = 1;
			EXPECT_EQ(net_addr_from_str(&Info.m_aAddresses[0], "127.0.0.1"), 0);
			Info.m_aAddresses[0].port = Port;
			Info.m_MaxClients = Info.m_MaxPlayers = 16;
			Info.m_NumClients = Info.m_NumPlayers = Players;
			str_copy(Info.m_aName, pName);
			str_copy(Info.m_aGameType, "DDRace");
			str_copy(Info.m_aMap, "Map");
			for(int Index = 0; Index < Players; ++Index)
			{
				CServerInfo::CClient Client{};
				Client.m_Player = true;
				str_format(Client.m_aName, sizeof(Client.m_aName), "Player%d", Index);
				Info.m_vClients.push_back(Client);
			}
			return Info;
		}
		void Publish(std::vector<CServerInfo> vServers)
		{
			m_pHttp->m_vServers = std::move(vServers);
			++m_pHttp->m_Revision;
			m_Browser.Update();
		}
		void Retry()
		{
			m_Now += time_freq();
			m_Browser.Update();
		}
		void LoadCommunities()
		{
			const char *pJson = R"({"communities":[
{"id":"a","name":"Alpha","icon":{"sha256":"0000000000000000000000000000000000000000000000000000000000000000","url":"https://a.test/icon"},"has_finishes":true,"finishes":["Map"],"servers":[{"name":"AA","flagId":1,"servers":{"RaceA":["127.0.0.1:8301"]}}]},
{"id":"b","name":"Beta","icon":{"sha256":"1111111111111111111111111111111111111111111111111111111111111111","url":"https://b.test/icon"},"has_finishes":false,"servers":[{"name":"BB","flagId":2,"servers":{"RaceB":["127.0.0.1:8302"]}}]},
{"id":"c","name":"Gamma","icon":{"sha256":"2222222222222222222222222222222222222222222222222222222222222222","url":"https://c.test/icon"},"has_finishes":true,"finishes":["Other"],"servers":[{"name":"CC","flagId":3,"servers":{"RaceC":["127.0.0.1:8303"]}}]}
]})";
			json_value *pInfo = JsonParse(pJson, str_length(pJson));
			ASSERT_NE(pInfo, nullptr);
			CServerBrowserTestAccess::LoadCommunities(m_Browser, pInfo);
			ASSERT_EQ(m_Browser.Communities().size(), 4U);
			m_Browser.CommunityCache().Update(true);
		}
	};
}

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
