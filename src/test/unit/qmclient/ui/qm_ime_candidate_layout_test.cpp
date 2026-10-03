#include <game/client/QmUi/QmImeCandidateLayout.h>
#include <game/client/qm_ime_candidate_popup.h>

#include <engine/shared/config.h>

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

TEST(QmImeCandidateLayout, GrowingLongPageKeepsAnimatedWidthAndAllCandidates)
{
	const auto Config = LayoutConfig(760.0f);
	const auto aMeasures = CandidateMeasures({200.0f, 110.0f, 65.0f, 40.0f, 45.0f});
	const auto ShortLayout = qm_ime_overlay::BuildCandidateRowLayout(CandidateMeasures({20.0f, 20.0f, 20.0f, 20.0f, 20.0f}), 5, Config);
	const auto TargetLayout = qm_ime_overlay::BuildCandidateRowLayout(aMeasures, 5, Config);
	const CUIRect Bounds = {4.0f, 4.0f, 760.0f, 292.0f};
	float PreviousTextWidth = 0.0f;
	for(float AnimatedWidth : {ShortLayout.m_PanelWidth, 350.0f, 500.0f, TargetLayout.m_PanelWidth})
	{
		SCOPED_TRACE(AnimatedWidth);
		const CUIRect Panel = qm_ime_overlay::FitCandidatePanel({600.0f, 10.0f, AnimatedWidth, 22.0f}, Bounds);
		const auto Layout = qm_ime_overlay::BuildCandidateRowLayoutForPanel(aMeasures, 5, Config, Panel);
		const auto Presentation = qm_ime_overlay::BuildCandidateRowPresentation(Layout, Panel, 16.0f, 6.0f, 3.0f, 1.0f);

		ASSERT_EQ(Layout.m_Count, 5);
		EXPECT_FLOAT_EQ(Presentation.m_Scale, 1.0f);
		EXPECT_FLOAT_EQ(Panel.w, AnimatedWidth);
		EXPECT_GE(Panel.x, Bounds.x);
		EXPECT_LE(Panel.x + Panel.w, Bounds.x + Bounds.w);
		EXPECT_GE(Layout.m_aCells[0].m_TextWidth, PreviousTextWidth);
		PreviousTextWidth = Layout.m_aCells[0].m_TextWidth;
		ExpectAllCellsInsidePanel(Layout, Presentation, Panel);
	}
	EXPECT_FLOAT_EQ(PreviousTextWidth, aMeasures[0].m_TextWidth);
}

TEST(QmImeCandidateLayout, EnterAnimationKeepsWholeRowInsidePanel)
{
	const auto aMeasures = CandidateMeasures({200.0f, 110.0f, 65.0f, 40.0f, 45.0f});
	const CUIRect Panel = qm_ime_overlay::FitCandidatePanel({600.0f, 295.0f, 300.0f, 22.0f}, {4.0f, 4.0f, 760.0f, 292.0f});
	const auto Layout = qm_ime_overlay::BuildCandidateRowLayoutForPanel(aMeasures, 5, LayoutConfig(760.0f), Panel);
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
	const CUIRect Panel = qm_ime_overlay::FitCandidatePanel({20.0f, 10.0f, 450.0f, 22.0f}, {4.0f, 4.0f, 100.0f, 292.0f});
	const auto Layout = qm_ime_overlay::BuildCandidateRowLayoutForPanel(CandidateMeasures({600.0f, 300.0f, 40.0f, 30.0f, 25.0f}), 5, LayoutConfig(100.0f), Panel);
	const auto Presentation = qm_ime_overlay::BuildCandidateRowPresentation(Layout, Panel, 16.0f, 6.0f, 3.0f, 1.0f);

	ASSERT_EQ(Layout.m_Count, 5);
	EXPECT_FLOAT_EQ(Panel.w, 100.0f);
	EXPECT_GT(Presentation.m_Scale, 0.0f);
	EXPECT_LT(Presentation.m_Scale, 1.0f);
	for(int i = 0; i < Layout.m_Count; ++i)
		EXPECT_GE(Layout.m_aCells[i].m_TextWidth, 16.0f);
	ExpectAllCellsInsidePanel(Layout, Presentation, Panel);
}

