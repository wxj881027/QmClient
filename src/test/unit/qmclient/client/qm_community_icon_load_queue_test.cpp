#include <game/client/components/qmclient/community_icon_scan.h>

#include <gtest/gtest.h>

#include <memory>
#include <new>
#include <stdexcept>
#include <vector>

namespace
{
	using CQueue = qm_community_icons::CLoadQueue<int>;
	bool Enqueue(CQueue &Queue, int Id)
	{
		return Queue.Enqueue([=] { return std::make_shared<int>(Id); });
	}
}

TEST(QmCommunityIconLoadQueue, RegistrationOwnsJobBeforeAnyExternalSubmission)
{
	CQueue Queue;
	auto pJob = std::make_shared<int>(7);
	std::weak_ptr<int> Weak = pJob;
	ASSERT_TRUE(Queue.Enqueue([&] { return pJob; }));
	pJob.reset();
	EXPECT_FALSE(Weak.expired());
	EXPECT_EQ(Queue.Submitted(), 0u);
	int Calls = 0;
	Queue.SubmitPending(4, [&](const auto &pSubmitted) {
		++Calls;
		EXPECT_EQ(Queue.Front(), pSubmitted);
		EXPECT_EQ(Queue.Size(), 1u);
		return true;
	});
	EXPECT_EQ(Calls, 1);
	EXPECT_EQ(Queue.Submitted(), 1u);
	Queue.PopFront();
	EXPECT_TRUE(Weak.expired());
}

TEST(QmCommunityIconLoadQueue, RetainedResultsOccupyAllSlotsUntilConsumed)
{
	CQueue Queue;
	for(int Id = 0; Id < 7; ++Id)
		ASSERT_TRUE(Enqueue(Queue, Id));
	std::vector<int> Submitted;
	const auto Submit = [&](const auto &pJob) { Submitted.push_back(*pJob); return true; };
	Queue.SubmitPending(4, Submit);
	Queue.SubmitPending(4, Submit);
	EXPECT_EQ(Submitted, (std::vector<int>{0, 1, 2, 3}));
	EXPECT_EQ(Queue.Submitted(), 4u);
	Queue.PopFront();
	Queue.SubmitPending(4, Submit);
	EXPECT_EQ(Submitted, (std::vector<int>{0, 1, 2, 3, 4}));
	EXPECT_EQ(Queue.Submitted(), 4u);
}

TEST(QmCommunityIconLoadQueue, ZeroBudgetDoesNotStartQueuedJobs)
{
	CQueue Queue;
	ASSERT_TRUE(Enqueue(Queue, 1));
	int Calls = 0;
	Queue.SubmitPending(0, [&](const auto &) { ++Calls; return true; });
	EXPECT_EQ(Calls, 0);
	EXPECT_EQ(Queue.Submitted(), 0u);
	EXPECT_EQ(Queue.Size(), 1u);
}

TEST(QmCommunityIconLoadQueue, RejectedSubmissionRetainsObserverAndRetriesInOrder)
{
	CQueue Queue;
	ASSERT_TRUE(Enqueue(Queue, 1));
	ASSERT_TRUE(Enqueue(Queue, 2));
	std::vector<int> Attempts;
	Queue.SubmitPending(4, [&](const auto &pJob) { Attempts.push_back(*pJob); return false; });
	EXPECT_EQ(Queue.Submitted(), 0u);
	EXPECT_EQ(Queue.Size(), 2u);
	EXPECT_EQ(Attempts, (std::vector<int>{1}));
	Queue.SubmitPending(4, [&](const auto &pJob) { Attempts.push_back(*pJob); return true; });
	EXPECT_EQ(Attempts, (std::vector<int>{1, 1, 2}));
	EXPECT_EQ(Queue.Submitted(), 2u);
}

TEST(QmCommunityIconLoadQueue, ThrowingSubmissionLeavesJobAvailableForRetry)
{
	CQueue Queue;
	ASSERT_TRUE(Enqueue(Queue, 3));
	Queue.SubmitPending(4, [](const auto &) -> bool { throw std::runtime_error("not accepted"); });
	EXPECT_EQ(Queue.Submitted(), 0u);
	EXPECT_EQ(*Queue.Front(), 3);
	Queue.SubmitPending(4, [](const auto &) { return true; });
	EXPECT_EQ(Queue.Submitted(), 1u);
}

TEST(QmCommunityIconLoadQueue, FactoryFailureOrNullDoesNotDisturbExistingObserver)
{
	CQueue Queue;
	ASSERT_TRUE(Enqueue(Queue, 1));
	EXPECT_FALSE(Queue.Enqueue([]() -> std::shared_ptr<int> { throw std::bad_alloc(); }));
	EXPECT_FALSE(Queue.Enqueue([] { return std::shared_ptr<int>{}; }));
	EXPECT_EQ(Queue.Size(), 1u);
	EXPECT_EQ(*Queue.Front(), 1);
}

TEST(QmCommunityIconLoadQueue, ClearReleasesUnsubmittedJobsAndResetsAdmission)
{
	CQueue Queue;
	ASSERT_TRUE(Enqueue(Queue, 1));
	Queue.PopFront();
	EXPECT_EQ(Queue.Submitted(), 0u);
	ASSERT_TRUE(Enqueue(Queue, 2));
	Queue.SubmitPending(4, [](const auto &) { return true; });
	Queue.Clear();
	EXPECT_TRUE(Queue.Empty());
	EXPECT_EQ(Queue.Submitted(), 0u);
	ASSERT_TRUE(Enqueue(Queue, 3));
	Queue.SubmitPending(1, [](const auto &) { return true; });
	EXPECT_EQ(Queue.Submitted(), 1u);
}

TEST(QmCommunityIconLoadQueue, FullCacheListContinuesBeyondFirstAdmissionBatch)
{
	CQueue Queue;
	for(int Id = 0; Id < 13; ++Id)
		ASSERT_TRUE(Enqueue(Queue, Id));
	std::vector<int> Submitted;
	for(int Id = 0; Id < 13; ++Id)
	{
		Queue.SubmitPending(4, [&](const auto &pJob) { Submitted.push_back(*pJob); return true; });
		ASSERT_FALSE(Queue.Empty());
		ASSERT_GT(Queue.Submitted(), 0u);
		EXPECT_LE(Queue.Submitted(), 4u);
		EXPECT_EQ(*Queue.Front(), Id);
		Queue.PopFront();
	}
	EXPECT_TRUE(Queue.Empty());
	ASSERT_EQ(Submitted.size(), 13u);
	for(int Id = 0; Id < 13; ++Id)
		EXPECT_EQ(Submitted[Id], Id);
}
