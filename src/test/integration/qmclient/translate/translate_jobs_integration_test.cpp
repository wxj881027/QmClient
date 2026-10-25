#include <game/client/components/chat.h>
#include <game/client/components/qmclient/translate/translate_jobs.h>

#include <test/support/translate_completion_test_memory.h>
#include <test/support/translate_http_test_support.h>

namespace
{
	STranslateJob MakeJob(CTranslateTestHttp &Http, bool Outgoing, bool Automatic, const char *pText = "original", bool DisplayOriginal = true)
	{
		STranslateJob Job;
		Job.m_Outgoing = Outgoing;
		Job.m_AutoTriggered = Automatic;
		Job.m_DisplayOriginal = DisplayOriginal;
		Job.m_OriginalText = pText;
		Job.m_Team = 1;
		Job.m_pBackend = CreateTranslateBackend(Http, pText, "zh", "auto", CreateTranslateTestRequest);
		return Job;
	}
}

class CTranslateQueueTest : public CTranslateBackendTest
{
};

TEST_F(CTranslateQueueTest, OutgoingQueueAllocationFailureAbortsRequestAndRetrySendsOnlyTranslation)
{
	for(bool Automatic : {false, true})
	{
		SCOPED_TRACE(Automatic);
		CTranslateCompletionTestMemory Memory;
		CTranslateJobQueue Queue(&Memory);
		Memory.m_FailAt = 1;
		EXPECT_FALSE(Queue.Submit(MakeJob(m_Http, true, Automatic, "original"), 1));
		EXPECT_TRUE(m_Http.m_vSubmissions.back().m_pRequest->IsAbortRequested());
		EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
		EXPECT_EQ(Queue.Size(), 0u);
		Memory.AllowAllocations();
		ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, Automatic, "retry original"), 1));
		m_Http.m_vSubmissions.back().m_pRequest->Finish(R"({"choices":[{"message":{"content":"translated"}}]})");
		const auto Done = Queue.Update([](const auto &) { return true; });
		ASSERT_EQ(Done.size(), 1u);
		int Sends = 0;
		EXPECT_TRUE(DispatchOutgoingTranslation(Done[0], [&](int, const char *pText, const char *pOriginal, int) {
			++Sends;
			EXPECT_STREQ(pText, "translated");
			EXPECT_STREQ(pOriginal, "retry original");
			return true;
		}));
		EXPECT_EQ(Sends, 1);
		EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
	}
}

TEST_F(CTranslateQueueTest, IncomingQueueAllocationFailureReleasesResponseAndAllowsFreshRequest)
{
	CTranslateCompletionTestMemory Memory;
	CTranslateJobQueue Queue(&Memory);
	auto Job = MakeJob(m_Http, false, false);
	const std::weak_ptr<CTranslateResponse> Response = Job.m_pTranslateResponse;
	Memory.m_FailAt = 1;
	EXPECT_FALSE(Queue.Submit(std::move(Job), 1));
	EXPECT_TRUE(Response.expired());
	EXPECT_TRUE(m_Http.m_vSubmissions.back().m_pRequest->IsAbortRequested());
	Memory.AllowAllocations();
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, false, false), 1));
	m_Http.m_vSubmissions.back().m_pRequest->Finish(R"({"choices":[{"message":{"content":"translated"}}]})");
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Job.m_Outgoing);
	EXPECT_TRUE(Done[0].m_Success);
	EXPECT_STREQ(Done[0].m_Job.m_pTranslateResponse->m_Text, "translated");
}

TEST_F(CTranslateQueueTest, RefusedAutomaticOutgoingSendsNeitherOriginalNorServiceText)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true, "original"), 1));
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"choices":[{"message":{"refusal":"Provider explanation","content":"partial"}}]})");
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Success);
	EXPECT_TRUE(Done[0].m_SendText.empty());
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

