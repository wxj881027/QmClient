// QmAnim 行为测试：tree。
// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <engine/shared/config.h>

#include <game/client/QmUi/QmAnim.h>
#include <game/client/QmUi/QmAnimCurves.h>
#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmDropdown.h>
#include <game/client/QmUi/QmScroll.h>
#include <game/client/QmUi/QmTree.h>
#include <game/client/QmUi/SettingsCardGeometry.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/QmUi/UiFormLogic.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiMotion.h>
#include <game/client/QmUi/UiOverlays.h>
#include <game/client/QmUi/UiTheme.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_rect.h>
#include <game/client/ui_scrollregion.h>

#include <gtest/gtest.h>
#include <test/support/qm_anim_test_helpers.h>
#include <test/test.h>

#include <array>
#include <cmath>

namespace
{

	void AdvanceFor(CUiV2AnimationRuntime &Runtime, float Seconds)
	{
		g_Config.m_QmUiMotionLevel = 2;
		const float Dt = 1.0f / 60.0f;
		int Steps = static_cast<int>(Seconds / Dt) + 1;
		for(int i = 0; i < Steps; ++i)
			Runtime.Advance(Dt);
	}

	SUiAnimRequest MakeRequest(uint64_t NodeKey, EUiAnimProperty Property, float Target, float DurationSec, int Priority, EUiAnimInterruptPolicy Interrupt, uint32_t TrackId)
	{
		g_Config.m_QmUiMotionLevel = 2;
		SUiAnimRequest Request;
		Request.m_NodeKey = NodeKey;
		Request.m_Property = Property;
		Request.m_Target = Target;
		Request.m_Transition.m_DurationSec = DurationSec;
		Request.m_Transition.m_Priority = Priority;
		Request.m_Transition.m_Interrupt = Interrupt;
		Request.m_Transition.m_Easing = EEasing::LINEAR;
		Request.m_TrackId = TrackId;
		return Request;
	}
}

