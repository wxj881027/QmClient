// 公共内容声明、不可变行布局与输入回放；不复制页面行数或测量算法。
#include <game/client/QmUi/SettingsCardDeck.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPersonalization.h>

#include <gtest/gtest.h>

namespace
{
	std::vector<SSettingsContentRowDeclaration> ControlRows()
	{
		return {{"toggle", {}, 20.0f, 0.0f, 0.0f, 4.0f}, {"value", {}, 20.0f}};
	}
}

TEST(SettingsContentLayout, DeclarationDoesNotExecuteControlInputOrDrawing)
{
	std::vector<SSettingsContentRowDeclaration> vRows;
	int Executions = 0;
	CUIRect Content{0, 0, 320, 0}, Row;
	{
		CSettingsContentLayoutScope Scope(vRows);
		CSettingsContentLayoutFlow Flow(Content);
		if(Flow.Control("toggle", 20, Row))
			++Executions;
		Flow.Space(4);
		Flow.Text("help", "A description", 40, 10, 4, [&](const char *, float, float) { ++Executions; return 10.0f; }, [&](const CUIRect &, const char *) { ++Executions; });
	}
	ASSERT_EQ(vRows.size(), 2u);
	EXPECT_EQ(Executions, 0);
	EXPECT_EQ(vRows[0].m_Id, "toggle");
	EXPECT_FLOAT_EQ(vRows[0].m_Spacing, 4);
	EXPECT_EQ(vRows[1].m_Text, "A description");
}

TEST(SettingsContentLayout, InputGrowthPublishesFinalLayoutInTheSameFrame)
{
	CSettingsContentLayoutCache Cache;
	auto vRows = ControlRows();
	const auto pBefore = Cache.Resolve(vRows, 320, 1, {});
	std::shared_ptr<const CSettingsContentLayout> pFinal;
	CUIRect InputRow{};
	const bool Changed = ExecuteSettingsContentInputThenResolve(pBefore, {10, 30, 320, pBefore->Height()}, [&](const CSettingsContentLayout &Layout, CUIRect Content) {
			CSettingsContentLayoutScope Scope(Layout, ESettingsContentPass::INPUT);
			CSettingsContentLayoutFlow Flow(Content);
			ASSERT_TRUE(Flow.Control("toggle", 20, InputRow));
			vRows.push_back({"advanced", {}, 40});
			CUIRect Added{};
			EXPECT_FALSE(Flow.Control("advanced", 40, Added)); }, [&](float Width) { pFinal = Cache.Resolve(vRows, Width, 1, {}); return pFinal; });
	ASSERT_TRUE(Changed);
	ASSERT_NE(pFinal, pBefore);
	EXPECT_FLOAT_EQ(InputRow.y, 30);
	EXPECT_GT(pFinal->Height(), pBefore->Height());
	EXPECT_EQ(pBefore->Find("advanced"), nullptr);
	CUIRect DrawContent{10, 30, 320, pFinal->Height()}, DrawRow{};
	CSettingsContentLayoutScope Scope(*pFinal, ESettingsContentPass::DRAW);
	CSettingsContentLayoutFlow Flow(DrawContent);
	ASSERT_TRUE(Flow.Control("advanced", 999, DrawRow));
	EXPECT_FLOAT_EQ(DrawRow.h, 40); // 绘制消费不可变结果，不重新计算传入高度。
	EXPECT_FLOAT_EQ(DrawContent.y - 30, pFinal->Height());
}

TEST(SettingsContentLayout, InputShrinkAndRestorePreserveEarlierImmutableSnapshots)
{
	CSettingsContentLayoutCache Cache;
	auto vRows = ControlRows();
	const auto pFull = Cache.Resolve(vRows, 320, 1, {});
	std::shared_ptr<const CSettingsContentLayout> pEmpty;
	EXPECT_TRUE(ExecuteSettingsContentInputThenResolve(pFull, {0, 0, 320, pFull->Height()}, [&](const auto &, auto) { vRows.clear(); }, [&](float Width) { pEmpty = Cache.Resolve(vRows, Width, 1, {}); return pEmpty; }));
	ASSERT_NE(pEmpty, nullptr);
	EXPECT_FLOAT_EQ(pEmpty->Height(), 0);
	EXPECT_NE(pFull->Find("value"), nullptr);
	vRows = ControlRows();
	const auto pRestored = Cache.Resolve(vRows, 320, 1, {});
	EXPECT_FLOAT_EQ(pRestored->Height(), pFull->Height());
	EXPECT_FLOAT_EQ(pEmpty->Height(), 0);
}

