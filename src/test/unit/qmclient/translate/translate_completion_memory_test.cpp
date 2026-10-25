#include <base/system.h>

#include <game/client/components/qmclient/translate/translate_jobs.h>

#include <gtest/gtest.h>
#include <test/support/translate_completion_test_memory.h>

#include <memory>
#include <string>

namespace
{
	struct SBackendState
	{
		std::optional<bool> m_Result = true;
		std::string m_Text = std::string(160, 'x');
		bool m_ThrowNext = false;
		int m_Updates = 0;
		int m_Destroyed = 0;
	};

	// 外部服务边界只提供待定/成功/失败，交接与重试由真实队列执行。
	class CBackend final : public ITranslateBackend
	{
		std::shared_ptr<SBackendState> m_pState;

	public:
		explicit CBackend(std::shared_ptr<SBackendState> pState) :
			m_pState(std::move(pState)) {}
		~CBackend() override { ++m_pState->m_Destroyed; }
		const char *Name() const override { return "completion test"; }
		std::optional<bool> Update(CTranslateResponse &Out) override
		{
			++m_pState->m_Updates;
			if(m_pState->m_ThrowNext)
			{
				m_pState->m_ThrowNext = false;
				throw std::bad_alloc();
			}
			if(m_pState->m_Result.has_value())
				str_copy(Out.m_Text, m_pState->m_Text.c_str());
			return m_pState->m_Result;
		}
	};

	STranslateJob MakeJob(const std::shared_ptr<SBackendState> &pState, bool Outgoing = true)
	{
		STranslateJob Job;
		Job.m_pBackend = std::make_unique<CBackend>(pState);
		Job.m_Outgoing = Outgoing;
		Job.m_OriginalText = "original";
		return Job;
	}
	bool Current(const STranslateJob &) { return true; }
}

TEST(TranslateCompletionMemory, ResultListFailureRetainsJobAndRetriesWithoutPollingAgain)
{
	CTranslateCompletionTestMemory Memory;
	CTranslateJobQueue Queue;
	auto pState = std::make_shared<SBackendState>();
	ASSERT_TRUE(Queue.Submit(MakeJob(pState), 1));
	Memory.m_FailAt = 1;
	EXPECT_TRUE(Queue.Update(Current, &Memory).empty());
	EXPECT_EQ(Queue.Size(), 1u);
	EXPECT_EQ(pState->m_Updates, 1);
	EXPECT_EQ(pState->m_Destroyed, 0);
	Memory.AllowAllocations();
	const auto Done = Queue.Update(Current, &Memory);
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_TRUE(Done[0].m_Success);
	EXPECT_STREQ(Done[0].m_SendText.c_str(), pState->m_Text.c_str());
	EXPECT_NE(Done[0].m_Job.m_pBackend, nullptr);
	EXPECT_EQ(Queue.Size(), 0u);
	EXPECT_EQ(pState->m_Updates, 1);
}

TEST(TranslateCompletionMemory, SendTextAllocationFailureKeepsCompletedResponseAndBackend)
{
	CTranslateCompletionTestMemory Memory;
	CTranslateJobQueue Queue;
	auto pState = std::make_shared<SBackendState>();
	ASSERT_TRUE(Queue.Submit(MakeJob(pState), 1));
	Memory.m_FailAt = 2;
	const auto Deferred = Queue.Update(Current, &Memory);
	EXPECT_TRUE(Deferred.empty());
	EXPECT_EQ(Queue.Size(), 1u);
	EXPECT_EQ(pState->m_Destroyed, 0);
	Memory.AllowAllocations();
	const auto Done = Queue.Update(Current, &Memory);
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_TRUE(Done[0].m_Success);
	EXPECT_STREQ(Done[0].m_SendText.c_str(), pState->m_Text.c_str());
	EXPECT_EQ(pState->m_Updates, 1);
}

