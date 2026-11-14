// 玩家信息卡几何契约（与好友列表卡片同一种读法）：
// 头部一行放头像 / 名字 / 战队 / 国家旗，右上角一排动作图标，下面只有皮肤行与状态行。
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
	EXPECT_EQ(QmPlayersUi::PlayerTileColumns(Tile * 4.0f + Gap * 3.0f), 4);
}

TEST(QmPlayerCardGeometry, HeaderKeepsEverySectionInsideTheCard)
{
	const CUIRect Card{0.0f, 0.0f, 320.0f, 200.0f};
	const auto Layout = QmPlayersUi::PlayerInfoCardLayout(Card, 4);
	EXPECT_TRUE(Inside(Layout.m_Header, Card));
	EXPECT_TRUE(Inside(Layout.m_Avatar, Card));
	EXPECT_TRUE(Inside(Layout.m_Name, Card));
	EXPECT_TRUE(Inside(Layout.m_Clan, Card));
	EXPECT_TRUE(Inside(Layout.m_Flag, Card));
	EXPECT_TRUE(Inside(Layout.m_Icons, Card));
	EXPECT_TRUE(Inside(Layout.m_SkinRow, Card));
	EXPECT_TRUE(Inside(Layout.m_Status, Card));
	// 头像必须是正方形，且高度等于头部高度（与好友列表卡片一致）。
	EXPECT_FLOAT_EQ(Layout.m_Avatar.w, Layout.m_Avatar.h);
	EXPECT_FLOAT_EQ(Layout.m_Avatar.h, Layout.m_Header.h);
}

TEST(QmPlayerCardGeometry, ActionIconsSitInTheTopRightCorner)
{
	const CUIRect Card{0.0f, 0.0f, 320.0f, 200.0f};
	const auto Layout = QmPlayersUi::PlayerInfoCardLayout(Card, 4);
	ASSERT_GT(Layout.m_Icons.w, 0.0f);
	// 图标条贴着卡片右上角：右边界等于头部右边界、上边界等于头部上边界。
	EXPECT_NEAR(Layout.m_Icons.x + Layout.m_Icons.w, Layout.m_Header.x + Layout.m_Header.w, 0.01f);
	EXPECT_NEAR(Layout.m_Icons.y, Layout.m_Header.y, 0.01f);
	// 图标条不压住名字列。
	EXPECT_FALSE(Overlaps(Layout.m_Icons, Layout.m_Name));
	EXPECT_FALSE(Overlaps(Layout.m_Icons, Layout.m_Avatar));
}

TEST(QmPlayerCardGeometry, SectionsStackWithoutOverlap)
{
	const CUIRect Card{0.0f, 0.0f, 320.0f, 200.0f};
	const auto Layout = QmPlayersUi::PlayerInfoCardLayout(Card, 4);
	EXPECT_LE(Layout.m_Header.y + Layout.m_Header.h, Layout.m_SkinRow.y + 0.01f);
	EXPECT_LE(Layout.m_SkinRow.y + Layout.m_SkinRow.h, Layout.m_Status.y + 0.01f);
	EXPECT_GT(Layout.m_Name.h, 0.0f);
	EXPECT_GT(Layout.m_Clan.h, 0.0f);
	EXPECT_GT(Layout.m_Status.h, 0.0f);
}

TEST(QmPlayerCardGeometry, IconCellsAreSquareAndRightAligned)
{
	const CUIRect Row{0.0f, 0.0f, 200.0f, 22.0f};
	const auto Slots = QmPlayersUi::PlayerIconRow(Row, 4);
	const CUIRect Last = Slots.m_aCells[3];
	const CUIRect First = Slots.m_aCells[0];
	EXPECT_FLOAT_EQ(Last.x + Last.w, Row.x + Row.w);
	for(int Index = 0; Index < 4; ++Index)
	{
		SCOPED_TRACE(Index);
		const CUIRect Cell = Slots.m_aCells[Index];
		EXPECT_GT(Cell.w, 0.0f);
		EXPECT_FLOAT_EQ(Cell.w, Cell.h);
		EXPECT_TRUE(Inside(Cell, Row));
	}
	// 从右往左排：第 0 格在最左。
	EXPECT_LT(First.x, Last.x);
}

TEST(QmPlayerCardGeometry, DegenerateSizesProduceEmptySectionsInsteadOfNegativeRects)
{
	const auto Empty = QmPlayersUi::PlayerInfoCardLayout(CUIRect{0.0f, 0.0f, 0.0f, 0.0f}, 4);
	EXPECT_LE(Empty.m_Header.w, 0.0f);
	EXPECT_LE(Empty.m_Icons.h, 0.0f);
	const CUIRect Narrow{0.0f, 0.0f, 40.0f, 30.0f};
	const auto Tight = QmPlayersUi::PlayerInfoCardLayout(Narrow, 4);
	EXPECT_GE(Tight.m_Header.w, 0.0f);
	EXPECT_GE(Tight.m_Header.h, 0.0f);
	EXPECT_LE(Tight.m_Header.y + Tight.m_Header.h, Narrow.y + Narrow.h + 0.01f);
}
