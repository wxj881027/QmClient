#include <game/client/QmUi/QmServerInfoGeometry.h>

#include <gtest/gtest.h>

using namespace QmServerInfoGeometry;

TEST(QmServerInfoLayout, DefaultsKeepTwoRowsAndSeparateColumnSplits)
{
	const auto Result = Layout({10, 20, 1004, 504}, 500, 500, 380, true);
	EXPECT_FLOAT_EQ(Result.m_Server.w, 500);
	EXPECT_FLOAT_EQ(Result.m_Server.h, 250);
	EXPECT_FLOAT_EQ(Result.m_Game.x, 514);
	EXPECT_FLOAT_EQ(Result.m_Game.w, 500);
	EXPECT_FLOAT_EQ(Result.m_Motd.y, 274);
	EXPECT_FLOAT_EQ(Result.m_Motd.w, 380);
	EXPECT_FLOAT_EQ(Result.m_Tags.x, 394);
	EXPECT_FLOAT_EQ(Result.m_Tags.w, 620);
	EXPECT_FLOAT_EQ(Result.m_Server.x, 10);
	EXPECT_FLOAT_EQ(Result.m_Server.y, 20);
	EXPECT_FLOAT_EQ(Result.m_Game.x - (Result.m_Server.x + Result.m_Server.w), 4);
	EXPECT_FLOAT_EQ(Result.m_Tags.x - (Result.m_Motd.x + Result.m_Motd.w), 4);
	EXPECT_FLOAT_EQ(Result.m_Motd.y - (Result.m_Server.y + Result.m_Server.h), 4);
}

TEST(QmServerInfoLayout, RowDividerResizesBothRowsWithoutChangingColumns)
{
	const auto Result = Layout({0, 0, 1004, 504}, 600, 500, 380, true);
	EXPECT_FLOAT_EQ(Result.m_Server.h, 300);
	EXPECT_FLOAT_EQ(Result.m_Game.h, 300);
	EXPECT_FLOAT_EQ(Result.m_Motd.h, 200);
	EXPECT_FLOAT_EQ(Result.m_Tags.h, 200);
	EXPECT_FLOAT_EQ(Result.m_Server.w, 500);
	EXPECT_FLOAT_EQ(Result.m_Motd.w, 380);
	EXPECT_FLOAT_EQ(Result.m_Motd.y, Result.m_Server.h + GAP);
}

TEST(QmServerInfoLayout, TopDividerLeavesBottomRowUnchanged)
{
	const auto Result = Layout({0, 0, 1004, 504}, 500, 650, 380, true);
	EXPECT_FLOAT_EQ(Result.m_Server.w, 650);
	EXPECT_FLOAT_EQ(Result.m_Game.w, 350);
	EXPECT_FLOAT_EQ(Result.m_Motd.w, 380);
	EXPECT_FLOAT_EQ(Result.m_Tags.w, 620);
}

TEST(QmServerInfoLayout, BottomDividerLeavesTopRowUnchanged)
{
	const auto Result = Layout({0, 0, 1004, 504}, 500, 500, 600, true);
	EXPECT_FLOAT_EQ(Result.m_Server.w, 500);
	EXPECT_FLOAT_EQ(Result.m_Game.w, 500);
	EXPECT_FLOAT_EQ(Result.m_Motd.w, 600);
	EXPECT_FLOAT_EQ(Result.m_Tags.w, 400);
}

TEST(QmServerInfoLayout, DragPreservesGrabOffsetAndFollowsPointer)
{
	EXPECT_EQ(DragRatio(514, 10, 4, 1000, 280, 280), 500);
	EXPECT_EQ(DragRatio(614, 10, 4, 1000, 280, 280), 600);
	EXPECT_EQ(DragRatio(514, 10, 4, 1000, 280, 280), 500);
}

