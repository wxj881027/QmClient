#ifndef TEST_SUPPORT_SERVERBROWSER_FIXTURE_H
#define TEST_SUPPORT_SERVERBROWSER_FIXTURE_H

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

namespace ServerBrowserTests
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
		bool m_UnknownFailure = false;
		int GetFriendState(const char *pName, const char *pClan) const override
		{
			if(++m_Calls == m_FailAt)
			{
				if(m_UnknownFailure)
					throw 7;
				throw std::bad_alloc();
			}
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
	class CServerBrowserFixture : public ::testing::Test
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

#endif
