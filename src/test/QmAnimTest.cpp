// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "test.h"

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

#include <array>
#include <cmath>

namespace
{
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

	TEST(SettingsCard, HeaderTextUsesCanonicalBoundedEllipsisContract)
	{
		const std::string Source = ReadTestSourceFile("src/game/client/QmUi/SettingsCard.cpp");
		EXPECT_NE(Source.find("TitleProps.m_MaxWidth = DrawFrame.m_TitleRect.w;"), std::string::npos);
		EXPECT_NE(Source.find("TitleProps.m_EllipsisAtEnd = true;"), std::string::npos);
		EXPECT_NE(Source.find("SubtitleProps.m_MaxWidth = DrawFrame.m_SubtitleRect.w;"), std::string::npos);
		EXPECT_NE(Source.find("SubtitleProps.m_EllipsisAtEnd = true;"), std::string::npos);
	}

	TEST(SettingsCard, DeckMeasuresContentFromCanonicalPaddingToken)
	{
		const std::string Source = ReadTestSourceFile("src/game/client/QmUi/SettingsCardDeck.cpp");
		EXPECT_NE(Source.find("2.0f * ui_token::settings::CARD_PADDING"), std::string::npos);
		EXPECT_EQ(Source.find("Slot.w - 28.0f"), std::string::npos);
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
		EXPECT_FLOAT_EQ(ResolveQmHudInputOverlayHeight(Metrics, true), 250.0f);
		EXPECT_FLOAT_EQ(ResolveQmHudNotificationsHeight(Metrics, true, false), 390.0f);
		EXPECT_FLOAT_EQ(ResolveQmHudNotificationsHeight(Metrics, true, true), 490.0f);
		EXPECT_FLOAT_EQ(ResolveQmHudDummyMiniViewHeight(Metrics, false), 25.0f);
		EXPECT_FLOAT_EQ(ResolveQmHudDummyMiniViewHeight(Metrics, true), 121.0f);
		EXPECT_FLOAT_EQ(ResolveQmHudVoiceHeight(Metrics, false, false, false, 0, false, false), 20.0f);
		EXPECT_FLOAT_EQ(ResolveQmHudVoiceHeight(Metrics, true, false, false, 0, false, false), 145.0f);
		EXPECT_FLOAT_EQ(ResolveQmHudVoiceHeight(Metrics, true, true, false, 0, false, false), 425.75f);
		EXPECT_NEAR(ResolveQmHudVoiceHeight(Metrics, true, true, true, 1, true, true), 798.7f, 0.001f);
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

	TEST(SettingsCard, EntryHoverSuppressionWaitsForStableLayout)
	{
		const std::string Source = ReadTestSourceFile("src/game/client/QmUi/SettingsCardDeck.cpp");
		EXPECT_NE(Source.find("const bool LayoutStable = !EntryPending && !EntryPositionActive"), std::string::npos);
		EXPECT_NE(Source.find("LayoutStable && m_HasPointerPosition"), std::string::npos);
		EXPECT_NE(Source.find("!ContentHeightAnimationActive"), std::string::npos);
		EXPECT_NE(Source.find("!ReflowTargetChanged"), std::string::npos);
		EXPECT_NE(Source.find("!ReflowPositionActive"), std::string::npos);
	}

	TEST(SettingsCard, ScrollingKeepsHoverFeedbackStable)
	{
		const std::string Source = ReadTestSourceFile("src/game/client/QmUi/SettingsCardDeck.cpp");
		EXPECT_EQ(Source.find("const bool ScrollOffsetChanged"), std::string::npos);
		EXPECT_NE(Source.find("State.m_HoverFeedbackEnabled = !m_SuppressHoverFeedbackOnce"), std::string::npos);
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

TEST(InputField, ClearAndTrailingSlotsDoNotOverlap)
{
	const CUIRect Rect{10.0f, 20.0f, 160.0f, 32.0f};
	const ui_widget::SInputFieldLayout Layout = ui_widget::ResolveInputFieldLayout(Rect, false, true, 1.0f, Rect.h);

	EXPECT_GT(Layout.m_ClearRect.w, 0.0f);
	EXPECT_GT(Layout.m_TrailingRect.w, 0.0f);
	EXPECT_LE(Layout.m_TrailingRect.x + Layout.m_TrailingRect.w, Layout.m_ClearRect.x);
	EXPECT_LT(Layout.m_ContentRect.x + Layout.m_ContentRect.w, Layout.m_TrailingRect.x);
}

TEST(SettingsPageLayout, ConfigRowsIncludePaddingAndResponsiveControlBlock)
{
	const SSettingsConfigRowMetrics Wide = ResolveSettingsConfigRowMetrics(false, false, 20.0f, 5.0f, 10.0f, 20.0f, 5.0f);
	EXPECT_FLOAT_EQ(Wide.m_ControlBlockHeight, 20.0f);
	EXPECT_FLOAT_EQ(Wide.m_RowHeight, 42.0f);

	const SSettingsConfigRowMetrics Narrow = ResolveSettingsConfigRowMetrics(false, true, 20.0f, 5.0f, 10.0f, 20.0f, 5.0f);
	EXPECT_FLOAT_EQ(Narrow.m_ControlBlockHeight, 45.0f);
	EXPECT_FLOAT_EQ(Narrow.m_RowHeight, 67.0f);

	const SSettingsConfigRowMetrics CompactNarrow = ResolveSettingsConfigRowMetrics(true, true, 20.0f, 5.0f, 10.0f, 24.0f, 5.0f);
	EXPECT_FLOAT_EQ(CompactNarrow.m_ControlLineHeight, 24.0f);
	EXPECT_FLOAT_EQ(CompactNarrow.m_ControlBlockHeight, 49.0f);
	EXPECT_FLOAT_EQ(CompactNarrow.m_RowHeight, 59.0f);
}

TEST(UiV2Anim, ReplacePolicyReplacesCurrentTrack)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(1, EUiAnimProperty::POS_X, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(1, EUiAnimProperty::POS_X, 10.0f, 0.5f, 1, EUiAnimInterruptPolicy::REPLACE, 11)));
	AdvanceFor(Runtime, 0.2f);
	const float MidValue = Runtime.GetValue(1, EUiAnimProperty::POS_X);
	EXPECT_GT(MidValue, 0.0f);
	EXPECT_LT(MidValue, 10.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(1, EUiAnimProperty::POS_X, 20.0f, 0.4f, 2, EUiAnimInterruptPolicy::REPLACE, 12)));
	EXPECT_EQ(Runtime.ActiveTrackCount(), 1);

	AdvanceFor(Runtime, 0.5f);
	EXPECT_NEAR(Runtime.GetValue(1, EUiAnimProperty::POS_X), 20.0f, 0.001f);

	SUiAnimCompleteEvent Event;
	ASSERT_TRUE(Runtime.PollCompletedEvent(Event));
	EXPECT_EQ(Event.m_TrackId, 12u);
	EXPECT_EQ(Event.m_NodeKey, 1u);
	EXPECT_EQ(Event.m_Property, EUiAnimProperty::POS_X);
	EXPECT_FALSE(Runtime.PollCompletedEvent(Event));
}

TEST(UiV2Anim, QueuePolicyRunsInOrder)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(7, EUiAnimProperty::ALPHA, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(7, EUiAnimProperty::ALPHA, 10.0f, 0.2f, 1, EUiAnimInterruptPolicy::QUEUE, 21)));
	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(7, EUiAnimProperty::ALPHA, 20.0f, 0.2f, 1, EUiAnimInterruptPolicy::QUEUE, 22)));
	EXPECT_EQ(Runtime.ActiveTrackCount(), 1);
	EXPECT_EQ(Runtime.QueuedTrackCount(), 1);

	AdvanceFor(Runtime, 0.25f);
	SUiAnimCompleteEvent Event;
	ASSERT_TRUE(Runtime.PollCompletedEvent(Event));
	EXPECT_EQ(Event.m_TrackId, 21u);
	EXPECT_TRUE(Runtime.HasActiveAnimation(7, EUiAnimProperty::ALPHA));

	AdvanceFor(Runtime, 0.25f);
	ASSERT_TRUE(Runtime.PollCompletedEvent(Event));
	EXPECT_EQ(Event.m_TrackId, 22u);
	EXPECT_FALSE(Runtime.HasActiveAnimation(7, EUiAnimProperty::ALPHA));
	EXPECT_NEAR(Runtime.GetValue(7, EUiAnimProperty::ALPHA), 20.0f, 0.001f);
}

TEST(UiV2Anim, KeepHigherPriorityRejectsLowerPriorityRequest)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(3, EUiAnimProperty::SCALE, 1.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(3, EUiAnimProperty::SCALE, 2.0f, 0.4f, 10, EUiAnimInterruptPolicy::REPLACE, 31)));
	AdvanceFor(Runtime, 0.1f);

	EXPECT_FALSE(Runtime.RequestAnimation(MakeRequest(3, EUiAnimProperty::SCALE, 5.0f, 0.3f, 5, EUiAnimInterruptPolicy::KEEP_HIGHER_PRIORITY, 32)));

	const float AfterRejected = Runtime.GetValue(3, EUiAnimProperty::SCALE);
	EXPECT_LT(AfterRejected, 2.5f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(3, EUiAnimProperty::SCALE, 5.0f, 0.3f, 20, EUiAnimInterruptPolicy::KEEP_HIGHER_PRIORITY, 33)));
	AdvanceFor(Runtime, 0.4f);
	EXPECT_NEAR(Runtime.GetValue(3, EUiAnimProperty::SCALE), 5.0f, 0.001f);

	SUiAnimCompleteEvent Event;
	ASSERT_TRUE(Runtime.PollCompletedEvent(Event));
	EXPECT_EQ(Event.m_TrackId, 33u);
}

TEST(UiV2Anim, MergeTargetKeepsContinuity)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(5, EUiAnimProperty::WIDTH, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(5, EUiAnimProperty::WIDTH, 10.0f, 1.0f, 1, EUiAnimInterruptPolicy::REPLACE, 41)));
	AdvanceFor(Runtime, 0.25f);
	const float BeforeMerge = Runtime.GetValue(5, EUiAnimProperty::WIDTH);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(5, EUiAnimProperty::WIDTH, 20.0f, 1.0f, 1, EUiAnimInterruptPolicy::MERGE_TARGET, 42)));
	const float AfterMerge = Runtime.GetValue(5, EUiAnimProperty::WIDTH);
	EXPECT_NEAR(BeforeMerge, AfterMerge, 0.0001f);

	AdvanceFor(Runtime, 1.25f);
	EXPECT_NEAR(Runtime.GetValue(5, EUiAnimProperty::WIDTH), 20.0f, 0.001f);

	SUiAnimCompleteEvent Event;
	ASSERT_TRUE(Runtime.PollCompletedEvent(Event));
	EXPECT_EQ(Event.m_TrackId, 42u);
}

TEST(UiV2Anim, DelayDefersAnimationStart)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(8, EUiAnimProperty::POS_Y, 0.0f);

	SUiAnimRequest Request = MakeRequest(8, EUiAnimProperty::POS_Y, 8.0f, 0.2f, 1, EUiAnimInterruptPolicy::REPLACE, 51);
	Request.m_Transition.m_DelaySec = 0.2f;
	EXPECT_TRUE(Runtime.RequestAnimation(Request));

	AdvanceFor(Runtime, 0.1f);
	EXPECT_NEAR(Runtime.GetValue(8, EUiAnimProperty::POS_Y), 0.0f, 0.0001f);

	AdvanceFor(Runtime, 0.15f);
	EXPECT_GT(Runtime.GetValue(8, EUiAnimProperty::POS_Y), 0.0f);

	AdvanceFor(Runtime, 0.2f);
	EXPECT_NEAR(Runtime.GetValue(8, EUiAnimProperty::POS_Y), 8.0f, 0.001f);
}

TEST(UiV2Anim, ZeroDeltaTimeDoesNotAdvance)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(9, EUiAnimProperty::POS_X, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(9, EUiAnimProperty::POS_X, 9.0f, 0.5f, 1, EUiAnimInterruptPolicy::REPLACE, 61)));
	const float Before = Runtime.GetValue(9, EUiAnimProperty::POS_X);
	Runtime.Advance(0.0f);
	const float After = Runtime.GetValue(9, EUiAnimProperty::POS_X);
	EXPECT_NEAR(Before, After, 0.0001f);
	EXPECT_TRUE(Runtime.HasActiveAnimation(9, EUiAnimProperty::POS_X));
}

TEST(UiV2Anim, ZeroDurationCompletesImmediately)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(10, EUiAnimProperty::ALPHA, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(10, EUiAnimProperty::ALPHA, 1.0f, 0.0f, 1, EUiAnimInterruptPolicy::REPLACE, 71)));
	EXPECT_NEAR(Runtime.GetValue(10, EUiAnimProperty::ALPHA), 1.0f, 0.0001f);
	EXPECT_FALSE(Runtime.HasActiveAnimation(10, EUiAnimProperty::ALPHA));

	SUiAnimCompleteEvent Event;
	ASSERT_TRUE(Runtime.PollCompletedEvent(Event));
	EXPECT_EQ(Event.m_TrackId, 71u);
	EXPECT_EQ(Event.m_NodeKey, 10u);
	EXPECT_EQ(Event.m_Property, EUiAnimProperty::ALPHA);
	EXPECT_FALSE(Runtime.PollCompletedEvent(Event));
}

TEST(UiV2Anim, AwaitTracksCompletesAfterAllTrackedIds)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(11, EUiAnimProperty::POS_X, 0.0f);
	Runtime.SetValue(11, EUiAnimProperty::POS_Y, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(11, EUiAnimProperty::POS_X, 10.0f, 0.1f, 1, EUiAnimInterruptPolicy::REPLACE, 91)));
	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(11, EUiAnimProperty::POS_Y, 20.0f, 0.3f, 1, EUiAnimInterruptPolicy::REPLACE, 92)));
	const uint32_t aTrackIds[] = {91, 92};
	const uint32_t GroupId = Runtime.AwaitTracks(aTrackIds, 2);
	EXPECT_NE(GroupId, 0u);

	AdvanceFor(Runtime, 0.15f);
	SUiAnimCompleteEvent TrackEvent;
	ASSERT_TRUE(Runtime.PollCompletedEvent(TrackEvent));
	EXPECT_EQ(TrackEvent.m_TrackId, 91u);
	SUiAnimGroupCompleteEvent GroupEvent;
	EXPECT_FALSE(Runtime.PollGroupCompletedEvent(GroupEvent));

	AdvanceFor(Runtime, 0.25f);
	ASSERT_TRUE(Runtime.PollCompletedEvent(TrackEvent));
	EXPECT_EQ(TrackEvent.m_TrackId, 92u);
	ASSERT_TRUE(Runtime.PollGroupCompletedEvent(GroupEvent));
	EXPECT_EQ(GroupEvent.m_GroupId, GroupId);
	EXPECT_FALSE(Runtime.PollGroupCompletedEvent(GroupEvent));
}