TEST(QmServerInfoLayout, DragBeyondPageKeepsBothCardsUsable)
{
	EXPECT_EQ(DragRatio(-1000, 0, 0, 1000, 280, 280), 280);
	EXPECT_EQ(DragRatio(3000, 0, 0, 1000, 280, 280), 720);
	EXPECT_EQ(DragRatio(3000, 0, 0, 500, 110, 160), 680);
}

TEST(QmServerInfoLayout, NarrowViewportKeepsCardsDisjointAndNonnegative)
{
	for(const float Width : {0.0f, 2.0f, 120.0f, 400.0f})
	{
		SCOPED_TRACE(Width);
		const auto Result = Layout({10, 20, Width, 100}, 999, 999, 1, true);
		for(const auto &Rect : {Result.m_Server, Result.m_Game, Result.m_Motd, Result.m_Tags})
		{
			EXPECT_GE(Rect.w, 0);
			EXPECT_GE(Rect.h, 0);
			EXPECT_LE(Rect.x + Rect.w, 10 + Width + 0.001f);
			EXPECT_LE(Rect.y + Rect.h, 120.001f);
		}
		EXPECT_LE(Result.m_Server.x + Result.m_Server.w, Result.m_Game.x);
		EXPECT_LE(Result.m_Motd.x + Result.m_Motd.w, Result.m_Tags.x);
		EXPECT_LE(Result.m_Server.y + Result.m_Server.h, Result.m_Motd.y);
	}
}

TEST(QmServerInfoLayout, HiddenTagsGiveMotdFullWidthAndReopenWithSavedSplit)
{
	const auto Hidden = Layout({0, 0, 1004, 504}, 500, 500, 650, false);
	EXPECT_FLOAT_EQ(Hidden.m_Motd.w, 1004);
	EXPECT_FLOAT_EQ(Hidden.m_Tags.w, 0);
	EXPECT_FLOAT_EQ(Hidden.m_aDividers[2].w, 0);
	const auto Reopened = Layout({0, 0, 1004, 504}, 500, 500, 650, true);
	EXPECT_FLOAT_EQ(Reopened.m_Motd.w, 650);
	EXPECT_FLOAT_EQ(Reopened.m_Tags.w, 350);
}

TEST(QmServerInfoLayout, SavedRatiosFollowViewportSize)
{
	const auto Small = Layout({0, 0, 1004, 604}, 600, 600, 400, true);
	const auto Large = Layout({0, 0, 2004, 1204}, 600, 600, 400, true);
	EXPECT_FLOAT_EQ(Large.m_Server.w, Small.m_Server.w * 2);
	EXPECT_FLOAT_EQ(Large.m_Server.h, Small.m_Server.h * 2);
	EXPECT_FLOAT_EQ(Large.m_Tags.w, Small.m_Tags.w * 2);
}

TEST(QmServerInfoLayout, InvalidConfigValuesStillRespectMinimumSizes)
{
	const auto Result = Layout({0, 0, 1004, 504}, -100, 2000, -100, true);
	EXPECT_FLOAT_EQ(Result.m_Server.h, 110);
	EXPECT_FLOAT_EQ(Result.m_Game.w, 280);
	EXPECT_FLOAT_EQ(Result.m_Motd.w, 180);
}

TEST(QmServerInfoLayout, ServerScrollExtentIncludesOptionalRowsAndFooter)
{
	EXPECT_FLOAT_EQ(ServerContentHeight(false, false), 217);
	EXPECT_FLOAT_EQ(ServerContentHeight(true, false), 237);
	EXPECT_FLOAT_EQ(ServerContentHeight(true, true), 257);
}

TEST(QmServerInfoLayout, GameScrollExtentKeepsNoteAfterAllOptionalFields)
{
	EXPECT_FLOAT_EQ(GameContentHeight(0, false, false), 97);
	EXPECT_FLOAT_EQ(GameContentHeight(0, true, true), 151);
	EXPECT_FLOAT_EQ(GameContentHeight(3, true, true), 211);
}
