#include <engine/shared/config.h>

#include <game/client/QmUi/QmTree.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <new>

namespace
{
	constexpr uint64_t NODE = 1001;
	struct SContext
	{
		CUiV2Tree m_Tree;
		CUiV2AnimationRuntime m_Runtime;
		SUiAnimTransition m_Transition;
		SUiPresenceResult m_Presence;
		const CUIRect m_Target{12, 24, 100, 40};
		SContext()
		{
			m_Transition.m_DurationSec = 0.2f;
			m_Transition.m_Easing = EEasing::LINEAR;
		}
		void ShowSettled()
		{
			m_Tree.BeginFrame();
			m_Tree.ResolvePresence(m_Runtime, NODE, true, m_Transition);
			m_Tree.EndFrame(m_Runtime);
			m_Runtime.Advance(1.0f);
			m_Tree.BeginFrame();
			m_Tree.ResolvePresence(m_Runtime, NODE, true, m_Transition);
			m_Tree.EndFrame(m_Runtime);
			m_Tree.BeginFrame();
		}
	};

	// 每次新建实际节点树/后端，逐次拒绝生产操作的分配，直到操作完整成功。
	// 断言在故障域退出后执行，不把Google Test自身的分配计入生产路径。
	template<typename TPrepare, typename TAction, typename TFailure, typename TSuccess>
	void ForEachAllocationFailure(TPrepare Prepare, TAction Action, TFailure Failure, TSuccess Success)
	{
		int Failures = 0;
		bool Completed = false;
		for(size_t Budget = 0; Budget < 64; ++Budget)
		{
			SCOPED_TRACE(Budget);
			SContext Context;
			Prepare(Context);
			bool Triggered = false, Threw = false;
			{
				CPredictionAllocationFailure Fault(Budget);
				try
				{
					Action(Context);
				}
				catch(const std::bad_alloc &)
				{
					Threw = true;
				}
				Triggered = Fault.Triggered();
			}
			ASSERT_EQ(Triggered, Threw);
			if(!Triggered)
			{
				Success(Context);
				Completed = true;
				break;
			}
			++Failures;
			Failure(Context);
		}
		EXPECT_GT(Failures, 0);
		EXPECT_TRUE(Completed);
	}

	void ExpectInitialPresence(SContext &Context, bool Animated)
	{
		const auto Presence = Context.m_Tree.ResolvePresence(Context.m_Runtime, NODE, true, Context.m_Transition);
		EXPECT_TRUE(Presence.m_Render);
		EXPECT_TRUE(Presence.m_FreshEnter);
		EXPECT_FLOAT_EQ(Presence.m_Alpha, Animated ? 0.0f : 1.0f);
		EXPECT_EQ(Context.m_Runtime.HasActiveAnimation(NODE, EUiAnimProperty::ALPHA), Animated);
		Context.m_Runtime.Advance(1.0f);
		EXPECT_FLOAT_EQ(Context.m_Runtime.GetValue(NODE, EUiAnimProperty::ALPHA), 1.0f);
	}

	void ExpectSynchronizedLayout(SContext &Context)
	{
		const auto Rect = Context.m_Tree.ResolveLayoutTransition(Context.m_Runtime, NODE, Context.m_Target, SUiSpringConfig{});
		EXPECT_FLOAT_EQ(Rect.x, Context.m_Target.x);
		EXPECT_FLOAT_EQ(Rect.y, Context.m_Target.y);
		EXPECT_FLOAT_EQ(Rect.w, Context.m_Target.w);
		EXPECT_FLOAT_EQ(Rect.h, Context.m_Target.h);
		EXPECT_TRUE(Context.m_Runtime.HasValue(NODE, EUiAnimProperty::POS_X));
		EXPECT_TRUE(Context.m_Runtime.HasValue(NODE, EUiAnimProperty::POS_Y));
		EXPECT_FLOAT_EQ(Context.m_Runtime.GetValue(NODE, EUiAnimProperty::POS_X), Context.m_Target.x);
		EXPECT_FLOAT_EQ(Context.m_Runtime.GetValue(NODE, EUiAnimProperty::POS_Y), Context.m_Target.y);
		EXPECT_FALSE(Context.m_Runtime.HasActiveAnimation(NODE, EUiAnimProperty::POS_X));
		EXPECT_FALSE(Context.m_Runtime.HasActiveAnimation(NODE, EUiAnimProperty::POS_Y));
	}
}