TEST(SettingsContentLayout, UnchangedDeclarationsReuseTheSameLayoutWithoutTextMeasurement)
{
	CSettingsContentLayoutCache Cache;
	const std::vector<SSettingsContentRowDeclaration> vRows{{"help", "Long description", 10, 10, 40, 4, true}};
	int Calls = 0;
	const auto Measure = [&](const char *, float, float) { ++Calls; return 24.0f; };
	const auto pLayout = Cache.Resolve(vRows, 320, 1, Measure);
	for(int Frame = 0; Frame < 20; ++Frame)
		EXPECT_EQ(Cache.Resolve(vRows, 320, 1, Measure), pLayout);
	EXPECT_EQ(Calls, 1);
}

TEST(SettingsContentLayout, ChangingOneCardKeepsStableNeighborMeasurementsCached)
{
	CSettingsContentLayoutCache Dynamic, Neighbor;
	const std::vector<SSettingsContentRowDeclaration> vNeighbor{{"help", "Stable text", 10, 10, 0, 0, true}};
	int NeighborMeasures = 0;
	const auto Measure = [&](const char *, float, float) { ++NeighborMeasures; return 12.0f; };
	const auto pNeighbor = Neighbor.Resolve(vNeighbor, 320, 1, Measure);
	auto vDynamic = ControlRows();
	const auto pOld = Dynamic.Resolve(vDynamic, 320, 1, {});
	vDynamic.push_back({"diagnostic", {}, 30});
	EXPECT_NE(Dynamic.Resolve(vDynamic, 320, 1, {}), pOld);
	EXPECT_EQ(Neighbor.Resolve(vNeighbor, 320, 1, Measure), pNeighbor);
	EXPECT_EQ(NeighborMeasures, 1);
}

TEST(SettingsContentLayout, EqualLengthDifferentTextInvalidatesWithoutManualRevision)
{
	CSettingsContentLayoutCache Cache;
	std::vector<SSettingsContentRowDeclaration> vRows{{"help", "WWW", 10, 10, 0, 0, true}};
	std::vector<std::string> vMeasured;
	const auto Measure = [&](const char *pText, float, float) { vMeasured.emplace_back(pText); return 12.0f; };
	const auto pWide = Cache.Resolve(vRows, 100, 1, Measure);
	vRows[0].m_Text = "iii";
	EXPECT_NE(Cache.Resolve(vRows, 100, 1, Measure), pWide);
	ASSERT_EQ(vMeasured.size(), 2u);
	EXPECT_EQ(vMeasured[1], "iii");
}

TEST(SettingsContentLayout, WidthFontAndVisibleRowsEachInvalidateTheLayout)
{
	CSettingsContentLayoutCache Cache;
	auto vRows = ControlRows();
	const auto pInitial = Cache.Resolve(vRows, 320, 1, {});
	const auto pNarrow = Cache.Resolve(vRows, 120, 1, {});
	EXPECT_NE(pInitial, pNarrow);
	const auto pFont = Cache.Resolve(vRows, 120, 2, {});
	EXPECT_NE(pFont, pNarrow);
	vRows.pop_back();
	const auto pRemoved = Cache.Resolve(vRows, 120, 2, {});
	EXPECT_NE(pRemoved, pFont);
	EXPECT_LT(pRemoved->Height(), pFont->Height());
	EXPECT_EQ(pFont->Rows().size(), 2u);
}

TEST(SettingsContentLayout, TextDrawingUsesThePublishedTextAndTranslatedRowOrigin)
{
	CSettingsContentLayoutCache Cache;
	const std::vector<SSettingsContentRowDeclaration> vRows{{"help", "Published text", 10, 10, 20, 4, true}};
	const auto pLayout = Cache.Resolve(vRows, 200, 1, [](const char *, float, float) { return 12.0f; });
	CUIRect Content{80, 100, 200, 1};
	CUIRect Submitted{};
	std::string Text;
	CSettingsContentLayoutScope Scope(*pLayout, ESettingsContentPass::DRAW);
	CSettingsContentLayoutFlow Flow(Content);
	Flow.Text("help", "Changed during draw", 999, 999, 999, [](const char *, float, float) -> float { ADD_FAILURE() << "draw must not measure"; return 999; }, [&](const CUIRect &Row, const char *pText) { Submitted = Row; Text = pText; });
	EXPECT_EQ(Text, "Published text");
	EXPECT_FLOAT_EQ(Submitted.x, 100);
	EXPECT_FLOAT_EQ(Submitted.y, 100);
	EXPECT_FLOAT_EQ(Submitted.h, 12);
	EXPECT_FLOAT_EQ(Content.y - 100, pLayout->Height());
}

