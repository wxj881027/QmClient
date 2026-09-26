#include <game/client/components/qmclient/demo_ui.h>

#include <gtest/gtest.h>

TEST(QmDemoUi, PlayerCentersAndDraggingStaysInsideScreen)
{
	const CUIRect Screen{40.0f, 20.0f, 1000.0f, 600.0f};
	const CUIRect Base = qm_demo_ui::PlayerRect(Screen, 70.0f);
	EXPECT_FLOAT_EQ(Base.x, 160.0f);
	EXPECT_FLOAT_EQ(Base.y, 538.0f);
	EXPECT_FLOAT_EQ(Base.w, 760.0f);
	EXPECT_FLOAT_EQ(Base.h, 70.0f);

	const CUIRect BottomRight = qm_demo_ui::DraggedPlayerRect(Screen, Base, 1000.0f, 1000.0f);
	EXPECT_FLOAT_EQ(BottomRight.x, 280.0f);
	EXPECT_FLOAT_EQ(BottomRight.y, 550.0f);
	const CUIRect TopLeft = qm_demo_ui::DraggedPlayerRect(Screen, Base, -1000.0f, -1000.0f);
	EXPECT_FLOAT_EQ(TopLeft.x, Screen.x);
	EXPECT_FLOAT_EQ(TopLeft.y, Screen.y);

	const CUIRect Narrow = qm_demo_ui::PlayerRect({0.0f, 0.0f, 400.0f, 240.0f}, 70.0f);
	EXPECT_FLOAT_EQ(Narrow.x, 12.0f);
	EXPECT_FLOAT_EQ(Narrow.w, 376.0f);
	const CUIRect Tiny{0.0f, 0.0f, 100.0f, 40.0f};
	EXPECT_FLOAT_EQ(qm_demo_ui::DraggedPlayerRect(Tiny, qm_demo_ui::PlayerRect(Tiny, 70.0f), 0.0f, 100.0f).y, 0.0f);
}

TEST(QmDemoUi, SliceContentAccountsForFourVisibleSegmentsAndDisplayOptions)
{
	EXPECT_FLOAT_EQ(qm_demo_ui::SliceContentHeight(0, false), 154.0f);
	EXPECT_FLOAT_EQ(qm_demo_ui::SliceContentHeight(1, false), 204.0f);
	EXPECT_FLOAT_EQ(qm_demo_ui::SliceContentHeight(4, false), 264.0f);
	EXPECT_FLOAT_EQ(qm_demo_ui::SliceContentHeight(5, false), 280.0f);
	EXPECT_FLOAT_EQ(qm_demo_ui::SliceContentHeight(5, true), 400.0f);
	EXPECT_FLOAT_EQ(qm_demo_ui::RenderContentHeight(false, false), 120.0f);
	EXPECT_FLOAT_EQ(qm_demo_ui::RenderContentHeight(true, true), 270.0f);
}

TEST(QmDemoUi, PopupFitsTheViewportAndKeepsContentHeightWhenPossible)
{
	const CUIRect Screen{40.0f, 20.0f, 1000.0f, 600.0f};
	const CUIRect Full = qm_demo_ui::PopupRect(Screen, 516.0f);
	EXPECT_FLOAT_EQ(Full.x, 260.0f);
	EXPECT_FLOAT_EQ(Full.y, 62.0f);
	EXPECT_FLOAT_EQ(Full.w, 560.0f);
	EXPECT_FLOAT_EQ(Full.h, 516.0f);

	const CUIRect Narrow = qm_demo_ui::PopupRect({0.0f, 0.0f, 400.0f, 240.0f}, 516.0f);
	EXPECT_FLOAT_EQ(Narrow.x, 12.0f);
	EXPECT_FLOAT_EQ(Narrow.y, 12.0f);
	EXPECT_FLOAT_EQ(Narrow.w, 376.0f);
	EXPECT_FLOAT_EQ(Narrow.h, 216.0f);
}