TEST_F(CTranslateQueueTest, AutomaticFailureSendsNothingAndReleasesCapacity)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true, "你好 original"), 1));
	m_Http.m_vSubmissions[0].m_pRequest->SetState(EHttpState::ERROR);
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Success);
	EXPECT_TRUE(Done[0].m_SendText.empty());
	EXPECT_EQ(Done[0].m_Job.m_Team, 1);
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
	EXPECT_EQ(Queue.Size(), 0u);
}

// 出站翻译失败时只返回本地提示，不产生网络正文或原文回退。
TEST_F(CTranslateQueueTest, FailureWithLocalOriginalEnabledSendsNothing)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true, "original", true), 1));
	m_Http.m_vSubmissions[0].m_pRequest->SetState(EHttpState::ERROR);
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Success);
	EXPECT_TRUE(Done[0].m_SendText.empty());
}

TEST_F(CTranslateQueueTest, SuccessKeepsOriginalAsLocalMetadataAndSendsOnlyTranslation)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true, "original", true), 1));
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"choices":[{"message":{"content":"translated"}}]})");
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_TRUE(Done[0].m_Success);
	EXPECT_EQ(Done[0].m_SendText, "translated");
}

TEST_F(CTranslateQueueTest, CancelledRequestWithLocalOriginalEnabledSendsNothing)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true, "original", true), 1));
	m_Http.m_vSubmissions[0].m_pRequest->SetState(EHttpState::ABORTED);
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Success);
	EXPECT_TRUE(Done[0].m_SendText.empty());
	EXPECT_TRUE(Queue.CanSubmit(1));
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
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

TEST_F(CTranslateQueueTest, EmptyAutomaticTranslationSendsNothing)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true), 1));
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"choices":[{"message":{"content":""}}]})");
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Success);
	EXPECT_TRUE(Done[0].m_SendText.empty());
	EXPECT_EQ(Done[0].m_Job.m_pTranslateResponse->m_Notice, ETranslateNotice::INVALID_RESPONSE);
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

TEST_F(CTranslateQueueTest, OversizedAutomaticServiceResultSendsNothingAndNextTaskRecovers)
{
	str_copy(g_Config.m_QmTranslateBackend, "auto");
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true, "完整原文"), 1));
	const std::string Body = "{\"ok\":true,\"text\":\"" + std::string(1024, 'x') + "\"}";
	m_Http.m_vSubmissions[0].m_pRequest->Finish(Body.c_str());
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Success);
	EXPECT_TRUE(Done[0].m_SendText.empty());
	EXPECT_EQ(Done[0].m_Job.m_pTranslateResponse->m_Notice, ETranslateNotice::INVALID_RESPONSE);
	EXPECT_EQ(Queue.Size(), 0u);
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true, "next"), 1));
	m_Http.m_vSubmissions.back().m_pRequest->Finish(R"({"ok":true,"text":"下一条"})");
	const auto Recovered = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Recovered.size(), 1u);
	EXPECT_TRUE(Recovered[0].m_Success);
	EXPECT_EQ(Recovered[0].m_SendText, "下一条");
}

TEST_F(CTranslateQueueTest, OversizedExplicitAutomaticServiceResultSendsNothing)
{
	str_copy(g_Config.m_QmTranslateBackend, "auto");
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, false), 1));
	const std::string Body = "{\"ok\":true,\"text\":\"" + std::string(1024, 'x') + "\"}";
	m_Http.m_vSubmissions[0].m_pRequest->Finish(Body.c_str());
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Success);
	EXPECT_TRUE(Done[0].m_SendText.empty());
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
	EXPECT_TRUE(Done[0].m_SendText.empty());
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

TEST_F(CTranslateQueueTest, TokenLimitedOutgoingDoesNotSendPartialText)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, false), 1));
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"choices":[{"finish_reason":"length","message":{"content":"partial translation"}}]})");
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Success);
	EXPECT_TRUE(Done[0].m_SendText.empty());
	EXPECT_EQ(Done[0].m_Job.m_pTranslateResponse->m_Notice, ETranslateNotice::INVALID_RESPONSE);
}

