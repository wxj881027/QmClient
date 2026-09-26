// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "qm_anim_test_helpers.h"
#include "test.h"

#include <engine/shared/config.h>

#include <game/client/QmUi/QmAnim.h>
#include <game/client/QmUi/QmAnimCurves.h>
#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmDropdown.h>
#include <game/client/QmUi/QmIslandNotice.h>
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

#include <array>
#include <cmath>

namespace
{
	void AdvanceFor(CUiV2AnimationRuntime &Runtime, float Seconds)
	{
		AdvanceQmAnimFor(Runtime, Seconds);
	}

} // namespace

namespace
{
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

TEST(UiRect, NestedZeroClipCannotExpand)
{
	const CUIRect RenderOnlyClip{100.0f, 100.0f, 0.0f, 0.0f};
	const CUIRect NestedContent{10.0f, 10.0f, 200.0f, 200.0f};
	const CUIRect Intersection = NestedContent.Intersection(RenderOnlyClip);
	EXPECT_FLOAT_EQ(Intersection.x, 100.0f);
	EXPECT_FLOAT_EQ(Intersection.y, 100.0f);
	EXPECT_FLOAT_EQ(Intersection.w, 0.0f);
	EXPECT_FLOAT_EQ(Intersection.h, 0.0f);
}

TEST(QmIslandNotice, FirstVisibleFrameStartsAtHiddenBallAndGatesExpansion)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2AnimationRuntime Runtime;
	qm_island::SNoticeState State;
	const auto First = qm_island::ResolveSprings(Runtime, 801, 802, State, true);
	EXPECT_FLOAT_EQ(First.m_DropProgress, 0.0f);
	EXPECT_FLOAT_EQ(First.m_ExpandProgress, 0.0f);
	EXPECT_TRUE(Runtime.HasActiveAnimation(801, EUiAnimProperty::ALPHA));
	EXPECT_FALSE(qm_island::AdvanceCountdown(State, true, 0.1f));
	EXPECT_FLOAT_EQ(State.m_ElapsedSeconds, 0.0f);

	Runtime.Advance(1.0f / 60.0f);
	const auto Dropping = qm_island::ResolveSprings(Runtime, 801, 802, State, true);
	EXPECT_GT(Dropping.m_DropProgress, 0.0f);
	EXPECT_LT(Dropping.m_DropProgress, 1.0f);
	EXPECT_FLOAT_EQ(Dropping.m_ExpandProgress, 0.0f);
}

TEST(QmIslandNotice, CountdownResumesAfterInterruptionAndResetClearsElapsedTime)
{
	qm_island::SNoticeState State;
	State.m_DurationSeconds = 2.0f;
	State.m_ExpandProgress = 1.0f;
	EXPECT_FALSE(qm_island::AdvanceCountdown(State, true, 0.5f));
	EXPECT_FLOAT_EQ(qm_island::RemainingFraction(State), 0.75f);
	EXPECT_FALSE(qm_island::AdvanceCountdown(State, false, 1.0f));
	EXPECT_FLOAT_EQ(State.m_ElapsedSeconds, 0.5f);
	EXPECT_TRUE(qm_island::AdvanceCountdown(State, true, 1.5f));
	qm_island::Reset(State);
	EXPECT_FLOAT_EQ(qm_island::RemainingFraction(State), 1.0f);
	EXPECT_FALSE(qm_island::NeedsRender(State, false));
}

TEST(QmIslandSurface, RoundedRectanglePerimeterVisitsStraightSidesAndCornersClockwise)
{
	const CUIRect Rect{10.0f, 20.0f, 100.0f, 60.0f};
	constexpr float Pi = 3.14159265359f;
	const float QuarterArc = 5.0f * Pi;
	const float Perimeter = qm_island::RoundedRectPerimeterLength(Rect, 10.0f);
	EXPECT_NEAR(Perimeter, 240.0f + 20.0f * Pi, 0.0001f);

	struct SPointAtDistance
	{
		float m_Distance;
		vec2 m_Position;
	};
	const std::array<SPointAtDistance, 11> aPoints{{
		{0.0f, {60.0f, 20.0f}},
		{40.0f, {100.0f, 20.0f}},
		{40.0f + QuarterArc, {110.0f, 30.0f}},
		{40.0f + QuarterArc + 20.0f, {110.0f, 50.0f}},
		{80.0f + QuarterArc, {110.0f, 70.0f}},
		{80.0f + 2.0f * QuarterArc, {100.0f, 80.0f}},
		{Perimeter * 0.5f, {60.0f, 80.0f}},
		{160.0f + 2.0f * QuarterArc, {20.0f, 80.0f}},
		{160.0f + 3.0f * QuarterArc, {10.0f, 70.0f}},
		{180.0f + 3.0f * QuarterArc, {10.0f, 50.0f}},
		{200.0f + 3.0f * QuarterArc, {10.0f, 30.0f}},
	}};
	for(const auto &Point : aPoints)
	{
		SCOPED_TRACE(Point.m_Distance);
		const vec2 Actual = qm_island::RoundedRectPerimeterPoint(Rect, 10.0f, Point.m_Distance / Perimeter);
		EXPECT_NEAR(Actual.x, Point.m_Position.x, 0.0001f);
		EXPECT_NEAR(Actual.y, Point.m_Position.y, 0.0001f);
	}
	const vec2 LastCorner = qm_island::RoundedRectPerimeterPoint(Rect, 10.0f, (200.0f + 4.0f * QuarterArc) / Perimeter);
	EXPECT_NEAR(LastCorner.x, 20.0f, 0.0001f);
	EXPECT_NEAR(LastCorner.y, 20.0f, 0.0001f);
	EXPECT_EQ(qm_island::RoundedRectPerimeterPoint(Rect, 10.0f, 1.0f), qm_island::RoundedRectPerimeterPoint(Rect, 10.0f, 0.0f));
}

TEST(QmIslandSurface, CapsuleAndSquareCornersKeepTheSameStartAndDirection)
{
	const CUIRect Capsule{10.0f, 20.0f, 100.0f, 20.0f};
	const float CapsulePerimeter = qm_island::RoundedRectPerimeterLength(Capsule, 10.0f);
	EXPECT_NEAR(CapsulePerimeter, 160.0f + 20.0f * 3.14159265359f, 0.0001f);
	EXPECT_EQ(qm_island::RoundedRectPerimeterPoint(Capsule, 10.0f, 0.0f), vec2(60.0f, 20.0f));
	const vec2 CapsuleBottom = qm_island::RoundedRectPerimeterPoint(Capsule, 10.0f, 0.5f);
	EXPECT_NEAR(CapsuleBottom.x, 60.0f, 0.0001f);
	EXPECT_NEAR(CapsuleBottom.y, 40.0f, 0.0001f);

	const CUIRect SquareCorners{10.0f, 20.0f, 100.0f, 60.0f};
	const float Perimeter = qm_island::RoundedRectPerimeterLength(SquareCorners, 0.0f);
	EXPECT_FLOAT_EQ(Perimeter, 320.0f);
	EXPECT_EQ(qm_island::RoundedRectPerimeterPoint(SquareCorners, 0.0f, 0.0f), vec2(60.0f, 20.0f));
	EXPECT_EQ(qm_island::RoundedRectPerimeterPoint(SquareCorners, 0.0f, 80.0f / Perimeter), vec2(110.0f, 50.0f));
	EXPECT_EQ(qm_island::RoundedRectPerimeterPoint(SquareCorners, 0.0f, 0.5f), vec2(60.0f, 80.0f));
	EXPECT_EQ(qm_island::RoundedRectPerimeterPoint(SquareCorners, 0.0f, 240.0f / Perimeter), vec2(10.0f, 50.0f));
}

TEST(SettingsCard, CanonicalRectOwnsDisplayHitDragAndProxyGeometry)
{
	SSettingsCardSpec Spec;
	Spec.m_pStableId = "deck:graphics-display";
	Spec.m_pTitle = "Graphics display";
	Spec.m_pSubtitle = "Window and monitor";
	const SSettingsCardFrame Frame = BuildSettingsCardFrame({10.0f, 20.0f, 400.0f, 0.0f}, Spec, 180.0f, 1.0f);
	EXPECT_EQ(&Frame.DisplayRect(), &Frame.HitRect());
	EXPECT_EQ(&Frame.DisplayRect(), &Frame.DragRect());
	EXPECT_EQ(&Frame.DisplayRect(), &Frame.ProxySourceRect());
	EXPECT_GE(Frame.m_ContentRect.x, Frame.m_Rect.x);
	EXPECT_GE(Frame.m_ContentRect.y, Frame.m_Rect.y);
	EXPECT_LE(Frame.m_ContentRect.x + Frame.m_ContentRect.w, Frame.m_Rect.x + Frame.m_Rect.w);
	EXPECT_LE(Frame.m_ContentRect.y + Frame.m_ContentRect.h, Frame.m_Rect.y + Frame.m_Rect.h);
	EXPECT_GT(Frame.m_SubtitleRect.h, 0.0f);
}

TEST(SettingsCard, MotionPolicyKeepsRequiredFeedbackAtLevelZero)
{
	const SCardMotionSpec Full = ResolveCardMotionSpec(2, true, true, true, true);
	const SCardMotionSpec Reduced = ResolveCardMotionSpec(1, true, true, true, true);
	const SCardMotionSpec Off = ResolveCardMotionSpec(0, true, true, true, true);
	EXPECT_GT(Full.m_EntryDistance, Reduced.m_EntryDistance);
	EXPECT_FLOAT_EQ(Full.m_EntryDuration, 0.16f);
	EXPECT_FLOAT_EQ(Full.m_ContentHeightDuration, 0.18f);
	EXPECT_FLOAT_EQ(Reduced.m_ReflowDuration, 0.12f);
	EXPECT_TRUE(Full.m_DecorativeMotion);
	EXPECT_FALSE(ResolveCardMotionSpec(2, true, true, true, false).m_DecorativeMotion);
	EXPECT_FLOAT_EQ(ResolveCardMotionSpec(2, false, true, true, true).m_EntryDuration, 0.0f);
	EXPECT_FLOAT_EQ(ResolveCardMotionSpec(2, true, false, true, true).m_ContentHeightDuration, 0.0f);
	EXPECT_FLOAT_EQ(ResolveCardMotionSpec(2, true, true, false, true).m_ReflowDuration, 0.0f);
	EXPECT_FLOAT_EQ(Off.m_EntryDistance, 0.0f);
	EXPECT_FLOAT_EQ(Off.m_EntryDuration, 0.0f);
	EXPECT_FLOAT_EQ(Off.m_ContentHeightDuration, 0.0f);
	EXPECT_FLOAT_EQ(Off.m_ReflowDuration, 0.0f);
	EXPECT_GT(Off.m_DropFeedbackDuration, 0.0f);
	EXPECT_GT(Off.m_ReflowCompleteFeedbackDuration, 0.0f);
	EXPECT_FLOAT_EQ(ResolveCardMotionSpec(-1, true, true, true, true).m_EntryDistance, 0.0f);
	EXPECT_FLOAT_EQ(ResolveCardMotionSpec(99, true, true, true, true).m_EntryDistance, Full.m_EntryDistance);
	EXPECT_TRUE(Off.m_KeepDragProxy);
	EXPECT_TRUE(Off.m_KeepDropFeedback);
	EXPECT_TRUE(Off.m_KeepReflowCompleteFeedback);
}

TEST(SettingsPageLayout, WideViewportUsesEqualColumnsBelowFullWidthTabs)
{
	const SSettingsPageLayoutFrame Frame = ResolveSettingsPageLayout({0.0f, 0.0f, 1000.0f, 700.0f}, true, 1.0f);
	EXPECT_TRUE(Frame.m_TwoColumns);
	EXPECT_FLOAT_EQ(Frame.m_SubTabRect.w, Frame.m_PageRect.w);
	EXPECT_LT(Frame.m_SubTabRect.y, Frame.m_aColumns[0].y);
	EXPECT_FLOAT_EQ(Frame.m_aColumns[0].w, Frame.m_aColumns[1].w);
	EXPECT_GT(Frame.m_aColumns[1].x, Frame.m_aColumns[0].x + Frame.m_aColumns[0].w);
}

TEST(SettingsPageLayout, SharedSubTabsUseOneScaledHeightAndGapContract)
{
	const SSettingsSubTabLayoutFrame Standard = ResolveSettingsSubTabLayout({10.0f, 20.0f, 800.0f, 600.0f}, 1.0f);
	EXPECT_FLOAT_EQ(Standard.m_TabBarRect.h, 26.0f);
	EXPECT_FLOAT_EQ(Standard.m_ContentRect.y, 56.0f);
	EXPECT_FLOAT_EQ(Standard.m_ContentRect.h, 564.0f);

	const SSettingsSubTabLayoutFrame Compact = ResolveSettingsSubTabLayout({10.0f, 20.0f, 800.0f, 600.0f}, 0.8f);
	EXPECT_FLOAT_EQ(Compact.m_TabBarRect.h, 20.8f);
	EXPECT_FLOAT_EQ(Compact.m_ContentRect.y, 48.8f);
	EXPECT_FLOAT_EQ(Compact.m_ContentRect.h, 571.2f);
}

