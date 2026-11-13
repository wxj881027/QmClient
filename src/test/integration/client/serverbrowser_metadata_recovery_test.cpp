#include <engine/storage.h>

#include <test/support/serverbrowser_fixture.h>
#include <test/test.h>

namespace
{
	class ServerBrowserMetadataRecovery : public ServerBrowserTests::CServerBrowserFixture
	{
	protected:
		CTestInfo m_TestInfo;
		std::unique_ptr<IStorage> m_pStorage;
		void SetUp() override
		{
			CServerBrowserFixture::SetUp();
			m_pStorage = m_TestInfo.CreateTestStorage();
			CServerBrowserTestAccess::SetStorage(m_Browser, m_pStorage.get());
		}
		std::string Info(const char *pId, const char *pLocation = "eu", const char *pMap = "Map")
		{
			return std::string(R"({"location":")") + pLocation + R"(","communities":[{"id":")" + pId +
			       R"(","name":"Community","icon":{"sha256":"0000000000000000000000000000000000000000000000000000000000000000","url":"https://community.test/icon"},"has_finishes":true,"finishes":[")" + pMap +
			       R"("],"servers":[{"name":"Country","flagId":1,"servers":{"Race":["127.0.0.1:8301"]}}]}]})";
		}
		void Write(const std::string &Json)
		{
			IOHANDLE File = m_pStorage->OpenFile(DDNET_INFO_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Json.data(), Json.size()), Json.size());
			EXPECT_EQ(io_close(File), 0);
		}
		void LoadInitial()
		{
			ASSERT_NO_FATAL_FAILURE(Write(Info("old")));
			ASSERT_NE(m_Browser.LoadDDNetInfo(), nullptr);
			Publish({Server("Visible", 8301)});
			m_Browser.CommunityCache().Update(true);
		}
	};
}

TEST_F(ServerBrowserMetadataRecovery, MalformedJsonKeepsPublishedInfoAndBorrowedResources)
{
	ASSERT_NO_FATAL_FAILURE(LoadInitial());
	const auto *pInfo = CServerBrowserTestAccess::Info(m_Browser);
	const auto Digest = m_Browser.DDNetInfoSha256();
	const auto *pCommunity = m_Browser.Community("old");
	const auto *pCountry = m_Browser.CommunityCache().SelectableCountries()[0];
	m_Browser.FavoriteCommunitiesFilter().Add("old");
	ASSERT_NO_FATAL_FAILURE(Write("{invalid"));
	EXPECT_EQ(m_Browser.LoadDDNetInfo(), nullptr);
	EXPECT_EQ(CServerBrowserTestAccess::Info(m_Browser), pInfo);
	EXPECT_EQ(m_Browser.DDNetInfoSha256(), Digest);
	EXPECT_EQ(m_Browser.Community("old"), pCommunity);
	EXPECT_EQ(m_Browser.CommunityCache().SelectableCountries()[0], pCountry);
	EXPECT_TRUE(m_Browser.FavoriteCommunitiesFilter().Filtered("old"));
	EXPECT_STREQ(m_Browser.Get(0)->m_aCommunityId, "old");
	EXPECT_TRUE(m_Browser.DDNetInfoAvailable());
}

TEST_F(ServerBrowserMetadataRecovery, NonObjectRootDoesNotConsumeDigestAndCanBeReplaced)
{
	ASSERT_NO_FATAL_FAILURE(LoadInitial());
	const auto Digest = m_Browser.DDNetInfoSha256();
	ASSERT_NO_FATAL_FAILURE(Write("[]"));
	EXPECT_EQ(m_Browser.LoadDDNetInfo(), nullptr);
	EXPECT_EQ(m_Browser.DDNetInfoSha256(), Digest);
	ASSERT_NO_FATAL_FAILURE(Write(Info("new")));
	ASSERT_NE(m_Browser.LoadDDNetInfo(), nullptr);
	EXPECT_NE(m_Browser.DDNetInfoSha256(), Digest);
	EXPECT_EQ(m_Browser.Community("old"), nullptr);
	ASSERT_NE(m_Browser.Community("new"), nullptr);
	EXPECT_STREQ(m_Browser.Get(0)->m_aCommunityId, "new");
}

