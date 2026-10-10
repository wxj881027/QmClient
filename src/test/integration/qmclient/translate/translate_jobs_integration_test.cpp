#include <game/client/components/chat.h>
#include <game/client/components/qmclient/translate/translate_jobs.h>

#include <test/support/translate_http_test_support.h>

namespace
{
	STranslateJob MakeJob(CTranslateTestHttp &Http, bool Outgoing, bool Automatic, const char *pText = "original", bool OriginalSent = false)
	{
		STranslateJob Job;
		Job.m_Outgoing = Outgoing;
		Job.m_AutoTriggered = Automatic;
		Job.m_OriginalSent = OriginalSent;
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

// 原文已随提交先行发送时，失败回退不再重复发送原文。
TEST_F(CTranslateQueueTest, FailureWithAlreadySentOriginalDoesNotResend)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true, "original", true), 1));
	m_Http.m_vSubmissions[0].m_pRequest->SetState(EHttpState::ERROR);
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Success);
	EXPECT_TRUE(Done[0].m_SendText.empty());
}

TEST_F(CTranslateQueueTest, SuccessWithAlreadySentOriginalStillSendsTranslation)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true, "original", true), 1));
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"choices":[{"message":{"content":"translated"}}]})");
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_TRUE(Done[0].m_Success);
	EXPECT_EQ(Done[0].m_SendText, "translated");
}

TEST_F(CTranslateQueueTest, CancelledRequestWithAlreadySentOriginalDoesNotResend)
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

TEST_F(CTranslateQueueTest, EmptyAutomaticTranslationRecoversOriginal)
{
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true), 1));
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"choices":[{"message":{"content":""}}]})");
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Success);
	EXPECT_EQ(Done[0].m_SendText, "original");
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

TEST_F(CTranslateQueueTest, OversizedAutomaticServiceResultRecoversOriginalExactlyOnce)
{
	str_copy(g_Config.m_QmTranslateBackend, "auto");
	CTranslateJobQueue Queue;
	ASSERT_TRUE(Queue.Submit(MakeJob(m_Http, true, true, "完整原文"), 1));
	const std::string Body = "{\"ok\":true,\"text\":\"" + std::string(1024, 'x') + "\"}";
	m_Http.m_vSubmissions[0].m_pRequest->Finish(Body.c_str());
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Success);
	EXPECT_EQ(Done[0].m_SendText, "完整原文");
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

// 聊天接纳与翻译任务队列协作；时钟和待发送数量由测试输入，HTTP 只替换外部传输。
class CTranslateOriginalAdmissionTest : public CTranslateBackendTest
{
protected:
	CTranslateJobQueue m_Queue;
	int m_PendingCount = 0;
	int m_OriginalAttempts = 0;
	int64_t m_Now = 101;

	bool Start(bool KeepOriginal = true, bool Automatic = true)
	{
		const auto SendOriginal = [&] {
			++m_OriginalAttempts;
			return CChat::ReserveChatMessage(100, m_Now, 10, m_PendingCount);
		};
		const auto StartTranslation = [&](bool OriginalSent) {
			return m_Queue.Submit(MakeJob(m_Http, true, Automatic, "original", OriginalSent), 1);
		};
		return CChat::TryStartOutgoingTranslation(KeepOriginal, SendOriginal, StartTranslation);
	}
};

TEST_F(CTranslateOriginalAdmissionTest, FullChatQueueRejectsBothOutgoingModesBeforeCreatingRequest)
{
	m_PendingCount = 3;
	m_Now = 110;
	for(bool Automatic : {false, true})
	{
		SCOPED_TRACE(Automatic);
		EXPECT_FALSE(Start(true, Automatic));
		EXPECT_EQ(m_PendingCount, 3);
		EXPECT_EQ(m_Queue.Size(), 0u);
		EXPECT_TRUE(m_Http.m_vSubmissions.empty());
	}
	EXPECT_EQ(m_OriginalAttempts, 2);
}