TEST(SettingsPageLayout, NarrowViewportUsesOneColumnWithoutPhantomRightColumn)
{
	const SSettingsPageLayoutFrame Frame = ResolveSettingsPageLayout({0.0f, 0.0f, 620.0f, 700.0f}, false, 1.0f);
	EXPECT_FALSE(Frame.m_TwoColumns);
	EXPECT_FLOAT_EQ(Frame.m_aColumns[0].w, Frame.m_ContentViewport.w);
	EXPECT_FLOAT_EQ(Frame.m_aColumns[1].w, 0.0f);
	EXPECT_FLOAT_EQ(Frame.m_SubTabRect.h, 0.0f);
}

TEST(SettingsPageLayout, ScrollViewportReflowsColumnsAroundScrollbar)
{
	const SSettingsPageLayoutFrame Original = ResolveSettingsPageLayout({0.0f, 0.0f, 800.0f, 700.0f}, false, 1.0f);
	ASSERT_TRUE(Original.m_TwoColumns);
	EXPECT_FLOAT_EQ(Original.m_UnreservedScrollViewport.w, Original.m_ScrollViewport.w);
	const SSettingsPageLayoutFrame Shrunk = ResolveSettingsPageLayoutForScrollViewport(Original, {Original.m_ScrollViewport.x, Original.m_ScrollViewport.y, 740.0f, Original.m_ScrollViewport.h}, 1.0f);
	EXPECT_FALSE(Shrunk.m_TwoColumns);
	EXPECT_FLOAT_EQ(Shrunk.m_UnreservedScrollViewport.w, Original.m_UnreservedScrollViewport.w);
	EXPECT_FLOAT_EQ(Shrunk.m_ContentViewport.w, 740.0f);
	EXPECT_FLOAT_EQ(Shrunk.m_aColumns[0].w, 740.0f);
	EXPECT_FLOAT_EQ(Shrunk.m_aColumns[1].w, 0.0f);
}

TEST(SettingsPageLayout, NonPositiveScaleUsesBaseGeometry)
{
	const SSettingsPageLayoutFrame Base = ResolveSettingsPageLayout({0.0f, 0.0f, 620.0f, 700.0f}, false, 1.0f);
	const SSettingsPageLayoutFrame Zero = ResolveSettingsPageLayout({0.0f, 0.0f, 620.0f, 700.0f}, false, 0.0f);
	EXPECT_FLOAT_EQ(Zero.m_CardGap, Base.m_CardGap);
	EXPECT_FLOAT_EQ(Zero.m_ContentViewport.w, Base.m_ContentViewport.w);
	EXPECT_FLOAT_EQ(Zero.m_aColumns[0].w, Base.m_aColumns[0].w);
}

TEST(SettingsPageLayout, ContentMetricsReserveControlWidthWithoutPageMagicNumbers)
{
	const SSettingsContentMetrics Narrow = ResolveSettingsContentMetrics(420.0f);
	const SSettingsContentMetrics Wide = ResolveSettingsContentMetrics(900.0f);
	EXPECT_GE(420.0f - Narrow.m_LabelWidth, 160.0f * Narrow.m_UiScale);
	EXPECT_GE(900.0f - Wide.m_LabelWidth, 160.0f * Wide.m_UiScale);
	EXPECT_LT(Narrow.m_LabelWidth, Wide.m_LabelWidth);
}

TEST(SettingsPageLayout, ContentMetricsShareOneResponsiveScaleContract)
{
	const SSettingsContentMetrics Narrow = ResolveSettingsContentMetrics(640.0f);
	const SSettingsContentMetrics Standard = ResolveSettingsContentMetrics(1000.0f);
	EXPECT_FLOAT_EQ(Narrow.m_BodySize, 10.0f);
	EXPECT_FLOAT_EQ(Narrow.m_LineHeight, 16.0f);
	EXPECT_FLOAT_EQ(Narrow.m_LineSpacing, 3.9f);
	EXPECT_FLOAT_EQ(Narrow.m_CardGap, ui_token::settings::CARD_GAP * Narrow.m_UiScale);
	EXPECT_FLOAT_EQ(Standard.m_BodySize, ui_token::font::BODY);
	EXPECT_FLOAT_EQ(Standard.m_LineHeight, ui_token::settings::ROW_HEIGHT);
	EXPECT_FLOAT_EQ(Standard.m_LineSpacing, ui_token::settings::ROW_GAP);
	EXPECT_FLOAT_EQ(Standard.m_CardGap, ui_token::settings::CARD_GAP);
	EXPECT_FLOAT_EQ(Narrow.m_SmallSize, ResolveSettingsSmallFontSize(Narrow.m_UiScale));
	EXPECT_FLOAT_EQ(Standard.m_SmallSize, ResolveSettingsSmallFontSize(Standard.m_UiScale));
}

TEST(SettingsPageLayout, DisplayCycleStateStartsOncePerVisiblePage)
{
	SSettingsCardDeckDisplayCycleState State;
	EXPECT_TRUE(State.EnterPage(9));
	EXPECT_FALSE(State.EnterPage(9));
	EXPECT_TRUE(State.EnterView((uint64_t)9 << 32 | 1));
	EXPECT_FALSE(State.EnterView((uint64_t)9 << 32 | 1));
	EXPECT_TRUE(State.EnterPage(10));
	EXPECT_FALSE(State.EnterPage(10));
	State.LeaveSettings();
	EXPECT_TRUE(State.EnterPage(10));
}

TEST(SettingsPageLayout, CardDefinitionRevisionChangesOnlyForStructuralInputs)
{
	const uint64_t Stable = ResolveSettingsCardDefinitionsRevision(4, 7, 800.0f, 3);
	EXPECT_EQ(ResolveSettingsCardDefinitionsRevision(4, 7, 800.0f, 3), Stable);
	EXPECT_NE(ResolveSettingsCardDefinitionsRevision(5, 7, 800.0f, 3), Stable);
	EXPECT_NE(ResolveSettingsCardDefinitionsRevision(4, 8, 800.0f, 3), Stable);
	EXPECT_NE(ResolveSettingsCardDefinitionsRevision(4, 7, 801.0f, 3), Stable);
	EXPECT_NE(ResolveSettingsCardDefinitionsRevision(4, 7, 800.0f, 4), Stable);
}

TEST(SettingsPageLayout, CardMetricsUseColumnWidthForTwoColumnControls)
{
	const SSettingsContentMetrics PageMetrics = ResolveSettingsContentMetrics(1600.0f);
	const float CardLabelWidth = ResolveSettingsCardLabelWidth(700.0f, PageMetrics);
	EXPECT_LT(CardLabelWidth, PageMetrics.m_LabelWidth);
	EXPECT_GE(700.0f - CardLabelWidth, 160.0f * PageMetrics.m_UiScale);
}

TEST(SettingsPageLayout, GridHeightUsesOnlyRowsContainingItems)
{
	EXPECT_FLOAT_EQ(ResolveSettingsGridHeight(6, 3, 40.0f, 5.0f), 85.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsGridHeight(7, 3, 40.0f, 5.0f), 130.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsGridHeight(0, 3, 40.0f, 5.0f), 0.0f);
}

TEST(SettingsPageLayout, RowStackDoesNotAddSpacingAfterLastRow)
{
	EXPECT_FLOAT_EQ(ResolveSettingsRowsHeight(6, 20.0f, 5.0f), 145.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsRowsHeight(1, 20.0f, 5.0f), 20.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsRowsHeight(0, 20.0f, 5.0f), 0.0f);
}

TEST(SettingsPageLayout, ListViewportShowsOnlyWholeRowsAndCapsAtEight)
{
	EXPECT_FLOAT_EQ(ResolveSettingsListViewportHeight(8, 20.0f, 0.0f), 160.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsListViewportHeight(9, 20.0f, 0.0f), 160.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsListViewportHeight(8, 20.0f, 3.0f), 181.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsListViewportHeight(1, 20.0f, 3.0f), 20.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsListViewportHeight(0, 20.0f, 3.0f), 0.0f);
}

TEST(SettingsPageLayout, PageListCardsUseExactProductionViewports)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(1000.0f);
	const SSettingsListCardGeometry OneLanguage = ResolveSettingsGeneralLanguageListGeometry(1, Metrics);
	const SSettingsListCardGeometry EightLanguages = ResolveSettingsGeneralLanguageListGeometry(8, Metrics);
	const SSettingsListCardGeometry NineLanguages = ResolveSettingsGeneralLanguageListGeometry(9, Metrics);
	EXPECT_EQ(OneLanguage.m_VisibleRows, 1);
	EXPECT_FLOAT_EQ(OneLanguage.m_ContentHeight, Metrics.m_ListRowHeight);
	EXPECT_EQ(EightLanguages.m_VisibleRows, 8);
	EXPECT_FLOAT_EQ(EightLanguages.m_ListViewportHeight, 8.0f * Metrics.m_ListRowHeight);
	EXPECT_FLOAT_EQ(NineLanguages.m_ListViewportHeight, EightLanguages.m_ListViewportHeight);

	const SSettingsListCardGeometry Theme = ResolveSettingsGeneralThemeListGeometry(12, Metrics);
	EXPECT_FLOAT_EQ(Theme.m_ContentHeight, Metrics.m_LineHeight + Metrics.m_LineSpacing + 8.0f * Metrics.m_ListRowHeight);
	EXPECT_FLOAT_EQ(ResolveSettingsGeneralClientContentHeight(Metrics, Theme.m_ContentHeight),
		Metrics.m_LineHeight + Metrics.m_SectionGap +
			ResolveSettingsRowsHeight(2, Metrics.m_LineHeight, Metrics.m_LineSpacing) + Metrics.m_SectionGap +
			ResolveSettingsRowsHeight(2, Metrics.m_ButtonHeight, Metrics.m_LineSpacing) + Metrics.m_SectionGap + Theme.m_ContentHeight);

	SSettingsContentMetrics IndependentSpacingMetrics = Metrics;
	IndependentSpacingMetrics.m_LineSpacing = 3.0f;
	IndependentSpacingMetrics.m_SectionGap = 11.0f;
	EXPECT_FLOAT_EQ(ResolveSettingsGeneralClientContentHeight(IndependentSpacingMetrics, 80.0f),
		3.0f * IndependentSpacingMetrics.m_LineHeight + 2.0f * IndependentSpacingMetrics.m_ButtonHeight +
			2.0f * IndependentSpacingMetrics.m_LineSpacing + 3.0f * IndependentSpacingMetrics.m_SectionGap + 80.0f);

	const SSettingsContentMetrics CardMetrics = ResolveSettingsContentMetrics(480.0f);
	const SSettingsListCardGeometry CompactLanguages = ResolveSettingsGeneralLanguageListGeometry(9, CardMetrics);
	EXPECT_NE(CardMetrics.m_ListRowHeight, Metrics.m_ListRowHeight);
	EXPECT_FLOAT_EQ(CompactLanguages.m_ContentHeight, 8.0f * CardMetrics.m_ListRowHeight);

	const SSettingsListCardGeometry OneMode = ResolveSettingsGraphicsModesGeometry(1, Metrics);
	const SSettingsListCardGeometry ManyModes = ResolveSettingsGraphicsModesGeometry(20, Metrics);
	EXPECT_FLOAT_EQ(OneMode.m_ContentHeight, Metrics.m_RowStep * 2.0f + Metrics.m_ListRowHeight);
	EXPECT_FLOAT_EQ(ManyModes.m_ContentHeight, Metrics.m_RowStep * 2.0f + 8.0f * Metrics.m_ListRowHeight);
	EXPECT_FLOAT_EQ(ManyModes.m_ListViewportHeight, 8.0f * Metrics.m_ListRowHeight);

	const SSettingsListCardGeometry OneAudioPack = ResolveSettingsSoundAudioPackGeometry(1, Metrics);
	const SSettingsListCardGeometry EightAudioPacks = ResolveSettingsSoundAudioPackGeometry(8, Metrics);
	const SSettingsListCardGeometry NineAudioPacks = ResolveSettingsSoundAudioPackGeometry(9, Metrics);
	EXPECT_EQ(OneAudioPack.m_VisibleRows, 1);
	EXPECT_FLOAT_EQ(OneAudioPack.m_ListViewportHeight, Metrics.m_ListRowHeight);
	EXPECT_EQ(EightAudioPacks.m_VisibleRows, 8);
	EXPECT_FLOAT_EQ(EightAudioPacks.m_ListViewportHeight, 8.0f * Metrics.m_ListRowHeight);
	EXPECT_FLOAT_EQ(NineAudioPacks.m_ListViewportHeight, EightAudioPacks.m_ListViewportHeight);
	EXPECT_FLOAT_EQ(OneAudioPack.m_ContentHeight, Metrics.m_LineHeight + Metrics.m_LineSpacing + OneAudioPack.m_ListViewportHeight + 16.0f);
	EXPECT_NE(ResolveSettingsSoundLayoutRevision(false, true, 7), ResolveSettingsSoundLayoutRevision(false, true, 8));
	EXPECT_EQ(ResolveSettingsSoundLayoutRevision(false, true, 9), ResolveSettingsSoundLayoutRevision(false, true, 10));
}