// 真实任务队列和发送分发器协作；外部聊天传输只记录收到的网络正文。
TEST_F(CTranslateQueueTest, SuccessfulOutgoingDispatchSendsOneTranslationWithSeparateLocalOriginal)
{
	for(bool ShowOriginal : {false, true})
	{
		SCOPED_TRACE(ShowOriginal);
		CTranslateJobQueue Queue;
		ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true, "原文", ShowOriginal), 1));
		EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
		m_Http.m_vSubmissions.back().m_pRequest->Finish(R"({"choices":[{"message":{"content":"translated"}}]})");
		const auto Done = Queue.Update([](const auto &) { return true; });
		ASSERT_EQ(Done.size(), 1u);
		std::vector<std::string> NetworkMessages;
		std::string LocalOriginal;
		ASSERT_TRUE(DispatchOutgoingTranslation(Done[0], [&](int Team, const char *pText, const char *pOriginal, int Connection) {
			EXPECT_EQ(Team, 1);
			EXPECT_EQ(Connection, 0);
			NetworkMessages.emplace_back(pText);
			LocalOriginal = pOriginal != nullptr ? pOriginal : "";
			return true;
		}));
		ASSERT_EQ(NetworkMessages.size(), 1u);
		EXPECT_EQ(NetworkMessages[0], "translated");
		EXPECT_EQ(LocalOriginal, ShowOriginal ? "原文" : "");
		EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
	}
}
TEST_F(CTranslateQueueTest, FailureOrCancellationNeverCallsTheChatTransport)
{
	for(EHttpState State : {EHttpState::ERROR, EHttpState::ABORTED})
	{
		SCOPED_TRACE(static_cast<int>(State));
		CTranslateJobQueue Queue;
		ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true, "原文"), 1));
		m_Http.m_vSubmissions.back().m_pRequest->SetState(State);
		const auto Done = Queue.Update([](const auto &) { return true; });
		ASSERT_EQ(Done.size(), 1u);
		int Sends = 0;
		EXPECT_FALSE(DispatchOutgoingTranslation(Done[0], [&](int, const char *, const char *, int) { ++Sends; return true; }));
		EXPECT_EQ(Sends, 0);
	}
}
TEST_F(CTranslateQueueTest, RejectedTranslationDoesNotFallBackToOriginal)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true, "原文"), 1));
	m_Http.m_vSubmissions.back().m_pRequest->Finish(R"({"choices":[{"message":{"content":"translated"}}]})");
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	std::vector<std::string> Attempts;
	EXPECT_FALSE(DispatchOutgoingTranslation(Done[0], [&](int, const char *pText, const char *, int) { Attempts.emplace_back(pText); return false; }));
	ASSERT_EQ(Attempts.size(), 1u);
	EXPECT_EQ(Attempts[0], "translated");
}
TEST_F(CTranslateQueueTest, AFullTranslationQueueDoesNotDispatchEitherMessage)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, false, false), 1));
	EXPECT_FALSE(Queue.Submit(MakeJob(m_Http, true, true, "原文"), 1));
	EXPECT_EQ(Queue.Size(), 1u);
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
}

TEST_F(CTranslateQueueTest, DummyDisconnectCancelsOnlyItsOutgoingJobsAndReconnectionStartsFresh)
{
	CTranslateJobQueue Queue;
	auto Dummy = MakeJob(m_Http, true, true, "old dummy");
	Dummy.m_Connection = 1;
	ASSERT_TRUE(Queue.Submit(std::move(Dummy), 3));
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true, "main"), 3));
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, false, true, "incoming"), 3));
	Queue.CancelOutgoing(1);
	EXPECT_EQ(Queue.Size(), 2u);
	EXPECT_TRUE(m_Http.m_vSubmissions[0].m_pRequest->IsAbortRequested());
	auto NewDummy = MakeJob(m_Http, true, true, "new dummy");
	NewDummy.m_Connection = 1;
	ASSERT_TRUE(Queue.Submit(std::move(NewDummy), 3));
	m_Http.m_vSubmissions.back().m_pRequest->Finish(R"({"choices":[{"message":{"content":"fresh translation"}}]})");
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_EQ(Done[0].m_Job.m_OriginalText, "new dummy");
	EXPECT_EQ(Done[0].m_SendText, "fresh translation");
}

