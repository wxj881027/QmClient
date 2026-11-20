#include <game/client/components/qmclient/translate/translate_jobs.h>

#include <test/support/translate_http_test_support.h>

#include <new>
#include <utility>

namespace
{
	struct SRequestState
	{
		bool m_FailFactory = false;
		bool m_FailPreparation = false;
		int m_Created = 0;
		int m_Destroyed = 0;
	};
	thread_local SRequestState *s_pRequestState = nullptr;

	class CRequestScope
	{
		SRequestState *m_pPrevious;

	public:
		explicit CRequestScope(SRequestState &State) :
			m_pPrevious(s_pRequestState) { s_pRequestState = &State; }
		~CRequestScope() { s_pRequestState = m_pPrevious; }
	};

	// 只替换请求边界；重试判定、请求owner和格式切换均由生产后端执行。
	class CRequest : public CTranslateTestRequest
	{
		SRequestState &m_State;

	public:
		CRequest(const char *pUrl, SRequestState &State) :
			CTranslateTestRequest(pUrl), m_State(State) { ++m_State.m_Created; }
		~CRequest() override { ++m_State.m_Destroyed; }
		void Header(const char *pHeader) override
		{
			if(m_State.m_FailPreparation)
			{
				m_State.m_FailPreparation = false;
				throw std::bad_alloc();
			}
			CTranslateTestRequest::Header(pHeader);
		}
	};

	std::unique_ptr<IHttpRequest> CreateRequest(const char *pUrl)
	{
		if(s_pRequestState->m_FailFactory)
		{
			s_pRequestState->m_FailFactory = false;
			throw std::bad_alloc();
		}
		return std::make_unique<CRequest>(pUrl, *s_pRequestState);
	}

	class CControlledHttp : public CTranslateTestHttp
	{
	public:
		bool m_FailNext = false;
		int m_Calls = 0;
		std::weak_ptr<IHttpRequest> m_FailedRequest;
		void Run(std::shared_ptr<IHttpRequest> pRequest) override
		{
			++m_Calls;
			if(m_FailNext)
			{
				m_FailNext = false;
				m_FailedRequest = pRequest;
				throw std::bad_alloc();
			}
			CTranslateTestHttp::Run(std::move(pRequest));
		}
	};
}

class CTranslateRetryMemoryTest : public CTranslateBackendTest
{
protected:
	SRequestState m_State;
	CControlledHttp m_Transport;
	CRequestScope m_Scope{m_State};
	void SetUp() override
	{
		CTranslateBackendTest::SetUp();
		// 本组只观察待定/失败/取消，不写会话格式记忆，重复执行也从Chat开始。
		str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "https://retry-allocation.test/v1");
	}
	bool StartChatAndFinishNotFound(CTranslateJobQueue &Queue)
	{
		const bool Accepted = Queue.CreateAndSubmit([&] {
			STranslateJob Job;
			Job.m_Outgoing = true;
			Job.m_OriginalText = "original";
			Job.m_pBackend = CreateTranslateBackend(m_Transport, "original", "en", "auto", CreateRequest);
			return Job;
		},
			1);
		if(!Accepted || m_Transport.m_vSubmissions.size() != 1)
			return false;
		const auto &Request = m_Transport.m_vSubmissions[0].m_pRequest;
		if(str_comp(Request->Url(), "https://retry-allocation.test/v1/chat/completions") != 0)
			return false;
		Request->Finish("{}", 404);
		return true;
	}
};

TEST_F(CTranslateRetryMemoryTest, RequestFactoryFailureKeepsChatRetryEligible)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(StartChatAndFinishNotFound(Queue));
	m_State.m_FailFactory = true;
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
	EXPECT_EQ(Queue.Size(), 1u);
	EXPECT_EQ(m_Transport.m_vSubmissions.size(), 1u);
	EXPECT_EQ(m_State.m_Created, 1);
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
	ASSERT_EQ(m_Transport.m_vSubmissions.size(), 2u);
	EXPECT_STREQ(m_Transport.m_vSubmissions[1].m_pRequest->Url(), "https://retry-allocation.test/v1/responses");
	EXPECT_EQ(Queue.Size(), 1u);
	Queue.CancelOutgoing(0);
	EXPECT_TRUE(m_Transport.m_vSubmissions[1].m_pRequest->IsAbortRequested());
}

