#include <base/assert_handler_slot.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

TEST(AssertHandlerSlot, EmptySlotHasNoCallback)
{
	CAssertHandlerSlot Slot;
	EXPECT_FALSE(Slot.TryTake());
}

TEST(AssertHandlerSlot, TakingCallbackTransfersItExactlyOnce)
{
	CAssertHandlerSlot Slot;
	std::string Seen;
	Slot.Set([&](const char *pMessage) { Seen = pMessage; });
	auto Handler = Slot.TryTake();
	ASSERT_TRUE(Handler);
	EXPECT_FALSE(Slot.TryTake());
	Handler("original assertion");
	EXPECT_EQ(Seen, "original assertion");
}

TEST(AssertHandlerSlot, ReplacementCallsOnlyLatestPendingCallback)
{
	CAssertHandlerSlot Slot;
	int OldCalls = 0, NewCalls = 0;
	Slot.Set([&](const char *) { ++OldCalls; });
	Slot.Set([&](const char *) { ++NewCalls; });
	auto Handler = Slot.TryTake();
	ASSERT_TRUE(Handler);
	Handler("message");
	EXPECT_EQ(OldCalls, 0);
	EXPECT_EQ(NewCalls, 1);
}

TEST(AssertHandlerSlot, ClearingReleasesPendingCaptureWithoutInvokingCallback)
{
	CAssertHandlerSlot Slot;
	auto pValue = std::make_shared<int>(7);
	std::weak_ptr<int> Lifetime = pValue;
	int Calls = 0;
	Slot.Set([pValue, &Calls](const char *) { ++Calls; });
	pValue.reset();
	EXPECT_FALSE(Lifetime.expired());
	Slot.Set({});
	EXPECT_TRUE(Lifetime.expired());
	EXPECT_FALSE(Slot.TryTake());
	EXPECT_EQ(Calls, 0);
}

TEST(AssertHandlerSlot, TakenCaptureOutlivesUnregisterUntilCallerReleasesIt)
{
	CAssertHandlerSlot Slot;
	auto pValue = std::make_shared<int>(11);
	std::weak_ptr<int> Lifetime = pValue;
	Slot.Set([pValue](const char *) { EXPECT_EQ(*pValue, 11); });
	pValue.reset();
	auto Handler = Slot.TryTake();
	ASSERT_TRUE(Handler);
	Slot.Set({});
	EXPECT_FALSE(Lifetime.expired());
	Handler("message");
	Handler = {};
	EXPECT_TRUE(Lifetime.expired());
}

TEST(AssertHandlerSlot, TakingDoesNotCopyAnAllocationFailingTarget)
{
	struct STarget
	{
		bool *m_pRejectCopy;
		int *m_pCalls;
		STarget(bool *pReject, int *pCalls) :
			m_pRejectCopy(pReject), m_pCalls(pCalls) {}
		STarget(const STarget &Other) :
			m_pRejectCopy(Other.m_pRejectCopy), m_pCalls(Other.m_pCalls)
		{
			if(*m_pRejectCopy)
				throw std::bad_alloc();
		}
		void operator()(const char *) const { ++*m_pCalls; }
	};
	CAssertHandlerSlot Slot;
	bool RejectCopy = false;
	int Calls = 0;
	Slot.Set(STarget(&RejectCopy, &Calls));
	RejectCopy = true;
	auto Handler = Slot.TryTake();
	ASSERT_TRUE(Handler);
	Handler("message");
	EXPECT_EQ(Calls, 1);
}

TEST(AssertHandlerSlot, ReplacedCaptureCanTakeNewCallbackDuringItsDestruction)
{
	CAssertHandlerSlot Slot;
	int NewCalls = 0;
	auto pValue = std::shared_ptr<int>(new int(1), [&](int *pData) {
		delete pData;
		auto Handler = Slot.TryTake();
		if(Handler)
			Handler("old capture released");
	});
	Slot.Set([pValue](const char *) {});
	pValue.reset();
	Slot.Set([&](const char *) { ++NewCalls; });
	EXPECT_EQ(NewCalls, 1);
	EXPECT_FALSE(Slot.TryTake());
}

TEST(AssertHandlerSlot, NewRegistrationDoesNotReplaceAlreadyTakenCallback)
{
	CAssertHandlerSlot Slot;
	int OldCalls = 0, NewCalls = 0;
	Slot.Set([&](const char *) { ++OldCalls; });
	auto Old = Slot.TryTake();
	ASSERT_TRUE(Old);
	Slot.Set([&](const char *) { ++NewCalls; });
	Old("old");
	auto New = Slot.TryTake();
	ASSERT_TRUE(New);
	New("new");
	EXPECT_EQ(OldCalls, 1);
	EXPECT_EQ(NewCalls, 1);
}

TEST(AssertHandlerSlot, ConcurrentConsumersShareOneOwnedCallback)
{
	struct SState
	{
		CAssertHandlerSlot m_Slot;
		std::atomic<int> m_Ready{0}, m_Calls{0};
		std::atomic<bool> m_Start{false};
	};
	auto pState = std::make_shared<SState>();
	pState->m_Slot.Set([pState](const char *) { ++pState->m_Calls; });
	ASSERT_TRUE(WaitForAsyncIo([pState] {
		std::vector<std::thread> vReaders;
		try
		{
			for(int Index = 0; Index < 8; ++Index)
				vReaders.emplace_back([pState] {
					++pState->m_Ready;
					const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
					while(!pState->m_Start && std::chrono::steady_clock::now() < Deadline)
						std::this_thread::yield();
					auto Handler = pState->m_Slot.TryTake();
					if(Handler)
						Handler("concurrent");
				});
		}
		catch(...)
		{
			pState->m_Start = true;
			for(auto &Reader : vReaders)
				Reader.join();
			throw;
		}
		const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while(pState->m_Ready < 8 && std::chrono::steady_clock::now() < Deadline)
			std::this_thread::yield();
		pState->m_Start = true;
		for(auto &Reader : vReaders)
			Reader.join();
		// try_lock允许拒绝；读者全部结束后再消费尚未取出的唯一回调。
		auto Remaining = pState->m_Slot.TryTake();
		if(Remaining)
			Remaining("remaining");
		pState->m_Slot.Set({});
	}));
	EXPECT_EQ(pState->m_Ready, 8);
	EXPECT_EQ(pState->m_Calls, 1);
}
