#include <game/client/QmUi/QmServerInfoGeometry.h>

#include <gtest/gtest.h>

using namespace QmServerInfoGeometry;

namespace
{
	void ExpectInside(CUIRect Outer, CUIRect Inner)
	{
		EXPECT_GE(Inner.w, 0.0f);
		EXPECT_GE(Inner.h, 0.0f);
		EXPECT_GE(Inner.x, Outer.x);
		EXPECT_GE(Inner.y, Outer.y);
		EXPECT_LE(Inner.x + Inner.w, Outer.x + Outer.w + 0.001f);
		EXPECT_LE(Inner.y + Inner.h, Outer.y + Outer.h + 0.001f);
	}
}

TEST(QmServerInfoLayout, FixedLayoutKeepsTwoRowsAndOriginalColumnProportions)
{
	const auto Result = Layout({10, 20, 1004, 504}, true);
	EXPECT_FLOAT_EQ(Result.m_Server.w, 500);
	EXPECT_FLOAT_EQ(Result.m_Server.h, 250);
	EXPECT_FLOAT_EQ(Result.m_Game.x, 514);
	EXPECT_FLOAT_EQ(Result.m_Game.w, 500);
	EXPECT_FLOAT_EQ(Result.m_Motd.y, 274);
	EXPECT_FLOAT_EQ(Result.m_Motd.w, 380);
	EXPECT_FLOAT_EQ(Result.m_Tags.x, 394);
	EXPECT_FLOAT_EQ(Result.m_Tags.w, 620);
	EXPECT_FLOAT_EQ(Result.m_Game.x - (Result.m_Server.x + Result.m_Server.w), 4);
	EXPECT_FLOAT_EQ(Result.m_Tags.x - (Result.m_Motd.x + Result.m_Motd.w), 4);
	EXPECT_FLOAT_EQ(Result.m_Motd.y - (Result.m_Server.y + Result.m_Server.h), 4);
}

TEST(QmServerInfoLayout, NarrowViewportKeepsCardsDisjointAndNonnegative)
{
	for(const float Width : {0.0f, 2.0f, 120.0f, 400.0f})
	{
		for(const float Height : {0.0f, 2.0f, 100.0f, 600.0f})
		{
			SCOPED_TRACE(Width);
			SCOPED_TRACE(Height);
			const CUIRect View{10, 20, Width, Height};
			const auto Result = Layout(View, true);
			for(const auto &Rect : {Result.m_Server, Result.m_Game, Result.m_Motd, Result.m_Tags})
			{
				ExpectInside(View, Rect);
				ExpectInside(Rect, ContentRect(Rect));
			}
			EXPECT_LE(Result.m_Server.x + Result.m_Server.w, Result.m_Game.x);
			EXPECT_LE(Result.m_Motd.x + Result.m_Motd.w, Result.m_Tags.x);
			EXPECT_LE(Result.m_Server.y + Result.m_Server.h, Result.m_Motd.y);
		}
	}
}

TEST(QmServerInfoLayout, HiddenTagsGiveMotdFullWidthWithoutMovingUpperCards)
{
	const CUIRect View{0, 0, 1004, 504};
	const auto Hidden = Layout(View, false);
	const auto Shown = Layout(View, true);
	EXPECT_FLOAT_EQ(Hidden.m_Motd.w, 1004);
	EXPECT_FLOAT_EQ(Hidden.m_Tags.w, 0);
	ExpectInside(View, Hidden.m_Tags);
	EXPECT_FLOAT_EQ(Hidden.m_Server.w, Shown.m_Server.w);
	EXPECT_FLOAT_EQ(Hidden.m_Game.x, Shown.m_Game.x);
	EXPECT_FLOAT_EQ(Hidden.m_Motd.y, Shown.m_Motd.y);
	EXPECT_FLOAT_EQ(Layout(View, true).m_Motd.w, 380);
}

TEST(QmServerInfoLayout, WindowResizeKeepsFixedProportions)
{
	const auto Small = Layout({0, 0, 1004, 604}, true);
	const auto Large = Layout({0, 0, 2004, 1204}, true);
	EXPECT_FLOAT_EQ(Large.m_Server.w, Small.m_Server.w * 2);
	EXPECT_FLOAT_EQ(Large.m_Server.h, Small.m_Server.h * 2);
	EXPECT_FLOAT_EQ(Large.m_Tags.w, Small.m_Tags.w * 2);
}