TEST(SettingsPageLayout, GeneralDefinitionsRevisionTracksDynamicListCounts)
{
	const uint64_t Stable = ResolveSettingsGeneralLayoutRevision(false, 3, 640.0f, 8, 8);
	EXPECT_EQ(ResolveSettingsGeneralLayoutRevision(false, 3, 640.0f, 8, 8), Stable);
	EXPECT_NE(ResolveSettingsGeneralLayoutRevision(false, 3, 640.0f, 9, 8), Stable);
	EXPECT_NE(ResolveSettingsGeneralLayoutRevision(false, 3, 640.0f, 8, 9), Stable);
	EXPECT_NE(ResolveSettingsGeneralLayoutRevision(true, 3, 640.0f, 8, 8), Stable);
	EXPECT_NE(ResolveSettingsGeneralLayoutRevision(false, 4, 640.0f, 8, 8), Stable);
	EXPECT_NE(ResolveSettingsGeneralLayoutRevision(false, 3, 641.0f, 8, 8), Stable);
}

TEST(SettingsPageLayout, CustomSelectionNeverFallsBackToTheFirstSupportedItem)
{
	EXPECT_EQ(ResolveSettingsSelectionWithCustomFallback(1, 3), 1);
	EXPECT_EQ(ResolveSettingsSelectionWithCustomFallback(-1, 3), 3);
	EXPECT_EQ(ResolveSettingsSelectionWithCustomFallback(9, 3), 3);
	EXPECT_EQ(ResolveSettingsSelectionWithCustomFallback(-1, 0), 0);
}

TEST(SettingsPageLayout, ControllerRadioRowAddsASecondLineOnlyWhenWidthRequiresIt)
{
	const SSettingsContentMetrics WideMetrics = ResolveSettingsContentMetrics(700.0f);
	const SSettingsRadioRowLayout Wide = ResolveSettingsRadioRowLayout({0.0f, 0.0f, 700.0f, 100.0f}, 2, WideMetrics);
	EXPECT_FALSE(Wide.m_Stacked);
	EXPECT_FLOAT_EQ(Wide.m_Height, WideMetrics.m_LineHeight);

	const SSettingsContentMetrics NarrowMetrics = ResolveSettingsContentMetrics(240.0f);
	const SSettingsRadioRowLayout Narrow = ResolveSettingsRadioRowLayout({0.0f, 0.0f, 240.0f, 100.0f}, 2, NarrowMetrics);
	EXPECT_TRUE(Narrow.m_Stacked);
	EXPECT_FLOAT_EQ(Narrow.m_Height, NarrowMetrics.m_LineHeight + NarrowMetrics.m_LineSpacing + NarrowMetrics.m_ButtonHeight);
}

TEST(SettingsPageLayout, ControllerHeightTracksStateWidthAndAxisLimit)
{
	constexpr float RowHeight = 20.0f;
	constexpr float RowSpacing = 5.0f;
	const float Disabled = ResolveSettingsControllerContentHeight(700.0f, false, false, false, 0, 8, RowHeight, RowSpacing);
	const float MissingDevice = ResolveSettingsControllerContentHeight(700.0f, true, false, false, 0, 8, RowHeight, RowSpacing);
	const float Relative = ResolveSettingsControllerContentHeight(700.0f, true, true, false, 4, 8, RowHeight, RowSpacing);
	const float Absolute = ResolveSettingsControllerContentHeight(700.0f, true, true, true, 4, 8, RowHeight, RowSpacing);
	const float Narrow = ResolveSettingsControllerContentHeight(240.0f, true, true, false, 4, 8, RowHeight, RowSpacing);
	const float ClampedAxes = ResolveSettingsControllerContentHeight(700.0f, true, true, false, 99, 8, RowHeight, RowSpacing);

	EXPECT_LT(Disabled, MissingDevice);
	EXPECT_LT(MissingDevice, Absolute);
	EXPECT_FLOAT_EQ(Relative - Absolute, RowHeight + RowSpacing);
	EXPECT_GT(Narrow, Relative);
	EXPECT_FLOAT_EQ(ClampedAxes - Relative, 4.0f * (RowHeight + RowSpacing));
}

TEST(SettingsPageLayout, ControllerHeightMatchesEveryRenderedRowWithoutTrailingSpacing)
{
	constexpr float RowHeight = 20.0f;
	constexpr float RowSpacing = 5.0f;
	constexpr float RowStep = RowHeight + RowSpacing;
	const SSettingsContentMetrics WideMetrics = ResolveSettingsContentMetrics(700.0f);
	const SSettingsRadioRowLayout WideRadio = ResolveSettingsRadioRowLayout({0.0f, 0.0f, 700.0f, 100.0f}, 2, WideMetrics);
	const float ExpectedRelative =
		2.0f * RowStep + WideRadio.m_Height + RowStep + 2.0f * RowStep + RowSpacing + 5.0f * RowStep;
	EXPECT_FLOAT_EQ(ResolveSettingsControllerContentHeight(700.0f, true, true, false, 4, 8, RowHeight, RowSpacing), ExpectedRelative);
	EXPECT_FLOAT_EQ(ResolveSettingsControllerContentHeight(700.0f, true, true, true, 4, 8, RowHeight, RowSpacing), ExpectedRelative - RowStep);
	EXPECT_FLOAT_EQ(ResolveSettingsControllerContentHeight(700.0f, false, false, false, 0, 8, RowHeight, RowSpacing), RowStep);
	EXPECT_FLOAT_EQ(ResolveSettingsControllerContentHeight(700.0f, true, false, false, 0, 8, RowHeight, RowSpacing), 2.0f * RowStep + RowSpacing);

	const SSettingsContentMetrics NarrowMetrics = ResolveSettingsContentMetrics(240.0f);
	const SSettingsRadioRowLayout NarrowRadio = ResolveSettingsRadioRowLayout({0.0f, 0.0f, 240.0f, 100.0f}, 2, NarrowMetrics);
	const float ExpectedNarrow =
		2.0f * RowStep + NarrowRadio.m_Height + RowStep + 2.0f * RowStep + RowSpacing + 5.0f * RowStep;
	EXPECT_FLOAT_EQ(ResolveSettingsControllerContentHeight(240.0f, true, true, false, 4, 8, RowHeight, RowSpacing), ExpectedNarrow);

	for(int AxisCount = 0; AxisCount <= 12; ++AxisCount)
	{
		const int VisibleAxes = std::clamp(AxisCount, 0, 8);
		EXPECT_FLOAT_EQ(ResolveSettingsControllerAxisPickerHeight(AxisCount, 8, RowHeight, RowSpacing), (VisibleAxes + 1) * RowStep);
		const float Expected = 2.0f * RowStep + WideRadio.m_Height + RowStep + 2.0f * RowStep + RowSpacing + (VisibleAxes + 1) * RowStep;
		EXPECT_FLOAT_EQ(ResolveSettingsControllerContentHeight(700.0f, true, true, false, AxisCount, 8, RowHeight, RowSpacing), Expected);
	}
}

TEST(SettingsPageLayout, StatusCodeHelpUsesTwoColumnsOnlyWhenWide)
{
	EXPECT_EQ(ResolveSettingsStatusCodeRows(20, 700.0f), 10);
	EXPECT_EQ(ResolveSettingsStatusCodeRows(20, 360.0f), 20);
	EXPECT_EQ(ResolveSettingsStatusCodeRows(19, 700.0f), 10);
	EXPECT_EQ(ResolveSettingsStatusCodeRows(0, 700.0f), 0);
}

TEST(SettingsPageLayout, SavedProfilesHeightTracksRowsAndCapsTheViewport)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(1000.0f);
	const float EmptyHeight = ResolveSettingsProfilesListHeight(Metrics, 500.0f, 0);
	const float OneProfileHeight = ResolveSettingsProfilesListHeight(Metrics, 500.0f, 1);
	const float ThreeRowsHeight = ResolveSettingsProfilesListHeight(Metrics, 500.0f, 5);
	const float CappedHeight = ResolveSettingsProfilesListHeight(Metrics, 500.0f, 50);

	EXPECT_FLOAT_EQ(EmptyHeight, Metrics.m_ButtonHeight + Metrics.m_LineSpacing + Metrics.m_ListRowHeight * 2.0f);
	EXPECT_GT(OneProfileHeight, EmptyHeight);
	EXPECT_GT(ThreeRowsHeight, OneProfileHeight);
	EXPECT_FLOAT_EQ(CappedHeight, ThreeRowsHeight);
	EXPECT_GT(ResolveSettingsProfilesListHeight(Metrics, 300.0f, 4), ResolveSettingsProfilesListHeight(Metrics, 900.0f, 4));
}

TEST(SettingsPageLayout, TeeQueueListViewportUsesCompleteRowsAndPrioritizesQueueSpace)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(1000.0f);
	const float FixedChromeHeight = Metrics.m_LineSpacing * 5.0f + Metrics.m_LineHeight + Metrics.m_ButtonHeight;
	const float PresetRowSpacing = Metrics.m_LineSpacing * 0.5f;
	EXPECT_FLOAT_EQ(ResolveSettingsTeeQueuePresetHeight(Metrics, 3), FixedChromeHeight + ResolveSettingsListViewportHeight(3, Metrics.m_ListRowHeight, PresetRowSpacing));
	EXPECT_FLOAT_EQ(ResolveSettingsTeeQueuePresetHeight(Metrics, 6), FixedChromeHeight + ResolveSettingsListViewportHeight(6, Metrics.m_ListRowHeight, PresetRowSpacing));
	EXPECT_EQ(ResolveSettingsTeeVisiblePresetRows(0), 2);
	EXPECT_EQ(ResolveSettingsTeeVisiblePresetRows(3), 3);
	EXPECT_EQ(ResolveSettingsTeeVisiblePresetRows(12), 3);
	EXPECT_EQ(ResolveSettingsTeeVisibleQueueRows(1), 1);
	EXPECT_EQ(ResolveSettingsTeeVisibleQueueRows(8), 8);
	EXPECT_EQ(ResolveSettingsTeeVisibleQueueRows(9), 8);
	EXPECT_EQ(ResolveSettingsTeeVisibleQueueRows(10), 8);
	const SSettingsTeeQueuePanelGeometry OneQueueItem = ResolveSettingsTeeQueuePanelGeometry(Metrics, 1, 12);
	const SSettingsTeeQueuePanelGeometry EightQueueItems = ResolveSettingsTeeQueuePanelGeometry(Metrics, 8, 12);
	const SSettingsTeeQueuePanelGeometry NineQueueItems = ResolveSettingsTeeQueuePanelGeometry(Metrics, 9, 12);
	EXPECT_FLOAT_EQ(OneQueueItem.m_QueueListViewportHeight, Metrics.m_ListRowHeight);
	EXPECT_FLOAT_EQ(EightQueueItems.m_QueueListViewportHeight, Metrics.m_ListRowHeight * 8.0f);
	EXPECT_FLOAT_EQ(NineQueueItems.m_QueueListViewportHeight, EightQueueItems.m_QueueListViewportHeight);
	EXPECT_FLOAT_EQ(EightQueueItems.m_QueueListSurfaceHeight, Metrics.m_LineHeight + Metrics.m_LineSpacing * 3.0f + EightQueueItems.m_QueueListViewportHeight);
	EXPECT_EQ(EightQueueItems.m_VisiblePresetRows, 3);
	const float StackedIntervalHeight = Metrics.m_LineHeight + Metrics.m_LineSpacing + Metrics.m_InputHeight;
	EXPECT_GE(EightQueueItems.m_ContentHeight, Metrics.m_LineSpacing * 5.0f + Metrics.m_LineHeight + StackedIntervalHeight + EightQueueItems.m_QueueListSurfaceHeight + EightQueueItems.m_QueuePresetHeight);
	EXPECT_NE(ResolveSettingsTeeQueueLayoutRevision(false, false, false, 7, 3), ResolveSettingsTeeQueueLayoutRevision(false, false, false, 8, 3));
	EXPECT_EQ(ResolveSettingsTeeQueueLayoutRevision(false, false, false, 9, 3), ResolveSettingsTeeQueueLayoutRevision(false, false, false, 10, 4));
}

TEST(SettingsPageLayout, TeeIdentityPreviewReservesSemanticHeight)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(1000.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsTeeIdentityHeight(Metrics), Metrics.m_InputHeight + Metrics.m_LineSpacing + Metrics.m_LineHeight * 2.0f + Metrics.m_ButtonHeight * 4.0f);
}

TEST(SettingsPageLayout, TeeCustomColorsUseTwoStackedFullWidthGroups)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(500.0f);
	const CUIRect View{10.0f, 20.0f, 300.0f, 600.0f};
	const SSettingsTeeCustomColorsLayout Layout = ResolveSettingsTeeCustomColorsLayout(View, true, Metrics);

	EXPECT_FLOAT_EQ(Layout.m_BodyGroup.w, View.w);
	EXPECT_FLOAT_EQ(Layout.m_FeetGroup.w, View.w);
	EXPECT_LE(Layout.m_BodyGroup.y + Layout.m_BodyGroup.h, Layout.m_FeetGroup.y);
	EXPECT_FLOAT_EQ(Layout.m_BodyControls.h, ResolveSettingsHslaRowsHeight(Metrics, false));
	EXPECT_FLOAT_EQ(Layout.m_FeetControls.h, ResolveSettingsHslaRowsHeight(Metrics, false));
	EXPECT_FLOAT_EQ(Layout.m_Height, Layout.m_FeetGroup.y + Layout.m_FeetGroup.h - View.y + Metrics.m_LineSpacing);

	const SSettingsTeeCustomColorsLayout Disabled = ResolveSettingsTeeCustomColorsLayout(View, false, Metrics);
	EXPECT_FLOAT_EQ(Disabled.m_Height, Metrics.m_LineSpacing * 2.0f);
	EXPECT_FLOAT_EQ(Disabled.m_BodyGroup.h, 0.0f);
}

