#include <engine/shared/config.h>

#include <game/client/QmUi/QmAnim.h>
#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsCardDeck.h>
#include <game/client/QmUi/SettingsCardDeckLogic.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/components/menus.h>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>


TEST(SettingsCardAnimation, AnimatedColumnFramesNeverOverlap)
{
	const float Gap = 12.0f;
	const SSettingsCardColumnFrame First = ResolveSettingsCardColumnFrame(100.0f, 80.0f, Gap);
	const SSettingsCardColumnFrame Second = ResolveSettingsCardColumnFrame(First.m_NextY, 140.0f, Gap);
	EXPECT_FLOAT_EQ(First.m_NextY, Second.m_Y);
	EXPECT_GE(Second.m_Y, First.m_Y + First.m_Height + Gap);
	EXPECT_FLOAT_EQ(Second.m_Height, 140.0f);
}

TEST(SettingsCardAnimation, StableAnimationFramesSkipRuntimeWork)
{
	const SSettingsCardAnimationWork Stable = ResolveSettingsCardAnimationWork(0.16f, false, false, false, 0.18f, false, false);
	EXPECT_FALSE(Stable.m_ResolveEntry);
	EXPECT_FALSE(Stable.m_ResetEntry);
	EXPECT_FALSE(Stable.m_ResolveReflow);
	EXPECT_FALSE(Stable.m_SetReflowTarget);

	const SSettingsCardAnimationWork TargetChanged = ResolveSettingsCardAnimationWork(0.16f, false, false, false, 0.18f, true, false);
	EXPECT_TRUE(TargetChanged.m_ResolveReflow);
	EXPECT_FALSE(TargetChanged.m_SetReflowTarget);

	const SSettingsCardAnimationWork Active = ResolveSettingsCardAnimationWork(0.16f, true, false, false, 0.18f, false, true);
	EXPECT_TRUE(Active.m_ResolveEntry);
	EXPECT_TRUE(Active.m_ResolveReflow);
}

TEST(SettingsCardAnimation, DisabledOrSnappedAnimationsOnlyResetTargets)
{
	const SSettingsCardAnimationWork Disabled = ResolveSettingsCardAnimationWork(0.0f, true, false, false, 0.0f, true, true);
	EXPECT_TRUE(Disabled.m_ResetEntry);
	EXPECT_FALSE(Disabled.m_ResolveReflow);
	EXPECT_TRUE(Disabled.m_SetReflowTarget);

	const SSettingsCardAnimationWork Snapped = ResolveSettingsCardAnimationWork(0.16f, false, false, true, 0.18f, true, true);
	EXPECT_FALSE(Snapped.m_ResolveReflow);
	EXPECT_TRUE(Snapped.m_SetReflowTarget);

	const SSettingsCardAnimationWork FirstFrame = ResolveSettingsCardAnimationWork(0.16f, false, true, false, 0.18f, false, false);
	EXPECT_FALSE(FirstFrame.m_SetReflowTarget);
}

TEST(SettingsCardDeck, ContentHeightAnimationSkipsStableFramesAndSnapsFirstLayout)
{
	const SSettingsCardHeightAnimationWork FirstLayout = ResolveSettingsCardHeightAnimationWork(true, false, false, 0.18f, false);
	EXPECT_FALSE(FirstLayout.m_ResolveHeight);
	EXPECT_FALSE(FirstLayout.m_SetHeightTarget);

	const SSettingsCardHeightAnimationWork Stable = ResolveSettingsCardHeightAnimationWork(false, false, false, 0.18f, false);
	EXPECT_FALSE(Stable.m_ResolveHeight);
	EXPECT_FALSE(Stable.m_SetHeightTarget);

	const SSettingsCardHeightAnimationWork Changed = ResolveSettingsCardHeightAnimationWork(false, true, false, 0.18f, false);
	EXPECT_TRUE(Changed.m_ResolveHeight);
	EXPECT_FALSE(Changed.m_SetHeightTarget);

	const SSettingsCardHeightAnimationWork Active = ResolveSettingsCardHeightAnimationWork(false, false, true, 0.18f, false);
	EXPECT_TRUE(Active.m_ResolveHeight);
	EXPECT_FALSE(Active.m_SetHeightTarget);

	const SSettingsCardHeightAnimationWork Disabled = ResolveSettingsCardHeightAnimationWork(false, true, true, 0.0f, false);
	EXPECT_FALSE(Disabled.m_ResolveHeight);
	EXPECT_TRUE(Disabled.m_SetHeightTarget);

	const SSettingsCardHeightAnimationWork DragSnap = ResolveSettingsCardHeightAnimationWork(false, true, true, 0.18f, true);
	EXPECT_FALSE(DragSnap.m_ResolveHeight);
	EXPECT_TRUE(DragSnap.m_SetHeightTarget);
}

