#include <game/client/components/qmclient/chat_emoji.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

namespace
{
	std::shared_ptr<CQmChatEmojiLoadJob> EmptyJob()
	{
		return std::make_shared<CQmChatEmojiLoadJob>([](CImageInfo &) {});
	}
}

TEST(QmChatEmojiLoadState, InvalidEnumsDoNotEnterTheQueue)
{
	CQmChatEmojiLoadState State;
	EXPECT_FALSE(State.Request(EQmChatEmoji::NONE));
	EXPECT_FALSE(State.Request(EQmChatEmoji::COUNT));
	EXPECT_FALSE(State.Request(static_cast<EQmChatEmoji>(-1)));
	EXPECT_EQ(State.PendingCount(), 0u);
}

TEST(QmChatEmojiLoadState, DuplicateRequestsKeepASingleQueueEntry)
{
	CQmChatEmojiLoadState State;
	EXPECT_TRUE(State.Request(EQmChatEmoji::LOVE));
	EXPECT_FALSE(State.Request(EQmChatEmoji::LOVE));
	EXPECT_EQ(State.PendingCount(), 1u);
}

TEST(QmChatEmojiLoadState, EveryKnownEmojiCanBeQueuedWithoutAllocating)
{
	bool Triggered = false;
	size_t Requested = 0, Pending = 0;
	{
		CPredictionAllocationFailure Fault;
		CQmChatEmojiLoadState State;
		for(size_t Index = 1; Index <= QM_CHAT_EMOJI_COUNT; ++Index)
			Requested += State.Request(static_cast<EQmChatEmoji>(Index));
		Pending = State.PendingCount();
		Triggered = Fault.Triggered();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Requested, QM_CHAT_EMOJI_COUNT);
	EXPECT_EQ(Pending, QM_CHAT_EMOJI_COUNT);
}

TEST(QmChatEmojiLoadState, FactoryAllocationFailureKeepsTheFrontForRetry)
{
	CQmChatEmojiLoadState State;
	State.Request(EQmChatEmoji::LOVE);
	State.Request(EQmChatEmoji::NO);
	EXPECT_FALSE(State.TryStart([](EQmChatEmoji) -> std::shared_ptr<CQmChatEmojiLoadJob> { throw std::bad_alloc(); }, [](const auto &) { return true; }));
	EXPECT_EQ(State.PendingCount(), 2u);
	EXPECT_FALSE(State.Job());
	EQmChatEmoji Created = EQmChatEmoji::NONE;
	ASSERT_TRUE(State.TryStart([&](EQmChatEmoji Emoji) {
		Created = Emoji;
		return EmptyJob(); }, [](const auto &) { return true; }));
	EXPECT_EQ(Created, EQmChatEmoji::LOVE);
	EXPECT_EQ(State.PendingCount(), 1u);
}

TEST(QmChatEmojiLoadState, NullCandidateDoesNotAdvanceOrSubmitTheRequest)
{
	CQmChatEmojiLoadState State;
	State.Request(EQmChatEmoji::LOVE);
	bool Submitted = false;
	EXPECT_FALSE(State.TryStart([](EQmChatEmoji) { return std::shared_ptr<CQmChatEmojiLoadJob>(); }, [&](const auto &) {
		Submitted = true;
		return true; }));
	EXPECT_FALSE(Submitted);
	EXPECT_EQ(State.PendingCount(), 1u);
	EXPECT_EQ(State.Loading(), EQmChatEmoji::NONE);
}

TEST(QmChatEmojiLoadState, RejectedAdmissionDoesNotPublishACurrentJob)
{
	CQmChatEmojiLoadState State;
	State.Request(EQmChatEmoji::LOVE);
	auto pJob = EmptyJob();
	EXPECT_FALSE(State.TryStart([&](EQmChatEmoji) { return pJob; }, [](const auto &) { return false; }));
	EXPECT_FALSE(State.Job());
	EXPECT_EQ(State.PendingCount(), 1u);
	ASSERT_TRUE(State.TryStart([&](EQmChatEmoji) { return pJob; }, [](const auto &) { return true; }));
	EXPECT_EQ(State.Job(), pJob);
}

TEST(QmChatEmojiLoadState, AdmissionExceptionPreservesThePendingRequest)
{
	CQmChatEmojiLoadState State;
	State.Request(EQmChatEmoji::LOVE);
	auto pJob = EmptyJob();
	EXPECT_FALSE(State.TryStart([&](EQmChatEmoji) { return pJob; }, [](const auto &) -> bool { throw std::length_error("admission"); }));
	EXPECT_FALSE(State.Job());
	EXPECT_EQ(State.PendingCount(), 1u);
}

TEST(QmChatEmojiLoadState, PublicationFollowsSuccessfulAdmission)
{
	CQmChatEmojiLoadState State;
	State.Request(EQmChatEmoji::LOVE);
	bool UnpublishedDuringAdmission = false;
	ASSERT_TRUE(State.TryStart([](EQmChatEmoji) { return EmptyJob(); }, [&](const auto &) {
		UnpublishedDuringAdmission = !State.Job() && State.Loading() == EQmChatEmoji::NONE && State.PendingCount() == 1;
		return true; }));
	EXPECT_TRUE(UnpublishedDuringAdmission);
	EXPECT_EQ(State.Loading(), EQmChatEmoji::LOVE);
	EXPECT_EQ(State.PendingCount(), 0u);
}

TEST(QmChatEmojiLoadState, AnActiveJobPreventsAnotherCreation)
{
	CQmChatEmojiLoadState State;
	State.Request(EQmChatEmoji::LOVE);
	State.Request(EQmChatEmoji::NO);
	ASSERT_TRUE(State.TryStart([](EQmChatEmoji) { return EmptyJob(); }, [](const auto &) { return true; }));
	bool Created = false;
	EXPECT_FALSE(State.TryStart([&](EQmChatEmoji) {
		Created = true;
		return EmptyJob(); }, [](const auto &) { return true; }));
	EXPECT_FALSE(Created);
	EXPECT_EQ(State.PendingCount(), 1u);
}

TEST(QmChatEmojiLoadState, ClearAllowsTheSameEmojiToBeRequestedAgain)
{
	CQmChatEmojiLoadState State;
	State.Request(EQmChatEmoji::LOVE);
	ASSERT_TRUE(State.TryStart([](EQmChatEmoji) { return EmptyJob(); }, [](const auto &) { return true; }));
	State.Clear();
	EXPECT_FALSE(State.Job());
	EXPECT_EQ(State.Loading(), EQmChatEmoji::NONE);
	EXPECT_TRUE(State.Request(EQmChatEmoji::LOVE));
}

TEST(QmChatEmojiLoadState, PreparedJobAdmissionAndClearDoNotAllocate)
{
	auto pJob = EmptyJob();
	bool Started = false, Triggered = false;
	{
		CPredictionAllocationFailure Fault;
		CQmChatEmojiLoadState State;
		State.Request(EQmChatEmoji::LOVE);
		Started = State.TryStart([&](EQmChatEmoji) { return pJob; }, [](const auto &) { return true; });
		State.Clear();
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Started);
	EXPECT_FALSE(Triggered);
}
