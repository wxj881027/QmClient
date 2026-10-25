#include <base/system.h>

#include <game/client/components/qmclient/translate/translate_jobs.h>

#include <gtest/gtest.h>
#include <test/support/translate_completion_test_memory.h>

namespace
{
	class CBackend final : public ITranslateBackend
	{
		int &m_Destroyed;

	public:
		explicit CBackend(int &Destroyed) :
			m_Destroyed(Destroyed) {}
		~CBackend() override { ++m_Destroyed; }
		const char *Name() const override { return "submission test"; }
		std::optional<bool> Update(CTranslateResponse &Out) override
		{
			str_copy(Out.m_Text, "translated");
			return true;
		}
	};

	STranslateJob MakeJob(int &Destroyed)
	{
		STranslateJob Job;
		Job.m_pBackend = std::make_unique<CBackend>(Destroyed);
		Job.m_Outgoing = true;
		Job.m_OriginalText = "original";
		return Job;
	}
}

TEST(TranslateSubmissionMemory, AllocationFailureReleasesUnacceptedBackendAndResponse)
{
	CTranslateCompletionTestMemory Memory;
	int Destroyed = 0;
	CTranslateJobQueue Queue(&Memory);
	auto Job = MakeJob(Destroyed);
	const std::weak_ptr<CTranslateResponse> Response = Job.m_pTranslateResponse;
	Memory.m_FailAt = 1;
	EXPECT_FALSE(Queue.Submit(std::move(Job), 1));
	EXPECT_EQ(Queue.Size(), 0u);
	EXPECT_TRUE(Queue.CanSubmit(1));
	EXPECT_EQ(Destroyed, 1);
	EXPECT_TRUE(Response.expired());
	EXPECT_EQ(Memory.m_LiveBlocks, 0u);
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
}

TEST(TranslateSubmissionMemory, RecoveryAcceptsNewRequestAndReturnsOnlyItsTranslation)
{
	CTranslateCompletionTestMemory Memory;
	int Destroyed = 0;
	CTranslateJobQueue Queue(&Memory);
	Memory.m_FailAt = 1;
	EXPECT_FALSE(Queue.Submit(MakeJob(Destroyed), 1));
	Memory.AllowAllocations();
	ASSERT_TRUE(Queue.Submit(MakeJob(Destroyed), 1));
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_TRUE(Done[0].m_Success);
	EXPECT_EQ(Done[0].m_SendText, "translated");
	EXPECT_EQ(Destroyed, 1);
	EXPECT_EQ(Queue.Size(), 0u);
	EXPECT_TRUE(Queue.Update([](const auto &) { return true; }).empty());
}

TEST(TranslateSubmissionMemory, CapacityRejectionDoesNotAllocateOrDisturbAcceptedJob)
{
	CTranslateCompletionTestMemory Memory;
	int Destroyed = 0;
	CTranslateJobQueue Queue(&Memory);
	ASSERT_TRUE(Queue.Submit(MakeJob(Destroyed), 1));
	const size_t Allocations = Memory.m_Allocations;
	Memory.m_FailAt = Allocations + 1;
	EXPECT_FALSE(Queue.Submit(MakeJob(Destroyed), 1));
	EXPECT_EQ(Memory.m_Allocations, Allocations);
	EXPECT_EQ(Destroyed, 1);
	EXPECT_EQ(Queue.Size(), 1u);
	const auto Done = Queue.Update([](const auto &) { return true; });
	ASSERT_EQ(Done.size(), 1u);
	EXPECT_TRUE(Done[0].m_Success);
	EXPECT_EQ(Done[0].m_SendText, "translated");
}

TEST(TranslateSubmissionMemory, InvalidJobRejectionDoesNotAllocateQueueStorage)
{
	for(bool MissingBackend : {false, true})
	{
		SCOPED_TRACE(MissingBackend);
		CTranslateCompletionTestMemory Memory;
		int Destroyed = 0;
		CTranslateJobQueue Queue(&Memory);
		auto Job = MakeJob(Destroyed);
		if(MissingBackend)
			Job.m_pBackend.reset();
		else
			Job.m_pTranslateResponse.reset();
		Memory.m_FailAt = 1;
		EXPECT_FALSE(Queue.Submit(std::move(Job), 1));
		EXPECT_EQ(Memory.m_Allocations, 0u);
		EXPECT_EQ(Queue.Size(), 0u);
		EXPECT_EQ(Destroyed, 1);
	}
}

TEST(TranslateSubmissionMemory, QueueDestructionReleasesItsBorrowedMemoryDomain)
{
	CTranslateCompletionTestMemory Memory;
	int Destroyed = 0;
	{
		CTranslateJobQueue Queue(&Memory);
		ASSERT_TRUE(Queue.Submit(MakeJob(Destroyed), 1));
		EXPECT_GT(Memory.m_LiveBlocks, 0u);
		Queue.Clear();
		EXPECT_EQ(Destroyed, 1);
		ASSERT_TRUE(Queue.Submit(MakeJob(Destroyed), 1));
	}
	EXPECT_EQ(Destroyed, 2);
	EXPECT_EQ(Memory.m_LiveBlocks, 0u);
}