TEST(SettingsPageLayout, ColorRowsUseCanonicalControlHeightAndSpacing)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(640.0f);
	const CUIRect View{10.0f, 20.0f, 360.0f, 200.0f};
	const SSettingsColorRowLayout Layout = ResolveSettingsColorRowLayout(View, Metrics, false);

	EXPECT_FLOAT_EQ(Layout.m_RowRect.h, Metrics.m_ButtonHeight);
	EXPECT_FLOAT_EQ(Layout.m_LabelRect.h, Metrics.m_ButtonHeight);
	EXPECT_FLOAT_EQ(Layout.m_ColorButtonRect.h, Metrics.m_ButtonHeight);
	EXPECT_FLOAT_EQ(Layout.m_ResetButtonRect.h, Metrics.m_ButtonHeight);
	EXPECT_FLOAT_EQ(Layout.m_ConsumedHeight, Metrics.m_ButtonHeight + Metrics.m_LineSpacing);
	EXPECT_LE(Layout.m_LabelRect.x + Layout.m_LabelRect.w, Layout.m_ColorButtonRect.x);
	EXPECT_LE(Layout.m_ColorButtonRect.x + Layout.m_ColorButtonRect.w, Layout.m_ResetButtonRect.x);

	const SSettingsColorRowLayout Indented = ResolveSettingsColorRowLayout(View, Metrics, true);
	EXPECT_GT(Indented.m_LabelRect.x, Layout.m_LabelRect.x);
	EXPECT_FLOAT_EQ(Indented.m_ColorButtonRect.h, Layout.m_ColorButtonRect.h);
	EXPECT_FLOAT_EQ(Indented.m_ResetButtonRect.h, Layout.m_ResetButtonRect.h);

	const CUIRect NarrowView{10.0f, 20.0f, 100.0f, 200.0f};
	const SSettingsColorRowLayout Narrow = ResolveSettingsColorRowLayout(NarrowView, Metrics, false);
	EXPECT_GE(Narrow.m_LabelRect.w, 0.0f);
	EXPECT_GE(Narrow.m_ColorButtonRect.w, 0.0f);
	EXPECT_GE(Narrow.m_ResetButtonRect.w, 0.0f);
	EXPECT_GE(Narrow.m_LabelRect.x, NarrowView.x);
	EXPECT_LE(Narrow.m_ResetButtonRect.x + Narrow.m_ResetButtonRect.w, NarrowView.x + NarrowView.w);

	SSettingsContentMetrics NoBottomSpacing = Metrics;
	NoBottomSpacing.m_LineSpacing = 0.0f;
	const SSettingsColorRowLayout NoBottomSpacingLayout = ResolveSettingsColorRowLayout(View, NoBottomSpacing, false);
	EXPECT_FLOAT_EQ(NoBottomSpacingLayout.m_ConsumedHeight, Metrics.m_ButtonHeight);
	EXPECT_LT(NoBottomSpacingLayout.m_LabelRect.x + NoBottomSpacingLayout.m_LabelRect.w, NoBottomSpacingLayout.m_ColorButtonRect.x);
	EXPECT_LT(NoBottomSpacingLayout.m_ColorButtonRect.x + NoBottomSpacingLayout.m_ColorButtonRect.w, NoBottomSpacingLayout.m_ResetButtonRect.x);

	const SSettingsColorRowLayout LastRow = ResolveSettingsColorRowLayout(View, Metrics, false, false);
	EXPECT_FLOAT_EQ(LastRow.m_ConsumedHeight, Metrics.m_ButtonHeight);
}

TEST(SettingsPageLayout, RadioRowsUseMetricsAndStackOnlyWhenRequired)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(640.0f);
	const SSettingsRadioRowLayout Inline = ResolveSettingsRadioRowLayout({0.0f, 0.0f, 600.0f, 100.0f}, 3, Metrics);
	EXPECT_FALSE(Inline.m_Stacked);
	EXPECT_FLOAT_EQ(Inline.m_Height, Metrics.m_ButtonHeight);
	EXPECT_FLOAT_EQ(Inline.m_LabelRect.h, Metrics.m_LineHeight);
	EXPECT_FLOAT_EQ(Inline.m_ButtonsRect.h, Metrics.m_ButtonHeight);

	const SSettingsRadioRowLayout Stacked = ResolveSettingsRadioRowLayout({0.0f, 0.0f, 140.0f, 100.0f}, 3, Metrics);
	EXPECT_TRUE(Stacked.m_Stacked);
	EXPECT_FLOAT_EQ(Stacked.m_Height, Metrics.m_LineHeight + Metrics.m_LineSpacing + Metrics.m_ButtonHeight);
	EXPECT_FLOAT_EQ(Stacked.m_ButtonsRect.y, Metrics.m_LineHeight + Metrics.m_LineSpacing);
}

TEST(SettingsPageLayout, InlineRowMinimumWidthIncludesEveryGap)
{
	EXPECT_FLOAT_EQ(ResolveSettingsInlineRowMinimumWidth(745.0f, 5.0f, 7), 780.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsInlineRowMinimumWidth(745.0f, 3.0f, 7), 766.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsInlineRowMinimumWidth(-1.0f, 5.0f, -1), 0.0f);
}

TEST(SettingsPageLayout, CheckboxBodySizeUsesRowHeightLimit)
{
	EXPECT_FLOAT_EQ(ResolveSettingsCheckboxFontSize(10.0f, 10.0f, 16.0f, 12.0f, 0.8f), 10.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsCheckboxFontSize(12.0f, 12.0f, 20.0f, 16.0f, 0.8f), 12.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsCheckboxFontSize(10.0f, -1.0f, 16.0f, 12.0f, 0.8f), 10.0f);
}

TEST(SettingsPageLayout, AppearanceDynamicCardsMatchConsumedPrimitivesAtBothScales)
{
	const SSettingsContentMetrics Compact = ResolveSettingsContentMetrics(640.0f);
	EXPECT_FLOAT_EQ(Compact.m_UiScale, 0.78f);
	EXPECT_NEAR(ResolveAppearanceChatMessagesHeight(Compact), 278.6f, 0.001f);
	EXPECT_NEAR(ResolveQmHudCoordsHeight(Compact), 155.3f, 0.001f);
	EXPECT_NEAR(ResolveQmHudNotificationsHeight(Compact, false, false), 95.6f, 0.001f);
	EXPECT_NEAR(ResolveQmHudNotificationsHeight(Compact, true, false), 311.4f, 0.001f);
	EXPECT_NEAR(ResolveQmHudNotificationsHeight(Compact, true, true), 391.0f, 0.001f);
	EXPECT_NEAR(ResolveAppearanceLaserColorsHeight(Compact), 319.8f, 0.001f);
	EXPECT_NEAR(ResolveAppearanceLaserEnhancedHeight(Compact, false), 95.6f, 0.001f);
	EXPECT_NEAR(ResolveAppearanceLaserEnhancedHeight(Compact, true), 135.4f, 0.001f);

	const SSettingsContentMetrics Standard = ResolveSettingsContentMetrics(1000.0f);
	EXPECT_FLOAT_EQ(Standard.m_UiScale, 1.0f);
	EXPECT_FLOAT_EQ(ResolveAppearanceChatMessagesHeight(Standard), 350.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudCoordsHeight(Standard), 195.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudNotificationsHeight(Standard, false, false), 120.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudNotificationsHeight(Standard, true, false), 390.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudNotificationsHeight(Standard, true, true), 490.0f);
	EXPECT_FLOAT_EQ(ResolveAppearanceLaserColorsHeight(Standard), 400.0f);
	EXPECT_FLOAT_EQ(ResolveAppearanceLaserEnhancedHeight(Standard, false), 120.0f);
	EXPECT_FLOAT_EQ(ResolveAppearanceLaserEnhancedHeight(Standard, true), 170.0f);
}

TEST(SettingsPageLayout, BothSettingsShellsUseFinalContentWidthMetrics)
{
	const SSettingsContentMetrics LegacyMetrics = ResolveSettingsContentMetrics(640.0f);
	const SSettingsContentMetrics NewMetrics = ResolveSettingsContentMetrics(1000.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsColorRowLayout({0.0f, 0.0f, 640.0f, 100.0f}, LegacyMetrics, false).m_ConsumedHeight, LegacyMetrics.m_RowStep);
	EXPECT_FLOAT_EQ(ResolveSettingsColorRowLayout({0.0f, 0.0f, 1000.0f, 100.0f}, NewMetrics, false).m_ConsumedHeight, NewMetrics.m_RowStep);
}

TEST(SettingsPageLayout, QmHudDynamicCardsMatchEveryVisibleState)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(1000.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudPlayerStatsHeight(Metrics, false, false), 70.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudPlayerStatsHeight(Metrics, true, true), 120.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudPlayerStatsHeight(Metrics, true, false), 245.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudInputOverlayHeight(Metrics, false), 20.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudInputOverlayHeight(Metrics, true), 150.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudNotificationsHeight(Metrics, true, false), 390.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudNotificationsHeight(Metrics, true, true), 490.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudDummyMiniViewHeight(Metrics, false), 25.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudDummyMiniViewHeight(Metrics, true), 121.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudVoiceHeight(Metrics, false, false, false, 0, false, false), 20.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudVoiceHeight(Metrics, true, false, false, 0, false, false), 145.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudVoiceHeight(Metrics, true, true, false, 0, false, false), 450.75f);
	EXPECT_NEAR(ResolveQmHudVoiceHeight(Metrics, true, true, true, 1, true, true), 823.7f, 0.001f);
	EXPECT_FLOAT_EQ(ResolveQmHudBackground3DHeight(Metrics, 600.0f, false, false, false, false, false, false), 20.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudBackground3DHeight(Metrics, 600.0f, true, false, false, false, false, false), 470.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudBackground3DHeight(Metrics, 600.0f, true, true, true, true, true, true), 670.0f);
}

TEST(SettingsPageLayout, QmHudRevisionsCoverEveryHeightBranch)
{
	EXPECT_NE(ResolveQmHudVoiceRevision(true, true, false, 0, false, false), ResolveQmHudVoiceRevision(true, true, true, 0, false, false));
	EXPECT_NE(ResolveQmHudVoiceRevision(true, true, true, 0, false, false), ResolveQmHudVoiceRevision(true, true, true, 1, false, false));
	EXPECT_NE(ResolveQmHudVoiceRevision(true, true, true, 1, false, false), ResolveQmHudVoiceRevision(true, true, true, 1, true, false));
	EXPECT_NE(ResolveQmHudVoiceRevision(true, true, true, 1, true, false), ResolveQmHudVoiceRevision(true, true, true, 1, true, true));
	EXPECT_NE(ResolveQmHudBackground3DRevision(true, false, false, false, false, false), ResolveQmHudBackground3DRevision(true, true, false, false, false, false));
	EXPECT_NE(ResolveQmHudBackground3DRevision(true, true, false, false, false, false), ResolveQmHudBackground3DRevision(true, true, true, true, true, true));
}

TEST(SettingsPageLayout, ConditionalRowsUseFrameSnapshotUntilNextLayout)
{
	bool ReplaysEnabled = true;
	bool RaceGhostEnabled = false;
	const bool ReplaysFrameSnapshot = ReplaysEnabled;
	const bool FrameSnapshot = RaceGhostEnabled;
	EXPECT_FLOAT_EQ(ResolveDDNetDemoRows(ReplaysFrameSnapshot, FrameSnapshot, false), 5.0f);

	ReplaysEnabled = false;
	RaceGhostEnabled = true;
	EXPECT_FLOAT_EQ(ResolveDDNetDemoRows(ReplaysFrameSnapshot, FrameSnapshot, false), 5.0f);
	EXPECT_FLOAT_EQ(ResolveDDNetDemoRows(ReplaysEnabled, RaceGhostEnabled, false), 6.0f);
	EXPECT_FLOAT_EQ(ResolveDDNetDemoRows(ReplaysEnabled, RaceGhostEnabled, true), 7.0f);
	EXPECT_FLOAT_EQ(ResolveDDNetDemoRows(true, RaceGhostEnabled, false), 8.0f);
	EXPECT_FLOAT_EQ(ResolveDDNetDemoRows(true, RaceGhostEnabled, true), 9.0f);
	EXPECT_FLOAT_EQ(ResolveDDNetGameplayRows(false, false), 9.0f);
	EXPECT_FLOAT_EQ(ResolveDDNetGameplayRows(true, false), 9.0f);
	EXPECT_FLOAT_EQ(ResolveDDNetGameplayRows(false, true), 12.0f);
	EXPECT_FLOAT_EQ(ResolveDDNetGameplayRows(true, true), 12.0f);
}

TEST(SettingsPageLayout, SettingsPagesShareTheQmScaleBaseline)
{
	EXPECT_FLOAT_EQ(ResolveSettingsUiScale(800.0f), 0.85f);
	EXPECT_FLOAT_EQ(ResolveSettingsUiScale(1000.0f), 1.0f);
	EXPECT_LT(ResolveSettingsUiScale(680.0f), ResolveSettingsUiScale(681.0f));
	EXPECT_NEAR(ResolveSettingsUiScale(679.0f), ResolveSettingsUiScale(680.0f), 0.01f);
}