TEST(SettingsContentLayout, EmptyLayoutDoesNotRunInputWithoutASnapshot)
{
	int Calls = 0;
	EXPECT_FALSE(ExecuteSettingsContentInputThenResolve({}, {}, [&](const auto &, auto) { ++Calls; }, [&](float) { ++Calls; return std::shared_ptr<const CSettingsContentLayout>{}; }));
	EXPECT_EQ(Calls, 0);
}

TEST(SettingsContentLayout, ChainedContentHelpersReplayFromTheSameCardOrigin)
{
	const auto Helpers = [](CUIRect &Content, CUIRect &First, CUIRect &Second) {
		{
			CSettingsContentLayoutFlow Prefix(Content);
			Prefix.Control("prefix", 20, First);
			Prefix.Space(4);
		}
		CSettingsContentLayoutFlow Body(Content);
		Body.Control("body", 30, Second);
	};
	std::vector<SSettingsContentRowDeclaration> vRows;
	CUIRect Content{80, 100, 200, 0}, First{}, Second{};
	{
		CSettingsContentLayoutScope Scope(vRows);
		Helpers(Content, First, Second);
	}
	CSettingsContentLayoutCache Cache;
	const auto pLayout = Cache.Resolve(vRows, 200, 1, {});
	for(const auto Pass : {ESettingsContentPass::INPUT, ESettingsContentPass::DRAW})
	{
		Content = {80, 100, 200, pLayout->Height()};
		CSettingsContentLayoutScope Scope(*pLayout, Pass);
		Helpers(Content, First, Second);
		EXPECT_FLOAT_EQ(First.y, 100);
		EXPECT_FLOAT_EQ(Second.y, 124);
		EXPECT_FLOAT_EQ(Content.y, 154);
		EXPECT_FLOAT_EQ(Content.h, 0);
	}
}

TEST(SettingsContentLayout, BetweenRowSpacingHasNoLeadingOrTrailingGap)
{
	std::vector<SSettingsContentRowDeclaration> vRows;
	CUIRect Content{0, 0, 320, 100}, Row{};
	{
		CSettingsContentLayoutScope Scope(vRows);
		CSettingsContentLayoutFlow Flow(Content);
		Flow.Control("first", 20, Row, 4);
		Flow.Control("second", 20, Row, 4);
	}
	CSettingsContentLayoutCache Cache;
	const auto pLayout = Cache.Resolve(vRows, 320, 1, {});
	EXPECT_FLOAT_EQ(pLayout->Find("first")->m_Rect.y, 0);
	EXPECT_FLOAT_EQ(pLayout->Find("second")->m_Rect.y, 24);
	EXPECT_FLOAT_EQ(pLayout->Height(), 44);
	CSettingsContentLayoutFlow Legacy(Content);
	Legacy.Control("first", 20, Row, 4);
	Legacy.Control("second", 20, Row, 4);
	EXPECT_FLOAT_EQ(Content.y, pLayout->Height());
}

TEST(SettingsContentLayout, TextMinimumHeightIsPreservedUntilWrappedTextExceedsIt)
{
	std::vector<SSettingsContentRowDeclaration> vRows;
	CUIRect Content{0, 0, 320, 100};
	int DrawCalls = 0;
	const auto Measure = [](const char *, float Width, float) { return Width < 100 ? 60.0f : 10.0f; };
	const auto Draw = [&](const CUIRect &Row, const char *) { ++DrawCalls; EXPECT_FLOAT_EQ(Row.h, 40); };
	{
		CSettingsContentLayoutScope Scope(vRows);
		CSettingsContentLayoutFlow Flow(Content);
		Flow.Text("status", "Message", 0, 10, 0, Measure, Draw, 40);
	}
	EXPECT_EQ(DrawCalls, 0);
	CSettingsContentLayoutCache Cache;
	EXPECT_FLOAT_EQ(Cache.Resolve(vRows, 320, 1, Measure)->Height(), 40);
	EXPECT_FLOAT_EQ(Cache.Resolve(vRows, 80, 1, Measure)->Height(), 60);
	CSettingsContentLayoutFlow Legacy(Content);
	Legacy.Text("status", "Message", 0, 10, 0, Measure, Draw, 40);
	EXPECT_EQ(DrawCalls, 1);
	EXPECT_FLOAT_EQ(Content.y, 40);
}