TEST_F(ServerBrowserMetadataRecovery, FirstInvalidLoadRemainsUnavailableAndLaterValidLoadRecovers)
{
	ASSERT_NO_FATAL_FAILURE(Write("null"));
	EXPECT_EQ(m_Browser.LoadDDNetInfo(), nullptr);
	EXPECT_FALSE(m_Browser.DDNetInfoAvailable());
	EXPECT_FALSE(m_Browser.DDNetInfoSha256().has_value());
	EXPECT_TRUE(m_Browser.CommunityCache().SelectableCountries().empty());
	ASSERT_NO_FATAL_FAILURE(Write(Info("new")));
	ASSERT_NE(m_Browser.LoadDDNetInfo(), nullptr);
	EXPECT_TRUE(m_Browser.DDNetInfoAvailable());
	EXPECT_TRUE(m_Browser.DDNetInfoSha256().has_value());
	ASSERT_NE(m_Browser.Community("new"), nullptr);
}

TEST_F(ServerBrowserMetadataRecovery, CustomSourceExceptionKeepsOldStateAndRetriesTheSamePayload)
{
	ASSERT_NO_FATAL_FAILURE(LoadInitial());
	const auto Digest = m_Browser.DDNetInfoSha256();
	const auto *pCommunity = m_Browser.Community("old");
	CServerBrowserTestAccess::SetCustomSource(m_Browser, [](auto &) { throw 7; });
	ASSERT_NO_FATAL_FAILURE(Write(Info("new")));
	EXPECT_NO_THROW(EXPECT_EQ(m_Browser.LoadDDNetInfo(), nullptr));
	EXPECT_EQ(m_Browser.Community("old"), pCommunity);
	EXPECT_EQ(m_Browser.DDNetInfoSha256(), Digest);
	EXPECT_STREQ(m_Browser.Get(0)->m_aCommunityId, "old");
	CServerBrowserTestAccess::SetCustomSource(m_Browser, nullptr);
	ASSERT_NE(m_Browser.LoadDDNetInfo(), nullptr);
	EXPECT_NE(m_Browser.DDNetInfoSha256(), Digest);
	EXPECT_STREQ(m_Browser.Get(0)->m_aCommunityId, "new");
}

TEST_F(ServerBrowserMetadataRecovery, LatencyFailureDoesNotPublishPreparedMetadataOrCleanFilters)
{
	ASSERT_NO_FATAL_FAILURE(LoadInitial());
	m_Browser.FavoriteCommunitiesFilter().Add("old");
	m_Browser.Refresh(IServerBrowser::TYPE_FAVORITE_COMMUNITY_1, true);
	m_Browser.CommunityCache().Update(true);
	const auto *pVisible = m_Browser.Get(0);
	const auto Digest = m_Browser.DDNetInfoSha256();
	const int Location = m_Browser.GetCurrentClientLocation();
	m_pPings->m_Fail = true;
	ASSERT_NO_FATAL_FAILURE(Write(Info("new", "as", "Other")));
	EXPECT_NO_THROW(EXPECT_EQ(m_Browser.LoadDDNetInfo(), nullptr));
	EXPECT_EQ(m_Browser.Get(0), pVisible);
	EXPECT_EQ(m_Browser.GetCurrentType(), IServerBrowser::TYPE_FAVORITE_COMMUNITY_1);
	EXPECT_TRUE(m_Browser.FavoriteCommunitiesFilter().Filtered("old"));
	EXPECT_EQ(m_Browser.GetCurrentClientLocation(), Location);
	EXPECT_EQ(m_Browser.DDNetInfoSha256(), Digest);
	EXPECT_STREQ(m_Browser.CommunityCache().CountryTypeFilterKey(), "old");
	EXPECT_EQ(m_Browser.Get(0)->m_HasRank, CServerInfo::RANK_RANKED);
	m_pPings->m_Fail = false;
	ASSERT_NE(m_Browser.LoadDDNetInfo(), nullptr);
	EXPECT_EQ(m_Browser.Get(0), pVisible);
	EXPECT_EQ(m_Browser.GetCurrentType(), IServerBrowser::TYPE_INTERNET);
	EXPECT_TRUE(m_Browser.FavoriteCommunitiesFilter().Empty());
	EXPECT_STREQ(m_Browser.Get(0)->m_aCommunityId, "new");
	EXPECT_EQ(m_Browser.Get(0)->m_HasRank, CServerInfo::RANK_UNRANKED);
	EXPECT_NE(m_Browser.GetCurrentClientLocation(), Location);
	EXPECT_STREQ(m_Browser.CommunityCache().CountryTypeFilterKey(), IServerBrowser::COMMUNITY_ALL);
}

