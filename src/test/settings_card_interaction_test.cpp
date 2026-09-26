#include <game/client/components/menus.h>

#include <gtest/gtest.h>

TEST(SettingsCardInteraction, EdgeDragRequestsBoundedAutoScroll)
{
	const CUIRect Viewport{0.0f, 100.0f, 600.0f, 400.0f};
	EXPECT_LT(SettingsCardDeckAutoScrollDelta(101.0f, Viewport, 1.0f), 0.0f);
	EXPECT_GT(SettingsCardDeckAutoScrollDelta(499.0f, Viewport, 1.0f), 0.0f);
	EXPECT_FLOAT_EQ(SettingsCardDeckAutoScrollDelta(300.0f, Viewport, 1.0f), 0.0f);
}

TEST(SettingsCardInteraction, CollapsedCardsSkipContentWorkAndExpandedDynamicCardsRemeasure)
{
	EXPECT_FALSE(SettingsCardDeckNeedsContentMeasure(true, false, -1.0f));
	EXPECT_FALSE(SettingsCardDeckRendersContent(true));
	EXPECT_TRUE(SettingsCardDeckNeedsContentMeasure(false, false, -1.0f));
	EXPECT_FALSE(SettingsCardDeckNeedsContentMeasure(false, false, 96.0f));
	EXPECT_TRUE(SettingsCardDeckNeedsContentMeasure(false, true, 96.0f));
	EXPECT_TRUE(SettingsCardDeckRendersContent(false));
}

TEST(SettingsCardInteraction, EveryCardUsesTheSharedCollapseControl)
{
	EXPECT_TRUE(SettingsCardDeckUsesDefaultCollapseControl(false, false));
	EXPECT_FALSE(SettingsCardDeckUsesDefaultCollapseControl(true, false));
	EXPECT_FALSE(SettingsCardDeckUsesDefaultCollapseControl(false, true));
	EXPECT_FALSE(SettingsCardDeckUsesDefaultCollapseControl(true, true));
}

TEST(SettingsCardInteraction, OrdinaryCollapseStateTogglesOnlyFromVisibleHeaderInput)
{
	EXPECT_TRUE(SettingsCardDeckApplyDefaultCollapseToggle(false, false, true, false));
	EXPECT_FALSE(SettingsCardDeckApplyDefaultCollapseToggle(false, true, true, false));
	EXPECT_FALSE(SettingsCardDeckApplyDefaultCollapseToggle(false, false, true, true));
	EXPECT_TRUE(SettingsCardDeckApplyDefaultCollapseToggle(false, true, false, false));
	// 自定义折叠状态的卡片不能被公共折叠按钮改写。
	EXPECT_TRUE(SettingsCardDeckApplyDefaultCollapseToggle(true, true, false, false));
	EXPECT_FALSE(SettingsCardDeckApplyDefaultCollapseToggle(true, false, true, false));
}

TEST(SettingsCardInteraction, PreLayoutReleaseUsesTheLastVisibleAnimatedFrame)
{
	const SSettingsCardSpec Spec{"card", "Card", "Subtitle"};
	const SSettingsCardFrame TargetFrame = BuildSettingsCardFrame({40.0f, 100.0f, 320.0f, 0.0f}, Spec, 120.0f, 1.0f);
	const SSettingsCardFrame VisibleFrame = ResolveSettingsCardDrawFrame(TargetFrame, 0.0f, 18.0f);
	const float ReleaseX = VisibleFrame.m_HandleRect.x + VisibleFrame.m_HandleRect.w * 0.5f;
	const float ReleaseY = VisibleFrame.m_HandleRect.y + VisibleFrame.m_HandleRect.h - 1.0f;

	EXPECT_FALSE(TargetFrame.m_HandleRect.Inside(vec2(ReleaseX, ReleaseY)));
	EXPECT_TRUE(VisibleFrame.m_HandleRect.Inside(vec2(ReleaseX, ReleaseY)));
	EXPECT_FLOAT_EQ(VisibleFrame.m_Rect.y, TargetFrame.m_Rect.y + 18.0f);
	EXPECT_FLOAT_EQ(VisibleFrame.m_HeaderRect.y, TargetFrame.m_HeaderRect.y + 18.0f);
	EXPECT_FLOAT_EQ(VisibleFrame.m_ContentRect.y, TargetFrame.m_ContentRect.y + 18.0f);
}