TEST(SettingsPageLayout, SettingsShellUsesFluidNarrowAndCappedWideGeometry)
{
	const std::array<float, 6> Widths = {640.0f, 800.0f, 1280.0f, 1920.0f, 2560.0f, 3840.0f};
	for(const float Width : Widths)
	{
		const SSettingsShellLayoutFrame Shell = ResolveSettingsShellLayout({0.0f, 0.0f, Width, 600.0f});
		EXPECT_LE(Shell.m_ContentRect.w, ui_token::settings::MAX_CONTENT_WIDTH);
		EXPECT_FLOAT_EQ(Shell.m_UiScale, ResolveSettingsUiScale(Shell.m_ContentRect.w));
		EXPECT_FLOAT_EQ(Shell.m_ScrollViewport.w, Shell.m_ContentRect.w - 2.0f * ui_token::settings::PAGE_INSET * Shell.m_UiScale - ui_token::settings::OUTER_SCROLLBAR_SLOT);
		if(Width <= 800.0f)
		{
			EXPECT_FLOAT_EQ(Shell.m_ShellRect.w, Width);
			EXPECT_FALSE(Shell.m_TwoColumns);
		}
		else
		{
			EXPECT_FLOAT_EQ(Shell.m_ContentRect.w, ui_token::settings::MAX_CONTENT_WIDTH);
			EXPECT_TRUE(Shell.m_TwoColumns);
			EXPECT_GT(Shell.m_ShellRect.x, 0.0f);
		}
	}
}

TEST(SettingsPageLayout, SettingsShellReservesRestartBarBeforeContent)
{
	const SSettingsShellLayoutFrame Shell = ResolveSettingsShellLayout({0.0f, 10.0f, 1280.0f, 700.0f}, 30.0f);
	EXPECT_FLOAT_EQ(Shell.m_ShellRect.y, 10.0f);
	EXPECT_FLOAT_EQ(Shell.m_ShellRect.h, 670.0f);
	EXPECT_FLOAT_EQ(Shell.m_RestartBarRect.y, 690.0f);
	EXPECT_FLOAT_EQ(Shell.m_RestartBarRect.h, 20.0f);
	EXPECT_FLOAT_EQ(Shell.m_RestartBarRect.x, Shell.m_ContentPanelRect.x);
	EXPECT_FLOAT_EQ(Shell.m_RestartBarRect.w, Shell.m_ContentPanelRect.w);
}

TEST(UiTheme, RuntimeThemeTracksBaseColorAndOpacity)
{
	const SUiTheme Blue = ResolveUiTheme(ColorHSLA(0.60f, 0.75f, 0.45f, 1.0f), 1.0f, ColorHSLA(0.60f, 0.78f, 0.52f, 1.0f), ColorHSLA(0.60f, 0.75f, 0.45f, 1.0f));
	const SUiTheme RedHalf = ResolveUiTheme(ColorHSLA(0.00f, 0.75f, 0.45f, 1.0f), 0.5f, ColorHSLA(0.60f, 0.78f, 0.52f, 1.0f), ColorHSLA(0.00f, 0.75f, 0.45f, 1.0f));
	EXPECT_NE(Blue.m_Accent.r, RedHalf.m_Accent.r);
	EXPECT_NE(Blue.m_Accent.b, RedHalf.m_Accent.b);
	EXPECT_LT(RedHalf.m_Surface.a, Blue.m_Surface.a);
	EXPECT_FLOAT_EQ(RedHalf.m_InputSurface.a, RedHalf.m_Surface.a);
	EXPECT_GE(RedHalf.m_FocusRing.a, 0.60f);
}

TEST(InputField, AffordanceSlotsOnlyExistWhenRequested)
{
	const CUIRect Rect{10.0f, 20.0f, 240.0f, 32.0f};
	const ui_widget::SInputFieldLayout Plain = ui_widget::ResolveInputFieldLayout(Rect, false, false, 1.0f);
	const ui_widget::SInputFieldLayout Both = ui_widget::ResolveInputFieldLayout(Rect, true, true, 1.0f);

	EXPECT_FLOAT_EQ(Plain.m_IconRect.w, 0.0f);
	EXPECT_FLOAT_EQ(Plain.m_ClearRect.w, 0.0f);
	EXPECT_GT(Plain.m_ContentRect.w, Both.m_ContentRect.w);
	EXPECT_GT(Both.m_IconRect.w, 0.0f);
	EXPECT_GT(Both.m_ClearRect.w, 0.0f);
	EXPECT_FLOAT_EQ(Both.m_ShellRect.x, Rect.x);
	EXPECT_FLOAT_EQ(Both.m_ShellRect.w, Rect.w);
}

TEST(InputField, ContentRectStaysInsideOuterShell)
{
	const CUIRect Rect{4.0f, 8.0f, 96.0f, 24.0f};
	const ui_widget::SInputFieldLayout Layout = ui_widget::ResolveInputFieldLayout(Rect, true, true, 1.25f);

	EXPECT_GE(Layout.m_ContentRect.x, Rect.x);
	EXPECT_LE(Layout.m_ContentRect.x + Layout.m_ContentRect.w, Rect.x + Rect.w);
	EXPECT_FLOAT_EQ(Layout.m_ContentRect.y, Rect.y);
	EXPECT_FLOAT_EQ(Layout.m_ContentRect.h, Rect.h);
	EXPECT_LE(Layout.m_IconRect.x + Layout.m_IconRect.w, Layout.m_ContentRect.x);
	EXPECT_GE(Layout.m_ClearRect.x, Layout.m_ContentRect.x + Layout.m_ContentRect.w);
	EXPECT_FLOAT_EQ(Layout.m_ClearRect.x + Layout.m_ClearRect.w, Rect.x + Rect.w);
}

TEST(InputField, TrailingTextStaysInsideSingleShell)
{
	const CUIRect Rect{4.0f, 8.0f, 108.0f, 24.0f};
	const ui_widget::SInputFieldLayout Layout = ui_widget::ResolveInputFieldLayout(Rect, false, false, 1.0f, 34.0f);

	EXPECT_FLOAT_EQ(Layout.m_ShellRect.x, Rect.x);
	EXPECT_FLOAT_EQ(Layout.m_ShellRect.w, Rect.w);
	EXPECT_FLOAT_EQ(Layout.m_TrailingRect.x + Layout.m_TrailingRect.w, Rect.x + Rect.w);
	EXPECT_LE(Layout.m_ContentRect.x + Layout.m_ContentRect.w, Layout.m_TrailingRect.x);
	EXPECT_GE(Layout.m_ContentRect.w, 52.0f);
}

TEST(UiV2AnimSpring, ReplaceInheritsVelocity)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(601, EUiAnimProperty::POS_X, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeSpringRequest(601, EUiAnimProperty::POS_X, 100.0f, 151)));
	AdvanceFor(Runtime, 0.15f);
	const float Before = Runtime.GetValue(601, EUiAnimProperty::POS_X);
	EXPECT_GT(Before, 0.0f);
	EXPECT_LT(Before, 100.0f);

	// REPLACE 打断运行中的弹簧：值连续，且继承当前速度（首帧继续上行，而不是静止重放）。
	EXPECT_TRUE(Runtime.RequestAnimation(MakeSpringRequest(601, EUiAnimProperty::POS_X, 0.0f, 152)));
	EXPECT_NEAR(Before, Runtime.GetValue(601, EUiAnimProperty::POS_X), 1e-3f);

	Runtime.Advance(1.0f / 60.0f);
	const float AfterOneFrame = Runtime.GetValue(601, EUiAnimProperty::POS_X);
	EXPECT_GT(AfterOneFrame, Before);

	AdvanceFor(Runtime, 3.0f);
	EXPECT_NEAR(Runtime.GetValue(601, EUiAnimProperty::POS_X), 0.0f, 0.5f);
	EXPECT_FALSE(Runtime.HasActiveAnimation(601, EUiAnimProperty::POS_X));
}

TEST(UiV2Anim, TweenInterruptTakeoverInheritsVelocity)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(701, EUiAnimProperty::POS_X, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(701, EUiAnimProperty::POS_X, 100.0f, 1.0f, 1, EUiAnimInterruptPolicy::REPLACE, 161)));
	AdvanceFor(Runtime, 0.5f);
	const float Before = Runtime.GetValue(701, EUiAnimProperty::POS_X);
	EXPECT_NEAR(Before, 50.0f, 0.05f);

	// REPLACE 打断线性 tween：转弹簧接管（响应≈0.75s），初速度=线性曲线速度 100/s。
	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(701, EUiAnimProperty::POS_X, 0.0f, 1.0f, 1, EUiAnimInterruptPolicy::REPLACE, 162)));
	EXPECT_NEAR(Before, Runtime.GetValue(701, EUiAnimProperty::POS_X), 1e-3f);

	Runtime.Advance(1.0f / 60.0f);
	const float AfterOneFrame = Runtime.GetValue(701, EUiAnimProperty::POS_X);
	// 继承速度：接管首帧继续上行（冲向原目标方向），而不是从当前值静止重放。
	EXPECT_GT(AfterOneFrame, Before);

	AdvanceFor(Runtime, 2.0f);
	EXPECT_NEAR(Runtime.GetValue(701, EUiAnimProperty::POS_X), 0.0f, 0.05f);
	EXPECT_FALSE(Runtime.HasActiveAnimation(701, EUiAnimProperty::POS_X));
}

TEST(UiV2Anim, SameValueReplaceStopsRunningTrackImmediately)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(703, EUiAnimProperty::POS_X, 0.0f);
	ASSERT_TRUE(Runtime.RequestAnimation(MakeRequest(703, EUiAnimProperty::POS_X, 10.0f, 1.0f, 1, EUiAnimInterruptPolicy::REPLACE, 173)));
	AdvanceFor(Runtime, 0.2f);
	const float Current = Runtime.GetValue(703, EUiAnimProperty::POS_X);
	ASSERT_GT(Current, 0.0f);
	ASSERT_LT(Current, 10.0f);
	ASSERT_TRUE(Runtime.HasActiveAnimation(703, EUiAnimProperty::POS_X));

	EXPECT_FALSE(Runtime.RequestAnimation(MakeRequest(703, EUiAnimProperty::POS_X, Current, 0.4f, 2, EUiAnimInterruptPolicy::REPLACE, 174)));
	EXPECT_FALSE(Runtime.HasActiveAnimation(703, EUiAnimProperty::POS_X));
	EXPECT_EQ(Runtime.ActiveTrackCount(), 0);
	EXPECT_FLOAT_EQ(Runtime.GetValue(703, EUiAnimProperty::POS_X), Current);

	SUiAnimCompleteEvent Event;
	ASSERT_TRUE(Runtime.PollCompletedEvent(Event));
	EXPECT_EQ(Event.m_TrackId, 174u);
	EXPECT_FALSE(Runtime.PollCompletedEvent(Event));
	AdvanceFor(Runtime, 1.0f);
	EXPECT_FALSE(Runtime.HasActiveAnimation(703, EUiAnimProperty::POS_X));
	EXPECT_FLOAT_EQ(Runtime.GetValue(703, EUiAnimProperty::POS_X), Current);
	EXPECT_FALSE(Runtime.PollCompletedEvent(Event));
}

TEST(UiV2Anim, MergeTargetInstantRequestStillSnaps)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(702, EUiAnimProperty::POS_X, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeSpringRequest(702, EUiAnimProperty::POS_X, 100.0f, 171)));
	AdvanceFor(Runtime, 0.1f);
	EXPECT_TRUE(Runtime.HasActiveAnimation(702, EUiAnimProperty::POS_X));

	// 0 时长 tween 是显式瞬移：MERGE_TARGET 直接到位，不转弹簧接管。
	SUiAnimRequest Snap = MakeRequest(702, EUiAnimProperty::POS_X, 50.0f, 0.0f, 5, EUiAnimInterruptPolicy::MERGE_TARGET, 172);
	EXPECT_TRUE(Runtime.RequestAnimation(Snap));
	EXPECT_NEAR(Runtime.GetValue(702, EUiAnimProperty::POS_X), 50.0f, 1e-6f);
	EXPECT_FALSE(Runtime.HasActiveAnimation(702, EUiAnimProperty::POS_X));
}

TEST(UiV2AnimSpring, ResolveSpringValueUsesRuntimeSpringTrack)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(401, EUiAnimProperty::POS_X, 0.0f);

	SUiSpringConfig Spring;
	Spring.m_Stiffness = 280.0f;
	Spring.m_Damping = 18.0f;
	Spring.m_RestEpsilon = 0.01f;
	Spring.m_RestVelocity = 0.05f;

	EXPECT_NEAR(ResolveUiAnimSpringValue(Runtime, 401, EUiAnimProperty::POS_X, 100.0f, Spring), 0.0f, 1e-6f);
	EXPECT_TRUE(Runtime.HasActiveAnimation(401, EUiAnimProperty::POS_X));

	AdvanceFor(Runtime, 0.15f);
	const float BeforeMerge = Runtime.GetValue(401, EUiAnimProperty::POS_X);
	EXPECT_GT(BeforeMerge, 0.0f);
	EXPECT_TRUE(Runtime.HasActiveAnimation(401, EUiAnimProperty::POS_X));

	EXPECT_NEAR(ResolveUiAnimSpringValue(Runtime, 401, EUiAnimProperty::POS_X, -50.0f, Spring), BeforeMerge, 1e-3f);
	EXPECT_TRUE(Runtime.HasActiveAnimation(401, EUiAnimProperty::POS_X));

	AdvanceFor(Runtime, 3.0f);
	EXPECT_NEAR(Runtime.GetValue(401, EUiAnimProperty::POS_X), -50.0f, 0.5f);
	EXPECT_FALSE(Runtime.HasActiveAnimation(401, EUiAnimProperty::POS_X));
}

