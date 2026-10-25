#include <game/client/components/qmclient/translate/translate_probe.h>

#include <test/support/translate_http_test_support.h>

#include <new>

namespace
{
	struct SRequestState
	{
		bool m_FailFactory = false;
		bool m_FailHeader = false;
		int m_Created = 0;
		int m_Destroyed = 0;
	};
	thread_local SRequestState *s_pState = nullptr;
	class CRequestScope
	{
		SRequestState *m_pPrevious;

	public:
		explicit CRequestScope(SRequestState &State) :
			m_pPrevious(s_pState) { s_pState = &State; }
		~CRequestScope() { s_pState = m_pPrevious; }
	};

	// 仅替换请求创建和配置边界；探测、回退及owner均使用生产实现。
	class CRequest : public CTranslateTestRequest
	{
		SRequestState &m_State;

	public:
		CRequest(const char *pUrl, SRequestState &State) :
			CTranslateTestRequest(pUrl), m_State(State) { ++m_State.m_Created; }
		~CRequest() override { ++m_State.m_Destroyed; }
		void Header(const char *pHeader) override
		{
			if(m_State.m_FailHeader)
				throw std::bad_alloc();
			CTranslateTestRequest::Header(pHeader);
		}
	};
	std::unique_ptr<IHttpRequest> CreateRequest(const char *pUrl)
	{
		if(s_pState->m_FailFactory)
			throw std::bad_alloc();
		return std::make_unique<CRequest>(pUrl, *s_pState);
	}
}

class CTranslateProbeMemoryTest : public CTranslateBackendTest
{
protected:
	SRequestState m_State;
	CRequestScope m_Scope{m_State};
	CTranslateTestHttp m_Transport;
	void SetUp() override
	{
		CTranslateBackendTest::SetUp();
		// 显式Chat的成功不写格式记忆；回退测试也只发布失败结果。
		str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "https://probe-allocation.test/v1/chat/completions");
	}
	bool Start(CTranslateProbe &Probe) { return Probe.Start(m_Transport, "hello", "zh", "en", CreateRequest); }
	bool StartFallback(CTranslateProbe &Probe)
	{
		str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "https://probe-allocation.test/v1");
		if(!Start(Probe) || m_Transport.m_vSubmissions.size() != 1)
			return false;
		m_Transport.m_vSubmissions[0].m_pRequest->Finish("{}", 404);
		return true;
	}
};

TEST_F(CTranslateProbeMemoryTest, CreationAllocationFailureLeavesNoPendingOwnerAndAllowsRetry)
{
	CTranslateProbe Probe;
	m_State.m_FailFactory = true;
	EXPECT_FALSE(Start(Probe));
	EXPECT_FALSE(Probe.Pending());
	EXPECT_FALSE(Probe.HasResult());
	EXPECT_TRUE(m_Transport.m_vSubmissions.empty());
	m_State.m_FailFactory = false;
	ASSERT_TRUE(Start(Probe));
	ASSERT_EQ(m_Transport.m_vSubmissions.size(), 1u);
	m_Transport.m_vSubmissions[0].m_pRequest->Finish(R"({"choices":[{"message":{"content":"你好"}}]})");
	Probe.AdvanceFrame();
	ASSERT_TRUE(Probe.HasResult());
	EXPECT_FALSE(Probe.Pending());
	EXPECT_FALSE(Probe.Response().m_Error);
	EXPECT_STREQ(Probe.Response().m_Text, "你好");
}

TEST_F(CTranslateProbeMemoryTest, CreationPreparationFailureReleasesCandidateAndClearsPriorResult)
{
	CTranslateProbe Probe;
	ASSERT_TRUE(Start(Probe));
	m_Transport.m_vSubmissions[0].m_pRequest->SetState(EHttpState::ERROR);
	Probe.AdvanceFrame();
	ASSERT_TRUE(Probe.HasResult());
	m_State.m_FailHeader = true;
	EXPECT_FALSE(Start(Probe));
	EXPECT_FALSE(Probe.Pending());
	EXPECT_FALSE(Probe.HasResult());
	EXPECT_STREQ(Probe.Service(), "");
	EXPECT_STREQ(Probe.Response().m_Text, "");
	EXPECT_EQ(m_State.m_Created, 2);
	EXPECT_EQ(m_State.m_Destroyed, 1);
	EXPECT_EQ(m_Transport.m_vSubmissions.size(), 1u);
	m_State.m_FailHeader = false;
	ASSERT_TRUE(Start(Probe));
	EXPECT_EQ(m_Transport.m_vSubmissions.size(), 2u);
}