TEST_F(CTranslateQueueTest, LongOutgoingTranslationUsesOneUtf8SafeNetworkTextForEchoMatching)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true, "original"), 1));
	std::string LongText;
	for(int i = 0; i < 100; ++i)
		LongText += "译";
	const std::string Json = "{\"choices\":[{\"message\":{\"content\":\"" + LongText + "\"}}]}";
	m_Http.m_vSubmissions.back().m_pRequest->Finish(Json.c_str());
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	int Sends = 0;
	ASSERT_TRUE(DispatchOutgoingTranslation(Done[0], [&](int, const char *pText, const char *pOriginal, int) {
		++Sends;
		EXPECT_EQ(str_length(pText), 255);
		EXPECT_TRUE(str_utf8_check(pText));
		EXPECT_STREQ(pOriginal, "original");
		return true;
	}));
	EXPECT_EQ(Sends, 1);
}

TEST_F(CTranslateQueueTest, OutgoingPayloadFailureRetainsHttpOwnerAndDispatchesOnceAfterRecovery)
{
	CTranslateCompletionTestMemory Memory;
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true, "原文"), 1));
	const std::string Text(160, 'z');
	const std::string Json = "{\"choices\":[{\"message\":{\"content\":\"" + Text + "\"}}]}";
	const auto pRequest = m_Http.m_vSubmissions.back().m_pRequest;
	pRequest->Finish(Json.c_str());
	Memory.m_FailAt = 2;
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }, &Memory).empty());
	EXPECT_EQ(Queue.Size(), 1u);
	EXPECT_FALSE(pRequest->IsAbortRequested());
	Memory.AllowAllocations();
	const auto Done = Queue.Update([](const auto &) { return true; }, &Memory);
	ASSERT_EQ(Done.size(), 1u);
	std::vector<std::string> Sent;
	EXPECT_TRUE(DispatchOutgoingTranslation(Done[0], [&](int Team, const char *pText, const char *pOriginal, int Connection) {
		EXPECT_EQ(Team, 1);
		EXPECT_EQ(Connection, 0);
		EXPECT_STREQ(pOriginal, "原文");
		Sent.emplace_back(pText);
		return true;
	}));
	ASSERT_EQ(Sent.size(), 1u);
	EXPECT_EQ(Sent[0], Text);
	EXPECT_EQ(m_Http.m_vSubmissions.size(), 1u);
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }, &Memory).empty());
}

TEST_F(CTranslateQueueTest, CancellationAfterDeferredCompletionAbortsHttpAndPublishesNothing)
{
	CTranslateCompletionTestMemory Memory;
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true, "原文"), 1));
	const auto pRequest = m_Http.m_vSubmissions.back().m_pRequest;
	pRequest->Finish(R"({"choices":[{"message":{"content":"translated"}}]})");
	Memory.m_FailAt = 1;
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }, &Memory).empty());
	Queue.CancelOutgoing(0);
	EXPECT_TRUE(pRequest->IsAbortRequested());
	Memory.AllowAllocations();
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }, &Memory).empty());
	EXPECT_EQ(Queue.Size(), 0u);
}

TEST_F(CTranslateQueueTest, FailedHttpResultRemainsUnsendableAfterCompletionStorageRecovery)
{
	CTranslateCompletionTestMemory Memory;
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true, "原文"), 1));
	m_Http.m_vSubmissions.back().m_pRequest->SetState(EHttpState::ERROR);
	Memory.m_FailAt = 1;
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }, &Memory).empty());
	Memory.AllowAllocations();
	const auto Done = Queue.Update([](const auto &) { return true; }, &Memory);
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Success);
	EXPECT_TRUE(Done[0].m_SendText.empty());
	int Sends = 0;
	EXPECT_FALSE(DispatchOutgoingTranslation(Done[0], [&](int, const char *, const char *, int) { ++Sends; return true; }));
	EXPECT_EQ(Sends, 0);
}
