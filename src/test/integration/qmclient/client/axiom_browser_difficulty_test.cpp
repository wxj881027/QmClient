#include <base/time.h>

#include <engine/serverbrowser.h>

#include <game/client/components/qmclient/axiom_scores_data.h>
#include <game/client/components/qmclient/map_difficulty_catalog.h>

#include <gtest/gtest.h>
#include <test/support/map_http_recorder.h>

#include <functional>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace
{
	class CInterruptibleHttp : public MapHttpTest::CHttpRecorder
	{
	public:
		std::function<void()> m_OnNextRun;

		void Run(std::shared_ptr<IHttpRequest> pRequest) override
		{
			if(auto OnRun = std::exchange(m_OnNextRun, {}))
				OnRun();
			CHttpRecorder::Run(std::move(pRequest));
		}
	};

	class CHeaderFailureResponse : public MapHttpTest::CResponse
	{
	public:
		using CResponse::CResponse;
		void Header(const char *) override { throw std::bad_alloc(); }
	};

	std::unique_ptr<IHttpRequest> CreateHeaderFailureResponse(const char *pUrl)
	{
		return std::make_unique<CHeaderFailureResponse>(pUrl);
	}

	class AxiomBrowserDifficulty : public ::testing::Test
	{
	protected:
		CServerInfo m_Server{};
		CInterruptibleHttp m_Http;
		CQmMapDifficultyCatalog m_Catalog{MapHttpTest::CreateResponse};
		CQmAxiomRequestBudget m_Budget;
		int64_t m_Now = time_freq();

		void SetUp() override
		{
			str_copy(m_Server.m_aCommunityId, "axiom");
			str_copy(m_Server.m_aGameType, "Gores");
			str_copy(m_Server.m_aMap, "Two Words");
		}
		void Request(const char *pKey = "")
		{
			m_Catalog.RequestAxiomDifficulty(m_Server, m_Http, m_Budget, pKey, m_Now);
		}
		bool Poll() { return m_Catalog.PollAxiomRequests(m_Budget, m_Now); }
	};
}

TEST_F(AxiomBrowserDifficulty, RoomsShareRequestAndPersistCategoryAndStars)
{
	Request();
	str_copy(m_Server.m_aName, "Second room");
	Request();
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	const auto &pRequest = m_Http.m_vRequests.front();
	EXPECT_STREQ(pRequest->Url(), "https://api.axiom.teeworlds.cn/v1/open/map/info?mode=gores&map=Two%20Words");
	EXPECT_TRUE(pRequest->m_Header.empty());
	pRequest->Finish(R"({"code":200,"data":{"map":"Two Words","stars":2,"difficulty":"Easy"}})");
	ASSERT_TRUE(Poll());
	CQmMapDifficultyCatalog Restored;
	const std::string Json = m_Catalog.SerializeAxiomCache();
	ASSERT_TRUE(Restored.LoadAxiomCacheJson(Json.data(), Json.size()));
	const auto *pEntry = Restored.FindForServer(m_Server);
	ASSERT_NE(pEntry, nullptr);
	EXPECT_EQ(pEntry->m_Category, "Easy");
	EXPECT_STREQ(pEntry->StarLabel(), "★★☆");
	Restored.RequestAxiomDifficulty(m_Server, m_Http, m_Budget, "", m_Now);
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
}

TEST_F(AxiomBrowserDifficulty, SameNameUsesSeparateModesAndCommunity)
{
	Request();
	str_copy(m_Server.m_aGameType, "AXRace");
	Request("example");
	ASSERT_EQ(m_Http.m_vRequests.size(), 2u);
	EXPECT_STREQ(m_Http.m_vRequests[1]->Url(), "https://api.axiom.teeworlds.cn/v1/open/map/info?mode=axrace&map=Two%20Words");
	EXPECT_EQ(m_Http.m_vRequests[1]->m_Header, "Authorization: Bearer example");
	m_Http.m_vRequests[1]->Finish(R"({"code":200,"data":{"map":"Two Words","stars":5,"difficulty":"Solo"}})");
	ASSERT_TRUE(Poll());
	ASSERT_NE(m_Catalog.FindForServer(m_Server), nullptr);
	EXPECT_STREQ(m_Catalog.FindForServer(m_Server)->StarLabel(), "★★★★★");
	str_copy(m_Server.m_aGameType, "Gores");
	EXPECT_EQ(m_Catalog.FindForServer(m_Server), nullptr);
	str_copy(m_Server.m_aCommunityId, "kog");
	str_copy(m_Server.m_aMap, "Other");
	Request();
	EXPECT_EQ(m_Http.m_vRequests.size(), 2u);
}

