#include <game/client/components/qmclient/translate/translate_jobs.h>

#include <test/support/translate_http_test_support.h>

#include <new>
#include <utility>

namespace
{
	std::unique_ptr<IHttpRequest> FailRequestAllocation(const char *)
	{
		throw std::bad_alloc();
	}

	// 只在HTTP接收前失败，弱引用用于确认请求随构造展开释放。
	class CFailOnceHttp : public CTranslateTestHttp
	{
	public:
		bool m_Fail = true;
		std::weak_ptr<IHttpRequest> m_LastAttempt;
		void Run(std::shared_ptr<IHttpRequest> pRequest) override
		{
			if(m_Fail)
			{
				m_Fail = false;
				m_LastAttempt = pRequest;
				throw std::bad_alloc();
			}
			CTranslateTestHttp::Run(std::move(pRequest));
		}
	};
}

class CTranslateCreationTest : public CTranslateBackendTest
{
};

TEST_F(CTranslateCreationTest, RequestFactoryAllocationFailureReleasesResponseAndAllowsRetry)
{
	CTranslateJobQueue Queue;
	std::weak_ptr<CTranslateResponse> Response;
	EXPECT_FALSE(Queue.CreateAndSubmit([&] {
		STranslateJob Job;
		Response = Job.m_pTranslateResponse;
		Job.m_pBackend = CreateTranslateBackend(m_Http, "original", "en", "auto", FailRequestAllocation);
		return Job;
	},
		1));
	EXPECT_TRUE(Response.expired());
	EXPECT_TRUE(m_Http.m_vSubmissions.empty());
	EXPECT_EQ(Queue.Size(), 0u);
	ASSERT_TRUE(Queue.CreateAndSubmit([&] {
		STranslateJob Job;
		Job.m_pBackend = Create("original", "en");
		return Job;
	},
		1));
	m_Http.m_vSubmissions.back().m_pRequest->Finish(R"({"choices":[{"message":{"content":"translated"}}]})");
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_TRUE(Done[0].m_Success);
	EXPECT_STREQ(Done[0].m_Job.m_pTranslateResponse->m_Text, "translated");
}

TEST_F(CTranslateCreationTest, HttpAdmissionAllocationFailureReleasesPartialBackendAndRequest)
{
	for(const char *pBackend : {"auto", "llm"})
	{
		SCOPED_TRACE(pBackend);
		str_copy(g_Config.m_QmTranslateBackend, pBackend);
		CFailOnceHttp Http;
		CTranslateJobQueue Queue;
		const auto CreateJob = [&] {
			STranslateJob Job;
			Job.m_Outgoing = true;
			Job.m_OriginalText = "original";
			Job.m_pBackend = CreateTranslateBackend(Http, "original", "en", "auto", CreateTranslateTestRequest);
			return Job;
		};
		EXPECT_FALSE(Queue.CreateAndSubmit(CreateJob, 1));
		EXPECT_TRUE(Http.m_LastAttempt.expired());
		EXPECT_TRUE(Http.m_vSubmissions.empty());
		EXPECT_EQ(Queue.Size(), 0u);
		EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
		ASSERT_TRUE(Queue.CreateAndSubmit(CreateJob, 1));
		Http.m_vSubmissions.back().m_pRequest->Finish(str_comp(pBackend, "auto") == 0 ?
								      R"({"ok":true,"text":"translated"})" :
								      R"({"choices":[{"message":{"content":"translated"}}]})");
		const auto Done = Queue.Update([](const auto &) { return true; });
		ASSERT_EQ(Done.size(), 1u);
		EXPECT_TRUE(Done[0].m_Success);
		EXPECT_EQ(Done[0].m_SendText, "translated");
	}
}