class QmTreeAllocationFailure : public ::testing::Test
{
	int m_PreviousMotionLevel = 0;
	void SetUp() override
	{
		m_PreviousMotionLevel = g_Config.m_QmUiMotionLevel;
		g_Config.m_QmUiMotionLevel = 2;
	}
	void TearDown() override { g_Config.m_QmUiMotionLevel = m_PreviousMotionLevel; }
};

TEST_F(QmTreeAllocationFailure, FirstPresenceFailureLeavesNoNodeAndRetriesTheEntrance)
{
	ForEachAllocationFailure([](SContext &) {},
		[](SContext &C) { C.m_Presence = C.m_Tree.ResolvePresence(C.m_Runtime, NODE, true, C.m_Transition); },
		[](SContext &C) {
			C.m_Tree.EndFrame();
			EXPECT_EQ(C.m_Tree.NodeCount(), 0);
			C.m_Tree.EndFrame(C.m_Runtime);
			EXPECT_EQ(C.m_Tree.NodeCount(), 0);
			ExpectInitialPresence(C, true);
		},
		[](SContext &C) {
			EXPECT_TRUE(C.m_Presence.m_FreshEnter);
			EXPECT_FLOAT_EQ(C.m_Presence.m_Alpha, 0.0f);
			EXPECT_TRUE(C.m_Runtime.HasActiveAnimation(NODE, EUiAnimProperty::ALPHA));
		});
}

TEST_F(QmTreeAllocationFailure, FailedPresenceKeepsAnIndependentlyTouchedNode)
{
	ForEachAllocationFailure([](SContext &C) { C.m_Tree.TouchNode(NODE); },
		[](SContext &C) { C.m_Tree.ResolvePresence(C.m_Runtime, NODE, true, C.m_Transition); },
		[](SContext &C) {
			C.m_Tree.EndFrame();
			EXPECT_EQ(C.m_Tree.NodeCount(), 1);
			ExpectInitialPresence(C, true);
		},
		[](SContext &C) {
			C.m_Tree.EndFrame(C.m_Runtime);
			EXPECT_EQ(C.m_Tree.NodeCount(), 1);
		});
}

TEST_F(QmTreeAllocationFailure, MotionOffFirstPresenceFailureRetriesAtTheVisibleValue)
{
	g_Config.m_QmUiMotionLevel = 0;
	ForEachAllocationFailure([](SContext &) {},
		[](SContext &C) { C.m_Tree.ResolvePresence(C.m_Runtime, NODE, true, C.m_Transition); },
		[](SContext &C) { ExpectInitialPresence(C, false); },
		[](SContext &C) {
			EXPECT_FLOAT_EQ(C.m_Runtime.GetValue(NODE, EUiAnimProperty::ALPHA), 1.0f);
			EXPECT_FALSE(C.m_Runtime.HasActiveAnimation(NODE, EUiAnimProperty::ALPHA));
		});
}

TEST_F(QmTreeAllocationFailure, FailedExplicitExitRetriesTheFadeAndEventuallyRemovesTheNode)
{
	const auto VerifyExit = [](SContext &C) {
		EXPECT_TRUE(C.m_Runtime.HasActiveAnimation(NODE, EUiAnimProperty::ALPHA));
		C.m_Runtime.Advance(1.0f);
		EXPECT_FALSE(C.m_Tree.ResolvePresence(C.m_Runtime, NODE, false, C.m_Transition).m_Render);
		C.m_Tree.EndFrame(C.m_Runtime);
		EXPECT_EQ(C.m_Tree.NodeCount(), 0);
	};
	ForEachAllocationFailure([](SContext &C) { C.ShowSettled(); },
		[](SContext &C) { C.m_Tree.ResolvePresence(C.m_Runtime, NODE, false, C.m_Transition); },
		[&](SContext &C) {
			const auto Presence = C.m_Tree.ResolvePresence(C.m_Runtime, NODE, false, C.m_Transition);
			EXPECT_TRUE(Presence.m_Render);
			EXPECT_FLOAT_EQ(Presence.m_Alpha, 1.0f);
			VerifyExit(C);
		},
		VerifyExit);
}