TEST(UiV2TreeLayoutTransition, StartsInstantlyAndAnimatesOnChange)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2Tree Tree;
	CUiV2AnimationRuntime Runtime;
	const uint64_t NodeKey = 403;
	Runtime.SetValue(NodeKey, EUiAnimProperty::POS_X, 0.0f);
	Runtime.SetValue(NodeKey, EUiAnimProperty::POS_Y, 0.0f);

	CUIRect Target;
	Target.x = 40.0f;
	Target.y = 80.0f;
	Target.w = 120.0f;
	Target.h = 240.0f;

	const CUIRect FirstResolved = Tree.ResolveLayoutTransition(Runtime, NodeKey, Target, ui_spring::SNAPPY);
	EXPECT_NEAR(FirstResolved.x, Target.x, 1e-6f);
	EXPECT_NEAR(FirstResolved.y, Target.y, 1e-6f);
	EXPECT_NEAR(FirstResolved.w, Target.w, 1e-6f);
	EXPECT_NEAR(FirstResolved.h, Target.h, 1e-6f);
	EXPECT_FALSE(Runtime.HasActiveAnimation(NodeKey, EUiAnimProperty::POS_X));
	EXPECT_FALSE(Runtime.HasActiveAnimation(NodeKey, EUiAnimProperty::POS_Y));

	AdvanceFor(Runtime, 0.15f);
	EXPECT_NEAR(Runtime.GetValue(NodeKey, EUiAnimProperty::POS_X), Target.x, 1e-6f);
	EXPECT_NEAR(Runtime.GetValue(NodeKey, EUiAnimProperty::POS_Y), Target.y, 1e-6f);

	CUIRect NextTarget = Target;
	NextTarget.x = 90.0f;
	NextTarget.y = 120.0f;

	const CUIRect SecondResolved = Tree.ResolveLayoutTransition(Runtime, NodeKey, NextTarget, ui_spring::SNAPPY);
	EXPECT_NEAR(SecondResolved.x, Target.x, 1e-3f);
	EXPECT_NEAR(SecondResolved.y, Target.y, 1e-3f);
	EXPECT_NEAR(SecondResolved.w, NextTarget.w, 1e-6f);
	EXPECT_NEAR(SecondResolved.h, NextTarget.h, 1e-6f);
	EXPECT_TRUE(Runtime.HasActiveAnimation(NodeKey, EUiAnimProperty::POS_X));
	EXPECT_TRUE(Runtime.HasActiveAnimation(NodeKey, EUiAnimProperty::POS_Y));

	Runtime.Advance(1.0f / 60.0f);
	const CUIRect ThirdResolved = Tree.ResolveLayoutTransition(Runtime, NodeKey, NextTarget, ui_spring::SNAPPY);
	EXPECT_GT(ThirdResolved.x, Target.x);
	EXPECT_LT(ThirdResolved.x, NextTarget.x);
	EXPECT_GT(ThirdResolved.y, Target.y);
	EXPECT_LT(ThirdResolved.y, NextTarget.y);
	EXPECT_NEAR(ThirdResolved.w, NextTarget.w, 1e-6f);
	EXPECT_NEAR(ThirdResolved.h, NextTarget.h, 1e-6f);

	AdvanceFor(Runtime, 3.0f);
	EXPECT_NEAR(Runtime.GetValue(NodeKey, EUiAnimProperty::POS_X), NextTarget.x, 0.5f);
	EXPECT_NEAR(Runtime.GetValue(NodeKey, EUiAnimProperty::POS_Y), NextTarget.y, 0.5f);
}
TEST(UiV2TreeLayoutTransition, FirstTargetSyncsImmediately)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2Tree Tree;
	CUiV2AnimationRuntime Runtime;

	CUIRect Target;
	Target.x = 24.0f;
	Target.y = 48.0f;
	Target.w = 140.0f;
	Target.h = 72.0f;

	const CUIRect FirstResolved = Tree.ResolveLayoutTransition(Runtime, 901, Target, ui_spring::SNAPPY);
	EXPECT_NEAR(FirstResolved.x, Target.x, 1e-6f);
	EXPECT_NEAR(FirstResolved.y, Target.y, 1e-6f);
	EXPECT_NEAR(FirstResolved.w, Target.w, 1e-6f);
	EXPECT_NEAR(FirstResolved.h, Target.h, 1e-6f);
	EXPECT_FALSE(Runtime.HasActiveAnimation(901, EUiAnimProperty::POS_X));
	EXPECT_FALSE(Runtime.HasActiveAnimation(901, EUiAnimProperty::POS_Y));
}
TEST(UiV2TreeLayoutTransition, ReusesStableNodeAcrossFrames)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2Tree Tree;
	CUiV2AnimationRuntime Runtime;

	CUIRect Target;
	Target.x = 18.0f;
	Target.y = 36.0f;
	Target.w = 110.0f;
	Target.h = 60.0f;

	const CUIRect FirstResolved = Tree.ResolveLayoutTransition(Runtime, 902, Target, ui_spring::SNAPPY);
	EXPECT_NEAR(FirstResolved.x, Target.x, 1e-6f);
	EXPECT_NEAR(FirstResolved.y, Target.y, 1e-6f);

	CUIRect NextTarget = Target;
	NextTarget.x = 58.0f;
	NextTarget.y = 96.0f;

	const CUIRect SecondResolved = Tree.ResolveLayoutTransition(Runtime, 902, NextTarget, ui_spring::SNAPPY);
	EXPECT_NEAR(SecondResolved.x, Target.x, 1e-3f);
	EXPECT_NEAR(SecondResolved.y, Target.y, 1e-3f);
	EXPECT_NEAR(SecondResolved.w, NextTarget.w, 1e-6f);
	EXPECT_NEAR(SecondResolved.h, NextTarget.h, 1e-6f);

	AdvanceFor(Runtime, 0.2f);
	const CUIRect MidResolved = Tree.ResolveLayoutTransition(Runtime, 902, NextTarget, ui_spring::SNAPPY);
	EXPECT_GT(MidResolved.x, Target.x);
	EXPECT_LT(MidResolved.x, NextTarget.x);
	EXPECT_GT(MidResolved.y, Target.y);
	EXPECT_LT(MidResolved.y, NextTarget.y);
	EXPECT_NEAR(MidResolved.w, NextTarget.w, 1e-6f);
	EXPECT_NEAR(MidResolved.h, NextTarget.h, 1e-6f);
}
TEST(UiV2TreeLayoutTransition, CanSyncTargetWithoutAnimating)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2Tree Tree;
	CUiV2AnimationRuntime Runtime;
	const uint64_t NodeKey = 903;

	CUIRect Target;
	Target.x = 10.0f;
	Target.y = 20.0f;
	Target.w = 120.0f;
	Target.h = 60.0f;

	EXPECT_EQ(Tree.ResolveLayoutTransition(Runtime, NodeKey, Target, ui_spring::SNAPPY).x, Target.x);

	CUIRect DragTarget = Target;
	DragTarget.x = 80.0f;
	DragTarget.y = 160.0f;

	const CUIRect DragResolved = Tree.ResolveLayoutTransition(Runtime, NodeKey, DragTarget, ui_spring::SNAPPY, 1, false);
	EXPECT_NEAR(DragResolved.x, DragTarget.x, 1e-6f);
	EXPECT_NEAR(DragResolved.y, DragTarget.y, 1e-6f);
	EXPECT_NEAR(Runtime.GetValue(NodeKey, EUiAnimProperty::POS_X), DragTarget.x, 1e-6f);
	EXPECT_NEAR(Runtime.GetValue(NodeKey, EUiAnimProperty::POS_Y), DragTarget.y, 1e-6f);
	EXPECT_FALSE(Runtime.HasActiveAnimation(NodeKey, EUiAnimProperty::POS_X));
	EXPECT_FALSE(Runtime.HasActiveAnimation(NodeKey, EUiAnimProperty::POS_Y));
}
TEST(UiV2TreeLayoutTransition, ScrollOffsetDoesNotDriveCardLayoutSpring)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2Tree Tree;
	CUiV2AnimationRuntime Runtime;
	const uint64_t NodeKey = 905;

	auto ToAnimRect = [](CUIRect Rect, float ScrollOffsetY) {
		Rect.y -= ScrollOffsetY;
		return Rect;
	};
	auto ToScreenRect = [](CUIRect Rect, float ScrollOffsetY) {
		Rect.y += ScrollOffsetY;
		return Rect;
	};

	CUIRect Target;
	Target.x = 32.0f;
	Target.y = 96.0f;
	Target.w = 140.0f;
	Target.h = 72.0f;

	const CUIRect FirstScreen = ToScreenRect(Tree.ResolveLayoutTransition(Runtime, NodeKey, ToAnimRect(Target, 0.0f), ui_token::motion::CARD_REORDER), 0.0f);
	EXPECT_NEAR(FirstScreen.y, Target.y, 1e-6f);

	const float ScrollOffsetY = -64.0f;
	CUIRect ScrolledTarget = Target;
	ScrolledTarget.y += ScrollOffsetY;
	const CUIRect ScrolledScreen = ToScreenRect(Tree.ResolveLayoutTransition(Runtime, NodeKey, ToAnimRect(ScrolledTarget, ScrollOffsetY), ui_token::motion::CARD_REORDER), ScrollOffsetY);
	EXPECT_NEAR(ScrolledScreen.y, ScrolledTarget.y, 1e-6f);
	EXPECT_FALSE(Runtime.HasActiveAnimation(NodeKey, EUiAnimProperty::POS_Y));

	CUIRect ReorderedTarget = Target;
	ReorderedTarget.y += 120.0f;
	CUIRect ReorderedScreenTarget = ReorderedTarget;
	ReorderedScreenTarget.y += ScrollOffsetY;
	const CUIRect ReorderedStart = ToScreenRect(Tree.ResolveLayoutTransition(Runtime, NodeKey, ToAnimRect(ReorderedScreenTarget, ScrollOffsetY), ui_token::motion::CARD_REORDER), ScrollOffsetY);
	EXPECT_LT(ReorderedStart.y, ReorderedScreenTarget.y);
	EXPECT_TRUE(Runtime.HasActiveAnimation(NodeKey, EUiAnimProperty::POS_Y));
}
TEST(UiV2TreeLayoutTransition, DragReleaseCanAnimateFromPointerPosition)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2Tree Tree;
	CUiV2AnimationRuntime Runtime;
	const uint64_t NodeKey = 904;

	CUIRect InitialTarget;
	InitialTarget.x = 40.0f;
	InitialTarget.y = 80.0f;
	InitialTarget.w = 180.0f;
	InitialTarget.h = 90.0f;
	Tree.ResolveLayoutTransition(Runtime, NodeKey, InitialTarget, ui_token::motion::CARD_REORDER);

	CUIRect DragTarget = InitialTarget;
	DragTarget.x = 260.0f;
	DragTarget.y = 360.0f;
	Tree.SyncLayoutTransition(Runtime, NodeKey, DragTarget);
	EXPECT_NEAR(Runtime.GetValue(NodeKey, EUiAnimProperty::POS_X), DragTarget.x, 1e-6f);
	EXPECT_NEAR(Runtime.GetValue(NodeKey, EUiAnimProperty::POS_Y), DragTarget.y, 1e-6f);

	CUIRect ReleaseTarget = InitialTarget;
	ReleaseTarget.x = 80.0f;
	ReleaseTarget.y = 120.0f;
	const CUIRect ReleaseResolved = Tree.ResolveLayoutTransition(Runtime, NodeKey, ReleaseTarget, ui_token::motion::CARD_REORDER);
	EXPECT_NEAR(ReleaseResolved.x, DragTarget.x, 1e-3f);
	EXPECT_NEAR(ReleaseResolved.y, DragTarget.y, 1e-3f);
	EXPECT_TRUE(Runtime.HasActiveAnimation(NodeKey, EUiAnimProperty::POS_X));
	EXPECT_TRUE(Runtime.HasActiveAnimation(NodeKey, EUiAnimProperty::POS_Y));

	Runtime.Advance(1.0f / 60.0f);
	const CUIRect MidResolved = Tree.ResolveLayoutTransition(Runtime, NodeKey, ReleaseTarget, ui_token::motion::CARD_REORDER);
	EXPECT_LT(MidResolved.x, DragTarget.x);
	EXPECT_GT(MidResolved.x, ReleaseTarget.x);
	EXPECT_LT(MidResolved.y, DragTarget.y);
	EXPECT_GT(MidResolved.y, ReleaseTarget.y);
	EXPECT_NEAR(MidResolved.w, ReleaseTarget.w, 1e-6f);
	EXPECT_NEAR(MidResolved.h, ReleaseTarget.h, 1e-6f);
}
TEST(UiV2TreePresence, EnterStartsFromTransparentAndAnimatesToVisible)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2Tree Tree;
	CUiV2AnimationRuntime Runtime;
	const uint64_t NodeKey = 1001;

	Tree.BeginFrame();
	const float FirstAlpha = Tree.ResolvePresenceAlpha(Runtime, NodeKey, ui_token::motion::HOVER_FADE);
	EXPECT_NEAR(FirstAlpha, 0.0f, 1e-6f);
	EXPECT_TRUE(Runtime.HasActiveAnimation(NodeKey, EUiAnimProperty::ALPHA));
	Tree.EndFrame(Runtime);
	EXPECT_EQ(Tree.NodeCount(), 1);

	AdvanceFor(Runtime, 0.06f);
	Tree.BeginFrame();
	const float MidAlpha = Tree.ResolvePresenceAlpha(Runtime, NodeKey, ui_token::motion::HOVER_FADE);
	EXPECT_GT(MidAlpha, 0.0f);
	EXPECT_LT(MidAlpha, 1.0f);
	Tree.EndFrame(Runtime);

	AdvanceFor(Runtime, 1.0f);
	Tree.BeginFrame();
	const float FinalAlpha = Tree.ResolvePresenceAlpha(Runtime, NodeKey, ui_token::motion::HOVER_FADE);
	EXPECT_NEAR(FinalAlpha, 1.0f, 0.001f);
	Tree.EndFrame(Runtime);
}
TEST(UiV2TreePresence, MissingNodeExitsBeforeRemoval)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2Tree Tree;
	CUiV2AnimationRuntime Runtime;
	const uint64_t NodeKey = 1002;

	Tree.BeginFrame();
	Tree.ResolvePresenceAlpha(Runtime, NodeKey, ui_token::motion::HOVER_FADE);
	Tree.EndFrame(Runtime);
	AdvanceFor(Runtime, 1.0f);

	Tree.BeginFrame();
	Tree.EndFrame(Runtime);
	EXPECT_EQ(Tree.NodeCount(), 1);
	EXPECT_TRUE(Runtime.HasActiveAnimation(NodeKey, EUiAnimProperty::ALPHA));

	AdvanceFor(Runtime, 1.0f);
	Tree.BeginFrame();
	Tree.EndFrame(Runtime);
	EXPECT_EQ(Tree.NodeCount(), 0);
	EXPECT_FALSE(Runtime.HasActiveAnimation(NodeKey, EUiAnimProperty::ALPHA));
}
TEST(UiV2TreePresence, RetouchingExitingNodeCancelsRemoval)
{
	CScopedQmFullMotion FullMotion;
	CUiV2Tree Tree;
	CUiV2AnimationRuntime Runtime;
	const uint64_t NodeKey = 1003;

	Tree.BeginFrame();
	Tree.ResolvePresenceAlpha(Runtime, NodeKey, ui_token::motion::HOVER_FADE);
	Tree.EndFrame(Runtime);
	AdvanceFor(Runtime, 1.0f);

	Tree.BeginFrame();
	Tree.EndFrame(Runtime);
	AdvanceFor(Runtime, 0.08f);
	const float ExitingAlpha = Runtime.GetValue(NodeKey, EUiAnimProperty::ALPHA, 1.0f);
	EXPECT_LT(ExitingAlpha, 1.0f);

	Tree.BeginFrame();
	const float RetouchedAlpha = Tree.ResolvePresenceAlpha(Runtime, NodeKey, ui_token::motion::HOVER_FADE);
	EXPECT_NEAR(RetouchedAlpha, ExitingAlpha, 0.05f);
	Tree.EndFrame(Runtime);
	EXPECT_EQ(Tree.NodeCount(), 1);

	Runtime.Advance(0.05f);
	EXPECT_GT(Runtime.GetValue(NodeKey, EUiAnimProperty::ALPHA), RetouchedAlpha);

	AdvanceFor(Runtime, 1.0f);
	Tree.BeginFrame();
	const float FinalAlpha = Tree.ResolvePresenceAlpha(Runtime, NodeKey, ui_token::motion::HOVER_FADE);
	EXPECT_NEAR(FinalAlpha, 1.0f, 0.001f);
	Tree.EndFrame(Runtime);
}

