#include <test/support/qm_animation_lifecycle_fixture.h>

using QmAnimationMotionDisable = CQmAnimationLifecycle;

TEST_F(QmAnimationMotionDisable, SameTweenTargetSettlesWhenMotionIsDisabledAndDoesNotReplay)
{
	m_Runtime.SetValue(NODE, PROPERTY, 0.0f);
	const auto Transition = Request(10.0f, 0).m_Transition;
	m_Runtime.ResolveTargetValue(NODE, PROPERTY, 10.0f, Transition);
	m_Runtime.Advance(0.1f);
	ASSERT_TRUE(m_Runtime.HasActiveAnimation(NODE, PROPERTY));
	g_Config.m_QmUiMotionLevel = 0;
	EXPECT_FLOAT_EQ(m_Runtime.ResolveTargetValue(NODE, PROPERTY, 10.0f, Transition), 10.0f);
	EXPECT_FALSE(m_Runtime.HasActiveAnimation(NODE, PROPERTY));
	g_Config.m_QmUiMotionLevel = 2;
	EXPECT_FLOAT_EQ(m_Runtime.ResolveTargetValue(NODE, PROPERTY, 10.0f, Transition), 10.0f);
	EXPECT_FALSE(m_Runtime.HasActiveAnimation(NODE, PROPERTY));
}

TEST_F(QmAnimationMotionDisable, SamePresentationTargetSettlesWhenMotionIsDisabled)
{
	m_Runtime.SetValue(NODE, PROPERTY, 0.0f);
	ResolveUiPresentationStateValue(m_Runtime, NODE, PROPERTY, 10.0f, ui_spring::SNAPPY);
	m_Runtime.Advance(0.02f);
	ASSERT_TRUE(m_Runtime.HasActiveAnimation(NODE, PROPERTY));
	g_Config.m_QmUiMotionLevel = 0;
	EXPECT_FLOAT_EQ(ResolveUiPresentationStateValue(m_Runtime, NODE, PROPERTY, 10.0f, ui_spring::SNAPPY), 10.0f);
	EXPECT_FALSE(m_Runtime.HasActiveAnimation(NODE, PROPERTY));
	g_Config.m_QmUiMotionLevel = 2;
	ResolveUiPresentationStateValue(m_Runtime, NODE, PROPERTY, 10.0f, ui_spring::SNAPPY);
	EXPECT_FALSE(m_Runtime.HasActiveAnimation(NODE, PROPERTY));
}

TEST_F(QmAnimationMotionDisable, AdvanceSettlesDelayedSpringWithoutAnotherResolverCall)
{
	m_Runtime.SetValue(NODE, PROPERTY, 0.0f);
	auto Delayed = Request(10.0f, 71);
	Delayed.m_Transition.m_Driver = EUiAnimDriver::SPRING;
	Delayed.m_Transition.m_Spring = ui_spring::SNAPPY;
	Delayed.m_Transition.m_DelaySec = 1.0f;
	ASSERT_TRUE(m_Runtime.RequestAnimation(Delayed));
	g_Config.m_QmUiMotionLevel = 0;
	m_Runtime.Advance(0.001f);
	EXPECT_FLOAT_EQ(m_Runtime.GetValue(NODE, PROPERTY), 10.0f);
	EXPECT_FALSE(m_Runtime.HasActiveAnimation(NODE, PROPERTY));
	SUiAnimCompleteEvent Event;
	ASSERT_TRUE(m_Runtime.PollCompletedEvent(Event));
	EXPECT_EQ(Event.m_TrackId, 71u);
}

TEST_F(QmAnimationMotionDisable, DisabledQueueCompletesInOrderAndReleasesItsAwaitGroup)
{
	m_Runtime.SetValue(NODE, PROPERTY, 0.0f);
	ASSERT_TRUE(m_Runtime.RequestAnimation(Request(10.0f, 71)));
	ASSERT_TRUE(m_Runtime.RequestAnimation(Request(20.0f, 72, EUiAnimInterruptPolicy::QUEUE)));
	const uint32_t aIds[] = {71, 72};
	const uint32_t Group = m_Runtime.AwaitTracks(aIds, 2);
	ASSERT_NE(Group, 0u);
	g_Config.m_QmUiMotionLevel = 0;
	m_Runtime.Advance(0.01f);
	EXPECT_EQ(m_Runtime.ActiveTrackCount(), 0);
	EXPECT_EQ(m_Runtime.QueuedTrackCount(), 0);
	EXPECT_FLOAT_EQ(m_Runtime.GetValue(NODE, PROPERTY), 20.0f);
	SUiAnimCompleteEvent TrackEvent;
	ASSERT_TRUE(m_Runtime.PollCompletedEvent(TrackEvent));
	EXPECT_EQ(TrackEvent.m_TrackId, 71u);
	ASSERT_TRUE(m_Runtime.PollCompletedEvent(TrackEvent));
	EXPECT_EQ(TrackEvent.m_TrackId, 72u);
	SUiAnimGroupCompleteEvent GroupEvent;
	ASSERT_TRUE(m_Runtime.PollGroupCompletedEvent(GroupEvent));
	EXPECT_EQ(GroupEvent.m_GroupId, Group);
	EXPECT_FALSE(m_Runtime.PollGroupCompletedEvent(GroupEvent));
}