TEST(SettingsCardDeck, AnimatedColumnFramesNeverOverlapFollowingCards)
{
	for(const float Progress : {0.0f, 0.2f, 0.5f, 0.8f, 1.0f})
	{
		CSettingsCardColumnFramePlan ColumnPlan(100.0f, 10.0f);
		const SSettingsCardColumnFrame First = ColumnPlan.Append(240.0f + (60.0f - 240.0f) * Progress);
		const SSettingsCardColumnFrame Second = ColumnPlan.Append(90.0f + (180.0f - 90.0f) * Progress);
		const SSettingsCardColumnFrame Third = ColumnPlan.Append(120.0f + (40.0f - 120.0f) * Progress);
		EXPECT_FLOAT_EQ(Second.m_Y, First.m_NextY);
		EXPECT_FLOAT_EQ(Third.m_Y, Second.m_NextY);
		EXPECT_GE(Second.m_Y, First.m_Y + First.m_Height + 10.0f);
		EXPECT_GE(Third.m_Y, Second.m_Y + Second.m_Height + 10.0f);
		EXPECT_FLOAT_EQ(ColumnPlan.CursorY(), Third.m_NextY);
	}

	CSettingsCardColumnFramePlan ClampedPlan(50.0f, -5.0f);
	const SSettingsCardColumnFrame Clamped = ClampedPlan.Append(-20.0f);
	EXPECT_FLOAT_EQ(Clamped.m_Height, 0.0f);
	EXPECT_FLOAT_EQ(Clamped.m_NextY, 50.0f);
}

TEST(SettingsCardDeck, RuntimeHeightAnimationKeepsFollowingGeometryDisjoint)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2AnimationRuntime Runtime;
	CSettingsCardDeckFrameRuntime FrameRuntime;
	SSettingsCardDeckFrameDiagnostics Diagnostics;
	const SSettingsCardSpec Spec{"animated-card", "Animated", nullptr};
	constexpr uint64_t HeightKey = 0x51f5a7ULL;
	constexpr float InitialHeight = 60.0f;
	constexpr float TargetHeight = 220.0f;
	constexpr float FollowingHeight = 90.0f;
	constexpr float CardGap = 10.0f;
	Runtime.SetValue(HeightKey, EUiAnimProperty::HEIGHT, InitialHeight);

	float AnimatedHeight = ResolveUiAnimValue(Runtime, HeightKey, EUiAnimProperty::HEIGHT, TargetHeight, 0.18f, EEasing::EASE_OUT);
	EXPECT_FLOAT_EQ(AnimatedHeight, InitialHeight);
	for(int FrameIndex = 0; FrameIndex < 16; ++FrameIndex)
	{
		FrameRuntime.BeginFrame(&Diagnostics);
		CSettingsCardColumnFramePlan ColumnPlan(100.0f, CardGap);
		const SSettingsCardFrame FirstCard = BuildSettingsCardFrame({10.0f, ColumnPlan.CursorY(), 200.0f, 0.0f}, Spec, AnimatedHeight, 1.0f);
		const SSettingsCardColumnFrame First = ColumnPlan.Append(FirstCard.m_Rect.h);
		FrameRuntime.RecordGeometry({Spec.m_pStableId, 1, FirstCard.m_Rect, TargetHeight, FirstCard.m_ContentRect.h, FrameIndex == 0, Runtime.HasActiveAnimation(HeightKey, EUiAnimProperty::HEIGHT)});
		const SSettingsCardFrame FollowingCard = BuildSettingsCardFrame({10.0f, ColumnPlan.CursorY(), 200.0f, 0.0f}, Spec, FollowingHeight, 1.0f);
		const SSettingsCardColumnFrame Following = ColumnPlan.Append(FollowingCard.m_Rect.h);
		EXPECT_GE(Following.m_Y, First.m_Y + First.m_Height + CardGap);
		EXPECT_FLOAT_EQ(Following.m_Y, First.m_NextY);
		ASSERT_EQ(Diagnostics.m_GeometryCount, 1u);
		EXPECT_FLOAT_EQ(Diagnostics.m_aGeometry[0].m_AnimatedContentHeight, AnimatedHeight);
		EXPECT_FLOAT_EQ(Diagnostics.m_aGeometry[0].m_Rect.h, First.m_Height);

		Runtime.Advance(1.0f / 60.0f);
		if(Runtime.HasActiveAnimation(HeightKey, EUiAnimProperty::HEIGHT))
			AnimatedHeight = ResolveUiAnimValue(Runtime, HeightKey, EUiAnimProperty::HEIGHT, TargetHeight, 0.18f, EEasing::EASE_OUT);
		else
			AnimatedHeight = Runtime.GetValue(HeightKey, EUiAnimProperty::HEIGHT, TargetHeight);
	}
	EXPECT_NEAR(AnimatedHeight, TargetHeight, 0.001f);
}

