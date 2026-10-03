#include <game/client/QmUi/QmImeCandidateLayout.h>

#include <gtest/gtest.h>

#include <array>
#include <initializer_list>

namespace
{
	std::array<qm_ime_overlay::SCandidateMeasure, qm_ime_overlay::MAX_CANDIDATES> CandidateMeasures(std::initializer_list<float> TextWidths)
	{
		std::array<qm_ime_overlay::SCandidateMeasure, qm_ime_overlay::MAX_CANDIDATES> aMeasures{};
		int Index = 0;
		for(float TextWidth : TextWidths)
			aMeasures[Index++] = {18.0f, TextWidth};
		return aMeasures;
	}

	qm_ime_overlay::SCandidateLayoutConfig LayoutConfig(float MaxPanelWidth)
	{
		qm_ime_overlay::SCandidateLayoutConfig Config;
		Config.m_Gap = 6.0f;
		Config.m_TrailingWidth = 24.0f;
		Config.m_PaddingX = 6.0f;
		Config.m_MinPanelWidth = 100.0f;
		Config.m_MaxPanelWidth = MaxPanelWidth;
		Config.m_MinTextWidth = 16.0f;
		return Config;
	}

	void ExpectAllCellsInsidePanel(const qm_ime_overlay::SCandidateRowLayout &Layout, const qm_ime_overlay::SCandidateRowPresentation &Presentation, const CUIRect &Panel)
	{
		float PreviousRight = Panel.x;
		for(int i = 0; i < Layout.m_Count; ++i)
		{
			SCOPED_TRACE(i);
			const auto &Cell = Layout.m_aCells[i];
			const CUIRect Rect = Presentation.Transform({Cell.m_X, 0.0f, Cell.m_Width, 16.0f});
			EXPECT_GT(Rect.w, 0.0f);
			EXPECT_GT(Rect.h, 0.0f);
			EXPECT_GE(Rect.x, PreviousRight);
			EXPECT_LE(Rect.x + Rect.w, Panel.x + Panel.w + 0.001f);
			EXPECT_GE(Rect.y, Panel.y);
			EXPECT_LE(Rect.y + Rect.h, Panel.y + Panel.h + 0.001f);
			PreviousRight = Rect.x + Rect.w;
		}
		const CUIRect TrailingRect = Presentation.Transform({Layout.m_ContentWidth - 24.0f, 0.0f, 24.0f, 16.0f});
		EXPECT_GE(TrailingRect.x + 0.001f, PreviousRight);
		EXPECT_LE(TrailingRect.x + TrailingRect.w, Panel.x + Panel.w + 0.001f);
	}
}

TEST(QmImeCandidateLayout, LongPageExpandsWithoutHidingCandidates)
{
	const auto aMeasures = CandidateMeasures({200.0f, 110.0f, 65.0f, 40.0f, 45.0f});
	const auto Layout = qm_ime_overlay::BuildCandidateRowLayout(aMeasures, 5, LayoutConfig(760.0f));

	ASSERT_EQ(Layout.m_Count, 5);
	EXPECT_FLOAT_EQ(Layout.m_PanelWidth, 610.0f);
	for(int i = 0; i < Layout.m_Count; ++i)
	{
		SCOPED_TRACE(i);
		EXPECT_FLOAT_EQ(Layout.m_aCells[i].m_TextWidth, aMeasures[i].m_TextWidth);
	}
}

