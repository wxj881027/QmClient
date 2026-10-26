#include <base/borrowed_allocator.h>

#include <game/client/components/qmclient/community_icon_scan.h>

#include <gtest/gtest.h>

#include <deque>
#include <memory>
#include <new>
#include <vector>

namespace
{
	using qm_community_icons::CScan;
	using qm_community_icons::EActive;
	using qm_community_icons::SRequest;

	SHA256_DIGEST Digest(unsigned char Value)
	{
		SHA256_DIGEST Result{};
		Result.data[0] = Value;
		return Result;
	}

	std::vector<CCommunity> Communities(int Count)
	{
		std::vector<CCommunity> Result;
		for(int Index = 0; Index < Count; ++Index)
		{
			char aId[16], aUrl[128];
			str_format(aId, sizeof(aId), "c%d", Index);
			str_format(aUrl, sizeof(aUrl), "https://example.test/c%d", Index);
			Result.emplace_back(aId, aId, Digest(1), aUrl);
		}
		return Result;
	}

	bool Missing(const SRequest &) { return false; }
	EActive Idle(const SRequest &) { return EActive::NONE; }

	class CFailureResource : public IAllocationResource
	{
		void *Allocate(std::size_t Bytes, std::size_t Alignment) override
		{
			if(m_Fail)
				throw std::bad_alloc();
			void *pMemory = IAllocationResource::AllocateDefault(Bytes, Alignment);
			++m_Live;
			return pMemory;
		}
		void Deallocate(void *pMemory, std::size_t Bytes, std::size_t Alignment) noexcept override
		{
			--m_Live;
			IAllocationResource::DeallocateDefault(pMemory, Bytes, Alignment);
		}

	public:
		bool m_Fail = false;
		int m_Live = 0;
	};

	class QmCommunityIconScan : public ::testing::Test
	{
	protected:
		CScan m_Scan;
		std::vector<SRequest> m_Started;
		bool Start(const SRequest &Request)
		{
			m_Started.push_back(Request);
			return true;
		}
		void Pump(std::size_t Active = 0, std::size_t Limit = 4)
		{
			m_Scan.Pump(Active, Limit, Missing, Idle, [&](const auto &Request) { return Start(Request); });
		}
	};
}

TEST_F(QmCommunityIconScan, MissingSubmittedTargetsDoNotRepeatAndTailContinues)
{
	const auto Input = Communities(7);
	ASSERT_TRUE(m_Scan.Prepare(Digest(10), Input));
	Pump();
	ASSERT_EQ(m_Started.size(), 4u);
	EXPECT_EQ(m_Scan.Pending(), 3u);
	// 已提交的请求即使失败且仍无缓存，也不能在相同信息中抢占后续目标。
	Pump();
	ASSERT_EQ(m_Started.size(), 7u);
	EXPECT_EQ(m_Scan.Pending(), 0u);
	for(int Index = 0; Index < 7; ++Index)
		EXPECT_STREQ(m_Started[Index].m_aId, Input[Index].Id());
	Pump();
	EXPECT_EQ(m_Started.size(), 7u);
}

TEST_F(QmCommunityIconScan, ExistingActiveJobsConsumeTheFourSlotBudget)
{
	ASSERT_TRUE(m_Scan.Prepare(Digest(10), Communities(3)));
	Pump(4);
	EXPECT_TRUE(m_Started.empty());
	EXPECT_EQ(m_Scan.Pending(), 3u);
	Pump(3);
	EXPECT_EQ(m_Started.size(), 1u);
	EXPECT_EQ(m_Scan.Pending(), 2u);
}

TEST_F(QmCommunityIconScan, MissingRemoteDigestAndCurrentCacheDoNotStartDownloads)
{
	auto Input = Communities(2);
	Input.emplace_back("local", "local", std::nullopt, "");
	ASSERT_TRUE(m_Scan.Prepare(Digest(10), Input));
	m_Scan.Pump(0, 4, [](const auto &Request) { return str_comp(Request.m_aId, "c0") == 0; }, Idle, [&](const auto &Request) { return Start(Request); });
	ASSERT_EQ(m_Started.size(), 1u);
	EXPECT_STREQ(m_Started[0].m_aId, "c1");
	EXPECT_EQ(m_Scan.Pending(), 0u);
}

TEST_F(QmCommunityIconScan, MatchingInFlightTargetsAreAdoptedWithoutStartingAgain)
{
	ASSERT_TRUE(m_Scan.Prepare(Digest(10), Communities(2)));
	m_Scan.Pump(2, 4, Missing, [](const auto &) { return EActive::SAME; }, [&](const auto &Request) { return Start(Request); });
	EXPECT_TRUE(m_Started.empty());
	EXPECT_EQ(m_Scan.Pending(), 0u);
	Pump();
	EXPECT_TRUE(m_Started.empty());
}