TEST(InputField, InlineTrailingTextCentersItsVisualGroup)
{
	const CUIRect Content{10.0f, 20.0f, 120.0f, 24.0f};
	const ui_widget::SInlineTrailingTextLayout Layout = ui_widget::ResolveInlineTrailingTextLayout(Content, 24.0f, 12.0f, 1.0f);

	EXPECT_FLOAT_EQ(Layout.m_VisualGroupRect.x + Layout.m_VisualGroupRect.w * 0.5f, Content.x + Content.w * 0.5f);
	EXPECT_FLOAT_EQ(Layout.m_TrailingRect.x, Layout.m_VisualGroupRect.x + 27.0f);
	EXPECT_FLOAT_EQ(Layout.m_TrailingRect.x + Layout.m_TrailingRect.w, Layout.m_VisualGroupRect.x + Layout.m_VisualGroupRect.w);
	EXPECT_FLOAT_EQ(Layout.m_TextRect.x + Layout.m_TextRect.w, Layout.m_VisualGroupRect.x + 24.0f);
	EXPECT_GE(Layout.m_TextRect.x, Content.x);
	EXPECT_LE(Layout.m_TrailingRect.x + Layout.m_TrailingRect.w, Content.x + Content.w);
}

TEST(InputField, FocusRingExpandsShellAndMultilineDefaultsToTopLeft)
{
	const CUIRect Rect{10.0f, 20.0f, 240.0f, 32.0f};
	const ui_widget::SInputFieldLayout Layout = ui_widget::ResolveInputFieldLayout(Rect, true, true, 1.0f);
	EXPECT_LT(Layout.m_FocusRingRect.x, Rect.x);
	EXPECT_LT(Layout.m_FocusRingRect.y, Rect.y);
	EXPECT_GT(Layout.m_FocusRingRect.w, Rect.w);
	EXPECT_GT(Layout.m_FocusRingRect.h, Rect.h);

	ui_widget::SInputFieldOptions Options;
	EXPECT_EQ(ui_widget::ResolveInputFieldTextAlign(Options), TEXTALIGN_ML);
	Options.m_Mode = ui_widget::EInputFieldMode::MULTILINE;
	EXPECT_EQ(ui_widget::ResolveInputFieldTextAlign(Options), TEXTALIGN_TL);
	Options.m_TextAlign = TEXTALIGN_MC;
	EXPECT_EQ(ui_widget::ResolveInputFieldTextAlign(Options), TEXTALIGN_MC);
}
TEST(UiTheme, FocusRingKeepsInputFillStable)
{
	const SUiTheme Theme = ResolveUiTheme(ColorHSLA(0.58f, 0.35f, 0.48f, 1.0f), 1.0f);
	EXPECT_FLOAT_EQ(Theme.m_InputSurface.r, Theme.m_InputSurfaceFocused.r);
	EXPECT_FLOAT_EQ(Theme.m_InputSurface.g, Theme.m_InputSurfaceFocused.g);
	EXPECT_FLOAT_EQ(Theme.m_InputSurface.b, Theme.m_InputSurfaceFocused.b);
	EXPECT_LT(Theme.m_InputSurface.r, Theme.m_Surface.r);
	EXPECT_GE(Theme.m_FocusRingWidth, 2.0f);
	EXPECT_GT(Theme.m_FocusRing.a, Theme.m_Border.a);
}

TEST(UiTheme, InputFallbackTracksConfiguredFocusColor)
{
	const SUiTheme Theme = ResolveInputFallbackTheme(0x97FFA6);

	EXPECT_NEAR(Theme.m_FocusRing.r, 0x4D / 255.0f, 0.01f);
	EXPECT_NEAR(Theme.m_FocusRing.g, 0x9C / 255.0f, 0.01f);
	EXPECT_NEAR(Theme.m_FocusRing.b, 1.0f, 0.01f);
}

TEST(UiV2WidgetPresence, AnimatePresenceRendersWhileExiting)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2Tree Tree;
	CUiV2AnimationRuntime Runtime;
	IUiContext Ctx;
	Ctx.m_pTree = &Tree;
	Ctx.m_pAnim = &Runtime;
	Ctx.m_ScopeHash = MakeUiScopeHash("presence_widget_test");
	const void *pId = reinterpret_cast<const void *>(static_cast<uintptr_t>(0x7654));

	Tree.BeginFrame();
	const ui_widget::SAnimatePresenceResult First = ui_widget::AnimatePresence(Ctx, pId, true, ui_token::motion::HOVER_FADE);
	EXPECT_TRUE(First.m_Render);
	EXPECT_NEAR(First.m_Alpha, 0.0f, 1e-6f);
	EXPECT_TRUE(Runtime.HasActiveAnimation(First.m_NodeKey, EUiAnimProperty::ALPHA));
	Tree.EndFrame(Runtime);

	AdvanceFor(Runtime, 1.0f);
	Tree.BeginFrame();
	const ui_widget::SAnimatePresenceResult ExitStart = ui_widget::AnimatePresence(Ctx, pId, false, ui_token::motion::HOVER_FADE);
	EXPECT_TRUE(ExitStart.m_Render);
	EXPECT_GT(ExitStart.m_Alpha, 0.99f);
	EXPECT_TRUE(Runtime.HasActiveAnimation(ExitStart.m_NodeKey, EUiAnimProperty::ALPHA));
	Tree.EndFrame(Runtime);

	AdvanceFor(Runtime, 0.08f);
	Tree.BeginFrame();
	const ui_widget::SAnimatePresenceResult Exiting = ui_widget::AnimatePresence(Ctx, pId, false, ui_token::motion::HOVER_FADE);
	EXPECT_TRUE(Exiting.m_Render);
	EXPECT_GT(Exiting.m_Alpha, 0.0f);
	EXPECT_LT(Exiting.m_Alpha, 1.0f);
	Tree.EndFrame(Runtime);

	AdvanceFor(Runtime, 1.0f);
	Tree.BeginFrame();
	const ui_widget::SAnimatePresenceResult Gone = ui_widget::AnimatePresence(Ctx, pId, false, ui_token::motion::HOVER_FADE);
	EXPECT_FALSE(Gone.m_Render);
	EXPECT_NEAR(Gone.m_Alpha, 0.0f, 1e-6f);
	Tree.EndFrame(Runtime);
}

TEST(UiV2WidgetPresence, FreshEnterIsReportedOnlyAfterFullRemoval)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2Tree Tree;
	CUiV2AnimationRuntime Runtime;
	IUiContext Ctx;
	Ctx.m_pTree = &Tree;
	Ctx.m_pAnim = &Runtime;
	Ctx.m_ScopeHash = MakeUiScopeHash("presence_fresh_enter_test");
	const void *pId = reinterpret_cast<const void *>(static_cast<uintptr_t>(0x7655));

	Tree.BeginFrame();
	const ui_widget::SAnimatePresenceResult First = ui_widget::AnimatePresence(Ctx, pId, true, ui_token::motion::HOVER_FADE);
	EXPECT_TRUE(First.m_FreshEnter);
	Tree.EndFrame(Runtime);
	AdvanceFor(Runtime, 1.0f);

	Tree.BeginFrame();
	const ui_widget::SAnimatePresenceResult ExitStart = ui_widget::AnimatePresence(Ctx, pId, false, ui_token::motion::HOVER_FADE);
	EXPECT_FALSE(ExitStart.m_FreshEnter);
	Tree.EndFrame(Runtime);
	AdvanceFor(Runtime, 0.08f);

	Tree.BeginFrame();
	const ui_widget::SAnimatePresenceResult ReenterWhileExiting = ui_widget::AnimatePresence(Ctx, pId, true, ui_token::motion::HOVER_FADE);
	EXPECT_FALSE(ReenterWhileExiting.m_FreshEnter);
	Tree.EndFrame(Runtime);

	Tree.BeginFrame();
	ui_widget::AnimatePresence(Ctx, pId, false, ui_token::motion::HOVER_FADE);
	Tree.EndFrame(Runtime);
	AdvanceFor(Runtime, 1.0f);
	Tree.BeginFrame();
	const ui_widget::SAnimatePresenceResult Gone = ui_widget::AnimatePresence(Ctx, pId, false, ui_token::motion::HOVER_FADE);
	EXPECT_FALSE(Gone.m_Render);
	Tree.EndFrame(Runtime);

	Tree.BeginFrame();
	const ui_widget::SAnimatePresenceResult Reopened = ui_widget::AnimatePresence(Ctx, pId, true, ui_token::motion::HOVER_FADE);
	EXPECT_TRUE(Reopened.m_FreshEnter);
	Tree.EndFrame(Runtime);
}

TEST(UiV2WidgetPresence, ModalScaleCanResetOnFreshEnterAfterExit)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2Tree Tree;
	CUiV2AnimationRuntime Runtime;
	IUiContext Ctx;
	Ctx.m_pTree = &Tree;
	Ctx.m_pAnim = &Runtime;
	Ctx.m_ScopeHash = MakeUiScopeHash("presence_modal_scale_test");
	const void *pId = reinterpret_cast<const void *>(static_cast<uintptr_t>(0x7656));

	Tree.BeginFrame();
	ui_widget::SAnimatePresenceResult Presence = ui_widget::AnimatePresence(Ctx, pId, true, ui_token::motion::MODAL_IN);
	ASSERT_TRUE(Presence.m_FreshEnter);
	Runtime.SetValue(Presence.m_NodeKey, EUiAnimProperty::SCALE, 0.92f);
	ResolveUiAnimValue(Runtime, Presence.m_NodeKey, EUiAnimProperty::SCALE, 1.0f, ui_token::motion::MODAL_IN.m_DurationSec, ui_token::motion::MODAL_IN.m_Easing);
	Tree.EndFrame(Runtime);
	AdvanceFor(Runtime, 1.0f);

	Tree.BeginFrame();
	Presence = ui_widget::AnimatePresence(Ctx, pId, false, ui_token::motion::MODAL_IN);
	EXPECT_FALSE(Presence.m_FreshEnter);
	ResolveUiAnimValue(Runtime, Presence.m_NodeKey, EUiAnimProperty::SCALE, 0.96f, ui_token::motion::MODAL_IN.m_DurationSec, ui_token::motion::MODAL_IN.m_Easing);
	Tree.EndFrame(Runtime);
	AdvanceFor(Runtime, 1.0f);

	Tree.BeginFrame();
	const ui_widget::SAnimatePresenceResult Gone = ui_widget::AnimatePresence(Ctx, pId, false, ui_token::motion::MODAL_IN);
	EXPECT_FALSE(Gone.m_Render);
	Tree.EndFrame(Runtime);

	Tree.BeginFrame();
	Presence = ui_widget::AnimatePresence(Ctx, pId, true, ui_token::motion::MODAL_IN);
	ASSERT_TRUE(Presence.m_FreshEnter);
	Runtime.SetValue(Presence.m_NodeKey, EUiAnimProperty::SCALE, 0.92f);
	const float ReopenedScale = ResolveUiAnimValue(Runtime, Presence.m_NodeKey, EUiAnimProperty::SCALE, 1.0f, ui_token::motion::MODAL_IN.m_DurationSec, ui_token::motion::MODAL_IN.m_Easing);
	EXPECT_NEAR(ReopenedScale, 0.92f, 1e-6f);
	Tree.EndFrame(Runtime);
}

TEST(UiV2WidgetStateAnimation, MissingRuntimeReturnsTarget)
{
	IUiContext Ctx;
	Ctx.m_ScopeHash = MakeUiScopeHash("widget_state_test");
	int Id = 0;
	SUiAnimTransition Transition = ui_curve::DECELERATE;

	EXPECT_FLOAT_EQ(ui_widget::AnimateStateValue(Ctx, &Id, EUiAnimProperty::ALPHA, 0.75f, Transition), 0.75f);
}

TEST(UiV2WidgetStateAnimation, PreservesFullTransitionSemantics)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2AnimationRuntime Runtime;
	IUiContext Ctx;
	Ctx.m_pAnim = &Runtime;
	Ctx.m_ScopeHash = MakeUiScopeHash("widget_state_test");
	int Id = 0;
	const uint64_t NodeKey = BuildUiAnimNodeKey(Ctx.m_ScopeHash, reinterpret_cast<uint64_t>(&Id));
	Runtime.SetValue(NodeKey, EUiAnimProperty::ALPHA, 0.0f);

	SUiAnimTransition Transition;
	Transition.m_Driver = EUiAnimDriver::TWEEN;
	Transition.m_DurationSec = 1.0f;
	Transition.m_DelaySec = 0.5f;
	Transition.m_Easing = EEasing::LINEAR;
	Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
	Transition.m_Priority = 7;

	EXPECT_FLOAT_EQ(ui_widget::AnimateStateValue(Ctx, &Id, EUiAnimProperty::ALPHA, 1.0f, Transition), 0.0f);
	AdvanceFor(Runtime, 0.25f);
	EXPECT_FLOAT_EQ(Runtime.GetValue(NodeKey, EUiAnimProperty::ALPHA, 0.0f), 0.0f);

	AdvanceFor(Runtime, 0.5f);
	const float MidValue = Runtime.GetValue(NodeKey, EUiAnimProperty::ALPHA, 0.0f);
	EXPECT_GT(MidValue, 0.0f);
	EXPECT_LT(MidValue, 1.0f);
}