TEST(SettingsCardInteraction, CollapseAndVisibilityChangesSnapWithoutDisablingDragReflow)
{
	EXPECT_FALSE(SettingsCardDeckContentHeightChanged(-1.0f, 96.0f));
	EXPECT_FALSE(SettingsCardDeckContentHeightChanged(96.0f, 96.005f));
	EXPECT_TRUE(SettingsCardDeckContentHeightChanged(96.0f, 120.0f));
	EXPECT_TRUE(SettingsCardDeckShouldSnapReflow(true, false));
	EXPECT_FALSE(SettingsCardDeckShouldSnapReflow(false, false));
	EXPECT_FALSE(SettingsCardDeckShouldSnapReflow(true, true));
}

TEST(SettingsCardInteraction, SubtitleVisibilityLatchesOnlyWhileCardIsMoving)
{
	EXPECT_TRUE(ResolveSettingsCardSubtitleMotionLatch(true, true, false, false));
	EXPECT_FALSE(ResolveSettingsCardSubtitleMotionLatch(false, true, false, false));
	EXPECT_TRUE(ResolveSettingsCardSubtitleMotionLatch(false, true, true, true));
	EXPECT_FALSE(ResolveSettingsCardSubtitleMotionLatch(false, true, true, false));
	EXPECT_FALSE(ResolveSettingsCardSubtitleMotionLatch(false, false, true, true));
}

TEST(SettingsCardInteraction, HoverOnlyRevealsSubtitleWithoutChangingCardChrome)
{
	const ColorRGBA BaseSurface(0.12f, 0.24f, 0.36f, 0.48f);
	SSettingsCardVisualState Resting;
	SSettingsCardVisualState Hovered = Resting;
	Hovered.m_Hovered = true;

	EXPECT_TRUE(SettingsCardSubtitleVisible(Hovered.m_Hovered, false, false));
	EXPECT_FALSE(SettingsCardInteractionBorderVisible(Hovered));

	const ColorRGBA RestingSurface = ResolveSettingsCardSurfaceColor(BaseSurface, Resting);
	const ColorRGBA HoveredSurface = ResolveSettingsCardSurfaceColor(BaseSurface, Hovered);
	EXPECT_FLOAT_EQ(RestingSurface.r, HoveredSurface.r);
	EXPECT_FLOAT_EQ(RestingSurface.g, HoveredSurface.g);
	EXPECT_FLOAT_EQ(RestingSurface.b, HoveredSurface.b);
	EXPECT_FLOAT_EQ(RestingSurface.a, HoveredSurface.a);
}

TEST(SettingsCardInteraction, PreLayoutContentInputRequiresPointerOrPendingInputOrActivePointerContinuation)
{
	EXPECT_TRUE(SettingsCardDeckShouldRunPreLayoutInput(true, false, false, true, false, 1.0f));
	EXPECT_TRUE(SettingsCardDeckShouldRunPreLayoutInput(false, true, false, true, false, 1.0f));
	EXPECT_TRUE(SettingsCardDeckShouldRunPreLayoutInput(false, false, true, false, false, 1.0f));
	EXPECT_FALSE(SettingsCardDeckShouldRunPreLayoutInput(false, false, false, true, false, 1.0f));
	EXPECT_FALSE(SettingsCardDeckShouldRunPreLayoutInput(true, false, false, false, false, 1.0f));
	EXPECT_FALSE(SettingsCardDeckShouldRunPreLayoutInput(true, false, false, true, true, 1.0f));
	EXPECT_FALSE(SettingsCardDeckShouldRunPreLayoutInput(true, false, false, true, false, 0.0f));
}

TEST(SettingsCardInteraction, ActiveItemContinuationRequiresPointerInput)
{
	EXPECT_TRUE(SettingsCardDeckHasActiveItemContinuation(true, true));
	EXPECT_FALSE(SettingsCardDeckHasActiveItemContinuation(true, false));
	EXPECT_FALSE(SettingsCardDeckHasActiveItemContinuation(false, true));
	EXPECT_FALSE(SettingsCardDeckHasActiveItemContinuation(false, false));
}

TEST(SettingsCardInteraction, SubtitleVisibilityUsesCurrentPointerMotionLatchAndFocus)
{
	EXPECT_TRUE(SettingsCardSubtitleVisible(true, false, false));
	EXPECT_TRUE(SettingsCardSubtitleVisible(false, true, false));
	EXPECT_TRUE(SettingsCardSubtitleVisible(false, false, true));
	EXPECT_FALSE(SettingsCardSubtitleVisible(false, false, false));
}
