#include "qm_anim_test_helpers.h"

#include <gtest/gtest.h>

TEST(UiV2Anim, QueuePolicyRunsInOrder)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(7, EUiAnimProperty::ALPHA, 0.0f);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(7, EUiAnimProperty::ALPHA, 10.0f, 0.2f, 1, EUiAnimInterruptPolicy::QUEUE, 21)));
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(7, EUiAnimProperty::ALPHA, 20.0f, 0.2f, 1, EUiAnimInterruptPolicy::QUEUE, 22)));
	EXPECT_EQ(Runtime.ActiveTrackCount(), 1);
	EXPECT_EQ(Runtime.QueuedTrackCount(), 1);
	AdvanceQmAnimFor(Runtime, 0.25f);
	SUiAnimCompleteEvent Event;
	ASSERT_TRUE(Runtime.PollCompletedEvent(Event));
	EXPECT_EQ(Event.m_TrackId, 21u);
	EXPECT_TRUE(Runtime.HasActiveAnimation(7, EUiAnimProperty::ALPHA));
	AdvanceQmAnimFor(Runtime, 0.25f);
	ASSERT_TRUE(Runtime.PollCompletedEvent(Event));
	EXPECT_EQ(Event.m_TrackId, 22u);
	EXPECT_FALSE(Runtime.HasActiveAnimation(7, EUiAnimProperty::ALPHA));
	EXPECT_NEAR(Runtime.GetValue(7, EUiAnimProperty::ALPHA), 20.0f, 0.001f);
}

TEST(UiV2Anim, DelayDefersAnimationStart)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(8, EUiAnimProperty::POS_Y, 0.0f);
	SUiAnimRequest Request = MakeQmAnimRequest(8, EUiAnimProperty::POS_Y, 8.0f, 0.2f, 1, EUiAnimInterruptPolicy::REPLACE, 51);
	Request.m_Transition.m_DelaySec = 0.2f;
	EXPECT_TRUE(Runtime.RequestAnimation(Request));
	AdvanceQmAnimFor(Runtime, 0.1f);
	EXPECT_NEAR(Runtime.GetValue(8, EUiAnimProperty::POS_Y), 0.0f, 0.0001f);
	AdvanceQmAnimFor(Runtime, 0.15f);
	EXPECT_GT(Runtime.GetValue(8, EUiAnimProperty::POS_Y), 0.0f);
	AdvanceQmAnimFor(Runtime, 0.2f);
	EXPECT_NEAR(Runtime.GetValue(8, EUiAnimProperty::POS_Y), 8.0f, 0.001f);
}

TEST(UiV2Anim, ZeroDeltaTimeDoesNotAdvance)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(9, EUiAnimProperty::POS_X, 0.0f);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(9, EUiAnimProperty::POS_X, 9.0f, 0.5f, 1, EUiAnimInterruptPolicy::REPLACE, 61)));
	const float Before = Runtime.GetValue(9, EUiAnimProperty::POS_X);
	Runtime.Advance(0.0f);
	EXPECT_NEAR(Before, Runtime.GetValue(9, EUiAnimProperty::POS_X), 0.0001f);
	EXPECT_TRUE(Runtime.HasActiveAnimation(9, EUiAnimProperty::POS_X));
}

TEST(UiV2Anim, ZeroDurationCompletesImmediately)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(10, EUiAnimProperty::ALPHA, 0.0f);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(10, EUiAnimProperty::ALPHA, 1.0f, 0.0f, 1, EUiAnimInterruptPolicy::REPLACE, 71)));
	EXPECT_NEAR(Runtime.GetValue(10, EUiAnimProperty::ALPHA), 1.0f, 0.0001f);
	EXPECT_FALSE(Runtime.HasActiveAnimation(10, EUiAnimProperty::ALPHA));
	SUiAnimCompleteEvent Event;
	ASSERT_TRUE(Runtime.PollCompletedEvent(Event));
	EXPECT_EQ(Event.m_TrackId, 71u);
	EXPECT_EQ(Event.m_NodeKey, 10u);
	EXPECT_EQ(Event.m_Property, EUiAnimProperty::ALPHA);
	EXPECT_FALSE(Runtime.PollCompletedEvent(Event));
}