TEST(UiV2ImePresence, ExitKeepsRenderingUntilPresenceCompletes)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2Tree Tree;
	CUiV2AnimationRuntime Runtime;
	const uint64_t PopupKey = BuildUiAnimNodeKey(str_quickhash("qm_ime_popup_test"), 1);
	SUiAnimTransition Transition;
	Transition.m_DurationSec = 0.12f;
	Transition.m_Easing = EEasing::EASE_OUT;

	Tree.BeginFrame();
	SUiPresenceResult Presence = Tree.ResolvePresence(Runtime, PopupKey, true, Transition);
	EXPECT_TRUE(Presence.m_Render);
	EXPECT_TRUE(Presence.m_FreshEnter);
	Runtime.SetValue(PopupKey, EUiAnimProperty::POS_Y, 1.4f);
	const float InitialOffset = ResolveUiAnimValue(Runtime, PopupKey, EUiAnimProperty::POS_Y, 0.0f, 0.12f, EEasing::EASE_OUT);
	EXPECT_NEAR(InitialOffset, 1.4f, 1e-6f);
	Tree.EndFrame(Runtime);

	AdvanceFor(Runtime, 0.2f);
	Tree.BeginFrame();
	Presence = Tree.ResolvePresence(Runtime, PopupKey, false, Transition);
	EXPECT_TRUE(Presence.m_Render);
	EXPECT_GT(Presence.m_Alpha, 0.99f);
	const float ExitOffset = ResolveUiAnimValue(Runtime, PopupKey, EUiAnimProperty::POS_Y, -0.8f, 0.08f, EEasing::EASE_OUT);
	EXPECT_GT(ExitOffset, -0.8f);
	Tree.EndFrame(Runtime);

	AdvanceFor(Runtime, 0.04f);
	Tree.BeginFrame();
	Presence = Tree.ResolvePresence(Runtime, PopupKey, false, Transition);
	EXPECT_TRUE(Presence.m_Render);
	EXPECT_GT(Presence.m_Alpha, 0.0f);
	EXPECT_LT(Presence.m_Alpha, 1.0f);
	Tree.EndFrame(Runtime);
}

TEST(UiV2ImePresence, MotionLevelZeroHiddenPopupStopsRenderingImmediately)
{
	g_Config.m_QmUiMotionLevel = 0;
	CUiV2Tree Tree;
	CUiV2AnimationRuntime Runtime;
	const uint64_t PopupKey = BuildUiAnimNodeKey(str_quickhash("qm_ime_popup_test"), 2);
	SUiAnimTransition Transition;
	Transition.m_DurationSec = 0.08f;
	Transition.m_Easing = EEasing::EASE_OUT;

	Tree.BeginFrame();
	SUiPresenceResult Presence = Tree.ResolvePresence(Runtime, PopupKey, true, Transition);
	EXPECT_TRUE(Presence.m_Render);
	Tree.EndFrame(Runtime);

	Tree.BeginFrame();
	Presence = Tree.ResolvePresence(Runtime, PopupKey, false, Transition);
	EXPECT_FALSE(Presence.m_Render);
	EXPECT_NEAR(Presence.m_Alpha, 0.0f, 1e-6f);
	Tree.EndFrame(Runtime);

	g_Config.m_QmUiMotionLevel = 2;
}

TEST(UiV2ImePresence, ReenterWhileExitingKeepsPresenceContinuous)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2Tree Tree;
	CUiV2AnimationRuntime Runtime;
	const uint64_t PopupKey = BuildUiAnimNodeKey(str_quickhash("qm_ime_popup_test"), 3);
	SUiAnimTransition Transition;
	Transition.m_DurationSec = 0.12f;
	Transition.m_Easing = EEasing::EASE_OUT;

	Tree.BeginFrame();
	SUiPresenceResult Presence = Tree.ResolvePresence(Runtime, PopupKey, true, Transition);
	EXPECT_TRUE(Presence.m_FreshEnter);
	Tree.EndFrame(Runtime);
	AdvanceFor(Runtime, 0.2f);

	Tree.BeginFrame();
	Presence = Tree.ResolvePresence(Runtime, PopupKey, false, Transition);
	Tree.EndFrame(Runtime);
	AdvanceFor(Runtime, 0.04f);

	Tree.BeginFrame();
	const SUiPresenceResult Reentered = Tree.ResolvePresence(Runtime, PopupKey, true, Transition);
	EXPECT_TRUE(Reentered.m_Render);
	EXPECT_FALSE(Reentered.m_FreshEnter);
	EXPECT_GT(Reentered.m_Alpha, 0.0f);
	EXPECT_LT(Reentered.m_Alpha, 1.0f);
	Tree.EndFrame(Runtime);
}

namespace
{
	SUiAnimRequest MakeTweenRequest(uint64_t NodeKey, EUiAnimProperty Property, float Target, float DurationSec, EEasing Easing, uint32_t TrackId)
	{
		g_Config.m_QmUiMotionLevel = 2;
		SUiAnimRequest Request;
		Request.m_NodeKey = NodeKey;
		Request.m_Property = Property;
		Request.m_Target = Target;
		Request.m_Transition.m_DurationSec = DurationSec;
		Request.m_Transition.m_Easing = Easing;
		Request.m_Transition.m_Interrupt = EUiAnimInterruptPolicy::REPLACE;
		Request.m_TrackId = TrackId;
		return Request;
	}
}

TEST(UiV2AnimEasing, OutBackOvershoots)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(201, EUiAnimProperty::ALPHA, 0.0f);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeTweenRequest(201, EUiAnimProperty::ALPHA, 1.0f, 1.0f, EEasing::EASE_OUT_BACK, 91)));

	AdvanceFor(Runtime, 0.7f);
	EXPECT_GT(Runtime.GetValue(201, EUiAnimProperty::ALPHA), 1.0f);

	AdvanceFor(Runtime, 0.5f);
	EXPECT_NEAR(Runtime.GetValue(201, EUiAnimProperty::ALPHA), 1.0f, 1e-3f);
}

TEST(UiV2AnimEasing, CubicBezierMatchesReference)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(202, EUiAnimProperty::ALPHA, 0.0f);
	SUiAnimRequest Request = MakeTweenRequest(202, EUiAnimProperty::ALPHA, 1.0f, 1.0f, EEasing::CUBIC_BEZIER, 92);
	Request.m_Transition.m_Bezier = {0.2f, 0.0f, 0.0f, 1.0f};
	EXPECT_TRUE(Runtime.RequestAnimation(Request));

	AdvanceFor(Runtime, 0.25f);
	const float At25 = Runtime.GetValue(202, EUiAnimProperty::ALPHA);
	EXPECT_GT(At25, 0.55f);
	EXPECT_LT(At25, 0.75f);

	AdvanceFor(Runtime, 0.25f);
	const float At50 = Runtime.GetValue(202, EUiAnimProperty::ALPHA);
	EXPECT_GT(At50, 0.82f);
	EXPECT_LT(At50, 0.95f);

	AdvanceFor(Runtime, 0.25f);
	const float At75 = Runtime.GetValue(202, EUiAnimProperty::ALPHA);
	EXPECT_GT(At75, 0.93f);
	EXPECT_LT(At75, 1.0f);
}

TEST(UiV2AnimEasing, NewEnumsRoundTrip)
{
	const EEasing aEasings[] = {EEasing::EASE_OUT_QUART, EEasing::EASE_OUT_BACK, EEasing::EASE_IN_OUT_CUBIC};
	uint32_t NextTrackId = 100;
	for(EEasing Easing : aEasings)
	{
		CUiV2AnimationRuntime Runtime;
		Runtime.SetValue(1, EUiAnimProperty::ALPHA, 0.0f);
		EXPECT_TRUE(Runtime.RequestAnimation(MakeTweenRequest(1, EUiAnimProperty::ALPHA, 1.0f, 1.0f, Easing, NextTrackId++)));

		EXPECT_NEAR(Runtime.GetValue(1, EUiAnimProperty::ALPHA), 0.0f, 1e-3f);

		AdvanceFor(Runtime, 1.5f);
		EXPECT_NEAR(Runtime.GetValue(1, EUiAnimProperty::ALPHA), 1.0f, 1e-3f);
	}
}

TEST(UiV2AnimEasing, CurvePresetsExposed)
{
	EXPECT_EQ(ui_curve::STANDARD.m_Easing, EEasing::EASE_IN_OUT_CUBIC);
	EXPECT_EQ(ui_curve::EMPHASIZED.m_Easing, EEasing::CUBIC_BEZIER);
	EXPECT_NEAR(ui_curve::EMPHASIZED.m_Bezier.m_X1, 0.2f, 1e-6f);
	EXPECT_NEAR(ui_curve::EMPHASIZED.m_Bezier.m_Y2, 1.0f, 1e-6f);
	EXPECT_EQ(ui_curve::BOUNCE_OUT.m_Easing, EEasing::EASE_OUT_BACK);
	EXPECT_NEAR(ui_spring::SNAPPY.m_Stiffness, 280.0f, 1e-6f);
	EXPECT_NEAR(ui_spring::GENTLE.m_Damping, 14.0f, 1e-6f);
	EXPECT_NEAR(ui_token::motion::CARD_REORDER.m_Stiffness, 900.0f, 1e-6f);
	EXPECT_NEAR(ui_token::motion::CARD_REORDER.m_Damping, 48.0f, 1e-6f);
}

TEST(UiV2AnimEasing, CustomEasingCanBeRegisteredAndReset)
{
	struct SCustomEasingState
	{
		float m_Multiplier = 1.0f;
		int m_Calls = 0;
	};
	SCustomEasingState State{2.0f, 0};
	auto CustomEase = [](float Progress, void *pUser) -> float {
		SCustomEasingState *pState = static_cast<SCustomEasingState *>(pUser);
		++pState->m_Calls;
		return std::min(1.0f, Progress * pState->m_Multiplier);
	};

	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(203, EUiAnimProperty::ALPHA, 0.0f);
	Runtime.RegisterCustomEasing(7, CustomEase, &State);

	SUiAnimRequest Request = MakeTweenRequest(203, EUiAnimProperty::ALPHA, 1.0f, 1.0f, EEasing::CUSTOM, 93);
	Request.m_Transition.m_CustomEasingId = 7;
	EXPECT_TRUE(Runtime.RequestAnimation(Request));
	AdvanceFor(Runtime, 0.25f);

	EXPECT_GT(State.m_Calls, 0);
	EXPECT_GT(Runtime.GetValue(203, EUiAnimProperty::ALPHA), 0.45f);
	EXPECT_LT(Runtime.GetValue(203, EUiAnimProperty::ALPHA), 0.65f);

	Runtime.Reset();
	Runtime.SetValue(204, EUiAnimProperty::ALPHA, 0.0f);
	Request.m_NodeKey = 204;
	Request.m_TrackId = 94;
	EXPECT_TRUE(Runtime.RequestAnimation(Request));
	AdvanceFor(Runtime, 0.25f);

	EXPECT_NEAR(Runtime.GetValue(204, EUiAnimProperty::ALPHA), 0.25f, 0.05f);
}

TEST(UiV2AnimEasing, CustomEasingIsSnapshottedForActiveTrack)
{
	struct SCustomEasingState
	{
		float m_Multiplier = 1.0f;
		int m_Calls = 0;
	};
	SCustomEasingState State{2.0f, 0};
	auto CustomEase = [](float Progress, void *pUser) -> float {
		SCustomEasingState *pState = static_cast<SCustomEasingState *>(pUser);
		++pState->m_Calls;
		return std::min(1.0f, Progress * pState->m_Multiplier);
	};

	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(205, EUiAnimProperty::ALPHA, 0.0f);
	Runtime.RegisterCustomEasing(8, CustomEase, &State);

	SUiAnimRequest Request = MakeTweenRequest(205, EUiAnimProperty::ALPHA, 1.0f, 1.0f, EEasing::CUSTOM, 95);
	Request.m_Transition.m_CustomEasingId = 8;
	EXPECT_TRUE(Runtime.RequestAnimation(Request));
	AdvanceFor(Runtime, 0.1f);
	Runtime.UnregisterCustomEasing(8);
	State.m_Calls = 0;

	AdvanceFor(Runtime, 0.2f);

	EXPECT_GT(State.m_Calls, 0);
	EXPECT_GT(Runtime.GetValue(205, EUiAnimProperty::ALPHA), 0.45f);
}