TEST(UiV2Anim, AwaitTracksIgnoresZeroAndDuplicateTrackIds)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(12, EUiAnimProperty::ALPHA, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(12, EUiAnimProperty::ALPHA, 1.0f, 0.1f, 1, EUiAnimInterruptPolicy::REPLACE, 93)));
	const uint32_t aTrackIds[] = {0, 93, 93};
	const uint32_t GroupId = Runtime.AwaitTracks(aTrackIds, 3);
	EXPECT_NE(GroupId, 0u);

	AdvanceFor(Runtime, 0.15f);
	SUiAnimCompleteEvent TrackEvent;
	ASSERT_TRUE(Runtime.PollCompletedEvent(TrackEvent));
	EXPECT_EQ(TrackEvent.m_TrackId, 93u);

	SUiAnimGroupCompleteEvent GroupEvent;
	ASSERT_TRUE(Runtime.PollGroupCompletedEvent(GroupEvent));
	EXPECT_EQ(GroupEvent.m_GroupId, GroupId);
	EXPECT_FALSE(Runtime.PollGroupCompletedEvent(GroupEvent));
}

TEST(UiV2Anim, ReplacedAwaitedTrackDoesNotCompleteGroup)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(13, EUiAnimProperty::POS_X, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(13, EUiAnimProperty::POS_X, 10.0f, 0.5f, 1, EUiAnimInterruptPolicy::REPLACE, 94)));
	const uint32_t aTrackIds[] = {94};
	EXPECT_NE(Runtime.AwaitTracks(aTrackIds, 1), 0u);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(13, EUiAnimProperty::POS_X, 20.0f, 0.1f, 1, EUiAnimInterruptPolicy::REPLACE, 95)));
	AdvanceFor(Runtime, 0.2f);

	SUiAnimCompleteEvent TrackEvent;
	ASSERT_TRUE(Runtime.PollCompletedEvent(TrackEvent));
	EXPECT_EQ(TrackEvent.m_TrackId, 95u);
	SUiAnimGroupCompleteEvent GroupEvent;
	EXPECT_FALSE(Runtime.PollGroupCompletedEvent(GroupEvent));
}

TEST(UiV2Anim, SetValueCancelsAwaitedActiveAndQueuedTracks)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(18, EUiAnimProperty::POS_X, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(18, EUiAnimProperty::POS_X, 10.0f, 0.5f, 1, EUiAnimInterruptPolicy::REPLACE, 102)));
	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(18, EUiAnimProperty::POS_X, 20.0f, 0.5f, 1, EUiAnimInterruptPolicy::QUEUE, 103)));
	const uint32_t aCancelledTrackIds[] = {102, 103};
	EXPECT_NE(Runtime.AwaitTracks(aCancelledTrackIds, 2), 0u);

	Runtime.SetValue(18, EUiAnimProperty::POS_X, 5.0f);
	EXPECT_FALSE(Runtime.HasActiveAnimation(18, EUiAnimProperty::POS_X));
	EXPECT_EQ(Runtime.QueuedTrackCount(), 0);

	Runtime.SetValue(19, EUiAnimProperty::POS_X, 0.0f);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(19, EUiAnimProperty::POS_X, 1.0f, 0.0f, 1, EUiAnimInterruptPolicy::REPLACE, 102)));
	Runtime.SetValue(20, EUiAnimProperty::POS_X, 0.0f);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(20, EUiAnimProperty::POS_X, 1.0f, 0.0f, 1, EUiAnimInterruptPolicy::REPLACE, 103)));

	SUiAnimGroupCompleteEvent GroupEvent;
	EXPECT_FALSE(Runtime.PollGroupCompletedEvent(GroupEvent));
}

TEST(UiV2Anim, ReplacePolicyCancelsAwaitedQueuedTracks)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(21, EUiAnimProperty::POS_X, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(21, EUiAnimProperty::POS_X, 10.0f, 0.5f, 1, EUiAnimInterruptPolicy::REPLACE, 104)));
	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(21, EUiAnimProperty::POS_X, 20.0f, 0.5f, 1, EUiAnimInterruptPolicy::QUEUE, 105)));
	const uint32_t aQueuedTrackIds[] = {105};
	EXPECT_NE(Runtime.AwaitTracks(aQueuedTrackIds, 1), 0u);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(21, EUiAnimProperty::POS_X, 30.0f, 0.1f, 2, EUiAnimInterruptPolicy::REPLACE, 106)));
	EXPECT_EQ(Runtime.QueuedTrackCount(), 0);
	AdvanceFor(Runtime, 0.2f);

	Runtime.SetValue(22, EUiAnimProperty::POS_X, 0.0f);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(22, EUiAnimProperty::POS_X, 1.0f, 0.0f, 1, EUiAnimInterruptPolicy::REPLACE, 105)));

	SUiAnimGroupCompleteEvent GroupEvent;
	EXPECT_FALSE(Runtime.PollGroupCompletedEvent(GroupEvent));
}

TEST(UiV2Anim, CancellingOneAwaitedTrackCancelsWholeGroup)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(23, EUiAnimProperty::POS_X, 0.0f);
	Runtime.SetValue(23, EUiAnimProperty::POS_Y, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(23, EUiAnimProperty::POS_X, 10.0f, 0.5f, 1, EUiAnimInterruptPolicy::REPLACE, 107)));
	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(23, EUiAnimProperty::POS_Y, 20.0f, 0.2f, 1, EUiAnimInterruptPolicy::REPLACE, 108)));
	const uint32_t aTrackIds[] = {107, 108};
	EXPECT_NE(Runtime.AwaitTracks(aTrackIds, 2), 0u);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(23, EUiAnimProperty::POS_X, 30.0f, 0.1f, 2, EUiAnimInterruptPolicy::REPLACE, 109)));
	AdvanceFor(Runtime, 0.3f);

	SUiAnimGroupCompleteEvent GroupEvent;
	EXPECT_FALSE(Runtime.PollGroupCompletedEvent(GroupEvent));
}

TEST(UiV2Anim, AwaitTracksHandlesQueuedImmediateCompletion)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(14, EUiAnimProperty::ALPHA, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(14, EUiAnimProperty::ALPHA, 0.5f, 0.1f, 1, EUiAnimInterruptPolicy::QUEUE, 96)));
	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(14, EUiAnimProperty::ALPHA, 1.0f, 0.0f, 1, EUiAnimInterruptPolicy::QUEUE, 97)));
	const uint32_t aTrackIds[] = {97};
	const uint32_t GroupId = Runtime.AwaitTracks(aTrackIds, 1);
	EXPECT_NE(GroupId, 0u);

	AdvanceFor(Runtime, 0.15f);
	SUiAnimCompleteEvent TrackEvent;
	ASSERT_TRUE(Runtime.PollCompletedEvent(TrackEvent));
	EXPECT_EQ(TrackEvent.m_TrackId, 96u);
	ASSERT_TRUE(Runtime.PollCompletedEvent(TrackEvent));
	EXPECT_EQ(TrackEvent.m_TrackId, 97u);

	SUiAnimGroupCompleteEvent GroupEvent;
	ASSERT_TRUE(Runtime.PollGroupCompletedEvent(GroupEvent));
	EXPECT_EQ(GroupEvent.m_GroupId, GroupId);
}

TEST(UiV2Anim, AwaitTracksSupportsMultipleGroupsForSameTrack)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(15, EUiAnimProperty::ALPHA, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(15, EUiAnimProperty::ALPHA, 1.0f, 0.1f, 1, EUiAnimInterruptPolicy::REPLACE, 98)));
	const uint32_t aTrackIds[] = {98};
	const uint32_t GroupA = Runtime.AwaitTracks(aTrackIds, 1);
	const uint32_t GroupB = Runtime.AwaitTracks(aTrackIds, 1);
	EXPECT_NE(GroupA, 0u);
	EXPECT_NE(GroupB, 0u);
	EXPECT_NE(GroupA, GroupB);

	AdvanceFor(Runtime, 0.15f);
	SUiAnimCompleteEvent TrackEvent;
	ASSERT_TRUE(Runtime.PollCompletedEvent(TrackEvent));
	EXPECT_EQ(TrackEvent.m_TrackId, 98u);

	SUiAnimGroupCompleteEvent GroupEvent;
	ASSERT_TRUE(Runtime.PollGroupCompletedEvent(GroupEvent));
	EXPECT_EQ(GroupEvent.m_GroupId, GroupA);
	ASSERT_TRUE(Runtime.PollGroupCompletedEvent(GroupEvent));
	EXPECT_EQ(GroupEvent.m_GroupId, GroupB);
}

TEST(UiV2Anim, MergeTargetCancelsAwaitForReplacedTrackId)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(16, EUiAnimProperty::WIDTH, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(16, EUiAnimProperty::WIDTH, 10.0f, 0.3f, 1, EUiAnimInterruptPolicy::REPLACE, 99)));
	const uint32_t aTrackIds[] = {99};
	EXPECT_NE(Runtime.AwaitTracks(aTrackIds, 1), 0u);
	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(16, EUiAnimProperty::WIDTH, 20.0f, 0.3f, 1, EUiAnimInterruptPolicy::MERGE_TARGET, 100)));

	AdvanceFor(Runtime, 0.4f);
	SUiAnimCompleteEvent TrackEvent;
	ASSERT_TRUE(Runtime.PollCompletedEvent(TrackEvent));
	EXPECT_EQ(TrackEvent.m_TrackId, 100u);
	SUiAnimGroupCompleteEvent GroupEvent;
	EXPECT_FALSE(Runtime.PollGroupCompletedEvent(GroupEvent));
}

TEST(UiV2Anim, AwaitTracksRejectsAlreadyCompletedOrUnknownTrackIds)
{
	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(17, EUiAnimProperty::ALPHA, 0.0f);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(17, EUiAnimProperty::ALPHA, 1.0f, 0.0f, 1, EUiAnimInterruptPolicy::REPLACE, 101)));
	SUiAnimCompleteEvent TrackEvent;
	ASSERT_TRUE(Runtime.PollCompletedEvent(TrackEvent));
	EXPECT_EQ(TrackEvent.m_TrackId, 101u);

	const uint32_t aTrackIds[] = {101, 99999};
	EXPECT_EQ(Runtime.AwaitTracks(aTrackIds, 2), 0u);
}

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
}

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

	AdvanceFor(Runtime, 0.12f);
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
	g_Config.m_QmUiMotionLevel = 2;
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

	AdvanceFor(Runtime, 1.0f);
	Tree.BeginFrame();
	const float FinalAlpha = Tree.ResolvePresenceAlpha(Runtime, NodeKey, ui_token::motion::HOVER_FADE);
	EXPECT_NEAR(FinalAlpha, 1.0f, 0.001f);
	Tree.EndFrame(Runtime);
}

TEST(UiV2TreePresence, ExitCleansUpWhenHigherPriorityAlphaWasActive)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2Tree Tree;
	CUiV2AnimationRuntime Runtime;
	const uint64_t NodeKey = 1004;

	Tree.BeginFrame();
	Tree.ResolvePresenceAlpha(Runtime, NodeKey, ui_token::motion::HOVER_FADE);
	Tree.EndFrame(Runtime);

	EXPECT_TRUE(Runtime.RequestAnimation(MakeRequest(NodeKey, EUiAnimProperty::ALPHA, 1.0f, 0.2f, 5, EUiAnimInterruptPolicy::REPLACE, 110)));

	Tree.BeginFrame();
	Tree.EndFrame(Runtime);
	EXPECT_EQ(Tree.NodeCount(), 1);

	AdvanceFor(Runtime, 1.0f);
	Tree.BeginFrame();
	Tree.EndFrame(Runtime);
	EXPECT_EQ(Tree.NodeCount(), 0);
	EXPECT_FALSE(Runtime.HasActiveAnimation(NodeKey, EUiAnimProperty::ALPHA));
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

	EXPECT_EQ(SlowState.m_Calls, 0);
	EXPECT_GT(FastState.m_Calls, 0);
}

TEST(UiV2AnimEasing, MergeTargetRestartsTweenFromCurrentValue)
{
	struct SCustomEasingState
	{
		float m_Multiplier = 1.0f;
	};
	SCustomEasingState SlowState{0.25f};
	SCustomEasingState FastState{2.0f};
	auto CustomEase = [](float Progress, void *pUser) -> float {
		const SCustomEasingState *pState = static_cast<const SCustomEasingState *>(pUser);
		return std::min(1.0f, Progress * pState->m_Multiplier);
	};

	CUiV2AnimationRuntime Runtime;
	Runtime.SetValue(207, EUiAnimProperty::POS_X, 0.0f);
	Runtime.RegisterCustomEasing(11, CustomEase, &SlowState);
	Runtime.RegisterCustomEasing(12, CustomEase, &FastState);

	SUiAnimRequest Request = MakeTweenRequest(207, EUiAnimProperty::POS_X, 100.0f, 1.0f, EEasing::CUSTOM, 110);
	Request.m_Transition.m_CustomEasingId = 11;
	EXPECT_TRUE(Runtime.RequestAnimation(Request));
	AdvanceFor(Runtime, 0.2f);
	const float BeforeMerge = Runtime.GetValue(207, EUiAnimProperty::POS_X);

	Request.m_Target = 200.0f;
	Request.m_Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
	Request.m_Transition.m_CustomEasingId = 12;
	Request.m_TrackId = 111;
	EXPECT_TRUE(Runtime.RequestAnimation(Request));
	EXPECT_NEAR(Runtime.GetValue(207, EUiAnimProperty::POS_X), BeforeMerge, 1e-6f);

	Runtime.Advance(1.0f / 60.0f);
	const float AfterOneFrame = Runtime.GetValue(207, EUiAnimProperty::POS_X);
	EXPECT_GE(AfterOneFrame, BeforeMerge);
	EXPECT_LT(AfterOneFrame - BeforeMerge, 10.0f);
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

TEST(UiV2ScrollPhysics, WheelImpulseDecaysAndClampsToRange)
{
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 500.0f;

	SQmScrollConfig Config;
	Config.m_WheelScale = 1.0f;
	Config.m_NativeWheelStep = false;
	Config.m_MaxOverscroll = 72.0f;

	CQmScrollState State;
	State.AddWheelImpulse(-120.0f, Metrics, Config);
	EXPECT_GT(State.Offset(), 0.0f);
	EXPECT_GT(State.Velocity(), 0.0f);

	const float InitialOffset = State.Offset();
	State.Advance(0.2f, Metrics, Config);
	EXPECT_GT(State.Offset(), InitialOffset);
	EXPECT_LT(State.Offset(), Metrics.MaxOffset());
	EXPECT_GT(State.Velocity(), 0.0f);

	for(int i = 0; i < 240; ++i)
		State.Advance(1.0f / 60.0f, Metrics, Config);
	EXPECT_GE(State.Offset(), 0.0f);
	EXPECT_LE(State.Offset(), Metrics.MaxOffset());
	EXPECT_NEAR(State.Velocity(), 0.0f, 0.5f);
}

TEST(UiV2ScrollPhysics, NativeWheelStepMatchesDdnetScrollUnit)
{
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 500.0f;
	SQmScrollConfig Config;
	Config.m_WheelScale = 10.0f;
	Config.m_NativeWheelStep = true;
	Config.m_NativeWheelAnimationTime = 0.5f;

	CQmScrollState State;
	State.AddWheelImpulse(-120.0f, Metrics, Config);

	EXPECT_NEAR(State.Offset(), 0.0f, 0.01f);
	EXPECT_NEAR(State.Velocity(), 0.0f, 0.01f);

	State.Advance(1.0f / 60.0f, Metrics, Config);
	EXPECT_GT(State.Offset(), 0.0f);
	EXPECT_LT(State.Offset(), 10.0f);
	EXPECT_NEAR(State.Velocity(), 0.0f, 0.01f);

	for(int i = 0; i < 40; ++i)
		State.Advance(1.0f / 60.0f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), 10.0f, 0.01f);
	EXPECT_NEAR(State.Velocity(), 0.0f, 0.01f);

	State.AddWheelImpulse(-120.0f, Metrics, Config);
	for(int i = 0; i < 40; ++i)
		State.Advance(1.0f / 60.0f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), 20.0f, 0.01f);
	EXPECT_NEAR(State.Velocity(), 0.0f, 0.01f);
}