TEST(QmImeCandidateLayout, ScreenLimitShortensLongEntriesAndPreservesShortWords)
{
	const auto aMeasures = CandidateMeasures({600.0f, 300.0f, 40.0f, 30.0f, 25.0f});
	const auto Layout = qm_ime_overlay::BuildCandidateRowLayout(aMeasures, 5, LayoutConfig(450.0f));

	ASSERT_EQ(Layout.m_Count, 5);
	EXPECT_FLOAT_EQ(Layout.m_PanelWidth, 450.0f);
	EXPECT_FLOAT_EQ(Layout.m_aCells[0].m_TextWidth, 102.5f);
	EXPECT_FLOAT_EQ(Layout.m_aCells[1].m_TextWidth, 102.5f);
	EXPECT_FLOAT_EQ(Layout.m_aCells[2].m_TextWidth, 40.0f);
	EXPECT_FLOAT_EQ(Layout.m_aCells[3].m_TextWidth, 30.0f);
	EXPECT_FLOAT_EQ(Layout.m_aCells[4].m_TextWidth, 25.0f);
	const auto &LastCell = Layout.m_aCells[4];
	EXPECT_LE(LastCell.m_X + LastCell.m_Width, Layout.m_ContentWidth - 24.0f);
}

TEST(QmImeCandidateLayout, SingleLongCandidateStaysWithinScreenLimit)
{
	const auto Layout = qm_ime_overlay::BuildCandidateRowLayout(CandidateMeasures({1200.0f}), 1, LayoutConfig(450.0f));

	ASSERT_EQ(Layout.m_Count, 1);
	EXPECT_FLOAT_EQ(Layout.m_PanelWidth, 450.0f);
	EXPECT_FLOAT_EQ(Layout.m_aCells[0].m_TextWidth, 396.0f);
}

TEST(QmImeCandidateLayout, ShortPageDoesNotInventCandidates)
{
	const auto Layout = qm_ime_overlay::BuildCandidateRowLayout(CandidateMeasures({20.0f, 30.0f, 40.0f}), 3, LayoutConfig(450.0f));

	EXPECT_EQ(Layout.m_Count, 3);
	EXPECT_FLOAT_EQ(Layout.m_aCells[2].m_TextWidth, 40.0f);
	EXPECT_FLOAT_EQ(Layout.m_aCells[3].m_Width, 0.0f);
}

TEST(QmImeCandidateLayout, AnimatedWidthKeepsFifthCandidateAndFontSizeStable)
{
	const auto Layout = qm_ime_overlay::BuildCandidateRowLayout(CandidateMeasures({40.0f, 40.0f, 40.0f, 40.0f, 40.0f}), 5, LayoutConfig(760.0f));
	const CUIRect Bounds = {4.0f, 4.0f, 760.0f, 292.0f};
	for(float AnimatedWidth : {40.0f, Layout.m_PanelWidth - 0.001f, Layout.m_PanelWidth, Layout.m_PanelWidth + 0.001f, 500.0f})
	{
		SCOPED_TRACE(AnimatedWidth);
		const CUIRect Panel = qm_ime_overlay::FitCandidatePanel(Layout, {600.0f, 10.0f, AnimatedWidth, 18.0f}, 22.0f, Bounds);
		const auto Presentation = qm_ime_overlay::BuildCandidateRowPresentation(Layout, Panel, 16.0f, 6.0f, 3.0f, 1.0f);

		ASSERT_EQ(Layout.m_Count, 5);
		EXPECT_FLOAT_EQ(Presentation.m_Scale, 1.0f);
		EXPECT_GE(Panel.w, Layout.m_PanelWidth);
		EXPECT_GE(Panel.x, Bounds.x);
		EXPECT_LE(Panel.x + Panel.w, Bounds.x + Bounds.w);
		ExpectAllCellsInsidePanel(Layout, Presentation, Panel);
	}
}

