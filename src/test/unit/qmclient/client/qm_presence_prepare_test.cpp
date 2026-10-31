#include <game/client/components/qmclient/qm_presence_prepare.h>
#include <game/client/components/qmclient/qmclient_utils.h>

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

TEST(QmPresencePrepare, SuccessfulPreparationPublishesCompleteResult)
{
	SQmTitlePresenceParseResult Out;
	ASSERT_TRUE(TryPrepareQmPresenceResult(Out, [](auto &Candidate) {
		Candidate.m_Parsed = true;
		Candidate.m_ServerTime = 1000;
		Candidate.m_vPresences.push_back({3, "Name", "Title", "gold", 10, EQmSponsorChatStyle::NONE, {}});
		return true;
	}));
	EXPECT_TRUE(Out.m_Parsed);
	EXPECT_EQ(Out.m_ServerTime, 1000);
	ASSERT_EQ(Out.m_vPresences.size(), 1u);
	EXPECT_EQ(Out.m_vPresences.front().m_Title, "Title");
}

TEST(QmPresencePrepare, RejectedPartialCandidateLeavesEmptyDeveloperOutput)
{
	SQmDeveloperPresenceParseResult Out{true, 500, {SQmDeveloperPresence{}}};
	EXPECT_FALSE(TryPrepareQmPresenceResult(Out, [](auto &Candidate) {
		Candidate.m_Parsed = true;
		Candidate.m_ServerTime = 1000;
		Candidate.m_vPresences.emplace_back();
		return false;
	}));
	EXPECT_FALSE(Out.m_Parsed);
	EXPECT_EQ(Out.m_ServerTime, 0);
	EXPECT_TRUE(Out.m_vPresences.empty());
}

TEST(QmPresencePrepare, AllocationExceptionDoesNotPublishParsedTimeOrPrefix)
{
	SQmTitlePresenceParseResult Out{true, 500, {SQmTitlePresence{}}};
	EXPECT_FALSE(TryPrepareQmPresenceResult(Out, [](auto &Candidate) -> bool {
		Candidate.m_Parsed = true;
		Candidate.m_ServerTime = 1000;
		Candidate.m_vPresences.emplace_back();
		throw std::bad_alloc();
	}));
	EXPECT_FALSE(Out.m_Parsed);
	EXPECT_EQ(Out.m_ServerTime, 0);
	EXPECT_TRUE(Out.m_vPresences.empty());
}

TEST(QmPresencePrepare, FailedCandidateReleasesItsOwnedResource)
{
	struct SOwnedResult
	{
		std::shared_ptr<int> m_pResource;
	};
	SOwnedResult Out;
	std::weak_ptr<int> Resource;
	EXPECT_FALSE(TryPrepareQmPresenceResult(Out, [&](auto &Candidate) -> bool {
		Candidate.m_pResource = std::make_shared<int>(7);
		Resource = Candidate.m_pResource;
		throw std::bad_alloc();
	}));
	EXPECT_EQ(Out.m_pResource, nullptr);
	EXPECT_TRUE(Resource.expired());
}

TEST(QmPresencePrepare, SuccessfulPreparationCanFollowAllocationFailure)
{
	SQmDeveloperPresenceParseResult Out;
	ASSERT_FALSE(TryPrepareQmPresenceResult(Out, [](auto &) -> bool { throw std::bad_alloc(); }));
	ASSERT_TRUE(TryPrepareQmPresenceResult(Out, [](auto &Candidate) {
		Candidate.m_Parsed = true;
		Candidate.m_ServerTime = 2000;
		return true;
	}));
	EXPECT_TRUE(Out.m_Parsed);
	EXPECT_EQ(Out.m_ServerTime, 2000);
}

TEST(QmPresencePrepare, SuccessfulEmptySnapshotDiffersFromFailedPreparation)
{
	SQmTitlePresenceParseResult Out;
	ASSERT_TRUE(TryPrepareQmPresenceResult(Out, [](auto &Candidate) {
		Candidate.m_Parsed = true;
		Candidate.m_ServerTime = 1000;
		return true;
	}));
	EXPECT_TRUE(Out.m_Parsed);
	EXPECT_EQ(Out.m_ServerTime, 1000);
	EXPECT_TRUE(Out.m_vPresences.empty());
	ASSERT_FALSE(TryPrepareQmPresenceResult(Out, [](auto &) { return false; }));
	EXPECT_FALSE(Out.m_Parsed);
	EXPECT_EQ(Out.m_ServerTime, 0);
}

TEST(QmPresencePrepare, UnexpectedLogicExceptionsAreNotHidden)
{
	SQmTitlePresenceParseResult Out;
	EXPECT_THROW(TryPrepareQmPresenceResult(Out, [](auto &) -> bool { throw std::logic_error("unexpected"); }), std::logic_error);
	EXPECT_FALSE(Out.m_Parsed);
}