TEST(QmServerInfoLayout, ServerFieldsAndFooterFitWithoutExtendingCard)
{
	for(const bool HasPing : {false, true})
	{
		for(const bool HasCommunity : {false, true})
		{
			const float Height = ServerContentHeight(HasPing, HasCommunity);
			for(const CUIRect View : {CUIRect{0, 0, 480, 200}, CUIRect{0, 0, 200, 100}})
			{
				const float Scale = FitScale(View, 280, Height);
				EXPECT_GT(Scale, 0.0f);
				EXPECT_LT(Scale, 1.0f);
				ExpectInside(View, {View.x, View.y, 280 * Scale, Height * Scale});
			}
		}
	}
}

TEST(QmServerInfoLayout, GameWithAllOptionalFieldsKeepsRoomForNote)
{
	const CUIRect View{0, 0, 300, 150};
	const float Height = GameContentHeight(3, true, true);
	const float Scale = FitScale(View, 280, Height);
	EXPECT_LT(Scale, 1.0f);
	ExpectInside(View, {0, 0, 280 * Scale, Height * Scale});
	EXPECT_GT(GameContentHeight(3, true, true), GameContentHeight(3, true, false));
}

TEST(QmServerInfoLayout, ShortContentKeepsOriginalFontSize)
{
	EXPECT_FLOAT_EQ(FitScale({0, 0, 500, 300}, 280, ServerContentHeight(true, true)), 1);
	EXPECT_FLOAT_EQ(FitScale({0, 0, 300, 200}, 200, 32), 1);
}

TEST(QmServerInfoLayout, LongMotdScalesEntireParagraphToAvailableHeight)
{
	const CUIRect View{10, 20, 350, 200};
	const float Scale = FitScale(View, 350, 1600);
	EXPECT_FLOAT_EQ(Scale, 0.125f);
	ExpectInside(View, {View.x, View.y, 350 * Scale, 1600 * Scale});
}

TEST(QmServerInfoLayout, WideMotdFitsAfterWindowNarrows)
{
	const CUIRect View{10, 20, 200, 150};
	const float Scale = FitScale(View, 800, 100);
	EXPECT_FLOAT_EQ(Scale, 0.25f);
	ExpectInside(View, {View.x, View.y, 800 * Scale, 100 * Scale});
}

TEST(QmServerInfoLayout, EmptyViewportDoesNotProduceNegativeScale)
{
	EXPECT_FLOAT_EQ(FitScale({0, 0, 0, 100}, 280, 257), 0);
	EXPECT_FLOAT_EQ(FitScale({0, 0, 100, 0}, 280, 257), 0);
	EXPECT_FLOAT_EQ(FitScale({0, 0, -10, -10}, 280, 257), 0);
	EXPECT_FLOAT_EQ(FitScale({0, 0, 100, 100}, 0, 0), 1);
}

TEST(QmServerInfoLayout, TagGridShowsAllTagsInsideListAtDifferentSizes)
{
	for(const int Count : {1, 4, 17, 64})
	{
		for(const CUIRect View : {CUIRect{10, 20, 600, 200}, CUIRect{10, 20, 120, 30}})
		{
			SCOPED_TRACE(Count);
			SCOPED_TRACE(View.w);
			const auto Grid = TagGrid(View, Count, 210, 1);
			EXPECT_GT(Grid.m_Scale, 0.0f);
			EXPECT_LE(Grid.m_Scale, 1.0f);
			for(int Index = 0; Index < Count; ++Index)
			{
				const CUIRect Tag = TagRect(View, Grid, Index);
				ExpectInside(View, Tag);
				if(Index > 0)
				{
					const CUIRect Previous = TagRect(View, Grid, Index - 1);
					EXPECT_TRUE(Tag.x >= Previous.x + Previous.w || Tag.y >= Previous.y + Previous.h);
				}
			}
		}
	}
}

TEST(QmServerInfoLayout, TagGridUsesOriginalSizeWhenAllLabelsFit)
{
	const auto Grid = TagGrid({0, 0, 600, 200}, 4, 100, 1);
	EXPECT_FLOAT_EQ(Grid.m_Scale, 1);
}

TEST(QmServerInfoLayout, TagGridRespectsPanelScaleAndEmptyList)
{
	EXPECT_FLOAT_EQ(TagGrid({0, 0, 600, 200}, 4, 100, 0.5f).m_Scale, 0.5f);
	EXPECT_FLOAT_EQ(TagGrid({0, 0, 600, 200}, 0, 100, 1).m_Scale, 0);
	EXPECT_FLOAT_EQ(TagGrid({0, 0, 0, 0}, 64, 100, 1).m_Scale, 0);
}
