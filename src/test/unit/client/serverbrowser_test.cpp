// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/system.h>

#include <engine/client/friends.h>
#include <engine/client/serverbrowser.h>
#include <engine/client/serverbrowser_http_parse.h>
#include <engine/client/serverbrowser_ping_cache.h>
#include <engine/console.h>
#include <engine/engine.h>
#include <engine/favorites.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/sqlite.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/browser_column_layout.h>
#include <game/client/components/qmclient/browser_friend_list.h>
#include <game/client/components/qmclient/browser_status_layout.h>

#include <gtest/gtest.h>
#include <sqlite3.h>
#include <test/test.h>

#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

TEST(ServerBrowserColumnLayout, ReservesNameAndMapBeforeAllocatingFixedColumns)
{
	const SQmBrowserNameMapLayout Wide = QmBrowserNameMapLayout(680.0f, 210.0f, 120.0f, 90.0f, 0.6f);
	EXPECT_FLOAT_EQ(Wide.m_RightScale, 1.0f);
	EXPECT_FLOAT_EQ(Wide.m_NameWidth, 276.0f);
	EXPECT_FLOAT_EQ(Wide.m_MapWidth, 194.0f);

	const SQmBrowserNameMapLayout Narrow = QmBrowserNameMapLayout(300.0f, 210.0f, 120.0f, 90.0f, 0.6f);
	EXPECT_NEAR(Narrow.m_RightScale, 90.0f / 210.0f, 0.0001f);
	EXPECT_FLOAT_EQ(Narrow.m_NameWidth, 120.0f);
	EXPECT_FLOAT_EQ(Narrow.m_MapWidth, 90.0f);

	const SQmBrowserNameMapLayout Tiny = QmBrowserNameMapLayout(100.0f, 210.0f, 120.0f, 90.0f, 0.6f);
	EXPECT_FLOAT_EQ(Tiny.m_RightScale, 0.0f);
	EXPECT_GE(Tiny.m_NameWidth, 0.0f);
	EXPECT_GE(Tiny.m_MapWidth, 0.0f);
	EXPECT_FLOAT_EQ(Tiny.m_NameWidth + Tiny.m_MapWidth, 100.0f);
}

namespace
{
	void ExpectBrowserRectWithin(const CUIRect &Rect, const CUIRect &Area)
	{
		EXPECT_GE(Rect.w, 0.0f);
		EXPECT_GE(Rect.h, 0.0f);
		EXPECT_GE(Rect.x, Area.x - 0.001f);
		EXPECT_GE(Rect.y, Area.y - 0.001f);
		EXPECT_LE(Rect.x + Rect.w, Area.x + Area.w + 0.001f);
		EXPECT_LE(Rect.y + Rect.h, Area.y + Area.h + 0.001f);
	}
}

TEST(ServerBrowserStatusLayout, StaleNoticeReservesItsOwnHeightAndLeavesControlsUnchanged)
{
	const CUIRect Area{10.0f, 20.0f, 700.0f, 64.0f};
	const auto Normal = QmBrowserStatusLayout(Area, false);
	const auto Stale = QmBrowserStatusLayout(Area, true);
	EXPECT_FLOAT_EQ(Stale.m_SearchInput.h, 20.0f);
	EXPECT_FLOAT_EQ(Stale.m_ExcludeInput.h, 20.0f);
	EXPECT_FLOAT_EQ(Stale.m_AddressInput.h, 20.0f);
	EXPECT_FLOAT_EQ(Stale.m_RefreshButton.h, 24.0f);
	EXPECT_FLOAT_EQ(Stale.m_ConnectButton.h, 24.0f);
	EXPECT_FLOAT_EQ(Stale.m_ConnectButton.y, Normal.m_ConnectButton.y);
	EXPECT_GT(Stale.m_ContentHeight, Normal.m_ContentHeight);
	EXPECT_GE(Stale.m_Notice.y, Stale.m_ConnectButton.y + Stale.m_ConnectButton.h);
	EXPECT_GE(Stale.m_Notice.y, Stale.m_AddressInput.y + Stale.m_AddressInput.h);
}

TEST(ServerBrowserStatusLayout, FreshListDoesNotReserveAnEmptyNoticeRow)
{
	const CUIRect View{20.0f, 30.0f, 800.0f, 400.0f};
	const auto Fresh = QmBrowserPanelLayout(View, false);
	const auto Stale = QmBrowserPanelLayout(View, true);
	const auto FreshContent = QmBrowserStatusLayout(Fresh.m_StatusViewport, false);
	const auto StaleContent = QmBrowserStatusLayout(Stale.m_StatusViewport, true);
	EXPECT_FLOAT_EQ(FreshContent.m_Notice.h, 0.0f);
	EXPECT_FLOAT_EQ(StaleContent.m_Notice.h, 12.0f);
	const float NoticeSpace = StaleContent.m_ContentHeight - FreshContent.m_ContentHeight;
	EXPECT_GT(NoticeSpace, 0.0f);
	EXPECT_FLOAT_EQ(Stale.m_Status.h - Fresh.m_Status.h, NoticeSpace);
	EXPECT_FLOAT_EQ(Fresh.m_List.h - Stale.m_List.h, NoticeSpace);
	EXPECT_FLOAT_EQ(Stale.m_Status.y + Stale.m_Status.h, View.y + View.h);
}

TEST(ServerBrowserPanelLayout, DecreasingHeightShrinksListBeforeStatusContent)
{
	const auto Tall = QmBrowserPanelLayout({8.0f, 16.0f, 960.0f, 500.0f}, true);
	const auto Short = QmBrowserPanelLayout({8.0f, 16.0f, 960.0f, 280.0f}, true);
	EXPECT_FLOAT_EQ(Tall.m_Status.h, Short.m_Status.h);
	EXPECT_FLOAT_EQ(Tall.m_List.h - Short.m_List.h, 220.0f);
	const auto Layout = QmBrowserStatusLayout(Short.m_StatusViewport, true);
	EXPECT_GE(Short.m_StatusViewport.h, Layout.m_ContentHeight);
	EXPECT_GE(Short.m_Status.y, Short.m_List.y + Short.m_List.h);
}