TEST(UiV2ScrollPhysics, NativeWheelStepPreservesWheelMagnitude)
{
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 500.0f;
	const SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, 0.0f);

	CQmScrollState State;
	State.AddWheelImpulse(-360.0f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), 30.0f, 0.01f);

	State.AddWheelImpulse(240.0f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), 10.0f, 0.01f);
}

TEST(UiV2WheelOwnership, AltMagnitudeReachesNativeScrollStateOnce)
{
	CScrollWheelOwnership Router;
	ASSERT_TRUE(Router.BeginFrame(41, -120.0f, true));
	CQmScrollState State;
	Router.Register(&State, EUiWheelOwnerPriority::PAGE, true);
	float Delta = 0.0f;
	ASSERT_TRUE(Router.TryConsume(&State, &Delta));
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 500.0f;
	State.AddWheelImpulse(Delta, Metrics, QmNativeWheelScrollConfig(1.0f, 0.0f));
	EXPECT_NEAR(State.Offset(), 30.0f, 0.01f);
	EXPECT_FALSE(Router.TryConsume(&State, &Delta));
}

TEST(UiV2ScrollPhysics, NativeWheelAnimationMatchesScrollRegionEaseOut)
{
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 500.0f;
	SQmScrollConfig Config;
	Config.m_WheelScale = 10.0f;
	Config.m_NativeWheelStep = true;
	Config.m_NativeWheelAnimationTime = 0.5f;

	CQmScrollState State;
	State.AddWheelImpulse(-120.0f, Metrics, Config);
	State.Advance(0.125f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), 5.78125f, 0.001f);

	State.Advance(0.125f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), 8.75f, 0.001f);

	State.Advance(0.25f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), 10.0f, 0.001f);

	State.AddWheelImpulse(-120.0f, Metrics, Config);
	State.Advance(0.25f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), 18.75f, 0.001f);
}

TEST(UiV2ScrollPhysics, NativeWheelAnimationPausesWhileModifierIsPressed)
{
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 500.0f;
	SQmScrollConfig Config;
	Config.m_WheelScale = 10.0f;
	Config.m_NativeWheelStep = true;
	Config.m_NativeWheelAnimationTime = 0.5f;

	CQmScrollState State;
	State.AddWheelImpulse(-120.0f, Metrics, Config);
	State.Advance(0.125f, Metrics, Config);
	const float OffsetBeforeModifier = State.Offset();

	State.Advance(0.25f, Metrics, Config, true);
	EXPECT_NEAR(State.Offset(), OffsetBeforeModifier, 0.001f);

	State.Advance(0.125f, Metrics, Config);
	State.Advance(0.25f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), 10.0f, 0.001f);
}

TEST(UiV2ScrollPhysics, NativeWheelAnimationConsumesLargeFrameDeltaLikeScrollRegion)
{
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 500.0f;
	SQmScrollConfig Config;
	Config.m_WheelScale = 10.0f;
	Config.m_NativeWheelStep = true;
	Config.m_NativeWheelAnimationTime = 0.5f;

	CQmScrollState State;
	State.AddWheelImpulse(-120.0f, Metrics, Config);
	State.Advance(0.5f, Metrics, Config);

	EXPECT_NEAR(State.Offset(), 10.0f, 0.001f);
	EXPECT_NEAR(State.Velocity(), 0.0f, 0.01f);
}

TEST(UiV2ScrollContainer, ModifierDoesNotGloballySuppressWheelAndAltAcceleratesIt)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	Input.m_ModifierPressed = true;
	Input.m_WheelDelta = -120.0f;
	const SQmScrollContainerFrame ModifierFrame = Container.Update(State, View, 300.0f, 1.0f / 60.0f, Input);
	EXPECT_NEAR(ModifierFrame.m_Offset, 10.0f, 0.001f);

	Input.m_ModifierPressed = false;
	Input.m_AltPressed = true;
	const SQmScrollContainerFrame WheelFrame = Container.Update(State, View, 300.0f, 0.5f, Input);
	EXPECT_NEAR(WheelFrame.m_Offset, 40.0f, 0.001f);

	Input.m_WheelDelta = 0.0f;
	const SQmScrollContainerFrame DoneFrame = Container.Update(State, View, 300.0f, 0.5f, Input);
	EXPECT_NEAR(DoneFrame.m_Offset, 40.0f, 0.001f);
}

TEST(UiV2ScrollOwnership, PopupConsumesWheelWithoutLeakingToUnderlyingRegion)
{
	EXPECT_FALSE(QmScrollRegionCanConsumeWheel(true, false, true, false));
	EXPECT_TRUE(QmScrollRegionCanConsumeWheel(false, true, true, true));
	EXPECT_TRUE(QmScrollRegionCanConsumeWheel(false, true, false, false));
	EXPECT_TRUE(QmScrollRegionCanConsumeWheel(true, false, false, false));
}

TEST(UiV2WheelOwnership, HighestEligibleOwnerConsumesRawWheelOnce)
{
	CScrollWheelOwnership Router;
	ASSERT_TRUE(Router.BeginFrame(41, -120.0f, false));
	Router.Register(reinterpret_cast<void *>(1), EUiWheelOwnerPriority::PAGE, true);
	Router.Register(reinterpret_cast<void *>(2), EUiWheelOwnerPriority::POPUP, true);
	float Delta = 0.0f;
	EXPECT_FALSE(Router.TryConsume(reinterpret_cast<void *>(1), &Delta));
	EXPECT_TRUE(Router.TryConsume(reinterpret_cast<void *>(2), &Delta));
	EXPECT_FLOAT_EQ(Delta, -120.0f);
	EXPECT_FALSE(Router.TryConsume(reinterpret_cast<void *>(2), &Delta));
}

TEST(UiV2WheelOwnership, AltAcceleratesButOtherModifiersDoNotDiscardWheel)
{
	CScrollWheelOwnership Router;
	ASSERT_TRUE(Router.BeginFrame(41, -120.0f, true));
	Router.Register(reinterpret_cast<void *>(1), EUiWheelOwnerPriority::PAGE, true);
	float Delta = 0.0f;
	ASSERT_TRUE(Router.TryConsume(reinterpret_cast<void *>(1), &Delta));
	EXPECT_FLOAT_EQ(Delta, -360.0f);
}

TEST(UiV2WheelOwnership, LaterEqualPriorityOwnerWinsAndIneligibleOwnerCannotWin)
{
	CScrollWheelOwnership Router;
	int Outer = 0;
	int Inner = 0;
	int Disabled = 0;
	ASSERT_TRUE(Router.BeginFrame(41, 120.0f, false));
	Router.Register(&Outer, EUiWheelOwnerPriority::COMPOSITE_CONTROL, true);
	Router.Register(&Disabled, EUiWheelOwnerPriority::POPUP, false);
	Router.Register(&Inner, EUiWheelOwnerPriority::COMPOSITE_CONTROL, true);
	float Delta = 0.0f;
	EXPECT_FALSE(Router.TryConsume(&Outer, &Delta));
	EXPECT_FALSE(Router.TryConsume(&Disabled, &Delta));
	EXPECT_TRUE(Router.TryConsume(&Inner, &Delta));
}

TEST(UiV2WheelOwnership, BeginFrameIsIdempotentForOneProductionFrame)
{
	CScrollWheelOwnership Router;
	int Popup = 0;
	ASSERT_TRUE(Router.BeginFrame(41, -120.0f, false));
	Router.Register(&Popup, EUiWheelOwnerPriority::POPUP, true);
	EXPECT_FALSE(Router.BeginFrame(41, 120.0f, true));
	float Delta = 0.0f;
	ASSERT_TRUE(Router.TryConsume(&Popup, &Delta));
	EXPECT_FLOAT_EQ(Delta, -120.0f);
	EXPECT_TRUE(Router.BeginFrame(42, 120.0f, false));
	EXPECT_FALSE(Router.TryConsume(&Popup, &Delta));
}
TEST(UiV2WheelOwnership, CandidateOutsideHotRectCannotConsumeWheel)
{
	CScrollWheelOwnership Router;
	int Owner = 0;
	ASSERT_TRUE(Router.BeginFrame(41, -120.0f, false));
	QmRegisterWheelOwnerCandidate(Router, {&Owner, EUiWheelOwnerPriority::PAGE, {10.0f, 10.0f, 30.0f, 30.0f}, true}, {50.0f, 50.0f}, true);
	float Delta = 0.0f;
	EXPECT_FALSE(QmTryConsumeWheel(Router, &Owner, &Delta));
}
TEST(UiV2ScrollPolicy, ListBoxExplicitScrollbarMetricsOverridePolicyDefaults)
{
	EXPECT_NEAR(QmListBoxScrollbarMetric(20.0f, 15.0f, false), 20.0f, 0.001f);
	EXPECT_NEAR(QmListBoxScrollbarMetric(20.0f, 15.0f, true), 15.0f, 0.001f);
}

TEST(UiV2ScrollPolicy, ScrollbarReservationPreventsNestedListWidthJitter)
{
	EXPECT_FALSE(QmScrollRegionShouldReserveScrollbarSpace(false, false));
	EXPECT_TRUE(QmScrollRegionShouldReserveScrollbarSpace(false, true));
	EXPECT_TRUE(QmScrollRegionShouldReserveScrollbarSpace(true, false));
	EXPECT_TRUE(QmScrollRegionShouldReserveScrollbarSpace(true, true));
}

TEST(UiV2ScrollPolicy, ScrollbarOverflowIgnoresSubpixelLayoutRemainder)
{
	EXPECT_FALSE(QmScrollRegionContentOverflows(100.0f, 100.0f, 0.5f));
	EXPECT_FALSE(QmScrollRegionContentOverflows(100.125f, 100.0f, 0.5f));
	EXPECT_TRUE(QmScrollRegionContentOverflows(100.126f, 100.0f, 0.5f));
	EXPECT_FALSE(QmScrollRegionContentOverflows(100.0625f, 100.0f, 0.25f));
	EXPECT_TRUE(QmScrollRegionContentOverflows(100.0626f, 100.0f, 0.25f));
}

TEST(UiV2ScrollPolicy, ListBoxInitialSelectionRequestsOneAnimatedReveal)
{
	EXPECT_TRUE(QmListBoxShouldScrollToInitialSelection(true, 4));
	EXPECT_FALSE(QmListBoxShouldScrollToInitialSelection(true, -1));
	EXPECT_FALSE(QmListBoxShouldScrollToInitialSelection(false, 4));
	EXPECT_TRUE(QmListBoxInitialScrollRemainsPending(true, -1));
	EXPECT_FALSE(QmListBoxInitialScrollRemainsPending(true, 4));
	EXPECT_FALSE(QmListBoxInitialScrollRemainsPending(false, -1));
}

TEST(UiV2ScrollPolicy, ListBoxEntryAnimationStartsAfterAnInactiveGap)
{
	EXPECT_TRUE(QmListBoxShouldStartEntryAnimation(true, false, 0, 1000, 400));
	EXPECT_TRUE(QmListBoxShouldStartEntryAnimation(true, false, 500, 1000, 400));
	EXPECT_FALSE(QmListBoxShouldStartEntryAnimation(true, false, 600, 1000, 400));
	EXPECT_FALSE(QmListBoxShouldStartEntryAnimation(false, false, 0, 1000, 400));
	EXPECT_FALSE(QmListBoxShouldStartEntryAnimation(true, true, 0, 1000, 400));
	EXPECT_NEAR(QmListBoxEntryOffset(0.0f, 0.16f, 12.0f), -12.0f, 0.001f);
	EXPECT_LT(QmListBoxEntryOffset(0.08f, 0.16f, 12.0f), 0.0f);
	EXPECT_NEAR(QmListBoxEntryOffset(0.16f, 0.16f, 12.0f), 0.0f, 0.001f);
	EXPECT_FALSE(QmListBoxEntryAnimationFinished(true, false, 0.08f, 0.16f));
	EXPECT_TRUE(QmListBoxEntryAnimationFinished(true, false, 0.16f, 0.16f));
	EXPECT_TRUE(QmListBoxEntryAnimationFinished(true, false, 0.20f, 0.16f));
	EXPECT_TRUE(QmListBoxEntryAnimationFinished(false, false, 0.0f, 0.16f));
	EXPECT_FALSE(QmListBoxEntryAnimationFinished(true, true, 0.20f, 0.16f));
	const CUIRect BaseRect{10.0f, 20.0f, 100.0f, 18.0f};
	const CUIRect AnimatedRect = QmListBoxEntryAnimatedRect(BaseRect, -12.0f);
	EXPECT_FLOAT_EQ(AnimatedRect.x, BaseRect.x);
	EXPECT_FLOAT_EQ(AnimatedRect.y, 8.0f);
	EXPECT_FLOAT_EQ(AnimatedRect.w, BaseRect.w);
	EXPECT_FLOAT_EQ(AnimatedRect.h, BaseRect.h);
}