TEST_F(QmCommunityIconScan, ForeignVersionWaitsWithoutBlockingOtherCommunities)
{
	ASSERT_TRUE(m_Scan.Prepare(Digest(10), Communities(2)));
	m_Scan.Pump(1, 4, Missing, [](const auto &Request) { return str_comp(Request.m_aId, "c0") == 0 ? EActive::OTHER : EActive::NONE; }, [&](const auto &Request) { return Start(Request); });
	ASSERT_EQ(m_Started.size(), 1u);
	EXPECT_STREQ(m_Started[0].m_aId, "c1");
	EXPECT_EQ(m_Scan.Pending(), 1u);
	Pump();
	ASSERT_EQ(m_Started.size(), 2u);
	EXPECT_STREQ(m_Started[1].m_aId, "c0");
}

TEST_F(QmCommunityIconScan, NewMetadataRetainsItsNewDigestWhileOldRequestFinishes)
{
	ASSERT_TRUE(m_Scan.Prepare(Digest(10), Communities(1)));
	Pump();
	std::vector<CCommunity> Changed;
	Changed.emplace_back("c0", "c0", Digest(2), "https://example.test/new");
	ASSERT_TRUE(m_Scan.Prepare(Digest(11), Changed));
	m_Scan.Pump(1, 4, Missing, [](const auto &) { return EActive::OTHER; }, [&](const auto &Request) { return Start(Request); });
	EXPECT_EQ(m_Started.size(), 1u);
	EXPECT_EQ(m_Scan.Pending(), 1u);
	Pump();
	ASSERT_EQ(m_Started.size(), 2u);
	EXPECT_EQ(m_Started[1].m_Sha256, Digest(2));
	EXPECT_STREQ(m_Started[1].m_aUrl, "https://example.test/new");
}

TEST(QmCommunityIconRequest, TargetIdentityIncludesIdUrlAndDigest)
{
	SRequest Request;
	str_copy(Request.m_aId, "one");
	str_copy(Request.m_aUrl, "https://example.test/one");
	Request.m_Sha256 = Digest(1);
	EXPECT_TRUE(Request.Matches("one", "https://example.test/one", Digest(1)));
	EXPECT_FALSE(Request.Matches("other", "https://example.test/one", Digest(1)));
	EXPECT_FALSE(Request.Matches("one", "https://example.test/new", Digest(1)));
	EXPECT_FALSE(Request.Matches("one", "https://example.test/one", Digest(2)));
}

TEST_F(QmCommunityIconScan, PreparingSameSnapshotPreservesRemainingWork)
{
	const auto Input = Communities(5);
	ASSERT_TRUE(m_Scan.Prepare(Digest(10), Input));
	Pump();
	ASSERT_TRUE(m_Scan.Prepare(Digest(10), Input));
	EXPECT_EQ(m_Scan.Pending(), 1u);
	Pump();
	EXPECT_EQ(m_Started.size(), 5u);
}

TEST_F(QmCommunityIconScan, RejectedStartKeepsTargetPendingForRecovery)
{
	ASSERT_TRUE(m_Scan.Prepare(Digest(10), Communities(1)));
	m_Scan.Pump(0, 4, Missing, Idle, [](const auto &) { return false; });
	EXPECT_EQ(m_Scan.Pending(), 1u);
	Pump();
	EXPECT_EQ(m_Started.size(), 1u);
	EXPECT_EQ(m_Scan.Pending(), 0u);
}

TEST_F(QmCommunityIconScan, ThrowingStartDoesNotConsumeTarget)
{
	ASSERT_TRUE(m_Scan.Prepare(Digest(10), Communities(1)));
	m_Scan.Pump(0, 4, Missing, Idle, [](const auto &) -> bool { throw std::bad_alloc(); });
	EXPECT_EQ(m_Scan.Pending(), 1u);
	Pump();
	EXPECT_EQ(m_Started.size(), 1u);
}

TEST(QmCommunityIconPreparation, AllocationFailurePreservesOldSnapshotAndCanRetry)
{
	CFailureResource Memory;
	{
		CScan Scan(&Memory);
		ASSERT_TRUE(Scan.Prepare(Digest(10), Communities(1)));
		Memory.m_Fail = true;
		EXPECT_FALSE(Scan.Prepare(Digest(11), Communities(3)));
		EXPECT_TRUE(Scan.Matches(Digest(10)));
		EXPECT_FALSE(Scan.Matches(Digest(11)));
		EXPECT_EQ(Scan.Pending(), 1u);
		Memory.m_Fail = false;
		ASSERT_TRUE(Scan.Prepare(Digest(11), Communities(3)));
		EXPECT_EQ(Scan.Pending(), 3u);
	}
	EXPECT_EQ(Memory.m_Live, 0);
}

