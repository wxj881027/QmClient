#include <game/client/QmUi/QmSearchHeader.h>

#include <gtest/gtest.h>

#include <set>
#include <string_view>

TEST(QmSearchHeader, RepeatedFramesKeepTheSameSplash)
{
	qm_search_header::CSession Session;
	Session.Enter(1, 3, 0);
	const char *pFirst = Session.Text();
	Session.Enter(1, 8, 0);
	EXPECT_STREQ(Session.Text(), pFirst);
	EXPECT_FALSE(Session.NeedsEntry(1));
}

TEST(QmSearchHeader, NewDisplayCycleExcludesPreviousSplash)
{
	for(int Choice = 0; Choice < qm_search_header::NUM_SPLASHES; ++Choice)
	{
		qm_search_header::CSession Session;
		Session.Enter(1, Choice, 0);
		const char *pFirst = Session.Text();
		Session.Enter(2, Choice, 0);
		EXPECT_STRNE(Session.Text(), pFirst) << Choice;
	}
}

TEST(QmSearchHeader, ClosingAndReopeningChangesSplashEvenInSameCycle)
{
	qm_search_header::CSession Session;
	Session.Enter(1, 0, 0);
	const char *pFirst = Session.Text();
	Session.Leave();
	EXPECT_TRUE(Session.NeedsEntry(1));
	Session.Enter(1, 0, 0);
	EXPECT_STRNE(Session.Text(), pFirst);
}

TEST(QmSearchHeader, EveryPlaceholderCanBeSelectedOnFirstEntry)
{
	for(int Choice = 0; Choice < qm_search_header::NUM_SEARCH_PLACEHOLDERS; ++Choice)
	{
		SCOPED_TRACE(Choice);
		qm_search_header::CSession Session;
		EXPECT_EQ(Session.PlaceholderChoiceCount(), qm_search_header::NUM_SEARCH_PLACEHOLDERS);
		Session.Enter(1, 0, 0, Choice);
		EXPECT_STREQ(Session.Placeholder(), qm_search_header::SEARCH_PLACEHOLDERS[Choice]);
		EXPECT_STRNE(Session.Placeholder(), "");
	}
}

TEST(QmSearchHeader, EnterAndSearchButtonSubmitOnlyTheVisiblePlaceholder)
{
	qm_search_header::CSession Session;
	Session.Enter(1, 0, 0, 3);
	const char *apEmpty[] = {nullptr, ""};
	for(const char *pBlank : apEmpty)
	{
		EXPECT_EQ(Session.PlaceholderForSubmit(pBlank, false, false), nullptr);
		EXPECT_STREQ(Session.PlaceholderForSubmit(pBlank, true, false), Session.Placeholder());
		EXPECT_STREQ(Session.PlaceholderForSubmit(pBlank, false, true), Session.Placeholder());
	}
	for(const char *pQuery : {" ", "　", " \t　", "地图", "　我的搜索  ", Session.Placeholder()})
	{
		SCOPED_TRACE(pQuery);
		EXPECT_EQ(Session.PlaceholderForSubmit(pQuery, true, false), nullptr);
		EXPECT_EQ(Session.PlaceholderForSubmit(pQuery, false, true), nullptr);
	}
}

TEST(QmSearchHeader, ClearingAndResubmittingUsesTheCurrentlyDisplayedPlaceholder)
{
	qm_search_header::CSession Session;
	Session.Enter(1, 0, 0, 5);
	const char *pFirstQuery = Session.PlaceholderForSubmit("", false, true);
	ASSERT_STREQ(pFirstQuery, Session.Placeholder());
	Session.Enter(2, 0, 0, 5);
	EXPECT_EQ(Session.PlaceholderForSubmit(pFirstQuery, true, false), nullptr);
	const char *pNextQuery = Session.PlaceholderForSubmit("", true, false);
	EXPECT_STREQ(pNextQuery, Session.Placeholder());
	EXPECT_STRNE(pNextQuery, pFirstQuery);
}