TEST(UiV2ScrollPolicy, ResolvesSharedVisualAndInteractionProfiles)
{
	SQmScrollRequest Settings;
	Settings.m_Profile = EQmScrollProfile::SETTINGS_OUTER;
	const SQmResolvedScrollPolicy SettingsPolicy = QmResolveScrollPolicy(Settings, 1.0f, 0.5f);
	EXPECT_NEAR(SettingsPolicy.m_Style.m_ScrollbarWidth, 20.0f, 0.01f);
	EXPECT_NEAR(SettingsPolicy.m_Style.m_ScrollbarMargin, 5.0f, 0.01f);
	EXPECT_TRUE(SettingsPolicy.m_Style.m_ReserveScrollbarSpace);
	EXPECT_TRUE(SettingsPolicy.m_ScrollbarAlwaysReserved);
	EXPECT_NEAR(SettingsPolicy.m_Config.m_WheelScale, 120.0f, 0.01f);
	EXPECT_NEAR(SettingsPolicy.m_AltMultiplier, 3.0f, 0.01f);
	EXPECT_EQ(SettingsPolicy.m_RailVisibility, EQmScrollRailVisibility::AUTO);

	SQmScrollRequest FilterGrid;
	FilterGrid.m_Profile = EQmScrollProfile::FILTER_GRID;
	FilterGrid.m_RowExtent = 18.0f;
	const SQmResolvedScrollPolicy FilterPolicy = QmResolveScrollPolicy(FilterGrid, 1.0f, 0.0f);
	EXPECT_EQ(FilterPolicy.m_RailVisibility, EQmScrollRailVisibility::HIDDEN);
	EXPECT_NEAR(FilterPolicy.m_Config.m_WheelScale, 36.0f, 0.01f);
	EXPECT_FALSE(FilterPolicy.m_ContentDragAllowed);

	SQmScrollRequest Grid;
	Grid.m_Profile = EQmScrollProfile::SETTINGS_GRID;
	Grid.m_RowExtent = 18.0f;
	Grid.m_RowsPerStep = 1;
	const SQmResolvedScrollPolicy GridPolicy = QmResolveScrollPolicy(Grid, 1.0f, 0.0f);
	EXPECT_EQ(GridPolicy.m_RailVisibility, EQmScrollRailVisibility::AUTO);
	EXPECT_NEAR(GridPolicy.m_Config.m_WheelScale, 18.0f, 0.01f);
	EXPECT_FALSE(GridPolicy.m_ContentDragAllowed);

	SQmScrollRequest Popup;
	Popup.m_Profile = EQmScrollProfile::POPUP_LIST;
	Popup.m_RowExtent = 20.0f;
	const SQmResolvedScrollPolicy PopupPolicy = QmResolveScrollPolicy(Popup, 1.0f, 0.0f);
	EXPECT_EQ(PopupPolicy.m_MaxVisibleItems, QM_POPUP_LIST_MAX_VISIBLE_ITEMS);
	EXPECT_NEAR(PopupPolicy.m_Config.m_WheelScale, 60.0f, 0.01f);
}

TEST(UiV2ScrollController, SettingsGridShowsRailOnlyWhenContentOverflows)
{
	const SQmResolvedScrollPolicy Policy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_GRID, EQmScrollAxis::VERTICAL, 40.0f, 2});
	CQmScrollController Controller;
	CQmScrollState State;
	const CUIRect View{0.0f, 0.0f, 200.0f, 100.0f};
	const SQmScrollContainerFrame Fits = Controller.PreviewFrame(State, View, 100.0f, Policy.m_Style);
	EXPECT_FALSE(Fits.m_Scrollable);
	EXPECT_FALSE(Fits.m_ScrollbarVisible);
	EXPECT_FLOAT_EQ(Fits.m_ClipRect.w, View.w);

	const SQmScrollContainerFrame Overflows = Controller.PreviewFrame(State, View, 101.0f, Policy.m_Style);
	EXPECT_TRUE(Overflows.m_Scrollable);
	EXPECT_TRUE(Overflows.m_ScrollbarVisible);
	EXPECT_LT(Overflows.m_ClipRect.w, View.w);
	EXPECT_EQ(Policy.m_RailVisibility, EQmScrollRailVisibility::AUTO);
	EXPECT_FALSE(Policy.m_ContentDragAllowed);
}

TEST(UiV2ScrollController, SettingsOuterReservesSlotBeforeOverflowIsKnown)
{
	const SQmResolvedScrollPolicy Policy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER});
	CQmScrollController Controller;
	CQmScrollState State;
	const SQmScrollContainerFrame Frame = Controller.PreviewFrame(State, {0.0f, 0.0f, 200.0f, 100.0f}, 100.0f, Policy.m_Style);
	EXPECT_FALSE(Frame.m_Scrollable);
	EXPECT_FALSE(Frame.m_ScrollbarVisible);
	EXPECT_FLOAT_EQ(Frame.m_ClipRect.w, 180.0f);
}

TEST(UiV2ScrollPolicy, NonCardMenuListAndFilterGridUseResolvedSteps)
{
	SQmScrollRequest ListRequest;
	ListRequest.m_Profile = EQmScrollProfile::MENU_LIST;
	ListRequest.m_RowExtent = 24.0f;
	ListRequest.m_RowsPerStep = 3;
	const SQmResolvedScrollPolicy List = QmResolveScrollPolicy(ListRequest, 1.0f, 0.0f);
	EXPECT_FLOAT_EQ(List.m_Config.m_WheelScale, 72.0f);
	EXPECT_EQ(List.m_RailVisibility, EQmScrollRailVisibility::AUTO);
	EXPECT_FLOAT_EQ(List.m_AltMultiplier, 3.0f);

	SQmScrollRequest GridRequest;
	GridRequest.m_Profile = EQmScrollProfile::FILTER_GRID;
	GridRequest.m_RowExtent = 30.0f;
	GridRequest.m_RowsPerStep = 2;
	const SQmResolvedScrollPolicy Grid = QmResolveScrollPolicy(GridRequest, 1.0f, 0.0f);
	EXPECT_FLOAT_EQ(Grid.m_Config.m_WheelScale, 60.0f);
	EXPECT_EQ(Grid.m_RailVisibility, EQmScrollRailVisibility::HIDDEN);
	EXPECT_FALSE(Grid.m_ContentDragAllowed);
}

TEST(UiV2ScrollPolicy, FinalPresetMatrixCoversLargeMediumSmallAndHorizontal)
{
	const SQmResolvedScrollPolicy Outer = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER, EQmScrollAxis::VERTICAL, 0.0f, 0}, 0.78f, 0.12f);
	const SQmResolvedScrollPolicy Medium = QmResolveScrollPolicy({EQmScrollProfile::MENU_LIST, EQmScrollAxis::VERTICAL, 24.0f, 3}, 0.78f, 0.0f);
	const SQmResolvedScrollPolicy Small = QmResolveScrollPolicy({EQmScrollProfile::POPUP_LIST, EQmScrollAxis::VERTICAL, 24.0f, 1}, 1.0f, 0.0f);
	const SQmResolvedScrollPolicy Horizontal = QmResolveScrollPolicy({EQmScrollProfile::POPUP_LIST, EQmScrollAxis::HORIZONTAL, 24.0f, 1}, 1.0f, 0.0f);

	EXPECT_FLOAT_EQ(Outer.m_Style.m_ScrollbarWidth, 20.0f);
	EXPECT_FLOAT_EQ(Outer.m_Style.m_ScrollbarMargin, 5.0f);
	EXPECT_GT(Outer.m_Style.m_ScrollbarWidth, Medium.m_Style.m_ScrollbarWidth);
	EXPECT_GT(Medium.m_Style.m_ScrollbarWidth, Small.m_Style.m_ScrollbarWidth);
	EXPECT_EQ(Horizontal.m_Style.m_Axis, EQmScrollAxis::HORIZONTAL);
	EXPECT_EQ(Small.m_MaxVisibleItems, 8);
	EXPECT_FLOAT_EQ(Medium.m_AltMultiplier, 3.0f);
}

TEST(UiV2ScrollController, HiddenRailKeepsScrollableContentAtFullWidth)
{
	CQmScrollState State;
	CQmScrollController Controller;
	SQmScrollRequest Request;
	Request.m_Profile = EQmScrollProfile::FILTER_GRID;
	Request.m_RowExtent = 20.0f;
	CUIRect View{10.0f, 20.0f, 200.0f, 100.0f};
	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	Input.m_WheelDelta = -120.0f;
	const SQmScrollContainerFrame Frame = Controller.Update(State, View, 300.0f, 0.0f, Input, Request, 1.0f, 0.0f);
	EXPECT_TRUE(Frame.m_Scrollable);
	EXPECT_FALSE(Frame.m_ScrollbarVisible);
	EXPECT_NEAR(Frame.m_ClipRect.w, View.w, 0.001f);
	EXPECT_GT(Frame.m_Offset, 0.0f);
}

TEST(UiV2ScrollPhysics, PresetsExposeSharedSmallMediumLargeGeometry)
{
	const SQmScrollContainerStyle Small = QmScrollContainerStyleForSize(EQmScrollSize::SMALL, 1.0f);
	EXPECT_NEAR(Small.m_ScrollbarWidth, 10.0f, 0.01f);
	EXPECT_NEAR(Small.m_ScrollbarMargin, 2.0f, 0.01f);
	EXPECT_NEAR(Small.m_MinThumbHeight, 36.0f, 0.01f);

	const SQmScrollContainerStyle Medium = QmScrollContainerStyleForSize(EQmScrollSize::MEDIUM, 1.0f);
	EXPECT_NEAR(Medium.m_ScrollbarWidth, 20.0f, 0.01f);
	EXPECT_NEAR(Medium.m_ScrollbarMargin, 5.0f, 0.01f);
	EXPECT_NEAR(Medium.m_MinThumbHeight, 42.0f, 0.01f);

	const SQmScrollContainerStyle Large = QmScrollContainerStyleForSize(EQmScrollSize::LARGE, 1.0f);
	EXPECT_NEAR(Large.m_ScrollbarWidth, 28.0f, 0.01f);
	EXPECT_NEAR(Large.m_ScrollbarMargin, 8.0f, 0.01f);
	EXPECT_NEAR(Large.m_MinThumbHeight, 48.0f, 0.01f);

	const SQmScrollConfig NativeWheel = QmNativeWheelScrollConfig(1.0f, 0.5f);
	EXPECT_NEAR(NativeWheel.m_WheelScale, 10.0f, 0.01f);
	EXPECT_TRUE(NativeWheel.m_NativeWheelStep);
	EXPECT_NEAR(NativeWheel.m_NativeWheelAnimationTime, 0.5f, 0.01f);
	EXPECT_NEAR(NativeWheel.m_MaxOverscroll, 0.0f, 0.01f);

	const SQmScrollConfig ScaledNativeWheel = QmNativeWheelScrollConfig(2.0f, 0.5f);
	EXPECT_NEAR(ScaledNativeWheel.m_WheelScale, 10.0f, 0.01f);

	const SQmScrollConfig InstantNativeWheel = QmNativeWheelScrollConfig(1.0f, 0.0f);
	EXPECT_NEAR(InstantNativeWheel.m_NativeWheelAnimationTime, 0.0f, 0.01f);

	const SQmScrollConfig SettingsWheel = QmSettingsScrollConfig(1.0f, 0.5f);
	EXPECT_NEAR(SettingsWheel.m_WheelScale, 120.0f, 0.01f);
	EXPECT_TRUE(SettingsWheel.m_NativeWheelStep);
	EXPECT_NEAR(SettingsWheel.m_NativeWheelAnimationTime, 0.5f, 0.01f);
}

TEST(UiV2ScrollPhysics, ScrollRegionParamsUseSharedQmScrollPreset)
{
	const SQmScrollContainerStyle Medium = QmScrollContainerStyleForSize(EQmScrollSize::MEDIUM, 1.0f);
	const CScrollRegionParams Params = QmScrollRegionParamsForSize(EQmScrollSize::MEDIUM, 1.0f);
	const CScrollRegionParams DefaultParams;

	EXPECT_NEAR(Params.m_ScrollbarThickness, Medium.m_ScrollbarWidth, 0.01f);
	EXPECT_NEAR(Params.m_ScrollbarMargin, Medium.m_ScrollbarMargin, 0.01f);
	EXPECT_NEAR(Params.m_SliderMinSize, Medium.m_MinThumbHeight, 0.01f);
	EXPECT_NEAR(Params.m_ScrollUnit, QmNativeWheelScrollConfig(1.0f, 0.0f).m_WheelScale, 0.01f);
	EXPECT_FALSE(Params.m_ScrollHorizontal);
	EXPECT_NEAR(DefaultParams.m_ScrollbarThickness, Medium.m_ScrollbarWidth, 0.01f);
	EXPECT_NEAR(DefaultParams.m_ScrollbarMargin, Medium.m_ScrollbarMargin, 0.01f);
	EXPECT_NEAR(DefaultParams.m_SliderMinSize, 25.0f, 0.01f);
	EXPECT_NEAR(DefaultParams.m_ScrollUnit, QmNativeWheelScrollConfig(1.0f, 0.0f).m_WheelScale, 0.01f);

	const CScrollRegionParams Horizontal = QmScrollRegionParamsForSize(EQmScrollSize::SMALL, 1.0f, EQmScrollAxis::HORIZONTAL);
	EXPECT_TRUE(Horizontal.m_ScrollHorizontal);
	EXPECT_NEAR(Horizontal.m_ScrollbarThickness, QmScrollContainerStyleForSize(EQmScrollSize::SMALL, 1.0f).m_ScrollbarWidth, 0.01f);
}

TEST(UiV2ScrollPhysics, OverscrollSpringsBackIntoRange)
{
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 300.0f;
	SQmScrollConfig Config;
	Config.m_NativeWheelStep = false;
	Config.m_MaxOverscroll = 72.0f;

	CQmScrollState State;
	State.SetOffset(Metrics.MaxOffset() + 40.0f, Metrics, Config, true);

	State.Advance(0.1f, Metrics, Config);
	EXPECT_GT(State.Offset(), Metrics.MaxOffset());
	EXPECT_LT(State.Offset(), Metrics.MaxOffset() + 40.0f);

	for(int i = 0; i < 240; ++i)
		State.Advance(1.0f / 60.0f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), Metrics.MaxOffset(), 0.75f);
	EXPECT_NEAR(State.Velocity(), 0.0f, 0.75f);
}