TEST(UiV2Anim, AwaitTracksIgnoresZeroAndDuplicateTrackIds)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(12, EUiAnimProperty::ALPHA, 0.0f);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(12, EUiAnimProperty::ALPHA, 1.0f, 0.1f, 1, EUiAnimInterruptPolicy::REPLACE, 93)));
	const uint32_t aTrackIds[] = {0, 93, 93};
	const uint32_t GroupId = Runtime.AwaitTracks(aTrackIds, 3);
	EXPECT_NE(GroupId, 0u);
	AdvanceQmAnimFor(Runtime, 0.15f);
	SUiAnimCompleteEvent TrackEvent;
	ASSERT_TRUE(Runtime.PollCompletedEvent(TrackEvent));
	EXPECT_EQ(TrackEvent.m_TrackId, 93u);
	SUiAnimGroupCompleteEvent GroupEvent;
	ASSERT_TRUE(Runtime.PollGroupCompletedEvent(GroupEvent));
	EXPECT_EQ(GroupEvent.m_GroupId, GroupId);
	EXPECT_FALSE(Runtime.PollGroupCompletedEvent(GroupEvent));
}

TEST(UiV2Anim, AwaitTracksCompletesAfterAllTrackedIds)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(11, EUiAnimProperty::POS_X, 0.0f);
	Runtime.SetValue(11, EUiAnimProperty::POS_Y, 0.0f);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(11, EUiAnimProperty::POS_X, 10.0f, 0.1f, 1, EUiAnimInterruptPolicy::REPLACE, 91)));
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(11, EUiAnimProperty::POS_Y, 20.0f, 0.3f, 1, EUiAnimInterruptPolicy::REPLACE, 92)));
	const uint32_t aTrackIds[] = {91, 92};
	const uint32_t GroupId = Runtime.AwaitTracks(aTrackIds, 2);
	EXPECT_NE(GroupId, 0u);
	AdvanceQmAnimFor(Runtime, 0.15f);
	SUiAnimCompleteEvent TrackEvent;
	ASSERT_TRUE(Runtime.PollCompletedEvent(TrackEvent));
	EXPECT_EQ(TrackEvent.m_TrackId, 91u);
	SUiAnimGroupCompleteEvent GroupEvent;
	EXPECT_FALSE(Runtime.PollGroupCompletedEvent(GroupEvent));
	AdvanceQmAnimFor(Runtime, 0.25f);
	ASSERT_TRUE(Runtime.PollCompletedEvent(TrackEvent));
	EXPECT_EQ(TrackEvent.m_TrackId, 92u);
	ASSERT_TRUE(Runtime.PollGroupCompletedEvent(GroupEvent));
	EXPECT_EQ(GroupEvent.m_GroupId, GroupId);
	EXPECT_FALSE(Runtime.PollGroupCompletedEvent(GroupEvent));
}

TEST(UiV2Anim, ReplacedAwaitedTrackDoesNotCompleteGroup)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(13, EUiAnimProperty::POS_X, 0.0f);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(13, EUiAnimProperty::POS_X, 10.0f, 0.5f, 1, EUiAnimInterruptPolicy::REPLACE, 94)));
	const uint32_t aTrackIds[] = {94};
	EXPECT_NE(Runtime.AwaitTracks(aTrackIds, 1), 0u);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(13, EUiAnimProperty::POS_X, 20.0f, 0.1f, 1, EUiAnimInterruptPolicy::REPLACE, 95)));
	AdvanceQmAnimFor(Runtime, 0.2f);
	SUiAnimCompleteEvent TrackEvent;
	ASSERT_TRUE(Runtime.PollCompletedEvent(TrackEvent));
	EXPECT_EQ(TrackEvent.m_TrackId, 95u);
	SUiAnimGroupCompleteEvent GroupEvent;
	EXPECT_FALSE(Runtime.PollGroupCompletedEvent(GroupEvent));
}