TEST(UiV2TreePresence, ShowingDuringExplicitExitReversesTheFade)
{
	CScopedQmFullMotion FullMotion;
	CUiV2Tree Tree;
	CUiV2AnimationRuntime Runtime;
	constexpr uint64_t NodeKey = 1004;
	const SUiAnimTransition Transition{.m_DurationSec = 0.12f, .m_Easing = EEasing::EASE_OUT};

	Tree.BeginFrame();
	Tree.ResolvePresence(Runtime, NodeKey, true, Transition);
	Tree.EndFrame(Runtime);
	Runtime.Advance(0.2f);

	Tree.BeginFrame();
	Tree.ResolvePresence(Runtime, NodeKey, false, Transition);
	Tree.EndFrame(Runtime);
	Runtime.Advance(0.04f);
	const float ExitingAlpha = Runtime.GetValue(NodeKey, EUiAnimProperty::ALPHA);
	ASSERT_GT(ExitingAlpha, 0.0f);
	ASSERT_LT(ExitingAlpha, 1.0f);

	Tree.BeginFrame();
	const SUiPresenceResult Reentered = Tree.ResolvePresence(Runtime, NodeKey, true, Transition);
	EXPECT_TRUE(Reentered.m_Render);
	EXPECT_FALSE(Reentered.m_FreshEnter);
	EXPECT_FLOAT_EQ(Reentered.m_Alpha, ExitingAlpha);
	Tree.EndFrame(Runtime);

	Runtime.Advance(0.04f);
	EXPECT_GT(Runtime.GetValue(NodeKey, EUiAnimProperty::ALPHA), ExitingAlpha);
}

