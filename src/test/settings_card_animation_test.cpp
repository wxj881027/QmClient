#include <game/client/components/menus.h>

#include <gtest/gtest.h>

TEST(SettingsCardAnimation, GeometryMotionIncludesCardsPushedByAnEarlierHeightAnimation)
{
	EXPECT_FALSE(SettingsCardDeckGeometryMoved(false, 100.0f, 80.0f, 120.0f, 80.0f));
	EXPECT_FALSE(SettingsCardDeckGeometryMoved(true, 100.0f, 80.0f, 100.0f, 80.0f));
	EXPECT_TRUE(SettingsCardDeckGeometryMoved(true, 100.0f, 80.0f, 120.0f, 80.0f));
	EXPECT_TRUE(SettingsCardDeckGeometryMoved(true, 100.0f, 80.0f, 100.0f, 90.0f));
}

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