TEST(UiV2ScrollPhysics, NonScrollableContentResetsState)
{
	SQmScrollMetrics ScrollableMetrics;
	ScrollableMetrics.m_ViewportSize = 100.0f;
	ScrollableMetrics.m_ContentSize = 500.0f;
	SQmScrollMetrics NonScrollableMetrics;
	NonScrollableMetrics.m_ViewportSize = 300.0f;
	NonScrollableMetrics.m_ContentSize = 120.0f;
	SQmScrollConfig Config;
	Config.m_WheelScale = 1.0f;
	Config.m_NativeWheelStep = false;
	Config.m_MaxOverscroll = 72.0f;

	CQmScrollState State;
	State.SetOffset(80.0f, ScrollableMetrics);
	State.AddWheelImpulse(-120.0f, ScrollableMetrics, Config);
	EXPECT_GT(State.Offset(), 0.0f);
	EXPECT_GT(State.Velocity(), 0.0f);

	State.Advance(0.0f, NonScrollableMetrics, Config);
	EXPECT_NEAR(State.Offset(), 0.0f, 1e-6f);
	EXPECT_NEAR(State.Velocity(), 0.0f, 1e-6f);

	State.SetOffset(80.0f, ScrollableMetrics);
	State.AddWheelImpulse(-120.0f, ScrollableMetrics, Config);
	EXPECT_GT(State.Offset(), 0.0f);
	EXPECT_GT(State.Velocity(), 0.0f);

	State.Advance(0.2f, NonScrollableMetrics, Config);
	EXPECT_NEAR(State.Offset(), 0.0f, 1e-6f);
	EXPECT_NEAR(State.Velocity(), 0.0f, 1e-6f);
}

TEST(UiV2ScrollPhysics, ShrinkingScrollableContentClampsOffsetToNewRange)
{
	SQmScrollMetrics TallMetrics;
	TallMetrics.m_ViewportSize = 100.0f;
	TallMetrics.m_ContentSize = 500.0f;
	SQmScrollMetrics ShortMetrics;
	ShortMetrics.m_ViewportSize = 100.0f;
	ShortMetrics.m_ContentSize = 220.0f;

	CQmScrollState State;
	State.SetOffset(400.0f, TallMetrics);
	EXPECT_NEAR(State.Offset(), TallMetrics.MaxOffset(), 1e-6f);

	State.Advance(0.0f, ShortMetrics);
	EXPECT_NEAR(State.Offset(), ShortMetrics.MaxOffset(), 1e-6f);
	EXPECT_NEAR(State.Velocity(), 0.0f, 1e-6f);

	State.SetOffset(400.0f, TallMetrics);
	State.AddWheelImpulse(-120.0f, TallMetrics);
	EXPECT_GT(State.Offset(), ShortMetrics.MaxOffset());

	State.Advance(1.0f / 60.0f, ShortMetrics);
	EXPECT_LE(State.Offset(), ShortMetrics.MaxOffset());
}

TEST(UiV2ScrollContainer, ComputesContentRectAndScrollbarVisibility)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	Container.ScrollByWheel(State, -120.0f, View.h, 300.0f);
	const SQmScrollContainerFrame Frame = Container.Update(State, View, 300.0f, 1.0f / 60.0f);

	EXPECT_TRUE(Frame.m_ScrollbarVisible);
	EXPECT_NEAR(Frame.m_ClipRect.x, View.x, 1e-6f);
	EXPECT_NEAR(Frame.m_ClipRect.y, View.y, 1e-6f);
	EXPECT_NEAR(Frame.m_ClipRect.w, View.w - SQmScrollContainerStyle().m_ScrollbarWidth, 1e-6f);
	EXPECT_NEAR(Frame.m_ClipRect.h, View.h, 1e-6f);
	EXPECT_NEAR(Frame.m_ContentRect.x, View.x, 1e-6f);
	EXPECT_LT(Frame.m_ContentRect.y, View.y);
	EXPECT_NEAR(Frame.m_ContentRect.w, View.w - SQmScrollContainerStyle().m_ScrollbarWidth, 1e-6f);
	EXPECT_NEAR(Frame.m_ContentRect.h, 300.0f, 1e-6f);
	EXPECT_GT(Frame.m_Offset, 0.0f);

	State.Reset();
	const SQmScrollContainerFrame NonOverflowFrame = Container.Update(State, View, 80.0f, 0.0f);
	EXPECT_FALSE(NonOverflowFrame.m_ScrollbarVisible);
	EXPECT_NEAR(NonOverflowFrame.m_ClipRect.w, View.w, 1e-6f);
}

TEST(UiV2ScrollContainer, DefaultWheelInputUsesDdnetNativeStep)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	Container.ScrollByWheel(State, -120.0f, View.h, 300.0f);
	const SQmScrollContainerFrame FirstFrame = Container.Update(State, View, 300.0f, 0.0f);
	EXPECT_NEAR(FirstFrame.m_Offset, 10.0f, 0.001f);
	EXPECT_NEAR(State.Velocity(), 0.0f, 0.001f);
}

TEST(UiV2ScrollContainer, ExplicitDdnetSmoothTimeUsesEaseOutStep)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;
	const SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, 0.5f);

	Container.ScrollByWheel(State, -120.0f, View.h, 300.0f, Config);
	const SQmScrollContainerFrame FirstFrame = Container.Update(State, View, 300.0f, 0.0f, Config);
	EXPECT_NEAR(FirstFrame.m_Offset, 0.0f, 0.001f);

	const SQmScrollContainerFrame HalfFrame = Container.Update(State, View, 300.0f, 0.25f, Config);
	EXPECT_NEAR(HalfFrame.m_Offset, 8.75f, 0.001f);

	const SQmScrollContainerFrame DoneFrame = Container.Update(State, View, 300.0f, 0.25f, Config);
	EXPECT_NEAR(DoneFrame.m_Offset, 10.0f, 0.001f);

	Container.ScrollByWheel(State, -360.0f, View.h, 300.0f, Config);
	Container.Update(State, View, 300.0f, 0.5f, Config);
	Container.Update(State, View, 300.0f, 0.125f, Config);
	Container.Update(State, View, 300.0f, 0.25f, Config);
	Container.Update(State, View, 300.0f, 0.25f, Config);
	EXPECT_NEAR(State.Offset(), 40.0f, 0.001f);
	EXPECT_NEAR(State.Velocity(), 0.0f, 0.001f);
}

TEST(UiV2ScrollContainer, NonScrollableContentKeepsContentAtViewOrigin)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 4.0f;
	View.y = 8.0f;
	View.w = 180.0f;
	View.h = 120.0f;

	const SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, 0.0f);
	Container.ScrollByWheel(State, -120.0f, View.h, 400.0f, Config);
	EXPECT_GT(State.Offset(), 0.0f);

	const SQmScrollContainerFrame Frame = Container.Update(State, View, 80.0f, 0.0f, Config);
	EXPECT_FALSE(Frame.m_ScrollbarVisible);
	EXPECT_NEAR(Frame.m_Offset, 0.0f, 1e-6f);
	EXPECT_NEAR(Frame.m_ContentRect.x, View.x, 1e-6f);
	EXPECT_NEAR(Frame.m_ContentRect.y, View.y, 1e-6f);
	EXPECT_NEAR(Frame.m_ContentRect.w, View.w, 1e-6f);
	EXPECT_NEAR(Frame.m_ContentRect.h, 80.0f, 1e-6f);
	EXPECT_NEAR(State.Offset(), 0.0f, 1e-6f);
}

TEST(UiV2ScrollContainer, WheelInputOnlyMovesWhenHovered)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = false;
	Input.m_WheelDelta = -120.0f;
	SQmScrollContainerFrame Frame = Container.Update(State, View, 300.0f, 1.0f / 60.0f, Input);
	EXPECT_NEAR(Frame.m_Offset, 0.0f, 1e-6f);

	Input.m_Hovered = true;
	Frame = Container.Update(State, View, 300.0f, 1.0f / 60.0f, Input);
	EXPECT_GT(Frame.m_Offset, 0.0f);
	EXPECT_LT(Frame.m_ContentRect.y, View.y);
}

TEST(UiV2ScrollContainer, ComputesScrollbarTrackAndThumbGeometry)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerStyle Style;
	Style.m_ScrollbarWidth = 8.0f;
	Style.m_ScrollbarMargin = 2.0f;
	Style.m_MinThumbHeight = 24.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	Input.m_WheelDelta = -120.0f;
	const SQmScrollContainerFrame Frame = Container.Update(State, View, 400.0f, 1.0f / 60.0f, Input, Style);

	EXPECT_TRUE(Frame.m_ScrollbarVisible);
	EXPECT_NEAR(Frame.m_ClipRect.w, View.w - Style.m_ScrollbarWidth, 1e-6f);
	EXPECT_NEAR(Frame.m_ScrollbarTrackRect.x, View.x + View.w - Style.m_ScrollbarWidth + Style.m_ScrollbarMargin, 1e-6f);
	EXPECT_NEAR(Frame.m_ScrollbarTrackRect.y, View.y + Style.m_ScrollbarMargin, 1e-6f);
	EXPECT_NEAR(Frame.m_ScrollbarTrackRect.w, Style.m_ScrollbarWidth - Style.m_ScrollbarMargin * 2.0f, 1e-6f);
	EXPECT_NEAR(Frame.m_ScrollbarTrackRect.h, View.h - Style.m_ScrollbarMargin * 2.0f, 1e-6f);
	EXPECT_GE(Frame.m_ScrollbarThumbRect.h, Style.m_MinThumbHeight);
	EXPECT_GE(Frame.m_ScrollbarThumbRect.y, Frame.m_ScrollbarTrackRect.y);
	EXPECT_LE(Frame.m_ScrollbarThumbRect.y + Frame.m_ScrollbarThumbRect.h, Frame.m_ScrollbarTrackRect.y + Frame.m_ScrollbarTrackRect.h);
}

TEST(UiV2ScrollContainer, ComputesHorizontalContentRectAndScrollbarGeometry)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerStyle Style;
	Style.m_Axis = EQmScrollAxis::HORIZONTAL;
	Style.m_ScrollbarWidth = 8.0f;
	Style.m_ScrollbarMargin = 2.0f;
	Style.m_MinThumbHeight = 24.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	Input.m_WheelDelta = -120.0f;
	const SQmScrollContainerFrame Frame = Container.Update(State, View, 500.0f, 1.0f / 60.0f, Input, Style);

	EXPECT_TRUE(Frame.m_ScrollbarVisible);
	EXPECT_NEAR(Frame.m_ClipRect.x, View.x, 1e-6f);
	EXPECT_NEAR(Frame.m_ClipRect.y, View.y, 1e-6f);
	EXPECT_NEAR(Frame.m_ClipRect.w, View.w, 1e-6f);
	EXPECT_NEAR(Frame.m_ClipRect.h, View.h - Style.m_ScrollbarWidth, 1e-6f);
	EXPECT_LT(Frame.m_ContentRect.x, View.x);
	EXPECT_NEAR(Frame.m_ContentRect.y, View.y, 1e-6f);
	EXPECT_NEAR(Frame.m_ContentRect.w, 500.0f, 1e-6f);
	EXPECT_NEAR(Frame.m_ContentRect.h, View.h - Style.m_ScrollbarWidth, 1e-6f);
	EXPECT_NEAR(Frame.m_ScrollbarTrackRect.x, View.x + Style.m_ScrollbarMargin, 1e-6f);
	EXPECT_NEAR(Frame.m_ScrollbarTrackRect.y, View.y + View.h - Style.m_ScrollbarWidth + Style.m_ScrollbarMargin, 1e-6f);
	EXPECT_NEAR(Frame.m_ScrollbarTrackRect.w, View.w - Style.m_ScrollbarMargin * 2.0f, 1e-6f);
	EXPECT_NEAR(Frame.m_ScrollbarTrackRect.h, Style.m_ScrollbarWidth - Style.m_ScrollbarMargin * 2.0f, 1e-6f);
	EXPECT_GE(Frame.m_ScrollbarThumbRect.w, Style.m_MinThumbHeight);
	EXPECT_GE(Frame.m_ScrollbarThumbRect.x, Frame.m_ScrollbarTrackRect.x);
	EXPECT_LE(Frame.m_ScrollbarThumbRect.x + Frame.m_ScrollbarThumbRect.w, Frame.m_ScrollbarTrackRect.x + Frame.m_ScrollbarTrackRect.w);
}

TEST(UiV2ScrollContainer, OverscrollKeepsScrollbarThumbInsideTrack)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerStyle Style;
	Style.m_ScrollbarWidth = 8.0f;
	Style.m_ScrollbarMargin = 2.0f;
	Style.m_MinThumbHeight = 24.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	Input.m_WheelDelta = 1000.0f;
	SQmScrollConfig Config;
	Config.m_WheelScale = 1.0f;
	Config.m_NativeWheelStep = false;
	Config.m_MaxOverscroll = 72.0f;
	SQmScrollContainerFrame Frame = Container.Update(State, View, 400.0f, 1.0f / 60.0f, Input, Style, Config);
	EXPECT_LT(Frame.m_Offset, 0.0f);
	EXPECT_GE(Frame.m_ScrollbarThumbRect.y, Frame.m_ScrollbarTrackRect.y);

	State.Reset();
	Input.m_WheelDelta = -100000.0f;
	Frame = Container.Update(State, View, 400.0f, 1.0f / 60.0f, Input, Style, Config);
	EXPECT_GT(Frame.m_Offset, 300.0f);
	EXPECT_LE(Frame.m_ScrollbarThumbRect.y + Frame.m_ScrollbarThumbRect.h, Frame.m_ScrollbarTrackRect.y + Frame.m_ScrollbarTrackRect.h);
}