TEST(QmSearchHeader, RepeatedFramesKeepTheSamePlaceholder)
{
	qm_search_header::CSession Session;
	Session.Enter(1, 0, 0, 2);
	const char *pFirst = Session.Placeholder();
	Session.Enter(1, 0, 0, 5);
	EXPECT_STREQ(Session.Placeholder(), pFirst);
}

TEST(QmSearchHeader, NewCycleCanSelectEveryPlaceholderExceptThePreviousOne)
{
	for(int Previous = 0; Previous < qm_search_header::NUM_SEARCH_PLACEHOLDERS; ++Previous)
	{
		SCOPED_TRACE(Previous);
		qm_search_header::CSession FirstEntry;
		FirstEntry.Enter(1, 0, 0, Previous);
		const int ChoiceCount = FirstEntry.PlaceholderChoiceCount();
		ASSERT_EQ(ChoiceCount, qm_search_header::NUM_SEARCH_PLACEHOLDERS - 1);
		std::set<std::string_view> Selected;
		for(int Choice = 0; Choice < ChoiceCount; ++Choice)
		{
			SCOPED_TRACE(Choice);
			auto Session = FirstEntry;
			Session.Enter(2, 0, 0, Choice);
			EXPECT_STRNE(Session.Placeholder(), FirstEntry.Placeholder());
			EXPECT_TRUE(Selected.insert(Session.Placeholder()).second);
		}
	}
}

TEST(QmSearchHeader, ClosingAndReopeningChangesPlaceholderEvenInSameCycle)
{
	qm_search_header::CSession Session;
	Session.Enter(1, 0, 0, 0);
	const char *pFirst = Session.Placeholder();
	Session.Leave();
	Session.Enter(1, 0, 0, 0);
	EXPECT_STRNE(Session.Placeholder(), pFirst);
}

TEST(QmSearchHeader, QueryRemovesHeaderAndPlacesInputAtTop)
{
	const auto Layout = qm_search_header::Layout({20.0f, 30.0f, 600.0f, 400.0f}, 100.0f, 1.0f, true, 0);
	EXPECT_FLOAT_EQ(Layout.m_Logo.h, 0.0f);
	EXPECT_FLOAT_EQ(Layout.m_Subtitle.h, 0.0f);
	EXPECT_FLOAT_EQ(Layout.m_InputOffsetY, 0.0f);
}

TEST(QmSearchHeader, EmptySearchAnchorsLogoAtQuarterAndInputCardAtHalf)
{
	const CUIRect Viewport{20.0f, 30.0f, 900.0f, 600.0f};
	for(int Image = 0; Image < qm_search_header::NUM_LOGOS; ++Image)
	{
		SCOPED_TRACE(Image);
		const auto Layout = qm_search_header::Layout(Viewport, 100.0f, 1.0f, false, Image);
		ASSERT_GT(Layout.m_Logo.h, 0.0f);
		EXPECT_FLOAT_EQ(Layout.m_Logo.x + Layout.m_Logo.w * 0.5f, Viewport.x + Viewport.w * 0.5f);
		EXPECT_NEAR(Layout.m_Logo.y + Layout.m_Logo.h * 0.5f, Viewport.y + Viewport.h * 0.25f, 0.001f);
		EXPECT_FLOAT_EQ(Layout.m_InputOffsetY + 50.0f, Viewport.h * 0.5f);
		EXPECT_LT(Layout.m_Logo.y + Layout.m_Logo.h, Layout.m_Subtitle.y);
		EXPECT_LT(Layout.m_Subtitle.y + Layout.m_Subtitle.h, Viewport.y + Layout.m_InputOffsetY);
	}
}

TEST(QmSearchHeader, ChangingInputCardHeightKeepsItsCenterAtHalf)
{
	const CUIRect Viewport{20.0f, 30.0f, 900.0f, 600.0f};
	const float aInputHeights[] = {40.0f, 100.0f, 140.0f};
	for(const float InputHeight : aInputHeights)
	{
		SCOPED_TRACE(InputHeight);
		const auto Layout = qm_search_header::Layout(Viewport, InputHeight, 1.0f, false, 0);
		ASSERT_GT(Layout.m_Logo.h, 0.0f);
		EXPECT_NEAR(Layout.m_Logo.y + Layout.m_Logo.h * 0.5f, Viewport.y + Viewport.h * 0.25f, 0.001f);
		EXPECT_FLOAT_EQ(Layout.m_InputOffsetY + InputHeight * 0.5f, Viewport.h * 0.5f);
		EXPECT_LT(Layout.m_Subtitle.y + Layout.m_Subtitle.h, Viewport.y + Layout.m_InputOffsetY);
	}
}

