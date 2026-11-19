#include <engine/shared/config.h>

#include <gtest/gtest.h>
#include <test/support/serverbrowser_test_access.h>

#include <memory>
#include <string>

namespace
{
	class ServerBrowserCommunityLoad : public ::testing::Test
	{
	protected:
		std::unique_ptr<CConfig> m_pSavedConfig;
		CServerBrowser m_Browser;
		void SetUp() override
		{
			m_pSavedConfig = std::make_unique<CConfig>(g_Config);
			g_Config.m_QmCustomCommunitiesUrl[0] = '\0';
		}
		void TearDown() override { g_Config = *m_pSavedConfig; }
		void Load(bool HasFinishes, const std::string &Fields, const std::string &IconFields = {})
		{
			const std::string Json = std::string(R"({"communities":[{"id":"custom","name":"Custom","has_finishes":)") +
						 (HasFinishes ? "true" : "false") +
						 R"(,"icon":{"sha256":"0000000000000000000000000000000000000000000000000000000000000000","url":"https://custom.test/icon")" +
						 IconFields + "}" + Fields + "}]}";
			json_value *pInfo = JsonParse(Json.c_str(), Json.size());
			ASSERT_NE(pInfo, nullptr);
			CServerBrowserTestAccess::LoadCommunities(m_Browser, pInfo);
			ASSERT_NE(m_Browser.Community("custom"), nullptr);
		}
	};
}

TEST_F(ServerBrowserCommunityLoad, MissingFinishesDoesNotReplaceValidTopLevelServers)
{
	ASSERT_NO_FATAL_FAILURE(Load(false, R"(,"servers":[{"name":"Current","flagId":1,"servers":{"Race":["127.0.0.1:8301"]}}])"));
	const auto *pCommunity = m_Browser.Community("custom");
	EXPECT_FALSE(pCommunity->HasRanks());
	ASSERT_EQ(pCommunity->Countries().size(), 1U);
	EXPECT_STREQ(pCommunity->Countries()[0].Name(), "Current");
	ASSERT_EQ(pCommunity->Countries()[0].Servers().size(), 1U);
	EXPECT_EQ(pCommunity->Countries()[0].Servers()[0].Address().port, 8301);
}

TEST_F(ServerBrowserCommunityLoad, LegacyIconFieldsRestoreServersAndFinishedMaps)
{
	ASSERT_NO_FATAL_FAILURE(Load(true, "", R"(,"finishes":["Legacy"],"servers":[{"name":"Legacy","flagId":2,"servers":{"LegacyRace":["127.0.0.1:8302"]}}])"));
	const auto *pCommunity = m_Browser.Community("custom");
	EXPECT_EQ(pCommunity->HasRank("Legacy"), CServerInfo::RANK_RANKED);
	EXPECT_EQ(pCommunity->HasRank("Other"), CServerInfo::RANK_UNRANKED);
	ASSERT_EQ(pCommunity->Countries().size(), 1U);
	ASSERT_EQ(pCommunity->Countries()[0].Servers().size(), 1U);
	EXPECT_EQ(pCommunity->Countries()[0].Servers()[0].Address().port, 8302);
}

TEST_F(ServerBrowserCommunityLoad, TopLevelFieldsTakePriorityOverLegacyIconFields)
{
	ASSERT_NO_FATAL_FAILURE(Load(true,
		R"(,"finishes":["Current"],"servers":[{"name":"Current","flagId":1,"servers":{"Race":["127.0.0.1:8301"]}}])",
		R"(,"finishes":["Legacy"],"servers":[{"name":"Legacy","flagId":2,"servers":{"LegacyRace":["127.0.0.1:8302"]}}])"));
	const auto *pCommunity = m_Browser.Community("custom");
	EXPECT_EQ(pCommunity->HasRank("Current"), CServerInfo::RANK_RANKED);
	EXPECT_EQ(pCommunity->HasRank("Legacy"), CServerInfo::RANK_UNRANKED);
	ASSERT_EQ(pCommunity->Countries().size(), 1U);
	ASSERT_EQ(pCommunity->Countries()[0].Servers().size(), 1U);
	EXPECT_EQ(pCommunity->Countries()[0].Servers()[0].Address().port, 8301);
}