TEST_F(CTranslateProbeMemoryTest, PollAllocationFailureRetainsFallbackAndPublishesOnlyFinalFailure)
{
	CTranslateProbe Probe;
	ASSERT_TRUE(StartFallback(Probe));
	m_State.m_FailFactory = true;
	Probe.AdvanceFrame();
	EXPECT_TRUE(Probe.Pending());
	EXPECT_FALSE(Probe.HasResult());
	ASSERT_EQ(m_Transport.m_vSubmissions.size(), 1u);
	m_State.m_FailFactory = false;
	Probe.Touch();
	Probe.AdvanceFrame();
	ASSERT_EQ(m_Transport.m_vSubmissions.size(), 2u);
	EXPECT_STREQ(m_Transport.m_vSubmissions[1].m_pRequest->Url(), "https://probe-allocation.test/v1/responses");
	EXPECT_FALSE(Probe.HasResult());
	m_Transport.m_vSubmissions[1].m_pRequest->Finish("{}", 404);
	Probe.Touch();
	Probe.AdvanceFrame();
	EXPECT_FALSE(Probe.Pending());
	ASSERT_TRUE(Probe.HasResult());
	EXPECT_TRUE(Probe.Response().m_Error);
	EXPECT_EQ(Probe.Response().m_HttpStatus, 404);
	EXPECT_STREQ(Probe.Response().m_Text, "");
	EXPECT_EQ(m_Transport.m_vSubmissions.size(), 2u);
}

TEST_F(CTranslateProbeMemoryTest, RepeatedPreparationFailuresReleaseEachCandidateWithoutLosingCurrentOwner)
{
	CTranslateProbe Probe;
	ASSERT_TRUE(StartFallback(Probe));
	m_State.m_FailHeader = true;
	for(int Attempt = 1; Attempt <= 3; ++Attempt)
	{
		SCOPED_TRACE(Attempt);
		Probe.Touch();
		Probe.AdvanceFrame();
		EXPECT_TRUE(Probe.Pending());
		EXPECT_FALSE(Probe.HasResult());
		EXPECT_EQ(m_State.m_Created, Attempt + 1);
		EXPECT_EQ(m_State.m_Destroyed, Attempt);
		EXPECT_EQ(m_Transport.m_vSubmissions.size(), 1u);
	}
	m_State.m_FailHeader = false;
	Probe.Touch();
	Probe.AdvanceFrame();
	EXPECT_EQ(m_Transport.m_vSubmissions.size(), 2u);
	EXPECT_TRUE(Probe.Pending());
}

TEST_F(CTranslateProbeMemoryTest, CancelAfterAllocationFailureAbortsOwnerAndAllowsFreshProbe)
{
	CTranslateProbe Probe;
	ASSERT_TRUE(StartFallback(Probe));
	auto pOld = m_Transport.m_vSubmissions[0].m_pRequest;
	m_State.m_FailFactory = true;
	Probe.AdvanceFrame();
	Probe.Cancel();
	EXPECT_TRUE(pOld->IsAbortRequested());
	Probe.Touch();
	Probe.AdvanceFrame();
	EXPECT_FALSE(Probe.Pending());
	EXPECT_FALSE(Probe.HasResult());
	EXPECT_EQ(m_Transport.m_vSubmissions.size(), 1u);
	m_State.m_FailFactory = false;
	ASSERT_TRUE(Start(Probe));
	EXPECT_EQ(m_Transport.m_vSubmissions.size(), 2u);
}

TEST_F(CTranslateProbeMemoryTest, LeavingPageAfterAllocationFailureCancelsInsteadOfRetrying)
{
	CTranslateProbe Probe;
	ASSERT_TRUE(StartFallback(Probe));
	auto pOld = m_Transport.m_vSubmissions[0].m_pRequest;
	m_State.m_FailFactory = true;
	Probe.AdvanceFrame();
	m_State.m_FailFactory = false;
	Probe.AdvanceFrame();
	EXPECT_TRUE(pOld->IsAbortRequested());
	EXPECT_FALSE(Probe.Pending());
	EXPECT_FALSE(Probe.HasResult());
	EXPECT_EQ(m_Transport.m_vSubmissions.size(), 1u);
}

TEST_F(CTranslateProbeMemoryTest, ConfigurationChangeAfterAllocationFailureInvalidatesRetainedOwner)
{
	CTranslateProbe Probe;
	ASSERT_TRUE(StartFallback(Probe));
	auto pOld = m_Transport.m_vSubmissions[0].m_pRequest;
	m_State.m_FailFactory = true;
	Probe.AdvanceFrame();
	m_State.m_FailFactory = false;
	str_copy(g_Config.m_QmTranslateLlmModelCustom, "new-model");
	Probe.Touch();
	Probe.AdvanceFrame();
	EXPECT_TRUE(pOld->IsAbortRequested());
	EXPECT_FALSE(Probe.Pending());
	EXPECT_FALSE(Probe.HasResult());
	EXPECT_EQ(m_Transport.m_vSubmissions.size(), 1u);
}
