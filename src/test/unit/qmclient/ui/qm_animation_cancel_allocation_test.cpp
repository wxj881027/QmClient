#include <game/client/QmUi/QmAnimationBackend.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <new>

namespace
{
	SUiAnimRequest Request(uint64_t Node, uint32_t Track, EUiAnimInterruptPolicy Policy = EUiAnimInterruptPolicy::REPLACE)
	{
		SUiAnimRequest Result;
		Result.m_NodeKey = Node;
		Result.m_Property = EUiAnimProperty::POS_X;
		Result.m_TrackId = Track;
		Result.m_Target = 1.0f;
		Result.m_Transition.m_DurationSec = 1.0f;
		Result.m_Transition.m_Interrupt = Policy;
		Result.m_Transition.m_RespectMotionLevel = false;
		return Result;
	}

	struct SCancelResult
	{
		bool m_AllocationAttempted;
		bool m_Threw;
	};

	SCancelResult Cancel(CQmAnimationBackend &Runtime, uint64_t Node, float Value)
	{
		CPredictionAllocationFailure Failure(0);
		bool Threw = false;
		try
		{
			Runtime.SetValue(Node, EUiAnimProperty::POS_X, Value);
		}
		catch(...)
		{
			Threw = true;
		}
		return {Failure.Triggered(), Threw};
	}
}

TEST(QmAnimationCancelAllocation, ExistingValueCancelsAwaitedTrackWithoutAllocatingOrCompleting)
{
	CQmAnimationBackend Runtime;
	Runtime.SetValue(1, EUiAnimProperty::POS_X, 0.0f);
	ASSERT_TRUE(Runtime.RequestAnimation(Request(1, 11)));
	const uint32_t Id = 11;
	ASSERT_NE(Runtime.AwaitTracks(&Id, 1), 0u);
	const auto Result = Cancel(Runtime, 1, 0.25f);
	ASSERT_FALSE(Result.m_Threw);
	EXPECT_FALSE(Result.m_AllocationAttempted);
	EXPECT_FLOAT_EQ(Runtime.GetValue(1, EUiAnimProperty::POS_X), 0.25f);
	EXPECT_FALSE(Runtime.HasActiveAnimation(1, EUiAnimProperty::POS_X));
	SUiAnimCompleteEvent TrackEvent;
	SUiAnimGroupCompleteEvent GroupEvent;
	EXPECT_FALSE(Runtime.PollCompletedEvent(TrackEvent));
	EXPECT_FALSE(Runtime.PollGroupCompletedEvent(GroupEvent));
}

TEST(QmAnimationCancelAllocation, SharedTrackCancelsAllItsGroupsAndPreservesIndependentGroup)
{
	CQmAnimationBackend Runtime;
	for(uint64_t Node : {1u, 2u, 3u})
	{
		Runtime.SetValue(Node, EUiAnimProperty::POS_X, 0.0f);
		ASSERT_TRUE(Runtime.RequestAnimation(Request(Node, static_cast<uint32_t>(Node * 10 + 1))));
	}
	const uint32_t Single[] = {11};
	const uint32_t PairA[] = {11, 21};
	const uint32_t PairB[] = {11, 31};
	const uint32_t Independent = 21;
	ASSERT_NE(Runtime.AwaitTracks(Single, 1), 0u);
	ASSERT_NE(Runtime.AwaitTracks(PairA, 2), 0u);
	ASSERT_NE(Runtime.AwaitTracks(PairB, 2), 0u);
	const uint32_t Kept = Runtime.AwaitTracks(&Independent, 1);
	ASSERT_NE(Kept, 0u);
	const auto Result = Cancel(Runtime, 1, 0.5f);
	ASSERT_FALSE(Result.m_Threw);
	EXPECT_FALSE(Result.m_AllocationAttempted);
	EXPECT_TRUE(Runtime.HasActiveAnimation(2, EUiAnimProperty::POS_X));
	EXPECT_TRUE(Runtime.HasActiveAnimation(3, EUiAnimProperty::POS_X));
	Runtime.Advance(2.0f);
	SUiAnimGroupCompleteEvent Event;
	ASSERT_TRUE(Runtime.PollGroupCompletedEvent(Event));
	EXPECT_EQ(Event.m_GroupId, Kept);
	EXPECT_FALSE(Runtime.PollGroupCompletedEvent(Event));
}