TEST_F(CTranslateOriginalAdmissionTest, LastChatSlotAcceptsOriginalAndFailureDoesNotResend)
{
	m_PendingCount = 2;
	ASSERT_TRUE(Start());
	EXPECT_EQ(m_PendingCount, 3);
	ASSERT_EQ(m_Http.m_vSubmissions.size(), 1u);
	m_Http.m_vSubmissions[0].m_pRequest->SetState(EHttpState::ERROR);
	const auto Done = m_Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_TRUE(Done[0].m_Job.m_OriginalSent);
	EXPECT_TRUE(Done[0].m_SendText.empty());
	EXPECT_TRUE(m_Queue.Update([](const auto &) { return true; }).empty());
}

TEST_F(CTranslateOriginalAdmissionTest, ImmediateOriginalDoesNotConsumePendingCapacityAndSuccessSendsTranslation)
{
	m_PendingCount = 3;
	m_Now = 111;
	ASSERT_TRUE(Start());
	EXPECT_EQ(m_PendingCount, 3);
	m_Http.m_vSubmissions[0].m_pRequest->Finish(R"({"choices":[{"message":{"content":"translated"}}]})");
	const auto Done = m_Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_TRUE(Done[0].m_Job.m_OriginalSent);
	EXPECT_EQ(Done[0].m_SendText, "translated");
}

TEST_F(CTranslateOriginalAdmissionTest, ExactSendDelayStillQueuesOriginal)
{
	m_PendingCount = 2;
	m_Now = 110;
	ASSERT_TRUE(Start());
	EXPECT_EQ(m_PendingCount, 3);
	m_Http.m_vSubmissions[0].m_pRequest->SetState(EHttpState::ABORTED);
	const auto Done = m_Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_TRUE(Done[0].m_Job.m_OriginalSent);
	EXPECT_TRUE(Done[0].m_SendText.empty());
}

TEST_F(CTranslateOriginalAdmissionTest, RejectedOriginalCanRetryAfterChatCapacityRecovers)
{
	m_PendingCount = 3;
	ASSERT_FALSE(Start());
	// 外部聊天消费者释放一个槽位后，用户再次提交才启动翻译。
	m_PendingCount = 2;
	ASSERT_TRUE(Start());
	EXPECT_EQ(m_PendingCount, 3);
	EXPECT_EQ(m_OriginalAttempts, 2);
	EXPECT_EQ(m_Queue.Size(), 1u);
	ASSERT_EQ(m_Http.m_vSubmissions.size(), 1u);
	m_Http.m_vSubmissions[0].m_pRequest->SetState(EHttpState::ERROR);
	const auto Done = m_Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_TRUE(Done[0].m_Job.m_OriginalSent);
	EXPECT_TRUE(Done[0].m_SendText.empty());
}

TEST_F(CTranslateOriginalAdmissionTest, OriginalDisabledSkipsChatAdmissionAndRetainsAutomaticFailureRecovery)
{
	m_PendingCount = 3;
	ASSERT_TRUE(Start(false));
	EXPECT_EQ(m_OriginalAttempts, 0);
	EXPECT_EQ(m_PendingCount, 3);
	m_Http.m_vSubmissions[0].m_pRequest->SetState(EHttpState::ERROR);
	const auto Done = m_Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Job.m_OriginalSent);
	EXPECT_EQ(Done[0].m_SendText, "original");
}

TEST_F(CTranslateOriginalAdmissionTest, LocallyHandledOriginalDoesNotStartTranslation)
{
	const auto StartTranslation = [&](bool OriginalSent) {
		return m_Queue.Submit(MakeJob(m_Http, true, true, "original", OriginalSent), 1);
	};
	EXPECT_FALSE(CChat::TryStartOutgoingTranslation(true, [] { return CChat::EChatSendResult::HANDLED; }, StartTranslation));
	EXPECT_EQ(m_Queue.Size(), 0u);
	EXPECT_TRUE(m_Http.m_vSubmissions.empty());
}
