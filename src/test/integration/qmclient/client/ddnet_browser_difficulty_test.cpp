#include <base/time.h>

#include <engine/serverbrowser.h>

#include <game/client/components/qmclient/map_difficulty_catalog.h>

#include <gtest/gtest.h>
#include <test/support/map_http_recorder.h>

#include <memory>
#include <stdexcept>
#include <utility>

namespace
{
	class DdnetBrowserDifficulty : public ::testing::Test
	{
	protected:
		CServerInfo m_Server{};
		MapHttpTest::CHttpRecorder m_Http;
		CQmMapDifficultyCatalog m_Catalog{MapHttpTest::CreateResponse};
		int64_t m_Now = time_freq();

		void SetUp() override
		{
			str_copy(m_Server.m_aCommunityId, "ddnet");
			str_copy(m_Server.m_aGameType, "DDNet");
			str_copy(m_Server.m_aMap, "New & Map");
		}
		void Request() { m_Catalog.RequestDdnetDifficulty(m_Server, m_Http, m_Now); }
		bool Poll() { return m_Catalog.PollDdnetRequests(m_Now); }
	};
}

TEST_F(DdnetBrowserDifficulty, MissingMapIsFetchedOnceAndRestoredFromCache)
{
	EXPECT_EQ(m_Catalog.FindForServer(m_Server), nullptr);
	Request();
	m_Now += time_freq();
	str_copy(m_Server.m_aName, "Another room");
	Request();
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	EXPECT_STREQ(m_Http.m_vRequests[0]->Url(), "https://ddnet.org/maps/?json=New%20%26%20Map");
	EXPECT_TRUE(m_Http.m_vRequests[0]->m_Header.empty());
	const auto Revision = m_Catalog.Revision();
	m_Http.m_vRequests[0]->Finish(R"({"name":"New & Map","type":"Brutal","difficulty":4})");
	ASSERT_TRUE(Poll());
	EXPECT_GT(m_Catalog.Revision(), Revision);
	EXPECT_TRUE(m_Catalog.DdnetCacheDirty());
	const auto *pEntry = m_Catalog.FindForServer(m_Server);
	ASSERT_NE(pEntry, nullptr);
	EXPECT_EQ(pEntry->m_Category, "Brutal");
	EXPECT_STREQ(pEntry->StarLabel(), "★★★★☆");

	CQmMapDifficultyCatalog Restored(MapHttpTest::CreateResponse);
	const auto Json = m_Catalog.SerializeDdnetCache();
	ASSERT_TRUE(Restored.LoadDdnetCacheJson(Json.data(), Json.size()));
	str_copy(m_Server.m_aMap, "new & map");
	ASSERT_NE(Restored.FindForServer(m_Server), nullptr);
	EXPECT_EQ(Restored.FindForServer(m_Server)->m_Stars, 4);
	Restored.RequestDdnetDifficulty(m_Server, m_Http, m_Now);
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
}

TEST_F(DdnetBrowserDifficulty, BundledSqlRemainsAnImmediateOfflineSource)
{
	constexpr char aSql[] = "CREATE TABLE map_difficulty(map_name,category,stars);"
				"INSERT INTO map_difficulty VALUES('New & Map','Novice',2);";
	ASSERT_TRUE(m_Catalog.LoadSqlText(aSql, sizeof(aSql) - 1));
	Request();
	EXPECT_TRUE(m_Http.m_vRequests.empty());
	ASSERT_NE(m_Catalog.FindForServer(m_Server), nullptr);
	EXPECT_EQ(m_Catalog.FindForServer(m_Server)->m_Stars, 2);
}

TEST_F(DdnetBrowserDifficulty, UnrelatedCommunitiesAndModesDoNotQueryDdnet)
{
	const struct
	{
		const char *m_pCommunity;
		const char *m_pGameType;
		const char *m_pCommunityType;
	} aCases[] = {
		{"axiom", "DDNet", "Other"},
		{"kog", "DDRace", "Novice"},
		{"none", "DDNet", "Gores"},
		{"ddnet", "Gores", "Novice"},
		{"none", "DM", "None"},
	};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_pCommunity);
		str_copy(m_Server.m_aCommunityId, Case.m_pCommunity);
		str_copy(m_Server.m_aGameType, Case.m_pGameType);
		str_copy(m_Server.m_aCommunityType, Case.m_pCommunityType);
		Request();
		EXPECT_TRUE(m_Http.m_vRequests.empty());
		m_Now += time_freq();
	}
}

