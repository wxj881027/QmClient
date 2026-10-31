#include <base/borrowed_allocator.h>

#include <game/client/components/qmclient/qm_realtime.h>

#include <gtest/gtest.h>

#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace
{
	class CQueueMemory : public IAllocationResource
	{
		void *Allocate(size_t Bytes, size_t Alignment) override
		{
			if(m_Reject)
				throw std::bad_alloc();
			return AllocateDefault(Bytes, Alignment);
		}
		void Deallocate(void *pData, size_t Bytes, size_t Alignment) noexcept override
		{
			DeallocateDefault(pData, Bytes, Alignment);
		}

	public:
		bool m_Reject = false;
	};

	template<typename TAllocator>
	std::vector<int> Order(const std::deque<SQmRealtimeMessage, TAllocator> &Events)
	{
		std::vector<int> Result;
		for(const auto &Message : Events)
			Result.push_back(Message.m_TitleStatusCode);
		return Result;
	}
}

TEST(QmRealtimeQueueCapacity, AnonymousCapacityKeepsLatest64InArrivalOrder)
{
	std::deque<SQmRealtimeMessage> Events;
	for(int Id = 0; Id < 65; ++Id)
	{
		SQmRealtimeMessage Message;
		Message.m_TitleStatusCode = Id;
		ASSERT_TRUE(TryQueueQmRealtimeMessage(Events, std::move(Message), 64));
	}
	ASSERT_EQ(Events.size(), 64u);
	for(int Index = 0; Index < 64; ++Index)
		EXPECT_EQ(Events[Index].m_TitleStatusCode, Index + 1);
}

TEST(QmRealtimeQueueCapacity, FullQueueAllocationFailureKeepsOldItemsAndIncomingThenCanRetry)
{
	CQueueMemory Memory;
	std::deque<SQmRealtimeMessage, CBorrowedAllocator<SQmRealtimeMessage>> Events(&Memory);
	for(int Id = 0; Id < 64; ++Id)
	{
		SQmRealtimeMessage Message;
		Message.m_TitleStatusCode = Id;
		ASSERT_TRUE(TryQueueQmRealtimeMessage(Events, std::move(Message), 64));
	}
	Memory.m_Reject = true;
	bool Failed = false;
	// 等待该 STL 实际申请节点或索引，不假定每块容纳多少消息。
	for(int Id = 64; Id < 576; ++Id)
	{
		SCOPED_TRACE(Id);
		const auto Before = Order(Events);
		SQmRealtimeMessage Incoming;
		Incoming.m_Type = "incoming";
		Incoming.m_TitleStatusCode = Id;
		if(TryQueueQmRealtimeMessage(Events, std::move(Incoming), 64))
			continue;
		Failed = true;
		EXPECT_EQ(Order(Events), Before);
		EXPECT_EQ(Incoming.m_Type, "incoming");
		EXPECT_EQ(Incoming.m_TitleStatusCode, Id);
		Memory.m_Reject = false;
		ASSERT_TRUE(TryQueueQmRealtimeMessage(Events, std::move(Incoming), 64));
		ASSERT_EQ(Events.size(), 64u);
		for(size_t Index = 0; Index < 63; ++Index)
			EXPECT_EQ(Events[Index].m_TitleStatusCode, Before[Index + 1]);
		EXPECT_EQ(Events.back().m_TitleStatusCode, Id);
		break;
	}
	EXPECT_TRUE(Failed);
}

TEST(QmRealtimeQueueCapacity, ZeroCapacityRejectsWithoutConsumingQueueOrInput)
{
	std::deque<SQmRealtimeMessage> Events;
	SQmRealtimeMessage First;
	First.m_TitleStatusCode = 1;
	ASSERT_TRUE(TryQueueQmRealtimeMessage(Events, std::move(First)));
	SQmRealtimeMessage Incoming;
	Incoming.m_Type = "incoming";
	EXPECT_FALSE(TryQueueQmRealtimeMessage(Events, std::move(Incoming), 0));
	ASSERT_EQ(Events.size(), 1u);
	EXPECT_EQ(Events.front().m_TitleStatusCode, 1);
	EXPECT_EQ(Incoming.m_Type, "incoming");
}

TEST(QmRealtimeQueueCapacity, LowerLimitKeepsNewestItemsAfterSuccessfulAdmission)
{
	std::deque<SQmRealtimeMessage> Events;
	for(int Id = 0; Id < 6; ++Id)
	{
		SQmRealtimeMessage Message;
		Message.m_TitleStatusCode = Id;
		ASSERT_TRUE(TryQueueQmRealtimeMessage(Events, std::move(Message)));
	}
	SQmRealtimeMessage Incoming;
	Incoming.m_TitleStatusCode = 6;
	ASSERT_TRUE(TryQueueQmRealtimeMessage(Events, std::move(Incoming), 3));
	ASSERT_EQ(Events.size(), 3u);
	EXPECT_EQ(Events[0].m_TitleStatusCode, 4);
	EXPECT_EQ(Events[1].m_TitleStatusCode, 5);
	EXPECT_EQ(Events[2].m_TitleStatusCode, 6);
}