TEST(ServerBrowserPanelLayout, ExtremelyLowViewportClipsContentWithoutCompressingControls)
{
	for(float Height : {0.0f, 1.0f, 20.0f, 60.0f, 80.0f})
	{
		SCOPED_TRACE(Height);
		const CUIRect View{8.0f, 16.0f, 800.0f, Height};
		const auto Panels = QmBrowserPanelLayout(View, true);
		ExpectBrowserRectWithin(Panels.m_List, View);
		ExpectBrowserRectWithin(Panels.m_Status, View);
		ExpectBrowserRectWithin(Panels.m_StatusViewport, Panels.m_Status);
		EXPECT_FLOAT_EQ(Panels.m_List.h, 0.0f);
		const auto Content = QmBrowserStatusLayout(Panels.m_StatusViewport, true);
		EXPECT_GT(Content.m_ContentHeight, Panels.m_StatusViewport.h);
		EXPECT_FLOAT_EQ(Content.m_SearchInput.h, 20.0f);
		EXPECT_FLOAT_EQ(Content.m_RefreshButton.h, 24.0f);
		EXPECT_FLOAT_EQ(Content.m_ConnectButton.h, 24.0f);
	}
}

TEST(ServerBrowserStatusLayout, NarrowContentStacksFieldsFilterAndActionsWithoutOverlap)
{
	const CUIRect Area{40.0f, 50.0f, 180.0f, 40.0f};
	const auto Layout = QmBrowserStatusLayout(Area, true);
	EXPECT_GE(Layout.m_SearchInput.y, Layout.m_SearchLabel.y + Layout.m_SearchLabel.h);
	EXPECT_GE(Layout.m_ExcludeLabel.y, Layout.m_SearchInput.y + Layout.m_SearchInput.h);
	EXPECT_GE(Layout.m_AddressLabel.y, Layout.m_ExcludeInput.y + Layout.m_ExcludeInput.h);
	EXPECT_GE(Layout.m_MapFilter.y, Layout.m_AddressInput.y + Layout.m_AddressInput.h);
	EXPECT_GE(Layout.m_Players.y, Layout.m_MapFilter.y + Layout.m_MapFilter.h);
	EXPECT_GE(Layout.m_RefreshButton.y, Layout.m_Servers.y + Layout.m_Servers.h);
	EXPECT_GE(Layout.m_ConnectButton.x, Layout.m_RefreshButton.x + Layout.m_RefreshButton.w);
}

TEST(ServerBrowserStatusLayout, WidthTransitionsKeepEveryControlWithinMeasuredContent)
{
	for(float Width : {0.0f, 1.0f, 16.0f, 80.0f, 219.0f, 220.0f, 429.0f, 430.0f, 549.0f, 550.0f, 630.0f, 1000.0f})
	{
		SCOPED_TRACE(Width);
		const auto Layout = QmBrowserStatusLayout({11.0f, 23.0f, Width, 1.0f}, true);
		const CUIRect Content{11.0f, 23.0f, Width, Layout.m_ContentHeight};
		for(const CUIRect &Rect : {Layout.m_RefreshBar, Layout.m_SearchLabel, Layout.m_SearchInput,
			    Layout.m_ExcludeLabel, Layout.m_ExcludeInput, Layout.m_AddressLabel, Layout.m_AddressInput,
			    Layout.m_Players, Layout.m_Servers, Layout.m_RefreshButton, Layout.m_ConnectButton,
			    Layout.m_MapFilter, Layout.m_Notice})
			ExpectBrowserRectWithin(Rect, Content);
		EXPECT_FLOAT_EQ(Layout.m_SearchInput.h, 20.0f);
		EXPECT_FLOAT_EQ(Layout.m_ConnectButton.h, 24.0f);
	}
}

TEST(ServerBrowserMapFilterLayout, LongTranslatedLabelCannotDisplaceToggleOrSlider)
{
	for(float Width : {1.0f, 30.0f, 80.0f, 140.0f, 350.0f})
	{
		SCOPED_TRACE(Width);
		const CUIRect Area{10.0f, 20.0f, Width, 64.0f};
		const auto Layout = QmBrowserMapFilterLayout(Area, 1200.0f);
		for(const CUIRect &Rect : {Layout.m_Heading, Layout.m_CurrentLabel, Layout.m_Slider,
			    Layout.m_FavoriteGroup, Layout.m_FavoriteLabel, Layout.m_FavoriteToggle, Layout.m_FavoriteIcon})
			ExpectBrowserRectWithin(Rect, Area);
		EXPECT_GT(Layout.m_Slider.w, 0.0f);
		EXPECT_GT(Layout.m_FavoriteToggle.w, 0.0f);
		EXPECT_GE(Layout.m_FavoriteToggle.x, Layout.m_FavoriteLabel.x + Layout.m_FavoriteLabel.w);
		EXPECT_GE(Layout.m_Slider.x, Layout.m_CurrentLabel.x + Layout.m_CurrentLabel.w);
	}
}

TEST(ServerBrowserPanelLayout, ResizeRecoveryRestoresOriginalContentAndListAllocation)
{
	const CUIRect View{8.0f, 16.0f, 960.0f, 500.0f};
	const auto Original = QmBrowserPanelLayout(View, false);
	const auto Tiny = QmBrowserPanelLayout({8.0f, 16.0f, 180.0f, 40.0f}, true);
	EXPECT_FLOAT_EQ(Tiny.m_List.h, 0.0f);
	EXPECT_GT(Tiny.m_StatusViewport.w, 0.0f);
	const auto Recovered = QmBrowserPanelLayout(View, false);
	EXPECT_FLOAT_EQ(Recovered.m_List.h, Original.m_List.h);
	EXPECT_FLOAT_EQ(Recovered.m_StatusViewport.w, Original.m_StatusViewport.w);
	EXPECT_FLOAT_EQ(Recovered.m_StatusViewport.h, Original.m_StatusViewport.h);
}