TEST_F(ServerBrowserMetadataRecovery, SuccessfulReloadPublishesNewCacheBeforeReturning)
{
	ASSERT_NO_FATAL_FAILURE(LoadInitial());
	ASSERT_NO_FATAL_FAILURE(Write(Info("new")));
	ASSERT_NE(m_Browser.LoadDDNetInfo(), nullptr);
	const auto &Cache = m_Browser.CommunityCache();
	ASSERT_EQ(Cache.SelectedCommunities().size(), 2U);
	EXPECT_EQ(Cache.SelectedCommunities()[0], m_Browser.Community("new"));
	EXPECT_EQ(Cache.SelectableCountries()[0], &m_Browser.Community("new")->Countries()[0]);
	EXPECT_EQ(Cache.SelectableTypes()[0], &m_Browser.Community("new")->Types()[0]);
	EXPECT_TRUE(Cache.AnyRanksAvailable());
	EXPECT_TRUE(CServerBrowserTestAccess::NeedsResort(m_Browser));
}

TEST_F(ServerBrowserMetadataRecovery, MissingFileRetainsThePreviousJson)
{
	ASSERT_NO_FATAL_FAILURE(LoadInitial());
	const auto *pInfo = CServerBrowserTestAccess::Info(m_Browser);
	const auto Digest = m_Browser.DDNetInfoSha256();
	ASSERT_TRUE(m_pStorage->RemoveFile(DDNET_INFO_FILE, IStorage::TYPE_SAVE));
	EXPECT_EQ(m_Browser.LoadDDNetInfo(), pInfo);
	EXPECT_EQ(m_Browser.DDNetInfoSha256(), Digest);
	EXPECT_STREQ(m_Browser.Get(0)->m_aCommunityId, "old");
}

TEST_F(ServerBrowserMetadataRecovery, CustomCallbackSeesThePreviousPublishedState)
{
	ASSERT_NO_FATAL_FAILURE(LoadInitial());
	const auto *pInfo = CServerBrowserTestAccess::Info(m_Browser);
	const auto *pCommunity = m_Browser.Community("old");
	int Calls = 0;
	CServerBrowserTestAccess::SetCustomSource(m_Browser, [&](auto &) {
		++Calls;
		EXPECT_EQ(CServerBrowserTestAccess::Info(m_Browser), pInfo);
		EXPECT_EQ(m_Browser.Community("old"), pCommunity);
		EXPECT_EQ(m_Browser.Community("new"), nullptr);
	});
	ASSERT_NO_FATAL_FAILURE(Write(Info("new")));
	ASSERT_NE(m_Browser.LoadDDNetInfo(), nullptr);
	EXPECT_EQ(Calls, 1);
	EXPECT_EQ(m_Browser.Community("old"), nullptr);
}

TEST_F(ServerBrowserMetadataRecovery, CustomReloadRetriesAfterCooldownAndKeepsVisibleRows)
{
	ASSERT_NO_FATAL_FAILURE(LoadInitial());
	const auto *pVisible = m_Browser.SortedGet(0);
	const auto *pCommunity = m_Browser.Community("old");
	bool Fail = true;
	int Calls = 0;
	CServerBrowserTestAccess::SetCustomSource(m_Browser, [&](auto &) {
		++Calls;
		if(Fail)
			throw std::bad_alloc();
	});
	EXPECT_FALSE(m_Browser.LoadDDNetServers());
	EXPECT_TRUE(m_Browser.IsServerlistError());
	EXPECT_EQ(m_Browser.Community("old"), pCommunity);
	EXPECT_EQ(m_Browser.SortedGet(0), pVisible);
	Fail = false;
	m_Browser.Update();
	EXPECT_EQ(Calls, 1);
	Retry();
	EXPECT_EQ(Calls, 2);
	EXPECT_FALSE(m_Browser.IsServerlistError());
	EXPECT_EQ(m_Browser.SortedGet(0), pVisible);
	EXPECT_STREQ(m_Browser.Get(0)->m_aCommunityId, "old");
}

TEST_F(ServerBrowserMetadataRecovery, ExplicitCustomReloadCanRecoverBeforeTheRetryDeadline)
{
	ASSERT_NO_FATAL_FAILURE(LoadInitial());
	CServerBrowserTestAccess::SetCustomSource(m_Browser, [](auto &) { throw 7; });
	EXPECT_FALSE(m_Browser.LoadDDNetServers());
	CServerBrowserTestAccess::SetCustomSource(m_Browser, nullptr);
	EXPECT_TRUE(m_Browser.LoadDDNetServers());
	EXPECT_EQ(m_Now, 100);
	EXPECT_FALSE(m_Browser.IsServerlistError());
}