TEST_F(CTranslateRetryMemoryTest, PreparationFailureReleasesCandidateAndRetriesInsteadOfWaitingForever)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(StartChatAndFinishNotFound(Queue));
	m_State.m_FailPreparation = true;
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
	EXPECT_EQ(m_State.m_Created, 2);
	EXPECT_EQ(m_State.m_Destroyed, 1);
	EXPECT_EQ(m_Transport.m_vSubmissions.size(), 1u);
	EXPECT_EQ(Queue.Size(), 1u);
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
	ASSERT_EQ(m_Transport.m_vSubmissions.size(), 2u);
	EXPECT_STREQ(m_Transport.m_vSubmissions[1].m_pRequest->Url(), "https://retry-allocation.test/v1/responses");
	Queue.Clear();
}

TEST_F(CTranslateRetryMemoryTest, HttpAdmissionFailureReleasesUnsubmittedCandidateAndRetries)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(StartChatAndFinishNotFound(Queue));
	m_Transport.m_FailNext = true;
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
	EXPECT_TRUE(m_Transport.m_FailedRequest.expired());
	EXPECT_EQ(m_State.m_Destroyed, 1);
	EXPECT_EQ(m_Transport.m_vSubmissions.size(), 1u);
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
	ASSERT_EQ(m_Transport.m_vSubmissions.size(), 2u);
	EXPECT_STREQ(m_Transport.m_vSubmissions[1].m_pRequest->Url(), "https://retry-allocation.test/v1/responses");
	EXPECT_EQ(m_Transport.m_Calls, 3);
	Queue.Clear();
}

TEST_F(CTranslateRetryMemoryTest, RepeatedAdmissionFailuresRetainOneJobAndPublishOneRecoveredRequest)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(StartChatAndFinishNotFound(Queue));
	for(int Attempt = 0; Attempt < 3; ++Attempt)
	{
		SCOPED_TRACE(Attempt);
		m_Transport.m_FailNext = true;
		EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
		EXPECT_TRUE(m_Transport.m_FailedRequest.expired());
		EXPECT_EQ(Queue.Size(), 1u);
		EXPECT_EQ(m_Transport.m_vSubmissions.size(), 1u);
	}
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
	ASSERT_EQ(m_Transport.m_vSubmissions.size(), 2u);
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
	EXPECT_EQ(m_Transport.m_vSubmissions.size(), 2u);
	EXPECT_EQ(Queue.Size(), 1u);
	Queue.CancelOutgoing(0);
}

TEST_F(CTranslateRetryMemoryTest, CancellationAfterFailedRetryStartsNoFurtherRequests)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(StartChatAndFinishNotFound(Queue));
	m_Transport.m_FailNext = true;
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
	Queue.CancelOutgoing(0);
	EXPECT_EQ(Queue.Size(), 0u);
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
	EXPECT_EQ(m_Transport.m_Calls, 2);
	EXPECT_EQ(m_Transport.m_vSubmissions.size(), 1u);
	EXPECT_TRUE(m_Transport.m_FailedRequest.expired());
}

TEST_F(CTranslateRetryMemoryTest, RecoveredFallbackFailureFinishesWithoutSendingOriginal)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(StartChatAndFinishNotFound(Queue));
	m_Transport.m_FailNext = true;
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
	ASSERT_EQ(m_Transport.m_vSubmissions.size(), 2u);
	m_Transport.m_vSubmissions[1].m_pRequest->Finish("{}", 500);
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Success);
	EXPECT_EQ(Queue.Size(), 0u);
	int Sends = 0;
	EXPECT_FALSE(DispatchOutgoingTranslation(Done[0], [&](int, const char *, const char *, int) { ++Sends; return true; }));
	EXPECT_EQ(Sends, 0);
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
	EXPECT_EQ(m_Transport.m_vSubmissions.size(), 2u);
}