TEST(ServerBrowserPanelLayout, InvalidExtentsProduceBoundedEmptyPanels)
{
	for(float Value : {-1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		const auto Layout = QmBrowserPanelLayout({0.0f, 0.0f, Value, Value}, true);
		EXPECT_FLOAT_EQ(Layout.m_List.w, 0.0f);
		EXPECT_FLOAT_EQ(Layout.m_List.h, 0.0f);
		EXPECT_FLOAT_EQ(Layout.m_Status.w, 0.0f);
		EXPECT_FLOAT_EQ(Layout.m_Status.h, 0.0f);
	}
}

class CServerBrowserTestAccess
{
public:
	static void Initialize(CServerBrowser &Browser, IFriends *pFriends, IFavorites *pFavorites)
	{
		Browser.m_pFriends = pFriends;
		Browser.m_pFavorites = pFavorites;
	}
	static void Add(CServerBrowser &Browser, const NETADDR &Address, const CServerInfo &Info)
	{
		Browser.SetInfo(Browser.Add(&Address, 1), Info);
	}
	static void ReplaceFirstAddress(CServerBrowser &Browser, const NETADDR &Address)
	{
		Browser.ReplaceEntry(Browser.m_vpServerlist[0], &Address, 1);
	}
	static void SetFirstInfo(CServerBrowser &Browser, const CServerInfo &Info)
	{
		Browser.SetInfo(Browser.m_vpServerlist[0], Info);
	}
	static void Sort(CServerBrowser &Browser) { Browser.Sort(); }
	static void CleanUp(CServerBrowser &Browser) { Browser.CleanUp(); }
	static bool NeedsResort(const CServerBrowser &Browser) { return Browser.m_NeedResort; }
	static void SetPingCache(CServerBrowser &Browser, IServerBrowserPingCache *pCache)
	{
		Browser.m_pPingCache = pCache;
	}
	static void SetLatency(CServerBrowser &Browser, NETADDR Address, int Latency)
	{
		Browser.SetLatency(Address, Latency);
	}
};

namespace
{
	class CTestPingCache : public IServerBrowserPingCache
	{
		int m_Ping = -1;

	public:
		void Load() override {}
		int NumEntries() const override { return m_Ping < 0 ? 0 : 1; }
		void CachePing(const NETADDR &, int Ping) override { m_Ping = Ping; }
		int GetPing(const NETADDR *, int) const override { return m_Ping; }
	};

	class CCountingFriends : public CFriends
	{
	public:
		mutable int m_Queries = 0;

		int GetFriendState(const char *pName, const char *pClan) const override
		{
			++m_Queries;
			return CFriends::GetFriendState(pName, pClan);
		}
	};

	class CServerBrowserStateTest : public ::testing::Test
	{
	protected:
		std::unique_ptr<CConfig> m_pSavedConfig;
		CCountingFriends m_Friends;
		std::unique_ptr<IFavorites> m_pFavorites = CreateFavorites();
		CServerBrowser m_Browser;

		void SetUp() override
		{
			m_pSavedConfig = std::make_unique<CConfig>(g_Config);
			g_Config.m_BrFilterEmpty = g_Config.m_BrFilterFull = g_Config.m_BrFilterPw = 0;
			g_Config.m_BrFilterCountry = g_Config.m_BrFilterFriends = g_Config.m_BrFilterSpectators = 0;
			g_Config.m_BrFilterUnfinishedMap = g_Config.m_BrFilterLogin = g_Config.m_BrFilterConnectingPlayers = 0;
			g_Config.m_BrFilterServerAddress[0] = g_Config.m_BrFilterGametype[0] = '\0';
			g_Config.m_BrFilterString[0] = g_Config.m_BrExcludeString[0] = '\0';
			g_Config.m_BrSort = IServerBrowser::SORT_NAME;
			g_Config.m_BrSortOrder = 0;
			g_Config.m_ClFriendsIgnoreClan = 0;
			CServerBrowserTestAccess::Initialize(m_Browser, &m_Friends, m_pFavorites.get());
		}

		void TearDown() override { g_Config = *m_pSavedConfig; }

		void AddServer(const NETADDR &Address)
		{
			CServerInfo Info{};
			str_copy(Info.m_aName, "Example");
			Info.m_NumClients = Info.m_NumPlayers = 1;
			Info.m_MaxClients = Info.m_MaxPlayers = 16;
			CServerInfo::CClient Client{};
			str_copy(Client.m_aName, "Alice");
			str_copy(Client.m_aClan, "Clan");
			Client.m_Player = true;
			Info.m_vClients.push_back(Client);
			CServerBrowserTestAccess::Add(m_Browser, Address, Info);
		}

		void AddServer(const char *pAddress, const char *pName, int NumPlayers, int NumClients, std::vector<CServerInfo::CClient> vClients)
		{
			NETADDR Address;
			ASSERT_FALSE(net_addr_from_str(&Address, pAddress));
			CServerInfo Info{};
			str_copy(Info.m_aName, pName);
			Info.m_NumPlayers = NumPlayers;
			Info.m_NumClients = NumClients;
			Info.m_MaxPlayers = Info.m_MaxClients = 16;
			Info.m_vClients = std::move(vClients);
			CServerBrowserTestAccess::Add(m_Browser, Address, Info);
		}

		static CServerInfo::CClient Client(const char *pName, const char *pClan)
		{
			CServerInfo::CClient Client{};
			str_copy(Client.m_aName, pName);
			str_copy(Client.m_aClan, pClan);
			Client.m_Player = true;
			return Client;
		}
	};
}

TEST_F(CServerBrowserStateTest, PlayerCountSortUsesFilteredPopulationAndReversesOrder)
{
	AddServer("127.0.0.1:8303", "Two", 2, 2, {});
	AddServer("127.0.0.1:8304", "Three clients", 1, 3, {});
	AddServer("127.0.0.1:8305", "One", 1, 1, {});
	g_Config.m_BrSort = IServerBrowser::SORT_NUMPLAYERS;
	g_Config.m_BrFilterSpectators = 1;
	CServerBrowserTestAccess::Sort(m_Browser);
	ASSERT_EQ(m_Browser.NumSortedServers(), 3);
	EXPECT_STREQ(m_Browser.SortedGet(0)->m_aName, "Two");
	EXPECT_STREQ(m_Browser.SortedGet(1)->m_aName, "Three clients");

	g_Config.m_BrFilterSpectators = 0;
	CServerBrowserTestAccess::Sort(m_Browser);
	EXPECT_STREQ(m_Browser.SortedGet(0)->m_aName, "Three clients");
	EXPECT_STREQ(m_Browser.SortedGet(1)->m_aName, "Two");
	EXPECT_STREQ(m_Browser.SortedGet(2)->m_aName, "One");
	g_Config.m_BrSortOrder = 1;
	CServerBrowserTestAccess::Sort(m_Browser);
	EXPECT_STREQ(m_Browser.SortedGet(0)->m_aName, "One");
	EXPECT_STREQ(m_Browser.SortedGet(2)->m_aName, "Three clients");
}

TEST_F(CServerBrowserStateTest, FriendCountSortBreaksTiesByPopulationAndRefreshesOnFriendChange)
{
	m_Friends.AddFriend("Alice", "Clan");
	m_Friends.AddFriend("Bob", "Clan");
	AddServer("127.0.0.1:8303", "Few friends", 2, 2, {Client("Alice", "Clan"), Client("Other", "")});
	AddServer("127.0.0.1:8304", "More friends", 2, 2, {Client("Alice", "Clan"), Client("Bob", "Clan")});
	AddServer("127.0.0.1:8305", "Tie with more players", 3, 3, {Client("Alice", "Clan"), Client("Other", ""), Client("Third", "")});
	g_Config.m_BrSort = IServerBrowser::SORT_NUMFRIENDS;
	CServerBrowserTestAccess::Sort(m_Browser);
	ASSERT_EQ(m_Browser.NumSortedServers(), 3);
	EXPECT_STREQ(m_Browser.SortedGet(0)->m_aName, "More friends");
	EXPECT_STREQ(m_Browser.SortedGet(1)->m_aName, "Tie with more players");
	EXPECT_STREQ(m_Browser.SortedGet(2)->m_aName, "Few friends");

	m_Friends.RemoveFriend("Bob", "Clan");
	CServerBrowserTestAccess::Sort(m_Browser);
	EXPECT_STREQ(m_Browser.SortedGet(0)->m_aName, "Tie with more players");
	EXPECT_STREQ(m_Browser.SortedGet(2)->m_aName, "More friends");
}

TEST_F(CServerBrowserStateTest, QmClientCountSortFollowsPushedDistributionInBothDirections)
{
	AddServer("127.0.0.1:8303", "Alpha", 1, 1, {});
	AddServer("127.0.0.1:8304", "Beta", 1, 1, {});
	AddServer("127.0.0.1:8305", "Gamma", 1, 1, {});
	g_Config.m_BrSort = IServerBrowser::SORT_QM_CLIENTS;
	CServerBrowserTestAccess::Sort(m_Browser);
	ASSERT_EQ(m_Browser.NumSortedServers(), 3);
	EXPECT_STREQ(m_Browser.SortedGet(0)->m_aName, "Alpha");

	m_Browser.SetQmClientServerCounts({{"127.0.0.1:8304", 5}, {"127.0.0.1:8305", 2}});
	EXPECT_TRUE(CServerBrowserTestAccess::NeedsResort(m_Browser));
	CServerBrowserTestAccess::Sort(m_Browser);
	EXPECT_STREQ(m_Browser.SortedGet(0)->m_aName, "Beta");
	EXPECT_EQ(m_Browser.SortedGet(0)->m_QmClientCount, 5);
	EXPECT_STREQ(m_Browser.SortedGet(1)->m_aName, "Gamma");
	EXPECT_EQ(m_Browser.SortedGet(1)->m_QmClientCount, 2);
	EXPECT_STREQ(m_Browser.SortedGet(2)->m_aName, "Alpha");
	EXPECT_EQ(m_Browser.SortedGet(2)->m_QmClientCount, 0);

	g_Config.m_BrSortOrder = 1;
	CServerBrowserTestAccess::Sort(m_Browser);
	EXPECT_STREQ(m_Browser.SortedGet(0)->m_aName, "Alpha");
	EXPECT_STREQ(m_Browser.SortedGet(1)->m_aName, "Gamma");
	EXPECT_STREQ(m_Browser.SortedGet(2)->m_aName, "Beta");

	m_Browser.SetQmClientServerCounts({{"127.0.0.1:8303", 7}, {"127.0.0.1:8304", 1}});
	EXPECT_TRUE(CServerBrowserTestAccess::NeedsResort(m_Browser));
	CServerBrowserTestAccess::Sort(m_Browser);
	EXPECT_STREQ(m_Browser.SortedGet(0)->m_aName, "Gamma");
	EXPECT_STREQ(m_Browser.SortedGet(1)->m_aName, "Beta");
	EXPECT_STREQ(m_Browser.SortedGet(2)->m_aName, "Alpha");
	EXPECT_EQ(m_Browser.SortedGet(2)->m_QmClientCount, 7);
}

TEST_F(CServerBrowserStateTest, QmClientCountsStayCurrentWhileSortingByName)
{
	AddServer("127.0.0.1:8303", "Alpha", 1, 1, {});
	AddServer("127.0.0.1:8304", "Beta", 1, 1, {});
	CServerBrowserTestAccess::Sort(m_Browser);

	m_Browser.SetQmClientServerCounts({{"127.0.0.1:8304", 7}});
	EXPECT_FALSE(CServerBrowserTestAccess::NeedsResort(m_Browser));
	ASSERT_EQ(m_Browser.NumSortedServers(), 2);
	EXPECT_STREQ(m_Browser.SortedGet(0)->m_aName, "Alpha");
	EXPECT_EQ(m_Browser.SortedGet(0)->m_QmClientCount, 0);
	EXPECT_EQ(m_Browser.SortedGet(1)->m_QmClientCount, 7);

	// 服务器信息的计数不能覆盖游戏层推送的分布。
	CServerInfo Updated = *m_Browser.Get(0);
	Updated.m_QmClientCount = 99;
	CServerBrowserTestAccess::SetFirstInfo(m_Browser, Updated);
	EXPECT_EQ(m_Browser.Get(0)->m_QmClientCount, 0);
	m_Browser.SetQmClientServerCounts({});
	EXPECT_EQ(m_Browser.SortedGet(1)->m_QmClientCount, 0);
	EXPECT_FALSE(CServerBrowserTestAccess::NeedsResort(m_Browser));
}

TEST_F(CServerBrowserStateTest, QmClientCountsApplyBeforeFirstServerAndAfterListReload)
{
	m_Browser.SetQmClientServerCounts({{"127.0.0.1:8304", 3}});
	AddServer("127.0.0.1:8303", "Alpha", 1, 1, {});
	AddServer("127.0.0.1:8304", "Beta", 1, 1, {});
	g_Config.m_BrSort = IServerBrowser::SORT_QM_CLIENTS;
	CServerBrowserTestAccess::Sort(m_Browser);
	ASSERT_EQ(m_Browser.NumSortedServers(), 2);
	EXPECT_STREQ(m_Browser.SortedGet(0)->m_aName, "Beta");
	EXPECT_EQ(m_Browser.SortedGet(0)->m_QmClientCount, 3);

	CServerBrowserTestAccess::CleanUp(m_Browser);
	EXPECT_EQ(m_Browser.NumServers(), 0);
	AddServer("127.0.0.1:8303", "Alpha", 1, 1, {});
	AddServer("127.0.0.1:8304", "Beta", 1, 1, {});
	CServerBrowserTestAccess::Sort(m_Browser);
	ASSERT_EQ(m_Browser.NumSortedServers(), 2);
	EXPECT_STREQ(m_Browser.SortedGet(0)->m_aName, "Beta");
	EXPECT_EQ(m_Browser.SortedGet(0)->m_QmClientCount, 3);
	EXPECT_STREQ(m_Browser.SortedGet(1)->m_aName, "Alpha");
	EXPECT_EQ(m_Browser.SortedGet(1)->m_QmClientCount, 0);
}

TEST_F(CServerBrowserStateTest, QmClientCountFollowsReplacedServerAddress)
{
	AddServer("127.0.0.1:8303", "Alpha", 1, 1, {});
	m_Browser.SetQmClientServerCounts({{"127.0.0.1:8303", 4}, {"127.0.0.1:8304", 9}});
	ASSERT_EQ(m_Browser.Get(0)->m_QmClientCount, 4);

	NETADDR OriginalAddress;
	ASSERT_FALSE(net_addr_from_str(&OriginalAddress, "127.0.0.1:8303"));
	NETADDR Address;
	ASSERT_FALSE(net_addr_from_str(&Address, "127.0.0.1:8304"));
	CServerBrowserTestAccess::ReplaceFirstAddress(m_Browser, Address);
	EXPECT_EQ(m_Browser.Get(0)->m_QmClientCount, 9);
	EXPECT_EQ(m_Browser.Find(OriginalAddress), nullptr);
	EXPECT_NE(m_Browser.Find(Address), nullptr);

	ASSERT_FALSE(net_addr_from_str(&Address, "127.0.0.1:8305"));
	CServerBrowserTestAccess::ReplaceFirstAddress(m_Browser, Address);
	EXPECT_EQ(m_Browser.Get(0)->m_QmClientCount, 0);
	EXPECT_NE(m_Browser.Find(Address), nullptr);
}

TEST_F(CServerBrowserStateTest, FriendListMovesOnlineEntriesAndKeepsServerSnapshots)
{
	ASSERT_TRUE(m_Friends.AddCategory("Team"));
	m_Friends.AddFriend("Alice", "Clan", "Team");
	m_Friends.AddFriend("Bob", "Clan");
	m_Friends.AddFriend("", "Guild");
	CQmBrowserFriendList List;
	List.Update(m_Friends, m_Browser, false);
	const int Offline = m_Friends.FindCategory(IFriends::OFFLINE_CATEGORY);
	ASSERT_EQ(List.Groups()[Offline].size(), 2u);
	EXPECT_EQ(List.Groups()[Offline][0].ServerInfo(), nullptr);

	AddServer("127.0.0.1:8303", "Old server", 2, 2, {Client("Alice", "Clan"), Client("Member", "Guild")});
	CServerBrowserTestAccess::Sort(m_Browser);
	List.Update(m_Friends, m_Browser, false);
	const int Team = m_Friends.FindCategory("Team");
	const int Clan = m_Friends.FindCategory(IFriends::CLAN_MEMBERS_CATEGORY);
	ASSERT_EQ(List.Groups()[Team].size(), 1u);
	EXPECT_STREQ(List.Groups()[Team][0].Name(), "Alice");
	ASSERT_NE(List.Groups()[Team][0].ServerInfo(), nullptr);
	EXPECT_STREQ(List.Groups()[Team][0].ServerInfo()->m_aName, "Old server");
	ASSERT_EQ(List.Groups()[Clan].size(), 1u);
	EXPECT_EQ(List.Groups()[Clan][0].FriendState(), IFriends::FRIEND_CLAN);
	ASSERT_EQ(List.Groups()[Offline].size(), 1u);
	EXPECT_STREQ(List.Groups()[Offline][0].Name(), "Bob");

	const CServerInfo *pPreviousSnapshot = List.Groups()[Team][0].ServerInfo();
	CServerInfo Updated = *m_Browser.Get(0);
	str_copy(Updated.m_aName, "New server");
	CServerBrowserTestAccess::SetFirstInfo(m_Browser, Updated);
	EXPECT_STREQ(pPreviousSnapshot->m_aName, "Old server");
	CServerBrowserTestAccess::Sort(m_Browser);
	List.Update(m_Friends, m_Browser, false);
	EXPECT_STREQ(List.Groups()[Team][0].ServerInfo()->m_aName, "New server");

	ASSERT_TRUE(m_Friends.SetFriendCategory("Alice", "Clan", IFriends::DEFAULT_CATEGORY));
	List.Update(m_Friends, m_Browser, false);
	EXPECT_TRUE(List.Groups()[Team].empty());
	const int Default = m_Friends.FindCategory(IFriends::DEFAULT_CATEGORY);
	ASSERT_EQ(List.Groups()[Default].size(), 1u);
	EXPECT_STREQ(List.Groups()[Default][0].Name(), "Alice");
}

TEST_F(CServerBrowserStateTest, FriendListRefreshesLatencyAfterPingUpdate)
{
	m_Friends.AddFriend("Alice", "Clan");
	AddServer("127.0.0.1:8303", "Server", 1, 1, {Client("Alice", "Clan")});
	CServerBrowserTestAccess::Sort(m_Browser);
	CQmBrowserFriendList List;
	List.Update(m_Friends, m_Browser, false);
	const int Default = m_Friends.FindCategory(IFriends::DEFAULT_CATEGORY);
	ASSERT_EQ(List.Groups()[Default].size(), 1u);
	const int OldLatency = List.Groups()[Default][0].ServerInfo()->m_Latency;

	CServerBrowserTestAccess::SetPingCache(m_Browser, new CTestPingCache());
	NETADDR Address;
	ASSERT_FALSE(net_addr_from_str(&Address, "127.0.0.1:8303"));
	CServerBrowserTestAccess::SetLatency(m_Browser, Address, 42);
	List.Update(m_Friends, m_Browser, false);
	ASSERT_EQ(List.Groups()[Default].size(), 1u);
	EXPECT_NE(OldLatency, 42);
	EXPECT_EQ(List.Groups()[Default][0].ServerInfo()->m_Latency, 42);
}

TEST_F(CServerBrowserStateTest, FriendListIgnoreClanMovesMatchedNameOutOfOffline)
{
	m_Friends.AddFriend("Alice", "OldClan");
	AddServer("127.0.0.1:8303", "Server", 1, 1, {Client("Alice", "NewClan")});
	CQmBrowserFriendList List;
	CServerBrowserTestAccess::Sort(m_Browser);
	List.Update(m_Friends, m_Browser, false);
	const int Offline = m_Friends.FindCategory(IFriends::OFFLINE_CATEGORY);
	ASSERT_EQ(List.Groups()[Offline].size(), 1u);
	EXPECT_EQ(List.Groups()[Offline][0].ServerInfo(), nullptr);

	g_Config.m_ClFriendsIgnoreClan = 1;
	CServerBrowserTestAccess::Sort(m_Browser);
	List.Update(m_Friends, m_Browser, true);
	EXPECT_TRUE(List.Groups()[Offline].empty());
	const int Default = m_Friends.FindCategory(IFriends::DEFAULT_CATEGORY);
	ASSERT_EQ(List.Groups()[Default].size(), 1u);
	EXPECT_STREQ(List.Groups()[Default][0].Clan(), "NewClan");
	EXPECT_NE(List.Groups()[Default][0].ServerInfo(), nullptr);
}

TEST_F(CServerBrowserStateTest, FriendStateIsReusedUntilFriendRevisionOrClanModeChanges)
{
	NETADDR Address;
	ASSERT_FALSE(net_addr_from_str(&Address, "127.0.0.1:8303"));
	AddServer(Address);
	CServerBrowserTestAccess::Sort(m_Browser);
	ASSERT_EQ(m_Friends.m_Queries, 1);
	EXPECT_EQ(m_Browser.Get(0)->m_FriendState, IFriends::FRIEND_NO);

	CServerBrowserTestAccess::Sort(m_Browser);
	EXPECT_EQ(m_Friends.m_Queries, 1);
	m_Friends.AddFriend("Alice", "Clan");
	CServerBrowserTestAccess::Sort(m_Browser);
	EXPECT_EQ(m_Friends.m_Queries, 2);
	EXPECT_EQ(m_Browser.Get(0)->m_FriendState, IFriends::FRIEND_PLAYER);

	g_Config.m_ClFriendsIgnoreClan = 1;
	CServerBrowserTestAccess::Sort(m_Browser);
	EXPECT_EQ(m_Friends.m_Queries, 3);

	CServerInfo Updated = *m_Browser.Get(0);
	str_copy(Updated.m_vClients[0].m_aName, "Bob");
	CServerBrowserTestAccess::SetFirstInfo(m_Browser, Updated);
	CServerBrowserTestAccess::Sort(m_Browser);
	EXPECT_EQ(m_Friends.m_Queries, 4);
	EXPECT_EQ(m_Browser.Get(0)->m_FriendState, IFriends::FRIEND_NO);
}

TEST_F(CServerBrowserStateTest, AddressReplacementInvalidatesFriendStateAndFriendListRevision)
{
	NETADDR Address;
	ASSERT_FALSE(net_addr_from_str(&Address, "127.0.0.1:8303"));
	const uint64_t EmptyRevision = m_Browser.FriendListRevision();
	AddServer(Address);
	EXPECT_NE(m_Browser.FriendListRevision(), EmptyRevision);
	CServerBrowserTestAccess::Sort(m_Browser);
	ASSERT_EQ(m_Friends.m_Queries, 1);

	const uint64_t LoadedRevision = m_Browser.FriendListRevision();
	ASSERT_FALSE(net_addr_from_str(&Address, "127.0.0.1:8304"));
	CServerBrowserTestAccess::ReplaceFirstAddress(m_Browser, Address);
	EXPECT_NE(m_Browser.FriendListRevision(), LoadedRevision);
	ASSERT_NE(m_Browser.Find(Address), nullptr);
	CServerBrowserTestAccess::Sort(m_Browser);
	EXPECT_EQ(m_Friends.m_Queries, 2);
	EXPECT_EQ(m_Browser.Get(0)->m_aAddresses[0], Address);
}

TEST_F(CServerBrowserStateTest, UpdatingExistingServerInfoInvalidatesFriendListSnapshot)
{
	NETADDR Address;
	ASSERT_FALSE(net_addr_from_str(&Address, "127.0.0.1:8303"));
	AddServer(Address);
	const uint64_t Revision = m_Browser.FriendListRevision();
	CServerInfo Updated = m_Browser.Find(Address)->m_Info;
	str_copy(Updated.m_aName, "Updated server");
	CServerBrowserTestAccess::SetFirstInfo(m_Browser, Updated);
	EXPECT_NE(m_Browser.FriendListRevision(), Revision);
	EXPECT_STREQ(m_Browser.Find(Address)->m_Info.m_aName, "Updated server");
}

TEST(ServerBrowser, PingCache)
{
	CTestInfo Info;
	Info.m_DeleteTestStorageFilesOnSuccess = true;

	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr) << "Error creating test storage";
	auto pPingCache = std::unique_ptr<IServerBrowserPingCache>(CreateServerBrowserPingCache(pConsole.get(), pStorage.get()));

	NETADDR Localhost4, Localhost6, OtherLocalhost4, OtherLocalhost6;
	ASSERT_FALSE(net_addr_from_str(&Localhost4, "127.0.0.1:8303"));
	ASSERT_FALSE(net_addr_from_str(&Localhost6, "[::1]:8304"));
	ASSERT_FALSE(net_addr_from_str(&OtherLocalhost4, "127.0.0.1:8305"));
	ASSERT_FALSE(net_addr_from_str(&OtherLocalhost6, "[::1]:8306"));
	EXPECT_LT(net_addr_comp(&Localhost4, &Localhost6), 0);
	NETADDR aLocalhostBoth[2] = {Localhost4, Localhost6};

	EXPECT_EQ(pPingCache->NumEntries(), 0);
	EXPECT_EQ(pPingCache->GetPing(&Localhost4, 1), -1);
	EXPECT_EQ(pPingCache->GetPing(&Localhost6, 1), -1);
	EXPECT_EQ(pPingCache->GetPing(aLocalhostBoth, 2), -1);
	EXPECT_EQ(pPingCache->GetPing(&OtherLocalhost4, 1), -1);
	EXPECT_EQ(pPingCache->GetPing(&OtherLocalhost6, 1), -1);

	pPingCache->Load();

	EXPECT_EQ(pPingCache->NumEntries(), 0);
	EXPECT_EQ(pPingCache->GetPing(&Localhost4, 1), -1);
	EXPECT_EQ(pPingCache->GetPing(&Localhost6, 1), -1);
	EXPECT_EQ(pPingCache->GetPing(aLocalhostBoth, 2), -1);
	EXPECT_EQ(pPingCache->GetPing(&OtherLocalhost4, 1), -1);
	EXPECT_EQ(pPingCache->GetPing(&OtherLocalhost6, 1), -1);

	// Newer pings overwrite older.
	pPingCache->CachePing(Localhost4, 123);
	pPingCache->CachePing(Localhost4, 234);
	pPingCache->CachePing(Localhost4, 345);
	pPingCache->CachePing(Localhost4, 456);
	pPingCache->CachePing(Localhost4, 567);
	pPingCache->CachePing(Localhost4, 678);
	pPingCache->CachePing(Localhost4, 789);
	pPingCache->CachePing(Localhost4, 890);
	pPingCache->CachePing(Localhost4, 901);
	pPingCache->CachePing(Localhost4, 135);
	pPingCache->CachePing(Localhost4, 246);
	pPingCache->CachePing(Localhost4, 357);
	pPingCache->CachePing(Localhost4, 468);
	pPingCache->CachePing(Localhost4, 579);
	pPingCache->CachePing(Localhost4, 680);
	pPingCache->CachePing(Localhost4, 791);
	pPingCache->CachePing(Localhost4, 802);
	pPingCache->CachePing(Localhost4, 913);

	EXPECT_EQ(pPingCache->NumEntries(), 1);
	EXPECT_EQ(pPingCache->GetPing(&Localhost4, 1), 913);
	EXPECT_EQ(pPingCache->GetPing(&Localhost6, 1), -1);
	EXPECT_EQ(pPingCache->GetPing(aLocalhostBoth, 2), 913);
	EXPECT_EQ(pPingCache->GetPing(&OtherLocalhost4, 1), 913);
	EXPECT_EQ(pPingCache->GetPing(&OtherLocalhost6, 1), -1);

	pPingCache->CachePing(Localhost4, 234);
	pPingCache->CachePing(Localhost6, 345);
	EXPECT_EQ(pPingCache->NumEntries(), 2);
	EXPECT_EQ(pPingCache->GetPing(&Localhost4, 1), 234);
	EXPECT_EQ(pPingCache->GetPing(&Localhost6, 1), 345);
	EXPECT_EQ(pPingCache->GetPing(aLocalhostBoth, 2), 234);
	EXPECT_EQ(pPingCache->GetPing(&OtherLocalhost4, 1), 234);
	EXPECT_EQ(pPingCache->GetPing(&OtherLocalhost6, 1), 345);

	// Port doesn't matter for overwriting.
	pPingCache->CachePing(Localhost4, 1337);
	EXPECT_EQ(pPingCache->NumEntries(), 2);
	EXPECT_EQ(pPingCache->GetPing(&Localhost4, 1), 1337);
	EXPECT_EQ(pPingCache->GetPing(&Localhost6, 1), 345);
	EXPECT_EQ(pPingCache->GetPing(aLocalhostBoth, 2), 345);
	EXPECT_EQ(pPingCache->GetPing(&OtherLocalhost4, 1), 1337);
	EXPECT_EQ(pPingCache->GetPing(&OtherLocalhost6, 1), 345);

	pPingCache.reset(CreateServerBrowserPingCache(pConsole.get(), pStorage.get()));

	// Persistence.
	pPingCache->Load();
	EXPECT_EQ(pPingCache->NumEntries(), 2);
	EXPECT_EQ(pPingCache->GetPing(&Localhost4, 1), 1337);
	EXPECT_EQ(pPingCache->GetPing(&Localhost6, 1), 345);
	EXPECT_EQ(pPingCache->GetPing(aLocalhostBoth, 2), 345);
	EXPECT_EQ(pPingCache->GetPing(&OtherLocalhost4, 1), 1337);
	EXPECT_EQ(pPingCache->GetPing(&OtherLocalhost6, 1), 345);

	// 重复加载必须复位读取语句，周期性缓存刷新不能把 SQLITE_DONE 当作失败。
	pPingCache->Load();
	EXPECT_EQ(pPingCache->NumEntries(), 2);
	EXPECT_EQ(pPingCache->GetPing(&Localhost4, 1), 1337);
	EXPECT_EQ(pPingCache->GetPing(&Localhost6, 1), 345);
}

