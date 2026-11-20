#include <game/client/components/qmclient/qm_markdown_broadcast.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <string>
#include <utility>

namespace
{
	using EResult = CQmMarkdownBroadcast::EApplyResult;
}

TEST(QmMarkdownBroadcastAllocation, FailedCopyKeepsPublishedStateAndAllowsRetry)
{
	CQmMarkdownBroadcast Broadcast;
	ASSERT_TRUE(Broadcast.Apply("previous", 2));
	const std::string ContentId = Broadcast.ContentId();
	const std::string Incoming(8192, 'x');
	EResult Result;
	bool Triggered;
	{
		CPredictionAllocationFailure Fault;
		Result = Broadcast.TryApply(Incoming, 3);
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_EQ(Result, EResult::RETRY);
	EXPECT_STREQ(Broadcast.Markdown(), "previous");
	EXPECT_EQ(Broadcast.Version(), 2);
	EXPECT_EQ(Broadcast.Revision(), 1);
	EXPECT_EQ(Broadcast.ContentId(), ContentId);
	EXPECT_EQ(Broadcast.TryApply(Incoming, 3), EResult::APPLIED);
	EXPECT_EQ(Broadcast.Markdown(), Incoming);
	EXPECT_EQ(Broadcast.Revision(), 2);
}

TEST(QmMarkdownBroadcastAllocation, OwnedTransferDoesNotAllocateCppStorage)
{
	CQmMarkdownBroadcast Broadcast;
	ASSERT_TRUE(Broadcast.Apply("previous", 2));
	std::string Incoming(8192, 'x');
	const auto *pIncoming = Incoming.data();
	EResult Result;
	bool Triggered;
	{
		CPredictionAllocationFailure Fault;
		Result = Broadcast.ApplyOwned(std::move(Incoming), 3);
		Triggered = Fault.Triggered();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Result, EResult::APPLIED);
	EXPECT_EQ(Broadcast.Markdown(), pIncoming);
	EXPECT_EQ(std::string(Broadcast.Markdown()), std::string(8192, 'x'));
	EXPECT_EQ(Broadcast.Version(), 3);
}

TEST(QmMarkdownBroadcastAllocation, IgnoredInputsDoNotAllocateOrConsumeOwnedText)
{
	CQmMarkdownBroadcast Broadcast;
	std::string Current(8192, 'x');
	ASSERT_TRUE(Broadcast.Apply(Current, 2));
	std::string Stale(8192, 's');
	std::string Oversized(CQmMarkdownBroadcast::MAX_BYTES + 1, 'o');
	EResult SameResult, StaleResult, OversizedResult;
	bool Triggered;
	{
		CPredictionAllocationFailure Fault;
		SameResult = Broadcast.TryApply(Current, 2);
		StaleResult = Broadcast.ApplyOwned(std::move(Stale), 1);
		OversizedResult = Broadcast.ApplyOwned(std::move(Oversized), 3);
		Triggered = Fault.Triggered();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(SameResult, EResult::UNCHANGED);
	EXPECT_EQ(StaleResult, EResult::REJECTED);
	EXPECT_EQ(OversizedResult, EResult::REJECTED);
	EXPECT_EQ(Stale, std::string(8192, 's'));
	EXPECT_EQ(Oversized.size(), CQmMarkdownBroadcast::MAX_BYTES + 1);
	EXPECT_EQ(Broadcast.Version(), 2);
	EXPECT_EQ(Broadcast.Revision(), 1);
}

TEST(QmMarkdownBroadcastAllocation, CopiedResultOwnsContentAfterInputChanges)
{
	CQmMarkdownBroadcast Broadcast;
	std::string Incoming(8192, 'x');
	ASSERT_EQ(Broadcast.TryApply(Incoming, 2), EResult::APPLIED);
	Incoming.assign("changed");
	EXPECT_EQ(std::string(Broadcast.Markdown()), std::string(8192, 'x'));
	EXPECT_EQ(Broadcast.Version(), 2);
}