TEST_F(QmAnimationMotionDisable, TrackOptingOutOfMotionLevelContinuesToAdvance)
{
	m_Runtime.SetValue(NODE, PROPERTY, 0.0f);
	auto Independent = Request(10.0f, 71);
	Independent.m_Transition.m_RespectMotionLevel = false;
	ASSERT_TRUE(m_Runtime.RequestAnimation(Independent));
	g_Config.m_QmUiMotionLevel = 0;
	m_Runtime.Advance(0.1f);
	EXPECT_NEAR(m_Runtime.GetValue(NODE, PROPERTY), 1.0f, 0.0001f);
	EXPECT_TRUE(m_Runtime.HasActiveAnimation(NODE, PROPERTY));
}

TEST_F(QmAnimationMotionDisable, DisabledQueuePreservesTheTimeOfAnOptedOutMiddleTrack)
{
	m_Runtime.SetValue(NODE, PROPERTY, 0.0f);
	ASSERT_TRUE(m_Runtime.RequestAnimation(Request(10.0f, 71)));
	auto Independent = Request(20.0f, 72, EUiAnimInterruptPolicy::QUEUE);
	Independent.m_Transition.m_RespectMotionLevel = false;
	ASSERT_TRUE(m_Runtime.RequestAnimation(Independent));
	ASSERT_TRUE(m_Runtime.RequestAnimation(Request(30.0f, 73, EUiAnimInterruptPolicy::QUEUE)));
	g_Config.m_QmUiMotionLevel = 0;
	m_Runtime.Advance(0.1f);
	EXPECT_NEAR(m_Runtime.GetValue(NODE, PROPERTY), 11.0f, 0.0001f);
	EXPECT_EQ(m_Runtime.QueuedTrackCount(), 1);
	m_Runtime.Advance(1.0f);
	EXPECT_FLOAT_EQ(m_Runtime.GetValue(NODE, PROPERTY), 30.0f);
	EXPECT_EQ(m_Runtime.ActiveTrackCount(), 0);
	EXPECT_EQ(m_Runtime.QueuedTrackCount(), 0);
}

TEST_F(QmAnimationMotionDisable, ZeroDeltaStillDoesNotAdvanceOrCompleteTracks)
{
	m_Runtime.SetValue(NODE, PROPERTY, 0.0f);
	ASSERT_TRUE(m_Runtime.RequestAnimation(Request(10.0f, 71)));
	g_Config.m_QmUiMotionLevel = 0;
	m_Runtime.Advance(0.0f);
	EXPECT_FLOAT_EQ(m_Runtime.GetValue(NODE, PROPERTY), 0.0f);
	EXPECT_TRUE(m_Runtime.HasActiveAnimation(NODE, PROPERTY));
}

TEST_F(QmAnimationMotionDisable, DisablingThroughResolverCompletesAnExistingHigherPriorityOwner)
{
	m_Runtime.SetValue(NODE, PROPERTY, 0.0f);
	auto Owner = Request(10.0f, 71);
	Owner.m_Transition.m_Priority = 5;
	ASSERT_TRUE(m_Runtime.RequestAnimation(Owner));
	const uint32_t Id = 71;
	const uint32_t Group = m_Runtime.AwaitTracks(&Id, 1);
	ASSERT_NE(Group, 0u);
	g_Config.m_QmUiMotionLevel = 0;
	const auto Transition = Request(10.0f, 0).m_Transition;
	EXPECT_FLOAT_EQ(m_Runtime.ResolveTargetValue(NODE, PROPERTY, 10.0f, Transition), 10.0f);
	EXPECT_FALSE(m_Runtime.HasActiveAnimation(NODE, PROPERTY));
	SUiAnimGroupCompleteEvent Event;
	ASSERT_TRUE(m_Runtime.PollGroupCompletedEvent(Event));
	EXPECT_EQ(Event.m_GroupId, Group);
}

TEST_F(QmAnimationMotionDisable, ResolverRetainsAnActiveTrackThatIgnoresMotionLevel)
{
	m_Runtime.SetValue(NODE, PROPERTY, 0.0f);
	auto Transition = Request(10.0f, 0).m_Transition;
	Transition.m_RespectMotionLevel = false;
	m_Runtime.ResolveTargetValue(NODE, PROPERTY, 10.0f, Transition);
	m_Runtime.Advance(0.1f);
	g_Config.m_QmUiMotionLevel = 0;
	EXPECT_NEAR(m_Runtime.ResolveTargetValue(NODE, PROPERTY, 10.0f, Transition), 1.0f, 0.0001f);
	EXPECT_TRUE(m_Runtime.HasActiveAnimation(NODE, PROPERTY));
}