TEST(ServerBrowser, PingCacheIgnoresExpiredEntries)
{
	// 过期缓存值不能当实测延迟用：延迟列应回落到地区估算，而不是显示很久以前的测量值。
	CTestInfo Info;
	Info.m_DeleteTestStorageFilesOnSuccess = true;

	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr) << "Error creating test storage";

	NETADDR OldAddr, FreshAddr;
	ASSERT_FALSE(net_addr_from_str(&OldAddr, "127.0.0.1:8303"));
	ASSERT_FALSE(net_addr_from_str(&FreshAddr, "127.0.0.2:8303"));

	// 直接写入缓存数据库：一条 30 天前的记录和一条刚写入的记录。
	{
		CSqlite pDisk = SqliteOpen(pConsole.get(), pStorage.get(), "ddnet-cache.sqlite3");
		ASSERT_TRUE(pDisk) << "Error opening ping cache database";
		ASSERT_EQ(sqlite3_exec(pDisk.get(), "CREATE TABLE IF NOT EXISTS server_pings (ip_address TEXT PRIMARY KEY NOT NULL, ping INTEGER NOT NULL, utc_timestamp TEXT NOT NULL)", nullptr, nullptr, nullptr), SQLITE_OK);
		ASSERT_EQ(sqlite3_exec(pDisk.get(), "INSERT OR REPLACE INTO server_pings (ip_address, ping, utc_timestamp) VALUES ('127.0.0.1', 171, datetime('now', '-30 days')), ('127.0.0.2', 123, datetime('now'))", nullptr, nullptr, nullptr), SQLITE_OK);
	}

	const int OldMaxAgeHours = g_Config.m_QmPingCacheMaxAgeHours;
	g_Config.m_QmPingCacheMaxAgeHours = 72;

	auto pPingCache = std::unique_ptr<IServerBrowserPingCache>(CreateServerBrowserPingCache(pConsole.get(), pStorage.get()));
	pPingCache->Load();
	EXPECT_EQ(pPingCache->NumEntries(), 2);
	EXPECT_EQ(pPingCache->GetPing(&OldAddr, 1), -1);
	EXPECT_EQ(pPingCache->GetPing(&FreshAddr, 1), 123);

	// 0 表示永不过期：旧记录重新可见。
	g_Config.m_QmPingCacheMaxAgeHours = 0;
	EXPECT_EQ(pPingCache->GetPing(&OldAddr, 1), 171);

	g_Config.m_QmPingCacheMaxAgeHours = OldMaxAgeHours;
}

