#include <engine/shared/config.h>

#include <game/client/QmUi/QmAnimationBackend.h>

#include <gtest/gtest.h>

#include <initializer_list>

namespace
{
	class QmMotionLevelResolver : public ::testing::Test
	{
		int m_SavedMotionLevel = 0;

	protected:
		void SetUp() override
		{
			m_SavedMotionLevel = g_Config.m_QmUiMotionLevel;
			g_Config.m_QmUiMotionLevel = 2;
		}

		void TearDown() override
		{
			g_Config.m_QmUiMotionLevel = m_SavedMotionLevel;
		}

		static SUiAnimTransition MakeTransition(EUiAnimDriver Driver)
		{
			SUiAnimTransition Transition;
			Transition.m_Driver = Driver;
			Transition.m_DurationSec = 1.0f;
			Transition.m_Easing = EEasing::LINEAR;
			return Transition;
		}
	};
}

TEST_F(QmMotionLevelResolver, DisablingMotionSettlesTheSameTargetAndClearsQueuedTracks)
{
	for(const auto Driver : {EUiAnimDriver::TWEEN, EUiAnimDriver::SPRING})
	{
		SCOPED_TRACE(static_cast<int>(Driver));
		g_Config.m_QmUiMotionLevel = 2;
		CQmAnimationBackend Backend;
		const SUiAnimTransition Transition = MakeTransition(Driver);
		Backend.SetValue(1, EUiAnimProperty::POS_X, 0.0f);
		Backend.ResolveTargetValue(1, EUiAnimProperty::POS_X, 10.0f, Transition);
		Backend.Advance(0.1f);
		ASSERT_GT(Backend.GetValue(1, EUiAnimProperty::POS_X), 0.0f);
		ASSERT_LT(Backend.GetValue(1, EUiAnimProperty::POS_X), 10.0f);

		SUiAnimRequest Queued;
		Queued.m_NodeKey = 1;
		Queued.m_Property = EUiAnimProperty::POS_X;
		Queued.m_Target = 20.0f;
		Queued.m_Transition = Transition;
		Queued.m_Transition.m_Interrupt = EUiAnimInterruptPolicy::QUEUE;
		ASSERT_TRUE(Backend.RequestAnimation(Queued));
		ASSERT_EQ(Backend.QueuedTrackCount(), 1);

		g_Config.m_QmUiMotionLevel = 0;
		EXPECT_FLOAT_EQ(Backend.ResolveTargetValue(1, EUiAnimProperty::POS_X, 10.0f, Transition), 10.0f);
		EXPECT_FALSE(Backend.HasActiveAnimation(1, EUiAnimProperty::POS_X));
		EXPECT_EQ(Backend.QueuedTrackCount(), 0);
		Backend.Advance(2.0f);
		EXPECT_FLOAT_EQ(Backend.GetValue(1, EUiAnimProperty::POS_X), 10.0f);
	}
}

TEST_F(QmMotionLevelResolver, RestoringMotionDoesNotReplayTheSettledTarget)
{
	for(const auto Driver : {EUiAnimDriver::TWEEN, EUiAnimDriver::SPRING})
	{
		SCOPED_TRACE(static_cast<int>(Driver));
		g_Config.m_QmUiMotionLevel = 2;
		CQmAnimationBackend Backend;
		const SUiAnimTransition Transition = MakeTransition(Driver);
		Backend.SetValue(1, EUiAnimProperty::POS_X, 0.0f);
		Backend.ResolveTargetValue(1, EUiAnimProperty::POS_X, 10.0f, Transition);
		Backend.Advance(0.1f);
		ASSERT_TRUE(Backend.HasActiveAnimation(1, EUiAnimProperty::POS_X));

		g_Config.m_QmUiMotionLevel = 0;
		Backend.ResolveTargetValue(1, EUiAnimProperty::POS_X, 10.0f, Transition);
		g_Config.m_QmUiMotionLevel = 2;
		EXPECT_FLOAT_EQ(Backend.ResolveTargetValue(1, EUiAnimProperty::POS_X, 10.0f, Transition), 10.0f);
		Backend.Advance(0.1f);
		EXPECT_FALSE(Backend.HasActiveAnimation(1, EUiAnimProperty::POS_X));
		EXPECT_FLOAT_EQ(Backend.GetValue(1, EUiAnimProperty::POS_X), 10.0f);
		SUiAnimCompleteEvent Event;
		EXPECT_FALSE(Backend.PollCompletedEvent(Event));
	}
}

TEST_F(QmMotionLevelResolver, ChangingTheTargetWhileMotionIsOffSynchronizesItsCache)
{
	for(const auto Driver : {EUiAnimDriver::TWEEN, EUiAnimDriver::SPRING})
	{
		SCOPED_TRACE(static_cast<int>(Driver));
		g_Config.m_QmUiMotionLevel = 2;
		CQmAnimationBackend Backend;
		SUiAnimTransition Transition = MakeTransition(Driver);
		Backend.SetValue(1, EUiAnimProperty::POS_X, 0.0f);
		Backend.ResolveTargetValue(1, EUiAnimProperty::POS_X, 10.0f, Transition);
		Backend.Advance(0.1f);

		g_Config.m_QmUiMotionLevel = 0;
		Transition.m_Driver = Driver == EUiAnimDriver::TWEEN ? EUiAnimDriver::SPRING : EUiAnimDriver::TWEEN;
		EXPECT_FLOAT_EQ(Backend.ResolveTargetValue(1, EUiAnimProperty::POS_X, 20.0f, Transition), 20.0f);
		EXPECT_FALSE(Backend.HasActiveAnimation(1, EUiAnimProperty::POS_X));

		g_Config.m_QmUiMotionLevel = 2;
		EXPECT_FLOAT_EQ(Backend.ResolveTargetValue(1, EUiAnimProperty::POS_X, 20.0f, Transition), 20.0f);
		EXPECT_FALSE(Backend.HasActiveAnimation(1, EUiAnimProperty::POS_X));
		SUiAnimCompleteEvent Event;
		EXPECT_FALSE(Backend.PollCompletedEvent(Event));
	}
}

TEST_F(QmMotionLevelResolver, ATransitionIgnoringMotionLevelKeepsAnimatingWhenMotionIsOff)
{
	for(const auto Driver : {EUiAnimDriver::TWEEN, EUiAnimDriver::SPRING})
	{
		SCOPED_TRACE(static_cast<int>(Driver));
		g_Config.m_QmUiMotionLevel = 2;
		CQmAnimationBackend Backend;
		SUiAnimTransition Transition = MakeTransition(Driver);
		Transition.m_RespectMotionLevel = false;
		Backend.SetValue(1, EUiAnimProperty::POS_X, 0.0f);
		Backend.ResolveTargetValue(1, EUiAnimProperty::POS_X, 10.0f, Transition);
		Backend.Advance(0.1f);
		const float Before = Backend.GetValue(1, EUiAnimProperty::POS_X);
		ASSERT_GT(Before, 0.0f);
		ASSERT_LT(Before, 10.0f);

		g_Config.m_QmUiMotionLevel = 0;
		EXPECT_FLOAT_EQ(Backend.ResolveTargetValue(1, EUiAnimProperty::POS_X, 10.0f, Transition), Before);
		EXPECT_TRUE(Backend.HasActiveAnimation(1, EUiAnimProperty::POS_X));
		Backend.Advance(0.1f);
		EXPECT_GT(Backend.GetValue(1, EUiAnimProperty::POS_X), Before);
		EXPECT_LT(Backend.GetValue(1, EUiAnimProperty::POS_X), 10.0f);
	}
}