TEST_F(AxiomBrowserDifficulty, InvalidResponsesLeaveMapUnknownAndDelayRetry)
{
	for(const char *pJson : {
		    R"({"code":200,"data":{"map":"Other","stars":2,"difficulty":"Easy"}})",
		    R"({"code":200,"data":{"map":"Two Words","stars":4,"difficulty":"Easy"}})",
		    R"({"code":200,"data":{"map":"Two Words","stars":2}})"})
	{
		SCOPED_TRACE(pJson);
		const auto Previous = m_Http.m_vRequests.size();
		Request();
		ASSERT_EQ(m_Http.m_vRequests.size(), Previous + 1);
		m_Http.m_vRequests.back()->Finish(pJson);
		EXPECT_FALSE(Poll());
		EXPECT_EQ(m_Catalog.FindForServer(m_Server), nullptr);
		Request();
		EXPECT_EQ(m_Http.m_vRequests.size(), Previous + 1);
		m_Now += 31 * time_freq();
	}
}

TEST_F(AxiomBrowserDifficulty, SharedLimitBlocksLookupAndRateLimitDelaysOtherConsumers)
{
	for(int i = 0; i < 3; ++i)
		ASSERT_TRUE(m_Budget.TryStart(m_Now, false));
	Request();
	EXPECT_TRUE(m_Http.m_vRequests.empty());
	m_Now += time_freq();
	Request();
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	m_Http.m_vRequests[0]->Finish("{}", 429);
	EXPECT_FALSE(Poll());
	EXPECT_FALSE(m_Budget.TryStart(m_Now, true));
	m_Now += time_freq();
	Request();
	ASSERT_EQ(m_Http.m_vRequests.size(), 2u);
}

TEST_F(AxiomBrowserDifficulty, CancelledLookupCannotPublishItsLateResponse)
{
	Request();
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	m_Catalog.CancelAxiomRequests();
	EXPECT_TRUE(m_Http.m_vRequests[0]->m_Aborted);
	m_Http.m_vRequests[0]->Finish(R"({"code":200,"data":{"map":"Two Words","stars":2,"difficulty":"Easy"}})");
	EXPECT_FALSE(Poll());
	EXPECT_EQ(m_Catalog.FindForServer(m_Server), nullptr);
}

TEST_F(AxiomBrowserDifficulty, HeaderAllocationFailureKeepsExistingRequestAndConsumesBudget)
{
	CQmMapDifficultyCatalog Catalog(CreateHeaderFailureResponse);
	Catalog.RequestAxiomDifficulty(m_Server, m_Http, m_Budget, "", m_Now);
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	const CServerInfo ExistingServer = m_Server;
	str_copy(m_Server.m_aMap, "Other Map");
	EXPECT_NO_THROW(Catalog.RequestAxiomDifficulty(m_Server, m_Http, m_Budget, "example", m_Now));
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	EXPECT_TRUE(m_Budget.TryStart(m_Now, false));
	EXPECT_FALSE(m_Budget.TryStart(m_Now, false));
	m_Http.m_vRequests[0]->Finish(R"({"code":200,"data":{"map":"Two Words","stars":2,"difficulty":"Easy"}})");
	EXPECT_TRUE(Catalog.PollAxiomRequests(m_Budget, m_Now));
	EXPECT_NE(Catalog.FindForServer(ExistingServer), nullptr);
	EXPECT_EQ(Catalog.FindForServer(m_Server), nullptr);
}

TEST_F(AxiomBrowserDifficulty, SubmissionResourceFailureReleasesLookupSlot)
{
	m_Http.m_OnNextRun = [] { throw std::length_error("test queue capacity"); };
	EXPECT_NO_THROW(Request());
	EXPECT_TRUE(m_Http.m_vRequests.empty());
	Request();
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	m_Http.m_vRequests[0]->Finish(R"({"code":200,"data":{"map":"Two Words","stars":2,"difficulty":"Easy"}})");
	EXPECT_TRUE(Poll());
}

TEST_F(AxiomBrowserDifficulty, FailedSubmissionKeepsReplacementFromReentrantRequest)
{
	m_Http.m_OnNextRun = [this] {
		m_Catalog.CancelAxiomRequests();
		Request();
		throw std::bad_alloc();
	};
	EXPECT_NO_THROW(Request());
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	m_Http.m_vRequests[0]->Finish(R"({"code":200,"data":{"map":"Two Words","stars":2,"difficulty":"Easy"}})");
	EXPECT_TRUE(Poll());
	EXPECT_NE(m_Catalog.FindForServer(m_Server), nullptr);
}

TEST_F(AxiomBrowserDifficulty, NotFoundRetryDelayBeginsAtResponseCompletion)
{
	Request();
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	m_Now += 10 * time_freq();
	m_Http.m_vRequests[0]->Finish("{}", 404);
	EXPECT_FALSE(Poll());

	m_Now += 600 * time_freq() - 1;
	Request();
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
	++m_Now;
	Request();
	EXPECT_EQ(m_Http.m_vRequests.size(), 2u);
}
