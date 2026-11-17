#include <game/client/QmUi/QmPieOptionPool.h>

#include <gtest/gtest.h>
#include <test/test.h>

namespace
{
	// 三个消费点共用同一份「池内胶囊必须落在容器里」的判断：
	// 池不会把胶囊画到容器外，也不会让胶囊互相重叠。
	void ExpectChipsInsideContainer(const SQmPieOptionPoolLayout &Pool)
	{
		EXPECT_GE(Pool.m_Container.w, 0.0f);
		for(int Index = 0; Index < Pool.m_Count; ++Index)
		{
			const CUIRect &Chip = Pool.m_aChips[Index].m_Rect;
			EXPECT_GE(Chip.x, Pool.m_Container.x - 0.01f);
			EXPECT_GE(Chip.y, Pool.m_Container.y - 0.01f);
			EXPECT_LE(Chip.x + Chip.w, Pool.m_Container.x + Pool.m_Container.w + 0.01f);
			EXPECT_LE(Chip.y + Chip.h, Pool.m_Container.y + Pool.m_Container.h + 0.01f);
		}
	}
}

TEST(QmPieOptionPool, ChipsWrapInsideContainerWithoutOverlap)
{
	const float aWidths[] = {90.0f, 120.0f, 70.0f, 140.0f, 80.0f};
	const CUIRect Area{10.0f, 20.0f, 260.0f, 0.0f};
	const SQmPieOptionPoolLayout Pool = QmResolvePieOptionPool(Area, 5, aWidths, 24.0f, 12.0f);
	ASSERT_EQ(Pool.m_Count, 5);
	ExpectChipsInsideContainer(Pool);
	// 260 宽放不下 90+120+70（含内边距与间距），必须换行。
	EXPECT_GT(Pool.m_aChips[0].m_Rect.y, 0.0f);
	EXPECT_LT(Pool.m_aChips[0].m_Rect.y, Pool.m_aChips[4].m_Rect.y + 1.0f);
	bool Wrapped = false;
	for(int Index = 1; Index < Pool.m_Count; ++Index)
		Wrapped = Wrapped || std::fabs(Pool.m_aChips[Index].m_Rect.y - Pool.m_aChips[0].m_Rect.y) > 0.5f;
	EXPECT_TRUE(Wrapped);
	// 同一行内相邻胶囊不重叠。
	for(int Index = 1; Index < Pool.m_Count; ++Index)
	{
		const CUIRect &Prev = Pool.m_aChips[Index - 1].m_Rect;
		const CUIRect &Cur = Pool.m_aChips[Index].m_Rect;
		if(std::fabs(Prev.y - Cur.y) < 0.5f)
			EXPECT_GE(Cur.x, Prev.x + Prev.w - 0.01f);
	}
	// 高度覆盖最下面一行：容器高度必须容得下所有胶囊。
	float LowestBottom = 0.0f;
	for(int Index = 0; Index < Pool.m_Count; ++Index)
		LowestBottom = std::max(LowestBottom, Pool.m_aChips[Index].m_Rect.y + Pool.m_aChips[Index].m_Rect.h);
	EXPECT_GE(Pool.m_Container.y + Pool.m_Height, LowestBottom - 0.01f);
}

TEST(QmPieOptionPool, EmptyPoolKeepsOneChipRowHeight)
{
	const CUIRect Area{0.0f, 0.0f, 300.0f, 0.0f};
	const float SingleWidth = 80.0f;
	const SQmPieOptionPoolLayout WithOne = QmResolvePieOptionPool(Area, 1, &SingleWidth, 24.0f, 12.0f);
	const SQmPieOptionPoolLayout Empty = QmResolvePieOptionPool(Area, 0, nullptr, 24.0f, 12.0f);
	EXPECT_EQ(Empty.m_Count, 0);
	// 空池与只装一个胶囊的池同高：最后一个选项被拖走时整卡高度不跳。
	EXPECT_FLOAT_EQ(Empty.m_Height, WithOne.m_Height);
	EXPECT_GT(Empty.m_Height, 0.0f);
}