TEST(QmImeCandidateLayout, NarrowScreenKeepsReadableTextBudgetThroughoutExpansion)
{
	const auto aMeasures = CandidateMeasures({600.0f, 300.0f, 40.0f, 30.0f, 25.0f});
	for(const float AnimatedWidth : {70.0f, 99.99f, 100.0f})
	{
		SCOPED_TRACE(AnimatedWidth);
		const CUIRect Panel = {4.0f, 10.0f, AnimatedWidth, 22.0f};
		const auto Layout = qm_ime_overlay::BuildCandidateRowLayoutForPanel(aMeasures, 5, LayoutConfig(100.0f), Panel);
		const auto Presentation = qm_ime_overlay::BuildCandidateRowPresentation(Layout, Panel, 16.0f, 6.0f, 3.0f, 1.0f);
		ASSERT_EQ(Layout.m_Count, 5);
		for(int i = 0; i < Layout.m_Count; ++i)
			EXPECT_GE(Layout.m_aCells[i].m_TextWidth, 16.0f);
		ExpectAllCellsInsidePanel(Layout, Presentation, Panel);
	}
}

TEST(QmImeCandidateLayout, ReplacingLongPageRestoresFullShortTextDuringShrink)
{
	const auto LongLayout = qm_ime_overlay::BuildCandidateRowLayout(CandidateMeasures({600.0f, 300.0f, 40.0f, 30.0f, 25.0f}), 5, LayoutConfig(450.0f));
	const auto aShortMeasures = CandidateMeasures({20.0f, 20.0f, 20.0f, 20.0f, 20.0f});
	const auto TargetShortLayout = qm_ime_overlay::BuildCandidateRowLayout(aShortMeasures, 5, LayoutConfig(450.0f));
	const CUIRect Panel = qm_ime_overlay::FitCandidatePanel({4.0f, 10.0f, LongLayout.m_PanelWidth, 22.0f}, {4.0f, 4.0f, 760.0f, 292.0f});
	const auto ShortLayout = qm_ime_overlay::BuildCandidateRowLayoutForPanel(aShortMeasures, 5, LayoutConfig(450.0f), Panel);
	const auto Presentation = qm_ime_overlay::BuildCandidateRowPresentation(ShortLayout, Panel, 16.0f, 6.0f, 3.0f, 1.0f);

	ASSERT_EQ(ShortLayout.m_Count, 5);
	EXPECT_LT(TargetShortLayout.m_PanelWidth, LongLayout.m_PanelWidth);
	EXPECT_FLOAT_EQ(Panel.w, LongLayout.m_PanelWidth);
	EXPECT_FLOAT_EQ(Presentation.m_Scale, 1.0f);
	EXPECT_FLOAT_EQ(Presentation.m_Origin.x, Panel.x + 6.0f);
	for(int i = 0; i < ShortLayout.m_Count; ++i)
		EXPECT_FLOAT_EQ(ShortLayout.m_aCells[i].m_TextWidth, 20.0f);
	ExpectAllCellsInsidePanel(ShortLayout, Presentation, Panel);
}

TEST(QmImeCandidateLayout, ReversingPageLengthKeepsCurrentPanelGeometry)
{
	const CUIRect AnimatedRect = {400.0f, 20.0f, 360.0f, 22.0f};
	const CUIRect Bounds = {4.0f, 4.0f, 760.0f, 292.0f};
	for(const auto &aMeasures : {CandidateMeasures({20.0f, 20.0f, 20.0f, 20.0f, 20.0f}), CandidateMeasures({600.0f, 300.0f, 40.0f, 30.0f, 25.0f})})
	{
		const CUIRect Panel = qm_ime_overlay::FitCandidatePanel(AnimatedRect, Bounds);
		const auto Layout = qm_ime_overlay::BuildCandidateRowLayoutForPanel(aMeasures, 5, LayoutConfig(760.0f), Panel);
		const auto Presentation = qm_ime_overlay::BuildCandidateRowPresentation(Layout, Panel, 16.0f, 6.0f, 3.0f, 1.0f);
		EXPECT_FLOAT_EQ(Panel.x, AnimatedRect.x);
		EXPECT_FLOAT_EQ(Panel.w, AnimatedRect.w);
		EXPECT_FLOAT_EQ(Presentation.m_Scale, 1.0f);
		EXPECT_FLOAT_EQ(Presentation.m_Origin.x, Panel.x + 6.0f);
		ExpectAllCellsInsidePanel(Layout, Presentation, Panel);
	}
}