TEST(SettingsContentLayout, MultilineControlCachesTextMetricsAndPublishesTheFullRow)
{
	std::vector<SSettingsContentRowDeclaration> vRows;
	CUIRect Content{0, 0, 200, 0}, Row{};
	{
		CSettingsContentLayoutScope Scope(vRows);
		CSettingsContentLayoutFlow Flow(Content);
		EXPECT_FALSE(Flow.MeasuredControl("words", "Original words", 20, 10, 80, 2, 2, Row, {}));
	}
	CSettingsContentLayoutCache Cache;
	int Measures = 0;
	const auto Measure = [&](const SSettingsContentRowDeclaration &Declaration, float Width) {
		++Measures;
		EXPECT_FLOAT_EQ(Width, 120);
		EXPECT_FLOAT_EQ(Declaration.m_TextLineSpacing, 2);
		EXPECT_FLOAT_EQ(Declaration.m_TextPadding, 2);
		return 40.0f;
	};
	const auto pOriginal = Cache.Resolve(vRows, 200, 1, {}, Measure);
	EXPECT_EQ(Cache.Resolve(vRows, 200, 1, {}, Measure), pOriginal);
	EXPECT_EQ(Measures, 1);
	EXPECT_FLOAT_EQ(pOriginal->Find("words")->m_Rect.x, 0);
	EXPECT_FLOAT_EQ(pOriginal->Find("words")->m_Rect.w, 200);
	vRows[0].m_Text = "Changed words";
	const auto pChanged = Cache.Resolve(vRows, 200, 1, {}, Measure);
	EXPECT_NE(pChanged, pOriginal);
	EXPECT_EQ(Measures, 2);
	EXPECT_EQ(pOriginal->Find("words")->m_Declaration.m_Text, "Original words");
	Content = {50, 100, 200, 40};
	CSettingsContentLayoutScope Scope(*pChanged, ESettingsContentPass::DRAW);
	CSettingsContentLayoutFlow Flow(Content);
	ASSERT_TRUE(Flow.MeasuredControl("words", "Draw must not measure", 999, 999, 0, 0, 0, Row, [&](const auto &, float) { ADD_FAILURE(); return 999.0f; }));
	EXPECT_FLOAT_EQ(Row.x, 50);
	EXPECT_FLOAT_EQ(Row.y, 100);
	EXPECT_FLOAT_EQ(Row.w, 200);
	EXPECT_FLOAT_EQ(Row.h, 40);
	EXPECT_EQ(Measures, 2);
}

namespace
{
	class CPersonalizationCard
	{
		CSettingsPersonalizationState &m_State;
		const char *m_pStableId;
		CSettingsContentLayoutCache m_Cache;

		void Content(CUIRect &Rect, bool Clicked, bool ReadOnly)
		{
			CSettingsContentLayoutFlow Flow(Rect);
			CUIRect Row;
			Flow.Control("basic", 20, Row);
			Flow.Space(4);
			if(SettingsPersonalizationSection(m_State, Rect, m_pStableId, 20, 4, ReadOnly,
				[&](const void *pId, const CUIRect &, bool) {
					m_pButtonId = pId;
					++m_ButtonCalls;
					return Clicked;
				}))
			{
				if(Flow.Control("detail", 30, Row) && Flow.Input())
					++m_DetailInputs;
			}
		}

	public:
		int m_ButtonCalls = 0;
		int m_DetailInputs = 0;
		const void *m_pButtonId = nullptr;

		CPersonalizationCard(CSettingsPersonalizationState &State, const char *pStableId) : m_State(State), m_pStableId(pStableId) {}

		std::shared_ptr<const CSettingsContentLayout> Resolve()
		{
			std::vector<SSettingsContentRowDeclaration> vRows;
			{
				CSettingsContentLayoutScope Scope(vRows);
				CUIRect Rect{0, 0, 320, 0};
				Content(Rect, true, false);
			}
			return m_Cache.Resolve(vRows, 320, 0, {});
		}

		void Replay(const CSettingsContentLayout &Layout, ESettingsContentPass Pass, bool Clicked, bool ReadOnly = false)
		{
			CSettingsContentLayoutScope Scope(Layout, Pass);
			CUIRect Rect{0, 0, Layout.Width(), Layout.Height()};
			Content(Rect, Clicked, ReadOnly);
		}
	};
}