TEST(TranslateCompletionMemory, LaterPayloadFailureReturnsEarlierResultsAndRetainsOnlyRemainingJob)
{
	CTranslateCompletionTestMemory Memory;
	CTranslateJobQueue Queue;
	auto pFirst = std::make_shared<SBackendState>();
	auto pSecond = std::make_shared<SBackendState>();
	pFirst->m_Text.assign(160, 'a');
	pSecond->m_Text.assign(160, 'b');
	ASSERT_TRUE(Queue.Submit(MakeJob(pFirst), 2));
	ASSERT_TRUE(Queue.Submit(MakeJob(pSecond), 2));
	Memory.m_FailAt = 3;
	const auto First = Queue.Update(Current, &Memory);
	ASSERT_EQ(First.size(), 1u);
	EXPECT_STREQ(First[0].m_SendText.c_str(), pFirst->m_Text.c_str());
	EXPECT_EQ(Queue.Size(), 1u);
	Memory.AllowAllocations();
	const auto Second = Queue.Update(Current, &Memory);
	ASSERT_EQ(Second.size(), 1u);
	EXPECT_STREQ(Second[0].m_SendText.c_str(), pSecond->m_Text.c_str());
	EXPECT_EQ(pFirst->m_Updates, 1);
	EXPECT_EQ(pSecond->m_Updates, 1);
	EXPECT_TRUE(Queue.Update(Current, &Memory).empty());
}

TEST(TranslateCompletionMemory, PendingBackendDoesNotAllocateCompletionStorage)
{
	CTranslateCompletionTestMemory Memory;
	CTranslateJobQueue Queue;
	auto pState = std::make_shared<SBackendState>();
	pState->m_Result = std::nullopt;
	ASSERT_TRUE(Queue.Submit(MakeJob(pState), 1));
	Memory.m_FailAt = 1;
	EXPECT_TRUE(Queue.Update(Current, &Memory).empty());
	EXPECT_EQ(Memory.m_Allocations, 0u);
	EXPECT_EQ(Queue.Size(), 1u);
}

TEST(TranslateCompletionMemory, OutgoingCancellationAfterDeferredCompletionReleasesBackend)
{
	CTranslateCompletionTestMemory Memory;
	CTranslateJobQueue Queue;
	auto pState = std::make_shared<SBackendState>();
	ASSERT_TRUE(Queue.Submit(MakeJob(pState), 1));
	Memory.m_FailAt = 1;
	EXPECT_TRUE(Queue.Update(Current, &Memory).empty());
	Queue.CancelOutgoing(0);
	EXPECT_EQ(Queue.Size(), 0u);
	EXPECT_EQ(pState->m_Destroyed, 1);
	Memory.AllowAllocations();
	EXPECT_TRUE(Queue.Update(Current, &Memory).empty());
}

TEST(TranslateCompletionMemory, StaleIncomingOwnerIsDiscardedAfterDeferredCompletionWithoutPollingAgain)
{
	CTranslateCompletionTestMemory Memory;
	CTranslateJobQueue Queue;
	auto pState = std::make_shared<SBackendState>();
	ASSERT_TRUE(Queue.Submit(MakeJob(pState, false), 1));
	Memory.m_FailAt = 1;
	EXPECT_TRUE(Queue.Update(Current, &Memory).empty());
	Memory.AllowAllocations();
	EXPECT_TRUE(Queue.Update([](const auto &) { return false; }, &Memory).empty());
	EXPECT_EQ(Queue.Size(), 0u);
	EXPECT_EQ(pState->m_Updates, 1);
	EXPECT_EQ(pState->m_Destroyed, 1);
}

TEST(TranslateCompletionMemory, FailureRemainsUnsendableAfterResultAllocationRecovery)
{
	CTranslateCompletionTestMemory Memory;
	CTranslateJobQueue Queue;
	auto pState = std::make_shared<SBackendState>();
	pState->m_Result = false;
	ASSERT_TRUE(Queue.Submit(MakeJob(pState), 1));
	Memory.m_FailAt = 1;
	EXPECT_TRUE(Queue.Update(Current, &Memory).empty());
	Memory.AllowAllocations();
	const auto Done = Queue.Update(Current, &Memory);
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Success);
	EXPECT_TRUE(Done[0].m_SendText.empty());
	EXPECT_EQ(pState->m_Updates, 1);
}