TEST(UiV2Anim, SetValueCancelsAwaitedActiveAndQueuedTracks)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(18, EUiAnimProperty::POS_X, 0.0f);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(18, EUiAnimProperty::POS_X, 10.0f, 0.5f, 1, EUiAnimInterruptPolicy::REPLACE, 102)));
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(18, EUiAnimProperty::POS_X, 20.0f, 0.5f, 1, EUiAnimInterruptPolicy::QUEUE, 103)));
	const uint32_t aCancelledTrackIds[] = {102, 103};
	EXPECT_NE(Runtime.AwaitTracks(aCancelledTrackIds, 2), 0u);
	Runtime.SetValue(18, EUiAnimProperty::POS_X, 5.0f);
	EXPECT_FALSE(Runtime.HasActiveAnimation(18, EUiAnimProperty::POS_X));
	EXPECT_EQ(Runtime.QueuedTrackCount(), 0);
	Runtime.SetValue(19, EUiAnimProperty::POS_X, 0.0f);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(19, EUiAnimProperty::POS_X, 1.0f, 0.0f, 1, EUiAnimInterruptPolicy::REPLACE, 102)));
	Runtime.SetValue(20, EUiAnimProperty::POS_X, 0.0f);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(20, EUiAnimProperty::POS_X, 1.0f, 0.0f, 1, EUiAnimInterruptPolicy::REPLACE, 103)));
	SUiAnimGroupCompleteEvent GroupEvent;
	EXPECT_FALSE(Runtime.PollGroupCompletedEvent(GroupEvent));
}

TEST(UiV2Anim, ReplacePolicyCancelsAwaitedQueuedTracks)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(21, EUiAnimProperty::POS_X, 0.0f);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(21, EUiAnimProperty::POS_X, 10.0f, 0.5f, 1, EUiAnimInterruptPolicy::REPLACE, 104)));
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(21, EUiAnimProperty::POS_X, 20.0f, 0.5f, 1, EUiAnimInterruptPolicy::QUEUE, 105)));
	const uint32_t aQueuedTrackIds[] = {105};
	EXPECT_NE(Runtime.AwaitTracks(aQueuedTrackIds, 1), 0u);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(21, EUiAnimProperty::POS_X, 30.0f, 0.1f, 2, EUiAnimInterruptPolicy::REPLACE, 106)));
	EXPECT_EQ(Runtime.QueuedTrackCount(), 0);
	AdvanceQmAnimFor(Runtime, 0.2f);
	Runtime.SetValue(22, EUiAnimProperty::POS_X, 0.0f);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(22, EUiAnimProperty::POS_X, 1.0f, 0.0f, 1, EUiAnimInterruptPolicy::REPLACE, 105)));
	SUiAnimGroupCompleteEvent GroupEvent;
	EXPECT_FALSE(Runtime.PollGroupCompletedEvent(GroupEvent));
}

TEST(UiV2Anim, CancellingOneAwaitedTrackCancelsWholeGroup)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(23, EUiAnimProperty::POS_X, 0.0f);
	Runtime.SetValue(23, EUiAnimProperty::POS_Y, 0.0f);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(23, EUiAnimProperty::POS_X, 10.0f, 0.5f, 1, EUiAnimInterruptPolicy::REPLACE, 107)));
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(23, EUiAnimProperty::POS_Y, 20.0f, 0.2f, 1, EUiAnimInterruptPolicy::REPLACE, 108)));
	const uint32_t aTrackIds[] = {107, 108};
	EXPECT_NE(Runtime.AwaitTracks(aTrackIds, 2), 0u);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(23, EUiAnimProperty::POS_X, 30.0f, 0.1f, 2, EUiAnimInterruptPolicy::REPLACE, 109)));
	AdvanceQmAnimFor(Runtime, 0.3f);
	SUiAnimGroupCompleteEvent GroupEvent;
	EXPECT_FALSE(Runtime.PollGroupCompletedEvent(GroupEvent));
}