TEST(SettingsPersonalization, NewCardsAreCollapsedAndDeclarationDoesNotProcessInput)
{
	CSettingsPersonalizationState State;
	CPersonalizationCard Card(State, "card-a");
	const auto pLayout = Card.Resolve();
	EXPECT_FALSE(State.Expanded("card-a"));
	EXPECT_FALSE(State.Expanded("card-b"));
	EXPECT_NE(pLayout->Find("basic"), nullptr);
	EXPECT_NE(pLayout->Find("personalization"), nullptr);
	EXPECT_EQ(pLayout->Find("detail"), nullptr);
	EXPECT_FLOAT_EQ(pLayout->Height(), 48);
	EXPECT_EQ(Card.m_ButtonCalls, 0);
	EXPECT_EQ(Card.m_DetailInputs, 0);
}

TEST(SettingsPersonalization, ToggleRebuildsHeightWithoutProcessingHiddenControls)
{
	CSettingsPersonalizationState State;
	CPersonalizationCard Card(State, "card-a");
	const auto pClosed = Card.Resolve();
	std::shared_ptr<const CSettingsContentLayout> pOpened;
	EXPECT_TRUE(ExecuteSettingsContentInputThenResolve(pClosed, {},
		[&](const auto &Layout, auto) { Card.Replay(Layout, ESettingsContentPass::INPUT, true); },
		[&](float) { pOpened = Card.Resolve(); return pOpened; }));
	ASSERT_NE(pOpened, nullptr);
	EXPECT_TRUE(State.Expanded("card-a"));
	EXPECT_NE(pOpened->Find("detail"), nullptr);
	EXPECT_FLOAT_EQ(pOpened->Height(), 78);
	EXPECT_EQ(Card.m_DetailInputs, 0);
	Card.Replay(*pOpened, ESettingsContentPass::INPUT, false);
	EXPECT_EQ(Card.m_DetailInputs, 1);
	Card.Replay(*pOpened, ESettingsContentPass::INPUT, true);
	const auto pClosedAgain = Card.Resolve();
	EXPECT_FALSE(State.Expanded("card-a"));
	EXPECT_EQ(pClosedAgain->Find("detail"), nullptr);
	EXPECT_FLOAT_EQ(pClosedAgain->Height(), pClosed->Height());
	EXPECT_EQ(Card.m_DetailInputs, 1);
	Card.Replay(*pClosedAgain, ESettingsContentPass::INPUT, true);
	EXPECT_TRUE(State.Expanded("card-a"));
	EXPECT_FLOAT_EQ(Card.Resolve()->Height(), pOpened->Height());
}

TEST(SettingsPersonalization, SameCardSharesStateAcrossPagesAndOtherCardsStayCollapsed)
{
	CSettingsPersonalizationState State;
	CPersonalizationCard Page(State, "card-a"), Search(State, "card-a"), Other(State, "card-b");
	Page.Replay(*Page.Resolve(), ESettingsContentPass::INPUT, true);
	const auto pSearch = Search.Resolve();
	EXPECT_NE(pSearch->Find("detail"), nullptr);
	EXPECT_EQ(Other.Resolve()->Find("detail"), nullptr);
	Search.Replay(*pSearch, ESettingsContentPass::INPUT, true);
	EXPECT_EQ(Page.m_pButtonId, Search.m_pButtonId);
	EXPECT_EQ(Page.Resolve()->Find("detail"), nullptr);
	EXPECT_FALSE(State.Expanded("card-b"));
}

TEST(SettingsPersonalization, DrawAndReadOnlyInputCannotChangeExpansion)
{
	CSettingsPersonalizationState State;
	CPersonalizationCard Card(State, "card-a");
	const auto pClosed = Card.Resolve();
	Card.Replay(*pClosed, ESettingsContentPass::DRAW, true);
	EXPECT_FALSE(State.Expanded("card-a"));
	Card.Replay(*pClosed, ESettingsContentPass::INPUT, true, true);
	EXPECT_FALSE(State.Expanded("card-a"));
	Card.Replay(*pClosed, ESettingsContentPass::INPUT, true);
	const auto pOpened = Card.Resolve();
	Card.Replay(*pOpened, ESettingsContentPass::DRAW, true);
	Card.Replay(*pOpened, ESettingsContentPass::INPUT, true, true);
	EXPECT_TRUE(State.Expanded("card-a"));
	EXPECT_NE(Card.Resolve()->Find("detail"), nullptr);
}