TEST(QmImeCandidateLayout, ReplacingSingleCharactersWithLongWordsKeepsFontSize)
{
	const auto Config = LayoutConfig(760.0f);
	const auto ShortLayout = qm_ime_overlay::BuildCandidateRowLayout(CandidateMeasures({8.0f, 8.0f, 8.0f, 8.0f, 8.0f}), 5, Config);
	const CUIRect Panel = {4.0f, 20.0f, ShortLayout.m_PanelWidth, 22.0f};
	const auto Layout = qm_ime_overlay::BuildCandidateRowLayoutForPanel(CandidateMeasures({200.0f, 200.0f, 200.0f, 200.0f, 200.0f}), 5, Config, Panel);
	const auto Presentation = qm_ime_overlay::BuildCandidateRowPresentation(Layout, Panel, 16.0f, 6.0f, 3.0f, 1.0f);

	ASSERT_EQ(Layout.m_Count, 5);
	EXPECT_FLOAT_EQ(Presentation.m_Scale, 1.0f);
	ExpectAllCellsInsidePanel(Layout, Presentation, Panel);
}

TEST(QmImeCandidateLayout, EmptyPageHasNoCells)
{
	const auto Layout = qm_ime_overlay::BuildCandidateRowLayout(CandidateMeasures({}), 0, LayoutConfig(450.0f));

	EXPECT_EQ(Layout.m_Count, 0);
	EXPECT_FLOAT_EQ(Layout.m_ContentWidth, 0.0f);
	EXPECT_FLOAT_EQ(Layout.m_PanelWidth, 0.0f);
}

class CQmImeCandidateTransitionTest : public ::testing::Test
{
protected:
	CUiV2AnimationRuntime m_Runtime;
	CQmImeCandidateTransition m_Transition;
	int m_OldMotionLevel = 0;

	void SetUp() override
	{
		m_OldMotionLevel = g_Config.m_QmUiMotionLevel;
		g_Config.m_QmUiMotionLevel = 2;
	}

	void TearDown() override
	{
		g_Config.m_QmUiMotionLevel = m_OldMotionLevel;
	}

	static SQmImePopupState Page(std::initializer_list<const char *> Candidates)
	{
		SQmImePopupState State;
		State.m_Visible = true;
		State.m_vCandidates.assign(Candidates.begin(), Candidates.end());
		State.m_SelectedIndex = 0;
		return State;
	}

	void Update(const SQmImePopupState &State, int CandidateStart = 0, bool Animate = true)
	{
		m_Transition.Update(m_Runtime, 123, State, CandidateStart, Animate);
	}

	void Advance(const SQmImePopupState &State, int Frames)
	{
		for(int i = 0; i < Frames; ++i)
		{
			m_Runtime.Advance(1.0f / 120.0f);
			Update(State);
		}
	}

	float AlphaFor(const char *pFirstCandidate) const
	{
		float Alpha = 0.0f;
		for(const auto &Layer : m_Transition.Layers())
		{
			if(Layer.m_Active && Layer.m_State.m_vCandidates.front() == pFirstCandidate)
				Alpha += Layer.m_Alpha;
		}
		return Alpha;
	}

	int ActiveLayers() const
	{
		return (int)std::count_if(m_Transition.Layers().begin(), m_Transition.Layers().end(), [](const auto &Layer) { return Layer.m_Active; });
	}
};

TEST_F(CQmImeCandidateTransitionTest, LongCandidatesFadeInWithoutReplacingVisibleTextAtOnce)
{
	const auto Short = Page({"a", "b", "c"});
	const auto Long = Page({"a much longer candidate", "another long candidate", "c"});
	Update(Short);
	Update(Long);
	EXPECT_FLOAT_EQ(AlphaFor("a"), 1.0f);
	EXPECT_FLOAT_EQ(AlphaFor("a much longer candidate"), 0.0f);

	Advance(Long, 3);
	EXPECT_GT(AlphaFor("a"), 0.0f);
	EXPECT_LT(AlphaFor("a"), 1.0f);
	EXPECT_GT(AlphaFor("a much longer candidate"), 0.0f);
	EXPECT_LT(AlphaFor("a much longer candidate"), 1.0f);
	EXPECT_NEAR(AlphaFor("a") + AlphaFor("a much longer candidate"), 1.0f, 0.0001f);
}

