#include <game/client/QmUi/UiNavigation.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>

TEST(QmNewUiMenuBranches, CapsuleTabBarRowRectSpansSlotsAndGaps)
{
	// 意图：胶囊容器覆盖整排 Tab（含 Tab 之间的间隙），而不是只包住第一个槽位。
	const CUIRect aSlots[] = {
		{10.0f, 4.0f, 60.0f, 20.0f},
		{74.0f, 4.0f, 60.0f, 20.0f},
		{138.0f, 4.0f, 100.0f, 20.0f},
	};
	const CUIRect Row = ui_widget::CapsuleTabBarRowRect(aSlots, 3);
	EXPECT_FLOAT_EQ(Row.x, 10.0f);
	EXPECT_FLOAT_EQ(Row.y, 4.0f);
	EXPECT_FLOAT_EQ(Row.w, 228.0f);
	EXPECT_FLOAT_EQ(Row.h, 20.0f);

	EXPECT_FLOAT_EQ(ui_widget::CapsuleTabBarRowRect(aSlots, 1).w, 60.0f);
	EXPECT_FLOAT_EQ(ui_widget::CapsuleTabBarRowRect(nullptr, 3).w, 0.0f);
	EXPECT_FLOAT_EQ(ui_widget::CapsuleTabBarRowRect(aSlots, 0).h, 0.0f);
}

TEST(QmNewUiMenuBranches, CapsuleTabSlotHitTestKeepsSlotsSeparateAndToleratesVerticalSlip)
{
	// 意图：胶囊 Tab 的命中判定按槽位横向半开区间（相邻页签不互相抢点击），
	// 纵向给少量容差，这样按在胶囊上下边缘或按下后轻微移动不会落空。
	const CUIRect aSlots[] = {
		{0.0f, 100.0f, 60.0f, 26.0f},
		{60.0f, 100.0f, 60.0f, 26.0f},
		{120.0f, 100.0f, 60.0f, 26.0f},
	};

	EXPECT_EQ(ui_widget::CapsuleTabBarSlotAtPoint(aSlots, 3, 30.0f, 113.0f), 0);
	EXPECT_EQ(ui_widget::CapsuleTabBarSlotAtPoint(aSlots, 3, 90.0f, 113.0f), 1);
	EXPECT_EQ(ui_widget::CapsuleTabBarSlotAtPoint(aSlots, 3, 150.0f, 113.0f), 2);
	// 槽位边界属于右侧槽位，与 CUIRect::Inside 的半开区间一致。
	EXPECT_EQ(ui_widget::CapsuleTabBarSlotAtPoint(aSlots, 3, 60.0f, 113.0f), 1);

	const float Top = 100.0f;
	const float Bottom = 100.0f + 26.0f;
	const float Slop = ui_widget::CAPSULE_TAB_HIT_SLOP;
	EXPECT_GT(Slop, 0.0f);
	EXPECT_EQ(ui_widget::CapsuleTabBarSlotAtPoint(aSlots, 3, 30.0f, Top - Slop + 0.5f), 0);
	EXPECT_EQ(ui_widget::CapsuleTabBarSlotAtPoint(aSlots, 3, 30.0f, Bottom + Slop - 0.5f), 0);
	EXPECT_EQ(ui_widget::CapsuleTabBarSlotAtPoint(aSlots, 3, 30.0f, Top - Slop - 1.0f), -1);
	EXPECT_EQ(ui_widget::CapsuleTabBarSlotAtPoint(aSlots, 3, 30.0f, Bottom + Slop + 1.0f), -1);
	// 横向不外扩：容差只作用于纵向。
	EXPECT_EQ(ui_widget::CapsuleTabBarSlotAtPoint(aSlots, 3, -1.0f, 113.0f), -1);
	EXPECT_EQ(ui_widget::CapsuleTabBarSlotAtPoint(aSlots, 3, 180.0f, 113.0f), -1);
	EXPECT_EQ(ui_widget::CapsuleTabBarSlotAtPoint(aSlots, 3, 30.0f, 113.0f, 0.0f), 0);
	EXPECT_EQ(ui_widget::CapsuleTabBarSlotAtPoint(aSlots, 3, 30.0f, Top - 1.0f, 0.0f), -1);
	EXPECT_EQ(ui_widget::CapsuleTabBarSlotAtPoint(nullptr, 3, 30.0f, 113.0f), -1);
	EXPECT_EQ(ui_widget::CapsuleTabBarSlotAtPoint(aSlots, 0, 30.0f, 113.0f), -1);
}
