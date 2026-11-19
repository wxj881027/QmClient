#include <engine/client/input_ime.h>

#include <gtest/gtest.h>

#include <deque>
#include <memory>
#include <new>
#include <stdexcept>
#include <vector>

namespace
{
	struct SAllocationState
	{
		bool m_Fail = false;
		int m_Failures = 0;
	};

	template<typename T>
	struct CFailingAllocator
	{
		using value_type = T;
		SAllocationState *m_pState;

		explicit CFailingAllocator(SAllocationState *pState) :
			m_pState(pState) {}
		template<typename U>
		CFailingAllocator(const CFailingAllocator<U> &Other) :
			m_pState(Other.m_pState)
		{
		}
		T *allocate(size_t Count)
		{
			if(m_pState->m_Fail)
			{
				++m_pState->m_Failures;
				throw std::bad_alloc();
			}
			return std::allocator<T>{}.allocate(Count);
		}
		void deallocate(T *pData, size_t Count) { std::allocator<T>{}.deallocate(pData, Count); }
		template<typename U>
		bool operator==(const CFailingAllocator<U> &Other) const
		{
			return m_pState == Other.m_pState;
		}
	};
}

TEST(NativeImeMessageQueue, SuccessfulNotificationsKeepMessageOrder)
{
	std::deque<int> Queue;
	std::vector<int> vNotifications;
	for(int Message : {10, 20, 30})
	{
		EXPECT_TRUE(QueueNativeImeMessage(Queue, Message, [&] {
			vNotifications.push_back(Queue.back());
			return true;
		}));
	}
	EXPECT_EQ(Queue, (std::deque<int>{10, 20, 30}));
	EXPECT_EQ(vNotifications, (std::vector<int>{10, 20, 30}));
}

TEST(NativeImeMessageQueue, RejectedNotificationRemovesOnlyTheNewMessage)
{
	std::deque<int> Queue{10, 20};
	EXPECT_FALSE(QueueNativeImeMessage(Queue, 30, [&] {
		EXPECT_EQ(Queue.back(), 30);
		return false;
	}));
	EXPECT_EQ(Queue, (std::deque<int>{10, 20}));
}

TEST(NativeImeMessageQueue, NotificationExceptionRollsBackWithoutEscaping)
{
	std::deque<int> Queue{10};
	bool Queued = true;
	EXPECT_NO_THROW(Queued = QueueNativeImeMessage(Queue, 20, []() -> bool { throw 7; }));
	EXPECT_FALSE(Queued);
	EXPECT_EQ(Queue, (std::deque<int>{10}));
}

TEST(NativeImeMessageQueue, DequeAllocationFailureDoesNotPublishANotification)
{
	SAllocationState Allocation;
	std::deque<int, CFailingAllocator<int>> Queue{CFailingAllocator<int>(&Allocation)};
	std::vector<int> vAccepted;
	int Notifications = 0;
	bool Failed = false;
	Allocation.m_Fail = true;
	// 容器可能复用首块存储；有界追加直到真实 deque 分配被拒绝。
	for(int Message = 0; Message < 4096; ++Message)
	{
		bool Queued = true;
		EXPECT_NO_THROW(Queued = QueueNativeImeMessage(Queue, Message, [&] {
			++Notifications;
			return true;
		}));
		if(!Queued)
		{
			Failed = true;
			break;
		}
		vAccepted.push_back(Message);
	}
	Allocation.m_Fail = false;
	ASSERT_TRUE(Failed);
	EXPECT_GT(Allocation.m_Failures, 0);
	EXPECT_EQ(Notifications, static_cast<int>(vAccepted.size()));
	EXPECT_EQ((std::vector<int>{Queue.begin(), Queue.end()}), vAccepted);
	ASSERT_TRUE(QueueNativeImeMessage(Queue, 9999, [] { return true; }));
	EXPECT_EQ(Queue.back(), 9999);
}