TEST(UiV2ScrollContainer, DraggingScrollbarThumbMapsMouseToOffset)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerStyle Style;
	Style.m_ScrollbarWidth = 8.0f;
	Style.m_ScrollbarMargin = 2.0f;
	Style.m_MinThumbHeight = 24.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	SQmScrollContainerFrame Frame = Container.Update(State, View, 400.0f, 0.0f, Input, Style);
	ASSERT_TRUE(Frame.m_ScrollbarVisible);

	Input.m_MouseY = Frame.m_ScrollbarThumbRect.y + Frame.m_ScrollbarThumbRect.h * 0.5f;
	Input.m_MousePressed = true;
	Input.m_MouseDown = true;
	Input.m_ThumbHovered = true;
	Frame = Container.Update(State, View, 400.0f, 0.0f, Input, Style);
	EXPECT_TRUE(Container.ScrollbarDragActive(State));

	Input.m_MousePressed = false;
	Input.m_MouseY = Frame.m_ScrollbarTrackRect.y + Frame.m_ScrollbarTrackRect.h - Frame.m_ScrollbarThumbRect.h * 0.5f;
	Frame = Container.Update(State, View, 400.0f, 0.0f, Input, Style);
	EXPECT_NEAR(Frame.m_Offset, 300.0f, 1.0f);

	Input.m_MouseDown = false;
	Container.Update(State, View, 400.0f, 0.0f, Input, Style);
	EXPECT_FALSE(Container.ScrollbarDragActive(State));
}

TEST(UiV2ScrollContainer, DraggingHorizontalScrollbarThumbMapsMouseToOffset)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerStyle Style;
	Style.m_Axis = EQmScrollAxis::HORIZONTAL;
	Style.m_ScrollbarWidth = 8.0f;
	Style.m_ScrollbarMargin = 2.0f;
	Style.m_MinThumbHeight = 24.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	SQmScrollContainerFrame Frame = Container.Update(State, View, 500.0f, 0.0f, Input, Style);
	ASSERT_TRUE(Frame.m_ScrollbarVisible);

	Input.m_MouseX = Frame.m_ScrollbarThumbRect.x + Frame.m_ScrollbarThumbRect.w * 0.5f;
	Input.m_MouseY = Frame.m_ScrollbarThumbRect.y + Frame.m_ScrollbarThumbRect.h * 0.5f;
	Input.m_MousePressed = true;
	Input.m_MouseDown = true;
	Input.m_ThumbHovered = true;
	Frame = Container.Update(State, View, 500.0f, 0.0f, Input, Style);
	EXPECT_TRUE(Container.ScrollbarDragActive(State));

	Input.m_MousePressed = false;
	Input.m_MouseX = Frame.m_ScrollbarTrackRect.x + Frame.m_ScrollbarTrackRect.w - Frame.m_ScrollbarThumbRect.w * 0.5f;
	Frame = Container.Update(State, View, 500.0f, 0.0f, Input, Style);
	EXPECT_NEAR(Frame.m_Offset, 300.0f, 1.0f);

	Input.m_MouseDown = false;
	Container.Update(State, View, 500.0f, 0.0f, Input, Style);
	EXPECT_FALSE(Container.ScrollbarDragActive(State));
}

TEST(UiV2ScrollContainer, ClickingScrollbarTrackPagesTowardMouse)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerStyle Style;
	Style.m_ScrollbarWidth = 8.0f;
	Style.m_ScrollbarMargin = 2.0f;
	Style.m_MinThumbHeight = 24.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	SQmScrollContainerFrame Frame = Container.Update(State, View, 400.0f, 0.0f, Input, Style);
	ASSERT_TRUE(Frame.m_ScrollbarVisible);

	Input.m_MouseY = Frame.m_ScrollbarTrackRect.y + Frame.m_ScrollbarTrackRect.h - 2.0f;
	Input.m_MousePressed = true;
	Input.m_MouseDown = true;
	Input.m_TrackHovered = true;
	Frame = Container.Update(State, View, 400.0f, 0.0f, Input, Style);

	EXPECT_NEAR(Frame.m_Offset, 100.0f, 1e-6f);
	EXPECT_TRUE(Container.ScrollbarDragActive(State));
}

TEST(UiV2ScrollState, ProgrammaticTargetSharesNativeAnimationAndClampsAfterContentShrink)
{
	SQmScrollMetrics TallMetrics;
	TallMetrics.m_ViewportSize = 100.0f;
	TallMetrics.m_ContentSize = 600.0f;
	SQmScrollMetrics ShortMetrics;
	ShortMetrics.m_ViewportSize = 100.0f;
	ShortMetrics.m_ContentSize = 180.0f;
	SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, 0.5f);

	CQmScrollState State;
	State.ScrollTo(300.0f, TallMetrics, Config);
	EXPECT_TRUE(State.Animating());
	State.Advance(0.25f, TallMetrics, Config);
	EXPECT_GT(State.Offset(), 0.0f);
	EXPECT_LT(State.Offset(), 300.0f);

	State.Advance(0.0f, ShortMetrics, Config);
	EXPECT_NEAR(State.Offset(), ShortMetrics.MaxOffset(), 1e-6f);
	EXPECT_FALSE(State.Animating());
}

TEST(UiV2ScrollState, DeferredProgrammaticTargetUsesFinalContentMetrics)
{
	SQmScrollMetrics TallMetrics;
	TallMetrics.m_ViewportSize = 100.0f;
	TallMetrics.m_ContentSize = 600.0f;
	SQmScrollMetrics FinalMetrics;
	FinalMetrics.m_ViewportSize = 100.0f;
	FinalMetrics.m_ContentSize = 300.0f;
	const SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, 0.5f);

	CQmScrollState State;
	State.SetOffset(40.0f, TallMetrics, Config);
	State.RequestScrollTo(900.0f);
	State.Advance(0.0f, FinalMetrics, Config);

	EXPECT_TRUE(State.Animating());
	State.Advance(0.5f, FinalMetrics, Config);
	EXPECT_NEAR(State.Offset(), FinalMetrics.MaxOffset(), 1e-6f);
}

TEST(UiV2ScrollState, UserWheelSupersedesDeferredProgrammaticTarget)
{
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 600.0f;
	const SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, 0.5f);

	CQmScrollState State;
	State.SetOffset(100.0f, Metrics, Config);
	State.RequestScrollTo(400.0f);
	State.AddWheelImpulse(-120.0f, Metrics, Config);
	State.Advance(0.0f, Metrics, Config);

	State.Advance(0.5f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), 100.0f + Config.m_WheelScale, 1e-6f);
}

TEST(UiV2ScrollState, NonScrollableResetPreservesActiveThumbGrabUntilRelease)
{
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 500.0f;

	CQmScrollState State;
	State.SetOffset(80.0f, Metrics);
	State.BeginThumbDrag(12.0f);
	State.ResetForNonScrollableContent(true);
	EXPECT_NEAR(State.Offset(), 0.0f, 1e-6f);
	EXPECT_TRUE(State.ThumbDragActive());
	EXPECT_NEAR(State.ThumbDragGrabOffset(), 12.0f, 1e-6f);

	State.ResetForNonScrollableContent(false);
	EXPECT_FALSE(State.ThumbDragActive());
	EXPECT_NEAR(State.ThumbDragGrabOffset(), 0.0f, 1e-6f);
}

TEST(UiV2ScrollContainer, PreviewFrameDoesNotCancelActiveScrollbarDrag)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerStyle Style;
	Style.m_ScrollbarWidth = 8.0f;
	Style.m_ScrollbarMargin = 2.0f;
	Style.m_MinThumbHeight = 24.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	SQmScrollContainerFrame Frame = Container.Update(State, View, 400.0f, 0.0f, Input, Style);
	ASSERT_TRUE(Frame.m_ScrollbarVisible);

	Input.m_MouseY = Frame.m_ScrollbarThumbRect.y + Frame.m_ScrollbarThumbRect.h * 0.5f;
	Input.m_MousePressed = true;
	Input.m_MouseDown = true;
	Input.m_ThumbHovered = true;
	Frame = Container.Update(State, View, 400.0f, 0.0f, Input, Style);
	ASSERT_TRUE(Container.ScrollbarDragActive(State));

	const SQmScrollContainerFrame Preview = Container.PreviewFrame(State, View, 400.0f, Style);
	EXPECT_TRUE(Preview.m_ScrollbarVisible);
	EXPECT_TRUE(Container.ScrollbarDragActive(State));

	Input.m_MousePressed = false;
	Input.m_ThumbHovered = false;
	Input.m_MouseY = Frame.m_ScrollbarTrackRect.y + Frame.m_ScrollbarTrackRect.h - Frame.m_ScrollbarThumbRect.h * 0.5f;
	Frame = Container.Update(State, View, 400.0f, 0.0f, Input, Style);
	EXPECT_NEAR(Frame.m_Offset, 300.0f, 1.0f);
}

TEST(UiV2ScrollContainer, DraggingContentMovesOffsetWithPointer)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	Input.m_MouseY = 70.0f;
	Input.m_MousePressed = true;
	Input.m_MouseDown = true;
	Input.m_ContentDragAllowed = true;
	SQmScrollContainerFrame Frame = Container.Update(State, View, 400.0f, 0.0f, Input);
	EXPECT_FALSE(Container.ContentDragActive(State));
	EXPECT_NEAR(Frame.m_Offset, 0.0f, 1e-6f);

	Input.m_MousePressed = false;
	Input.m_MouseY = 67.0f;
	Frame = Container.Update(State, View, 400.0f, 0.0f, Input);
	EXPECT_FALSE(Container.ContentDragActive(State));
	EXPECT_NEAR(Frame.m_Offset, 0.0f, 1e-6f);

	Input.m_MouseY = 40.0f;
	Frame = Container.Update(State, View, 400.0f, 0.0f, Input);
	EXPECT_NEAR(Frame.m_Offset, 30.0f, 1e-6f);
	EXPECT_TRUE(Container.ContentDragActive(State));

	Input.m_MouseDown = false;
	Container.Update(State, View, 400.0f, 0.0f, Input);
	EXPECT_FALSE(Container.ContentDragActive(State));
}

TEST(UiV2ScrollContainer, DraggingHorizontalContentUsesPointerX)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerStyle Style;
	Style.m_Axis = EQmScrollAxis::HORIZONTAL;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	Input.m_MouseX = 80.0f;
	Input.m_MouseY = 70.0f;
	Input.m_MousePressed = true;
	Input.m_MouseDown = true;
	Input.m_ContentDragAllowed = true;
	SQmScrollContainerFrame Frame = Container.Update(State, View, 500.0f, 0.0f, Input, Style);
	EXPECT_FALSE(Container.ContentDragActive(State));
	EXPECT_NEAR(Frame.m_Offset, 0.0f, 1e-6f);

	Input.m_MousePressed = false;
	Input.m_MouseX = 76.0f;
	Input.m_MouseY = 30.0f;
	Frame = Container.Update(State, View, 500.0f, 0.0f, Input, Style);
	EXPECT_FALSE(Container.ContentDragActive(State));
	EXPECT_NEAR(Frame.m_Offset, 0.0f, 1e-6f);

	Input.m_MouseX = 40.0f;
	Frame = Container.Update(State, View, 500.0f, 0.0f, Input, Style);
	EXPECT_NEAR(Frame.m_Offset, 40.0f, 1e-6f);
	EXPECT_TRUE(Container.ContentDragActive(State));
	EXPECT_LT(Frame.m_ContentRect.x, View.x);
	EXPECT_NEAR(Frame.m_ContentRect.y, View.y, 1e-6f);

	Input.m_MouseDown = false;
	Container.Update(State, View, 500.0f, 0.0f, Input, Style);
	EXPECT_FALSE(Container.ContentDragActive(State));
}

TEST(UiV2ScrollContainer, ScrollbarDragDoesNotStartContentDrag)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerStyle Style;
	Style.m_ScrollbarWidth = 8.0f;
	Style.m_ScrollbarMargin = 2.0f;
	Style.m_MinThumbHeight = 24.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	Input.m_ContentDragAllowed = true;
	SQmScrollContainerFrame Frame = Container.Update(State, View, 400.0f, 0.0f, Input, Style);
	ASSERT_TRUE(Frame.m_ScrollbarVisible);

	Input.m_MouseY = Frame.m_ScrollbarThumbRect.y + Frame.m_ScrollbarThumbRect.h * 0.5f;
	Input.m_MousePressed = true;
	Input.m_MouseDown = true;
	Input.m_ThumbHovered = true;
	Container.Update(State, View, 400.0f, 0.0f, Input, Style);

	EXPECT_TRUE(Container.ScrollbarDragActive(State));
	EXPECT_FALSE(Container.ContentDragActive(State));
}

TEST(UiV2ScrollContainer, BlockedContentDragDoesNotMoveOffset)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	Input.m_MouseY = 70.0f;
	Input.m_MousePressed = true;
	Input.m_MouseDown = true;
	Input.m_ContentDragAllowed = true;
	Input.m_ContentDragBlocked = true;
	SQmScrollContainerFrame Frame = Container.Update(State, View, 400.0f, 0.0f, Input);

	EXPECT_FALSE(Container.ContentDragActive(State));
	EXPECT_NEAR(Frame.m_Offset, 0.0f, 1e-6f);

	Input.m_MousePressed = false;
	Input.m_MouseY = 40.0f;
	Frame = Container.Update(State, View, 400.0f, 0.0f, Input);
	EXPECT_FALSE(Container.ContentDragActive(State));
	EXPECT_NEAR(Frame.m_Offset, 0.0f, 1e-6f);
}

TEST(UiV2ScrollContainer, BlockedContentDragCancelsPendingCandidate)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	Input.m_MouseY = 70.0f;
	Input.m_MousePressed = true;
	Input.m_MouseDown = true;
	Input.m_ContentDragAllowed = true;
	SQmScrollContainerFrame Frame = Container.Update(State, View, 400.0f, 0.0f, Input);
	EXPECT_FALSE(Container.ContentDragActive(State));
	EXPECT_NEAR(Frame.m_Offset, 0.0f, 1e-6f);

	Input.m_MousePressed = false;
	Input.m_ContentDragBlocked = true;
	Input.m_MouseY = 40.0f;
	Frame = Container.Update(State, View, 400.0f, 0.0f, Input);
	EXPECT_FALSE(Container.ContentDragActive(State));
	EXPECT_NEAR(Frame.m_Offset, 0.0f, 1e-6f);

	Input.m_ContentDragBlocked = false;
	Input.m_MouseY = 20.0f;
	Frame = Container.Update(State, View, 400.0f, 0.0f, Input);
	EXPECT_FALSE(Container.ContentDragActive(State));
	EXPECT_NEAR(Frame.m_Offset, 0.0f, 1e-6f);
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