TEST(QmSearchHeader, LargeScaleKeepsQuarterAndHalfAnchorsWhenSpaceAllows)
{
	const CUIRect Viewport{20.0f, 30.0f, 1400.0f, 1200.0f};
	const auto Layout = qm_search_header::Layout(Viewport, 240.0f, 2.0f, false, 1);
	ASSERT_GT(Layout.m_Logo.h, 0.0f);
	EXPECT_NEAR(Layout.m_Logo.y + Layout.m_Logo.h * 0.5f, Viewport.y + Viewport.h * 0.25f, 0.001f);
	EXPECT_FLOAT_EQ(Layout.m_InputOffsetY + 120.0f, Viewport.h * 0.5f);
	EXPECT_LT(Layout.m_Subtitle.y + Layout.m_Subtitle.h, Viewport.y + Layout.m_InputOffsetY);
}

TEST(QmSearchHeader, SmallViewportAndLargeScaleKeepHeaderInsideViewport)
{
	const CUIRect Viewport{20.0f, 30.0f, 180.0f, 260.0f};
	const auto Layout = qm_search_header::Layout(Viewport, 120.0f, 2.0f, false, 0);
	EXPECT_GT(Layout.m_Logo.h, 0.0f);
	EXPECT_GE(Layout.m_Logo.x, Viewport.x);
	EXPECT_GE(Layout.m_Logo.y, Viewport.y);
	EXPECT_LE(Layout.m_Logo.x + Layout.m_Logo.w, Viewport.x + Viewport.w);
	EXPECT_LT(Layout.m_Logo.y + Layout.m_Logo.h, Layout.m_Subtitle.y);
	EXPECT_LT(Layout.m_Subtitle.y + Layout.m_Subtitle.h, Viewport.y + Layout.m_InputOffsetY);
	EXPECT_LE(Layout.m_InputOffsetY + 120.0f, Viewport.h);
}

TEST(QmSearchHeader, VeryShortViewportReservesSpaceForInput)
{
	const auto Layout = qm_search_header::Layout({0.0f, 0.0f, 200.0f, 130.0f}, 100.0f, 1.0f, false, 0);
	EXPECT_FLOAT_EQ(Layout.m_Logo.h, 0.0f);
	EXPECT_FLOAT_EQ(Layout.m_InputOffsetY, 15.0f);
}

TEST(QmSearchHeader, EveryImageCanBeSelectedIndependentlyOfSplash)
{
	for(int Image = 0; Image < qm_search_header::NUM_LOGOS; ++Image)
	{
		qm_search_header::CSession Session;
		Session.Enter(1, 0, Image);
		EXPECT_EQ(Session.LogoIndex(), Image);
		EXPECT_STREQ(Session.Text(), qm_search_header::SPLASHES[0]);
	}
}

TEST(QmSearchHeader, RepeatedFramesKeepTheSameImage)
{
	qm_search_header::CSession Session;
	Session.Enter(1, 0, 2);
	Session.Enter(1, 4, 0);
	EXPECT_EQ(Session.LogoIndex(), 2);
}

TEST(QmSearchHeader, ReenteringSelectsAnotherImage)
{
	qm_search_header::CSession Session;
	Session.Enter(1, 0, 0);
	Session.Enter(2, 0, 1);
	EXPECT_EQ(Session.LogoIndex(), 1);
	Session.Leave();
	Session.Enter(2, 0, 2);
	EXPECT_EQ(Session.LogoIndex(), 2);
}