TEST(UiV2Anim, AwaitTracksHandlesQueuedImmediateCompletion)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(14, EUiAnimProperty::ALPHA, 0.0f);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(14, EUiAnimProperty::ALPHA, 0.5f, 0.1f, 1, EUiAnimInterruptPolicy::QUEUE, 96)));
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(14, EUiAnimProperty::ALPHA, 1.0f, 0.0f, 1, EUiAnimInterruptPolicy::QUEUE, 97)));
	const uint32_t aTrackIds[] = {97};
	const uint32_t GroupId = Runtime.AwaitTracks(aTrackIds, 1);
	EXPECT_NE(GroupId, 0u);
	AdvanceQmAnimFor(Runtime, 0.15f);
	SUiAnimCompleteEvent TrackEvent;
	ASSERT_TRUE(Runtime.PollCompletedEvent(TrackEvent));
	EXPECT_EQ(TrackEvent.m_TrackId, 96u);
	ASSERT_TRUE(Runtime.PollCompletedEvent(TrackEvent));
	EXPECT_EQ(TrackEvent.m_TrackId, 97u);
	SUiAnimGroupCompleteEvent GroupEvent;
	ASSERT_TRUE(Runtime.PollGroupCompletedEvent(GroupEvent));
	EXPECT_EQ(GroupEvent.m_GroupId, GroupId);
}

TEST(UiV2Anim, AwaitTracksSupportsMultipleGroupsForSameTrack)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(15, EUiAnimProperty::ALPHA, 0.0f);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(15, EUiAnimProperty::ALPHA, 1.0f, 0.1f, 1, EUiAnimInterruptPolicy::REPLACE, 98)));
	const uint32_t aTrackIds[] = {98};
	const uint32_t GroupA = Runtime.AwaitTracks(aTrackIds, 1);
	const uint32_t GroupB = Runtime.AwaitTracks(aTrackIds, 1);
	EXPECT_NE(GroupA, 0u);
	EXPECT_NE(GroupB, 0u);
	EXPECT_NE(GroupA, GroupB);
	AdvanceQmAnimFor(Runtime, 0.15f);
	SUiAnimCompleteEvent TrackEvent;
	ASSERT_TRUE(Runtime.PollCompletedEvent(TrackEvent));
	EXPECT_EQ(TrackEvent.m_TrackId, 98u);
	SUiAnimGroupCompleteEvent GroupEvent;
	ASSERT_TRUE(Runtime.PollGroupCompletedEvent(GroupEvent));
	EXPECT_EQ(GroupEvent.m_GroupId, GroupA);
	ASSERT_TRUE(Runtime.PollGroupCompletedEvent(GroupEvent));
	EXPECT_EQ(GroupEvent.m_GroupId, GroupB);
}

TEST(UiV2Anim, AwaitTracksRejectsAlreadyCompletedOrUnknownTrackIds)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(17, EUiAnimProperty::ALPHA, 0.0f);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeQmAnimRequest(17, EUiAnimProperty::ALPHA, 1.0f, 0.0f, 1, EUiAnimInterruptPolicy::REPLACE, 101)));
	SUiAnimCompleteEvent TrackEvent;
	ASSERT_TRUE(Runtime.PollCompletedEvent(TrackEvent));
	EXPECT_EQ(TrackEvent.m_TrackId, 101u);
	const uint32_t aTrackIds[] = {101, 99999};
	EXPECT_EQ(Runtime.AwaitTracks(aTrackIds, 2), 0u);
}