TEST(SettingsCardDeck, OptionalFrameDiagnosticsRecordAnimatedGeometryWithoutAllocation)
{
	SSettingsCardDeckFrameDiagnostics Diagnostics;
	CSettingsCardDeckFrameRuntime Runtime;
	Runtime.BeginFrame(&Diagnostics);
	for(size_t Index = 0; Index < SSettingsCardDeckFrameDiagnostics::MAX_GEOMETRY + 2; ++Index)
	{
		Runtime.RecordGeometry({
			"card",
			1,
			{10.0f, 20.0f + (float)Index * 50.0f, 200.0f, 40.0f},
			120.0f,
			Index == 0 ? 120.0f : 80.0f,
			Index == 0,
			Index != 0,
		});
	}

	EXPECT_EQ(Diagnostics.m_TotalGeometryCount, SSettingsCardDeckFrameDiagnostics::MAX_GEOMETRY + 2);
	EXPECT_EQ(Diagnostics.m_GeometryCount, SSettingsCardDeckFrameDiagnostics::MAX_GEOMETRY);
	EXPECT_STREQ(Diagnostics.m_aGeometry[0].m_pStableId, "card");
	EXPECT_FLOAT_EQ(Diagnostics.m_aGeometry[0].m_TargetContentHeight, 120.0f);
	EXPECT_FLOAT_EQ(Diagnostics.m_aGeometry[0].m_AnimatedContentHeight, 120.0f);
	EXPECT_TRUE(Diagnostics.m_aGeometry[0].m_FirstLayout);
	EXPECT_FALSE(Diagnostics.m_aGeometry[0].m_HeightAnimationActive);

	Runtime.BeginFrame(nullptr);
	Runtime.RecordGeometry({"ignored", 0, {}, 1.0f, 1.0f, false, false});
	EXPECT_EQ(Diagnostics.m_TotalGeometryCount, SSettingsCardDeckFrameDiagnostics::MAX_GEOMETRY + 2);
}

TEST(SettingsCardDeck, ClipsOnlyWhileContentHeightIsAnimating)
{
	EXPECT_FALSE(SettingsCardDeckShouldClipContent(true, false));
	EXPECT_TRUE(SettingsCardDeckShouldClipContent(true, true));
	EXPECT_FALSE(SettingsCardDeckShouldClipContent(false, true));
}

TEST(SettingsCardDeck, DrawGeometryKeepsSubpixelOffsetsThroughMotionAndRest)
{
	SSettingsCardSpec Spec;
	const SSettingsCardFrame Start = BuildSettingsCardFrame({10.2f, 20.3f, 199.6f, 0.0f}, Spec, 49.4f, 1.0f);
	for(const float Offset : {0.0f, 0.025f, 0.05f, 0.075f, 0.05f, 0.025f, 0.0f})
	{
		const SSettingsCardFrame Draw = ResolveSettingsCardDrawFrame(Start, Offset, Offset);
		EXPECT_FLOAT_EQ(Draw.m_Rect.x, Start.m_Rect.x + Offset);
		EXPECT_FLOAT_EQ(Draw.m_Rect.y, Start.m_Rect.y + Offset);
		EXPECT_FLOAT_EQ(Draw.m_Rect.w, Start.m_Rect.w);
		EXPECT_FLOAT_EQ(Draw.m_Rect.h, Start.m_Rect.h);
		EXPECT_FLOAT_EQ(Draw.m_HandleRect.x, Start.m_HandleRect.x + Offset);
		EXPECT_FLOAT_EQ(Draw.m_HandleRect.y, Start.m_HandleRect.y + Offset);
		EXPECT_FLOAT_EQ(ResolveSettingsCardBorderWidth(1.3f, 0.5f), 2.5f);
	}
}

