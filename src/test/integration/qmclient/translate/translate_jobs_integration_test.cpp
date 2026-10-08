#include <game/client/components/qmclient/translate/translate_jobs.h>

#include <test/support/translate_http_test_support.h>

namespace
{
	STranslateJob MakeJob(CTranslateTestHttp &Http, bool Outgoing, bool Automatic, const char *pText = "original")
	{
		STranslateJob Job;
		Job.m_Outgoing = Outgoing;
		Job.m_AutoTriggered = Automatic;
		Job.m_OriginalText = pText;
		Job.m_Team = 1;
		Job.m_pBackend = CreateTranslateBackend(Http, pText, "zh", "auto", CreateTranslateTestRequest);
		return Job;
	}
}

class CTranslateQueueTest : public CTranslateBackendTest
{
};

TEST_F(CTranslateQueueTest, RefusedAutomaticOutgoingSendsOriginalRatherThanServiceText)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true, "original"), 1));
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"choices":[{"message":{"refusal":"Provider explanation","content":"partial"}}]})");
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Success);
	EXPECT_EQ(Done[0].m_SendText, "original");
	EXPECT_EQ(Done[0].m_Job.m_pTranslateResponse->m_Notice, ETranslateNotice::CONTENT_REFUSED);
	EXPECT_EQ(Queue.Size(), 0u);
}

TEST_F(CTranslateQueueTest, IncomingAndOutgoingShareCapacityInEitherOrder)
{
	for(bool OutgoingFirst : {false, true})
	{
		SCOPED_TRACE(OutgoingFirst);
		CTranslateJobQueue Queue;
		ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, OutgoingFirst, false), 1));
		EXPECT_FALSE(Queue.CanSubmit(1));
		const auto pFirst = m_Http.m_vSubmissions.back().m_pRequest;
		EXPECT_FALSE(Queue.Submit(MakeJob(m_Http, !OutgoingFirst, false), 1));
		EXPECT_EQ(Queue.Size(), 1u);
		EXPECT_TRUE(m_Http.m_vSubmissions.back().m_pRequest->IsAbortRequested());
		pFirst->Finish(R"({"choices":[{"message":{"content":"translated"}}]})");
		const auto Done = Queue.Update([](const auto &) { return true; });
		EXPECT_EQ(Done.size(), 1u);
		EXPECT_TRUE(Queue.CanSubmit(1));
		EXPECT_TRUE(Queue.Submit(MakeJob(m_Http, !OutgoingFirst, false), 1));
	}
}

TEST_F(CTranslateQueueTest, PendingTaskDoesNotCompleteOrReleaseCapacity)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true), 1));
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
	EXPECT_EQ(Queue.Size(), 1u);
	EXPECT_FALSE(Queue.CanSubmit(1));
}

TEST_F(CTranslateQueueTest, AutomaticFailureRecoversOriginalExactlyOnce)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true, "你好 original"), 1));
	m_Http.m_vSubmissions[0].m_pRequest->SetState(EHttpState::ERROR);
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Success);
	EXPECT_EQ(Done[0].m_SendText, "你好 original");
	EXPECT_EQ(Done[0].m_Job.m_Team, 1);
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
	EXPECT_EQ(Queue.Size(), 0u);
}

TEST_F(CTranslateQueueTest, ExplicitFailureDoesNotSendUntranslatedText)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, false), 1));
	m_Http.m_vSubmissions[0].m_pRequest->SetState(EHttpState::ERROR);
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Success);
	EXPECT_TRUE(Done[0].m_SendText.empty());
}

TEST_F(CTranslateQueueTest, EmptyAutomaticTranslationRecoversOriginal)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true), 1));
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"choices":[{"message":{"content":""}}]})");
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Success);
	EXPECT_EQ(Done[0].m_SendText, "original");
	EXPECT_STREQ(Done[0].m_Job.m_pTranslateResponse->m_Text, "Empty translation result");
}

TEST_F(CTranslateQueueTest, SuccessfulAutomaticTranslationSendsResultOnly)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true), 1));
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"choices":[{"message":{"content":"你好"}}]})");
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_TRUE(Done[0].m_Success);
	EXPECT_EQ(Done[0].m_SendText, "你好");
}

TEST_F(CTranslateQueueTest, IncomingResponsePublishesToOriginalSharedResponse)
{
	CTranslateJobQueue Queue;
	auto Job = MakeJob(m_Http, false, true);
	auto pResponse = Job.m_pTranslateResponse;
	ASSERT_TRUE(Queue.Submit(std::move(Job), 1));
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"choices":[{"message":{"content":"你好"}}]})");
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_EQ(Done[0].m_Job.m_pTranslateResponse, pResponse);
	EXPECT_STREQ(pResponse->m_Text, "你好");
	EXPECT_FALSE(pResponse->m_Error);
	EXPECT_TRUE(Done[0].m_SendText.empty());
}

