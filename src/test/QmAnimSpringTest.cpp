// 动画弹簧驱动的运行时行为测试。
#include "qm_anim_test_helpers.h"
#include "test.h"

#include <engine/shared/config.h>

#include <game/client/QmUi/QmAnim.h>
#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmTree.h>
#include <game/client/QmUi/UiTokens.h>

#include <gtest/gtest.h>

#include <cmath>

namespace
{
	void AdvanceFor(CUiV2AnimationRuntime &Runtime, float Seconds)
	{
		AdvanceQmAnimFor(Runtime, Seconds);
	}

	SUiAnimRequest MakeSpringRequest(uint64_t NodeKey, EUiAnimProperty Property, float Target, uint32_t TrackId)
	{
		g_Config.m_QmUiMotionLevel = 2;
		SUiAnimRequest Request;
		Request.m_NodeKey = NodeKey;
		Request.m_Property = Property;
		Request.m_Target = Target;
		Request.m_Transition.m_Driver = EUiAnimDriver::SPRING;
		Request.m_Transition.m_Interrupt = EUiAnimInterruptPolicy::REPLACE;
		Request.m_TrackId = TrackId;
		return Request;
	}
} // namespace

TEST(UiV2AnimSpring, ConvergesToTarget)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(101, EUiAnimProperty::ALPHA, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeSpringRequest(101, EUiAnimProperty::ALPHA, 1.0f, 81)));
	AdvanceFor(Runtime, 2.0f);
	EXPECT_NEAR(Runtime.GetValue(101, EUiAnimProperty::ALPHA), 1.0f, 0.02f);
	EXPECT_FALSE(Runtime.HasActiveAnimation(101, EUiAnimProperty::ALPHA));
}

TEST(UiV2AnimSpring, AutoCompletesAndEmitsEvent)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(102, EUiAnimProperty::ALPHA, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeSpringRequest(102, EUiAnimProperty::ALPHA, 1.0f, 82)));
	AdvanceFor(Runtime, 2.0f);

	SUiAnimCompleteEvent Event;
	ASSERT_TRUE(Runtime.PollCompletedEvent(Event));
	EXPECT_EQ(Event.m_TrackId, 82u);
	EXPECT_EQ(Event.m_NodeKey, 102u);
	EXPECT_EQ(Event.m_Property, EUiAnimProperty::ALPHA);
	EXPECT_EQ(Runtime.ActiveTrackCount(), 0);
}

TEST(UiV2AnimSpring, ZeroDtSpringStaysPut)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(103, EUiAnimProperty::ALPHA, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeSpringRequest(103, EUiAnimProperty::ALPHA, 1.0f, 83)));
	const float Before = Runtime.GetValue(103, EUiAnimProperty::ALPHA);
	Runtime.Advance(0.0f);
	const float After = Runtime.GetValue(103, EUiAnimProperty::ALPHA);
	EXPECT_NEAR(Before, After, 1e-6f);
	EXPECT_TRUE(Runtime.HasActiveAnimation(103, EUiAnimProperty::ALPHA));
}

TEST(UiV2AnimSpring, ClampedDtNoExplosion)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(104, EUiAnimProperty::POS_X, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeSpringRequest(104, EUiAnimProperty::POS_X, 100.0f, 84)));

	Runtime.Advance(1.0f);
	const float After = Runtime.GetValue(104, EUiAnimProperty::POS_X);

	EXPECT_GE(After, 0.0f);
	EXPECT_LE(After, 150.0f);
}

TEST(UiV2AnimSpring, MergeTargetPreservesVelocity)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(301, EUiAnimProperty::POS_X, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeSpringRequest(301, EUiAnimProperty::POS_X, 100.0f, 121)));

	AdvanceFor(Runtime, 0.15f);
	const float Before = Runtime.GetValue(301, EUiAnimProperty::POS_X);
	EXPECT_GT(Before, 0.0f);
	EXPECT_LT(Before, 100.0f);

	SUiAnimRequest MergeReq = MakeSpringRequest(301, EUiAnimProperty::POS_X, -100.0f, 122);
	MergeReq.m_Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
	EXPECT_TRUE(Runtime.RequestAnimation(MergeReq));

	EXPECT_NEAR(Before, Runtime.GetValue(301, EUiAnimProperty::POS_X), 1e-3f);

	AdvanceFor(Runtime, 3.0f);
	EXPECT_NEAR(Runtime.GetValue(301, EUiAnimProperty::POS_X), -100.0f, 0.5f);
	EXPECT_FALSE(Runtime.HasActiveAnimation(301, EUiAnimProperty::POS_X));
}