TEST_F(QmCommunityIconScan, ResetAllowsFreshSessionToUseSameSnapshot)
{
	const auto Input = Communities(1);
	ASSERT_TRUE(m_Scan.Prepare(Digest(10), Input));
	Pump();
	m_Scan.Reset();
	EXPECT_FALSE(m_Scan.Matches(Digest(10)));
	ASSERT_TRUE(m_Scan.Prepare(Digest(10), Input));
	Pump();
	EXPECT_EQ(m_Started.size(), 2u);
}

TEST(QmCommunityIconResult, UnknownMetadataKeepsLocalCacheFallback)
{
	EXPECT_TRUE(qm_community_icons::AcceptsResult(false, nullptr, Digest(1)));
}

TEST(QmCommunityIconResult, RemovedCommunityCannotPublishLateResult)
{
	EXPECT_FALSE(qm_community_icons::AcceptsResult(true, nullptr, Digest(1)));
}

TEST(QmCommunityIconResult, CommunityWithoutRemoteDigestKeepsLocalCacheFallback)
{
	CCommunity Community("one", "one", std::nullopt, "");
	EXPECT_TRUE(qm_community_icons::AcceptsResult(true, &Community, Digest(1)));
}

TEST(QmCommunityIconResult, KnownDigestRejectsStalePixels)
{
	CCommunity Community("one", "one", Digest(2), "https://example.test/one");
	EXPECT_TRUE(qm_community_icons::AcceptsResult(true, &Community, Digest(2)));
	EXPECT_FALSE(qm_community_icons::AcceptsResult(true, &Community, Digest(1)));
}

TEST(QmCommunityIconHandoff, ObserverExistsBeforeExternalSubmission)
{
	std::deque<std::shared_ptr<int>> Queue;
	EXPECT_TRUE(qm_community_icons::ObserveAndSubmit(Queue, [] { return std::make_shared<int>(7); }, [&](const auto &pJob) {
		EXPECT_EQ(Queue.size(), 1u);
		EXPECT_EQ(Queue.back(), pJob);
		return true; }));
	ASSERT_EQ(Queue.size(), 1u);
	EXPECT_EQ(*Queue.front(), 7);
}

TEST(QmCommunityIconHandoff, FactoryFailureLeavesQueueAndSubmitUntouched)
{
	std::deque<std::shared_ptr<int>> Queue;
	int Submits = 0;
	EXPECT_FALSE(qm_community_icons::ObserveAndSubmit(Queue, []() -> std::shared_ptr<int> { throw std::bad_alloc(); }, [&](const auto &) { ++Submits; return true; }));
	EXPECT_TRUE(Queue.empty());
	EXPECT_EQ(Submits, 0);
}

TEST(QmCommunityIconHandoff, RejectionRemovesOnlyNewObserverAndReleasesJob)
{
	std::deque<std::shared_ptr<int>> Queue;
	auto pOld = std::make_shared<int>(1);
	Queue.push_back(pOld);
	std::weak_ptr<int> New;
	EXPECT_FALSE(qm_community_icons::ObserveAndSubmit(Queue, [&] { auto pJob = std::make_shared<int>(2); New = pJob; return pJob; }, [](const auto &) { return false; }));
	ASSERT_EQ(Queue.size(), 1u);
	EXPECT_EQ(Queue.front(), pOld);
	EXPECT_TRUE(New.expired());
}

TEST(QmCommunityIconHandoff, QueueAllocationFailureNeverStartsUnobservedJob)
{
	CFailureResource Memory;
	{
		std::deque<std::shared_ptr<int>, CBorrowedAllocator<std::shared_ptr<int>>> Queue(&Memory);
		Memory.m_Fail = true;
		bool Rejected = false;
		for(int Attempt = 0; Attempt < 1024 && !Rejected; ++Attempt)
		{
			const auto Before = Queue.size();
			int Submits = 0;
			const bool Accepted = qm_community_icons::ObserveAndSubmit(Queue, [] { return std::make_shared<int>(7); }, [&](const auto &) { ++Submits; return true; });
			if(!Accepted)
			{
				Rejected = true;
				EXPECT_EQ(Queue.size(), Before);
				EXPECT_EQ(Submits, 0);
			}
		}
		EXPECT_TRUE(Rejected);
	}
	EXPECT_EQ(Memory.m_Live, 0);
}

TEST(QmCommunityIconHandoff, SubmissionExceptionRollsBackObserverAndAllowsRetry)
{
	std::deque<std::shared_ptr<int>> Queue;
	EXPECT_FALSE(qm_community_icons::ObserveAndSubmit(Queue, [] { return std::make_shared<int>(1); }, [](const auto &) -> bool { throw std::bad_alloc(); }));
	EXPECT_TRUE(Queue.empty());
	EXPECT_TRUE(qm_community_icons::ObserveAndSubmit(Queue, [] { return std::make_shared<int>(2); }, [](const auto &) { return true; }));
	ASSERT_EQ(Queue.size(), 1u);
	EXPECT_EQ(*Queue.front(), 2);
}
