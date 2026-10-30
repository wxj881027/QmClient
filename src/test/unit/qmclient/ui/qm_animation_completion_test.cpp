#include <test/support/qm_animation_lifecycle_fixture.h>

using QmAnimationCompletion = CQmAnimationLifecycle;

TEST_F(QmAnimationCompletion, ImmediateMergeCompletesTheInheritedTrackAndAwaitGroup)
{
	m_Runtime.SetValue(NODE, PROPERTY, 0.0f);
	ASSERT_TRUE(m_Runtime.RequestAnimation(Request(10.0f, 71)));
	const uint32_t Id = 71;
	const uint32_t Group = m_Runtime.AwaitTracks(&Id, 1);
	ASSERT_NE(Group, 0u);
	auto Immediate = Request(20.0f, 0);
	Immediate.m_Transition.m_DurationSec = 0.0f;
	ASSERT_TRUE(m_Runtime.RequestAnimation(Immediate));
	EXPECT_FLOAT_EQ(m_Runtime.GetValue(NODE, PROPERTY), 20.0f);
	EXPECT_FALSE(m_Runtime.HasActiveAnimation(NODE, PROPERTY));
	SUiAnimCompleteEvent TrackEvent;
	ASSERT_TRUE(m_Runtime.PollCompletedEvent(TrackEvent));
	EXPECT_EQ(TrackEvent.m_TrackId, Id);
	SUiAnimGroupCompleteEvent GroupEvent;
	ASSERT_TRUE(m_Runtime.PollGroupCompletedEvent(GroupEvent));
	EXPECT_EQ(GroupEvent.m_GroupId, Group);
	EXPECT_FALSE(m_Runtime.PollGroupCompletedEvent(GroupEvent));
}

TEST_F(QmAnimationCompletion, ImmediateMergeLeavesGroupWaitingOnlyForItsOtherTrack)
{
	m_Runtime.SetValue(NODE, PROPERTY, 0.0f);
	m_Runtime.SetValue(NODE, EUiAnimProperty::POS_Y, 0.0f);
	ASSERT_TRUE(m_Runtime.RequestAnimation(Request(10.0f, 71)));
	auto Peer = Request(30.0f, 72);
	Peer.m_Property = EUiAnimProperty::POS_Y;
	ASSERT_TRUE(m_Runtime.RequestAnimation(Peer));
	const uint32_t aIds[] = {71, 72};
	const uint32_t Group = m_Runtime.AwaitTracks(aIds, 2);
	ASSERT_NE(Group, 0u);
	auto Immediate = Request(20.0f, 0);
	Immediate.m_Transition.m_DurationSec = 0.0f;
	ASSERT_TRUE(m_Runtime.RequestAnimation(Immediate));
	SUiAnimGroupCompleteEvent Event;
	EXPECT_FALSE(m_Runtime.PollGroupCompletedEvent(Event));
	m_Runtime.Advance(1.0f);
	ASSERT_TRUE(m_Runtime.PollGroupCompletedEvent(Event));
	EXPECT_EQ(Event.m_GroupId, Group);
	EXPECT_FALSE(m_Runtime.PollGroupCompletedEvent(Event));
}

TEST_F(QmAnimationCompletion, ImmediateMergeCompletesEveryGroupSharingTheTrack)
{
	m_Runtime.SetValue(NODE, PROPERTY, 0.0f);
	ASSERT_TRUE(m_Runtime.RequestAnimation(Request(10.0f, 71)));
	const uint32_t Id = 71;
	const uint32_t First = m_Runtime.AwaitTracks(&Id, 1);
	const uint32_t Second = m_Runtime.AwaitTracks(&Id, 1);
	ASSERT_NE(First, 0u);
	ASSERT_NE(Second, 0u);
	auto Immediate = Request(20.0f, 0);
	Immediate.m_Transition.m_DurationSec = 0.0f;
	ASSERT_TRUE(m_Runtime.RequestAnimation(Immediate));
	SUiAnimGroupCompleteEvent Event;
	ASSERT_TRUE(m_Runtime.PollGroupCompletedEvent(Event));
	const uint32_t FirstEvent = Event.m_GroupId;
	ASSERT_TRUE(m_Runtime.PollGroupCompletedEvent(Event));
	EXPECT_TRUE((FirstEvent == First && Event.m_GroupId == Second) || (FirstEvent == Second && Event.m_GroupId == First));
	EXPECT_FALSE(m_Runtime.PollGroupCompletedEvent(Event));
}

TEST_F(QmAnimationCompletion, ImmediateReplacementWithAnotherIdStillCancelsTheOldGroup)
{
	m_Runtime.SetValue(NODE, PROPERTY, 0.0f);
	ASSERT_TRUE(m_Runtime.RequestAnimation(Request(10.0f, 71)));
	const uint32_t Id = 71;
	ASSERT_NE(m_Runtime.AwaitTracks(&Id, 1), 0u);
	auto Immediate = Request(20.0f, 72, EUiAnimInterruptPolicy::REPLACE);
	Immediate.m_Transition.m_DurationSec = 0.0f;
	ASSERT_TRUE(m_Runtime.RequestAnimation(Immediate));
	SUiAnimGroupCompleteEvent GroupEvent;
	EXPECT_FALSE(m_Runtime.PollGroupCompletedEvent(GroupEvent));
	SUiAnimCompleteEvent TrackEvent;
	ASSERT_TRUE(m_Runtime.PollCompletedEvent(TrackEvent));
	EXPECT_EQ(TrackEvent.m_TrackId, 72u);
}