TEST(UiV2TreePresence, SplitTracksKeepLogicAndRenderClocksIndependent)
{
	CScopedQmFullMotion FullMotion;
	CUiV2Tree Tree;
	CUiV2AnimationRuntime Runtime;
	constexpr uint64_t NodeKey = 1005;

	Tree.BeginFrame();
	const SUiPresenceResult Entered = Tree.ResolvePresence(Runtime, NodeKey, true, ui_token::motion::MODAL_IN, &ui_token::motion::MODAL_PRESENCE);
	EXPECT_TRUE(Entered.m_FreshEnter);
	Tree.EndFrame(Runtime);

	// 只推进渲染帧：透明度按渲染 dt 前进，显隐状态停在逻辑步上不动。
	Runtime.Advance(0.10f, 0.0f);
	const float RenderAlpha = Runtime.GetValue(NodeKey, EUiAnimProperty::ALPHA);
	EXPECT_GT(RenderAlpha, 0.0f);
	EXPECT_LT(RenderAlpha, 1.0f);
	EXPECT_FLOAT_EQ(Runtime.GetValue(NodeKey, EUiAnimProperty::PRESENCE), 0.0f);

	// 只推进一个逻辑步：显隐状态前进，透明度保持上一帧的展示值。
	Runtime.Advance(0.0f, 1.0f / 120.0f, 1.0f);
	const float PresenceAfterStep = Runtime.GetValue(NodeKey, EUiAnimProperty::PRESENCE);
	EXPECT_GT(PresenceAfterStep, 0.0f);
	EXPECT_FLOAT_EQ(Runtime.GetValue(NodeKey, EUiAnimProperty::ALPHA), RenderAlpha);

	// 两条轨道各自走完后，节点稳定在场且不再持有动画。
	Runtime.Advance(1.0f, 1.0f, 1.0f);
	Tree.BeginFrame();
	const SUiPresenceResult Present = Tree.ResolvePresence(Runtime, NodeKey, true, ui_token::motion::MODAL_IN, &ui_token::motion::MODAL_PRESENCE);
	EXPECT_TRUE(Present.m_Render);
	EXPECT_FALSE(Present.m_FreshEnter);
	EXPECT_NEAR(Present.m_Alpha, 1.0f, 0.001f);
	EXPECT_FALSE(Runtime.HasActiveAnimation(NodeKey, EUiAnimProperty::PRESENCE));
	EXPECT_FALSE(Runtime.HasActiveAnimation(NodeKey, EUiAnimProperty::ALPHA));
	Tree.EndFrame(Runtime);
}