TEST(QmSearchHeader, EachImageKeepsItsVisibleArtworkAspectRatio)
{
	const float aExpectedAspects[] = {1505.0f / 497.0f, 1505.0f / 634.0f, 1285.0f / 431.0f};
	for(int Image = 0; Image < qm_search_header::NUM_LOGOS; ++Image)
	{
		const auto Layout = qm_search_header::Layout({0.0f, 0.0f, 600.0f, 400.0f}, 100.0f, 1.0f, false, Image);
		ASSERT_GT(Layout.m_Logo.h, 0.0f);
		EXPECT_EQ(Layout.m_LogoIndex, Image);
		EXPECT_NEAR(Layout.m_Logo.w / Layout.m_Logo.h, aExpectedAspects[Image], 0.001f) << Image;
	}
}

TEST(QmSearchHeader, SubtitleUsesMiddleSeventyPercentOfViewport)
{
	for(const float Width : {180.0f, 900.0f, 1400.0f})
	{
		SCOPED_TRACE(Width);
		const CUIRect Viewport{20.0f, 30.0f, Width, 600.0f};
		const auto Layout = qm_search_header::Layout(Viewport, 100.0f, 1.0f, false, 0);
		ASSERT_GT(Layout.m_Subtitle.h, 0.0f);
		EXPECT_FLOAT_EQ(Layout.m_Subtitle.w, Width * 0.7f);
		EXPECT_NEAR(Layout.m_Subtitle.x - Viewport.x, Width * 0.15f, 0.001f);
		EXPECT_NEAR(Layout.m_Subtitle.x + Layout.m_Subtitle.w * 0.5f, Viewport.x + Width * 0.5f, 0.001f);
	}
}

TEST(QmSearchHeader, MultilineSubtitleReservesHeightBeforeInput)
{
	for(const float Scale : {1.0f, 2.0f})
	{
		SCOPED_TRACE(Scale);
		const CUIRect Viewport{20.0f, 30.0f, 900.0f, 600.0f * Scale};
		const float TextHeight = 80.0f * Scale;
		const float InputHeight = 100.0f * Scale;
		const auto Layout = qm_search_header::Layout(Viewport, InputHeight, Scale, false, 0, TextHeight);
		ASSERT_GT(Layout.m_Logo.h, 0.0f);
		EXPECT_FLOAT_EQ(Layout.m_Subtitle.h, TextHeight);
		EXPECT_GT(Layout.m_Subtitle.y, Layout.m_Logo.y + Layout.m_Logo.h);
		EXPECT_LT(Layout.m_Subtitle.y + Layout.m_Subtitle.h, Viewport.y + Layout.m_InputOffsetY);
		EXPECT_LE(Layout.m_InputOffsetY + InputHeight, Viewport.h);
	}
}

TEST(QmSearchHeader, WrappingSubtitleKeepsLogoSizeAndMovesInputBelowText)
{
	const CUIRect Viewport{20.0f, 30.0f, 900.0f, 600.0f};
	const float InputHeight = 100.0f;
	for(int Image = 0; Image < qm_search_header::NUM_LOGOS; ++Image)
	{
		SCOPED_TRACE(Image);
		const auto SingleLine = qm_search_header::Layout(Viewport, InputHeight, 1.0f, false, Image);
		for(const float TextHeight : {50.0f, 80.0f, 300.0f})
		{
			SCOPED_TRACE(TextHeight);
			const auto Wrapped = qm_search_header::Layout(Viewport, InputHeight, 1.0f, false, Image, TextHeight);
			EXPECT_FLOAT_EQ(Wrapped.m_Logo.w, SingleLine.m_Logo.w);
			EXPECT_FLOAT_EQ(Wrapped.m_Logo.h, SingleLine.m_Logo.h);
			EXPECT_GE(Wrapped.m_Logo.y, Viewport.y);
			EXPECT_LT(Wrapped.m_Logo.y + Wrapped.m_Logo.h, Wrapped.m_Subtitle.y);
			EXPECT_FLOAT_EQ(Wrapped.m_Subtitle.h, TextHeight);
			EXPECT_LT(Wrapped.m_Subtitle.y + TextHeight, Viewport.y + Wrapped.m_InputOffsetY);
			EXPECT_GT(Wrapped.m_InputOffsetY, SingleLine.m_InputOffsetY);
			EXPECT_LE(Wrapped.m_InputOffsetY + InputHeight, Viewport.h);
		}
	}
}