TEST_F(CQmImeCandidateTransitionTest, RapidTypingPreservesTheVisibleBlendAndUsesLatestCandidates)
{
	const auto First = Page({"first"});
	const auto Second = Page({"second, longer candidate"});
	const auto Latest = Page({"latest"});
	Update(First);
	Update(Second);
	Advance(Second, 3);
	const float FirstAlpha = AlphaFor("first");
	const float SecondAlpha = AlphaFor("second, longer candidate");

	Update(Latest);
	EXPECT_FLOAT_EQ(AlphaFor("first"), FirstAlpha);
	EXPECT_FLOAT_EQ(AlphaFor("second, longer candidate"), SecondAlpha);
	EXPECT_FLOAT_EQ(AlphaFor("latest"), 0.0f);
	EXPECT_EQ(m_Transition.Layers()[m_Transition.CurrentLayerIndex()].m_State.m_vCandidates, Latest.m_vCandidates);

	Advance(Latest, 120);
	EXPECT_EQ(ActiveLayers(), 1);
	EXPECT_FLOAT_EQ(AlphaFor("latest"), 1.0f);
}

TEST_F(CQmImeCandidateTransitionTest, SelectionAndCaretMovementDoNotRestartTheFade)
{
	const auto First = Page({"first", "second"});
	auto Next = Page({"long first", "long second"});
	Update(First);
	Update(Next);
	Advance(Next, 3);
	const int CurrentLayer = m_Transition.CurrentLayerIndex();
	const float Alpha = AlphaFor("long first");

	Next.m_SelectedIndex = 1;
	Next.m_AnchorScreen = vec2(350.0f, 250.0f);
	Next.m_Composition = "changed composition";
	Update(Next);
	EXPECT_EQ(m_Transition.CurrentLayerIndex(), CurrentLayer);
	EXPECT_EQ(m_Transition.Layers()[CurrentLayer].m_State.m_SelectedIndex, 1);
	EXPECT_FLOAT_EQ(AlphaFor("long first"), Alpha);
	EXPECT_EQ(ActiveLayers(), 2);
}

TEST_F(CQmImeCandidateTransitionTest, ReturningToPreviousCandidatesKeepsTheCurrentOpacity)
{
	const auto First = Page({"short"});
	const auto Second = Page({"a much longer candidate"});
	Update(First);
	Update(Second);
	Advance(Second, 3);
	const float FirstAlpha = AlphaFor("short");
	const float SecondAlpha = AlphaFor("a much longer candidate");

	Update(First);
	EXPECT_FLOAT_EQ(AlphaFor("short"), FirstAlpha);
	EXPECT_FLOAT_EQ(AlphaFor("a much longer candidate"), SecondAlpha);
	Advance(First, 120);
	EXPECT_EQ(ActiveLayers(), 1);
	EXPECT_FLOAT_EQ(AlphaFor("short"), 1.0f);
}

TEST_F(CQmImeCandidateTransitionTest, DisablingMotionImmediatelyFinishesAnInterruptedFade)
{
	const auto First = Page({"first"});
	const auto Second = Page({"second"});
	const auto Latest = Page({"latest"});
	Update(First);
	Update(Second);
	Advance(Second, 3);
	Update(Latest, 0, false);

	EXPECT_EQ(ActiveLayers(), 1);
	EXPECT_FLOAT_EQ(AlphaFor("latest"), 1.0f);
	EXPECT_EQ(m_Runtime.ActiveTrackCount(), 0);
}

TEST_F(CQmImeCandidateTransitionTest, ResetDropsOldContentBeforeTheNextPopup)
{
	const auto First = Page({"first"});
	const auto Second = Page({"second"});
	const auto Reopened = Page({"reopened"});
	Update(First);
	Update(Second);
	Advance(Second, 3);
	m_Transition.Reset();
	Update(Reopened);

	EXPECT_EQ(ActiveLayers(), 1);
	EXPECT_FLOAT_EQ(AlphaFor("first"), 0.0f);
	EXPECT_FLOAT_EQ(AlphaFor("second"), 0.0f);
	EXPECT_FLOAT_EQ(AlphaFor("reopened"), 1.0f);
}

