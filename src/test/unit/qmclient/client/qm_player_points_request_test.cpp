#include <game/client/components/qmclient/player_points_request.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace
{
	class CTestRequest final : public IHttpRequest
	{
	public:
		CTestRequest() :
			IHttpRequest("https://example.test/player") {}
		void Header(const char *) override {}
	};

	class CTestHttp final : public IHttp
	{
	public:
		struct SCall
		{
			std::shared_ptr<IHttpRequest> m_pRequest;
			std::string m_Proxy;
			bool m_Aborted;
		};
		std::vector<SCall> m_vCalls;

		void Run(std::shared_ptr<IHttpRequest> pRequest) override
		{
			m_vCalls.push_back({pRequest, pRequest->ProxyUrl(), pRequest->IsAbortRequested()});
		}
		bool HasIpresolveBug() const override { return false; }
	};

	class CQmPlayerPointsRequestTest : public ::testing::Test
	{
	protected:
		std::shared_ptr<CTestRequest> m_pRequest = std::make_shared<CTestRequest>();
		CTestHttp m_Http;
		CQmPlayerPointsRequest m_Request{m_pRequest, 100.0};
	};
}

TEST_F(CQmPlayerPointsRequestTest, PendingSystemProxyDelaysHttpSubmission)
{
	EXPECT_FALSE(m_Request.Poll(m_Http, 100.0, false, nullptr));
	EXPECT_FALSE(m_Request.Submitted());
	EXPECT_TRUE(m_Http.m_vCalls.empty());
}

TEST_F(CQmPlayerPointsRequestTest, ResolvedSystemProxyIsAppliedBeforeOneSubmission)
{
	ASSERT_TRUE(m_Request.Poll(m_Http, 101.0, true, "http://proxy.test:7890"));
	ASSERT_EQ(m_Http.m_vCalls.size(), 1u);
	EXPECT_EQ(m_Http.m_vCalls[0].m_pRequest, m_pRequest);
	EXPECT_EQ(m_Http.m_vCalls[0].m_Proxy, "http://proxy.test:7890");
	EXPECT_FALSE(m_Http.m_vCalls[0].m_Aborted);
	EXPECT_TRUE(m_Request.Submitted());
	m_Request.Poll(m_Http, 102.0, true, "http://other.test:7890");
	EXPECT_EQ(m_Http.m_vCalls.size(), 1u);
	EXPECT_STREQ(m_pRequest->ProxyUrl(), "http://proxy.test:7890");
}

TEST_F(CQmPlayerPointsRequestTest, ExplicitSystemBypassClearsProxyBeforeSubmission)
{
	m_pRequest->Proxy("http://previous.test:7890");
	ASSERT_TRUE(m_Request.Poll(m_Http, 101.0, true, ""));
	ASSERT_EQ(m_Http.m_vCalls.size(), 1u);
	EXPECT_TRUE(m_Http.m_vCalls[0].m_Proxy.empty());
}

TEST_F(CQmPlayerPointsRequestTest, MissingProxyDecisionPreservesRequestConfiguration)
{
	m_pRequest->Proxy("socks5h://configured.test:1080");
	ASSERT_TRUE(m_Request.Poll(m_Http, 101.0, true, nullptr));
	ASSERT_EQ(m_Http.m_vCalls.size(), 1u);
	EXPECT_EQ(m_Http.m_vCalls[0].m_Proxy, "socks5h://configured.test:1080");
}

TEST_F(CQmPlayerPointsRequestTest, ProxyDeadlineFallsBackOnceAndIgnoresLateResult)
{
	EXPECT_FALSE(m_Request.Poll(m_Http, 107.999, false, nullptr));
	EXPECT_TRUE(m_Http.m_vCalls.empty());
	ASSERT_TRUE(m_Request.Poll(m_Http, 108.0, false, nullptr));
	ASSERT_EQ(m_Http.m_vCalls.size(), 1u);
	EXPECT_TRUE(m_Http.m_vCalls[0].m_Proxy.empty());
	m_Request.Poll(m_Http, 109.0, true, "http://late.test:7890");
	EXPECT_EQ(m_Http.m_vCalls.size(), 1u);
	EXPECT_STREQ(m_pRequest->ProxyUrl(), "");
}

TEST_F(CQmPlayerPointsRequestTest, CancelDuringProxyLookupOnlySubmitsAbortedRequest)
{
	m_Request.Cancel(m_Http);
	ASSERT_EQ(m_Http.m_vCalls.size(), 1u);
	EXPECT_TRUE(m_Http.m_vCalls[0].m_Aborted);
	EXPECT_TRUE(m_Http.m_vCalls[0].m_Proxy.empty());
	m_Request.Poll(m_Http, 101.0, true, "http://late.test:7890");
	m_Request.Cancel(m_Http);
	EXPECT_EQ(m_Http.m_vCalls.size(), 1u);
	EXPECT_STREQ(m_pRequest->ProxyUrl(), "");
}

TEST_F(CQmPlayerPointsRequestTest, CancelSubmittedRequestDoesNotSubmitAgain)
{
	ASSERT_TRUE(m_Request.Poll(m_Http, 101.0, true, "http://proxy.test:7890"));
	m_Request.Cancel(m_Http);
	EXPECT_TRUE(m_pRequest->IsAbortRequested());
	m_Request.Poll(m_Http, 102.0, true, "http://late.test:7890");
	EXPECT_EQ(m_Http.m_vCalls.size(), 1u);
}

TEST_F(CQmPlayerPointsRequestTest, NewRequestWaitsForItsOwnProxyAfterCancellation)
{
	m_Request.Cancel(m_Http);
	const auto pNext = std::make_shared<CTestRequest>();
	CQmPlayerPointsRequest Next(pNext, 200.0);
	EXPECT_FALSE(Next.Poll(m_Http, 200.0, false, nullptr));
	EXPECT_EQ(m_Http.m_vCalls.size(), 1u);
	m_Request.Poll(m_Http, 201.0, true, "http://old.test:7890");
	ASSERT_TRUE(Next.Poll(m_Http, 201.0, true, "http://new.test:7890"));
	ASSERT_EQ(m_Http.m_vCalls.size(), 2u);
	EXPECT_EQ(m_Http.m_vCalls[1].m_pRequest, pNext);
	EXPECT_EQ(m_Http.m_vCalls[1].m_Proxy, "http://new.test:7890");
}