TEST(QmAnimationCancelAllocation, ExistingValueCancelsActiveAndQueuedMembershipWithoutAllocating)
{
	CQmAnimationBackend Runtime;
	Runtime.SetValue(1, EUiAnimProperty::POS_X, 0.0f);
	ASSERT_TRUE(Runtime.RequestAnimation(Request(1, 11)));
	ASSERT_TRUE(Runtime.RequestAnimation(Request(1, 12, EUiAnimInterruptPolicy::QUEUE)));
	const uint32_t Both[] = {11, 12};
	const uint32_t Queued = 12;
	ASSERT_NE(Runtime.AwaitTracks(Both, 2), 0u);
	ASSERT_NE(Runtime.AwaitTracks(&Queued, 1), 0u);
	const auto Result = Cancel(Runtime, 1, 0.5f);
	ASSERT_FALSE(Result.m_Threw);
	EXPECT_FALSE(Result.m_AllocationAttempted);
	EXPECT_EQ(Runtime.ActiveTrackCount(), 0);
	EXPECT_EQ(Runtime.QueuedTrackCount(), 0);
	Runtime.Advance(2.0f);
	SUiAnimGroupCompleteEvent Event;
	EXPECT_FALSE(Runtime.PollGroupCompletedEvent(Event));
}

TEST(QmAnimationCancelAllocation, RepeatedCancelAllowsReusingTrackIdWithoutRevivingOldGroup)
{
	CQmAnimationBackend Runtime;
	Runtime.SetValue(1, EUiAnimProperty::POS_X, 0.0f);
	Runtime.SetValue(99, EUiAnimProperty::ALPHA, 0.6f);
	ASSERT_TRUE(Runtime.RequestAnimation(Request(1, 11)));
	const uint32_t Id = 11;
	const uint32_t Old = Runtime.AwaitTracks(&Id, 1);
	ASSERT_NE(Old, 0u);
	for(float Value : {0.25f, 0.5f})
	{
		const auto Result = Cancel(Runtime, 1, Value);
		ASSERT_FALSE(Result.m_Threw);
		EXPECT_FALSE(Result.m_AllocationAttempted);
	}
	EXPECT_FLOAT_EQ(Runtime.GetValue(99, EUiAnimProperty::ALPHA), 0.6f);
	ASSERT_TRUE(Runtime.RequestAnimation(Request(1, Id)));
	const uint32_t New = Runtime.AwaitTracks(&Id, 1);
	ASSERT_NE(New, 0u);
	ASSERT_NE(New, Old);
	Runtime.Advance(2.0f);
	SUiAnimGroupCompleteEvent Event;
	ASSERT_TRUE(Runtime.PollGroupCompletedEvent(Event));
	EXPECT_EQ(Event.m_GroupId, New);
	EXPECT_FALSE(Runtime.PollGroupCompletedEvent(Event));
}

TEST(QmAnimationCancelAllocation, PartiallyPreparedAwaitMembershipCanBeCancelledWithoutAllocating)
{
	bool ReachedSuccess = false;
	for(size_t BeforeFailure = 0; BeforeFailure < 64; ++BeforeFailure)
	{
		SCOPED_TRACE(BeforeFailure);
		CQmAnimationBackend Runtime;
		Runtime.SetValue(1, EUiAnimProperty::POS_X, 0.0f);
		Runtime.SetValue(2, EUiAnimProperty::POS_X, 0.0f);
		ASSERT_TRUE(Runtime.RequestAnimation(Request(1, 11)));
		ASSERT_TRUE(Runtime.RequestAnimation(Request(2, 21)));
		const uint32_t Ids[] = {11, 21};
		uint32_t Group = 0;
		bool Triggered = false;
		{
			CPredictionAllocationFailure Failure(BeforeFailure);
			try
			{
				Group = Runtime.AwaitTracks(Ids, 2);
			}
			catch(const std::bad_alloc &)
			{
			}
			Triggered = Failure.Triggered();
		}
		for(uint64_t Node : {1u, 2u})
		{
			const auto Result = Cancel(Runtime, Node, 0.5f);
			ASSERT_FALSE(Result.m_Threw);
			EXPECT_FALSE(Result.m_AllocationAttempted);
		}
		EXPECT_EQ(Runtime.ActiveTrackCount(), 0);
		SUiAnimGroupCompleteEvent Event;
		EXPECT_FALSE(Runtime.PollGroupCompletedEvent(Event));
		if(!Triggered)
		{
			EXPECT_NE(Group, 0u);
			ReachedSuccess = true;
			break;
		}
	}
	EXPECT_TRUE(ReachedSuccess);
}