namespace
{
	std::string HttpListEntry(const char *pAddresses, const char *pName = "Example")
	{
		return std::string("{\"addresses\":") + pAddresses + R"(,"location":"eu","info":{"max_clients":16,"max_players":16,"passworded":false,"game_type":"DDRace","name":")" + pName + R"(","map":{"name":"Map"},"version":"0.6","clients":[]}})";
	}

	bool ParseHttpListForTest(const std::string &Text, std::vector<CServerInfo> &vServers)
	{
		json_value *pJson = JsonParse(Text.c_str(), Text.size());
		const bool Failed = ServerBrowserParseHttpList(pJson, &vServers);
		json_value_free(pJson);
		return Failed;
	}
}

TEST(ServerBrowserHttpParse, PreservesAddressPreferenceAndSkipsUnsupportedServers)
{
	std::vector<CServerInfo> vServers;
	const std::string Text = "{\"servers\":[" +
				 HttpListEntry(R"(["tw-0.7+udp://127.0.0.1:8303","tw-0.6+udp://127.0.0.1:8304"])", "Mixed") + "," +
				 HttpListEntry(R"(["invalid://127.0.0.1:8303"])") + "," +
				 HttpListEntry(R"(["tw-0.7+udp://127.0.0.1:8305"])", "Seven") + "]}";
	ASSERT_FALSE(ParseHttpListForTest(Text, vServers));
	ASSERT_EQ(vServers.size(), 2u);
	EXPECT_STREQ(vServers[0].m_aName, "Mixed");
	EXPECT_EQ(vServers[0].m_NumAddresses, 1);
	EXPECT_EQ(vServers[0].m_aAddresses[0].port, 8304);
	EXPECT_STREQ(vServers[1].m_aName, "Seven");
	EXPECT_EQ(vServers[1].m_aAddresses[0].port, 8305);
}

TEST(ServerBrowserHttpParse, InvalidResponsePreservesPublishedList)
{
	std::vector<CServerInfo> vServers(1);
	str_copy(vServers[0].m_aName, "Old list");
	for(const std::string &Text : {std::string("not json"), std::string("{}"),
		    "{\"servers\":[" + HttpListEntry(R"(["tw-0.6+udp://127.0.0.1:8303"])") + R"(,{"addresses":false,"info":{}}]})"})
	{
		EXPECT_TRUE(ParseHttpListForTest(Text, vServers));
		ASSERT_EQ(vServers.size(), 1u);
		EXPECT_STREQ(vServers[0].m_aName, "Old list");
	}
}

TEST(ServerBrowserHttpParse, EmptySuccessReplacesOldListAndSkipsInvalidInfo)
{
	std::vector<CServerInfo> vServers(1);
	EXPECT_FALSE(ParseHttpListForTest(R"({"servers":[]})", vServers));
	EXPECT_TRUE(vServers.empty());
	const std::string Text = R"({"servers":[{"addresses":["tw-0.6+udp://127.0.0.1:8303"],"info":{}},)" +
				 HttpListEntry(R"(["tw-0.6+udp://127.0.0.1:8304"])") + "]}";
	ASSERT_FALSE(ParseHttpListForTest(Text, vServers));
	ASSERT_EQ(vServers.size(), 1u);
	EXPECT_EQ(vServers[0].m_aAddresses[0].port, 8304);
}