TEST(NativeImeMessageQueue, NextNotificationSucceedsAfterRejection)
{
	std::deque<int> Queue{10};
	ASSERT_FALSE(QueueNativeImeMessage(Queue, 20, [] { return false; }));
	ASSERT_TRUE(QueueNativeImeMessage(Queue, 30, [] { return true; }));
	EXPECT_EQ(Queue, (std::deque<int>{10, 30}));
}

TEST(NativeImeCandidates, SuccessfulReadReleasesContextAfterSnapshotIsReady)
{
	int Context = 7;
	int Releases = 0;
	const auto Result = ReadNativeImeCandidates(&Context, [&](int *pContext) noexcept {
		EXPECT_EQ(pContext, &Context);
		++Releases; }, [&](int *pContext) -> std::optional<SQmImeCandidateList> {
		EXPECT_EQ(pContext, &Context);
		EXPECT_EQ(Releases, 0);
		return SQmImeCandidateList{{"候选", "第二项"}, 1, 10, 5, 23}; });
	ASSERT_TRUE(Result.has_value());
	EXPECT_EQ(Result->m_vCandidates, (std::vector<std::string>{"候选", "第二项"}));
	EXPECT_EQ(Result->m_SelectedIndex, 1);
	EXPECT_EQ(Result->m_PageStart, 10);
	EXPECT_EQ(Result->m_PageSize, 5);
	EXPECT_EQ(Result->m_TotalCount, 23);
	EXPECT_EQ(Releases, 1);
}

TEST(NativeImeCandidates, NullContextIsNotReleased)
{
	int Releases = 0;
	const auto Result = ReadNativeImeCandidates(static_cast<int *>(nullptr), [&](int *) noexcept { ++Releases; }, [](int *pContext) -> std::optional<SQmImeCandidateList> {
		EXPECT_EQ(pContext, nullptr);
		return std::nullopt; });
	EXPECT_FALSE(Result.has_value());
	EXPECT_EQ(Releases, 0);
}

TEST(NativeImeCandidates, UnavailableListStillReleasesTheContext)
{
	int Context = 7;
	int Releases = 0;
	EXPECT_FALSE(ReadNativeImeCandidates(&Context, [&](int *) noexcept { ++Releases; }, [](int *) -> std::optional<SQmImeCandidateList> { return std::nullopt; }).has_value());
	EXPECT_EQ(Releases, 1);
}

TEST(NativeImeCandidates, ReadExceptionsReturnNoSnapshotAndReleaseTheContext)
{
	for(int Kind : {0, 1, 2})
	{
		SCOPED_TRACE(Kind);
		int Context = 7;
		int Releases = 0;
		std::optional<SQmImeCandidateList> Result;
		EXPECT_NO_THROW(Result = ReadNativeImeCandidates(&Context, [&](int *) noexcept { ++Releases; }, [Kind](int *) -> std::optional<SQmImeCandidateList> {
			SQmImeCandidateList Partial;
			Partial.m_vCandidates = {"已转换候选"};
			switch(Kind)
			{
			case 0: throw std::bad_alloc();
			case 1: throw std::length_error("native candidate conversion");
			default: throw 7;
			} }));
		EXPECT_FALSE(Result.has_value());
		EXPECT_EQ(Releases, 1);
	}
}

TEST(NativeImeCandidates, FailedReadDoesNotPreventTheNextSnapshot)
{
	int Context = 7;
	int Releases = 0;
	const auto Release = [&](int *) noexcept { ++Releases; };
	ASSERT_FALSE(ReadNativeImeCandidates(&Context, Release, [](int *) -> std::optional<SQmImeCandidateList> { throw std::bad_alloc(); }).has_value());
	const auto Result = ReadNativeImeCandidates(&Context, Release, [](int *) -> std::optional<SQmImeCandidateList> { return SQmImeCandidateList{{"重试"}, 0, 0, 1, 1}; });
	ASSERT_TRUE(Result.has_value());
	EXPECT_EQ(Result->m_vCandidates, (std::vector<std::string>{"重试"}));
	EXPECT_EQ(Releases, 2);
}
