#include <game/client/QmUi/QmSearchHeader.h>

#include <gtest/gtest.h>

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