TEST_F(DdnetBrowserDifficulty, InvalidResponsesLeaveMapUnknownAndDelayRetry)
{
	for(const char *pJson : {
		    R"({"name":"Other","type":"Brutal","difficulty":4})",
		    R"({"name":"New & Map","type":"Brutal","difficulty":6})",
		    R"({"name":"New & Map","type":"Brutal","difficulty":-1})",
		    R"({"name":"New & Map","type":"Brutal","difficulty":"4"})",
		    R"({"name":"New & Map","type":"Brutal\u0000Other","difficulty":4})",
		    R"({"name":"New & Map","difficulty":4})",
		    "null"})
	{
		SCOPED_TRACE(pJson);
		const auto Previous = m_Http.m_vRequests.size();
		Request();
		ASSERT_EQ(m_Http.m_vRequests.size(), Previous + 1);
		m_Http.m_vRequests.back()->Finish(pJson);
		EXPECT_FALSE(Poll());
		EXPECT_EQ(m_Catalog.FindForServer(m_Server), nullptr);
		EXPECT_FALSE(m_Catalog.DdnetCacheDirty());
		m_Now += 2 * time_freq();
		Request();
		EXPECT_EQ(m_Http.m_vRequests.size(), Previous + 1);
		m_Now += 30 * time_freq();
	}
}

TEST_F(DdnetBrowserDifficulty, LookupsArePacedAndHaveBoundedConcurrency)
{
	Request();
	str_copy(m_Server.m_aMap, "Second");
	Request();
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	m_Now += time_freq();
	Request();
	ASSERT_EQ(m_Http.m_vRequests.size(), 2u);
	str_copy(m_Server.m_aMap, "Third");
	m_Now += time_freq();
	Request();
	EXPECT_EQ(m_Http.m_vRequests.size(), 2u);
	m_Http.m_vRequests[0]->Fail();
	EXPECT_FALSE(Poll());
	Request();
	EXPECT_EQ(m_Http.m_vRequests.size(), 3u);
}

TEST_F(DdnetBrowserDifficulty, MissingMapCanBeRetriedAfterNegativeCacheExpires)
{
	Request();
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	m_Http.m_vRequests[0]->Finish("{}", 404);
	EXPECT_FALSE(Poll());
	m_Now += 599 * time_freq();
	Request();
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
	m_Now += time_freq();
	Request();
	ASSERT_EQ(m_Http.m_vRequests.size(), 2u);
	m_Http.m_vRequests[1]->Finish(R"({"name":"New & Map","type":"Race","difficulty":0})");
	ASSERT_TRUE(Poll());
	ASSERT_NE(m_Catalog.FindForServer(m_Server), nullptr);
	EXPECT_EQ(m_Catalog.FindForServer(m_Server)->m_Stars, 0);
}

TEST_F(DdnetBrowserDifficulty, RateLimitPausesRequestsForOtherMaps)
{
	Request();
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	m_Http.m_vRequests[0]->Finish("{}", 429);
	EXPECT_FALSE(Poll());
	str_copy(m_Server.m_aMap, "Second");
	m_Now += 59 * time_freq();
	Request();
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
	m_Now += time_freq();
	Request();
	EXPECT_EQ(m_Http.m_vRequests.size(), 2u);
}

TEST_F(DdnetBrowserDifficulty, CancelledRequestCannotPublishAndCanBeRequestedAgain)
{
	Request();
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	m_Catalog.CancelDdnetRequests();
	EXPECT_TRUE(m_Http.m_vRequests[0]->m_Aborted);
	m_Http.m_vRequests[0]->Finish(R"({"name":"New & Map","type":"Brutal","difficulty":4})");
	EXPECT_FALSE(Poll());
	EXPECT_EQ(m_Catalog.FindForServer(m_Server), nullptr);
	m_Now += time_freq();
	Request();
	EXPECT_EQ(m_Http.m_vRequests.size(), 2u);
}

TEST_F(DdnetBrowserDifficulty, FailedSubmissionAbortsRequestAndAllowsRetry)
{
	class CRejectingHttp : public MapHttpTest::CHttpRecorder
	{
	public:
		std::shared_ptr<MapHttpTest::CResponse> m_pRejected;
		void Run(std::shared_ptr<IHttpRequest> pRequest) override
		{
			if(!m_pRejected)
			{
				m_pRejected = std::static_pointer_cast<MapHttpTest::CResponse>(pRequest);
				throw std::length_error("test queue capacity");
			}
			CHttpRecorder::Run(std::move(pRequest));
		}
	} Http;
	EXPECT_NO_THROW(m_Catalog.RequestDdnetDifficulty(m_Server, Http, m_Now));
	ASSERT_NE(Http.m_pRejected, nullptr);
	EXPECT_TRUE(Http.m_pRejected->m_Aborted);
	EXPECT_TRUE(Http.m_vRequests.empty());
	m_Now += time_freq();
	m_Catalog.RequestDdnetDifficulty(m_Server, Http, m_Now);
	ASSERT_EQ(Http.m_vRequests.size(), 1u);
	Http.m_vRequests[0]->Finish(R"({"name":"New & Map","type":"Brutal","difficulty":4})");
	ASSERT_TRUE(Poll());
	EXPECT_NE(m_Catalog.FindForServer(m_Server), nullptr);
}