TEST(UiV2DropdownGeometry, PositionsPopupRelativeToScrolledAnchor)
{
	CUIRect Viewport;
	Viewport.x = 0.0f;
	Viewport.y = 0.0f;
	Viewport.w = 320.0f;
	Viewport.h = 240.0f;
	CUIRect Anchor;
	Anchor.x = 48.0f;
	Anchor.y = 18.0f;
	Anchor.w = 120.0f;
	Anchor.h = 24.0f;
	SQmDropdownGeometryConfig Config;
	Config.m_Width = Anchor.w;
	Config.m_Height = 80.0f;
	Config.m_Gap = 4.0f;
	Config.m_Margin = 8.0f;

	const SQmDropdownGeometryResult Result = QmComputeDropdownPopupGeometry(Anchor, Viewport, Config);

	EXPECT_TRUE(Result.m_AnchorVisible);
	EXPECT_TRUE(Result.m_PlacedBelow);
	EXPECT_NEAR(Result.m_Rect.x, Anchor.x, 0.001f);
	EXPECT_NEAR(Result.m_Rect.y, Anchor.y + Anchor.h + Config.m_Gap, 0.001f);
	EXPECT_NEAR(Result.m_Rect.w, Anchor.w, 0.001f);
	EXPECT_NEAR(Result.m_Rect.h, Config.m_Height, 0.001f);
}

TEST(UiV2DropdownGeometry, RejectsPartiallyVisibleAnchorBeforeOpening)
{
	const CUIRect Viewport{0.0f, 0.0f, 320.0f, 240.0f};
	const CUIRect Anchor{48.0f, -18.0f, 120.0f, 24.0f};
	SQmDropdownGeometryConfig Config;
	Config.m_Width = Anchor.w;
	Config.m_Height = 80.0f;
	Config.m_Gap = 4.0f;
	Config.m_Margin = 8.0f;

	const SQmDropdownGeometryResult Result = QmComputeDropdownPopupGeometry(Anchor, Viewport, Config);

	EXPECT_FALSE(Result.m_AnchorVisible);
}

TEST(UiV2DropdownVisuals, SettingsStyleSharesTriggerAndPopupSurface)
{
	const SUiTheme Theme = ResolveUiTheme(ColorHSLA(0.20f, 0.50f, 0.40f, 1.0f), 0.75f);
	const SQmDropdownVisualStyle Style = QmSettingsDropdownVisualStyle(Theme);
	EXPECT_FLOAT_EQ(Style.m_TriggerColor.r, Theme.m_InputSurface.r);
	EXPECT_FLOAT_EQ(Style.m_TriggerColor.g, Theme.m_InputSurface.g);
	EXPECT_FLOAT_EQ(Style.m_TriggerColor.b, Theme.m_InputSurface.b);
	EXPECT_FLOAT_EQ(Style.m_PopupBackgroundColor.a, Theme.m_Surface.a);
	EXPECT_TRUE(Style.m_TransparentEntries);
	EXPECT_FLOAT_EQ(Style.m_PopupBorderColor.a, Theme.m_Border.a);
	EXPECT_FLOAT_EQ(Style.m_ActiveEntryColor.r, Theme.m_SurfaceHovered.r);
	EXPECT_FLOAT_EQ(Style.m_ActiveEntryColor.g, Theme.m_SurfaceHovered.g);
	EXPECT_FLOAT_EQ(Style.m_ActiveEntryColor.b, Theme.m_SurfaceHovered.b);
	EXPECT_FLOAT_EQ(Style.m_ActiveEntryColor.a, Theme.m_SurfaceHovered.a);
}

TEST(UiV2DropdownGeometry, FlipsAboveWhenBelowWouldOverflow)
{
	CUIRect Viewport;
	Viewport.x = 0.0f;
	Viewport.y = 0.0f;
	Viewport.w = 320.0f;
	Viewport.h = 240.0f;
	CUIRect Anchor;
	Anchor.x = 48.0f;
	Anchor.y = 210.0f;
	Anchor.w = 120.0f;
	Anchor.h = 24.0f;
	SQmDropdownGeometryConfig Config;
	Config.m_Width = Anchor.w;
	Config.m_Height = 96.0f;
	Config.m_Gap = 4.0f;
	Config.m_Margin = 8.0f;

	const SQmDropdownGeometryResult Result = QmComputeDropdownPopupGeometry(Anchor, Viewport, Config);

	EXPECT_TRUE(Result.m_AnchorVisible);
	EXPECT_FALSE(Result.m_PlacedBelow);
	EXPECT_NEAR(Result.m_Rect.y, Anchor.y - Config.m_Gap - Config.m_Height, 0.001f);
}

TEST(UiV2DropdownGeometry, ClampsOversizedPopupInsideViewportMargins)
{
	CUIRect Viewport;
	Viewport.x = 10.0f;
	Viewport.y = 20.0f;
	Viewport.w = 120.0f;
	Viewport.h = 90.0f;
	CUIRect Anchor;
	Anchor.x = 100.0f;
	Anchor.y = 130.0f;
	Anchor.w = 80.0f;
	Anchor.h = 20.0f;
	SQmDropdownGeometryConfig Config;
	Config.m_Width = 200.0f;
	Config.m_Height = 120.0f;
	Config.m_Gap = 4.0f;
	Config.m_Margin = 6.0f;

	const SQmDropdownGeometryResult Result = QmComputeDropdownPopupGeometry(Anchor, Viewport, Config);

	EXPECT_FALSE(Result.m_AnchorVisible);
	EXPECT_TRUE(Result.m_Clamped);
	EXPECT_NEAR(Result.m_Rect.x, Viewport.x + Config.m_Margin, 0.001f);
	EXPECT_NEAR(Result.m_Rect.y, Viewport.y + Config.m_Margin, 0.001f);
	EXPECT_NEAR(Result.m_Rect.w, Viewport.w - Config.m_Margin * 2.0f, 0.001f);
	EXPECT_NEAR(Result.m_Rect.h, Viewport.h - Config.m_Margin * 2.0f, 0.001f);
}

TEST(UiV2DropdownGeometry, KeepsPopupVisibleWhenAnchorScrolledOut)
{
	CUIRect Viewport;
	Viewport.x = 0.0f;
	Viewport.y = 0.0f;
	Viewport.w = 320.0f;
	Viewport.h = 240.0f;
	CUIRect Anchor;
	Anchor.x = 48.0f;
	Anchor.y = -160.0f;
	Anchor.w = 120.0f;
	Anchor.h = 24.0f;
	SQmDropdownGeometryConfig Config;
	Config.m_Width = Anchor.w;
	Config.m_Height = 80.0f;
	Config.m_Gap = 4.0f;
	Config.m_Margin = 8.0f;

	const SQmDropdownGeometryResult Result = QmComputeDropdownPopupGeometry(Anchor, Viewport, Config);

	EXPECT_FALSE(Result.m_AnchorVisible);
	EXPECT_TRUE(Result.m_PopupVisible);
	EXPECT_TRUE(Result.m_Clamped);
	EXPECT_GE(Result.m_Rect.y, Viewport.y + Config.m_Margin);
	EXPECT_LE(Result.m_Rect.y + Result.m_Rect.h, Viewport.y + Viewport.h - Config.m_Margin);
}

TEST(UiV2DropdownGeometry, MarksPopupInvisibleWhenViewportHasNoUsableArea)
{
	CUIRect Viewport;
	Viewport.x = 0.0f;
	Viewport.y = 0.0f;
	Viewport.w = 12.0f;
	Viewport.h = 12.0f;
	CUIRect Anchor;
	Anchor.x = 4.0f;
	Anchor.y = 4.0f;
	Anchor.w = 16.0f;
	Anchor.h = 16.0f;
	SQmDropdownGeometryConfig Config;
	Config.m_Width = 120.0f;
	Config.m_Height = 80.0f;
	Config.m_Margin = 8.0f;

	const SQmDropdownGeometryResult Result = QmComputeDropdownPopupGeometry(Anchor, Viewport, Config);

	EXPECT_FALSE(Result.m_PopupVisible);
	EXPECT_NEAR(Result.m_Rect.w, 0.0f, 0.001f);
	EXPECT_NEAR(Result.m_Rect.h, 0.0f, 0.001f);
}

TEST(UiV2DropdownGeometry, AnchorMustRemainFullyInsideItsOwningContainer)
{
	const CUIRect Viewport{10.0f, 20.0f, 200.0f, 100.0f};
	EXPECT_TRUE(QmDropdownAnchorFullyVisible({20.0f, 30.0f, 80.0f, 20.0f}, Viewport));
	EXPECT_TRUE(QmDropdownAnchorFullyVisible(Viewport, Viewport));
	// 布局浮点误差不能让贴着卡片底边的下拉框在下一帧立即关闭。
	EXPECT_TRUE(QmDropdownAnchorFullyVisible({20.0f, 99.999f, 80.0f, 20.01f}, Viewport));
	EXPECT_FALSE(QmDropdownAnchorFullyVisible({9.0f, 30.0f, 80.0f, 20.0f}, Viewport));
	EXPECT_FALSE(QmDropdownAnchorFullyVisible({20.0f, 105.0f, 80.0f, 20.0f}, Viewport));
	EXPECT_FALSE(QmDropdownAnchorFullyVisible({20.0f, 30.0f, 0.0f, 20.0f}, Viewport));
}

TEST(UiV2DropdownScroll, ActiveItemOnlyRequestsAutoScrollOnOpenOrKeyboardNavigation)
{
	EXPECT_TRUE(QmDropdownActiveItemShouldScrollIntoView(true, true));
	EXPECT_FALSE(QmDropdownActiveItemShouldScrollIntoView(false, true));
	EXPECT_FALSE(QmDropdownActiveItemShouldScrollIntoView(true, false));
	EXPECT_FALSE(QmDropdownShouldRequestActiveScroll(true, 4, 4));
	EXPECT_FALSE(QmDropdownShouldRequestActiveScroll(false, 4, 5));
	EXPECT_TRUE(QmDropdownShouldRequestActiveScroll(true, 4, 5));
}

TEST(UiV2DropdownScroll, DraggedPopupOffsetSurvivesFollowingFrameWithoutNewTarget)
{
	const SQmScrollMetrics Metrics{160.0f, 320.0f};
	SQmScrollConfig Config;
	Config.m_WheelScale = 20.0f;
	Config.m_NativeWheelAnimationTime = 0.0f;
	CQmScrollState State;

	// 模拟打开后滚轮向下，再模拟用户把滚动条拖到末尾。
	State.AddWheelImpulse(-120.0f, Metrics, Config);
	State.SetOffset(Metrics.MaxOffset(), Metrics, Config);
	ASSERT_FLOAT_EQ(State.Offset(), Metrics.MaxOffset());

	// 下一个绘制帧没有 active-index 变化时不应自动定位回顶部。
	State.Advance(1.0f / 60.0f, Metrics, Config);
	EXPECT_FLOAT_EQ(State.Offset(), Metrics.MaxOffset());
}

TEST(UiV2DropdownPolicy, OwnsWheelWheneverViewportClipsContent)
{
	const SQmDropdownPopupPolicy ShortPolicy = QmResolveDropdownPopupPolicy(QM_POPUP_LIST_MAX_VISIBLE_ITEMS, 20.0f, 5.0f, false, 0.0f, 10.0f);
	EXPECT_EQ(ShortPolicy.m_MaxVisibleItems, QM_POPUP_LIST_MAX_VISIBLE_ITEMS);
	EXPECT_NEAR(ShortPolicy.m_ContentHeight, ShortPolicy.m_PreferredHeight, 0.001f);
	EXPECT_FALSE(QmDropdownPopupScrollable(ShortPolicy, ShortPolicy.m_PreferredHeight));

	const SQmDropdownPopupPolicy LongPolicy = QmResolveDropdownPopupPolicy(QM_POPUP_LIST_MAX_VISIBLE_ITEMS + 1, 20.0f, 5.0f, false, 0.0f, 10.0f);
	EXPECT_EQ(LongPolicy.m_MaxVisibleItems, QM_POPUP_LIST_MAX_VISIBLE_ITEMS);
	EXPECT_GT(LongPolicy.m_ContentHeight, LongPolicy.m_PreferredHeight);
	EXPECT_TRUE(QmDropdownPopupScrollable(LongPolicy, LongPolicy.m_PreferredHeight));

	EXPECT_TRUE(QmDropdownPopupScrollable(ShortPolicy, ShortPolicy.m_PreferredHeight - 1.0f));
}

TEST(UiV2DropdownPolicy, PopupAlwaysBlocksUnderlyingWheelButOnlyShowsRailOnOverflow)
{
	const SQmDropdownPopupPolicy EightRows = QmResolveDropdownPopupPolicy(QM_POPUP_LIST_MAX_VISIBLE_ITEMS, 20.0f, 5.0f, false, 0.0f, 10.0f);
	EXPECT_FALSE(QmDropdownPopupScrollable(EightRows, EightRows.m_PreferredHeight));
	EXPECT_TRUE(QmDropdownPopupBlocksUnderlying(true));

	const SQmDropdownPopupPolicy NineRows = QmResolveDropdownPopupPolicy(QM_POPUP_LIST_MAX_VISIBLE_ITEMS + 1, 20.0f, 5.0f, false, 0.0f, 10.0f);
	EXPECT_TRUE(QmDropdownPopupScrollable(NineRows, NineRows.m_PreferredHeight));
	EXPECT_TRUE(QmDropdownPopupBlocksUnderlying(true));
	EXPECT_FALSE(QmDropdownPopupBlocksUnderlying(false));
}

TEST(UiV2DropdownLifecycle, SourceMustRefreshInTheCurrentFrame)
{
	EXPECT_TRUE(QmDropdownSourceAlive(42, 42, true));
	EXPECT_FALSE(QmDropdownSourceAlive(42, 41, true));
	EXPECT_FALSE(QmDropdownSourceAlive(42, 42, false));
}