TEST(TranslateCompletionMemory, EmptySuccessfulOutgoingResponseStillFailsAfterDeferredPublication)
{
	CTranslateCompletionTestMemory Memory;
	CTranslateJobQueue Queue;
	auto pState = std::make_shared<SBackendState>();
	pState->m_Text.clear();
	ASSERT_TRUE(Queue.Submit(MakeJob(pState), 1));
	Memory.m_FailAt = 1;
	EXPECT_TRUE(Queue.Update(Current, &Memory).empty());
	Memory.AllowAllocations();
	const auto Done = Queue.Update(Current, &Memory);
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_FALSE(Done[0].m_Success);
	EXPECT_TRUE(Done[0].m_SendText.empty());
	EXPECT_TRUE(Done[0].m_Job.m_pTranslateResponse->m_Error);
	EXPECT_EQ(pState->m_Updates, 1);
}

TEST(TranslateCompletionMemory, IncomingCompletionDoesNotAllocateOutgoingPayload)
{
	CTranslateCompletionTestMemory Memory;
	CTranslateJobQueue Queue;
	auto pState = std::make_shared<SBackendState>();
	ASSERT_TRUE(Queue.Submit(MakeJob(pState, false), 1));
	Memory.m_FailAt = 2;
	const auto Done = Queue.Update(Current, &Memory);
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_TRUE(Done[0].m_Success);
	EXPECT_TRUE(Done[0].m_SendText.empty());
	EXPECT_EQ(Memory.m_Allocations, 1u);
}

TEST(TranslateCompletionMemory, ResultAndPayloadStorageAreReleasedWithReturnedOwner)
{
	CTranslateCompletionTestMemory Memory;
	CTranslateJobQueue Queue;
	auto pState = std::make_shared<SBackendState>();
	ASSERT_TRUE(Queue.Submit(MakeJob(pState), 1));
	{
		const auto Done = Queue.Update(Current, &Memory);
		ASSERT_EQ(Done.size(), 1u);
		EXPECT_EQ(Memory.m_LiveBlocks, 2u);
		EXPECT_EQ(pState->m_Destroyed, 0);
	}
	EXPECT_EQ(Memory.m_LiveBlocks, 0u);
	EXPECT_EQ(pState->m_Destroyed, 1);
}

TEST(TranslateCompletionMemory, LaterBackendAllocationFailureReturnsEarlierResultsAndRetriesPendingBackend)
{
	CTranslateCompletionTestMemory Memory;
	CTranslateJobQueue Queue;
	auto pFirst = std::make_shared<SBackendState>();
	auto pSecond = std::make_shared<SBackendState>();
	pSecond->m_ThrowNext = true;
	ASSERT_TRUE(Queue.Submit(MakeJob(pFirst), 2));
	ASSERT_TRUE(Queue.Submit(MakeJob(pSecond), 2));
	const auto First = Queue.Update(Current, &Memory);
	ASSERT_EQ(First.size(), 1u);
	EXPECT_EQ(Queue.Size(), 1u);
	EXPECT_EQ(pFirst->m_Updates, 1);
	EXPECT_EQ(pSecond->m_Updates, 1);
	const auto Second = Queue.Update(Current, &Memory);
	ASSERT_EQ(Second.size(), 1u);
	EXPECT_EQ(Queue.Size(), 0u);
	EXPECT_EQ(pFirst->m_Updates, 1);
	EXPECT_EQ(pSecond->m_Updates, 2);
}

TEST(TranslateCompletionMemory, OwnerValidationAllocationFailurePreservesEarlierReadyResult)
{
	CTranslateCompletionTestMemory Memory;
	CTranslateJobQueue Queue;
	auto pFirst = std::make_shared<SBackendState>();
	auto pSecond = std::make_shared<SBackendState>();
	ASSERT_TRUE(Queue.Submit(MakeJob(pFirst), 2));
	ASSERT_TRUE(Queue.Submit(MakeJob(pSecond, false), 2));
	bool FailValidation = true;
	const auto Validate = [&](const auto &) {
		if(FailValidation)
			throw std::bad_alloc();
		return true;
	};
	const auto First = Queue.Update(Validate, &Memory);
	ASSERT_EQ(First.size(), 1u);
	EXPECT_EQ(Queue.Size(), 1u);
	EXPECT_EQ(pSecond->m_Updates, 0);
	FailValidation = false;
	const auto Second = Queue.Update(Validate, &Memory);
	ASSERT_EQ(Second.size(), 1u);
	EXPECT_EQ(pFirst->m_Updates, 1);
	EXPECT_EQ(pSecond->m_Updates, 1);
}