TEST(QmImeCandidateLayout, EnterAnimationKeepsWholeRowInsidePanel)
{
	const auto Layout = qm_ime_overlay::BuildCandidateRowLayout(CandidateMeasures({200.0f, 110.0f, 65.0f, 40.0f, 45.0f}), 5, LayoutConfig(760.0f));
	const CUIRect Panel = qm_ime_overlay::FitCandidatePanel(Layout, {600.0f, 295.0f, 40.0f, 18.0f}, 22.0f, {4.0f, 4.0f, 760.0f, 292.0f});
	for(float ContentScale : {0.84f, 0.92f, 1.0f, 1.01f})
	{
		SCOPED_TRACE(ContentScale);
		const auto Presentation = qm_ime_overlay::BuildCandidateRowPresentation(Layout, Panel, 16.0f, 6.0f, 3.0f, ContentScale);
		EXPECT_LE(Presentation.m_Scale, 1.0f);
		ExpectAllCellsInsidePanel(Layout, Presentation, Panel);
	}
	EXPECT_LE(Panel.y + Panel.h, 296.0f);
}

TEST(QmImeCandidateLayout, VeryNarrowScreenPreservesEveryCandidate)
{
	const auto Layout = qm_ime_overlay::BuildCandidateRowLayout(CandidateMeasures({600.0f, 300.0f, 40.0f, 30.0f, 25.0f}), 5, LayoutConfig(100.0f));
	const CUIRect Panel = qm_ime_overlay::FitCandidatePanel(Layout, {20.0f, 10.0f, 40.0f, 22.0f}, 22.0f, {4.0f, 4.0f, 100.0f, 292.0f});
	const auto Presentation = qm_ime_overlay::BuildCandidateRowPresentation(Layout, Panel, 16.0f, 6.0f, 3.0f, 1.0f);

	ASSERT_EQ(Layout.m_Count, 5);
	EXPECT_FLOAT_EQ(Panel.w, 100.0f);
	EXPECT_GT(Presentation.m_Scale, 0.0f);
	EXPECT_LT(Presentation.m_Scale, 1.0f);
	for(int i = 0; i < Layout.m_Count; ++i)
		EXPECT_GE(Layout.m_aCells[i].m_TextWidth, 16.0f);
	ExpectAllCellsInsidePanel(Layout, Presentation, Panel);
}

TEST(QmImeCandidateLayout, ReplacingLongPageRestoresFullShortTextDuringShrink)
{
	const auto LongLayout = qm_ime_overlay::BuildCandidateRowLayout(CandidateMeasures({600.0f, 300.0f, 40.0f, 30.0f, 25.0f}), 5, LayoutConfig(450.0f));
	const auto ShortLayout = qm_ime_overlay::BuildCandidateRowLayout(CandidateMeasures({20.0f, 20.0f, 20.0f, 20.0f, 20.0f}), 5, LayoutConfig(450.0f));
	const CUIRect Panel = qm_ime_overlay::FitCandidatePanel(ShortLayout, {4.0f, 10.0f, LongLayout.m_PanelWidth, 22.0f}, 22.0f, {4.0f, 4.0f, 760.0f, 292.0f});
	const auto Presentation = qm_ime_overlay::BuildCandidateRowPresentation(ShortLayout, Panel, 16.0f, 6.0f, 3.0f, 1.0f);

	ASSERT_EQ(ShortLayout.m_Count, 5);
	EXPECT_LT(ShortLayout.m_PanelWidth, LongLayout.m_PanelWidth);
	EXPECT_FLOAT_EQ(Panel.w, LongLayout.m_PanelWidth);
	EXPECT_FLOAT_EQ(Presentation.m_Scale, 1.0f);
	for(int i = 0; i < ShortLayout.m_Count; ++i)
		EXPECT_FLOAT_EQ(ShortLayout.m_aCells[i].m_TextWidth, 20.0f);
	ExpectAllCellsInsidePanel(ShortLayout, Presentation, Panel);
}

TEST(QmImeCandidateLayout, EmptyPageHasNoCells)
{
	const auto Layout = qm_ime_overlay::BuildCandidateRowLayout(CandidateMeasures({}), 0, LayoutConfig(450.0f));

	EXPECT_EQ(Layout.m_Count, 0);
	EXPECT_FLOAT_EQ(Layout.m_ContentWidth, 0.0f);
	EXPECT_FLOAT_EQ(Layout.m_PanelWidth, 0.0f);
}
