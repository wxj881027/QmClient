// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/QmUi/QmPlayerCard.h>

#include <gtest/gtest.h>

#include <algorithm>

// 玩家页右卡的几何契约：展示区 / 立绘 / 名字 / 战队 / 图标行 / 皮肤行 / 状态行
// 七块互不重叠且都在卡片内；列表列数随宽度单调不增。这些是纯几何，跑真实 UI 反而看不清。
namespace
{
	bool Inside(const CUIRect &Outer, const CUIRect &Inner)
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
}

TEST(QmPlayerCardGeometry, TileColumnsShrinkAsTheListGetsNarrower)
{
	const float Wide = 1600.0f;
	const float Medium = 900.0f;
	const float Narrow = 220.0f;
	const int WideColumns = QmPlayersUi::PlayerTileColumns(Wide);
	const int MediumColumns = QmPlayersUi::PlayerTileColumns(Medium);
	const int NarrowColumns = QmPlayersUi::PlayerTileColumns(Narrow);
	// 宽屏一行多格（不是单列长条），窄屏至少留一格。
	EXPECT_GE(WideColumns, 4);
	EXPECT_GT(WideColumns, MediumColumns);
	EXPECT_GE(MediumColumns, NarrowColumns);
	EXPECT_EQ(NarrowColumns, 1);
	EXPECT_EQ(QmPlayersUi::PlayerTileColumns(0.0f), 1);
	EXPECT_EQ(QmPlayersUi::PlayerTileColumns(-100.0f), 1);
}

TEST(QmPlayerCardGeometry, ShowcaseSectionsStayInsideTheCardAndNeverOverlap)
{
	const CUIRect Card{100.0f, 50.0f, 320.0f, 460.0f};
	const auto Layout = QmPlayersUi::PlayerShowcaseLayout(Card, 4);
	EXPECT_TRUE(Inside(Card, Layout.m_Showcase));
	EXPECT_TRUE(Inside(Card, Layout.m_Figure));
	EXPECT_TRUE(Inside(Layout.m_Showcase, Layout.m_Figure));
	EXPECT_TRUE(Inside(Card, Layout.m_Name));
	EXPECT_TRUE(Inside(Card, Layout.m_Clan));
	EXPECT_TRUE(Inside(Card, Layout.m_Icons));
	EXPECT_TRUE(Inside(Card, Layout.m_SkinRow));
	EXPECT_TRUE(Inside(Card, Layout.m_Info));
	EXPECT_FALSE(Overlaps(Layout.m_Name, Layout.m_Clan));
	EXPECT_FALSE(Overlaps(Layout.m_Name, Layout.m_Icons));
	EXPECT_FALSE(Overlaps(Layout.m_Showcase, Layout.m_SkinRow));
	EXPECT_FALSE(Overlaps(Layout.m_SkinRow, Layout.m_Info));
}

TEST(QmPlayerCardGeometry, ShowcaseGrowsWithWidthButStaysAShowcase)
{
	const CUIRect Narrow{0.0f, 0.0f, 220.0f, 460.0f};
	const CUIRect Wide{0.0f, 0.0f, 420.0f, 460.0f};
	const auto NarrowLayout = QmPlayersUi::PlayerShowcaseLayout(Narrow, 4);
	const auto WideLayout = QmPlayersUi::PlayerShowcaseLayout(Wide, 4);
	EXPECT_GT(WideLayout.m_Showcase.h, NarrowLayout.m_Showcase.h);
	// 展示区再高也只是卡片的一部分，不能把下面的皮肤行与状态行挤没。
	EXPECT_LT(WideLayout.m_Showcase.h, Wide.h * 0.8f);
	EXPECT_GT(WideLayout.m_SkinRow.h, 0.0f);
	EXPECT_GT(WideLayout.m_Info.h, 0.0f);
}

TEST(QmPlayerCardGeometry, IconRowSlotsAreSquareAndInsideTheShowcase)
{
	const CUIRect Card{0.0f, 0.0f, 320.0f, 460.0f};
	const auto Layout = QmPlayersUi::PlayerShowcaseLayout(Card, 4);
	const auto Icons = QmPlayersUi::PlayerShowcaseIconRow(Layout.m_Icons, 4);
	for(int Index = 0; Index < 4; ++Index)
	{
		const CUIRect Slot = Icons.m_aCells[Index];
		EXPECT_TRUE(Inside(Layout.m_Showcase, Slot));
		EXPECT_FLOAT_EQ(Slot.w, Slot.h);
		if(Index > 0)
			EXPECT_FALSE(Overlaps(Slot, Icons.m_aCells[Index - 1]));
	}
	// 数量为 0 时不排任何槽。
	const auto Empty = QmPlayersUi::PlayerShowcaseIconRow(Layout.m_Icons, 0);
	for(const CUIRect &Slot : Empty.m_aCells)
		EXPECT_EQ(Slot.w, 0.0f);
}

// 展示分区必须是「非零高度且互不重叠」：这组断言专门锁住 HSplitBottom 参数顺序写反
// 导致的「图标行 0 高、名字 0 高、战队吃掉整块」这一类回归。
TEST(QmPlayerCardGeometry, ShowcaseSectionsHavePositiveHeightAndDoNotOverlap)
{
	const CUIRect Card{0.0f, 0.0f, 320.0f, 460.0f};
	const auto Layout = QmPlayersUi::PlayerShowcaseLayout(Card, 4);
	EXPECT_GT(Layout.m_Name.h, 0.0f);
	EXPECT_GT(Layout.m_Clan.h, 0.0f);
	EXPECT_GT(Layout.m_Icons.h, 0.0f);
	EXPECT_GE(Layout.m_Showcase.h, Layout.m_Name.h + Layout.m_Clan.h + Layout.m_Icons.h);
	// 名字在战队之上、图标行在名字之上，三者纵向不交叠。
	EXPECT_LE(Layout.m_Icons.y + Layout.m_Icons.h, Layout.m_Name.y + 0.01f);
	EXPECT_LE(Layout.m_Name.y + Layout.m_Name.h, Layout.m_Clan.y + 0.01f);
	// 立绘不得压到图标行。
	EXPECT_LE(Layout.m_Figure.y + Layout.m_Figure.h, Layout.m_Icons.y + 0.01f);
}

TEST(QmPlayerCardGeometry, ShowcaseIconSlotsAreSquareAndInsideTheIconRow)
{
	const CUIRect Card{0.0f, 0.0f, 320.0f, 460.0f};
	const auto Layout = QmPlayersUi::PlayerShowcaseLayout(Card, 4);
	const auto Slots = QmPlayersUi::PlayerShowcaseIconRow(Layout.m_Icons, 4);
	for(int i = 0; i < 4; ++i)
	{
		SCOPED_TRACE(i);
		const CUIRect &Cell = Slots.m_aCells[i];
		EXPECT_GT(Cell.w, 0.0f);
		EXPECT_FLOAT_EQ(Cell.w, Cell.h);
		EXPECT_GE(Cell.x, Layout.m_Icons.x - 0.01f);
		EXPECT_LE(Cell.x + Cell.w, Layout.m_Icons.x + Layout.m_Icons.w + 0.01f);
	}
}