TEST(QmPieOptionPool, OversizedChipClampsToUsableWidth)
{
	const CUIRect Area{0.0f, 0.0f, 200.0f, 0.0f};
	const float OversizedWidth = 600.0f;
	const SQmPieOptionPoolLayout Pool = QmResolvePieOptionPool(Area, 1, &OversizedWidth, 24.0f, 12.0f);
	ASSERT_EQ(Pool.m_Count, 1);
	ExpectChipsInsideContainer(Pool);
	EXPECT_LE(Pool.m_aChips[0].m_Rect.w, Area.w);
}

TEST(QmPieOptionPool, ChipWidthGrowsWithTextAndBodySize)
{
	const float Short = QmPieOptionChipWidth(20.0f, 12.0f);
	const float Long = QmPieOptionChipWidth(120.0f, 12.0f);
	const float Bigger = QmPieOptionChipWidth(120.0f, 16.0f);
	EXPECT_GT(Long, Short);
	EXPECT_GT(Bigger, Long);
	// 宽度必须容得下正文本身，否则名字只会剩省略号。
	EXPECT_GT(Short, 20.0f);
}

TEST(QmPieOptionPool, DragStateResetsToIdle)
{
	SQmPieOptionPoolDrag Drag;
	EXPECT_FALSE(Drag.Active());
	Drag.m_Pool = 1;
	Drag.m_Option = 3;
	Drag.m_Dragging = true;
	EXPECT_TRUE(Drag.Active());
	Drag.Reset();
	EXPECT_FALSE(Drag.Active());
	EXPECT_FALSE(Drag.m_Dragging);
	EXPECT_EQ(Drag.m_Option, -1);
}

// 池内排序的落点：指针落在某颗左半 → 插到它前面；全部超过 → 插到末尾。
// 被拖那颗不参与比较，指针刚离开它时不能把自己算成落点。
TEST(QmPieOptionPool, DropSlotFollowsChipCenters)
{
	CUIRect aRects[3] = {
		{0.0f, 0.0f, 20.0f, 16.0f},  // 中心 x = 10
		{24.0f, 0.0f, 20.0f, 16.0f}, // 中心 x = 34
		{48.0f, 0.0f, 20.0f, 16.0f}, // 中心 x = 58
	};
	EXPECT_EQ(QmResolveChipDropSlot(aRects, 3, -1, vec2(0.0f, 0.0f)), 0);
	EXPECT_EQ(QmResolveChipDropSlot(aRects, 3, -1, vec2(12.0f, 0.0f)), 1);
	EXPECT_EQ(QmResolveChipDropSlot(aRects, 3, -1, vec2(40.0f, 0.0f)), 2);
	EXPECT_EQ(QmResolveChipDropSlot(aRects, 3, -1, vec2(200.0f, 0.0f)), 3);

	// 拖第 0 颗：指针停在它自己原来的位置时落点仍是 0（插回原位），
	// 而不是因为「跳过自己」被算成 1。
	EXPECT_EQ(QmResolveChipDropSlot(aRects, 3, 0, vec2(5.0f, 0.0f)), 0);
	EXPECT_EQ(QmResolveChipDropSlot(aRects, 3, 0, vec2(36.0f, 0.0f)), 1);
	EXPECT_EQ(QmResolveChipDropSlot(aRects, 3, 0, vec2(60.0f, 0.0f)), 2);

	// 空池与单项池都有确定结果，不会返回 -1 让调用点越界。
	EXPECT_EQ(QmResolveChipDropSlot(aRects, 0, -1, vec2(0.0f, 0.0f)), 0);
	EXPECT_EQ(QmResolveChipDropSlot(aRects, 1, -1, vec2(0.0f, 0.0f)), 0);
}