TEST_F(CQmImeCandidateTransitionTest, PageIndicatorChangesFadeWithTheCandidates)
{
	auto State = Page({"same candidate"});
	State.m_PageCount = 3;
	State.m_PageIndex = 0;
	Update(State);
	const int PreviousLayer = m_Transition.CurrentLayerIndex();
	State.m_PageIndex = 1;
	Update(State);

	ASSERT_EQ(ActiveLayers(), 2);
	EXPECT_EQ(m_Transition.Layers()[PreviousLayer].m_State.m_PageIndex, 0);
	EXPECT_EQ(m_Transition.Layers()[m_Transition.CurrentLayerIndex()].m_State.m_PageIndex, 1);
	EXPECT_FLOAT_EQ(m_Transition.Layers()[PreviousLayer].m_Alpha, 1.0f);
}

TEST_F(CQmImeCandidateTransitionTest, MovingBeyondTheVisibleRowFadesTheShiftedCandidates)
{
	auto State = Page({"one", "two", "three", "four", "five", "six", "seven", "eight"});
	Update(State);
	const int PreviousLayer = m_Transition.CurrentLayerIndex();
	State.m_SelectedIndex = 7;
	Update(State, 1);

	ASSERT_EQ(ActiveLayers(), 2);
	EXPECT_EQ(m_Transition.Layers()[PreviousLayer].m_CandidateStart, 0);
	EXPECT_EQ(m_Transition.Layers()[m_Transition.CurrentLayerIndex()].m_CandidateStart, 1);
	EXPECT_FLOAT_EQ(m_Transition.Layers()[PreviousLayer].m_Alpha, 1.0f);
}

TEST_F(CQmImeCandidateTransitionTest, ReusingFinishedLayersKeepsVisibleTextInTheSameDrawOrder)
{
	const auto First = Page({"first"});
	const auto Second = Page({"second"});
	const auto Third = Page({"third"});
	const auto Fourth = Page({"fourth"});
	const auto Latest = Page({"latest"});
	Update(First);
	Update(Second);
	Update(Third);
	Advance(Third, 120);
	Update(Fourth);
	Advance(Fourth, 3);
	const float ThirdAlpha = AlphaFor("third");
	const float FourthAlpha = AlphaFor("fourth");

	Update(Latest);
	std::vector<std::string> vDrawOrder;
	for(const auto &Layer : m_Transition.Layers())
	{
		if(Layer.m_Active)
			vDrawOrder.push_back(Layer.m_State.m_vCandidates.front());
	}
	EXPECT_EQ(vDrawOrder, (std::vector<std::string>{"third", "fourth", "latest"}));
	EXPECT_FLOAT_EQ(AlphaFor("third"), ThirdAlpha);
	EXPECT_FLOAT_EQ(AlphaFor("fourth"), FourthAlpha);
}

TEST_F(CQmImeCandidateTransitionTest, OldWordsFinishFadingWithinEightyMilliseconds)
{
	const auto First = Page({"old words"});
	const auto Latest = Page({"new words"});
	Update(First);
	Update(Latest);
	Advance(Latest, 6);
	EXPECT_LT(AlphaFor("old words"), 0.05f);
	EXPECT_GT(AlphaFor("new words"), 0.95f);

	m_Runtime.Advance(0.03f);
	Update(Latest);
	EXPECT_FLOAT_EQ(AlphaFor("old words"), 0.0f);
	EXPECT_FLOAT_EQ(AlphaFor("new words"), 1.0f);
	EXPECT_EQ(ActiveLayers(), 1);
}

TEST_F(CQmImeCandidateTransitionTest, TypingAgainDoesNotExtendEarlierWordsFadeOut)
{
	const auto First = Page({"first"});
	const auto Second = Page({"second"});
	const auto Latest = Page({"latest"});
	Update(First);
	Update(Second);
	Advance(Second, 7);
	ASSERT_GT(AlphaFor("first"), 0.0f);

	Update(Latest);
	Advance(Latest, 3);
	EXPECT_FLOAT_EQ(AlphaFor("first"), 0.0f);
	EXPECT_GT(AlphaFor("second"), 0.0f);
	EXPECT_GT(AlphaFor("latest"), 0.0f);

	Advance(Latest, 7);
	EXPECT_FLOAT_EQ(AlphaFor("second"), 0.0f);
	EXPECT_FLOAT_EQ(AlphaFor("latest"), 1.0f);
	EXPECT_EQ(ActiveLayers(), 1);
}