TEST(UiV2AnimEasing, MergeTargetRefreshesCustomEasing)
{
	struct SCustomEasingState
	{
		float m_Multiplier = 1.0f;
		int m_Calls = 0;
	};
	SCustomEasingState SlowState{0.25f, 0};
	SCustomEasingState FastState{2.0f, 0};
	auto CustomEase = [](float Progress, void *pUser) -> float {
		SCustomEasingState *pState = static_cast<SCustomEasingState *>(pUser);
		++pState->m_Calls;
		return std::min(1.0f, Progress * pState->m_Multiplier);
	};

	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(206, EUiAnimProperty::ALPHA, 0.0f);
	Runtime.RegisterCustomEasing(9, CustomEase, &SlowState);
	Runtime.RegisterCustomEasing(10, CustomEase, &FastState);

	SUiAnimRequest Request = MakeTweenRequest(206, EUiAnimProperty::ALPHA, 1.0f, 1.0f, EEasing::CUSTOM, 96);
	Request.m_Transition.m_CustomEasingId = 9;
	EXPECT_TRUE(Runtime.RequestAnimation(Request));
	AdvanceFor(Runtime, 0.2f);
	SlowState.m_Calls = 0;
	FastState.m_Calls = 0;

	Request.m_Target = 2.0f;
	Request.m_Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
	Request.m_Transition.m_CustomEasingId = 10;
	Request.m_TrackId = 97;
	EXPECT_TRUE(Runtime.RequestAnimation(Request));
	SlowState.m_Calls = 0;
	FastState.m_Calls = 0;
	AdvanceFor(Runtime, 0.2f);

	// 打断后转弹簧接管：接管轨道由弹簧驱动，新的自定义缓动不再参与（FastState 不应被调用）。
	EXPECT_EQ(SlowState.m_Calls, 0);
	EXPECT_EQ(FastState.m_Calls, 0);
	EXPECT_GT(Runtime.GetValue(206, EUiAnimProperty::ALPHA), 0.45f);
}

TEST(UiV2AnimColor, DefaultInterpolationUsesLinearSrgb)
{
	g_Config.m_QmUiColorInterpolation = 0;
	const ColorRGBA From(1.0f, 0.0f, 0.0f, 0.25f);
	const ColorRGBA To(0.0f, 1.0f, 0.0f, 0.75f);

	const ColorRGBA Mid = ResolveUiAnimInterpolatedColor(From, To, 0.5f);

	EXPECT_NEAR(Mid.r, 0.5f, 1e-6f);
	EXPECT_NEAR(Mid.g, 0.5f, 1e-6f);
	EXPECT_NEAR(Mid.b, 0.0f, 1e-6f);
	EXPECT_NEAR(Mid.a, 0.5f, 1e-6f);
}

TEST(UiV2AnimColor, OklabInterpolationKeepsMidColorPerceptuallyBrighter)
{
	g_Config.m_QmUiColorInterpolation = 1;
	const ColorRGBA From(1.0f, 0.0f, 0.0f, 0.25f);
	const ColorRGBA To(0.0f, 1.0f, 0.0f, 0.75f);

	const ColorRGBA Mid = ResolveUiAnimInterpolatedColor(From, To, 0.5f);

	EXPECT_GT(Mid.r, 0.5f);
	EXPECT_GT(Mid.g, 0.5f);
	EXPECT_GT(Mid.b, 0.0f);
	EXPECT_NEAR(Mid.a, 0.5f, 1e-6f);
}

TEST(UiV2AnimColor, ResolveValueColorUsesConfiguredInterpolation)
{
	g_Config.m_QmUiMotionLevel = 2;
	g_Config.m_QmUiColorInterpolation = 1;
	CUiV2AnimationRuntime Runtime;
	const uint64_t NodeKey = 700;
	const ColorRGBA From(1.0f, 0.0f, 0.0f, 0.25f);
	const ColorRGBA To(0.0f, 1.0f, 0.0f, 0.75f);

	Runtime.SetValue(NodeKey, EUiAnimProperty::COLOR_R, From.r);
	Runtime.SetValue(NodeKey, EUiAnimProperty::COLOR_G, From.g);
	Runtime.SetValue(NodeKey, EUiAnimProperty::COLOR_B, From.b);
	Runtime.SetValue(NodeKey, EUiAnimProperty::COLOR_A, From.a);

	EXPECT_NEAR(ResolveUiAnimValueColor(Runtime, NodeKey, To, 1.0f, EEasing::LINEAR).r, From.r, 1e-6f);
	AdvanceFor(Runtime, 0.5f);
	const ColorRGBA Mid = ResolveUiAnimValueColor(Runtime, NodeKey, To, 1.0f, EEasing::LINEAR);

	EXPECT_GT(Mid.r, 0.5f);
	EXPECT_GT(Mid.g, 0.5f);
	EXPECT_GT(Mid.b, 0.0f);
	EXPECT_GT(Mid.a, 0.45f);
	EXPECT_LT(Mid.a, 0.55f);
}

TEST(UiV2AnimColor, OklabTargetChangeKeepsContinuity)
{
	g_Config.m_QmUiMotionLevel = 2;
	g_Config.m_QmUiColorInterpolation = 1;
	CUiV2AnimationRuntime Runtime;
	const uint64_t NodeKey = 701;
	const ColorRGBA From(1.0f, 0.0f, 0.0f, 1.0f);
	const ColorRGBA Green(0.0f, 1.0f, 0.0f, 1.0f);
	const ColorRGBA Blue(0.0f, 0.0f, 1.0f, 1.0f);

	Runtime.SetValue(NodeKey, EUiAnimProperty::COLOR_R, From.r);
	Runtime.SetValue(NodeKey, EUiAnimProperty::COLOR_G, From.g);
	Runtime.SetValue(NodeKey, EUiAnimProperty::COLOR_B, From.b);
	Runtime.SetValue(NodeKey, EUiAnimProperty::COLOR_A, From.a);

	ResolveUiAnimValueColor(Runtime, NodeKey, Green, 1.0f, EEasing::LINEAR);
	AdvanceFor(Runtime, 0.25f);
	const ColorRGBA BeforeChange = ResolveUiAnimValueColor(Runtime, NodeKey, Green, 1.0f, EEasing::LINEAR);

	const ColorRGBA AfterChange = ResolveUiAnimValueColor(Runtime, NodeKey, Blue, 1.0f, EEasing::LINEAR);

	EXPECT_NEAR(AfterChange.r, BeforeChange.r, 0.02f);
	EXPECT_NEAR(AfterChange.g, BeforeChange.g, 0.02f);
	EXPECT_NEAR(AfterChange.b, BeforeChange.b, 0.02f);
}

TEST(UiV2AnimColor, ResolveTargetCacheKeepsPropertiesIsolated)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2AnimationRuntime Runtime;
	const uint64_t NodeKey = 702;

	SUiAnimTransition PosTransition;
	PosTransition.m_Driver = EUiAnimDriver::SPRING;
	PosTransition.m_Spring = ui_spring::SNAPPY;
	PosTransition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;

	SUiAnimTransition MixTransition;
	MixTransition.m_DurationSec = 1.0f;
	MixTransition.m_Easing = EEasing::LINEAR;
	MixTransition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
	MixTransition.m_Driver = EUiAnimDriver::TWEEN;

	Runtime.SetValue(NodeKey, EUiAnimProperty::POS_Y, 0.0f);
	Runtime.SetValue(NodeKey, EUiAnimProperty::COLOR_MIX, 0.0f);

	Runtime.ResolveTargetValue(NodeKey, EUiAnimProperty::POS_Y, 80.0f, PosTransition);
	Runtime.ResolveTargetValue(NodeKey, EUiAnimProperty::COLOR_MIX, 1.0f, MixTransition);
	AdvanceFor(Runtime, 0.1f);
	const float PosBefore = Runtime.GetValue(NodeKey, EUiAnimProperty::POS_Y);
	const float MixBefore = Runtime.GetValue(NodeKey, EUiAnimProperty::COLOR_MIX);

	Runtime.ResolveTargetValue(NodeKey, EUiAnimProperty::POS_Y, 80.0f, PosTransition);
	Runtime.ResolveTargetValue(NodeKey, EUiAnimProperty::COLOR_MIX, 1.0f, MixTransition);

	EXPECT_NEAR(Runtime.GetValue(NodeKey, EUiAnimProperty::POS_Y), PosBefore, 1e-6f);
	EXPECT_NEAR(Runtime.GetValue(NodeKey, EUiAnimProperty::COLOR_MIX), MixBefore, 1e-6f);
	EXPECT_TRUE(Runtime.HasActiveAnimation(NodeKey, EUiAnimProperty::POS_Y));
	EXPECT_TRUE(Runtime.HasActiveAnimation(NodeKey, EUiAnimProperty::COLOR_MIX));
}

TEST(UiV2AnimColor, OklabPerFrameResolveUsesStableSegmentStart)
{
	g_Config.m_QmUiMotionLevel = 2;
	g_Config.m_QmUiColorInterpolation = 1;
	CUiV2AnimationRuntime Runtime;
	const uint64_t NodeKey = 703;
	const ColorRGBA From(1.0f, 0.0f, 0.0f, 1.0f);
	const ColorRGBA To(0.0f, 1.0f, 0.0f, 1.0f);
	const ColorRGBA ExpectedMid = ResolveUiAnimInterpolatedColor(From, To, 0.5f);

	Runtime.SetValue(NodeKey, EUiAnimProperty::COLOR_R, From.r);
	Runtime.SetValue(NodeKey, EUiAnimProperty::COLOR_G, From.g);
	Runtime.SetValue(NodeKey, EUiAnimProperty::COLOR_B, From.b);
	Runtime.SetValue(NodeKey, EUiAnimProperty::COLOR_A, From.a);

	ColorRGBA Mid = From;
	for(int i = 0; i < 30; ++i)
	{
		Mid = ResolveUiAnimValueColor(Runtime, NodeKey, To, 1.0f, EEasing::LINEAR);
		Runtime.Advance(1.0f / 60.0f);
	}
	Mid = ResolveUiAnimValueColor(Runtime, NodeKey, To, 1.0f, EEasing::LINEAR);

	EXPECT_NEAR(Mid.r, ExpectedMid.r, 0.03f);
	EXPECT_NEAR(Mid.g, ExpectedMid.g, 0.03f);
	EXPECT_NEAR(Mid.b, ExpectedMid.b, 0.03f);
	EXPECT_NEAR(Mid.a, ExpectedMid.a, 0.03f);
}

TEST(UiV2InputField, BuildsSharedResultForCommitAndClearStates)
{
	const ui_widget::SInputFieldResult ClickAwayCommit = ui_widget::BuildInputFieldResult(true, false, false, false, false, false, false);
	EXPECT_FALSE(ClickAwayCommit.m_Changed);
	EXPECT_TRUE(ClickAwayCommit.m_Deactivated);
	EXPECT_TRUE(ClickAwayCommit.m_Committed);
	EXPECT_FALSE(ClickAwayCommit.m_Submitted);
	EXPECT_FALSE(ClickAwayCommit.m_Cleared);

	const ui_widget::SInputFieldResult EnterSubmit = ui_widget::BuildInputFieldResult(true, false, false, true, false, false, false);
	EXPECT_TRUE(EnterSubmit.m_Deactivated);
	EXPECT_TRUE(EnterSubmit.m_Committed);
	EXPECT_TRUE(EnterSubmit.m_Submitted);

	const ui_widget::SInputFieldResult ClearButton = ui_widget::BuildInputFieldResult(true, true, true, false, false, true, true);
	EXPECT_TRUE(ClearButton.m_Changed);
	EXPECT_FALSE(ClearButton.m_Deactivated);
	EXPECT_FALSE(ClearButton.m_Committed);
	EXPECT_TRUE(ClearButton.m_Cleared);
}

TEST(UiV2InputField, DeclaresEditingCapabilitiesPerPublicFieldType)
{
	const auto HasCapability = [](unsigned Capabilities, ui_widget::EInputFieldCapability Capability) {
		return (Capabilities & static_cast<unsigned>(Capability)) != 0u;
	};

	const unsigned TextCaps = ui_widget::InputFieldCapabilities();
	EXPECT_TRUE(HasCapability(TextCaps, ui_widget::EInputFieldCapability::DOUBLE_CLICK_SELECT_ALL));
	EXPECT_TRUE(HasCapability(TextCaps, ui_widget::EInputFieldCapability::CLICK_AWAY_COMMIT));
	EXPECT_TRUE(HasCapability(TextCaps, ui_widget::EInputFieldCapability::CURSOR_INSERTION));
	EXPECT_TRUE(HasCapability(TextCaps, ui_widget::EInputFieldCapability::MOUSE_DRAG_SELECTION));
	EXPECT_FALSE(HasCapability(TextCaps, ui_widget::EInputFieldCapability::CLEAR_BUTTON));
	EXPECT_FALSE(HasCapability(TextCaps, ui_widget::EInputFieldCapability::SEARCH_HOTKEY));

	const unsigned ClearableCaps = ui_widget::ClearableInputFieldCapabilities();
	EXPECT_TRUE(HasCapability(ClearableCaps, ui_widget::EInputFieldCapability::CLEAR_BUTTON));
	EXPECT_FALSE(HasCapability(ClearableCaps, ui_widget::EInputFieldCapability::SEARCH_HOTKEY));
	EXPECT_EQ((ClearableCaps & TextCaps), TextCaps);

	const unsigned SearchCaps = ui_widget::SearchFieldCapabilities();
	EXPECT_TRUE(HasCapability(SearchCaps, ui_widget::EInputFieldCapability::CLEAR_BUTTON));
	EXPECT_TRUE(HasCapability(SearchCaps, ui_widget::EInputFieldCapability::SEARCH_HOTKEY));
	EXPECT_EQ((SearchCaps & ClearableCaps), ClearableCaps);
}
