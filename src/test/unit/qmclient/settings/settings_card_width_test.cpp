#include <game/client/QmUi/SettingsCardWidth.h>

#include <gtest/gtest.h>

TEST(SettingsCardWidth, MiddleCardStaysAtItsSlotWhenExpandingAndRestoring)
{
	CSettingsCardWidthLayout Layout;
	for(int Column : {1, 2})
	{
		SCOPED_TRACE(Column);
		for(bool FullWidth : {false, true, false, true})
		{
			SCOPED_TRACE(FullWidth);
			std::vector<SSettingsCardWidthPlacement> vCards{
				{Column, false, {0, 0, 100, 80}},
				{Column, FullWidth, {0, 100, FullWidth ? 220.0f : 100.0f, 80}},
				{Column, false, {0, 200, 100, 80}},
				{3 - Column, false, {120, 0, 100, 80}},
				{3 - Column, false, {120, 100, 100, 80}},
			};
			Layout.Arrange(vCards, 20);
			EXPECT_FLOAT_EQ(vCards[1].m_Rect.y, 100);
			EXPECT_FLOAT_EQ(vCards[0].m_Rect.y, 0);
			EXPECT_FLOAT_EQ(vCards[2].m_Rect.y, 200);
			EXPECT_FLOAT_EQ(vCards[3].m_Rect.y, 0);
			EXPECT_FLOAT_EQ(vCards[4].m_Rect.y, FullWidth ? 200 : 100);
		}
	}
}

TEST(SettingsCardWidth, TallOppositeCardMovesBelowExpandedCardWithoutMovingItsHeader)
{
	CSettingsCardWidthLayout Layout;
	std::vector<SSettingsCardWidthPlacement> vCards{
		{1, false, {0, 0, 100, 80}},
		{1, true, {0, 100, 220, 160}},
		{1, false, {0, 280, 100, 80}},
		{2, false, {120, 0, 100, 240}},
		{2, false, {120, 260, 100, 80}},
	};
	Layout.Arrange(vCards, 20);
	EXPECT_FLOAT_EQ(vCards[1].m_Rect.y, 100);
	EXPECT_FLOAT_EQ(vCards[3].m_Rect.y, 280);
	EXPECT_FLOAT_EQ(vCards[4].m_Rect.y, 540);
	EXPECT_FLOAT_EQ(vCards[2].m_Rect.y, 280);
}

TEST(SettingsCardWidth, MultipleExpandedCardsKeepTheirPredecessorsAndDoNotOverlap)
{
	CSettingsCardWidthLayout Layout;
	std::vector<SSettingsCardWidthPlacement> vCards{
		{1, false, {0, 0, 100, 80}},
		{1, true, {0, 100, 220, 160}},
		{1, false, {0, 280, 100, 80}},
		{2, false, {120, 0, 100, 80}},
		{2, false, {120, 100, 100, 80}},
		{2, true, {0, 200, 220, 100}},
	};
	Layout.Arrange(vCards, 20);
	EXPECT_FLOAT_EQ(vCards[1].m_Rect.y, 100);
	EXPECT_FLOAT_EQ(vCards[4].m_Rect.y, 280);
	EXPECT_FLOAT_EQ(vCards[5].m_Rect.y, 380);
	EXPECT_FLOAT_EQ(vCards[2].m_Rect.y, 500);
}

TEST(SettingsCardWidth, NativeFullWidthSlotDoesNotChangeWhenNarrowed)
{
	CSettingsCardWidthLayout Layout;
	for(bool FullWidth : {true, false, true})
	{
		std::vector<SSettingsCardWidthPlacement> vCards{
			{1, false, {0, 0, 100, 80}},
			{2, false, {120, 0, 100, 80}},
			{0, FullWidth, {0, 100, FullWidth ? 220.0f : 100.0f, 80}},
			{1, true, {0, 200, 220, 80}},
		};
		Layout.Arrange(vCards, 20);
		EXPECT_FLOAT_EQ(vCards[2].m_Rect.y, 100);
		EXPECT_FLOAT_EQ(vCards[3].m_Rect.y, 200);
	}
}

TEST(SettingsCardWidth, ScrolledCoordinatesKeepTheSameHeaderPosition)
{
	CSettingsCardWidthLayout Layout;
	std::vector<SSettingsCardWidthPlacement> vCards{
		{1, false, {0, -140, 100, 80}},
		{1, true, {0, -40, 220, 80}},
		{2, false, {120, -140, 100, 80}},
		{2, false, {120, -40, 100, 80}},
	};
	Layout.Arrange(vCards, 20);
	EXPECT_FLOAT_EQ(vCards[1].m_Rect.y, -40);
	EXPECT_FLOAT_EQ(vCards[3].m_Rect.y, 60);
}

