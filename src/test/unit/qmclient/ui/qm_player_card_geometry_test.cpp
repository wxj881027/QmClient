// 玩家行卡几何契约（与服务器浏览器「好友」页同一种读法）：
// 整行一张卡，左边头像、中间名字与战队两行、右侧一排动作图标；没有右侧展示页。
#include <game/client/QmUi/QmPlayerCard.h>
#include <game/client/QmUi/UiTokens.h>

#include <gtest/gtest.h>

namespace
{
	bool Inside(const CUIRect &Inner, const CUIRect &Outer)
	{
		return Inner.x >= Outer.x - 0.01f && Inner.y >= Outer.y - 0.01f &&
		       Inner.x + Inner.w <= Outer.x + Outer.w + 0.01f &&
		       Inner.y + Inner.h <= Outer.y + Outer.h + 0.01f;
	}
	bool Overlaps(const CUIRect &A, const CUIRect &B)
	{
		return A.x < B.x + B.w - 0.01f && B.x < A.x + A.w - 0.01f &&
		       A.y < B.y + B.h - 0.01f && B.y < A.y + A.h - 0.01f;
	}
} // namespace

TEST(QmPlayerCardGeometry, TileColumnsFollowAvailableWidth)
{
	EXPECT_EQ(QmPlayersUi::PlayerTileColumns(0.0f), 1);
	const float Tile = ui_token::player_card::TILE_MIN_WIDTH;
	const float Gap = ui_token::player_card::TILE_GAP;
	EXPECT_EQ(QmPlayersUi::PlayerTileColumns(Tile), 1);
	EXPECT_EQ(QmPlayersUi::PlayerTileColumns(Tile * 2.0f + Gap), 2);
}

TEST(QmPlayerCardGeometry, RowSectionsStayInsideTheCard)
{
	const CUIRect Row{0.0f, 0.0f, 640.0f, ui_token::player_card::LIST_ROW_HEIGHT};
	const auto Layout = QmPlayersUi::PlayerRowLayout(Row, 5);
	EXPECT_TRUE(Inside(Layout.m_Avatar, Row));
	EXPECT_TRUE(Inside(Layout.m_Name, Row));
	EXPECT_TRUE(Inside(Layout.m_Clan, Row));
	EXPECT_TRUE(Inside(Layout.m_Icons, Row));
	// 头像方形，且高度不小于名字与战队两行的合计（好友列表卡片就是这种比例）。
	EXPECT_FLOAT_EQ(Layout.m_Avatar.w, Layout.m_Avatar.h);
	EXPECT_GT(Layout.m_Avatar.h, 0.0f);
	EXPECT_GT(Layout.m_Name.h, 0.0f);
}

TEST(QmPlayerCardGeometry, ActionIconsSitAtTheRightEdgeOfTheRow)
{
	const CUIRect Row{0.0f, 0.0f, 640.0f, ui_token::player_card::LIST_ROW_HEIGHT};
	const auto Layout = QmPlayersUi::PlayerRowLayout(Row, 5);
	ASSERT_GT(Layout.m_Icons.w, 0.0f);
	const float CardRight = Row.x + Row.w - ui_token::player_card::PADDING;
	EXPECT_NEAR(Layout.m_Icons.x + Layout.m_Icons.w, CardRight, 0.01f);
	EXPECT_FALSE(Overlaps(Layout.m_Icons, Layout.m_Name));
	EXPECT_FALSE(Overlaps(Layout.m_Icons, Layout.m_Avatar));
}

TEST(QmPlayerCardGeometry, NameAndClanStackWithoutOverlap)
{
	const CUIRect Row{0.0f, 0.0f, 640.0f, ui_token::player_card::LIST_ROW_HEIGHT};
	const auto Layout = QmPlayersUi::PlayerRowLayout(Row, 5);
	EXPECT_LE(Layout.m_Name.y + Layout.m_Name.h, Layout.m_Clan.y + 0.01f);
	EXPECT_LT(Layout.m_Avatar.x + Layout.m_Avatar.w, Layout.m_Name.x + 0.01f);
}

TEST(QmPlayerCardGeometry, IconCellsAreSquareAndRightAligned)
{
	const CUIRect Row{0.0f, 0.0f, 200.0f, 22.0f};
	const auto Slots = QmPlayersUi::PlayerIconRow(Row, 5);
	const CUIRect Last = Slots.m_aCells[4];
	const CUIRect First = Slots.m_aCells[0];
	EXPECT_FLOAT_EQ(Last.x + Last.w, Row.x + Row.w);
	for(int Index = 0; Index < 5; ++Index)
	{
		SCOPED_TRACE(Index);
		const CUIRect Cell = Slots.m_aCells[Index];
		EXPECT_GT(Cell.w, 0.0f);
		EXPECT_FLOAT_EQ(Cell.w, Cell.h);
		EXPECT_TRUE(Inside(Cell, Row));
	}
	EXPECT_LT(First.x, Last.x);
}

TEST(QmPlayerCardGeometry, DegenerateSizesProduceEmptySectionsInsteadOfNegativeRects)
{
	const auto Empty = QmPlayersUi::PlayerRowLayout(CUIRect{0.0f, 0.0f, 0.0f, 0.0f}, 5);
	EXPECT_LE(Empty.m_Avatar.w, 0.0f);
	EXPECT_LE(Empty.m_Icons.h, 0.0f);
	const CUIRect Narrow{0.0f, 0.0f, 60.0f, 20.0f};
	const auto Tight = QmPlayersUi::PlayerRowLayout(Narrow, 5);
	EXPECT_GE(Tight.m_Card.w, 0.0f);
	EXPECT_LE(Tight.m_Icons.x + Tight.m_Icons.w, Narrow.x + Narrow.w + 0.01f);
}