TEST_F(QmTreeAllocationFailure, FailedMissingNodeExitAtFrameEndCanRetry)
{
	const auto VerifyExit = [](SContext &C) {
		EXPECT_EQ(C.m_Tree.NodeCount(), 1);
		EXPECT_TRUE(C.m_Runtime.HasActiveAnimation(NODE, EUiAnimProperty::ALPHA));
		C.m_Runtime.Advance(1.0f);
		C.m_Tree.BeginFrame();
		C.m_Tree.EndFrame(C.m_Runtime);
		EXPECT_EQ(C.m_Tree.NodeCount(), 0);
	};
	ForEachAllocationFailure([](SContext &C) { C.ShowSettled(); },
		[](SContext &C) { C.m_Tree.EndFrame(C.m_Runtime); },
		[&](SContext &C) {
			C.m_Tree.EndFrame(C.m_Runtime);
			VerifyExit(C);
		},
		VerifyExit);
}

TEST_F(QmTreeAllocationFailure, FailedFrameEndDoesNotForgetOtherVisibleNodes)
{
	// 遍历顺序属于标准库；逐个选择缺席节点，不能假定某个键先被访问。
	for(uint64_t Hidden = NODE; Hidden < NODE + 3; ++Hidden)
	{
		SCOPED_TRACE(Hidden);
		const auto Verify = [Hidden](SContext &C) {
			for(uint64_t Key = NODE; Key < NODE + 3; ++Key)
			{
				SCOPED_TRACE(Key);
				EXPECT_EQ(C.m_Runtime.HasActiveAnimation(Key, EUiAnimProperty::ALPHA), Key == Hidden);
				if(Key != Hidden)
					EXPECT_FLOAT_EQ(C.m_Runtime.GetValue(Key, EUiAnimProperty::ALPHA), 1.0f);
			}
		};
		ForEachAllocationFailure([Hidden](SContext &C) {
			C.m_Tree.BeginFrame();
			for(uint64_t Key = NODE; Key < NODE + 3; ++Key)
				C.m_Tree.ResolvePresence(C.m_Runtime, Key, true, C.m_Transition);
			C.m_Tree.EndFrame(C.m_Runtime);
			C.m_Runtime.Advance(1.0f);
			C.m_Tree.BeginFrame();
			for(uint64_t Key = NODE; Key < NODE + 3; ++Key)
				if(Key != Hidden)
					C.m_Tree.ResolvePresence(C.m_Runtime, Key, true, C.m_Transition); }, [](SContext &C) { C.m_Tree.EndFrame(C.m_Runtime); },
			[&](SContext &C) {
				C.m_Tree.EndFrame(C.m_Runtime);
				Verify(C);
			},
			Verify);
	}
}

TEST_F(QmTreeAllocationFailure, FailedInitialLayoutResolveRetriesBothAxesWithoutAnimation)
{
	ForEachAllocationFailure([](SContext &) {},
		[](SContext &C) { C.m_Tree.ResolveLayoutTransition(C.m_Runtime, NODE, C.m_Target, SUiSpringConfig{}); },
		ExpectSynchronizedLayout, ExpectSynchronizedLayout);
}

TEST_F(QmTreeAllocationFailure, FailedInitialLayoutSyncDoesNotPublishAnInitializedTarget)
{
	ForEachAllocationFailure([](SContext &) {},
		[](SContext &C) { C.m_Tree.SyncLayoutTransition(C.m_Runtime, NODE, C.m_Target); },
		ExpectSynchronizedLayout, ExpectSynchronizedLayout);
}