TEST(SettingsCardWidth, EmptyLayoutAndLayoutWithoutExpandedCardsKeepTheirGeometry)
{
	CSettingsCardWidthLayout Layout;
	std::vector<SSettingsCardWidthPlacement> vCards;
	Layout.Arrange(vCards, 20);
	EXPECT_TRUE(vCards.empty());
	vCards = {{1, false, {0, 100, 100, 80}}, {2, false, {120, 50, 100, 240}}};
	Layout.Arrange(vCards, 20);
	EXPECT_FLOAT_EQ(vCards[0].m_Rect.y, 100);
	EXPECT_FLOAT_EQ(vCards[1].m_Rect.y, 50);
}

TEST(SettingsCardWidth, ConsecutiveOppositeExpansionsKeepThePresentedOrder)
{
	CSettingsCardWidthLayout Layout;
	std::vector<SSettingsCardWidthPlacement> vCards{
		{1, false, {0, 0, 100, 80}},
		{2, true, {0, 0, 220, 120}},
	};
	Layout.Arrange(vCards, 20);
	ASSERT_FLOAT_EQ(vCards[0].m_Rect.y, 140);
	ASSERT_FLOAT_EQ(vCards[1].m_Rect.y, 0);

	for(auto &Card : vCards)
	{
		Card.m_ReferenceRect = Card.m_Rect;
		Card.m_HasReference = true;
	}
	// 左卡的输入坐标沿用点击前的顶部，右卡仍从其 canonical 槽位布局。
	vCards[0].m_FullWidth = true;
	vCards[0].m_Rect.w = 220;
	Layout.Arrange(vCards, 20);
	EXPECT_FLOAT_EQ(vCards[0].m_Rect.y, 140);
	EXPECT_FLOAT_EQ(vCards[1].m_Rect.y, 0);

	for(auto &Card : vCards)
		Card.m_ReferenceRect = Card.m_Rect;
	// 右卡收窄再展开时，两列的基准顶部相同，上一帧顺序不能被左列优先覆盖。
	for(bool FullWidth : {false, true})
	{
		SCOPED_TRACE(FullWidth);
		vCards[0].m_Rect.y = 0;
		vCards[1].m_FullWidth = FullWidth;
		vCards[1].m_Rect = {FullWidth ? 0.0f : 120.0f, 0, FullWidth ? 220.0f : 100.0f, 120};
		Layout.Arrange(vCards, 20);
		EXPECT_FLOAT_EQ(vCards[1].m_Rect.y, 0);
		EXPECT_FLOAT_EQ(vCards[0].m_Rect.y, 140);
		EXPECT_GE(vCards[0].m_Rect.y, vCards[1].m_Rect.y + vCards[1].m_Rect.h + 20);
		for(auto &Card : vCards)
			Card.m_ReferenceRect = Card.m_Rect;
	}
}

TEST(SettingsCardWidth, NarrowingBeforeAnotherExpandedCardKeepsItsHeaderInPlace)
{
	CSettingsCardWidthLayout Layout;
	std::vector<SSettingsCardWidthPlacement> vCards{
		{1, true, {0, 100, 220, 100}},
		{2, true, {0, 200, 220, 100}},
	};
	Layout.Arrange(vCards, 10);
	ASSERT_FLOAT_EQ(vCards[0].m_Rect.y, 100);
	ASSERT_FLOAT_EQ(vCards[1].m_Rect.y, 210);
	for(auto &Card : vCards)
	{
		Card.m_ReferenceRect = Card.m_Rect;
		Card.m_HasReference = true;
	}

	// 后一张卡按基准重排时仍是 200；判断前后关系必须使用已经呈现的 210。
	vCards[0].m_FullWidth = false;
	vCards[0].m_Rect.w = 100;
	vCards[1].m_Rect.y = 200;
	Layout.Arrange(vCards, 10);
	EXPECT_FLOAT_EQ(vCards[0].m_Rect.y, 100);
	EXPECT_FLOAT_EQ(vCards[1].m_Rect.y, 210);
	EXPECT_GE(vCards[1].m_Rect.y, vCards[0].m_Rect.y + vCards[0].m_Rect.h + 10);
}