TEST(UiV2AnimSpring, ResolveSpringRectXYAnimatesOnlyPosition)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2AnimationRuntime Runtime;
	const uint64_t NodeKey = 402;
	Runtime.SetValue(NodeKey, EUiAnimProperty::POS_X, 0.0f);
	Runtime.SetValue(NodeKey, EUiAnimProperty::POS_Y, 0.0f);

	CUIRect Target;
	Target.x = 40.0f;
	Target.y = 80.0f;
	Target.w = 120.0f;
	Target.h = 240.0f;

	const CUIRect Resolved = ResolveUiAnimSpringRectXY(Runtime, NodeKey, Target, ui_spring::SNAPPY);
	EXPECT_NEAR(Resolved.x, 0.0f, 1e-6f);
	EXPECT_NEAR(Resolved.y, 0.0f, 1e-6f);
	EXPECT_NEAR(Resolved.w, Target.w, 1e-6f);
	EXPECT_NEAR(Resolved.h, Target.h, 1e-6f);
	EXPECT_TRUE(Runtime.HasActiveAnimation(NodeKey, EUiAnimProperty::POS_X));
	EXPECT_TRUE(Runtime.HasActiveAnimation(NodeKey, EUiAnimProperty::POS_Y));
	EXPECT_FALSE(Runtime.HasActiveAnimation(NodeKey, EUiAnimProperty::WIDTH));
	EXPECT_FALSE(Runtime.HasActiveAnimation(NodeKey, EUiAnimProperty::HEIGHT));
}

TEST(UiV2AnimSpring, CardReorderFlipKeepsReleaseMotionVisible)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2Tree Tree;
	CUiV2AnimationRuntime Runtime;
	const uint64_t NodeKey = 404;

	CUIRect Target;
	Target.x = 100.0f;
	Target.y = 200.0f;
	Target.w = 180.0f;
	Target.h = 90.0f;

	const CUIRect FirstResolved = Tree.ResolveLayoutTransition(Runtime, NodeKey, Target, ui_token::motion::CARD_REORDER);
	EXPECT_NEAR(FirstResolved.x, Target.x, 1e-6f);
	EXPECT_NEAR(FirstResolved.y, Target.y, 1e-6f);

	CUIRect NextTarget = Target;
	NextTarget.x = 220.0f;
	NextTarget.y = 320.0f;

	const CUIRect StartResolved = Tree.ResolveLayoutTransition(Runtime, NodeKey, NextTarget, ui_token::motion::CARD_REORDER);
	EXPECT_NEAR(StartResolved.x, Target.x, 1e-3f);
	EXPECT_NEAR(StartResolved.y, Target.y, 1e-3f);

	AdvanceFor(Runtime, 0.15f);
	const CUIRect MidResolved = Tree.ResolveLayoutTransition(Runtime, NodeKey, NextTarget, ui_token::motion::CARD_REORDER);
	EXPECT_NEAR(MidResolved.w, NextTarget.w, 1e-6f);
	EXPECT_NEAR(MidResolved.h, NextTarget.h, 1e-6f);
	EXPECT_LT(std::abs(MidResolved.x - NextTarget.x), 4.0f);
	EXPECT_LT(std::abs(MidResolved.y - NextTarget.y), 4.0f);
	EXPECT_NE(MidResolved.x, StartResolved.x);
	EXPECT_NE(MidResolved.y, StartResolved.y);

	AdvanceFor(Runtime, 8.0f);
	EXPECT_NEAR(Runtime.GetValue(NodeKey, EUiAnimProperty::POS_X), NextTarget.x, 1.0f);
	EXPECT_NEAR(Runtime.GetValue(NodeKey, EUiAnimProperty::POS_Y), NextTarget.y, 1.0f);
}