TEST(SettingsCardDeck, ContinuousEntryKeepsPositionAndVelocityAcrossPageSwitches)
{
	CSettingsCardDeckFrameRuntime Runtime;
	CUiV2AnimationRuntime Animation;
	SCardMotionSpec Motion = ResolveCardMotionSpec(2, true, true, true, true);
	const uint64_t Node = 100;
	Runtime.BeginDisplayCycle(1, true);
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, Node, Motion), Motion.m_EntryDistance);
	Animation.Advance(0.05f);
	const float BeforeSwitch = Runtime.ResolveContinuousEntryOffset(Animation, Node, Motion);
	ASSERT_GT(BeforeSwitch, 0.0f);
	ASSERT_LT(BeforeSwitch, Motion.m_EntryDistance);
	CUiV2AnimationRuntime Reference = Animation;

	Runtime.BeginDisplayCycle(2, true);
	Runtime.OnTabChanged();
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, Node, Motion), BeforeSwitch);
	Animation.Advance(0.01f);
	Reference.Advance(0.01f);
	EXPECT_NEAR(Runtime.ResolveContinuousEntryOffset(Animation, Node, Motion), Reference.GetValue(Node, EUiAnimProperty::POS_Y), 0.0001f);

	Runtime.BeginDisplayCycle(3, true);
	Runtime.OnTabChanged();
	EXPECT_NEAR(Runtime.ResolveContinuousEntryOffset(Animation, Node, Motion), Reference.GetValue(Node, EUiAnimProperty::POS_Y), 0.0001f);
	EXPECT_EQ(Animation.ActiveTrackCount(), 1);
	EXPECT_EQ(Animation.QueuedTrackCount(), 0);
}

TEST(SettingsCardDeck, SubTabStartsEntryInTheClickFrameAndParentCycleDoesNotRestartIt)
{
	CSettingsCardDeckFrameRuntime Runtime;
	CUiV2AnimationRuntime Animation;
	const SCardMotionSpec Motion = ResolveCardMotionSpec(2, true, true, true, true);
	Runtime.BeginDisplayCycle(1, true);
	Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion);
	for(int Frame = 0; Frame < 30; ++Frame)
	{
		Animation.Advance(1.0f / 60.0f);
		Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion);
	}
	ASSERT_FALSE(Runtime.EntryWasActive());

	// 子分类输入发生在父设置页的 display cycle 检测之后，必须当帧启动。
	Runtime.OnTabChanged(true);
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion), Motion.m_EntryDistance);
	Animation.Advance(1.0f / 120.0f);
	const float BeforeParentCycle = Animation.GetValue(100, EUiAnimProperty::POS_Y);
	Runtime.BeginDisplayCycle(2, true);
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion), BeforeParentCycle);
	EXPECT_LT(BeforeParentCycle, Motion.m_EntryDistance);
	EXPECT_EQ(Animation.ActiveTrackCount(), 1);
}

TEST(SettingsCardDeck, ContinuousEntrySettlesWithOnlyASmallReboundAtDifferentRefreshRates)
{
	for(const int RefreshRate : {60, 120, 240})
	{
		SCOPED_TRACE(RefreshRate);
		CSettingsCardDeckFrameRuntime Runtime;
		CUiV2AnimationRuntime Animation;
		const SCardMotionSpec Motion = ResolveCardMotionSpec(2, true, true, true, true);
		Runtime.BeginDisplayCycle(1, true);
		Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion);
		float MinimumOffset = Motion.m_EntryDistance;
		for(int Frame = 0; Frame < RefreshRate / 2; ++Frame)
		{
			Animation.Advance(1.0f / RefreshRate);
			const float Offset = Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion);
			MinimumOffset = std::min(MinimumOffset, Offset);
			EXPECT_LE(Offset, Motion.m_EntryDistance);
		}
		EXPECT_LT(MinimumOffset, 0.0f);
		EXPECT_GT(MinimumOffset, -Motion.m_EntryDistance * 0.03f);
		EXPECT_FALSE(Runtime.EntryWasActive());
		EXPECT_EQ(Animation.ActiveTrackCount(), 0);
		EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion), 0.0f);
		EXPECT_EQ(Animation.ActiveTrackCount(), 0);
	}
}

TEST(SettingsCardDeck, ContinuousEntryCanBeDisabledImmediatelyAndReplayAfterSettling)
{
	CSettingsCardDeckFrameRuntime Runtime;
	CUiV2AnimationRuntime Animation;
	SCardMotionSpec Motion = ResolveCardMotionSpec(2, true, true, true, true);
	Runtime.BeginDisplayCycle(1, true);
	Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion);
	Animation.Advance(0.05f);
	Motion = ResolveCardMotionSpec(0, true, true, true, true);
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion), 0.0f);
	EXPECT_FALSE(Runtime.EntryWasActive());
	EXPECT_EQ(Animation.ActiveTrackCount(), 0);

	Motion = ResolveCardMotionSpec(1, true, true, true, true);
	Runtime.BeginDisplayCycle(2, true);
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion), Motion.m_EntryDistance);
	for(int Frame = 0; Frame < 30; ++Frame)
	{
		Animation.Advance(1.0f / 120.0f);
		Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion);
	}
	EXPECT_FALSE(Runtime.EntryWasActive());
	Runtime.BeginDisplayCycle(3, true);
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion), Motion.m_EntryDistance);

	Runtime.BeginDisplayCycle(4, false);
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion), 0.0f);
	EXPECT_EQ(Animation.ActiveTrackCount(), 0);
}