TEST(UiV2DropdownPolicy, MapPickerIncludesPopupChromeBeforeTestingEightRowOverflow)
{
	const float OuterHeight = CUi::PopupMenuContentInset();
	const SQmDropdownPopupPolicy NoRows = QmResolveDropdownPopupPolicy(0, 20.0f, 0.0f, false, 0.0f, OuterHeight, 1);
	EXPECT_EQ(NoRows.m_ItemCount, 0);
	EXPECT_NEAR(NoRows.m_PreferredHeight - OuterHeight, 20.0f, 0.001f);
	EXPECT_FALSE(QmDropdownPopupScrollable(NoRows, NoRows.m_PreferredHeight));

	const SQmDropdownPopupPolicy OneRow = QmResolveDropdownPopupPolicy(1, 20.0f, 0.0f, false, 0.0f, OuterHeight, 1);
	EXPECT_EQ(OneRow.m_ItemCount, 1);
	EXPECT_NEAR(OneRow.m_PreferredHeight - OuterHeight, 20.0f, 0.001f);
	EXPECT_NEAR(OneRow.m_ContentHeight, OneRow.m_PreferredHeight, 0.001f);
	EXPECT_FALSE(QmDropdownPopupScrollable(OneRow, OneRow.m_PreferredHeight));

	const SQmDropdownPopupPolicy EightRows = QmResolveDropdownPopupPolicy(8, 20.0f, 0.0f, false, 0.0f, OuterHeight);
	EXPECT_NEAR(EightRows.m_PreferredHeight, 8.0f * 20.0f + OuterHeight, 0.001f);
	EXPECT_NEAR(EightRows.m_ContentHeight, EightRows.m_PreferredHeight, 0.001f);
	EXPECT_FALSE(QmDropdownPopupScrollable(EightRows, EightRows.m_PreferredHeight));

	const SQmDropdownPopupPolicy NineRows = QmResolveDropdownPopupPolicy(9, 20.0f, 0.0f, false, 0.0f, OuterHeight);
	EXPECT_NEAR(NineRows.m_PreferredHeight, EightRows.m_PreferredHeight, 0.001f);
	EXPECT_GT(NineRows.m_ContentHeight, NineRows.m_PreferredHeight);
	EXPECT_TRUE(QmDropdownPopupScrollable(NineRows, NineRows.m_PreferredHeight));
}

TEST(UiV2DropdownPolicy, FirstFrameContentHintMatchesScrollableInnerContent)
{
	const float OuterHeight = CUi::PopupMenuContentInset();
	const SQmDropdownPopupPolicy EightRows = QmResolveDropdownPopupPolicy(8, 20.0f, 5.0f, false, 0.0f, OuterHeight);
	const SQmDropdownPopupPolicy NineRows = QmResolveDropdownPopupPolicy(9, 20.0f, 5.0f, false, 0.0f, OuterHeight);
	EXPECT_FLOAT_EQ(EightRows.m_ContentHeight - OuterHeight, EightRows.m_PreferredHeight - OuterHeight);
	EXPECT_GT(NineRows.m_ContentHeight - OuterHeight, NineRows.m_PreferredHeight - OuterHeight);
}

TEST(UiV2DropdownIntegration, LongPopupConsumesWheelBeforeParent)
{
	CScrollWheelOwnership Ownership;
	int ParentRegion = 0;
	int SelectionPopupContext = 0;
	CQmScrollState ParentState;
	CQmScrollState PopupState;
	const SQmScrollMetrics ParentMetrics{300.0f, 900.0f};
	const SQmScrollMetrics PopupMetrics{160.0f, 320.0f};
	const SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, 0.0f);
	const CUIRect ParentRect{0.0f, 0.0f, 300.0f, 300.0f};
	const CUIRect PopupRect{20.0f, 40.0f, 160.0f, 160.0f};
	const vec2 Pointer{50.0f, 80.0f};
	ASSERT_TRUE(Ownership.BeginFrame(41, -120.0f, false));
	QmRegisterWheelOwnerCandidate(Ownership, {&SelectionPopupContext, EUiWheelOwnerPriority::POPUP, PopupRect, true}, Pointer, true);
	QmRegisterWheelOwnerCandidate(Ownership, {&ParentRegion, EUiWheelOwnerPriority::PAGE, ParentRect, true}, Pointer, true);
	float Delta = 0.0f;
	EXPECT_FALSE(QmTryConsumeWheel(Ownership, &ParentRegion, &Delta));
	ASSERT_TRUE(QmTryConsumeWheel(Ownership, &SelectionPopupContext, &Delta));
	PopupState.AddWheelImpulse(Delta, PopupMetrics, Config);
	ParentState.Advance(1.0f / 60.0f, ParentMetrics, Config);
	PopupState.Advance(1.0f / 60.0f, PopupMetrics, Config);
	EXPECT_FLOAT_EQ(ParentState.Offset(), 0.0f);
	EXPECT_GT(PopupState.Offset(), 0.0f);
}

TEST(UiV2DropdownIntegration, ShortPopupHidesRailAndBlocksParentWheel)
{
	const SQmDropdownPopupPolicy Policy = QmResolveDropdownPopupPolicy(4, 20.0f, 5.0f, false, 0.0f, 10.0f);
	EXPECT_FALSE(QmDropdownPopupScrollable(Policy, Policy.m_PreferredHeight));
	CScrollWheelOwnership Ownership;
	int ParentRegion = 0;
	int SelectionPopupContext = 0;
	const CUIRect ParentRect{0.0f, 0.0f, 300.0f, 300.0f};
	const CUIRect PopupRect{20.0f, 40.0f, 160.0f, Policy.m_PreferredHeight};
	const vec2 Pointer{50.0f, 80.0f};
	ASSERT_TRUE(Ownership.BeginFrame(41, -120.0f, false));
	QmRegisterWheelOwnerCandidate(Ownership, {&SelectionPopupContext, EUiWheelOwnerPriority::POPUP, PopupRect, QmDropdownPopupBlocksUnderlying(true)}, Pointer, true);
	QmRegisterWheelOwnerCandidate(Ownership, {&ParentRegion, EUiWheelOwnerPriority::PAGE, ParentRect, true}, Pointer, true);
	float Delta = 0.0f;
	EXPECT_FALSE(QmTryConsumeWheel(Ownership, &ParentRegion, &Delta));
	EXPECT_TRUE(QmTryConsumeWheel(Ownership, &SelectionPopupContext, &Delta));
	EXPECT_FLOAT_EQ(Delta, -120.0f);
}
TEST(UiV2DropdownState, OpensWithCurrentItemAndClosesOnEscape)
{
	CQmDropdownState State;
	SQmDropdownInput Input;
	Input.m_TogglePressed = true;
	Input.m_InitialIndex = 2;

	SQmDropdownUpdateResult Result = State.Update(Input, 3);
	EXPECT_TRUE(Result.m_Opened);
	EXPECT_TRUE(State.IsOpen());
	EXPECT_EQ(State.ActiveIndex(), 2);

	Input = {};
	Input.m_KeyEscape = true;
	Result = State.Update(Input, 3);
	EXPECT_TRUE(Result.m_Closed);
	EXPECT_FALSE(State.IsOpen());
	EXPECT_EQ(State.ActiveIndex(), -1);
}

TEST(UiV2DropdownState, InvalidCurrentItemFallsBackToFirstItem)
{
	CQmDropdownState State;
	SQmDropdownInput Input;
	Input.m_TogglePressed = true;
	Input.m_InitialIndex = 9;

	const SQmDropdownUpdateResult Result = State.Update(Input, 3);
	EXPECT_TRUE(Result.m_Opened);
	EXPECT_EQ(State.ActiveIndex(), 0);
}

TEST(UiV2DropdownState, KeyboardNavigationWrapsAndEnterSelects)
{
	CQmDropdownState State;
	SQmDropdownInput Input;
	Input.m_TogglePressed = true;
	State.Update(Input, 3);

	Input = {};
	Input.m_KeyUp = true;
	SQmDropdownUpdateResult Result = State.Update(Input, 3);
	EXPECT_FALSE(Result.m_Selected);
	EXPECT_EQ(State.ActiveIndex(), 2);

	Input = {};
	Input.m_KeyDown = true;
	Result = State.Update(Input, 3);
	EXPECT_EQ(State.ActiveIndex(), 0);

	Input = {};
	Input.m_KeyEnter = true;
	Result = State.Update(Input, 3);
	EXPECT_TRUE(Result.m_Selected);
	EXPECT_EQ(Result.m_SelectedIndex, 0);
	EXPECT_TRUE(Result.m_Closed);
	EXPECT_FALSE(State.IsOpen());
}

TEST(UiV2DropdownState, MouseHoverAndClickSelectsHoveredItem)
{
	CQmDropdownState State;
	SQmDropdownInput Input;
	Input.m_TogglePressed = true;
	State.Update(Input, 4);

	Input = {};
	Input.m_HoveredIndex = 2;
	SQmDropdownUpdateResult Result = State.Update(Input, 4);
	EXPECT_FALSE(Result.m_Selected);
	EXPECT_EQ(State.ActiveIndex(), 2);

	Input.m_MouseSelectPressed = true;
	Result = State.Update(Input, 4);
	EXPECT_TRUE(Result.m_Selected);
	EXPECT_EQ(Result.m_SelectedIndex, 2);
	EXPECT_FALSE(State.IsOpen());
}

TEST(NumericField, FormatsAndParsesIntegerDecimalAndInfinity)
{
	ui_widget::SNumericValueFormat Integer;
	Integer.m_DisplayDivisor = 1;
	Integer.m_Precision = 0;
	EXPECT_EQ(ui_widget::FormatNumericFieldValue(42, Integer), "42");

	ui_widget::SNumericValueFormat Decimal;
	Decimal.m_DisplayDivisor = 100;
	Decimal.m_Precision = 2;
	EXPECT_EQ(ui_widget::FormatNumericFieldValue(125, Decimal), "1.25");
	int Stored = 0;
	EXPECT_TRUE(ui_widget::ParseNumericFieldValue("-3.50", Decimal, -1000, 1000, &Stored));
	EXPECT_EQ(Stored, -350);

	Decimal.m_AllowInfinite = true;
	Decimal.m_InfiniteStoredValue = 0;
	EXPECT_TRUE(ui_widget::ParseNumericFieldValue("∞", Decimal, -1000, 1000, &Stored));
	EXPECT_EQ(Stored, 0);
	EXPECT_EQ(ui_widget::FormatNumericFieldValue(0, Decimal), "∞");
}

TEST(NumericField, TextInputUsesIndependentBoundAndPreservesInfinity)
{
	EXPECT_EQ(ui_widget::NumericFieldTextInputStoredValue(0, 1, 0, 1000, 10000, false), 0);
	EXPECT_EQ(ui_widget::NumericFieldTextInputStoredValue(5, 1, 0, 1000, 10000, false), 5);
	EXPECT_EQ(ui_widget::NumericFieldTextInputStoredValue(5000, 1, 0, 1000, 10000, false), 5000);
	EXPECT_EQ(ui_widget::NumericFieldTextInputStoredValue(10001, 1, 0, 1000, 10000, false), 10000);
	EXPECT_EQ(ui_widget::NumericFieldTextInputStoredValue(10001, 1, 10, 1000, -1, false), 1000);
	EXPECT_EQ(ui_widget::NumericFieldTextInputStoredValue(1000, 1, 0, 1000, 10000, true), 0);
}

TEST(NumericField, QuantizedValuesRespectSliderAndExtendedInputBounds)
{
	EXPECT_EQ(ui_widget::QuantizeNumericFieldStoredValue(149, 100, 3000, 100), 100);
	EXPECT_EQ(ui_widget::QuantizeNumericFieldStoredValue(151, 100, 3000, 100), 200);
	EXPECT_EQ(ui_widget::QuantizeNumericFieldStoredValue(5001, 0, 10000, 100), 5000);
	EXPECT_EQ(ui_widget::QuantizeNumericFieldStoredValue(10001, 0, 10000, 100), 10000);
}

TEST(NumericField, DelayPolicyCommitsOnlyOnReleaseSubmitOrBlur)
{
	ui_widget::SInputFieldResult Editing;
	Editing.m_Changed = true;
	EXPECT_FALSE(ui_widget::NumericFieldShouldCommit(ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT, false, Editing));
	Editing.m_Submitted = true;
	EXPECT_TRUE(ui_widget::NumericFieldShouldCommit(ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT, false, Editing));
	Editing.m_Submitted = false;
	Editing.m_Deactivated = true;
	EXPECT_TRUE(ui_widget::NumericFieldShouldCommit(ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT, false, Editing));
	EXPECT_TRUE(ui_widget::NumericFieldShouldCommit(ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT, true, {}));
}

TEST(NumericField, DelayPolicyStagesSliderValueUntilRelease)
{
	ui_widget::SNumericFieldCommitState State;
	int StoredValue = 10;

	EXPECT_FALSE(ui_widget::UpdateNumericFieldSliderCommit(State, ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT, true, false, 25, &StoredValue));
	EXPECT_EQ(StoredValue, 10);
	EXPECT_TRUE(State.m_HasPendingValue);
	EXPECT_EQ(State.m_PendingStoredValue, 25);

	EXPECT_TRUE(ui_widget::UpdateNumericFieldSliderCommit(State, ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT, false, true, 10, &StoredValue));
	EXPECT_EQ(StoredValue, 25);
	EXPECT_FALSE(State.m_HasPendingValue);
}

TEST(NumericField, FallsBackToTwoRowsBeforeCollapsingSliderTrack)
{
	const ui_widget::SNumericFieldLayout Wide = ui_widget::ResolveNumericFieldLayout({0.0f, 0.0f, 500.0f, 36.0f}, true, true, 1.0f);
	const ui_widget::SNumericFieldLayout Narrow = ui_widget::ResolveNumericFieldLayout({0.0f, 0.0f, 260.0f, 36.0f}, true, true, 1.0f);
	EXPECT_FALSE(Wide.m_TwoRows);
	EXPECT_GE(Wide.m_SliderRect.w, 96.0f);
	EXPECT_TRUE(Narrow.m_TwoRows);
	EXPECT_GE(Narrow.m_SliderRect.w, 96.0f);
}
TEST(UiForms, SliderInputValueMappingPreservesStoredScaleAndInfiniteSentinel)
{
	EXPECT_EQ(ui_widget::SliderInputStoredMinimum(100, 20), 5);
	EXPECT_EQ(ui_widget::SliderInputStoredMaximum(300, 20), 15);
	EXPECT_EQ(ui_widget::SliderInputDisplayValue(5, 20), 100);
	EXPECT_EQ(ui_widget::SliderInputStoredValue(200, 20), 10);
	EXPECT_EQ(ui_widget::SliderInputStoredValue(201, 20), 10);
	EXPECT_TRUE(ui_widget::SliderInputIsInfiniteValue(0, true));
	EXPECT_FALSE(ui_widget::SliderInputIsInfiniteValue(1, true));
	EXPECT_EQ(ui_widget::SliderInputWheelStoredValue(0, 1, 1001, true, -28), 973);
	EXPECT_EQ(ui_widget::SliderInputWheelStoredValue(0, 1, 1001, true, 28), 0);
	EXPECT_NEAR(ui_widget::NumericFieldInfiniteEndpointStart(240.0f, 1.0f), 0.95f, 0.001f);
	EXPECT_LE(ui_widget::NumericFieldInfiniteEndpointStart(240.0f, 1.0f), 0.96f);
	EXPECT_GE(ui_widget::NumericFieldInfiniteEndpointStart(240.0f, 1.0f), 0.94f);
}