TEST_F(CTranslateQueueTest, InvalidatedIncomingTaskAbortsWithoutPublishing)
{
	CTranslateJobQueue Queue;
	auto Job = MakeJob(m_Http, false, true);
	auto pResponse = Job.m_pTranslateResponse;
	str_copy(pResponse->m_Text, "unchanged");
	ASSERT_TRUE(Queue.Submit(std::move(Job), 1));
	auto pRequest = m_Http.m_vSubmissions[0].m_pRequest;
	EXPECT_TRUE(Queue.Update([](const auto &) { return false; }).empty());
	EXPECT_TRUE(pRequest->IsAbortRequested());
	EXPECT_STREQ(pResponse->m_Text, "unchanged");
	EXPECT_TRUE(Queue.CanSubmit(1));
}

TEST_F(CTranslateQueueTest, ResetCancelsBothDirectionsWithoutResendingOldChat)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, false, true), 2));
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true), 2));
	Queue.Clear();
	Queue.Clear();
	EXPECT_EQ(Queue.Size(), 0u);
	for(const auto &Submission : m_Http.m_vSubmissions)
		EXPECT_TRUE(Submission.m_pRequest->IsAbortRequested());
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
	EXPECT_TRUE(Queue.CanSubmit(1));
}

TEST_F(CTranslateQueueTest, RetryKeepsCapacityAndSuccessDoesNotRecoverOriginal)
{
	str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "https://queue-retry.test/v1");
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true), 1));
	m_Http.m_vSubmissions[0].m_pRequest->Finish("{}", 404);
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
	EXPECT_FALSE(Queue.CanSubmit(1));
	ASSERT_EQ(m_Http.m_vSubmissions.size(), 2u);
	m_Http.m_vSubmissions[1].m_pRequest->Finish(R"({"output_text":"成功"})");
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_EQ(Done[0].m_SendText, "成功");
}

TEST_F(CTranslateQueueTest, InitializationFailureReleasesSlotAndRecoversAutomaticMessage)
{
	g_Config.m_QmTranslateLlmEndpointCustom[0] = '\0';
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true), 1));
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Success);
	EXPECT_EQ(Done[0].m_SendText, "original");
	EXPECT_TRUE(Queue.CanSubmit(1));
	EXPECT_TRUE(m_Http.m_vSubmissions.empty());
}

TEST_F(CTranslateQueueTest, ZeroLimitAndMissingBackendRejectWithoutTakingCapacity)
{
	CTranslateJobQueue Queue;
	EXPECT_FALSE(Queue.CanSubmit(0));
	EXPECT_FALSE(Queue.CanSubmit(-1));
	EXPECT_FALSE(Queue.Submit(MakeJob(m_Http, true, false), 0));
	STranslateJob MissingBackend;
	EXPECT_FALSE(Queue.Submit(std::move(MissingBackend), 1));
	EXPECT_EQ(Queue.Size(), 0u);
}

TEST_F(CTranslateQueueTest, ReusedLineIdDiscardsCompletedResponse)
{
	CTranslateJobQueue Queue;
	auto Job = MakeJob(m_Http, false, true);
	Job.m_TranslationId = 7;
	auto pOwnerResponse = Job.m_pTranslateResponse;
	ASSERT_TRUE(Queue.Submit(std::move(Job), 1));
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"choices":[{"message":{"content":"old"}}]})");
	EXPECT_TRUE(Queue.Update([&](const auto &Pending) { return IsTranslateResponseCurrent(true, 8, pOwnerResponse, Pending); }).empty());
	EXPECT_STREQ(pOwnerResponse->m_Text, "");
	EXPECT_TRUE(m_Http.m_vSubmissions[0].m_pRequest->IsAbortRequested());
}

TEST_F(CTranslateQueueTest, ReplacedResponsePointerDiscardsSameIdJob)
{
	CTranslateJobQueue Queue;
	auto Job = MakeJob(m_Http, false, true);
	Job.m_TranslationId = 7;
	auto pOldResponse = Job.m_pTranslateResponse;
	ASSERT_TRUE(Queue.Submit(std::move(Job), 1));
	auto pNewResponse = std::make_shared<CTranslateResponse>();
	EXPECT_TRUE(Queue.Update([&](const auto &Pending) { return IsTranslateResponseCurrent(true, 7, pNewResponse, Pending); }).empty());
	EXPECT_STREQ(pOldResponse->m_Text, "");
	EXPECT_STREQ(pNewResponse->m_Text, "");
}

TEST_F(CTranslateQueueTest, MatchingInitializedOwnerReceivesCompletedResponse)
{
	CTranslateJobQueue Queue;
	auto Job = MakeJob(m_Http, false, true);
	Job.m_TranslationId = 7;
	auto pOwnerResponse = Job.m_pTranslateResponse;
	ASSERT_TRUE(Queue.Submit(std::move(Job), 1));
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"choices":[{"message":{"content":"current"}}]})");
	const auto Done = Queue.Update([&](const auto &Pending) { return IsTranslateResponseCurrent(true, 7, pOwnerResponse, Pending); });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_STREQ(pOwnerResponse->m_Text, "current");
}