TEST_F(ServerBrowserMetadataRecovery, InvalidationDropsBorrowedPointersAndRebuildsOnDemand)
{
	ASSERT_NO_FATAL_FAILURE(LoadInitial());
	auto &Cache = static_cast<CCommunityCache &>(m_Browser.CommunityCache());
	Cache.Invalidate();
	EXPECT_TRUE(Cache.SelectedCommunities().empty());
	EXPECT_TRUE(Cache.SelectableCountries().empty());
	EXPECT_TRUE(Cache.SelectableTypes().empty());
	EXPECT_FALSE(Cache.AnyRanksAvailable());
	Cache.Update(false);
	ASSERT_EQ(Cache.SelectedCommunities().size(), 2U);
	EXPECT_EQ(Cache.SelectableCountries()[0], &m_Browser.Community("old")->Countries()[0]);
}

namespace
{
	// 在缓存的浏览器查询边界注入失败；选取社区仍交给真实浏览器实现。
	class CFailingCommunityBrowser : public CServerBrowser
	{
	public:
		bool m_FailSelection = false;
		mutable int m_Selections = 0;
		std::vector<const CCommunity *> CurrentCommunities() const override
		{
			++m_Selections;
			if(m_FailSelection)
				throw std::bad_alloc();
			return CServerBrowser::CurrentCommunities();
		}
	};
}

TEST(ServerBrowserCommunityCacheRecovery, FailedForcedUpdateKeepsCacheAndNextNormalUpdateRetries)
{
	CFailingCommunityBrowser Browser;
	ASSERT_TRUE(Browser.LoadDDNetServers());
	Browser.CommunityCache().Update(true);
	ASSERT_FALSE(Browser.CommunityCache().SelectedCommunities().empty());
	ASSERT_EQ(Browser.m_Selections, 1);
	const auto *pSelected = Browser.CommunityCache().SelectedCommunities().data();
	Browser.m_FailSelection = true;
	EXPECT_NO_THROW(Browser.CommunityCache().Update(true));
	EXPECT_EQ(Browser.m_Selections, 2);
	EXPECT_EQ(Browser.CommunityCache().SelectedCommunities().data(), pSelected);
	Browser.m_FailSelection = false;
	Browser.CommunityCache().Update(false);
	EXPECT_EQ(Browser.m_Selections, 3);
	EXPECT_EQ(Browser.CommunityCache().SelectedCommunities()[0], Browser.Community(IServerBrowser::COMMUNITY_NONE));
}

TEST_F(ServerBrowserMetadataRecovery, LateFailurePreservesAllFilterLists)
{
	ASSERT_NO_FATAL_FAILURE(LoadInitial());
	m_Browser.FavoriteCommunitiesFilter().Add("old");
	m_Browser.CommunitiesFilter().Add("unknown");
	m_Browser.CountriesFilter().Add(IServerBrowser::COMMUNITY_ALL, "Country");
	m_Browser.TypesFilter().Add(IServerBrowser::COMMUNITY_ALL, "Race");
	m_pPings->m_Fail = true;
	ASSERT_NO_FATAL_FAILURE(Write(Info("new", "as")));
	EXPECT_EQ(m_Browser.LoadDDNetInfo(), nullptr);
	EXPECT_TRUE(m_Browser.FavoriteCommunitiesFilter().Filtered("old"));
	EXPECT_TRUE(m_Browser.CommunitiesFilter().Filtered("unknown"));
	EXPECT_TRUE(m_Browser.CountriesFilter().Filtered("Country"));
	EXPECT_TRUE(m_Browser.TypesFilter().Filtered("Race"));
	m_pPings->m_Fail = false;
	ASSERT_NE(m_Browser.LoadDDNetInfo(), nullptr);
	EXPECT_TRUE(m_Browser.FavoriteCommunitiesFilter().Empty());
	EXPECT_FALSE(m_Browser.CommunitiesFilter().Filtered("unknown"));
	EXPECT_TRUE(m_Browser.CountriesFilter().Filtered("Country"));
	EXPECT_TRUE(m_Browser.TypesFilter().Filtered("Race"));
}
